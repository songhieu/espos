#!/usr/bin/env python3
"""(Copied from the screen-test project oledtest/tools/bot.py, the screens the owner saw on the board; paths fixed.)
AI-dog bot screens for the 128x64 mono OLED: renders animations into src/bot.h + preview sheet/GIF.

Shapes are drawn 4x larger and thresholded down (smooth curves on a 1-bit panel); text is drawn at
panel size without smoothing (crisp hinting). Each scene is a list of (frame, ms); the firmware plays
them. Clock screens get a background here and live digits from the firmware.
"""
import math
import os
from PIL import Image, ImageDraw, ImageFont

W, H, S = 128, 64, 4
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))  # s3desk
XT = os.path.dirname(ROOT)
UB = os.path.join(ROOT, "data/fonts/Ubuntu-Regular.ttf")
UBB = os.path.join(ROOT, "data/fonts/Ubuntu-Bold.ttf")
UBV = {UB: os.path.join(ROOT, "data/fonts/Ubuntu-Vietnamese-Regular.ttf"),
       UBB: os.path.join(ROOT, "data/fonts/Ubuntu-Vietnamese-Bold.ttf")}
_cm = {}


def has(path, ch):
    if path not in _cm:
        from fontTools.ttLib import TTFont
        _cm[path] = set(TTFont(path).getBestCmap().keys())
    return ord(ch) in _cm[path]


def runs_of(s, path):
    out = []
    for ch in s:
        p = path if has(path, ch) or path not in UBV else UBV[path]
        if out and out[-1][0] == p:
            out[-1][1] += ch
        else:
            out.append([p, ch])
    return out


def text_w(s, path, px):
    return sum(ImageFont.truetype(p, px).getlength(r) for p, r in runs_of(s, path))


def text(im, x, y, s, path=UB, px=11, anchor=None, fill=1):
    d = ImageDraw.Draw(im)
    d.fontmode = "1"
    if anchor == "mt":
        x -= text_w(s, path, px) / 2
    elif anchor == "rt":
        x -= text_w(s, path, px)
    base = y + ImageFont.truetype(path, px).getmetrics()[0]
    for p, r in runs_of(s, path):
        f = ImageFont.truetype(p, px)
        d.text((x, base), r, font=f, fill=fill, anchor="ls")
        x += f.getlength(r)


def wrap(s, path, px, width):
    lines, cur = [], ""
    for w in s.split():
        t = (cur + " " + w).strip()
        if text_w(t, path, px) <= width:
            cur = t
        else:
            lines.append(cur)
            cur = w
    if cur:
        lines.append(cur)
    return lines


class Big:
    """A 4x canvas for shapes; .out() thresholds it to the panel."""

    def __init__(self):
        self.im = Image.new("L", (W * S, H * S), 0)
        self.d = ImageDraw.Draw(self.im)

    def P(self, pts):
        return [(x * S, y * S) for x, y in pts]

    def ell(self, cx, cy, rx, ry, fill=255, rot=0.0, width=0):
        pts = []
        for i in range(48):
            t = 2 * math.pi * i / 48
            x, y = rx * math.cos(t), ry * math.sin(t)
            c, s = math.cos(rot), math.sin(rot)
            pts.append((cx + x * c - y * s, cy + x * s + y * c))
        if width:
            self.d.polygon(self.P(pts), outline=fill, width=int(width * S))
        else:
            self.d.polygon(self.P(pts), fill=fill)

    def rrect(self, x0, y0, x1, y1, r, fill=255, width=0):
        r = min(r, (x1 - x0) / 2, (y1 - y0) / 2)
        if width:
            self.d.rounded_rectangle([x0 * S, y0 * S, x1 * S, y1 * S], radius=r * S, outline=fill, width=int(width * S))
        else:
            self.d.rounded_rectangle([x0 * S, y0 * S, x1 * S, y1 * S], radius=r * S, fill=fill)

    def arc(self, cx, cy, r, a0, a1, width=2.0, fill=255):
        self.d.arc([(cx - r) * S, (cy - r) * S, (cx + r) * S, (cy + r) * S], a0, a1, fill=fill, width=int(width * S))

    def line(self, pts, width=2.0, fill=255):
        self.d.line(self.P(pts), fill=fill, width=int(width * S), joint="curve")
        for x, y in (pts[0], pts[-1]):
            self.ell(x, y, width / 2, width / 2, fill=fill)

    def poly(self, pts, fill=255):
        self.d.polygon(self.P(pts), fill=fill)

    def out(self):
        return self.im.resize((W, H), Image.BOX).point(lambda v: 255 if v >= 110 else 0).convert("1")


