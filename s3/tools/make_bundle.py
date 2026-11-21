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
# The bundle carries the build's partition table (docs/partitions.md): env s3desk = v1, env s3desk-v2 = v2.
# Table v1 only: PawBoop's packs go to their own partitions (pb_*), written once by the web installer with
# everything else. Table v2 has no pb_* partitions (PawBoop comes from the store, on the card). OTA updates only
# ever carry firmware.bin.
table = {}
tb = (build / "partitions.bin").read_bytes()
for i in range(0, len(tb) - 31, 32):
    if tb[i : i + 2] != b"\xaa\x50":
        break
    table[tb[i + 12 : i + 28].rstrip(b"\0").decode()] = int.from_bytes(tb[i + 4 : i + 8], "little")
packs = root / "sim/data/pawboop"
for name, part in (("biscuit_M.pbs", "pb_dog"), ("pawboop_ui.pbu", "pb_ui"), ("biscuit_voice_12k.pba", "pb_voice")):
    if part in table and (packs / name).exists():
        parts.append((name.replace(".", "_") + ".bin", packs / name, table[part]))
out.mkdir(parents=True, exist_ok=True)
man = {"version": version, "chip": "esp32s3", "flash_size": "16MB", "prefix": "fw-s3", "parts": [],
       "ptab": 2 if "apps" in table else 1}
for name, src, offset in parts:
    data = src.read_bytes()
    shutil.copyfile(src, out / name)
    man["parts"].append({"file": name, "offset": offset, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()})
(out / "manifest.json").write_text(json.dumps(man, indent=2))
print(version, "->", out)
for p in man["parts"]:
    print(f"  {p['file']:15} {p['size']:>8} B @ 0x{p['offset']:x}")
