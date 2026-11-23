#!/usr/bin/env python3
"""Drives the firmware in QEMU for apps/sessions/test/qemu.sh: its own serial port and monitor socket (not
build-qemu's, so it runs beside anyone else's QEMU).

    qemu_drive.py SERIAL_PORT MONITOR_SOCK OUTDIR STEP ...
STEP: a dev console line ("APP dev.espos.sessions 900:s-3"), "sleep:SECONDS", "shot:NAME" (OUTDIR/NAME.png),
"until:TEXT" (waits up to 120 s for TEXT in the serial output). The serial output goes to OUTDIR/serial.log.
"""
import os
import socket
import sys
import time
from pathlib import Path


def main():
    port, mon, out = int(sys.argv[1]), sys.argv[2], Path(sys.argv[3])
    log = open(out / "serial.log", "a", encoding="utf-8", errors="replace")
    for _ in range(300):
        try:
            s = socket.create_connection(("127.0.0.1", port))
            break
        except OSError:
            time.sleep(0.1)
    else:
        sys.exit(f"no QEMU on port {port}")
    s.settimeout(0.05)
    seen = ""

    def pump(sec):
        nonlocal seen
        end = time.time() + sec
        while time.time() < end:
            try:
                b = s.recv(65536)
                if b:
                    t = b.decode("utf-8", "replace")
                    log.write(t)
                    log.flush()
                    seen = (seen + t)[-200000:]
            except socket.timeout:
                pass

    for step in sys.argv[4:]:
        if step.startswith("sleep:"):
            pump(float(step[6:]))
        elif step.startswith("until:"):
            want, end = step[6:], time.time() + 120
            while want not in seen and time.time() < end:
                pump(0.5)
            print(f"[{'found' if want in seen else 'NOT found'} {want!r}]")
        elif step.startswith("shot:"):
            ppm = (out / (step[5:] + ".ppm")).resolve()
            m = socket.socket(socket.AF_UNIX)
            here = os.getcwd()
            os.chdir(os.path.dirname(os.path.abspath(mon)))  # a unix socket path must be under 104 bytes
            m.connect(os.path.basename(mon))
            os.chdir(here)
            m.sendall(f"screendump {ppm}\n".encode())
            time.sleep(0.8)
            m.close()
            try:
                from PIL import Image
                Image.open(ppm).save(out / (step[5:] + ".png"))
                ppm.unlink()
            except Exception as e:  # noqa: BLE001
                print(f"[shot {step[5:]}: {e}]")
            print(f"[shot {step[5:]}]")
        else:
            s.sendall((step + "\n").encode())
            pump(0.4)
    pump(0.5)


if __name__ == "__main__":
    main()