def heart(b, cx, cy, r):
    b.ell(cx - r * 0.5, cy - r * 0.2, r * 0.55, r * 0.55)
    b.ell(cx + r * 0.5, cy - r * 0.2, r * 0.55, r * 0.55)
    b.poly([(cx - r * 1.02, cy), (cx + r * 1.02, cy), (cx, cy + r * 1.1)])


def face(b, cx=64, cy=34, k=1.0, look=(0.0, 0.0), blink=0.0, eyes="open", mouth="smile", ears="down",
         ear_twitch=0.0, brows=None, tongue=False):
    """The dog: floppy ears, big eyes, nose and a 'w' mouth. Units are panel pixels at k = 1."""
    X = lambda v: cx + v * k
    Y = lambda v: cy + v * k
    # ears
    for sgn in (-1, 1):
        if ears == "down":
            b.ell(X(sgn * 50), Y(-4 + (ear_twitch if sgn > 0 else 0)), 9 * k, 22 * k, rot=sgn * -0.28)
        elif ears == "up":
            b.ell(X(sgn * 50), Y(-10), 8 * k, 20 * k, rot=sgn * 0.75)
        elif ears == "flat":
            b.ell(X(sgn * 52), Y(2), 9 * k, 22 * k, rot=sgn * -0.55)
    # eyes
    for sgn in (-1, 1):
        ex, ey = X(sgn * 24), Y(-8)
        if eyes == "open":
            h = max(0.12, 1.0 - blink) * 13 * k
            b.rrect(ex - 11 * k, ey - h, ex + 11 * k, ey + h, 9 * k)
            if blink < 0.6:
                px, py = ex + look[0] * 4.5 * k, ey + look[1] * 5 * k
                pr = 6 * k * (1.0 - blink)
                b.ell(px, py, pr, pr, fill=0)
                b.ell(px - 2.2 * k, py - 2.4 * k, 1.9 * k, 1.9 * k)
        elif eyes == "wide":
            b.rrect(ex - 12 * k, ey - 15 * k, ex + 12 * k, ey + 15 * k, 11 * k)
            px, py = ex + look[0] * 4 * k, ey + look[1] * 5 * k
            b.ell(px, py, 7 * k, 7 * k, fill=0)
            b.ell(px - 2.6 * k, py - 2.8 * k, 2.2 * k, 2.2 * k)
        elif eyes == "happy":  # ^ ^
            b.arc(ex, ey + 6 * k, 10 * k, 200, 340, width=3.2 * k)
        elif eyes == "sleep":  # closed, curved down
            b.arc(ex, ey - 6 * k, 10 * k, 30, 150, width=3.0 * k)
        elif eyes == "sad":
            b.rrect(ex - 10 * k, ey - 9 * k, ex + 10 * k, ey + 11 * k, 8 * k)
            b.ell(ex + look[0] * 4 * k, ey + 4 * k, 5 * k, 5 * k, fill=0)
            # droopy lid cut
            b.poly([(ex - 12 * k, ey - 11 * k), (ex + 12 * k, ey - 11 * k),
                    (ex + 12 * k * sgn, ey - 1 * k)], fill=0)
    if brows == "think":
        b.line([(X(14), Y(-27)), (X(34), Y(-30))], width=2.4 * k)
    # nose
    nx, ny = X(0), Y(11)
    b.rrect(nx - 7 * k, ny - 4 * k, nx + 7 * k, ny + 4 * k, 4 * k)
    b.poly([(nx - 5 * k, ny + 2 * k), (nx + 5 * k, ny + 2 * k), (nx, ny + 7 * k)])
    b.ell(nx - 2.5 * k, ny - 1.5 * k, 1.6 * k, 1.2 * k, fill=0)
    # mouth
    if mouth == "smile":
        b.line([(X(0), Y(17)), (X(0), Y(19))], width=2 * k)
        b.arc(X(-6), Y(18), 6 * k, 10, 170, width=2 * k)
        b.arc(X(6), Y(18), 6 * k, 10, 170, width=2 * k)
    elif mouth == "open":
        b.ell(X(0), Y(23), 9 * k, 7 * k)
        b.ell(X(0), Y(22), 7 * k, 5 * k, fill=0)
        b.ell(X(0), Y(26), 5 * k, 3 * k)
    elif mouth == "o":
        b.ell(X(0), Y(22), 4 * k, 4 * k, width=2 * k)
    elif mouth == "flat":
        b.line([(X(-6), Y(21)), (X(6), Y(21))], width=2 * k)
    elif mouth == "frown":
        b.arc(X(0), Y(28), 8 * k, 215, 325, width=2 * k)
    if tongue:
        b.rrect(X(-5), Y(20), X(5), Y(31), 5 * k)
        b.line([(X(0), Y(22)), (X(0), Y(27))], width=1.2 * k, fill=0)


