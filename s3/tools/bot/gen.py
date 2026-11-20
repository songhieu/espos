#!/usr/bin/env python3
"""EspOS Bot's art for the 128x64 OLED (src/bot/art.h + art.cpp), from bot-art's art2 (tools/bot/art2: looks2,
standby2, digits2, px; the owner picked look B "Mắt to" as the default, 2026-11-20).

  python3 tools/bot/gen.py      src/bot/art.{h,cpp} + tools/bot/out/preview.html (every scene and screen, 3x)

What goes in:
  - the dog's looks B, C, D: sleep, listen, think, happy, sad, idle, talk, talk_happy, talk_sad (no text), and where
    the reply goes in each (13-px lines);
  - the 21 standby screens: their background loops (no live text), their live fields (digits, text, hands, the
    seconds run, the month grid; standby2.draw_live is the reference src/bot/ui.cpp follows), the overlay masks
    (drawn over the live fields: the flip clock's split line), drift;
  - the digit sets (digits2.sets(), with standby2's additions), the fonts F11 (Ubuntu 11 + every Vietnamese letter),
    T5 (Tiny5 8 px: OFL), F16B (Ubuntu Bold 16, ASCII: codes and titles);
  - the ca dao list (i18n/vi.json "@quotes", the S3's own, same order) and the English one.
Frames are run-length coded in SSD1306 order: 0x80|n = the next byte n times; n = n bytes as they are.
"""
import json
import os
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))  # s3desk
sys.path.insert(0, os.path.join(HERE, "art2"))
import px  # noqa: E402
import digits2  # noqa: E402
import looks2  # noqa: E402
import standby2  # noqa: E402  (also completes the digit sets: seg16, bold16's "/", bold24's ",")

W, H = 128, 64
OUT_H = os.path.join(ROOT, "src/bot/art.h")
OUT_C = os.path.join(ROOT, "src/bot/art.cpp")
PREV = os.path.join(HERE, "out")
LOOKS = ["B", "C", "D"]
SCENES = ["sleep", "listen", "think", "happy", "sad", "idle", "talk", "talk_happy", "talk_sad"]
UB = os.path.join(ROOT, "data/fonts/Ubuntu-Regular.ttf")
UBB = os.path.join(ROOT, "data/fonts/Ubuntu-Bold.ttf")
UBV = os.path.join(ROOT, "data/fonts/Ubuntu-Vietnamese-Regular.ttf")
VI = "àáảãạăằắẳẵặâầấẩẫậèéẻẽẹêềếểễệìíỉĩịòóỏõọôồốổỗộơờớởỡợùúủũụưừứửữựỳýỷỹỵđ"
CHARS = "".join(chr(c) for c in range(32, 127)) + VI + VI.upper() + "°·–—“”‘’"


# ------------------------------------------------------------------ frames
def as_img(a):
    """numpy bool [64, 128] or a PIL "1" image -> PIL "1"."""
    return a if isinstance(a, Image.Image) else px.img(a)


def pack(im):
    p = im.load()
    raw = bytearray(1024)
    for y in range(H):
        for x in range(W):
            if p[x, y]:
                raw[(y >> 3) * 128 + x] |= 1 << (y & 7)
    out, i = bytearray(), 0
    lit = bytearray()

    def flush():
        while lit:
            n = min(127, len(lit))
            out.append(n)
            out.extend(lit[:n])
            del lit[:n]

    while i < 1024:
        j = i
        while j < 1024 and raw[j] == raw[i] and j - i < 127:
            j += 1
        if j - i >= 3:
            flush()
            out.append(0x80 | (j - i))
            out.append(raw[i])
            i = j
        else:
            lit.append(raw[i])
            i += 1
    flush()
    return bytes(out), bytes(raw)


