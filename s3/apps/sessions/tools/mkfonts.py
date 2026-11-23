#!/usr/bin/env python3
"""The Sessions app's terminal fonts: DejaVu Sans Mono (data/fonts/dejavu, Bitstream Vera licence + public domain)
cut to what a Claude Code screen prints, at the terminal's text sizes, in the OS's ESF format (tools/i18n.py esf,
loaded by fonts::fromFile through ui.term's `font` option).

    python3 apps/sessions/tools/mkfonts.py            (from s3desk)

Writes apps/sessions/pkg/fonts/m<px>.bin (regular) and b<px>.bin (bold, the two default sizes) and the licence.
Every glyph gets the mono cell's advance (8 px: a 6 px cell, ~52 columns in portrait and ~78 in landscape on the 3.5").
Glyphs DejaVu Sans Mono lacks:
  - the Vietnamese letters with the hook above (ả ấ ầ ẩ ẫ ủ ...): from the OS's Ubuntu Vietnamese, centred in the cell;
  - Claude Code's braille spinners and ⠿, ⏸ (plan mode), ※ (tips), ⧉ (editor selection), ⑂ (fork): drawn here;
  - ⏺ ⎿ ⏵ ⎯ and emoji: src/js/jsterm.cpp substitutes them at draw time.
Checked against Claude Code 2.1.330's own figures (its symbol table, spinners and logo).
"""
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools"))
import i18n  # noqa: E402  (esf(), ranges(), CONV)

SRC = ROOT / "data" / "fonts" / "dejavu"
OUT = Path(__file__).resolve().parents[1] / "pkg" / "fonts"
SIZES = [8, 10, 12, 14, 15, 17]  # 3.5": 8/10/12/14 (default 8); 2.8": 12/14/15/17 (default 12 = 8 px on its panel)
BOLD = [8, 12]                    # the defaults' bold (Claude's tool names); other sizes draw bold in the bold colour
CELL = {8: 6}                     # px -> cell width when DejaVu's own (0.6 em) is too narrow to read
BPP = 2
VIET = ROOT / "data" / "fonts"    # Ubuntu-Vietnamese-{Regular,Bold}.ttf: the OS's own (UFL)

VI = "ÀÁÂÃÈÉÊÌÍÒÓÔÕÙÚÝàáâãèéêìíòóôõùúýĂăĐđĨĩŨũƠơƯư" + "".join(chr(c) for c in range(0x1EA0, 0x1EFA))
CHARS = (
    [range(0x20, 0x7F), range(0xA0, 0x100)]
    + [[ord(c) for c in VI]]
    + [range(0x2010, 0x2028), range(0x2030, 0x203B)]   # dashes, quotes, bullets, …
    + [range(0x2190, 0x219A), [0x21B5, 0x21B9, 0x21E7, 0x21E5, 0x21E4]]  # arrows, ↵ ⇧ ⇥
    + [[0x2212, 0x00D7, 0x2248, 0x2260, 0x2264, 0x2265, 0x221E, 0x2026]]
    + [range(0x2500, 0x2580), range(0x2580, 0x25A0)]   # box drawing, blocks
    + [range(0x25A0, 0x2600)]                           # geometric shapes: ● ○ ◆ ■ □ ▶ ◀ …
    + [range(0x2713, 0x2719), range(0x2722, 0x273E), [0x276F, 0x276E, 0x2771, 0x2770, 0x26A0, 0x2605, 0x2606]]
    + [[0x2219, 0x2315, 0x2234, 0x2235, 0x2237, 0x21B3, 0x23CE, 0x21AF, 0x21BB, 0x2318, 0x2325, 0x2303]]  # ∙ ⌕ ∴ ∵ ∷ ↳ ⏎ ↯ ↻ ⌘ ⌥ ⌃
)
BRAILLE = [0x2800, 0x280B, 0x2819, 0x2839, 0x2838, 0x283C, 0x2834, 0x2826, 0x2827, 0x2807, 0x280F, 0x283F]
DRAWN = BRAILLE + [0x23F8, 0x203B, 0x29C9, 0x2442]   # ⏸ ※ ⧉ ⑂


