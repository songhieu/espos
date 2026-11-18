"""Copy the built S3 firmware into a flash bundle for the web installer and OTA.

Run from s3/ after `pio run`: python3 tools/make_bundle.py [out_dir]  (default ../fw-bundle-s3)
Then upload it with ../scripts/upload_fw.py (prefix fw-s3 comes from the manifest).
"""
import hashlib
import os
import json
import re
import shutil
import sys
from pathlib import Path

root = Path(__file__).resolve().parent.parent
out = Path(sys.argv[1]) if len(sys.argv) > 1 else root.parent / "fw-bundle-s3"
# BUILD_DIR: a PLATFORMIO_BUILD_DIR build (e.g. .pio/build-lead/s3desk); default .pio/build/<env>
build = Path(os.environ["BUILD_DIR"]) if os.environ.get("BUILD_DIR") else root / ".pio/build" / os.environ.get("PIO_ENV", "s3desk")
version = re.search(r'FW_VERSION = "([^"]+)"', (root / "src/board.h").read_text()).group(1)
boot_app0 = Path.home() / ".platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"
parts = [("bootloader.bin", build / "bootloader.bin", 0x0), ("partitions.bin", build / "partitions.bin", 0x8000),
         ("boot_app0.bin", boot_app0, 0xE000), ("firmware.bin", build / "firmware.bin", 0x10000)]
# PawBoop's packs go to their own partitions (partitions_pawboop.csv): the web installer writes them
# once with everything else. OTA updates only ever carry firmware.bin.
table = {}
for line in (root / "partitions_pawboop.csv").read_text().splitlines():
    f = [x.strip() for x in line.split(",")]
    if len(f) >= 5 and not line.startswith("#"):
        table[f[0]] = int(f[3], 16)
packs = root / "sim/data/pawboop"
for name, part in (("biscuit_M.pbs", "pb_dog"), ("pawboop_ui.pbu", "pb_ui"), ("biscuit_voice_12k.pba", "pb_voice")):
    if (packs / name).exists():
        parts.append((name.replace(".", "_") + ".bin", packs / name, table[part]))
out.mkdir(parents=True, exist_ok=True)
man = {"version": version, "chip": "esp32s3", "flash_size": "16MB", "prefix": "fw-s3", "parts": []}
for name, src, offset in parts:
    data = src.read_bytes()
    shutil.copyfile(src, out / name)
    man["parts"].append({"file": name, "offset": offset, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()})
(out / "manifest.json").write_text(json.dumps(man, indent=2))
print(version, "->", out)
for p in man["parts"]:
    print(f"  {p['file']:15} {p['size']:>8} B @ 0x{p['offset']:x}")