def bits(im):
    """{w, h, rows MSB first} bitmaps."""
    w, h = im.size
    bpr = (w + 7) // 8
    data = bytearray(bpr * h)
    p = im.load()
    for y in range(h):
        for x in range(w):
            if p[x, y]:
                data[y * bpr + x // 8] |= 0x80 >> (x % 8)
    return data


def carr(name, data, typ="uint8_t", static=True):
    return f"{'static ' if static else ''}const {typ} {name}[] = {{" + ",".join(str(x) for x in data) + "};"


def cstr(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


# ------------------------------------------------------------------ fonts
_cm = {}


def has(path, ch):
    if path not in _cm:
        from fontTools.ttLib import TTFont
        _cm[path] = set(TTFont(path).getBestCmap().keys())
    return ord(ch) in _cm[path]


def ub11_glyphs():
    """Ubuntu 11 (Vietnamese letters from the split font), one cell per letter: 3 rows above for the marks over
    capitals; the firmware draws a line at y with its baseline at y + ascent (as px.text)."""
    top = 3
    asc, desc = ImageFont.truetype(UB, 11).getmetrics()  # one baseline for both fonts (as px.text)
    hgt = asc + desc + top
    out = []
    for ch in CHARS:
        path = UB if has(UB, ch) else UBV
        f = ImageFont.truetype(path, 11)
        adv = int(round(f.getlength(ch)))
        im = Image.new("1", (adv + 6, hgt), 0)
        d = ImageDraw.Draw(im)
        d.fontmode = "1"
        d.text((1, top + asc), ch, font=f, fill=1, anchor="ls")
        out.append(glyph_of(ch, adv, im, hgt))
    return hgt, top, out


def ubb16_glyphs():
    out = []
    f = ImageFont.truetype(UBB, 16)
    asc, desc = f.getmetrics()
    hgt = asc + desc
    for c in range(32, 127):
        ch = chr(c)
        adv = int(round(f.getlength(ch)))
        im = Image.new("1", (adv + 6, hgt), 0)
        d = ImageDraw.Draw(im)
        d.fontmode = "1"
        d.text((1, asc), ch, font=f, fill=1, anchor="ls")
        out.append(glyph_of(ch, adv, im, hgt))
    return hgt, out


def tiny_glyphs():
    """Tiny5 at 8 px, as px.tiny: drawn with its baseline at row 9 of a 16-row cell that sits 4 rows above y."""
    f = ImageFont.truetype(px.TINY5, 8)
    out = []
    for ch in CHARS:
        adv = int(round(f.getlength(ch)))
        im = Image.new("1", (adv + 6, 16), 0)
        d = ImageDraw.Draw(im)
        d.fontmode = "1"
        d.text((1, 9), ch, font=f, fill=1, anchor="ls")
        out.append(glyph_of(ch, adv, im, 16))
    return 16, 4, out


def glyph_of(ch, adv, im, hgt):
    bbox = im.getbbox()
    if not bbox:
        return (ch, adv, 0, 0, b"")
    x0, x1 = bbox[0], bbox[2]
    g = im.crop((x0, 0, x1, hgt))
    return (ch, adv, x1 - x0, x0 - 1, bytes(bits(g)))


def font_block(name, hgt, gl, L, H_, extra=""):
    gl = sorted(gl, key=lambda g: ord(g[0]))
    data, rows = bytearray(), []
    for ch, adv, w, xo, b in gl:
        rows.append(f"{{{ord(ch)},{adv},{w},{xo},{len(data)}}}")
        data += b
    L.append(carr(f"{name}_BITS", data, static=False))
    L.append(f"const Glyph {name}_G[] = {{" + ",".join(rows) + "};")
    H_.append(f"constexpr int {name}_N = {len(gl)}, {name}_H = {hgt}{extra};")
    H_.append(f"extern const uint8_t {name}_BITS[];")
    H_.append(f"extern const Glyph {name}_G[];")


# ------------------------------------------------------------------ the generator
def main():
    frames, fidx, prev_frames = [], {}, {}

    def fid(a):
        b, raw = pack(as_img(a))
        if raw not in fidx:
            fidx[raw] = len(frames)
            frames.append(b)
        return fidx[raw]

    H_ = ["// Made by tools/bot/gen.py from bot-art's art2 (tools/bot/art2): do not edit.", "#pragma once",
          "#include <stdint.h>", "", "namespace art {", ""]
    L = ["// Made by tools/bot/gen.py from bot-art's art2 (tools/bot/art2): do not edit.", '#include "art.h"', "",
         "namespace art {", ""]

    # --- the looks
    looks, boxes = {}, {}
    for lk in LOOKS:
        sc, box = looks2.face_only(lk)
        looks[lk] = {n: [(fid(a), max(20, int(ms))) for a, ms in sc[n]] for n in SCENES}
        prev_frames[f"look {lk}"] = {n: sc[n] for n in SCENES}
        boxes[lk] = box
    # --- the standby screens
    fields, screens, lives = [], [], []
    kinds = {"digits": 0, "text": 1, "hands": 2, "progress": 3, "month": 4}
    sets = digits2.sets()
    set_names = list(sets.keys())
    for s in standby2.SCREENS:
        fr = s.frames()
        steps = [(fid(a), max(20, int(ms))) for a, ms in fr]
        prev_frames.setdefault("standby", {})[s.id] = [(s.render(i * s.ms / 1000.0), s.ms) for i in range(len(fr))] \
            if len(fr) > 1 else [(s.render(0.0), 1000)]
        ov_clear = ov_set = -1
        if s.overlay:
            on = px.new()
            on[:, :] = True
            s.overlay(on, 0.0)
            clear = px.new()
            clear[:, :] = ~on
            off = px.new()
            s.overlay(off, 0.0)
            ov_clear, ov_set = fid(clear), fid(off)
        live0 = len(lives)
        for f in s.live:
            name = f["field"]
            if name not in fields:
                fields.append(name)
            k = kinds[f["kind"]]
            al = {"l": 0, "m": 1, "r": 2}.get(f.get("align", "l"), 0)
            p = [0] * 7
            x0 = y0 = x1 = y1 = 0
            ref = 0
            if k == 0:
                x0, y0, ref = f["x"], f["y"], set_names.index(f["set"])
            elif k == 1:
                x0, y0, x1, y1 = f["box"]
                ref = 0 if f["font"] == "ub11" else 1
            elif k == 2:
                p = [f["cx"], f["cy"], f["h_len"], f["m_len"], f.get("s_len", 0), int(round(f["h_w"] * 10)),
                     int(round(f["m_w"] * 10))]
            elif k == 3:
                x0, x1, y0 = f["x0"], f["x1"], f["y"]
            elif k == 4:
                x0, y0, x1, y1 = f["box"]
                p[:3] = [f["cell_w"], f["cell_h"], f["first_row_y"]]
            lives.append("{%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,{%s}}" % (
                k, fields.index(name), ref, x0, y0, x1, y1, al, int(f.get("color", 1)), int(bool(f.get("upper"))),
                int(bool(f.get("wrap"))), int(f.get("colon") == "blink"), ",".join(map(str, p))))
        screens.append((s, steps, ov_clear, ov_set, live0, len(lives)))

    # frames
    offs, blob = [], bytearray()
    for b in frames:
        offs.append(len(blob))
        blob += b
    offs.append(len(blob))
    H_ += [f"constexpr int FRAMES = {len(frames)};", "extern const uint8_t FRAME_DATA[];",
           "extern const uint32_t FRAME_OFF[];  // FRAMES + 1",
           "struct Step { uint16_t frame, ms; };", "struct Scene { const Step* steps; uint16_t n; };",
           "enum SceneId : uint8_t { " + ", ".join(f"SC_{n.upper()}" for n in SCENES) + ", SC_COUNT };",
           f"constexpr int LOOK_COUNT = {len(LOOKS)};",
           "extern const Scene SCENES[LOOK_COUNT][SC_COUNT];",
           "struct Box { int8_t x0, y0, x1, y1; };",
           "extern const Box TALK_BOX[LOOK_COUNT];  // where the reply goes (inclusive)",
           "constexpr int TALK_LINE_H = 13;"]
    L += [carr("FRAME_DATA", blob, static=False), carr("FRAME_OFF", offs, "uint32_t", static=False)]
    for lk in LOOKS:
        for n in SCENES:
            L.append(f"static const Step S_{lk}_{n}[] = {{" + ",".join(f"{{{a},{b}}}" for a, b in looks[lk][n]) + "};")
    L.append("const Scene SCENES[LOOK_COUNT][SC_COUNT] = {" + ",".join(
        "{" + ",".join(f"{{S_{lk}_{n},{len(looks[lk][n])}}}" for n in SCENES) + "}" for lk in LOOKS) + "};")
    L.append("const Box TALK_BOX[LOOK_COUNT] = {" + ",".join("{%d,%d,%d,%d}" % tuple(boxes[lk]) for lk in LOOKS) + "};")

    # digit sets
    H_.append("struct DigitSet { const char* chars; const uint8_t* const* glyphs; uint8_t gap, h; };  // UTF-8 chars")
    H_.append("enum DigitSetId : uint8_t { " + ", ".join(f"DS_{n.upper()}" for n in set_names) + " };")
    H_.append("extern const DigitSet DIGIT_SETS[];")
    rows = []
    for n in set_names:
        g = sets[n]
        chars = [c for c in sorted(g.g.keys())]
        for j, c in enumerate(chars):
            a = g.g[c]
            im = Image.fromarray(a.astype("uint8") * 255).convert("1")
            L.append(f"static const uint8_t DG_{n}_{j}[] = {{{im.width},{im.height}," + ",".join(map(str, bits(im))) + "};")
        L.append(f"static const uint8_t* const DG_{n}[] = {{" + ",".join(f"DG_{n}_{j}" for j in range(len(chars))) + "};")
        rows.append("{%s,DG_%s,%d,%d}" % (cstr("".join(chars)), n, g.gap, g.h))
    L.append("const DigitSet DIGIT_SETS[] = {" + ",".join(rows) + "};")

    # fonts
    H_.append("struct Glyph { uint16_t cp; uint8_t adv, w; int8_t xo; uint16_t off; };")
    h11, top11, g11 = ub11_glyphs()
    font_block("F11", h11, g11, L, H_, f", F11_TOP = {top11}")
    h16, g16 = ubb16_glyphs()
    font_block("F16B", h16, g16, L, H_)
    ht5, top5, gt5 = tiny_glyphs()
    font_block("T5", ht5, gt5, L, H_, f", T5_TOP = {top5}")

    # standby screens
    fname = lambda n: "FD_" + ("HANDS" if n == "hh:mm:ss" else n.upper())  # noqa: E731
    H_ += ["enum FieldId : uint8_t { " + ", ".join(fname(n) for n in fields) + ", FD_COUNT };",
           "enum LiveKind : uint8_t { LK_DIGITS, LK_TEXT, LK_HANDS, LK_PROGRESS, LK_MONTH };",
           "// digits: ref = digit set, x0 y0 = x y; text: ref 0 F11, 1 T5, box x0..y1; hands: p = cx cy h m s h_w*10 m_w*10;",
           "// progress: x0 x1 at y0; month: box, p = cell_w cell_h first_row_y. align 0 l, 1 m, 2 r.",
           "struct Live { uint8_t kind, field, ref; int8_t x0, y0, x1, y1; uint8_t align, color, upper, wrap, blink; int16_t p[7]; };",
           "extern const Live LIVE[];",
           "struct Standby { const char* id; const char* name; Scene scene; bool drift; int16_t ovClear, ovSet; uint8_t live0, live1; };",
           f"constexpr int STANDBY_COUNT = {len(screens)};",
           "extern const Standby STANDBY[STANDBY_COUNT];"]
    L.append("const Live LIVE[] = {" + ",".join(lives) + "};")
    rows = []
    for s, steps, oc, os_, l0, l1 in screens:
        L.append(f"static const Step ST_{s.id}[] = {{" + ",".join(f"{{{a},{b}}}" for a, b in steps) + "};")
        rows.append("{%s,%s,{ST_%s,%d},%s,%d,%d,%d,%d}" % (cstr(s.id), cstr(s.name), s.id, len(steps),
                                                             "true" if s.drift else "false", oc, os_, l0, l1))
    L.append("const Standby STANDBY[STANDBY_COUNT] = {" + ",".join(rows) + "};")

    # sayings
    for lang in ("vi", "en"):
        q = json.load(open(os.path.join(ROOT, "i18n", f"{lang}.json"), encoding="utf-8"))["@quotes"]
        q = (q["en"] if isinstance(q, dict) else q).split("|")  # en.json holds the English list with its notes
        H_.append(f"extern const char* const QUOTES_{lang.upper()}[]; constexpr int QUOTES_{lang.upper()}_N = {len(q)};")
        L.append(f"const char* const QUOTES_{lang.upper()}[] = {{" + ",".join(cstr(x) for x in q) + "};")

    for X in (H_, L):
        X += ["", "}  // namespace art"]
    open(OUT_H, "w").write("\n".join(H_) + "\n")
    open(OUT_C, "w").write("\n".join(L) + "\n")

    # previews
    os.makedirs(PREV, exist_ok=True)
    html = ["<!doctype html><meta charset=utf-8><title>EspOS Bot art2</title><style>body{background:#111;color:#ddd;"
            "font:14px system-ui;margin:16px}figure{display:inline-block;margin:6px}img{image-rendering:pixelated;"
            "border:4px solid #222}</style><h1>EspOS Bot art (art2): the looks without text, the standby screens "
            "with sample values</h1>"]
    for group, scenes in prev_frames.items():
        html.append(f"<h2>{group}</h2>")
        for name, fr in scenes.items():
            ims = [Image.eval(as_img(a).convert("L").resize((W * 3, H * 3), Image.NEAREST), lambda x: 235 if x else 6)
                   for a, _ in fr]
            fn = f"{group.replace(' ', '_')}_{name}.gif"
            ims[0].save(os.path.join(PREV, fn), save_all=True, append_images=ims[1:],
                        duration=[max(20, int(ms)) for _, ms in fr], loop=0)
            html.append(f"<figure><img src='{fn}' width=384><figcaption>{name}</figcaption></figure>")
    open(os.path.join(PREV, "preview.html"), "w").write("\n".join(html))
    print(f"{len(frames)} frames: {len(blob) // 1024} KB coded, {len(screens)} standby screens, {len(fields)} fields, "
          f"digit sets {', '.join(set_names)}, fonts F11 {len(g11)} / F16B {len(g16)} / T5 {len(gt5)}")


if __name__ == "__main__":
    main()
