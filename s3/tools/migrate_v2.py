#!/usr/bin/env python3
"""Switch an EspOS S3 board to partition table v2 over USB, or flash a new unit for Kho máy (docs/partitions.md).

  python3 tools/migrate_v2.py                        the boards it would accept (lists USB data, opens no port)
  python3 tools/migrate_v2.py --serial MAC           the plan for that board: nothing is read or written
  python3 tools/migrate_v2.py --serial MAC --write   back up, write table v2 + bootloader + app, check what was kept
  python3 tools/migrate_v2.py --serial MAC --write --factory   a new unit: the whole flash erased first
  python3 tools/migrate_v2.py --image FLASH.bin [--write]      the same steps on a 16 MB flash image (QEMU tests)

MAC is the board's USB serial number (XX:XX:XX:XX:XX:XX). Build first: pio run -e s3desk-v2 (or --build DIR).

The switch (a board on table v1 or v2):
  written  0x0 bootloader, 0x8000 table v2, 0xE000 otadata (boot_app0: the next boot is app0), 0x10000 the app
  erased   0x4D0000-0xEBE000: app1 and the app area "apps" (both hold v1 bytes that mean nothing under v2; a board
           already on v2 keeps its app area: only the app and the bootloader are written)
  kept     nvs 0x9000-0xE000 (link token, WiFi, the package index, saves, the claim), LittleFS 0xEBE000-0xFF0000
           (settings, dashboard, photos, small apps), coredump. The firmware drops the package index's area records
           at the first boot under v2 (src/pkg/area.cpp checkGeometry); the store installs what the web lists again.
Before writing, nvs, otadata, LittleFS and the old table are read into ../backup/migrate-v2/<MAC>-<time>/; after
writing, nvs and LittleFS are read back and must match the backup byte for byte.
--factory (Kho máy, new units): the whole flash erased, then the same four parts; refused on a board that already
has an EspOS table unless --wipe is given too (a returned unit: ESPOS:FACTORY_RESET over USB is the gentle way).

Ports: only Espressif USB serial/JTAG ports (303a:1001) whose USB serial number is the --serial asked; never a port on
the deny list (ESPOS_DENY_PORTS and
~/.config/espos/config.json "deny_ports"), not even with --port. esptool also refuses a chip that is not an ESP32-S3
(the X4 reader shares the USB ids) and the script refuses a flash that is not 16 MB.
"""
import argparse
import hashlib
import json
import os
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PY = Path.home() / ".platformio/penv/bin/python"
BOOT_APP0 = Path.home() / ".platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"
DENY: list = []  # ports never opened: ESPOS_DENY_PORTS
FLASH = 16 << 20
CHUNK = 1 << 20  # big reads fail over this USB: 1 MB at a time


# ---------------------------------------------------------------- partition tables
def read_csv(path):
    out = []
    for line in Path(path).read_text().splitlines():
        line = line.split("#")[0].strip()
        if not line:
            continue
        f = [x.strip() for x in line.split(",")]
        out.append((f[0], int(f[3], 0), int(f[4], 0)))
    return out


def parse_table(b):
    """[(name, type, subtype, offset, size)] from a binary table, None when it is not one."""
    out = []
    for i in range(0, len(b) - 31, 32):
        e = b[i : i + 32]
        if e[:2] == b"\xaa\x50":
            _, t, st, off, size, name, _ = struct.unpack("<2sBBLL16sL", e)
            out.append((name.rstrip(b"\0").decode(errors="replace"), t, st, off, size))
        elif e[:2] in (b"\xeb\xeb", b"\xff\xff"):
            break
        else:
            return None
    return out or None


def layout(entries):
    return [(n, off, size) for n, _, _, off, size in entries]


V1 = read_csv(ROOT / "partitions_pawboop.csv")
V2 = read_csv(ROOT / "partitions_v2.csv")
P2 = {n: (off, size) for n, off, size in V2}


def which(entries):
    if not entries:
        return "none"
    lay = layout(entries)
    return "v1" if lay == V1 else "v2" if lay == V2 else "other"


# ---------------------------------------------------------------- the parts
def parts(build):
    files = {"bootloader": build / "bootloader.bin", "table": build / "partitions.bin", "otadata": BOOT_APP0,
             "app": build / "firmware.bin"}
    for k, f in files.items():
        if not f.exists():
            sys.exit(f"missing {f} (build env s3desk-v2 first)")
    data = {k: f.read_bytes() for k, f in files.items()}
    if layout(parse_table(data["table"]) or []) != V2:
        sys.exit(f"{files['table']} is not partitions_v2.csv (build env s3desk-v2)")
    app = data["app"]
    if app[0] != 0xE9 or struct.unpack_from("<H", app, 12)[0] != 9:
        sys.exit(f"{files['app']} is not an ESP32-S3 app image")
    if len(app) > P2["app0"][1]:
        sys.exit(f"the app is {len(app)} bytes, the v2 slot {P2['app0'][1]}")
    print(f"app {len(app):,} B (v2 slot {P2['app0'][1]:,} B, {P2['app0'][1] - len(app):,} B left; "
          f"v1 slot 3,932,160 B: {'fits' if len(app) <= 3932160 else 'does NOT fit'})")
    return data


