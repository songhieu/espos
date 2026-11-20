#!/usr/bin/env python3
"""(Copied from the screen-test project oledtest/tools/versions.py: the looks B, C, D the owner saw; gen.py builds the firmware's art from them.)
Several looks for the AI-dog bot on the 128x64 mono OLED, side by side.

A = the first try (tools/bot.py), B = big eyes (Vector-like), C = pixel puppy (Tamagotchi-like),
D = thin line art. Writes src/bot.h for the firmware, preview/<v>.gif and preview/versions.html.
Frames run at 25 a second (40 ms) unless a scene says otherwise.
"""
import math
import os
import random
from PIL import Image, ImageDraw, ImageFont

import botart as bot
from botart import Big, text, text_w, wrap, UB, UBB, W, H, S

ROOT = bot.ROOT
LEX = os.path.join(bot.XT, "brand/tools/Lexend-VF.ttf")
FPS_MS = 40


def lex(px, wght):
    f = ImageFont.truetype(LEX, px)
    f.set_variation_by_axes([wght])
    return f


def ltext(im, x, y, s, px, wght=400, anchor=None, fill=1):
    """Lexend (it has every Vietnamese letter itself)."""
    d = ImageDraw.Draw(im)
    d.fontmode = "1"
    f = lex(px, wght)
    if anchor == "mt":
        x -= f.getlength(s) / 2
    elif anchor == "rt":
        x -= f.getlength(s)
    d.text((x, y + f.getmetrics()[0]), s, font=f, fill=fill, anchor="ls")


def lwrap(s, px, wght, width):
    f = lex(px, wght)
    lines, cur = [], ""
    for w in s.split():
        t = (cur + " " + w).strip()
        if f.getlength(t) <= width:
            cur = t
        else:
            lines.append(cur)
            cur = w
    if cur:
        lines.append(cur)
    return lines


def ease(t):
    t = max(0.0, min(1.0, t))
    return t * t * (3 - 2 * t)


def lerp(a, b, t):
    return a + (b - a) * t


def keys(timeline, t):
    """timeline: [(time_s, value)], value a number or tuple; eased between keys."""
    if t <= timeline[0][0]:
        return timeline[0][1]
    for (t0, v0), (t1, v1) in zip(timeline, timeline[1:]):
        if t0 <= t <= t1:
            k = ease((t - t0) / (t1 - t0)) if t1 > t0 else 1
            if isinstance(v0, tuple):
                return tuple(lerp(a, b, k) for a, b in zip(v0, v1))
            return lerp(v0, v1, k)
    return timeline[-1][1]


def frames_for(seconds, fn, ms=FPS_MS):
    n = max(1, int(round(seconds * 1000 / ms)))
    return [(fn(i * ms / 1000.0), ms) for i in range(n)]


TALK = "Hà Nội hôm nay nắng nhẹ, 28 độ. Chiều có mưa rào, nhớ mang áo mưa nha! Tối mát, ngủ ngon lắm đó."
SORRY = "Mình chưa nghe rõ, bạn nói lại nha!"


# =================================================================== B: big eyes
def eyesB(b, t=0, look=(0, 0), blink=0.0, w=32, h=38, cy=28, gap=14, happy=0.0, sad=0.0, lid=0.0, squash=0.0,
          ears=False, nose=True):
    """Two big rounded eyes. look moves them and makes the near eye a bit bigger."""
    lx, ly = look
    for sgn in (-1, 1):
        persp = 1.0 + (-sgn) * lx * 0.07
        ew = w * persp * (1 + squash * 0.12)
        eh = h * persp * (1 - squash * 0.12) * max(0.08, 1 - blink)
        cx = 64 + sgn * (gap / 2 + w / 2) + lx * 9
        ecy = cy + ly * 7
        x0, y0, x1, y1 = cx - ew / 2, ecy - eh / 2, cx + ew / 2, ecy + eh / 2
        b.rrect(x0, y0, x1, y1, min(11, eh / 2))
        if happy > 0:  # a crescent: cut from below
            b.ell(cx, y1 + eh * 0.15, ew * 0.72, eh * 0.62 * happy, fill=0)
        if sad > 0:  # outer upper corner droops
            o = x0 - 2 if sgn < 0 else x1 + 2
            b.poly([(x0 - 2, y0 - 2), (x1 + 2, y0 - 2), (o, y0 + eh * 0.6 * sad)], fill=0)
        if lid > 0:  # flat upper lid
            b.d.rectangle([(x0 - 2) * S, (y0 - 2) * S, (x1 + 2) * S, (y0 + eh * lid) * S], fill=0)
    if ears:  # little floppy ear tips in the top corners: the dog's hint
        b.ell(4, 4, 7, 13, rot=0.5)
        b.ell(124, 4, 7, 13, rot=-0.5)
    if nose:
        b.rrect(60, 52, 68, 57, 2.5)
        b.poly([(61.5, 56), (66.5, 56), (64, 59.5)])


def zs(im, t, x, y):
    for i, sz in enumerate((7, 9, 11)):
        ph = (t * 0.6 + i / 3.0) % 1.0
        if ph < 0.9:
            text(im, x + i * 6 + ph * 3, y - i * 6 - ph * 8, "z", UBB, sz)


