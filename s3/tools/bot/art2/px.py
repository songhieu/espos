#!/usr/bin/env python3
"""Pixel-perfect 1-bit drawing for the 128x64 OLED (art2).

Everything is drawn at 1x, never thresholded from a bigger drawing:
  * filled shapes are sampled at pixel centres (clean, symmetric stair steps);
  * outlines are the 1-px edge of a shape's mask, so every outline is exactly one pixel;
  * 1-px strokes are plotted along the path and cleaned "pixel perfect" (no L-shaped doubles);
  * mid-tones are ordered (Bayer 4x4) dither anchored to the SCREEN, so a still background never shimmers;
  * small details (eyes, noses, sparkles, fish...) are hand-placed ASCII sprites.

A canvas is a numpy bool array [64, 128] (row, column); True = lit.
"""
import math
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFont

W, H = 128, 64
HERE = os.path.dirname(os.path.abspath(__file__))
# (copied from bot-art's art2, oledtest/tools: only this path changed) the s3desk folder: tools/bot/art2 -> ../../..
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
FONTS = os.path.join(ROOT, "data/fonts")
UB = os.path.join(FONTS, "Ubuntu-Regular.ttf")
UBB = os.path.join(FONTS, "Ubuntu-Bold.ttf")
UBV = {UB: os.path.join(FONTS, "Ubuntu-Vietnamese-Regular.ttf"), UBB: os.path.join(FONTS, "Ubuntu-Vietnamese-Bold.ttf")}
TINY5 = os.path.join(HERE, "fonts/Tiny5.ttf")

YY, XX = np.mgrid[0:H, 0:W].astype(float)
XC, YC = XX + 0.5, YY + 0.5  # pixel centres


def new(h=H, w=W):
    return np.zeros((h, w), bool)


def grid(h, w):
    yy, xx = np.mgrid[0:h, 0:w].astype(float)
    return xx + 0.5, yy + 0.5


# ------------------------------------------------------------------ shapes (masks)
def ell(cx, cy, rx, ry, rot=0.0, shape=(H, W)):
    """Filled ellipse sampled at pixel centres. cx/cy on a pixel edge (an integer) gives an even width."""
    xc, yc = (XC, YC) if shape == (H, W) else grid(*shape)
    dx, dy = xc - cx, yc - cy
    if rot:
        c, s = math.cos(rot), math.sin(rot)
        dx, dy = dx * c + dy * s, -dx * s + dy * c
    return (dx / rx) ** 2 + (dy / ry) ** 2 <= 1.0


def circ(cx, cy, r, shape=(H, W)):
    return ell(cx, cy, r, r, shape=shape)


def rrect(x0, y0, x1, y1, r, shape=(H, W)):
    """Filled rounded rect covering pixels x0..x1, y0..y1 (inclusive) with corner radius r."""
    xc, yc = (XC, YC) if shape == (H, W) else grid(*shape)
    ix0, iy0, ix1, iy1 = x0 + r, y0 + r, x1 + 1 - r, y1 + 1 - r
    dx = np.maximum(np.maximum(ix0 - xc, xc - ix1), 0)
    dy = np.maximum(np.maximum(iy0 - yc, yc - iy1), 0)
    inside = (xc >= x0) & (xc <= x1 + 1) & (yc >= y0) & (yc <= y1 + 1)
    return inside & (dx * dx + dy * dy <= r * r + 0.01)


def rect(x0, y0, x1, y1, shape=(H, W)):
    m = np.zeros(shape, bool)
    x0, x1 = max(0, int(x0)), min(shape[1] - 1, int(x1))
    y0, y1 = max(0, int(y0)), min(shape[0] - 1, int(y1))
    if x1 >= x0 and y1 >= y0:
        m[y0:y1 + 1, x0:x1 + 1] = True
    return m


def poly(pts, shape=(H, W)):
    im = Image.new("1", (shape[1], shape[0]), 0)
    ImageDraw.Draw(im).polygon([(x, y) for x, y in pts], fill=1)
    return np.array(im, bool)