def zzz(im, t):
    for i, (sz, dx) in enumerate(((8, 0), (10, 8), (13, 18))):
        ph = (t + i / 3.0) % 1.0
        x = 78 + dx + ph * 4
        y = 12 - i * 5 - ph * 6
        if ph < 0.85:
            text(im, x, y, "z", UBB, sz)


def compose(big, overlay=None):
    im = big.out()
    if overlay:
        overlay(im)
    return im


# ---------------------------------------------------------------- scenes
def sc_sleep():
    frames = []
    for i in range(24):
        t = i / 24
        b = Big()
        breathe = math.sin(t * 2 * math.pi) * 1.2
        face(b, cy=36 + breathe, eyes="sleep", mouth="flat", ears="flat")
        frames.append((compose(b, lambda im, t=t: zzz(im, t)), 120))
    return frames


def sc_wake():
    seq = []
    for i, bl in enumerate((0.9, 0.6, 0.3, 0.0)):
        b = Big()
        face(b, eyes="open", blink=bl, ears="down" if i < 2 else "up", mouth="smile")
        seq.append((b.out(), 90))
    return seq


def sc_listen():
    frames = []
    for i in range(16):
        t = i / 16
        b = Big()
        face(b, eyes="wide", look=(0, -0.2), ears="up", mouth="smile")
        for sgn in (-1, 1):
            for j in range(3):
                ph = (t * 2 + j / 3) % 1.0
                r = 6 + ph * 14
                cx = 64 + sgn * 52
                if ph < 0.8:
                    a0, a1 = (150, 210) if sgn < 0 else (-30, 30)
                    b.arc(cx - sgn * 8, 46, r, a0, a1, width=2)
        frames.append((b.out(), 70))
    return frames


def sc_think():
    frames = []
    for i in range(18):
        t = i / 18
        b = Big()
        face(b, look=(0.8, -0.8), ears="down", mouth="flat")
        n = int(t * 4) % 4
        for j in range(3):
            if j < n:
                b.ell(52 + j * 12, 4, 2.4, 2.4)
            else:
                b.ell(52 + j * 12, 4, 2.4, 2.4, width=1)
        frames.append((b.out(), 100))
    return frames


TALK = "Hà Nội hôm nay nắng nhẹ, 28 độ. Chiều có mưa rào nên nhớ mang áo mưa nha! Tối mát, 22 độ, ngủ ngon lắm đó."