def B_sleep():
    def f(t):
        b = Big()
        br = math.sin(t * 2 * math.pi / 3.0)
        for sgn in (-1, 1):
            cx = 64 + sgn * 23
            b.rrect(cx - 15, 36 + br, cx + 15, 40 + br, 2)
        b.rrect(60, 52 + br * 0.5, 68, 57 + br * 0.5, 2.5)
        im = b.out()
        zs(im, t, 92, 24)
        return im
    return frames_for(6, f)


def B_listen():
    rnd = random.Random(3)
    noise = [rnd.random() for _ in range(400)]

    def level(t, i):
        k = t * 9 + i * 1.7
        a, fr = noise[int(k) % 400], k - int(k)
        return lerp(a, noise[(int(k) + 1) % 400], ease(fr))

    def f(t):
        b = Big()
        open_ = keys([(0, 0.92), (0.25, -0.12), (0.4, 0.0)], t)  # bars open with a little overshoot
        v = sum(level(t, i) for i in range(7)) / 7
        eyesB(b, blink=open_, h=40, cy=26, look=(0, -0.3), squash=-v * 0.4, nose=False)
        if t > 0.35:
            for i in range(7):
                hh = 2 + level(t, i) * 9 * (1 - abs(i - 3) / 5)
                x = 64 + (i - 3) * 6
                b.rrect(x - 1.6, 58 - hh, x + 1.6, 58 + hh * 0.0 + 2, 1.5)
        return b.out()
    return frames_for(4.5, f)


def B_think():
    def f(t):
        b = Big()
        lk = keys([(0, (0, 0)), (0.3, (-0.9, -0.8)), (2.0, (-0.9, -0.8)), (2.3, (0.8, -0.8)), (4, (0.8, -0.8))], t)
        eyesB(b, look=lk, lid=0.22, h=36, cy=27, nose=False)
        for j in range(3):
            ph = (t * 2.2 - j * 0.18) % 1.0
            y = 57 - max(0, math.sin(ph * math.pi * 2)) * 3 if ph < 0.5 else 57
            b.ell(56 + j * 8, y, 2.2, 2.2)
        return b.out()
    return frames_for(4, f)


def talk_layout_B(msg, mood="happy"):
    lines = lwrap(msg, 11, 400, 124)
    lh = 12
    view = 64 - 26
    total = len(lines) * lh
    scroll_end = max(0, total - view + 2)
    hold0, hold1 = 1.2, 1.5
    dur = hold0 + scroll_end / 14 + hold1

    def f(t):
        b = Big()
        talking = t < dur - hold1 + 0.3
        sq = (math.sin(t * 22) * 0.5 + 0.5) * 0.6 if talking else 0
        eyesB(b, w=20, h=18 if mood != "sad" else 14, cy=11, gap=12, squash=sq, ears=False, nose=False,
              happy=0.0 if mood != "happy" or talking else 0.9, sad=0.8 if mood == "sad" else 0)
        im = b.out()
        off = 0 if t < hold0 else min(scroll_end, (t - hold0) * 14)
        txt = Image.new("1", (W, H), 0)
        for j, ln in enumerate(lines):
            y = 25 + j * lh - off
            if 10 < y < H:
                ltext(txt, 2, y, ln, 11, 400)
        mask = Image.new("1", (W, H), 0)
        ImageDraw.Draw(mask).rectangle([0, 24, 127, 63], fill=1)
        im.paste(txt, (0, 0), mask)
        return im
    return frames_for(dur, f)


def B_talk():
    return talk_layout_B(TALK)


def B_happy():
    def f(t):
        b = Big()
        bounce = -abs(math.sin(t * math.pi * 2.2)) * 3
        eyesB(b, happy=1.0, cy=28 + bounce, h=36)
        for j, (sx, sy, ph0) in enumerate(((12, 40, 0.0), (116, 38, 0.5), (108, 14, 0.25))):
            ph = (t * 1.4 + ph0) % 1.0
            r = 4 * math.sin(ph * math.pi)
            if r > 0.6:
                b.poly([(sx, sy - r), (sx + r * 0.3, sy - r * 0.3), (sx + r, sy), (sx + r * 0.3, sy + r * 0.3),
                        (sx, sy + r), (sx - r * 0.3, sy + r * 0.3), (sx - r, sy), (sx - r * 0.3, sy - r * 0.3)])
        return b.out()
    return frames_for(3.2, f)


def B_sad():
    def f(t):
        b = Big()
        lk = keys([(0, (0, 0)), (0.6, (-0.4, 0.8)), (2.0, (0.4, 0.8)), (2.8, (0, 0.8))], t)
        eyesB(b, sad=1.0, look=lk, h=30, cy=30)
        return b.out()
    return frames_for(2.8, f) + talk_layout_B(SORRY, "sad")


def B_idle():
    tl = [(0, (0, 0)), (1.0, (0, 0)), (1.25, (-1, 0)), (2.3, (-1, 0)), (2.55, (0.9, -0.6)), (3.6, (0.9, -0.6)),
          (3.85, (0, 0)), (5.2, (0, 0)), (5.45, (0.3, 0.8)), (6.2, (0.3, 0.8)), (6.45, (0, 0)), (7.5, (0, 0))]

    def blink_at(t):
        for t0 in (0.6, 3.0, 5.0, 5.35, 7.1):
            if t0 <= t < t0 + 0.24:
                return math.sin((t - t0) / 0.24 * math.pi)
        return 0.0

    def f(t):
        b = Big()
        eyesB(b, look=keys(tl, t), blink=blink_at(t), squash=0.25 if 4.2 < t < 4.9 else 0)
        return b.out()
    return frames_for(7.5, f)


