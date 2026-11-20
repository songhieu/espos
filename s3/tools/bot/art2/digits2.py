#!/usr/bin/env python3
"""Digit sets made for the pixel grid (art2). One geometric design (strokes in a unit box) drawn at several sizes
and weights, plus a 7-segment set with dim "ghost" segments and a round-dot matrix set.

Every set has "0123456789:" and the extras "-°%.," where it makes sense; px.Glyphs.draw() lays them out.
"""
import math

import numpy as np

import px

# ------------------------------------------------------------------ the geometric design: strokes in a unit box
def _arc(cx, cy, rx, ry, a0, a1, n=40):
    return [(cx + rx * math.cos(math.radians(a0 + (a1 - a0) * i / n)), cy + ry * math.sin(math.radians(a0 + (a1 - a0) * i / n)))
            for i in range(n + 1)]


def _stadium(x0, y0, x1, y1):
    r = (x1 - x0) / 2
    ry = r * 1.0
    cx = (x0 + x1) / 2
    return (_arc(cx, y0 + ry, r, ry, 180, 360) + [(x1, y1 - ry)] + _arc(cx, y1 - ry, r, ry, 0, 180) + [(x0, y0 + ry)])


STROKES = {
    "0": [_stadium(0, 0, 1, 1)],
    "1": [[(0.18, 0.2), (0.6, 0.0), (0.6, 1.0)]],
    "2": [_arc(0.5, 0.27, 0.5, 0.27, 185, 385) + [(0.0, 1.0), (1.0, 1.0)]],
    "3": [_arc(0.5, 0.25, 0.46, 0.25, 200, 450), _arc(0.5, 0.735, 0.5, 0.265, 270, 520)],
    "4": [[(0.74, 1.0), (0.74, 0.0), (0.0, 0.68), (1.0, 0.68)]],
    "5": [[(0.92, 0.0), (0.1, 0.0), (0.04, 0.47)], _arc(0.5, 0.68, 0.5, 0.32, 222, 520)],
    "6": [_arc(0.5, 0.68, 0.5, 0.32, 0, 360), [(0.8, 0.0), (0.06, 0.6)]],
    "7": [[(0.0, 0.0), (1.0, 0.0), (0.32, 1.0)]],
    "8": [_arc(0.5, 0.235, 0.43, 0.235, 0, 360), _arc(0.5, 0.72, 0.5, 0.28, 0, 360)],
    "9": [_arc(0.5, 0.32, 0.5, 0.32, 0, 360), [(0.94, 0.4), (0.2, 1.0)]],
}