def halfplane(x0, y0, x1, y1):
    """Pixels on the left of the directed line (x0,y0)->(x1,y1)."""
    return (x1 - x0) * (YC - y0) - (y1 - y0) * (XC - x0) < 0


# ------------------------------------------------------------------ morphology
def shift(m, dx, dy, fill=False):
    out = np.full_like(m, fill)
    h, w = m.shape
    xs0, xs1 = max(0, -dx), min(w, w - dx)
    ys0, ys1 = max(0, -dy), min(h, h - dy)
    if xs1 > xs0 and ys1 > ys0:
        out[ys0 + dy:ys1 + dy, xs0 + dx:xs1 + dx] = m[ys0:ys1, xs0:xs1]
    return out


def erode(m, edge_in=False):
    """4-neighbour erosion. edge_in: the screen border counts as inside (no outline along it)."""
    return m & shift(m, 1, 0, edge_in) & shift(m, -1, 0, edge_in) & shift(m, 0, 1, edge_in) & shift(m, 0, -1, edge_in)


def dilate(m, n=1, diag=False):
    for _ in range(n):
        o = m | shift(m, 1, 0) | shift(m, -1, 0) | shift(m, 0, 1) | shift(m, 0, -1)
        if diag:
            o |= shift(m, 1, 1) | shift(m, -1, -1) | shift(m, 1, -1) | shift(m, -1, 1)
        m = o
    return m


def edge(m, edge_in=True):
    """The 1-px inner outline of a mask (8-connected, no doubles)."""
    return m & ~erode(m, edge_in)


def outer(m):
    """The 1-px ring just outside a mask (a black gap or a halo)."""
    return dilate(m) & ~m


def rim(m, dx, dy):
    """The part of a mask within (dx, dy) of its edge on that side: form shadow away from a top-left light."""
    return m & ~shift(m, -dx, -dy)


# ------------------------------------------------------------------ dither and patterns (anchored to the screen)
BAYER = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]])
BAY = BAYER[(YY.astype(int)) % 4, (XX.astype(int)) % 4]


def dith(level, phase=(0, 0)):
    """Ordered dither, level 0 (black) .. 16 (white); 8 is a checkerboard."""
    if phase != (0, 0):
        return BAYER[(YY.astype(int) + phase[1]) % 4, (XX.astype(int) + phase[0]) % 4] < level
    return BAY < level


def hatch(step=3, slope=1, phase=0):
    """Diagonal lines every `step` pixels (slope 1: '/', -1: '\\')."""
    return ((XX.astype(int) + slope * YY.astype(int) + phase) % step) == 0


def hlines(step=2, phase=0):
    return (YY.astype(int) + phase) % step == 0


def vlines(step=2, phase=0):
    return (XX.astype(int) + phase) % step == 0


# ------------------------------------------------------------------ strokes
def _plot_path(pts):
    out = []
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        n = int(max(abs(x1 - x0), abs(y1 - y0)) * 3) + 1
        for i in range(n + 1):
            t = i / n
            p = (int(math.floor(x0 + (x1 - x0) * t)), int(math.floor(y0 + (y1 - y0) * t)))
            if not out or out[-1] != p:
                out.append(p)
    # pixel perfect: drop the corner of any L (the previous and next pixels touch diagonally)
    clean = []
    for p in out:
        if len(clean) >= 2:
            a, b = clean[-2], clean[-1]
            if abs(a[0] - p[0]) == 1 and abs(a[1] - p[1]) == 1 and (abs(b[0] - a[0]) + abs(b[1] - a[1]) == 1):
                clean.pop()
        if not clean or clean[-1] != p:
            clean.append(p)
    return clean


def path1(pts, shape=(H, W)):
    """A clean 1-px stroke through points given in pixel-centre coordinates (x + 0.5 is a pixel's middle)."""
    m = np.zeros(shape, bool)
    for x, y in _plot_path(pts):
        if 0 <= x < shape[1] and 0 <= y < shape[0]:
            m[y, x] = True
    return m