def sun(b, cx, cy, r, t, rays=8, stroke=2.2):
    b.ell(cx, cy, r, r)
    for j in range(rays):
        a = j * 2 * math.pi / rays + t * 0.6
        b.line([(cx + (r + 3.5) * math.cos(a), cy + (r + 3.5) * math.sin(a)),
                (cx + (r + 7.5) * math.cos(a), cy + (r + 7.5) * math.sin(a))], width=stroke)


def B_weather():
    def f(t):
        b = Big()
        sun(b, 26, 26, 10, t)
        im = b.out()
        ltext(im, 124, -4, "28°", 34, 600, anchor="rt")
        ltext(im, 124, 34, "nắng nhẹ", 11, 400, anchor="rt")
        ltext(im, 2, 50, "Hà Nội · chiều mưa 31°", 11, 400)
        return im
    return frames_for(4, f)


def B_clock_bg():
    def f(t):
        b = Big()
        bl = 1.0 if 2.0 < (t % 4) < 2.15 else 0.0
        lk = keys([(0, (0, 0)), (1, (0, 0)), (1.2, (-1, 0)), (2.4, (-1, 0)), (2.6, (1, 0)), (3.6, (1, 0)), (3.8, (0, 0)), (4, (0, 0))], t)
        for sgn in (-1, 1):
            cx = 64 + sgn * 7 + lk[0] * 3
            hh = 5.5 * (1 - bl * 0.85)
            b.rrect(cx - 4.5, 56 - hh, cx + 4.5, 56 + hh, 3)
        im = b.out()
        ltext(im, 64, 37, "Thứ Năm, 20 tháng 11", 10, 400, anchor="mt")
        return im
    return frames_for(4, f, ms=80)


# =================================================================== C: pixel puppy
def pup(im, x, y, eyes="open", ears="down", mouth="smile", tail=0, sit=True, blink=False, look=0):
    """A chibi puppy drawn pixel by pixel: white body, black features, 1 px black seams."""
    d = ImageDraw.Draw(im)
    # tail (behind the body), wags between 3 poses
    tx, ty = x + 25, y + 27
    tails = [[(tx, ty), (tx + 4, ty - 3), (tx + 6, ty - 8)], [(tx, ty), (tx + 5, ty - 2), (tx + 9, ty - 5)],
             [(tx, ty), (tx + 3, ty - 4), (tx + 3, ty - 10)]]
    d.line(tails[tail % 3], fill=1, width=3)
    # body
    d.rounded_rectangle([x + 5, y + 17, x + 25, y + 36], radius=6, fill=1, outline=0)
    # paws
    for px_ in (x + 7, x + 16):
        d.rounded_rectangle([px_, y + 31, px_ + 7, y + 37], radius=3, fill=1, outline=0)
    # chest patch line
    d.line([(x + 15, y + 22), (x + 15, y + 28)], fill=0)
    # ears behind the head
    if ears == "down":
        d.ellipse([x - 4, y + 2, x + 7, y + 19], fill=1, outline=0)
        d.ellipse([x + 23, y + 2, x + 34, y + 19], fill=1, outline=0)
    else:  # lifted, flying out
        d.polygon([(x + 2, y + 4), (x - 6, y - 2), (x - 5, y + 9), (x + 3, y + 12)], fill=1, outline=0)
        d.polygon([(x + 28, y + 4), (x + 36, y - 2), (x + 35, y + 9), (x + 27, y + 12)], fill=1, outline=0)
    # head
    d.rounded_rectangle([x + 1, y, x + 29, y + 21], radius=9, fill=1, outline=0)
    # face
    ex = (x + 8 + look, x + 19 + look)
    for e in ex:
        if eyes == "open" and not blink:
            d.rectangle([e, y + 8, e + 3, y + 12], fill=0)
            d.point((e + 1, y + 9), fill=1)
        elif eyes == "happy":
            d.line([(e, y + 11), (e + 1, y + 9), (e + 2, y + 9), (e + 3, y + 11)], fill=0, width=1)
            d.line([(e, y + 12), (e + 3, y + 12)], fill=1)
        elif eyes == "sad":
            d.rectangle([e, y + 10, e + 3, y + 12], fill=0)
            d.line([(e - 1, y + 8), (e + 4, y + 9) if e == ex[0] else (e - 1, y + 9)], fill=0)
        else:  # closed / blink
            d.line([(e, y + 11), (e + 3, y + 11)], fill=0)
    d.rectangle([x + 13, y + 13, x + 17, y + 15], fill=0)  # nose
    if mouth == "smile":
        d.line([(x + 15, y + 16), (x + 15, y + 17)], fill=0)
        d.point([(x + 13, y + 18), (x + 14, y + 18), (x + 16, y + 18), (x + 17, y + 18)], fill=0)
        d.point([(x + 12, y + 17), (x + 18, y + 17)], fill=0)
    elif mouth == "open":
        d.rectangle([x + 13, y + 17, x + 17, y + 19], fill=0)
        d.line([(x + 14, y + 19), (x + 16, y + 19)], fill=1)
    elif mouth == "flat":
        d.line([(x + 13, y + 17), (x + 17, y + 17)], fill=0)
    elif mouth == "frown":
        d.line([(x + 13, y + 18), (x + 14, y + 17), (x + 16, y + 17), (x + 17, y + 18)], fill=0)