def stroke_set(name, gw, gh, sw, colon_gap=None, gap=None, minus=True):
    """Geometric digits gw x gh pixels, stroke width sw (1 = a clean 1-px line)."""
    out = {}
    for ch, paths in STROKES.items():
        m = np.zeros((gh, gw), bool)
        inset = sw / 2 if sw > 1 else 0.5
        sx, sy = gw - 2 * inset, gh - 2 * inset
        for p in paths:
            pts = [(inset + u * sx, inset + v * sy) for u, v in p]
            if ch == "1":  # the 1 is narrower: centre its stem
                pts = [(inset + (u - 0.6) * sx + gw * 0.45, inset + v * sy) for u, v in p]
            if sw <= 1.01:
                m |= px.path1(pts, shape=(gh, gw))
            else:
                m |= px.thick(pts, sw, shape=(gh, gw))
        out[ch] = m
    # ':' two dots of the stroke's weight, '-' a bar, '°' a ring
    d = max(2, int(round(sw)))
    cw = d + 2 * max(1, int(round(sw * 0.6))) if colon_gap is None else colon_gap
    col = np.zeros((gh, cw), bool)
    for fy in (0.32, 0.72):
        cy = int(round(gh * fy))
        x0 = (cw - d) // 2
        col[cy - d // 2:cy - d // 2 + d, x0:x0 + d] = True
        if d >= 4:  # round the dots
            col[cy - d // 2, x0] = col[cy - d // 2, x0 + d - 1] = col[cy - d // 2 + d - 1, x0] = col[cy - d // 2 + d - 1, x0 + d - 1] = False
    out[":"] = col
    if minus:
        mw = max(3, gw // 2)
        mm = np.zeros((gh, mw), bool)
        y = gh // 2
        mm[y - d // 2:y - d // 2 + d, 0:mw] = True
        out["-"] = mm
    dw = max(4, int(gh * 0.3))
    deg = np.zeros((gh, dw + 1), bool)
    r = dw / 2
    ring_ = px.thick(_arc(r, r, r - max(0.5, sw / 2), r - max(0.5, sw / 2), 0, 360), max(1.0, sw * 0.7), shape=(gh, dw + 1)) \
        if sw > 1.2 else px.path1(_arc(r, r, r - 0.5, r - 0.5, 0, 360), shape=(gh, dw + 1))
    deg |= ring_
    out["°"] = deg
    dotw = max(1, d)
    dot = np.zeros((gh, dotw + 1), bool)
    dot[gh - dotw:gh, 0:dotw] = True
    out["."] = dot
    out[","] = dot
    space = np.zeros((gh, max(2, gw // 3)), bool)
    out[" "] = space
    g = px.Glyphs(name, out, gap if gap is not None else max(2, int(round(sw * 0.9))))
    return g


# ------------------------------------------------------------------ 7 segments with ghost segments
SEG = {"0": "abcdef", "1": "bc", "2": "abged", "3": "abgcd", "4": "fgbc", "5": "afgcd", "6": "afgedc", "7": "abc",
       "8": "abcdefg", "9": "abcdfg", "-": "g", " ": ""}


def seg_masks(gw, gh, t):
    """Hexagonal (bevelled) segments a..g in a gw x gh box, thickness t."""
    h2 = gh // 2
    s = {}
    g = 1  # the gap between segments

    def hseg(y):
        pts = [(g + t / 2, y + t / 2), (g + t, y), (gw - g - t, y), (gw - g - t / 2, y + t / 2), (gw - g - t, y + t), (g + t, y + t)]
        return px.poly(pts, shape=(gh, gw))

    def vseg(x, y0, y1):
        pts = [(x + t / 2, y0 + g + t / 2), (x + t, y0 + g + t), (x + t, y1 - g - t), (x + t / 2, y1 - g - t / 2), (x, y1 - g - t), (x, y0 + g + t)]
        return px.poly(pts, shape=(gh, gw))
    s["a"] = hseg(0)
    s["g"] = hseg(h2 - t / 2)
    s["d"] = hseg(gh - t)
    s["f"] = vseg(0, 0, h2)
    s["b"] = vseg(gw - t, 0, h2)
    s["e"] = vseg(0, h2, gh)
    s["c"] = vseg(gw - t, h2, gh)
    return s


def seg7_set(name, gw=22, gh=40, t=5, ghost=True):
    sm = seg_masks(gw, gh, t)
    allseg = np.zeros((gh, gw), bool)
    for m in sm.values():
        allseg |= m
    yy, xx = np.mgrid[0:gh, 0:gw]
    out = {}
    for ch, segs in SEG.items():
        m = np.zeros((gh, gw), bool)
        for k in segs:
            m |= sm[k]
        if ghost:  # the unlit segments: a faint dotted line down their middle, as on a real LCD
            for k, sm_ in sm.items():
                if k in segs:
                    continue
                core = sm_
                for _ in range(max(1, (t - 1) // 2)):
                    core = core & np.roll(core, 1, 0) & np.roll(core, -1, 0) & np.roll(core, 1, 1) & np.roll(core, -1, 1)
                m |= core & (((xx + yy) % 4) == 0)
        out[ch] = m
    col = np.zeros((gh, 6), bool)
    for fy in (0.3, 0.7):
        cy = int(gh * fy)
        col[cy - 2:cy + 2, 1:5] = True
        col[cy - 2, 1] = col[cy - 2, 4] = col[cy + 1, 1] = col[cy + 1, 4] = False
    out[":"] = col
    return px.Glyphs(name, out, 3)


def seg7_ghost(gw=22, gh=40, t=5):
    sm = seg_masks(gw, gh, t)
    m = np.zeros((gh, gw), bool)
    for v in sm.values():
        m |= v
    return m


# ------------------------------------------------------------------ round-dot matrix (5x7)
PIX = {
    "0": ["01110", "10001", "10011", "10101", "11001", "10001", "01110"],
    "1": ["00100", "01100", "00100", "00100", "00100", "00100", "01110"],
    "2": ["01110", "10001", "00001", "00010", "00100", "01000", "11111"],
    "3": ["11111", "00010", "00100", "00010", "00001", "10001", "01110"],
    "4": ["00010", "00110", "01010", "10010", "11111", "00010", "00010"],
    "5": ["11111", "10000", "11110", "00001", "00001", "10001", "01110"],
    "6": ["00110", "01000", "10000", "11110", "10001", "10001", "01110"],
    "7": ["11111", "00001", "00010", "00100", "01000", "01000", "01000"],
    "8": ["01110", "10001", "10001", "01110", "10001", "10001", "01110"],
    "9": ["01110", "10001", "10001", "01111", "00001", "00010", "01100"],
    ":": ["0", "0", "1", "0", "1", "0", "0"],
    "-": ["00000", "00000", "00000", "11111", "00000", "00000", "00000"],
}


def dots_set(name, pitch=5, dot=4, ghost=True):
    """A 5x7 matrix of round dots; ghost: the unlit dots show as a single faint pixel."""
    out = {}
    disc = np.array([[0, 1, 1, 0], [1, 1, 1, 1], [1, 1, 1, 1], [0, 1, 1, 0]], bool) if dot == 4 else \
        np.array([[0, 1, 0], [1, 1, 1], [0, 1, 0]], bool) if dot == 3 else np.ones((dot, dot), bool)
    for ch, rows in PIX.items():
        w, h = len(rows[0]), len(rows)
        m = np.zeros((h * pitch - (pitch - dot), w * pitch - (pitch - dot)), bool)
        for j, r in enumerate(rows):
            for i, v in enumerate(r):
                if v == "1":
                    m[j * pitch:j * pitch + dot, i * pitch:i * pitch + dot] |= disc
                elif ghost and ch != ":":
                    m[j * pitch + dot // 2, i * pitch + dot // 2 - (1 if dot == 4 else 0)] = True
        out[ch] = m
    return px.Glyphs(name, out, pitch - dot + 1)


# ------------------------------------------------------------------ the sets art2 uses (name -> Glyphs)
_cache = {}


def sets():
    if not _cache:
        _cache.update({
            "bold40": stroke_set("bold40", 23, 40, 6),       # big bold clock
            "bold24": stroke_set("bold24", 14, 24, 4),       # temperatures, day numbers
            "bold16": stroke_set("bold16", 9, 16, 2.6),      # small numbers
            "thin40": stroke_set("thin40", 22, 42, 1, gap=4),  # 1-px elegant clock
            "thin20": stroke_set("thin20", 11, 20, 1, gap=2),
            "seg40": seg7_set("seg40", 21, 40, 5),
            "dots": dots_set("dots", 5, 4, ghost=False),
            "flip30": stroke_set("flip30", 17, 30, 5),
        })
    return _cache
