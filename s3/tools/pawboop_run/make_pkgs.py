"""PawBoop Chạy (vn.pawboop.run) as EspOS packages (.espk, docs/ecosystem.md §2), from the game designer's handoff
folder (pawboop_run_handoff/pawboop_run: manifest.json, run.json, core/, roads10/, roads20/). Three parts:
  core     core/run_dog.pbs, core/run_things.pbs, core/run_ui.pbu, run.json, icon.jpg (always installed)
  roads10  roads10/road_<map>.pbv x3, roads10/link_<a>_<b>.pbv x3   (the roads at 10 pictures a second)
  roads20  the same at 20 pictures a second
  roads10s roads10s/road_<map>.pbv x3: roads10's maps without the links (the engine fades from map to map), small
           enough for the internal flash beside core.   Install ONE of roads10 / roads20 / roads10s.
The files are checked against the handoff's SHA-256 list and copied unchanged. icon.jpg (120x120 baseline JPEG)
is Biscuit's own picture from run_dog.pbs on the app's orange.

  python3 tools/pawboop_run/make_pkgs.py [--src DIR] [--out DIR] [--sim DIR [--roads 10|20]]
    --out   where the .espk files and catalog.json go (default build-pkgs/ next to src/, not uploaded)
    --sim   also install core + one roads part on the simulator's memory card (tools/espk.py --install-sim DIR)
"""
import argparse
import hashlib
import io
import json
import struct
import subprocess
import sys
import zlib
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/pawboop"))
from pbs import Pack  # noqa: E402  (the pet's PBS2 reader: a picture as RGBA with the soft edge)

SRC = Path("handoff/pawboop_run")
ID, VERSION, CODE = "vn.pawboop.run", "1.0.1", 10001
# An add-on of PawBoop (the pet game opens it: "Chạy lấy xu"; the coins buy her food). Home shows only PawBoop.
MANIFEST = {
    "id": ID, "name": "Chạy lấy xu", "addon_of": "vn.pawboop.pet", "version": VERSION, "code": CODE,
    "kind": "native", "engine": "run", "min_fw": 300, "category": "game", "author": "PawBoop",
    "desc": "Biscuit chạy qua chợ, công trường, rạp xiếc, lấy xu mua đồ ăn",
    "icon": {"bg": ["#ffb347", "#ff7a00"], "glyph": "play"}, "perms": ["sound", "notify"],
}
ICON_CLIP, ICON_FRAME = "jump", 7  # Biscuit high in a leap


def espk(manifest: dict, files: list) -> bytes:
    """files: [(name, bytes)] -> one .espk (header, manifest padded to 16, 64-byte table, 16-aligned data)."""
    m = json.dumps(manifest, ensure_ascii=False, separators=(",", ":")).encode()
    table_at = 16 + ((len(m) + 15) & ~15)
    off = table_at + 64 * len(files)
    head = bytearray(b"ESPK" + struct.pack("<HHII", 1, 0, len(m), len(files)) + m)
    head += b"\0" * (table_at - len(head))
    body = bytearray()
    for name, data in files:
        nb = name.encode()
        assert 0 < len(nb) <= 51 and not nb.startswith(b"/") and b".." not in nb, name
        off = (off + 15) & ~15
        head += nb.ljust(52, b"\0") + struct.pack("<III", off, len(data), zlib.crc32(data) & 0xFFFFFFFF)
        pad = off - (table_at + 64 * len(files)) - len(body)
        body += b"\0" * pad + data
        off += len(data)
    return bytes(head + body)