def ground(im, y=62):
    d = ImageDraw.Draw(im)
    for x in range(0, 128, 2):
        d.point((x, y), fill=1)


def bubble(im, x0, y0, x1, y1, tail_x, tail_y):
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([x0, y0, x1, y1], radius=4, outline=1, fill=0)
    d.polygon([(x0, tail_y - 3), (tail_x, tail_y), (x0, tail_y + 3)], fill=0)
    d.line([(x0, tail_y - 3), (tail_x, tail_y), (x0, tail_y + 3)], fill=1)


def C_scene(seconds, draw, ms=80):
    return frames_for(seconds, draw, ms=ms)


def C_sleep():
    def f(t):
        im = Image.new("1", (W, H), 0)
        d = ImageDraw.Draw(im)
        # a bed cushion
        d.rounded_rectangle([18, 46, 82, 61], radius=7, outline=1)
        d.line([(24, 53), (76, 53)], fill=1)
        br = int(math.sin(t * 2 * math.pi / 2.4) > 0)
        pup(im, 34, 14 + br, eyes="closed", ears="down", mouth="flat", tail=0)
        for i, sz in enumerate((7, 9, 11)):
            ph = (t * 0.5 + i / 3.0) % 1.0
            if ph < 0.9:
                text(im, 74 + i * 7 + ph * 4, 16 - i * 6 - ph * 8, "z", UBB, sz)
        ground(im)
        return im
    return C_scene(6, f)


def C_listen():
    def f(t):
        im = Image.new("1", (W, H), 0)
        pup(im, 49, 22, eyes="open", ears="up", mouth="open" if int(t * 3) % 2 == 0 and t < 0.8 else "smile",
            tail=int(t * 8))
        d = ImageDraw.Draw(im)
        for j in range(3):
            ph = (t * 1.6 + j / 3) % 1.0
            r = 6 + ph * 18
            if ph < 0.85:
                d.arc([22 - r, 30 - r, 22 + r, 30 + r], 150, 210, fill=1)
                d.arc([106 - r, 30 - r, 106 + r, 30 + r], -30, 30, fill=1)
        if t < 1.2:
            text(im, 64, 0, "Gâu gâu!", UBB, 13, anchor="mt")
        else:
            text(im, 64, 1, "Mình nghe nè", UB, 11, anchor="mt")
        ground(im)
        return im
    return C_scene(4.5, f)


def C_think():
    def f(t):
        im = Image.new("1", (W, H), 0)
        pup(im, 20, 22, eyes="open", look=1, ears="down", mouth="flat", tail=0)
        d = ImageDraw.Draw(im)
        d.ellipse([56, 12, 59, 15], outline=1)
        d.ellipse([62, 6, 67, 11], outline=1)
        d.rounded_rectangle([70, 0, 124, 20], radius=9, outline=1)
        n = int(t * 3) % 4
        for j in range(3):
            cx = 86 + j * 11
            if j < n:
                d.ellipse([cx - 2, 8, cx + 2, 12], fill=1)
            else:
                d.ellipse([cx - 1, 9, cx + 1, 11], fill=1)
        ground(im)
        return im
    return C_scene(4, f)


def C_talk_msg(msg, mood="smile"):
    lines = wrap(msg, UB, 11, 72)
    lh = 12
    view = 48
    total = len(lines) * lh
    scroll_end = max(0, total - view)
    hold0, hold1 = 1.2, 1.6
    dur = hold0 + scroll_end / 14 + hold1

    def f(t):
        im = Image.new("1", (W, H), 0)
        talking = t < dur - hold1
        pup(im, 6, 24, eyes="sad" if mood == "frown" else "open",
            ears="down", mouth=("open" if int(t * 7) % 2 == 0 else "smile") if talking else mood, tail=int(t * 6) if mood != "frown" else 0)
        bubble(im, 46, 2, 127, 56, 38, 34)
        off = 0 if t < hold0 else min(scroll_end, (t - hold0) * 14)
        clip = Image.new("1", (W, H), 0)
        for j, ln in enumerate(lines):
            y = 5 + j * lh - off
            if -lh < y < 56:
                text(clip, 51, y, ln, UB, 11)
        mask = Image.new("1", (W, H), 0)
        ImageDraw.Draw(mask).rectangle([48, 4, 125, 54], fill=1)
        im.paste(clip, (0, 0), mask)
        ground(im)
        return im
    return C_scene(dur, f, ms=50)


def C_talk():
    return C_talk_msg(TALK)


def C_happy():
    def f(t):
        im = Image.new("1", (W, H), 0)
        hop = int(abs(math.sin(t * math.pi * 2.4)) * 5)
        pup(im, 49, 22 - hop, eyes="happy", ears="up", mouth="open", tail=int(t * 10))
        b = Big()
        for j, (hx, ph0) in enumerate(((22, 0.0), (104, 0.4), (90, 0.75))):
            ph = (t * 0.9 + ph0) % 1.0
            bot.heart(b, hx + math.sin(ph * 7) * 2, 52 - ph * 46, 3.6)
        hearts = b.out()
        im = Image.composite(Image.new("1", (W, H), 1), im, hearts)
        ground(im)
        return im
    return C_scene(3.2, f, ms=60)