def parse(path):
    """lv_font_conv's C output -> (metrics, bitmap, {code point: glyph dsc}): like tools/i18n.py parse_lvgl_c, plus the
    FULL cmaps lv_font_conv writes when it merges identical glyphs (two code points, one glyph)."""
    s = Path(path).read_text()
    bm = re.search(r"glyph_bitmap\[\]\s*=\s*\{(.*?)\};", s, re.S).group(1)
    bitmap = bytes(int(x, 16) for x in re.findall(r"0x[0-9a-fA-F]+", re.sub(r"/\*.*?\*/", "", bm, flags=re.S)))
    gd = re.search(r"glyph_dsc\[\]\s*=\s*\{(.*?)\};", s, re.S).group(1)
    dsc = [tuple(int(v) for v in g) for g in re.findall(
        r"\.bitmap_index\s*=\s*(-?\d+),\s*\.adv_w\s*=\s*(-?\d+),\s*\.box_w\s*=\s*(-?\d+),\s*\.box_h\s*=\s*(-?\d+),"
        r"\s*\.ofs_x\s*=\s*(-?\d+),\s*\.ofs_y\s*=\s*(-?\d+)", gd)]
    arrays = {m.group(1): [int(x, 16) if x.startswith("0x") else int(x) for x in re.findall(r"0x[0-9a-fA-F]+|\b\d+\b", m.group(2))]
              for m in re.finditer(r"(\w+_list_\d+)\[\]\s*=\s*\{(.*?)\};", s, re.S)}
    glyph = {}
    for m in re.finditer(r"\.range_start\s*=\s*(\d+),\s*\.range_length\s*=\s*(\d+),\s*\.glyph_id_start\s*=\s*(\d+),"
                         r"\s*\.unicode_list\s*=\s*(\w+),\s*\.glyph_id_ofs_list\s*=\s*(\w+),\s*\.list_length\s*=\s*(\d+),"
                         r"\s*\.type\s*=\s*(\w+)", s, re.S):
        start, length, gstart, ul, ol, _, typ = m.groups()
        start, length, gstart = int(start), int(length), int(gstart)
        if typ.endswith("FORMAT0_TINY"):
            pairs = [(start + i, gstart + i) for i in range(length)]
        elif typ.endswith("FORMAT0_FULL"):
            pairs = [(start + i, gstart + o) for i, o in enumerate(arrays[ol])]
        elif typ.endswith("SPARSE_TINY"):
            pairs = [(start + u, gstart + i) for i, u in enumerate(arrays[ul])]
        elif typ.endswith("SPARSE_FULL"):
            pairs = [(start + u, gstart + o) for u, o in zip(arrays[ul], arrays[ol])]
        else:
            sys.exit(f"{path}: cmap type {typ} not handled")
        for cp, g in pairs:
            glyph[cp] = dsc[g]
    num = lambda k: int(re.search(r"\." + k + r"\s*=\s*(-?\d+)", s).group(1))
    return (num("line_height"), num("base_line"), num("underline_position"), num("underline_thickness"), num("bpp")), \
        bitmap, glyph


def unpack(bitmap, bi, bw, bh):
    """A glyph's 2 bpp pixels (LVGL: rows one after another, no padding, first pixel in the top bits) -> rows of 0..3."""
    rows, k = [], bi * 8
    for _ in range(bh):
        row = []
        for _ in range(bw):
            row.append((bitmap[k >> 3] >> (6 - (k & 7))) & 3)
            k += 2
        rows.append(row)
    return rows


def pack(rows):
    out, acc, n = bytearray(), 0, 0
    for row in rows:
        for v in row:
            acc = (acc << 2) | v
            n += 1
            if n == 4:
                out.append(acc), (acc := 0), (n := 0)
    if n:
        out.append(acc << (2 * (4 - n)))
    return bytes(out)


def crop(rows):
    """-> (rows, left, top) without empty borders; an empty glyph keeps 0x0."""
    ys = [y for y, r in enumerate(rows) if any(r)]
    if not ys:
        return [], 0, 0
    xs = [x for r in rows for x, v in enumerate(r) if v]
    l, rr = min(xs), max(xs)
    return [r[l:rr + 1] for r in rows[ys[0]:ys[-1] + 1]], l, ys[0]


