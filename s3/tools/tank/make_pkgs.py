#!/usr/bin/env python3
"""Hồ cá cảnh (engine "tank", src/tank, docs/tank.md): the store package from the hand-off (data/tank/handoff, a copy of
handoff_tank of 2026-11-19; checked against its manifest.json).

  python3 tools/tank/make_pkgs.py [--out DIR] [--sim DIR]

Parts (one package, vn.pawboop.tank):
  core   tank.json, the fish / things / thumbs packs as the hand-off made them, tank_ui.pbu (the machines' glyphs and
         the sinking / live food the page draws with canvas paths, drawn here from ref/ui.js glyph() and foodArt()),
         the Vietnamese Ubuntu text sizes the screens use (dev.espos.font.vi's files: every letter, whatever the OS
         language), icon.jpg
  bg35   the five looks for the 3.5" board (320 x 480 and 480 x 320), group "bg", the default
  bg28   the same at two thirds for the 2.8" board (213 x 320 and 320 x 213), group "bg"
  The engine takes either: with only the other board's pictures it resamples them once per look.
Out: build-pkgs/espos/s3/vn.pawboop.tank-<v>-{core,bg35,bg28}.espk, vn.pawboop.tank-<v>.jpg (the store's icon),
     vn.pawboop.tank-<v>-catalog.json (the catalog entry, for scripts/publish_pkg.py --meta). Nothing is uploaded.
--sim DIR: also installs core + bg35 + bg28 on that simulator data folder's card (tools/espk.py --install-sim).
"""
import argparse
import datetime
import json
import struct
import sys
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import espk  # noqa: E402

HAND = ROOT / "data/tank/handoff"
FONTS = ROOT / "build-pkgs/fonts/dev.espos.font.vi"
ID, VERSION = "vn.pawboop.tank", "1.0.0"
MIN_FW = 506  # the first firmware with the engine "tank" (0.5.6: the next after 0.5.5)
BASE = "https://api.espos.dev/storage/bb/149/apps-s3"
SS = 4  # supersampling of the drawn pieces

MANIFEST = {
    "id": ID, "name": "Aquarium", "version": VERSION, "kind": "native", "engine": "tank", "min_fw": MIN_FW,
    "category": "game", "author": "PawBoop",
    "desc": "Keep real-looking fish in a planted tank or a reef. Feed them, clean the glass, follow the keeper's rules.",
    "icon": {"bg": ["#2aa6c9", "#0b4f6c"], "glyph": "drop"}, "perms": [],
    "i18n": {"vi": {"name": "Hồ cá cảnh", "desc": "Nuôi cá như thật trong hồ thủy sinh hay hồ san hô. Cho ăn, lau kính, giữ đúng luật nuôi từng loài."},
             # the OS's "Aquarium" in each language pack (i18n/<tag>.json)
             "zh-Hans": {"name": "水族箱"}, "es": {"name": "Acuario"}, "fr": {"name": "Aquarium"}, "de": {"name": "Aquarium"},
             "ja": {"name": "アクアリウム"}, "ko": {"name": "아쿠아리움"}, "pt-BR": {"name": "Aquário"}, "ru": {"name": "Аквариум"},
             "id": {"name": "Akuarium"}},
}
PARTS = {
    # "board": the device takes the part made for it (s3desk src/pkg/store.cpp fitBoard), whatever was asked
    "bg35": {"group": "bg", "label": "3.5 inch screen", "labels": {"vi": "Màn 3,5 inch"}, "default": True, "board": "es3c35p",
             "files": ["tank_bg.pbu", "tank_bg_wide.pbu"]},
    "bg28": {"group": "bg", "label": "2.8 inch screen", "labels": {"vi": "Màn 2,8 inch"}, "board": "es3c28p",
             "files": ["tank_bg_28.pbu", "tank_bg_28_wide.pbu"]},
}
CORE_FILES = ["tank.json", "tank_fish_fresh.pbu", "tank_fish_marine.pbu", "tank_things.pbu", "tank_thumbs.pbu"]
FONT_FILES = ["r12.bin", "r16.bin", "b12.bin", "b13.bin", "b14.bin", "b15.bin", "b16.bin"]