def C_sad():
    def f(t):
        im = Image.new("1", (W, H), 0)
        pup(im, 49, 22, eyes="sad", ears="down", mouth="frown", tail=0, look=-1 if int(t) % 2 else 1)
        text(im, 64, 1, "Hả?", UBB, 13, anchor="mt")
        ground(im)
        return im
    return C_scene(2.4, f) + C_talk_msg(SORRY, "frown")


def C_idle():
    def f(t):
        im = Image.new("1", (W, H), 0)
        x = int(keys([(0, 49), (1.5, 49), (3.0, 86), (4.5, 86), (6.0, 49), (8, 49)], t))
        blink = any(t0 <= t < t0 + 0.16 for t0 in (1.0, 4.0, 6.6))
        lk = 1 if 1.5 < t < 3.0 else -1 if 4.5 < t < 6.0 else 0
        pup(im, x, 22, eyes="open", blink=blink, look=lk, ears="down", mouth="smile", tail=int(t * 5) if 3.2 < t < 4.4 else 0)
        d = ImageDraw.Draw(im)
        # a bowl and a ball for company
        d.chord([8, 50, 30, 66], 180, 360, fill=1)
        d.line([(8, 58), (30, 58)], fill=0)
        bx = 112
        d.ellipse([bx - 5, 52, bx + 5, 62], outline=1)
        d.arc([bx - 5, 52, bx + 5, 62], 200, 340, fill=1)
        ground(im)
        return im
    return C_scene(8, f)


PIX = {  # 5x7 digits for the pixel clock
    "0": ["01110", "10001", "10011", "10101", "11001", "10001", "01110"],
    "1": ["00100", "01100", "00100", "00100", "00100", "00100", "01110"],
    "2": ["01110", "10001", "00001", "00010", "00100", "01000", "11111"],
    "3": ["11110", "00001", "00001", "01110", "00001", "00001", "11110"],
    "4": ["00010", "00110", "01010", "10010", "11111", "00010", "00010"],
    "5": ["11111", "10000", "11110", "00001", "00001", "10001", "01110"],
    "6": ["00110", "01000", "10000", "11110", "10001", "10001", "01110"],
    "7": ["11111", "00001", "00010", "00100", "01000", "01000", "01000"],
    "8": ["01110", "10001", "10001", "01110", "10001", "10001", "01110"],
    "9": ["01110", "10001", "10001", "01111", "00001", "00010", "01100"],
    ":": ["0", "1", "1", "0", "1", "1", "0"],
}


def pixel_glyphs(scale=4):
    out = {}
    for ch, rows in PIX.items():
        w = len(rows[0]) * scale + scale
        im = Image.new("1", (w, 7 * scale), 0)
        d = ImageDraw.Draw(im)
        for r, row in enumerate(rows):
            for c, v in enumerate(row):
                if v == "1":
                    d.rectangle([c * scale, r * scale, c * scale + scale - 2, r * scale + scale - 2], fill=1)
        out[ch] = im
    return out


def C_weather():
    def f(t):
        im = Image.new("1", (W, H), 0)
        d = ImageDraw.Draw(im)
        # pixel sun
        cx, cy = 22, 22
        d.ellipse([cx - 8, cy - 8, cx + 8, cy + 8], fill=1)
        for j in range(8):
            a = j * math.pi / 4 + (int(t * 4) % 2) * math.pi / 8
            d.rectangle([cx + int(13 * math.cos(a)) - 1, cy + int(13 * math.sin(a)) - 1,
                         cx + int(13 * math.cos(a)) + 1, cy + int(13 * math.sin(a)) + 1], fill=1)
        g = pixel_glyphs(4)
        x = 50
        for ch in "28":
            im.paste(g[ch], (x, 4), g[ch])
            x += g[ch].width
        d.rectangle([x + 1, 4, x + 4, 7], outline=1)
        pup(im, 92, 25, eyes="happy", ears="down", mouth="smile", tail=int(t * 6))
        text(im, 2, 38, "Hà Nội", UB, 11)
        text(im, 2, 50, "nắng nhẹ", UB, 11)
        ground(im, 63)
        return im
    return C_scene(4, f)


def C_clock_bg():
    def f(t):
        im = Image.new("1", (W, H), 0)
        sleepy = int(t * 1.25) % 4 == 3
        pup(im, 92, 24, eyes="closed" if sleepy else "open", ears="down", mouth="smile", tail=int(t * 5))
        text(im, 2, 36, "Thứ Năm", UB, 11)
        text(im, 2, 48, "20/11  28°", UB, 11)
        ground(im)
        return im
    return C_scene(4, f, ms=100)