def thick(pts, w, shape=(H, W)):
    """A stroke of width w (>= 1.5): pixel centres within w/2 of the path."""
    xc, yc = (XC, YC) if shape == (H, W) else grid(*shape)
    d = np.full(shape, 1e9)
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        vx, vy = x1 - x0, y1 - y0
        L2 = vx * vx + vy * vy or 1e-9
        t = np.clip(((xc - x0) * vx + (yc - y0) * vy) / L2, 0, 1)
        px_, py_ = x0 + t * vx, y0 + t * vy
        d = np.minimum(d, np.hypot(xc - px_, yc - py_))
    return d <= w / 2


def taper(x0, y0, r0, x1, y1, r1):
    """A tapered capsule from a circle (x0, y0, r0) to a circle (x1, y1, r1): ears, tails, drops."""
    vx, vy = x1 - x0, y1 - y0
    L2 = vx * vx + vy * vy or 1e-9
    t = np.clip(((XC - x0) * vx + (YC - y0) * vy) / L2, 0, 1)
    d = np.hypot(XC - (x0 + t * vx), YC - (y0 + t * vy))
    return (d <= r0 + (r1 - r0) * t) | circ(x1, y1, r1) | circ(x0, y0, r0)


def arc_pts(cx, cy, rx, ry, a0, a1, n=None):
    """Points along an elliptical arc, angles in degrees (0 = right, 90 = down)."""
    n = n or max(8, int(abs(a1 - a0) / 360 * 2 * math.pi * max(rx, ry) * 1.5))
    return [(cx + rx * math.cos(math.radians(a0 + (a1 - a0) * i / n)), cy + ry * math.sin(math.radians(a0 + (a1 - a0) * i / n)))
            for i in range(n + 1)]


def ring(cx, cy, rx, ry=None, shape=(H, W)):
    """A 1-px ellipse outline (the edge of the filled ellipse)."""
    return edge(ell(cx, cy, rx, ry or rx, shape=shape), edge_in=False)


# ------------------------------------------------------------------ sprites
def sprite(rows):
    """ASCII art -> (on, opaque) masks. '#' lit, '.' black, ' ' transparent, 'o' 50% dither, '-' 25% dither."""
    h, w = len(rows), max(len(r) for r in rows)
    on = np.zeros((h, w), np.int8)
    for y, r in enumerate(rows):
        for x, ch in enumerate(r):
            on[y, x] = {"#": 1, ".": 0, " ": -1, "o": 2, "-": 3, "+": 4}.get(ch, -1)
    return on


def blit(a, spr, x, y, flip=False, invert=False):
    """Draws a sprite (from sprite()) with its top left at x, y; dither codes use the screen's pattern."""
    s = spr[:, ::-1] if flip else spr
    h, w = s.shape
    for j in range(h):
        yy = y + j
        if not 0 <= yy < a.shape[0]:
            continue
        for i in range(w):
            xx = x + i
            if not 0 <= xx < a.shape[1]:
                continue
            v = s[j, i]
            if v < 0:
                continue
            if v == 2:
                on = BAYER[yy % 4, xx % 4] < 8
            elif v == 3:
                on = BAYER[yy % 4, xx % 4] < 4
            elif v == 4:
                on = BAYER[yy % 4, xx % 4] < 12
            else:
                on = bool(v)
            a[yy, xx] = (not on) if invert else on


def spr_mask(spr, x, y, shape=(H, W), flip=False):
    """Where a sprite is opaque, as a screen mask."""
    s = spr[:, ::-1] if flip else spr
    m = np.zeros(shape, bool)
    h, w = s.shape
    for j in range(h):
        for i in range(w):
            if s[j, i] >= 0 and 0 <= y + j < shape[0] and 0 <= x + i < shape[1]:
                m[y + j, x + i] = True
    return m


# ------------------------------------------------------------------ text
_cm = {}
_fonts = {}


def font(path, px, var=None):
    k = (path, px, tuple(var) if var else None)
    if k not in _fonts:
        f = ImageFont.truetype(path, px)
        if var:
            f.set_variation_by_axes(list(var))
        _fonts[k] = f
    return _fonts[k]


