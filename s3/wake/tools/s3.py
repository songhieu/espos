#!/usr/bin/env python3
"""The wake word and the dog assistant on an S3 board, over its USB console (one session: opening the port
resets the board, so every command of a run goes through the same session).

    ~/.platformio/penv/bin/python wake/tools/s3.py pkg build-pkgs/wake/dev.espos.wake.gau-1.0.0-core.espk
    ~/.platformio/penv/bin/python wake/tools/s3.py rec 20 out.wav        # 20 s of the board's mic -> WAV (+ wakecli)
    ~/.platformio/penv/bin/python wake/tools/s3.py cmd "WAKE" "ASK mấy giờ rồi" "WAIT 15" "TALK"

The board is found by its USB serial number (the S3's MAC); ports in ESPOS_DENY_PORTS are never opened.
"""
import json
import os
import re
import struct
import subprocess
import sys
import time
import zlib

import serial
from serial.tools import list_ports

SERIAL = os.environ.get("S3_SERIAL", "")
DENY = {"/dev/cu." + p for p in os.environ.get("ESPOS_DENY_PORTS", "").replace(",", " ").split()}
HERE = os.path.dirname(os.path.abspath(__file__))
WAKE = os.path.dirname(HERE)


def port():
    if not SERIAL:
        sys.exit("set S3_SERIAL to the board's USB serial number")
    for p in list_ports.comports():
        if p.device in DENY or not p.device.startswith("/dev/cu.usbmodem"):
            continue
        if (p.serial_number or "").upper() == SERIAL.upper():
            return p.device
    sys.exit(f"the S3 board ({SERIAL}) is not connected")


class Board:
    def __init__(self, boot_wait=12):
        self.s = serial.Serial(port(), 115200, timeout=0.2)
        self.buf = b""
        t0 = time.time()
        # The port opening resets the board: wait for the boot to settle (the first "heap ... frames" line).
        while time.time() - t0 < boot_wait:
            line = self.readline(1.0)
            if line and (b"frames in 10 s" in line or b"wake:" in line):
                break
        self.drain(1.0)

    def readline(self, timeout):
        t0 = time.time()
        while b"\n" not in self.buf and time.time() - t0 < timeout:
            self.buf += self.s.read(4096)
        if b"\n" in self.buf:
            line, self.buf = self.buf.split(b"\n", 1)
            sys.stderr.write("  < " + line.decode("utf-8", "replace") + "\n")
            return line
        return None

    def drain(self, secs):
        t0 = time.time()
        while time.time() - t0 < secs:
            self.readline(0.2)

    def send(self, line):
        sys.stderr.write("  > " + line + "\n")
        self.s.write((line + "\n").encode())

    def until(self, pattern, timeout):
        t0 = time.time()
        rx = re.compile(pattern.encode())
        while time.time() - t0 < timeout:
            line = self.readline(0.5)
            if line is not None and rx.search(line):
                return line
        return None

    def read_exact(self, n, timeout):
        t0 = time.time()
        while len(self.buf) < n and time.time() - t0 < timeout:
            self.buf += self.s.read(min(65536, n - len(self.buf) + 64))
        out, self.buf = self.buf[:n], self.buf[n:]
        return out


def espk_files(path):
    sys.path.insert(0, os.path.join(WAKE, "..", "tools"))
    import espk  # noqa: E402
    m, files, b = espk.read(path)
    return m, [(n, b[off:off + size], crc) for n, off, size, crc in files]


def put_pkg(bd, path):
    m, files = espk_files(path)
    pid = m["id"]
    man = dict(m)
    man.pop("part", None)
    man["parts"] = ["core"]
    man["files"] = [{"n": n, "s": len(d), "c": crc} for n, d, crc in files]
    allf = files + [("manifest.json", json.dumps(man, ensure_ascii=False).encode(), 0)]
    for n, d, _ in allf:
        bd.send(f"PKGPUT {pid} {n} {len(d)}")
        time.sleep(0.2)
        for o in range(0, len(d), 1024):
            bd.s.write(d[o:o + 1024])
            time.sleep(0.005)
        if not bd.until(r"PKGPUT (ok|failed|bad)", 15):
            sys.exit(f"no answer for {n}")
    bd.send("RESTART")


def rec(bd, secs, out):
    bd.send(f"WAKEREC {secs}")
    line = bd.until(r"^WAKEREC \d+$", secs + 15)
    if not line:
        sys.exit("no recording")
    n = int(line.split()[1])
    data = bd.read_exact(n, 30)
    open(out, "wb").write(data)
    print(f"{out}: {len(data)} bytes, {max(0, len(data) - 44) / 32000:.1f} s")
    cli = os.path.join(WAKE, "host", "wakecli")
    model = os.path.join(WAKE, "pkg", "dev.espos.wake.gau", "gau.ewm")
    for thr in ("0.98", "0.95", "0.9"):
        print(f"wakecli, gâu gâu threshold {thr}:")
        print(subprocess.run([cli, model, out, "2.0", thr], capture_output=True, text=True).stdout)


def main(a):
    if not a:
        sys.exit(__doc__)
    if a[0] == "port":
        return print(port())
    bd = Board()
    if a[0] == "pkg":
        put_pkg(bd, a[1])
        bd.until(r"wake:|frames in 10 s", 20)
    elif a[0] == "rec":
        rec(bd, int(a[1]), a[2])
    elif a[0] == "cmd":
        for c in a[1:]:
            if c.startswith("WAIT "):
                bd.drain(float(c[5:]))
            else:
                bd.send(c)
                bd.drain(1.5)
    bd.s.close()


if __name__ == "__main__":
    main(sys.argv[1:])