# =================================================================== D: thin line art
def headD(b, cx=64, cy=36, k=1.0, eyes="open", look=(0, 0), blink=0.0, ears="down", mouth="smile", tongue=False):
    sw = 1.7
    X = lambda v: cx + v * k
    Y = lambda v: cy + v * k
    # ears: thin outlines, the head line cuts over them
    for sgn in (-1, 1):
        if ears == "down":
            b.ell(X(sgn * 27), Y(-2), 8 * k, 17 * k, rot=sgn * -0.35, width=sw)
        else:
            b.ell(X(sgn * 30), Y(-16), 7 * k, 15 * k, rot=sgn * 0.95, width=sw)
    b.ell(X(0), Y(0), 25 * k, 23 * k, fill=0)
    b.ell(X(0), Y(0), 25 * k, 23 * k, width=sw)
    for sgn in (-1, 1):
        ex, ey = X(sgn * 10 + look[0] * 2.5), Y(-4 + look[1] * 2.5)
        if eyes == "open":
            h = 4.2 * k * max(0.15, 1 - blink)
            b.ell(ex, ey, 3.6 * k, h)
            if blink < 0.5:
                b.ell(ex - 1.2 * k, ey - 1.5 * k, 1.1 * k, 1.1 * k, fill=0)
        elif eyes == "happy":
            b.arc(ex, ey + 2.5 * k, 4.2 * k, 205, 335, width=sw)
        elif eyes == "sleep":
            b.arc(ex, ey - 2.5 * k, 4.2 * k, 25, 155, width=sw)
        elif eyes == "sad":
            b.ell(ex, ey + 1 * k, 3.2 * k, 3.6 * k)
            b.line([(ex - 4.5 * k, ey - (4 if sgn < 0 else 7) * k), (ex + 4.5 * k, ey - (7 if sgn < 0 else 4) * k)], width=1.4)
    nx, ny = X(0), Y(6)
    b.ell(nx, ny, 4 * k, 2.8 * k)
    if mouth == "smile":
        b.arc(X(-3), Y(9), 3 * k, 10, 170, width=1.5)
        b.arc(X(3), Y(9), 3 * k, 10, 170, width=1.5)
    elif mouth == "open":
        b.ell(X(0), Y(13), 4.5 * k, 3.5 * k, width=1.5)
    elif mouth == "flat":
        b.line([(X(-3), Y(12)), (X(3), Y(12))], width=1.5)
    elif mouth == "frown":
        b.arc(X(0), Y(16), 4 * k, 215, 325, width=1.5)
    if tongue:
        b.rrect(X(-2.5), Y(11), X(2.5), Y(17), 2.5 * k)


def D_sleep():
    def f(t):
        b = Big()
        br = math.sin(t * 2 * math.pi / 3) * 0.8
        headD(b, cy=36 + br, eyes="sleep", mouth="flat")
        im = b.out()
        zs(im, t, 92, 20)
        return im
    return frames_for(6, f)


def D_listen():
    def f(t):
        b = Big()
        bl = keys([(0, 0.9), (0.3, 0.0)], t)
        headD(b, ears="up" if t > 0.25 else "down", blink=bl, mouth="smile")
        for sgn in (-1, 1):
            for j in range(3):
                ph = (t * 1.8 + j / 3) % 1.0
                r = 5 + ph * 12
                if ph < 0.85:
                    a0, a1 = (155, 205) if sgn < 0 else (-25, 25)
                    b.arc(64 + sgn * 38, 38, r, a0, a1, width=1.5)
        return b.out()
    return frames_for(4.5, f)


def D_think():
    def f(t):
        b = Big()
        headD(b, look=(1, -1), mouth="flat")
        for j in range(3):
            ph = (t * 2 - j * 0.2) % 1.0
            on = ph < 0.5
            cx = 100 + j * 8
            if on:
                b.ell(cx, 10, 1.8, 1.8)
            else:
                b.ell(cx, 10, 1.8, 1.8, width=1.0)
        return b.out()
    return frames_for(4, f)


def D_talk_msg(msg, mood="smile"):
    lines = lwrap(msg, 11, 300, 74)
    lh = 12
    total = len(lines) * lh
    scroll_end = max(0, total - 60)
    hold0, hold1 = 1.2, 1.6
    dur = hold0 + scroll_end / 14 + hold1

    def f(t):
        b = Big()
        talking = t < dur - hold1
        headD(b, cx=25, cy=32, k=0.78, mouth=("open" if int(t * 7) % 2 == 0 else "smile") if talking else mood,
              eyes="sad" if mood == "frown" else "open")
        im = b.out()
        off = 0 if t < hold0 else min(scroll_end, (t - hold0) * 14)
        for j, ln in enumerate(lines):
            y = 2 + j * lh - off
            if -lh < y < H:
                ltext(im, 52, y, ln, 11, 300)
        return im
    return frames_for(dur, f, ms=50)


def D_talk():
    return D_talk_msg(TALK)


def D_happy():
    def f(t):
        b = Big()
        bo = -abs(math.sin(t * math.pi * 2.2)) * 2.5
        headD(b, cy=36 + bo, eyes="happy", ears="up", mouth="smile", tongue=True)
        for j, (hx, ph0) in enumerate(((16, 0.0), (112, 0.5))):
            ph = (t + ph0) % 1.0
            cy = 52 - ph * 40
            r = 3.6
            b.ell(hx - r * 0.5, cy - r * 0.2, r * 0.55, r * 0.55, width=1.2)
            b.ell(hx + r * 0.5, cy - r * 0.2, r * 0.55, r * 0.55, width=1.2)
            b.line([(hx - r * 1.0, cy + 0.2), (hx, cy + r * 1.1), (hx + r * 1.0, cy + 0.2)], width=1.2)
        return b.out()
    return frames_for(3.2, f)


def D_sad():
    def f(t):
        b = Big()
        headD(b, eyes="sad", mouth="frown", look=(math.sin(t * 2) * 0.6, 0.6))
        return b.out()
    return frames_for(2.4, f) + D_talk_msg(SORRY, "frown")