def code_of(v):
    a, b, c = (list(map(int, v.split("."))) + [0, 0, 0])[:3]
    return a * 10000 + b * 100 + c


def checked():
    """Every hand-off file against its manifest.json (sizes)."""
    man = json.loads((HAND / "manifest.json").read_text())
    for p in man["packs"]:
        f = HAND / p["file"]
        if f.stat().st_size != p["bytes"]:
            sys.exit(f"{f}: {f.stat().st_size} bytes, the hand-off says {p['bytes']}")
    return man


# ---------------------------------------------------------------- PBU1 (fw/pbu.h): fmt 1 = RGB565 then alpha (straight)
def rgb565(r, g, b):
    return (r >> 3) << 11 | (g >> 2) << 5 | (b >> 3)


def spread(im):
    """The edge colour spread 4 px into the clear surround (the hand-off's rule: the blits sample between pixels)."""
    im = im.copy()
    px = im.load()
    w, h = im.size
    for _ in range(4):
        add = []
        for y in range(h):
            for x in range(w):
                if px[x, y][3]:
                    continue
                acc, n = [0, 0, 0], 0
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    X, Y = x + dx, y + dy
                    if 0 <= X < w and 0 <= Y < h and px[X, Y][3]:
                        c = px[X, Y]
                        acc = [acc[0] + c[0], acc[1] + c[1], acc[2] + c[2]]
                        n += 1
                if n:
                    add.append((x, y, (acc[0] // n, acc[1] // n, acc[2] // n)))
        for x, y, c in add:
            px[x, y] = c + (1,)  # marked as spread (alpha 1 -> written as 0)
    return im


def pbu(pieces):
    """pieces: [(name, RGBA image)] -> PBU1 bytes."""
    head = struct.pack("<4sHH", b"PBU1", len(pieces), 0)
    off = 8 + 40 * len(pieces)
    table, data = b"", b""
    for name, im in pieces:
        im = spread(im.convert("RGBA"))
        w, h = im.size
        c = b"".join(struct.pack("<H", rgb565(*im.getpixel((x, y))[:3])) for y in range(h) for x in range(w))
        a = bytes(0 if im.getpixel((x, y))[3] <= 1 else im.getpixel((x, y))[3] for y in range(h) for x in range(w))
        table += struct.pack("<24shhHHBBHI", name.encode(), 0, 0, w, h, 1, 0, 0, off + len(data))
        data += c + a
    return head + table + data


# ---------------------------------------------------------------- the pieces the page draws with canvas paths
def canvas(w, h):
    im = Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))
    return im, ImageDraw.Draw(im)


def S(v):
    return round(v * SS)


def rr(d, x, y, w, h, r, fill=None, stroke=None, lw=2):
    box = [S(x), S(y), S(x + w), S(y + h)]
    d.rounded_rectangle(box, S(r), fill=fill)
    if stroke:
        d.rounded_rectangle(box, S(r), outline=stroke, width=S(lw))


def poly(d, pts, col, lw):
    d.line([(S(x), S(y)) for x, y in pts], fill=col, width=S(lw), joint="curve")
    for x, y in (pts[0], pts[-1]):
        d.ellipse([S(x - lw / 2), S(y - lw / 2), S(x + lw / 2), S(y + lw / 2)], fill=col)


def quad(p0, p1, p2, n=12):
    return [((1 - t) ** 2 * p0[0] + 2 * (1 - t) * t * p1[0] + t * t * p2[0], (1 - t) ** 2 * p0[1] + 2 * (1 - t) * t * p1[1] + t * t * p2[1])
            for t in (i / n for i in range(n + 1))]


def cubic(p0, p1, p2, p3, n=16):
    def at(t):
        u = 1 - t
        return (u ** 3 * p0[0] + 3 * u * u * t * p1[0] + 3 * u * t * t * p2[0] + t ** 3 * p3[0],
                u ** 3 * p0[1] + 3 * u * u * t * p1[1] + 3 * u * t * t * p2[1] + t ** 3 * p3[1])
    return [at(i / n) for i in range(n + 1)]


def glyph(kind):
    """ref/ui.js glyph(): the machines' small pictures in a 52 x 50 box (stroke #2a5c88 2 px, fill #bfe3f7)."""
    W, H = 52, 50
    im, d = canvas(W, H)
    cx, cy = W / 2, H / 2
    ink, fill = (0x2a, 0x5c, 0x88, 255), (0xbf, 0xe3, 0xf7, 255)
    if kind == "size":
        rr(d, cx - 15, cy - 9, 30, 20, 3, fill, ink)
        poly(d, [(cx - 21, cy - 7), (cx - 21, cy - 15), (cx - 13, cy - 15)], ink, 2)
        poly(d, [(cx + 13, cy + 16), (cx + 21, cy + 16), (cx + 21, cy + 8)], ink, 2)
    elif kind == "filter":
        rr(d, cx - 11, cy - 15, 22, 30, 4, fill, ink)
        for i in (-1, 0, 1):
            y = cy + i * 7
            poly(d, quad((cx - 6, y), (cx - 3, y - 4), (cx, y)) + quad((cx, y), (cx + 3, y + 4), (cx + 6, y))[1:], ink, 2)
    elif kind == "heater":
        rr(d, cx - 4, cy - 16, 8, 24, 4, (255, 255, 255, 255), ink)
        d.ellipse([S(cx - 7), S(cy + 4), S(cx + 7), S(cy + 18)], fill=(0xe5, 0x48, 0x4d, 255), outline=ink, width=S(2))
        d.rectangle([S(cx - 1.5), S(cy - 6), S(cx + 1.5), S(cy + 8)], fill=(0xe5, 0x48, 0x4d, 255))
    elif kind == "lid":
        rr(d, cx - 15, cy - 6, 30, 20, 3, fill, ink)
        rr(d, cx - 18, cy - 12, 36, 6, 2, ink, ink)
        rr(d, cx - 5, cy - 17, 10, 5, 2, ink)
    elif kind == "skimmer":
        rr(d, cx - 9, cy - 4, 18, 20, 3, fill, ink)
        rr(d, cx - 6, cy - 16, 12, 12, 3, (255, 255, 255, 255), ink)
        for px, py in ((-3, 4), (3, 8), (-1, 11), (2, 1)):
            d.ellipse([S(cx + px - 1.6), S(cy + py - 1.6), S(cx + px + 1.6), S(cy + py + 1.6)], fill=ink)
    return im.resize((W, H), Image.LANCZOS)


def food(kind):
    """ref/ui.js foodArt(): green tablets (sink) and red worms (live), round the centre of a 32 x 32 piece."""
    W = 32
    im, d = canvas(W, W)
    x, y = W / 2, W / 2
    if kind == "sink":
        import math
        for px, py in ((-7, -3), (6, -5), (0, 6), (-9, 8), (9, 7)):
            pts = []
            for i in range(32):
                a = i / 32 * 2 * math.pi
                ex, ey = 5.2 * math.cos(a), 3.6 * math.sin(a)
                pts.append((S(x + px + ex * math.cos(0.3) - ey * math.sin(0.3)), S(y + py + ex * math.sin(0.3) + ey * math.cos(0.3))))
            d.polygon(pts, fill=(0x4f, 0x6a, 0x2a, 255), outline=(0x31, 0x45, 0x1a, 255), width=S(1))
    else:
        for px, py, k in ((-8, -6, 1), (2, -1, -1), (-5, 8, 1)):
            poly(d, cubic((x + px - 6, y + py), (x + px - 2, y + py - 6 * k), (x + px + 2, y + py + 6 * k), (x + px + 7, y + py - 1)),
                 (0xd2, 0x3a, 0x33, 255), 2.6)
    return im.resize((W, W), Image.LANCZOS)


# ---------------------------------------------------------------- the icon
def pbu_piece(path, name):
    b = path.read_bytes()
    n = struct.unpack_from("<H", b, 4)[0]
    for i in range(n):
        nm, x, y, w, h, fmt, adv, z, off = struct.unpack_from("<24shhHHBBHI", b, 8 + 40 * i)
        if nm.rstrip(b"\0").decode() == name:
            im = Image.new("RGBA", (w, h))
            px = im.load()
            for j in range(h):
                for k in range(w):
                    c = struct.unpack_from("<H", b, off + 2 * (j * w + k))[0]
                    a = b[off + 2 * w * h + j * w + k] if fmt else 255
                    px[k, j] = ((c >> 11) * 255 // 31, (c >> 5 & 63) * 255 // 63, (c & 31) * 255 // 31, a)
            return im
    raise SystemExit(f"{path.name}: no piece {name}")


def icon():
    bg = Image.open(HAND / "bg_reef.jpg").convert("RGB")  # 320 x 480: the reef, its middle
    side = 300
    bg = bg.crop((10, 110, 10 + side, 110 + side)).resize((120, 120), Image.LANCZOS).convert("RGBA")
    clown = pbu_piece(HAND / "tank_fish_marine.pbu", "clown")
    clown = clown.resize((int(clown.width * 1.45), int(clown.height * 1.45)), Image.LANCZOS)
    tang = pbu_piece(HAND / "tank_fish_marine.pbu", "btang").transpose(Image.FLIP_LEFT_RIGHT)
    tang = tang.resize((int(tang.width * 0.9), int(tang.height * 0.9)), Image.LANCZOS)
    bg.alpha_composite(tang, (8, 20))
    bg.alpha_composite(clown, (120 - clown.width - 8, 120 - clown.height - 14))
    out = Image.new("RGB", (120, 120))
    out.paste(bg)
    import io
    b = io.BytesIO()
    out.save(b, "JPEG", quality=90, progressive=False, optimize=False)
    return b.getvalue()


# ---------------------------------------------------------------- the parts
def build(out: Path):
    checked()
    code = code_of(VERSION)
    paths = []
    ui = pbu([(f"g_{k}", glyph(k)) for k in ("size", "filter", "heater", "lid", "skimmer")] + [("f_sink", food("sink")), ("f_live", food("live"))])
    ic = icon()
    with tempfile.TemporaryDirectory() as td:
        t = Path(td) / "core"
        (t / "font").mkdir(parents=True)
        man = dict(MANIFEST, code=code, part="core")
        (t / "manifest.json").write_text(json.dumps(man, ensure_ascii=False))
        for f in CORE_FILES:
            (t / f).write_bytes((HAND / f).read_bytes())
        (t / "tank_ui.pbu").write_bytes(ui)
        for f in FONT_FILES:
            (t / "font" / f).write_bytes((FONTS / f).read_bytes())
        (t / "font/LICENSE.txt").write_bytes((FONTS / "LICENSE.txt").read_bytes())
        (t / "icon.jpg").write_bytes(ic)
        paths.append(espk.build(t, out / f"{ID}-{VERSION}-core.espk"))
        for part, p in PARTS.items():
            t = Path(td) / part
            t.mkdir()
            pm = {"id": ID, "version": VERSION, "code": code, "part": part}
            pm.update({k: p[k] for k in ("group", "label", "labels", "default", "board") if k in p})
            (t / "manifest.json").write_text(json.dumps(pm, ensure_ascii=False))
            for f in p["files"]:
                (t / f).write_bytes((HAND / f).read_bytes())
            paths.append(espk.build(t, out / f"{ID}-{VERSION}-{part}.espk"))
    (out / f"{ID}-{VERSION}.jpg").write_bytes(ic)
    e = espk.entry(BASE, paths)
    e["icon_url"] = f"{BASE}/{ID}-{VERSION}.jpg"
    e["date"] = datetime.date.today().isoformat()
    errs = espk.check_catalog({"packages": [e]})
    if errs:
        sys.exit("catalog entry: " + "; ".join(errs))
    (out / f"{ID}-{VERSION}-catalog.json").write_text(json.dumps(e, ensure_ascii=False, indent=1))
    print(f"{ID}-{VERSION}-catalog.json: {sum(p['size'] for p in e['parts'])} bytes in {len(e['parts'])} parts")
    return paths


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(ROOT / "build-pkgs/espos/s3"))
    ap.add_argument("--sim")
    a = ap.parse_args()
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    paths = build(out)
    if a.sim:
        espk.install_sim(a.sim, [str(p) for p in paths], "sd")


if __name__ == "__main__":
    main()