def plan(current, factory):
    """The steps: ("erase", off, len) and ("write", off, part)."""
    steps = []
    if factory:
        steps.append(("erase", 0, FLASH))
    elif current == "v1":
        app1, apps = P2["app1"], P2["apps"]
        steps.append(("erase", app1[0], apps[0] + apps[1] - app1[0]))
    steps += [("write", 0x0, "bootloader"), ("write", 0x8000, "table"), ("write", P2["otadata"][0], "otadata"),
              ("write", P2["app0"][0], "app")]
    return steps


KEEP = [("nvs", P2["nvs"]), ("spiffs", P2["spiffs"])]
BACKUP = [("table", (0x8000, 0x1000)), ("nvs", P2["nvs"]), ("otadata", P2["otadata"]), ("spiffs", P2["spiffs"])]


# ---------------------------------------------------------------- a flash image file (QEMU)
class Image:
    def __init__(self, path):
        self.path = Path(path)
        self.b = bytearray(self.path.read_bytes())
        if len(self.b) != FLASH:
            sys.exit(f"{path}: {len(self.b)} bytes, not a 16 MB flash image")

    def read(self, off, n):
        return bytes(self.b[off : off + n])

    def erase(self, off, n):
        self.b[off : off + n] = b"\xff" * n

    def write_all(self, writes):
        for off, data in writes:
            n = (len(data) + 4095) & ~4095  # esptool erases whole sectors
            self.b[off : off + n] = data + b"\xff" * (n - len(data))
        self.path.write_bytes(self.b)

    def finish(self):
        self.path.write_bytes(self.b)


# ---------------------------------------------------------------- a board (esptool)
class Board:
    def __init__(self, port, mac):
        self.port, self.mac = port, mac

    def tool(self, *args, after="no-reset", capture=False):
        cmd = [str(PY), "-m", "esptool", "--chip", "esp32s3", "--port", self.port, "--baud", "921600",
               "--before", "default-reset", "--after", after, *args]
        print("  $", " ".join(cmd[2:]))
        r = subprocess.run(cmd, capture_output=capture, text=True)
        if r.returncode:
            sys.exit(f"esptool failed ({r.returncode})" + (f":\n{r.stdout}{r.stderr}" if capture else ""))
        return r.stdout if capture else ""

    def check(self):
        out = self.tool("flash-id", capture=True)
        mac = next((l.split()[-1] for l in out.splitlines() if l.strip().startswith("MAC:")), "")
        size = next((l.split(":")[-1].strip() for l in out.splitlines() if "flash size" in l.lower()), "")
        if mac.lower() != self.mac.lower():
            sys.exit(f"the chip's MAC is {mac or '?'}, not {self.mac}: stopped")
        if not size.startswith("16MB"):
            sys.exit(f"flash size {size or '?'}, not 16MB: stopped")
        print(f"  ESP32-S3 {mac}, flash {size}")

    def read(self, off, n):
        out = bytearray()
        with tempfile.TemporaryDirectory() as t:
            for at in range(off, off + n, CHUNK):
                k = min(CHUNK, off + n - at)
                f = Path(t) / "r.bin"
                self.tool("read-flash", hex(at), hex(k), str(f))
                out += f.read_bytes()
        return bytes(out)

    def erase(self, off, n):
        if off == 0 and n == FLASH:
            self.tool("erase-flash")
        else:
            self.tool("erase-region", hex(off), hex(n))

    def write_all(self, writes):
        with tempfile.TemporaryDirectory() as t:
            args = []
            for i, (off, data) in enumerate(writes):
                f = Path(t) / f"p{i}.bin"
                f.write_bytes(data)
                args += [hex(off), str(f)]
            self.tool("write-flash", "--flash-size", "keep", *args)

    def finish(self):
        self.tool("chip-id", after="hard-reset")