def D_idle():
    tl = [(0, (0, 0)), (1, (0, 0)), (1.25, (-1, 0)), (2.4, (-1, 0)), (2.65, (1, -0.6)), (3.8, (1, -0.6)),
          (4.05, (0, 0)), (6, (0, 0))]

    def f(t):
        b = Big()
        bl = 0
        for t0 in (0.6, 3.1, 5.0):
            if t0 <= t < t0 + 0.22:
                bl = math.sin((t - t0) / 0.22 * math.pi)
        headD(b, look=keys(tl, t), blink=bl)
        return b.out()
    return frames_for(6, f)


def D_weather():
    def f(t):
        b = Big()
        cx, cy = 24, 26
        b.ell(cx, cy, 9, 9, width=1.6)
        for j in range(8):
            a = j * math.pi / 4 + t * 0.5
            b.line([(cx + 12.5 * math.cos(a), cy + 12.5 * math.sin(a)), (cx + 16 * math.cos(a), cy + 16 * math.sin(a))], width=1.5)
        im = b.out()
        ltext(im, 124, -6, "28°", 36, 250, anchor="rt")
        ltext(im, 124, 34, "nắng nhẹ", 11, 300, anchor="rt")
        ImageDraw.Draw(im).line([(2, 50), (125, 50)], fill=1)
        ltext(im, 2, 52, "Hà Nội", 10, 300)
        ltext(im, 125, 52, "chiều mưa 31°", 10, 300, anchor="rt")
        return im
    return frames_for(4, f)


def D_clock_bg():
    def f(t):
        im = Image.new("1", (W, H), 0)
        d = ImageDraw.Draw(im)
        d.line([(14, 47), (114, 47)], fill=1)
        ltext(im, 64, 50, "Thứ Năm, 20 tháng 11", 10, 300, anchor="mt")
        return im
    return frames_for(0.04, f, ms=1000)


# =================================================================== the versions
def lex_glyphs(px, wght):
    out = {}
    f = lex(px, wght)
    asc, _ = f.getmetrics()
    for ch in "0123456789:":
        w = int(math.ceil(f.getlength(ch))) + (2 if ch == ":" else 0)
        im = Image.new("1", (w, asc), 0)
        d = ImageDraw.Draw(im)
        d.fontmode = "1"
        d.text((1 if ch == ":" else 0, asc), ch, font=f, fill=1, anchor="ls")
        out[ch] = im
    return out


def crop_top(g, rows):
    return {k: v.crop((0, rows, v.width, v.height)) for k, v in g.items()}


VERSIONS = [
    ("A", "Bản đầu", [
        ("Ngủ", lambda: bot.sc_sleep()),
        ("Gọi gâu gâu", lambda: bot.sc_wake() + bot.sc_listen() * 3),
        ("Đang nghĩ", lambda: bot.sc_think() * 2),
        ("Trả lời", lambda: bot.sc_talk()),
        ("Vui", lambda: bot.sc_happy() * 2),
        ("Chưa nghe rõ", lambda: bot.sc_sad() * 2 + bot.sc_talk(SORRY, "frown")[:40]),
        ("Mắt chó", lambda: bot.sc_idle_eyes() * 2),
        ("Thời tiết", lambda: bot.sc_weather() * 4),
    ], [("Đồng hồ", lambda: [(bot.bg_clock_big(), 1000)], ("ub40", 0, 3, True))]),
    ("B", "Mắt to", [
        ("Ngủ", B_sleep), ("Gọi gâu gâu", B_listen), ("Đang nghĩ", B_think), ("Trả lời", B_talk),
        ("Vui", lambda: B_happy() * 2), ("Chưa nghe rõ", B_sad), ("Mắt chó", B_idle), ("Thời tiết", lambda: B_weather() * 2),
    ], [("Đồng hồ", B_clock_bg, ("lex38", 0, 0, True))]),
    ("C", "Chó pixel", [
        ("Ngủ", C_sleep), ("Gọi gâu gâu", C_listen), ("Đang nghĩ", C_think), ("Trả lời", C_talk),
        ("Vui", lambda: C_happy() * 2), ("Chưa nghe rõ", C_sad), ("Chơi", C_idle), ("Thời tiết", lambda: C_weather() * 2),
    ], [("Đồng hồ", C_clock_bg, ("pix4", 2, 4, False))]),
    ("D", "Nét mảnh", [
        ("Ngủ", D_sleep), ("Gọi gâu gâu", D_listen), ("Đang nghĩ", D_think), ("Trả lời", D_talk),
        ("Vui", lambda: D_happy() * 2), ("Chưa nghe rõ", D_sad), ("Mắt chó", lambda: D_idle() * 2), ("Thời tiết", lambda: D_weather() * 2),
    ], [("Đồng hồ", D_clock_bg, ("lex40l", 0, -2, True))]),
]


VERSIONS = VERSIONS[1:] + VERSIONS[:1]  # the new looks first, the first try last


def glyph_sets():
    return {
        "ub40": bot.glyphs(40),
        "lex38": crop_top(lex_glyphs(40, 600), 6),
        "pix4": pixel_glyphs(4),
        "lex40l": crop_top(lex_glyphs(46, 200), 7),
    }


def title(v, vname, n, total, name):
    im = Image.new("1", (W, H), 0)
    text(im, 64, 6, f"Bản {v} · {vname}", UB, 11, anchor="mt")
    text(im, 64, 24, name, UBB, 15, anchor="mt")
    text(im, 64, 46, f"{n}/{total}", UB, 10, anchor="mt")
    return im