def _has(path, ch):
    if path not in _cm:
        from fontTools.ttLib import TTFont
        _cm[path] = set(TTFont(path).getBestCmap().keys())
    return ord(ch) in _cm[path]


def _runs(s, path):
    out = []
    for ch in s:
        p = path if (path not in UBV or _has(path, ch)) else UBV[path]
        if out and out[-1][0] == p:
            out[-1][1] += ch
        else:
            out.append([p, ch])
    return out


def text_w(s, path=UB, px=11):
    return sum(font(p, px).getlength(r) for p, r in _runs(s, path))


def _text_img(s, path, px):
    f = font(path, px)
    asc, desc = f.getmetrics()
    w = int(math.ceil(text_w(s, path, px))) + 4
    im = Image.new("1", (w, asc + desc + 4), 0)
    d = ImageDraw.Draw(im)
    d.fontmode = "1"
    x = 1
    for p, r in _runs(s, path):
        fr = font(p, px)
        d.text((x, asc + 2), r, font=fr, fill=1, anchor="ls")
        x += fr.getlength(r)
    return np.array(im, bool), asc


def text(a, x, y, s, path=UB, px=11, anchor="l", color=True, clip=None):
    """Ubuntu (with every Vietnamese letter merged from the split font). y = the top of the line box
    (the baseline is y + ascent). anchor l / m / r on x."""
    if not s:
        return
    m, asc = _text_img(s, path, px)
    tw = text_w(s, path, px)
    if anchor == "m":
        x -= tw / 2
    elif anchor == "r":
        x -= tw
    put(a, m, int(round(x)) - 1, y - 2, color, clip)


def put(a, m, x, y, color=True, clip=None):
    h, w = m.shape
    for j in range(h):
        yy = y + j
        if not 0 <= yy < a.shape[0]:
            continue
        if clip and not clip[1] <= yy <= clip[3]:
            continue
        row = m[j]
        for i in np.nonzero(row)[0]:
            xx = x + i
            if 0 <= xx < a.shape[1] and (not clip or clip[0] <= xx <= clip[2]):
                a[yy, xx] = color


def wrap(s, width, path=UB, px=11, measure=None):
    """Lines no wider than width, broken at spaces (never shortened)."""
    measure = measure or (lambda t: text_w(t, path, px))
    lines, cur = [], ""
    for w in s.split():
        t = (cur + " " + w).strip()
        if measure(t) <= width or not cur:
            cur = t
        else:
            lines.append(cur)
            cur = w
    if cur:
        lines.append(cur)
    return lines


# Tiny5: a real 5-px pixel font (OFL) with every Vietnamese letter; drawn at 8 px it is exactly on the grid.
def tiny_w(s, wdth=None):
    f = font(TINY5, 8)
    return int(round(f.getlength(s)))


def tiny(a, x, y, s, anchor="l", color=True, clip=None):
    """Tiny5 8 px. y = the top of the capitals (marks over capitals sit 3 rows above y). Returns the width."""
    if not s:
        return 0
    f = font(TINY5, 8)
    w = tiny_w(s)
    im = Image.new("1", (w + 4, 16), 0)
    d = ImageDraw.Draw(im)
    d.fontmode = "1"
    d.text((1, 9), s, font=f, fill=1, anchor="ls")
    m = np.array(im, bool)
    if anchor == "m":
        x -= w // 2
    elif anchor == "r":
        x -= w
    # baseline at row 9 of m; capitals are 5 rows: 4..8 -> y..y+4
    put(a, m, x - 1, y - 4, color, clip)
    return w


# ------------------------------------------------------------------ glyph sets (digits made for the grid)
class Glyphs:
    """Bitmaps for a small alphabet ("0123456789:" + extras), all one height, with a fixed gap after each."""

    def __init__(self, name, glyphs, gap=0, desc=""):
        self.name, self.g, self.gap, self.desc = name, glyphs, gap, desc
        self.h = max(v.shape[0] for v in glyphs.values())

    def w(self, s):
        return sum(self.g[c].shape[1] + self.gap for c in s if c in self.g) - (self.gap if s else 0)

    def draw(self, a, s, x, y, align="l", color=True, skip=""):
        if align == "m":
            x -= self.w(s) // 2
        elif align == "r":
            x -= self.w(s)
        for c in s:
            if c not in self.g:
                continue
            g = self.g[c]
            if c not in skip:
                put(a, g, x, y, color)
            x += g.shape[1] + self.gap
        return x