def find_port(serial):
    try:
        from serial.tools import list_ports
    except ImportError:  # PlatformIO's Python has pyserial and esptool
        if PY.exists() and Path(sys.executable).resolve() != PY.resolve():
            os.execv(str(PY), [str(PY), __file__, *sys.argv[1:]])
        sys.exit(f"pyserial missing: run with {PY}")
    deny = DENY + [p for p in os.environ.get("ESPOS_DENY_PORTS", "").replace(",", " ").split() if p]
    try:
        deny += json.loads((Path(os.environ.get("ESPOS_CONFIG_DIR") or Path.home() / ".config/espos") / "config.json")
                           .read_text()).get("deny_ports", [])
    except (OSError, ValueError):
        pass
    key = lambda p: os.path.basename(p).replace("cu.", "").replace("tty.", "")
    deny = {key(d) for d in deny}
    ok = []
    for p in list_ports.comports():
        if not p.device.startswith("/dev/cu.") and sys.platform == "darwin":
            continue
        why = ("on the deny list" if key(p.device) in deny else
               "not an Espressif USB serial/JTAG port" if (p.vid, p.pid) != (0x303A, 0x1001) else "")
        print(f"  {p.device:32} {p.serial_number or '-':20} {why or 'ESP32 USB (S3 or X4)'}")
        if not why:
            ok.append(p)
    if not serial:
        return None
    hit = [p for p in ok if (p.serial_number or "").lower() == serial.lower()]
    if len(hit) != 1:
        sys.exit(f"no accepted port has the USB serial {serial}: stopped")
    return hit[0].device


def main():
    a = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    a.add_argument("--serial", help="the board's USB serial number (its MAC)")
    a.add_argument("--image", help="a 16 MB flash image instead of a board")
    a.add_argument("--build", default=os.environ.get("BUILD_DIR") or str(ROOT / ".pio/build/s3desk-v2"))
    a.add_argument("--write", action="store_true", help="do it (without: the plan only)")
    a.add_argument("--factory", action="store_true", help="a new unit: erase the whole flash first")
    a.add_argument("--wipe", action="store_true", help="with --factory: also a board that has an EspOS table")
    a.add_argument("--backup", default=str(ROOT.parent / "backup/migrate-v2"))
    o = a.parse_args()

    print("ports (USB data only, none opened):")
    port = None if o.image else find_port(o.serial)
    if not o.image and not port:
        print("give --serial MAC to plan for one of the accepted boards")
        return
    data = parts(Path(o.build))
    dev = Image(o.image) if o.image else Board(port, o.serial)
    name = o.image or f"{port} ({o.serial})"
    if not o.write:
        print(f"\nplan for {name} (dry run: nothing read or written; the table is read when --write runs)")
        for kind in ("v1", "v2"):
            print(f"  if the board is on {kind}:")
            for s in plan(kind, o.factory):
                print(f"    {s[0]:5} 0x{s[1]:06x} " + (f"{s[2]:,} B" if s[0] == "erase" else f"{s[2]} ({len(data[s[2]]):,} B)"))
        print("  kept: " + ("nothing (--factory)" if o.factory else ", ".join(f"{n} 0x{off:x}+0x{n2:x}" for n, (off, n2) in KEEP)))
        return

    print(f"\n== {name}")
    if isinstance(dev, Board):
        dev.check()
    current = which(parse_table(dev.read(0x8000, 0x1000)))
    print(f"  table now: {current}")
    if o.factory and current in ("v1", "v2") and not o.wipe:
        sys.exit("this board has an EspOS table (a unit in use?): --factory needs --wipe too")
    if not o.factory and current not in ("v1", "v2"):
        sys.exit(f"table {current}: LittleFS may not be where v2 expects it; use --factory for a new unit")
    saved = {}
    if not o.factory:
        out = Path(o.backup) / f"{(o.serial or Path(o.image).stem).replace(':', '')}-{time.strftime('%Y%m%d-%H%M%S')}"
        out.mkdir(parents=True, exist_ok=True)
        for n, (off, size) in BACKUP:
            saved[n] = dev.read(off, size)
            (out / f"{n}-0x{off:x}.bin").write_bytes(saved[n])
        (out / "info.json").write_text(json.dumps({"table": current, "port": port, "serial": o.serial,
                                                   "sha256": {n: hashlib.sha256(b).hexdigest() for n, b in saved.items()}},
                                                  indent=2))
        print(f"  backup: {out}")
    writes = []
    for s in plan(current, o.factory):
        if s[0] == "erase":
            print(f"  erase 0x{s[1]:06x} +0x{s[2]:x}")
            dev.erase(s[1], s[2])
        else:
            writes.append((s[1], data[s[2]]))
    print("  write " + ", ".join(f"0x{off:x}" for off, _ in writes))
    dev.write_all(writes)
    now = which(parse_table(dev.read(0x8000, 0x1000)))
    if now != "v2":
        sys.exit(f"the table reads back as {now}: the board needs a look before it restarts")
    for n, (off, size) in KEEP if saved else []:
        back = dev.read(off, size)
        if back != saved[n]:
            sys.exit(f"{n} changed (0x{off:x}): restore it from the backup before anything else")
        print(f"  {n} kept ({size:,} B identical)")
    dev.finish()
    print("done: table v2, the new app in app0" + ("" if o.factory else ", data kept"))


if __name__ == "__main__":
    main()