def pack(im):
    return bot.pack(im)


def main():
    frames, index = [], {}

    def fid(im):
        b = pack(im)
        if b not in index:
            index[b] = len(frames)
            frames.append(b)
        return index[b]

    gsets = glyph_sets()
    gnames = list(gsets)
    seqs = []
    gifs = {}
    for v, vname, scenes, clocks in VERSIONS:
        total = len(scenes) + len(clocks)
        gif = []
        for n, (name, fn) in enumerate(scenes, 1):
            fr = fn()
            seqs.append(dict(v=v, name=name, title=fid(title(v, vname, n, total, name)),
                             steps=[(fid(im), ms) for im, ms in fr], clock=None))
            gif += fr
        for i, (name, fn, spec) in enumerate(clocks):
            fr = fn()
            seqs.append(dict(v=v, name=name, title=fid(title(v, vname, len(scenes) + i + 1, total, name)),
                             steps=[(fid(im), ms) for im, ms in fr], clock=spec))
            # preview: the clock drawn on its first frame
            g = gsets[spec[0]]
            for im, ms in fr[:1]:
                im = im.copy()
                s = "21:30"
                wsum = sum(g[c].width for c in s)
                x = (W - wsum) // 2 if spec[3] else spec[1]
                for c in s:
                    im.paste(g[c], (x, spec[2]), g[c])
                    x += g[c].width
                gif += [(im, 2000)]
        gifs[v] = (vname, gif)

    L = ["// Made by tools/versions.py: do not edit.", "#pragma once", "#include <stdint.h>",
         f"#define BOT_FRAMES {len(frames)}", "static const uint8_t BOT_FRAME[BOT_FRAMES][1024] = {"]
    for b in frames:
        L.append("{" + ",".join(str(x) for x in b) + "},")
    L.append("};")
    L.append("struct BotStep { uint16_t frame, ms; };")
    for i, q in enumerate(seqs):
        L.append(f"static const BotStep SEQ_{i}[] = {{" + ",".join(f"{{{f},{ms}}}" for f, ms in q["steps"]) + "};")
    # glyph sets
    for gn, g in gsets.items():
        for ch, im in g.items():
            w, h = im.size
            bpr = (w + 7) // 8
            data = bytearray(bpr * h)
            p = im.load()
            for y in range(h):
                for x in range(w):
                    if p[x, y]:
                        data[y * bpr + x // 8] |= 0x80 >> (x % 8)
            cn = "c" if ch == ":" else ch
            L.append(f"static const uint8_t GL_{gn}_{cn}[] = {{{w},{h}," + ",".join(map(str, data)) + "};")
        L.append(f"static const uint8_t* const GL_{gn}[11] = {{" + ",".join(f"GL_{gn}_{c}" for c in "0123456789") + f",GL_{gn}_c}};")
    L.append("static const uint8_t* const* const GLYPHS[] = {" + ",".join(f"GL_{g}" for g in gnames) + "};")
    L.append("struct BotSeq { char version; const char* name; uint16_t title; const BotStep* steps; uint16_t n;"
             " int8_t glyphs; int8_t gx, gy; bool center; };")
    L.append(f"#define BOT_SEQS {len(seqs)}")
    rows = []
    for i, q in enumerate(seqs):
        c = q["clock"]
        gi, gx, gy, ce = (gnames.index(c[0]), c[1], c[2], "true" if c[3] else "false") if c else (-1, 0, 0, "false")
        rows.append(f"{{'{q['v']}',\"{q['name']}\",{q['title']},SEQ_{i},{len(q['steps'])},{gi},{gx},{gy},{ce}}}")
    L.append("static const BotSeq BOT_SEQ[BOT_SEQS] = {" + ",".join(rows) + "};")
    open(os.path.join(ROOT, "src/bot.h"), "w").write("\n".join(L) + "\n")

    # previews
    os.makedirs(os.path.join(ROOT, "preview"), exist_ok=True)
    for v, (vname, gif) in gifs.items():
        ims = [Image.eval(im.convert("L").resize((W * 3, H * 3), Image.NEAREST), lambda x: 235 if x else 6) for im, _ in gif]
        ims[0].save(os.path.join(ROOT, f"preview/{v}.gif"), save_all=True, append_images=ims[1:],
                    duration=[ms for _, ms in gif], loop=0)
    html = ["<!doctype html><meta charset=utf-8><title>EspOS bot: các bản màn hình</title>",
            "<style>body{background:#111;color:#ddd;font:15px system-ui;margin:16px}h2{margin:18px 0 6px}"
            "img{image-rendering:pixelated;border:6px solid #222;border-radius:8px;max-width:100%}</style>",
            "<h1>Màn hình chó AI, OLED 128x64</h1><p>Mỗi bản chạy hết các màn rồi lặp lại. Phóng to 3 lần.</p>"]
    for v, (vname, _) in gifs.items():
        html.append(f"<h2>Bản {v} · {vname}</h2><img src='{v}.gif' width=384>")
    open(os.path.join(ROOT, "preview/versions.html"), "w").write("\n".join(html))
    print(f"{len(frames)} frames ({len(frames) // 1024 + 1} MB), {len(seqs)} screens")


if __name__ == "__main__":
    main()