def sc_talk(msg=TALK, mood="smile"):
    lines = wrap(msg, UB, 11, 65)
    lh = 13
    total = len(lines) * lh
    frames = []
    # hold 0.8 s, scroll 1 px a frame, hold 1.2 s at the end
    offs = [0] * 8 + list(range(0, max(0, total - 60) + 1)) + [max(0, total - 60)] * 12
    for i, off in enumerate(offs):
        b = Big()
        face(b, cx=28, cy=34, k=0.46, mouth="open" if (i // 2) % 2 == 0 and i < len(offs) - 12 else mood,
             ears="down", look=(0.2, 0))
        im = b.out()
        d = ImageDraw.Draw(im)
        d.line([(57, 6), (57, 58)], fill=1)
        d.polygon([(57, 30), (53, 33), (57, 36)], fill=1)
        for j, ln in enumerate(lines):
            y = 2 + j * lh - off
            if -lh < y < H:
                text(im, 62, y, ln, UB, 11)
        frames.append((im, 90))
    return frames


def sc_happy():
    frames = []
    for i in range(16):
        t = i / 16
        b = Big()
        bounce = -abs(math.sin(t * 2 * math.pi)) * 2.5
        face(b, cy=35 + bounce, eyes="happy", mouth="smile", ears="up", tongue=True)
        for j, (hx, base) in enumerate(((10, 0.0), (118, 0.5))):
            ph = (t + base) % 1.0
            heart(b, hx + math.sin(ph * 6) * 2, 54 - ph * 44, 4.2)
        frames.append((b.out(), 80))
    return frames


def sc_sad():
    frames = []
    for i in range(12):
        t = i / 12
        b = Big()
        face(b, eyes="sad", look=(math.sin(t * 2 * math.pi) * 0.6, 0), mouth="frown", ears="flat")
        frames.append((b.out(), 110))
    return frames


def sc_idle_eyes():
    """Full face, alive: looks around, blinks, an ear twitches."""
    keys = [(0, 0)] * 6 + [(-1, 0)] * 8 + [(0, 0)] * 4 + [(1, -0.5)] * 8 + [(0, 0)] * 6 + [(0.3, 0.7)] * 6
    frames = []
    for i, lk in enumerate(keys):
        bl = {12: 0.5, 13: 1.0, 14: 0.5, 30: 0.5, 31: 1.0, 32: 0.5}.get(i, 0.0)
        tw = {20: -4, 21: -6, 22: -3}.get(i, 0)
        b = Big()
        face(b, look=lk, blink=bl, ears="down", ear_twitch=tw, mouth="smile")
        frames.append((b.out(), 110))
    return frames


# clock backgrounds: the firmware writes the digits (see CLOCKS in bot.h)
def bg_clock_big():
    im = Image.new("1", (W, H), 0)
    text(im, 64, 48, "Thứ Năm 20/11  ·  28°", UB, 11, anchor="mt")
    return im


def bg_clock_dog(peek):
    b = Big()
    # the dog peeks over the bottom edge at the right
    face(b, cx=100, cy=66, k=0.62, look=peek[0], blink=peek[1], ears="down", mouth="smile")
    im = b.out()
    text(im, 2, 40, "Thứ Năm", UB, 11)
    text(im, 2, 52, "20 tháng 11", UB, 11)
    return im


def sc_weather():
    frames = []
    for i in range(12):
        t = i / 12
        b = Big()
        cx, cy = 26, 28
        b.ell(cx, cy, 10, 10)
        for j in range(8):
            a = j * math.pi / 4 + t * math.pi / 4
            r0, r1 = 14, 19
            b.line([(cx + r0 * math.cos(a), cy + r0 * math.sin(a)), (cx + r1 * math.cos(a), cy + r1 * math.sin(a))], width=2.4)
        im = b.out()
        text(im, 54, -2, "28°", UBB, 32)
        text(im, 56, 34, "nắng nhẹ", UB, 11)
        text(im, 2, 51, "Hà Nội  ·  chiều 31° mưa", UB, 11)
        frames.append((im, 120))
    return frames


def title(n, total, name):
    im = Image.new("1", (W, H), 0)
    text(im, 64, 14, f"{n}/{total}", UB, 11, anchor="mt")
    text(im, 64, 30, name, UBB, 14, anchor="mt")
    return im


def glyphs(px, chars="0123456789:"):
    out = {}
    for ch in chars:
        f = ImageFont.truetype(UBB, px)
        w = int(math.ceil(f.getlength(ch)))
        asc, desc = f.getmetrics()
        im = Image.new("1", (w, asc), 0)
        d = ImageDraw.Draw(im)
        d.fontmode = "1"
        d.text((0, asc), ch, font=f, fill=1, anchor="ls")
        out[ch] = im
    return out


SCENES = [
    ("Ngủ", "sleep", lambda: sc_sleep()),
    ("Gọi \"gâu gâu\"", "wake", lambda: sc_wake() + sc_listen() * 3),
    ("Đang nghĩ", "think", lambda: sc_think() * 2),
    ("Trả lời", "talk", lambda: sc_talk()),
    ("Vui", "happy", lambda: sc_happy() * 2),
    ("Chưa nghe rõ", "sad", lambda: sc_sad() * 2 + sc_talk("Mình chưa nghe rõ, bạn nói lại nha!", "frown")[:40]),
    ("Mắt chó", "idle", lambda: sc_idle_eyes() * 2),
    ("Thời tiết", "weather", lambda: sc_weather() * 4),
]
CLOCKS = [("Đồng hồ lớn", "clock_big"), ("Đồng hồ + chó", "clock_dog")]


def pack(im):
    px = im.load()
    out = bytearray(1024)
    for y in range(H):
        for x in range(W):
            if px[x, y]:
                out[(y >> 3) * 128 + x] |= 1 << (y & 7)
    return bytes(out)


def main():
    frames, index = [], {}

    def fid(im):
        b = pack(im)
        if b not in index:
            index[b] = len(frames)
            frames.append(b)
        return index[b]

    total = len(SCENES) + len(CLOCKS)
    seqs = []
    previews = []
    for n, (name, key, fn) in enumerate(SCENES, 1):
        fr = fn()
        seqs.append((key, fid(title(n, total, name)), [(fid(im), ms) for im, ms in fr]))
        previews.append((name, fr))
    # clock backgrounds
    big_bg = fid(bg_clock_big())
    dog_peeks = [((0, 0), 0), ((-0.8, -0.6), 0), ((0, 0), 0.6), ((0, 0), 1.0), ((0.8, -0.6), 0)]
    dog_bgs = [fid(bg_clock_dog(p)) for p in dog_peeks]
    titles = [fid(title(len(SCENES) + i + 1, total, nm)) for i, (nm, _) in enumerate(CLOCKS)]
    g40, g26 = glyphs(40), glyphs(28)

    L = ["// Made by tools/bot.py: do not edit.", "#pragma once", "#include <stdint.h>",
         f"#define BOT_FRAMES {len(frames)}", "static const uint8_t BOT_FRAME[BOT_FRAMES][1024] = {"]
    for b in frames:
        L.append("{" + ",".join(str(v) for v in b) + "},")
    L.append("};")
    L.append("struct BotStep { uint16_t frame, ms; };")
    for key, t, steps in seqs:
        L.append(f"static const BotStep SEQ_{key}[] = {{" + ",".join(f"{{{f},{ms}}}" for f, ms in steps) + "};")
    L.append("struct BotSeq { const char* name; uint16_t title; const BotStep* steps; uint16_t n; };")
    L.append(f"#define BOT_SEQS {len(seqs)}")
    L.append("static const BotSeq BOT_SEQ[BOT_SEQS] = {" +
             ",".join(f'{{"{k}",{t},SEQ_{k},{len(s)}}}' for k, t, s in seqs) + "};")
    L.append(f"static const uint16_t CLOCK_BIG_BG = {big_bg};")
    L.append(f"static const uint16_t CLOCK_DOG_BG[{len(dog_bgs)}] = {{" + ",".join(map(str, dog_bgs)) + "};")
    L.append(f"static const uint16_t CLOCK_TITLE[{len(titles)}] = {{" + ",".join(map(str, titles)) + "};")
    for nm, g in (("G40", g40), ("G26", g26)):
        L.append(f"// glyphs {nm}: 0-9 then ':'; rows of bits, MSB left")
        for ch, im in g.items():
            w, h = im.size
            bpr = (w + 7) // 8
            data = bytearray(bpr * h)
            p = im.load()
            for y in range(h):
                for x in range(w):
                    if p[x, y]:
                        data[y * bpr + x // 8] |= 0x80 >> (x % 8)
            nm_ch = "c" if ch == ":" else ch
            L.append(f"static const uint8_t {nm}_{nm_ch}[] = {{{w},{h}," + ",".join(map(str, data)) + "};")
        L.append(f"static const uint8_t* const {nm}[11] = {{" + ",".join(f"{nm}_{c}" for c in "0123456789") + f",{nm}_c}};")
    open(os.path.join(ROOT, "src/bot.h"), "w").write("\n".join(L) + "\n")

    # preview: first frames of each scene + a GIF of everything
    os.makedirs(os.path.join(ROOT, "preview"), exist_ok=True)
    cell = (W * 3 + 12, H * 3 + 30)
    allp = previews + [("Đồng hồ lớn", [(bg_clock_big(), 1)]), ("Đồng hồ + chó", [(bg_clock_dog(dog_peeks[0]), 1)])]
    sheet = Image.new("RGB", (cell[0] * 2, cell[1] * ((len(allp) + 1) // 2)), (30, 30, 30))
    sd = ImageDraw.Draw(sheet)
    lab = ImageFont.truetype(UB, 15)
    for i, (name, fr) in enumerate(allp):
        im = fr[min(len(fr) - 1, len(fr) // 3)][0]
        big = Image.eval(im.convert("RGB").resize((W * 3, H * 3), Image.NEAREST), lambda v: 235 if v else 6)
        x, y = (i % 2) * cell[0] + 6, (i // 2) * cell[1] + 24
        sheet.paste(big, (x, y))
        sd.text((x, y - 20), f"{i + 1}. {name}", font=lab, fill=(200, 200, 200))
    sheet.save(os.path.join(ROOT, "preview/bot.png"))
    gif = []
    durs = []
    for name, fr in previews:
        for im, ms in fr:
            gif.append(Image.eval(im.convert("L").resize((W * 3, H * 3), Image.NEAREST), lambda v: 235 if v else 6))
            durs.append(ms)
    gif[0].save(os.path.join(ROOT, "preview/bot.gif"), save_all=True, append_images=gif[1:], duration=durs, loop=0)
    print(f"{len(frames)} frames ({len(frames)} KB), {len(seqs)} scenes")


if __name__ == "__main__":
    main()
