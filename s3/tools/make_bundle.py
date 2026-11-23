"""Copy the built S3 firmware into a flash bundle for the web installer and OTA, or a product's delivery bundle.

Run from s3/ after `pio run`:
  python3 tools/make_bundle.py [out_dir]          (default ../fw-bundle-s3)
Then upload it with ../scripts/upload_fw.py (prefix fw-s3 comes from the manifest).

A product's delivery bundle (docs/app-slots.md §17.4), for the factory and Kho máy (a blank unit; it writes LittleFS):
  python3 tools/make_bundle.py --preset sdk/presets/clock-desk.json --board es3c35p [--out DIR]
      [--catalog build-pkgs/espos/s3/catalog.json] [--pkgs build-pkgs/espos/s3] [--saver-templates FILE]
writes DIR (default ../fw-preset-<id>-<board>):
  the firmware parts as above (never PawBoop 1.0's pb_* packs), littlefs.bin with /sys/preset.json (the compiled
  preset: `espos preset compile`), preset.json (the same), card/ (to copy onto the unit's microSD card: the preset's
  packages as the device keeps them, espos/preset.json), manifest.json (offsets, sizes, SHA-256, the preset, the card's
  files) and SHA256SUMS. tools/qemu/run.sh --bundle DIR boots it in QEMU.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parent.parent


def firmware_parts(build, with_packs):
    boot_app0 = Path.home() / ".platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"
    parts = [("bootloader.bin", build / "bootloader.bin", 0x0), ("partitions.bin", build / "partitions.bin", 0x8000),
             ("boot_app0.bin", boot_app0, 0xE000), ("firmware.bin", build / "firmware.bin", 0x10000)]
    # The bundle carries the build's partition table (docs/partitions.md): env s3desk = v1, env s3desk-v2 = v2.
    # Table v1 only: PawBoop's packs go to their own partitions (pb_*), written once by the web installer with
    # everything else. Table v2 has no pb_* partitions (PawBoop comes from the store, on the card). OTA updates only
    # ever carry firmware.bin. A product's bundle never carries them: the preset says what the unit comes with.
    table = {}
    tb = (build / "partitions.bin").read_bytes()
    for i in range(0, len(tb) - 31, 32):
        if tb[i : i + 2] != b"\xaa\x50":
            break
        table[tb[i + 12 : i + 28].rstrip(b"\0").decode()] = (int.from_bytes(tb[i + 4 : i + 8], "little"),
                                                               int.from_bytes(tb[i + 8 : i + 12], "little"))
    packs = root / "sim/data/pawboop"
    for name, part in (("biscuit_M.pbs", "pb_dog"), ("pawboop_ui.pbu", "pb_ui"), ("biscuit_voice_12k.pba", "pb_voice")):
        if with_packs and part in table and (packs / name).exists():
            parts.append((name.replace(".", "_") + ".bin", packs / name, table[part][0]))
    return parts, table


def sha(b):
    return hashlib.sha256(b).hexdigest()


def write_parts(out, parts, man):
    for name, src, offset in parts:
        data = src.read_bytes() if isinstance(src, Path) else src
        (out / name).write_bytes(data)
        man["parts"].append({"file": name, "offset": offset, "size": len(data), "sha256": sha(data)})


def plain(out_arg):
    out = Path(out_arg) if out_arg else root.parent / "fw-bundle-s3"
    # BUILD_DIR: a PLATFORMIO_BUILD_DIR build (e.g. .pio/build-lead/s3desk); default .pio/build/<env>
    build = Path(os.environ["BUILD_DIR"]) if os.environ.get("BUILD_DIR") else root / ".pio/build" / os.environ.get("PIO_ENV", "s3desk")
    version = re.search(r'FW_VERSION = "([^"]+)"', (root / "src/board.h").read_text()).group(1)
    parts, table = firmware_parts(build, True)
    out.mkdir(parents=True, exist_ok=True)
    man = {"version": version, "chip": "esp32s3", "flash_size": "16MB", "prefix": "fw-s3", "parts": [],
           "ptab": 2 if "apps" in table else 1}
    write_parts(out, parts, man)
    (out / "manifest.json").write_text(json.dumps(man, indent=2))
    print(version, "->", out)
    for p in man["parts"]:
        print(f"  {p['file']:15} {p['size']:>8} B @ 0x{p['offset']:x}")


def espos_cli():
    cli = root / "sdk/packages/cli/bin/espos.js"
    if not (root / "sdk/packages/cli/dist/main.js").exists():
        raise SystemExit("build the CLI first: cd sdk && npm run build")
    return ["node", str(cli)]


def product(o):
    build = Path(os.environ["BUILD_DIR"]) if os.environ.get("BUILD_DIR") else root / ".pio/build" / os.environ.get("PIO_ENV", "s3desk")
    version = re.search(r'FW_VERSION = "([^"]+)"', (root / "src/board.h").read_text()).group(1)
    catalog = Path(o.catalog or root / "build-pkgs/espos/s3/catalog.json")
    pkgdir = Path(o.pkgs or root / "build-pkgs/espos/s3")
    templates = Path(o.saver_templates or root.parent / "web/app/src/os/saver-templates.json")
    # The open checker and compiler (sdk/packages/cli/src/preset.ts): the same rules as `espos preset check`.
    args = espos_cli() + ["preset", "compile", o.preset, "--board", o.board, "--catalog", str(catalog), "--json"]
    if templates.exists():
        args += ["--saver-templates", str(templates)]
    r = subprocess.run(args, capture_output=True, text=True)
    if r.returncode:
        raise SystemExit(f"espos preset compile failed:\n{r.stdout}{r.stderr}")
    dev = json.loads(r.stdout)["device"]
    out = Path(o.out) if o.out else root.parent / f"fw-preset-{dev['id']}-{o.board}"
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    dev_text = json.dumps(dev, ensure_ascii=False, indent=1) + "\n"
    (out / "preset.json").write_text(dev_text)

    # The card: each package of the preset as the device keeps it on a card (tools/espk.py --install-sim --vol sd).
    cat = {e["id"]: e for e in json.loads(catalog.read_text())["packages"]}
    files = []
    for p in dev.get("packages", []):
        e = cat.get(p["id"])
        if not e:
            raise SystemExit(f"{p['id']}: not in {catalog}")
        for part in p["parts"]:
            cp = next((x for x in e["parts"] if x["id"] == part), None)
            if not cp:
                raise SystemExit(f"{p['id']}: no part {part} in the catalog")
            f = pkgdir / Path(cp.get("url", f"{p['id']}-{e['version']}-{part}.espk")).name
            if not f.exists():
                raise SystemExit(f"{p['id']} part {part}: {f} missing (build or download the package first)")
            if cp.get("sha256") and sha(f.read_bytes()) != cp["sha256"]:
                raise SystemExit(f"{f}: not the catalog's file (SHA-256 differs)")
            files.append(str(f))
    with tempfile.TemporaryDirectory() as t:
        if files:
            subprocess.run([sys.executable, str(root / "tools/espk.py"), "--install-sim", t, *files, "--vol", "sd"],
                           check=True, capture_output=True)
        card = out / "card"
        if (Path(t) / "sd").exists():
            shutil.copytree(Path(t) / "sd", card)
        (card / "espos").mkdir(parents=True, exist_ok=True)
        (card / "espos/preset.json").write_text(dev_text)

    # LittleFS with the preset (the unit's own copy: it stays when the card is changed).
    sys.path.insert(0, str(root / "tools/qemu"))
    import mkimage  # noqa: E402 (the same LittleFS builder as the QEMU image)

    parts, table = firmware_parts(build, False)
    with tempfile.TemporaryDirectory() as t:
        (Path(t) / "fs/sys").mkdir(parents=True)
        (Path(t) / "fs/sys/preset.json").write_text(dev_text)
        off, size = table["spiffs"]
        parts.append(("littlefs.bin", mkimage.littlefs(Path(t), size), off))
    man = {"version": version, "chip": "esp32s3", "flash_size": "16MB", "prefix": "fw-s3", "parts": [],
           "ptab": 2 if "apps" in table else 1, "factory": True,
           "preset": {"id": dev["id"], "version": dev["version"], "name": dev.get("name", ""), "board": o.board,
                      "packages": [p["id"] for p in dev.get("packages", [])]}}
    write_parts(out, parts, man)
    man["card"] = []
    for f in sorted((out / "card").rglob("*")):
        if f.is_file():
            b = f.read_bytes()
            man["card"].append({"path": str(f.relative_to(out / "card")), "size": len(b), "sha256": sha(b)})
    (out / "manifest.json").write_text(json.dumps(man, indent=2, ensure_ascii=False))
    sums = []
    for f in sorted(out.rglob("*")):
        if f.is_file() and f.name != "SHA256SUMS":
            sums.append(f"{sha(f.read_bytes())}  {f.relative_to(out)}")
    (out / "SHA256SUMS").write_text("\n".join(sums) + "\n")
    card_bytes = sum(c["size"] for c in man["card"])
    print(f"{dev['id']} v{dev['version']} for {o.board} (firmware {version}) -> {out}")
    for p in man["parts"]:
        print(f"  {p['file']:15} {p['size']:>8} B @ 0x{p['offset']:x}")
    print(f"  card/           {len(man['card'])} files, {card_bytes / 1048576:.1f} MB ({', '.join(man['preset']['packages']) or 'no packages'})")


def main():
    a = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    a.add_argument("out_dir", nargs="?", help="the plain bundle's folder (default ../fw-bundle-s3)")
    a.add_argument("--preset", help="a preset file: a product's delivery bundle")
    a.add_argument("--board", choices=["es3c35p", "es3c28p"], help="the board the bundle is for (with --preset)")
    a.add_argument("--out", help="the delivery bundle's folder")
    a.add_argument("--catalog", help="the store catalog (default build-pkgs/espos/s3/catalog.json)")
    a.add_argument("--pkgs", help="the folder with the packages' .espk files (default build-pkgs/espos/s3)")
    a.add_argument("--saver-templates", help="the saver templates (default ../web/app/src/os/saver-templates.json)")
    o = a.parse_args()
    if not o.preset:
        return plain(o.out_dir)
    if not o.board:
        raise SystemExit("--board es3c35p or es3c28p")
    product(o)


if __name__ == "__main__":
    main()