def icon(dog_pbs: Path) -> bytes:
    """120x120: the app's orange (top to bottom), Biscuit's picture filling it, as a baseline JPEG."""
    S = 4
    n = 120 * S
    top, bottom = (0xFF, 0xB3, 0x47), (0xFF, 0x7A, 0x00)
    bg = Image.new("RGB", (n, n))
    d = ImageDraw.Draw(bg)
    for y in range(n):
        t = y / (n - 1)
        d.line([(0, y), (n, y)], fill=tuple(int(a + (b - a) * t) for a, b in zip(top, bottom)))
    pic, _ = Pack(dog_pbs).picture(ICON_CLIP, ICON_FRAME)
    x0, y0, x1, y1 = pic.getbbox()
    side = int(max(x1 - x0, y1 - y0) * 1.12)
    cx, cy = (x0 + x1) // 2, (y0 + y1) // 2
    crop = pic.crop((cx - side // 2, cy - side // 2, cx + side // 2, cy + side // 2)).resize((n, n), Image.LANCZOS)
    out = bg.convert("RGBA")
    out.alpha_composite(crop)
    b = io.BytesIO()
    out.convert("RGB").resize((120, 120), Image.LANCZOS).save(b, "JPEG", quality=90, progressive=False,
                                                                optimize=True, subsampling=0)
    return b.getvalue()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", type=Path, default=SRC)
    ap.add_argument("--out", type=Path, default=ROOT / "build-pkgs")
    ap.add_argument("--sim", type=Path)
    ap.add_argument("--roads", choices=["10", "20", "10s"], default="10")
    a = ap.parse_args()
    src = a.src
    lead = json.loads((src / "manifest.json").read_text())
    parts = {"core": [], "roads10": [], "roads20": [], "roads10s": []}
    for f in lead["files"]:
        data = (src / f["path"]).read_bytes()
        if len(data) != f["bytes"] or hashlib.sha256(data).hexdigest() != f["sha256"]:
            sys.exit(f"{f['path']}: size or SHA-256 differs from the handoff's manifest.json")
        parts[f["part"]].append((f["path"], data))
        if f["part"] == "roads10" and "map" in f:  # the compact part: the same map files, no links
            parts["roads10s"].append(("roads10s/" + f["path"].split("/", 1)[1], data))
    ic = icon(src / "core/run_dog.pbs")
    parts["core"].append(("icon.jpg", ic))
    a.out.mkdir(parents=True, exist_ok=True)
    (a.out / f"{ID}-{VERSION}.jpg").write_bytes(ic)
    catalog = dict(MANIFEST, parts=[])
    # English labels for the store, with their Vietnamese (docs/packages.md §4); in the part's own manifest too, so a
    # catalog built again from the .espk files keeps the choice (tools/espk.py check_catalog).
    labels = {"roads10": "Light (10 pictures a second)", "roads20": "Smooth (20 pictures a second)",
              "roads10s": "Compact (3 maps, no gates)"}
    labels_vi = {"roads10": "Nhẹ (10 hình/giây)", "roads20": "Đẹp (20 hình/giây)", "roads10s": "Gọn (3 map, không cảnh nối)"}
    for part, files in parts.items():
        files.sort(key=lambda x: x[0])
        man = dict(MANIFEST, part=part) if part == "core" else {
            "id": ID, "version": VERSION, "code": CODE, "part": part, "group": "roads", "label": labels[part],
            "labels": {"vi": labels_vi[part]}, "default": part == "roads10"}
        blob = espk(man, files)
        name = f"{ID}-{VERSION}-{part}.espk"
        (a.out / name).write_bytes(blob)
        entry = {"id": part, "file": name, "size": len(blob), "sha256": hashlib.sha256(blob).hexdigest(),
                 "files": len(files), "bytes": sum(len(d) for _, d in files)}
        if part == "core":
            entry["req"] = True
        else:
            entry.update(group="roads", label=labels[part], labels={"vi": labels_vi[part]}, default=part == "roads10")
        catalog["parts"].append(entry)
        print(f"{name}: {len(files)} files, {len(blob):,} bytes")
    catalog["icon_file"] = f"{ID}-{VERSION}.jpg"
    (a.out / f"{ID}-{VERSION}-catalog.json").write_text(json.dumps(catalog, ensure_ascii=False, indent=1))
    print(f"catalog entry (without urls): {a.out / (ID + '-' + VERSION + '-catalog.json')}")
    if a.sim:
        parts_ = [str(a.out / f"{ID}-{VERSION}-{p}.espk") for p in ("core", "roads" + a.roads)]
        subprocess.run([sys.executable, str(ROOT / "tools/espk.py"), "--install-sim", str(a.sim)] + parts_, check=True)


if __name__ == "__main__":
    main()