def from_rows(d):
    return {k: np.array([[ch == "#" for ch in r] for r in v], bool) for k, v in d.items()}


def scale_glyphs(rows, sx, sy=None, block=None):
    """Grid glyphs scaled up; block(sx, sy) -> bool mask of one cell (rounded dots, squares...)."""
    sy = sy or sx
    out = {}
    for k, v in rows.items():
        h, w = len(v), len(v[0])
        g = np.zeros((h * sy, w * sx), bool)
        for j, r in enumerate(v):
            for i, ch in enumerate(r):
                if ch == "#":
                    g[j * sy:(j + 1) * sy, i * sx:(i + 1) * sx] = block if block is not None else True
        out[k] = g
    return out


# ------------------------------------------------------------------ output
def img(a):
    return Image.fromarray(a.astype(np.uint8) * 255).convert("1")


def pack(a):
    """SSD1306 page order: 8 rows a byte, LSB on top."""
    out = bytearray(1024)
    for page in range(8):
        blk = a[page * 8:page * 8 + 8, :]
        vals = np.zeros(W, np.uint16)
        for b in range(8):
            vals |= blk[b].astype(np.uint16) << b
        out[page * 128:(page + 1) * 128] = bytes(vals.astype(np.uint8))
    return bytes(out)


def rle(raw):
    """0x80|n: the next byte n times (n 3..127); n (1..127): n literal bytes."""
    out = bytearray()
    lit = bytearray()
    i = 0
    while i < len(raw):
        j = i
        while j < len(raw) and j - i < 127 and raw[j] == raw[i]:
            j += 1
        if j - i >= 3:
            if lit:
                out += bytes([len(lit)]) + lit
                lit = bytearray()
            out += bytes([0x80 | (j - i), raw[i]])
            i = j
        else:
            lit.append(raw[i])
            i += 1
            if len(lit) == 127:
                out += bytes([len(lit)]) + lit
                lit = bytearray()
    if lit:
        out += bytes([len(lit)]) + lit
    return bytes(out)


def zoom(a, k=3, on=(236, 236, 236), off=(8, 8, 8), grid_=False):
    rgb = np.where(a[..., None], np.array(on, np.uint8), np.array(off, np.uint8)).astype(np.uint8)
    im = Image.fromarray(rgb, "RGB").resize((a.shape[1] * k, a.shape[0] * k), Image.NEAREST)
    return im


# ------------------------------------------------------------------ easing
def ease(t):
    t = max(0.0, min(1.0, t))
    return t * t * (3 - 2 * t)


def ease_out(t):
    t = max(0.0, min(1.0, t))
    return 1 - (1 - t) ** 3


def back_out(t, s=1.7):
    t = max(0.0, min(1.0, t)) - 1
    return t * t * ((s + 1) * t + s) + 1


def lerp(a, b, t):
    return a + (b - a) * t


def keys(tl, t, fn=ease):
    """tl: [(time, value)], values numbers or tuples, eased between keys."""
    if t <= tl[0][0]:
        return tl[0][1]
    for (t0, v0), (t1, v1) in zip(tl, tl[1:]):
        if t0 <= t <= t1:
            k = fn((t - t0) / (t1 - t0)) if t1 > t0 else 1
            if isinstance(v0, tuple):
                return tuple(lerp(p, q, k) for p, q in zip(v0, v1))
            return lerp(v0, v1, k)
    return tl[-1][1]


def blink_at(t, starts, dur=0.2):
    """0..1..0 around each blink start."""
    for t0 in starts:
        if t0 <= t < t0 + dur:
            return math.sin((t - t0) / dur * math.pi)
    return 0.0


def frames_for(seconds, fn, ms=40):
    n = max(1, int(round(seconds * 1000 / ms)))
    return [(fn(i * ms / 1000.0), ms) for i in range(n)]
