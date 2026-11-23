#!/usr/bin/env python3
"""The 16 MB flash image QEMU boots (docs/simulator.md): the firmware (bootloader, partition table, app: PlatformIO's
firmware.factory.bin from offset 0), PawBoop's packs in their partitions (partitions_pawboop.csv), LittleFS (the
"spiffs" partition) made from a simulator data folder: its fs/ (packages, notifications), settings.json and dash.json
as the board keeps them; and the memory card as a FAT image (its sd/: build-qemu/sd.img).
  tools/qemu/mkimage.py OUT.bin [--env s3desk-qemu] [--data DIR]   (DIR default sim/data-live)
  tools/qemu/mkimage.py OUT.bin --app-only     a new firmware into an existing image, its data kept (as flashing
                                               the board over USB: NVS 0x9000-0x10000, LittleFS, the packs and the
                                               card stay)
  --table partitions_v2.csv                    partition table v2 (docs/partitions.md): written at 0x8000 over the
                                               build's; no PawBoop packs (v2 has no pb_* partitions). To switch an
                                               image that has data from v1 to v2, use tools/migrate_v2.py --image.
  --build DIR                                  the firmware from DIR (default .pio/build/<env>)
  --bundle DIR                                 a product's delivery bundle (tools/make_bundle.py --preset): its
                                               littlefs.bin and its card/ (with this env's firmware: QEMU's panel and
                                               network), no PawBoop 1.0 packs, nothing else: a unit off the line
"""
import argparse
import csv
import json
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKS = {"pb_dog": "biscuit_M.pbs", "pb_ui": "pawboop_ui.pbu", "pb_voice": "biscuit_voice_12k.pba"}


def partitions(path):
    out = {}
    for row in csv.reader(l for l in path.read_text().splitlines() if l.strip() and not l.lstrip().startswith("#")):
        row = [c.strip() for c in row]
        out[row[0]] = (int(row[3], 0), int(row[4], 0))
    return out


def main():
    a = argparse.ArgumentParser()
    a.add_argument("out")
    a.add_argument("--env", default="s3desk-qemu")
    a.add_argument("--data", default=str(ROOT / "sim/data-live"))
    a.add_argument("--app-only", action="store_true")
    a.add_argument("--sd", help="the card image (default: sd.img next to OUT)")
    a.add_argument("--table", default="partitions_pawboop.csv", help="the partition table (CSV in the project)")
    a.add_argument("--build", help="the build folder (default .pio/build/<env>)")
    a.add_argument("--bundle", help="a delivery bundle folder (make_bundle.py --preset)")
    o = a.parse_args()
    build = Path(o.build) if o.build else ROOT / ".pio/build" / o.env
    table = ROOT / o.table
    if o.app_only and Path(o.out).exists():
        img = bytearray(Path(o.out).read_bytes())
        fw = (build / "firmware.factory.bin").read_bytes()
        app0 = partitions(table)["app0"]
        if len(fw) > app0[0] + app0[1]:
            raise SystemExit("firmware larger than app0")
        # firmware.factory.bin runs from 0: bootloader, partition table, then (0xFF over nvs and otadata) the app.
        # Only the bootloader, the table and the app go in: nvs (the link token, WiFi, saves) and otadata stay.
        nvs = partitions(table)["nvs"][0]
        img[:nvs] = fw[:nvs]
        tableBin(img, table)
        img[app0[0] : len(fw)] = fw[app0[0] :]
        img[len(fw) : app0[0] + app0[1]] = b"\xff" * (app0[0] + app0[1] - len(fw))
        Path(o.out).write_bytes(img)
        print(f"{o.out}: new firmware ({len(fw)} bytes), data kept")
        return
    o.packs = str(Path(o.data) / "pawboop")
    img = bytearray(b"\xff" * (16 << 20))
    fw = (build / "firmware.factory.bin").read_bytes()
    img[: len(fw)] = fw
    tableBin(img, table)
    parts = partitions(table)
    if o.bundle:
        b = Path(o.bundle)
        lfs = (b / "littlefs.bin").read_bytes()
        off, size = parts["spiffs"]
        if len(lfs) != size:
            raise SystemExit(f"{b}/littlefs.bin: {len(lfs)} bytes, the partition has {size}")
        img[off : off + size] = lfs
        Path(o.out).write_bytes(img)
        print(f"{o.out}: firmware {len(fw)} bytes, the bundle's LittleFS")
        card(b / "card", Path(o.sd) if o.sd else Path(o.out).parent / "sd.img")
        return
    for name, fname in PACKS.items():
        if name not in parts:
            continue
        f = Path(o.packs) / fname
        off, size = parts[name]
        if not f.exists():
            print(f"{name}: {f} missing, left empty")
            continue
        b = f.read_bytes()
        if len(b) > size:
            raise SystemExit(f"{name}: {len(b)} bytes do not fit in {size}")
        img[off : off + len(b)] = b
        print(f"{name}: {fname} {len(b)} bytes at {off:#x}")
    off, size = parts["spiffs"]
    img[off : off + size] = littlefs(Path(o.data), size)
    Path(o.out).write_bytes(img)
    print(f"{o.out}: firmware {len(fw)} bytes")
    card(Path(o.data) / "sd", Path(o.sd) if o.sd else Path(o.out).parent / "sd.img")