def draw(cp, px, lh, base, cell, bold):
    """The glyphs no font we ship has, drawn at 8x and boxed down to the cell: (rows, ofs_x, ofs_y)."""
    from PIL import Image, ImageDraw
    S = 8
    w = max(1, int(cell + 0.999))
    W, H = w * S, lh * S
    im = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(im)
    bl, em, cx = (lh - base) * S, px * S, W / 2
    sw = max(S, int(em * (0.12 if bold else 0.085)))
    if cp in BRAILLE:  # dots 1-8: 1 2 3 7 down the left column, 4 5 6 8 down the right
        bits = cp - 0x2800
        cols, top, bot = (W * 0.3, W * 0.72), bl - em * 0.78, bl - em * 0.02
        ys = [top + (bot - top) * i / 3 for i in range(4)]
        r = max(S * 0.55, em * (0.085 if bold else 0.07))
        for b, (c, rw) in enumerate(((0, 0), (0, 1), (0, 2), (1, 0), (1, 1), (1, 2), (0, 3), (1, 3))):
            if bits >> b & 1:
                d.ellipse([cols[c] - r, ys[rw] - r, cols[c] + r, ys[rw] + r], fill=255)
    elif cp == 0x23F8:  # ⏸: two bars on whole pixels (a blurred pair reads as a block at 8 px)
        bw, gap = max(1, round(px * 0.16)) * S, max(2, round(px * 0.18)) * S
        x0 = (W - 2 * bw - gap) // (2 * S) * S
        top = (bl - em * 0.70) // S * S
        d.rectangle([x0, top, x0 + bw - 1, bl - 1], fill=255)
        d.rectangle([x0 + bw + gap, top, x0 + 2 * bw + gap - 1, bl - 1], fill=255)
    elif cp == 0x203B:  # ※: an x with a dot in each gap
        mid, a = bl - em * 0.36, min(W * 0.40, em * 0.28)
        d.line([cx - a, mid - a, cx + a, mid + a], fill=255, width=sw)
        d.line([cx - a, mid + a, cx + a, mid - a], fill=255, width=sw)
        r = max(S * 0.6, em * 0.06)
        for dx, dy in ((0, -a), (0, a), (-a, 0), (a, 0)):
            d.ellipse([cx + dx - r, mid + dy - r, cx + dx + r, mid + dy + r], fill=255)
    elif cp == 0x29C9:  # ⧉: two joined squares
        side = min(W * 0.62, em * 0.42)
        off = side * 0.42
        x0, y1 = cx - (side + off) / 2, bl - em * 0.06
        d.rectangle([x0, y1 - side, x0 + side, y1], outline=255, width=sw)
        d.rectangle([x0 + off, y1 - side - off, x0 + off + side, y1 - off], outline=255, width=sw)
    elif cp == 0x2442:  # ⑂: a stem that forks
        top, mid, a = bl - em * 0.72, bl - em * 0.34, min(W * 0.34, em * 0.22)
        d.line([cx, bl, cx, mid], fill=255, width=sw)
        for sgn in (-1, 1):
            d.line([cx, mid, cx + sgn * a, mid - a * 0.7], fill=255, width=sw)
            d.line([cx + sgn * a, mid - a * 0.7, cx + sgn * a, top], fill=255, width=sw)
    small = im.resize((w, lh), Image.BOX)
    rows = [[min(3, (small.getpixel((x, y)) + 42) // 85) for x in range(w)] for y in range(lh)]
    rows, left, top = crop(rows)
    return rows, left, (lh - base) - (top + len(rows))


def conv(ttf, px, cps, out):
    subprocess.run([str(i18n.CONV), "--bpp", str(BPP), "--size", str(px), "--no-compress", "--no-kerning",
                    "--format", "lvgl", "--lv-include", "lvgl.h", "--font", str(ttf), "-r", i18n.ranges(cps),
                    "-o", str(out), "--lv-font-name", "f"], check=True, stdout=subprocess.DEVNULL)
    return parse(out)


def font(px, bold, tmp):
    """One size: DejaVu Sans Mono + the Vietnamese letters it lacks + the drawn glyphs, every advance the cell's."""
    from fontTools.ttLib import TTFont
    ttf = SRC / ("DejaVuSansMono-Bold.ttf" if bold else "DejaVuSansMono.ttf")
    cmap = TTFont(ttf).getBestCmap()
    (lh, base, upos, uth, bpp), bitmap, glyph = conv(ttf, px, sorted({c for r in CHARS for c in r if c in cmap}),
                                                     Path(tmp) / f"m{px}{bold}.c")
    # The line box of DejaVu itself (ascent + descent = 1.164 em), not lv_font_conv's tallest glyph (stacked
    # Vietnamese marks, ~1.42 em): more lines on the screen; a tall mark may reach into the row above.
    lh, base = int(px * 1.164 + 0.999), int(px * 0.236 + 0.5)
    adv = CELL[px] * 16 if px in CELL else glyph[ord("M")][1]
    cell = adv / 16
    out = {}  # code point -> (rows, ofs_x, ofs_y)
    for cp, (bi, a, bw, bh, ox, oy) in glyph.items():
        out[cp] = (unpack(bitmap, bi, bw, bh), ox + int(round((cell - a / 16) / 2)), oy)
    viet = [ord(c) for c in VI if ord(c) not in glyph]
    if viet:
        vt = VIET / ("Ubuntu-Vietnamese-Bold.ttf" if bold else "Ubuntu-Vietnamese-Regular.ttf")
        _, vb, vg = conv(vt, px, viet, Path(tmp) / f"v{px}{bold}.c")
        for cp in viet:
            if cp in vg:
                bi, a, bw, bh, ox, oy = vg[cp]
                out[cp] = (unpack(vb, bi, bw, bh), int(round((cell - bw) / 2)), oy)
    for cp in DRAWN:
        if cp not in out:
            out[cp] = draw(cp, px, lh, base, cell, bold)
    return (lh, base, upos, uth, bpp), adv, out, len(viet)


def esf(metrics, adv, glyphs, px, bold):
    """The ESF1 file of fonts.cpp (layout as tools/i18n.py esf): glyph ids in code point order, one per code point."""
    lh, base, upos, uth, bpp = metrics
    cps = sorted(glyphs)
    first, n = cps[0], len(cps)
    off_list = 40
    off_dsc = (off_list + 2 * n + 3) & ~3
    off_bmp = off_dsc + 8 * (n + 1)
    lst = b"".join(struct.pack("<H", cp - first) for cp in cps)
    ds, bmp = bytearray(8), bytearray()
    for cp in cps:
        rows, ox, oy = glyphs[cp]
        bw, bh = (len(rows[0]), len(rows)) if rows else (0, 0)
        ds += struct.pack("<IBBbb", (len(bmp) & 0xFFFFF) | ((adv & 0xFFF) << 20), bw, bh, ox, oy)
        bmp += pack(rows)
    head = struct.pack("<4sBBBBhhbbHIIIIII", b"ESF1", px, 1 if bold else 0, bpp, 0, lh, base, upos, uth, 0, first, n,
                       off_list, off_dsc, off_bmp, cps[-1] - first + 1)
    return head + lst + b"\0" * (off_dsc - off_list - len(lst)) + bytes(ds) + bytes(bmp)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for old in OUT.glob("[mb]*.bin"):
        old.unlink()
    total = 0
    with tempfile.TemporaryDirectory() as tmp:
        for bold, sizes in ((False, SIZES), (True, BOLD)):
            for px in sizes:
                metrics, adv, glyphs, nv = font(px, bold, tmp)
                blob = esf(metrics, adv, glyphs, px, bold)
                (OUT / f"{'b' if bold else 'm'}{px}.bin").write_bytes(blob)
                total += len(blob)
                print(f"{'b' if bold else 'm'}{px}.bin: {len(glyphs)} letters ({nv} Vietnamese from Ubuntu), "
                      f"cell {adv / 16:g} px, line {metrics[0]} px, {len(blob)} B")
    (OUT / "LICENSE-DejaVu.txt").write_bytes((SRC / "LICENSE").read_bytes())
    ufl = VIET / "UFL.txt"
    if ufl.exists():
        (OUT / "LICENSE-Ubuntu.txt").write_bytes(ufl.read_bytes())
    print(f"total {total} B")


if __name__ == "__main__":
    main()