def tableBin(img, table):
    """The binary of `table` at 0x8000 (the factory image carries the table the build was made with)."""
    gen = Path.home() / ".platformio/packages/framework-arduinoespressif32/tools/gen_esp32part.py"
    with tempfile.TemporaryDirectory() as t:
        out = Path(t) / "p.bin"
        subprocess.run(["python3", str(gen), "-q", str(table), str(out)], check=True)
        b = out.read_bytes()
    img[0x8000 : 0x8000 + len(b)] = b
    img[0x8000 + len(b) : 0x9000] = b"\xff" * (0x1000 - len(b))


def littlefs(data, size):
    lfs = ROOT / "managed_components/joltwallet__littlefs/src/littlefs"
    tool = ROOT / "build-qemu/mklfs"
    src = ROOT / "tools/qemu/mklfs.c"
    if not tool.exists() or tool.stat().st_mtime < src.stat().st_mtime:
        subprocess.run(["cc", "-O2", "-w", f"-I{lfs}", str(src), str(lfs / "lfs.c"), str(lfs / "lfs_util.c"), "-o", str(tool)],
                       check=True)
    with tempfile.TemporaryDirectory() as t:
        stage = Path(t) / "fs"
        if (data / "fs").exists():
            shutil.copytree(data / "fs", stage)
        else:
            stage.mkdir()
        if (data / "settings.json").exists():  # the simulator's plain map -> the board's {"rev", "v": strings, "d"}
            flat = json.loads((data / "settings.json").read_text())
            v = {k: x if isinstance(x, str) else json.dumps(x, ensure_ascii=False) for k, x in flat.items()}
            (stage / "settings.json").write_text(json.dumps({"rev": 0, "v": v, "d": []}, ensure_ascii=False))
        if (data / "dash.json").exists():
            shutil.copy(data / "dash.json", stage / "dash.json")
        out = Path(t) / "lfs.bin"
        r = subprocess.run([str(tool), str(stage), str(size), str(out)], check=True, capture_output=True, text=True)
        print("littlefs:", r.stdout.strip())
        return out.read_bytes()


def card(sd, out):
    """The memory card: 64 MB FAT32 with an MBR (QEMU's SD card wants a power-of-2 size)."""
    if not sd.exists():
        return
    base = out.with_suffix("")
    for f in (out, base.with_suffix(".cdr")):
        f.unlink(missing_ok=True)
    subprocess.run(["hdiutil", "create", "-quiet", "-srcfolder", str(sd), "-fs", "MS-DOS FAT32", "-format", "UDTO",
                    "-layout", "MBRSPUD", "-size", "64m", "-volname", "CARD", str(base)], check=True)
    cdr = base.with_suffix(".cdr")
    b = cdr.read_bytes()
    want = 64 << 20
    if len(b) > want:
        raise SystemExit(f"card image {len(b)} bytes > 64 MB")
    out.write_bytes(b + b"\0" * (want - len(b)))
    cdr.unlink()
    print(f"{out}: {sd} as a 64 MB card")


if __name__ == "__main__":
    main()
