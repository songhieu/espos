#!/usr/bin/env python3
"""The bot's three looks for art2, drawn pixel by pixel (see px.py):

  B "Mắt to"   big anime eyes: dark irises with sparkles, heavy upper lids, dithered blush, floppy ears at the edges
  C "Chó pixel" a chibi white puppy with dithered brown ears and patch, black seams, dither form shadow, small scenes
  D "Nét mảnh" clean 1-px line art: a dog head with a muzzle, long ears with dithered linings, a collar tag

Every look has: sleep, listen (after "gâu gâu"), think, talk (reply scrolls), happy, sad (not heard), idle.
A scene function returns [(canvas, ms)]. talk_* also take the message so the firmware can see the layout; the
LAYOUT dict says where the live reply text goes.
"""
import math
import random

import numpy as np

import px
from px import W, H, new, ell, circ, rrect, rect, poly, edge, outer, rim, dith, shift, dilate, blit, sprite, keys, taper, \
    ease, lerp, frames_for, blink_at, path1, thick, arc_pts

TALK = "Hà Nội hôm nay nắng nhẹ, 28 độ. Chiều có mưa rào, nhớ mang áo mưa nha! Tối mát, ngủ ngon lắm đó."
SORRY = "Mình chưa nghe rõ, bạn nói lại nha!"

# ------------------------------------------------------------------ shared small sprites
SPARK4 = sprite([
    "   #   ",
    "   #   ",
    "  ###  ",
    "#######",
    "  ###  ",
    "   #   ",
    "   #   ",
])
SPARK3 = sprite([
    "  #  ",
    "  #  ",
    "#####",
    "  #  ",
    "  #  ",
])
SPARK2 = sprite([
    " # ",
    "###",
    " # ",
])
HEART = sprite([
    " ## ## ",
    "#######",
    "#######",
    " ##### ",
    "  ###  ",
    "   #   ",
])
HEART_S = sprite([
    "## ##",
    "#####",
    " ### ",
    "  #  ",
])
Z_BIG = sprite([
    "######",
    "    ##",
    "   ## ",
    "  ##  ",
    " ##   ",
    "######",
])
Z_MID = sprite([
    "#####",
    "   # ",
    "  #  ",
    " #   ",
    "#####",
])
Z_SM = sprite([
    "####",
    "  # ",
    " #  ",
    "####",
])
TEAR = sprite([
    " # ",
    " # ",
    "###",
    "###",
    " # ",
])
NOTE = sprite([
    "  ##",
    "  # ",
    "  # ",
    "### ",
    "### ",
])


def zzz(a, t, x, y, speed=0.5):
    """Three z's drifting up and right, small to big."""
    for i, s in enumerate((Z_SM, Z_MID, Z_BIG)):
        ph = (t * speed + i / 3.0) % 1.0
        if ph < 0.88:
            blit(a, s, int(x + i * 7 + ph * 5), int(y - i * 7 - ph * 7))


def sparkle(a, x, y, ph):
    """A twinkle: grows 2 -> 3 -> 4 and back; ph 0..1."""
    k = math.sin(ph * math.pi)
    if k > 0.75:
        blit(a, SPARK4, x - 3, y - 3)
    elif k > 0.4:
        blit(a, SPARK3, x - 2, y - 2)
    elif k > 0.1:
        blit(a, SPARK2, x - 1, y - 1)


# =================================================================== B: big eyes
def b_ear(a, side, lift=0.0, cy=0, flop=0.0):
    """A floppy ear hanging from above the screen: narrow where it joins the head (off screen), a round lobe low
    at the edge, 25% dither, a 1-px rim and a fold line. lift 0..1 swings the lobe out and up (listening); it
    never stands up, so it never reads as a rabbit's ear."""
    sgn = -1 if side == "l" else 1
    ax, ay = 64 + sgn * (46 - lift * 4), -10 + cy - lift * 4  # where it joins the head, above the screen
    ang = 0.32 + lift * 0.28 + flop * sgn                      # how far the lobe swings out from straight down
    L = 38 - lift * 9                                         # perked: the ear lifts and folds up a little
    lx, ly = ax + sgn * math.sin(ang) * L, ay + math.cos(ang) * L
    m = taper(ax, ay, 4, lx, ly, 9.5)
    a[m] = False
    a[m & dith(3)] = True
    a[edge(m)] = True
    # the fold: the ear's inner edge turns over, a line inside along the face side
    k = 0.55
    fx0, fy0 = ax - sgn * 1, ay + 6
    fx1, fy1 = lx - sgn * 6.5 * math.cos(ang), ly - 4 + sgn * 0
    fold = path1([(fx0 + (fx1 - fx0) * i / 12, fy0 + (fy1 - fy0) * i / 12) for i in range(13)]) & erode_(m)
    a[fold] = True
    a[dilate(fold) & m & ~fold & ~edge(m) & px.hlines(1)] = False


def erode_(m):
    return px.erode(m) if m.any() else m


def b_eye(a, cx, cy, w=24, h=30, look=(0, 0), blink=0.0, lid=0.0, low=0.0, sad=0.0, side=1, pupil=1.0):
    """One big eye: heavy top lid line, a dotted iris that glows at the bottom, a black pupil, two sparkles.
    blink 0..1 closes it; lid lowers the top lid (sleepy/thinking, a soft curve); low raises the lower lid (smiling);
    sad slants the top lid down toward the outside."""
    hh = h * (1 - 0.92 * blink)
    if hh < 3:  # closed: a soft curve
        pts = arc_pts(cx, cy - 4, w / 2 - 1, 5, 25, 155)
        a[thick(pts, 2.2)] = True
        return
    E = ell(cx, cy, w / 2, hh / 2)
    top = cy - hh / 2
    cut = np.zeros_like(E)
    if lid > 0:  # the lid line bulges up a little in the middle, like a real lid
        u = np.clip((px.XC - cx) / (w / 2), -1, 1)
        cut |= px.YC < top + hh * lid - 2.0 * (1 - u * u) + 1
    if sad > 0:  # higher on the inside, lower outside
        ix, ox = cx - side * w / 2, cx + side * w / 2
        iy, oy = top - hh * 0.05, top + hh * 0.45 * sad
        lin = iy + (oy - iy) * (px.XC - ix) / (ox - ix)
        cut |= px.YC < lin
    if low > 0:
        cut |= ell(cx, cy + hh / 2 + hh * (0.55 - low * 0.35), w * 0.62, hh * 0.55 * (0.3 + low))
    E = E & ~cut
    if not E.any():
        return
    lx, ly = look
    a[E] = False
    yrel = (px.YC - (cy - hh / 2)) / max(1, hh)
    a[E & dith(2)] = True                      # the iris: faint dots
    a[E & (yrel > 0.6) & dith(5)] = True       # light pooling at the bottom
    a[E & (yrel > 0.78) & dith(9)] = True
    pr_x, pr_y = w * 0.25 * pupil, hh * 0.25 * pupil
    pcx, pcy = cx + lx * w * 0.16, cy + ly * hh * 0.14
    P = ell(pcx, pcy + 1, pr_x, pr_y) & E
    a[P] = False
    a[edge(E, edge_in=False)] = True
    toplid = E & ~shift(E, 0, 3) & (px.YC < cy + 2)
    a[toplid] = True
    if lid < 0.3 and sad == 0 and low < 0.5:  # a lash flick at the outer corner
        ox = int(cx + side * (w / 2 - 1))
        oy = int(cy - hh * 0.3)
        for i in range(2):
            x_, y_ = ox + side * (i + 1), oy - i - 1
            if 0 <= x_ < W and 0 <= y_ < H:
                a[y_, x_] = True
    sx, sy = pcx - w * 0.16, pcy - hh * 0.15
    S = ell(sx, sy, max(2.0, w * 0.12), max(2.2, hh * 0.11)) & E & ~toplid
    a[S] = True
    s2 = (int(pcx + w * 0.18), int(pcy + hh * 0.17))
    if E[min(H - 1, max(0, s2[1])), min(W - 1, max(0, s2[0]))]:
        a[s2[1]:s2[1] + 2, s2[0]:s2[0] + 2] = True


NOSE9 = sprite([
    " ####### ",
    "#..######",
    "#########",
    " ####### ",
    "  #####  ",
    "   ###   ",
])
NOSE5 = sprite([
    " ### ",
    "#####",
    " ### ",
    "  #  ",
])
MOUTH_W = sprite([
    "    #    ",
    "    #    ",
    "#  # #  #",
    " ##   ## ",
])
MOUTH_W5 = sprite([
    "  #  ",
    "# # #",
    " # # ",
])


def b_nose(a, cx, cy, k=1.0):
    """The nose: a hand-placed rounded triangle with a shine (cx, cy = the middle of its top)."""
    if k >= 0.9:
        blit(a, NOSE9, int(cx) - 4, int(cy) - 2)
    else:
        blit(a, NOSE5, int(cx) - 2, int(cy) - 1)


def b_mouth(a, cx, cy, kind="w", k=1.0, t=0.0):
    """The dog's mouth under the nose at (cx, cy): w (the default smile), open, tongue, flat, frown, o."""
    def w_():
        blit(a, MOUTH_W, int(cx) - 4, int(cy) - 2)
    if kind == "w":
        w_()
    elif kind == "open":  # talking: a small open mouth
        a[int(cy) - 2:int(cy), int(cx)] = True
        M = ell(cx + 0.5, cy + 2.5 * k, 4 * k, 3 * k)
        a[M] = False
        a[edge(M, edge_in=False)] = True
        a[M & (px.YC > cy + 3 * k) & dith(8)] = True
    elif kind == "tongue":  # w with a tongue hanging below it
        w_()
        T = rrect(int(cx) - 2, int(cy) + 1, int(cx) + 2, int(cy) + 5, 2)
        a[outer(T) & (px.YC > cy + 1.5)] = False
        a[T] = True
        a[int(cy) + 2:int(cy) + 4, int(cx)] = False
    elif kind == "flat":
        a[path1([(cx - 3 * k, cy + 1), (cx + 3 * k, cy + 1)])] = True
    elif kind == "frown":
        a[path1(arc_pts(cx, cy + 4 * k, 4 * k, 3 * k, 205, 335))] = True
    elif kind == "o":
        a[int(cy) - 2:int(cy), int(cx)] = True
        a[px.ring(cx + 0.5, cy + 2 * k, 2.5 * k, 2.2 * k)] = True


def b_cheeks(a, y=45, level=5, dx=33):
    for sgn in (-1, 1):
        m = ell(64 + sgn * dx, y, 6, 3)
        a[m] = False
        a[m & dith(level)] = True


def b_face(t=0.0, look=(0, 0), blink=0.0, lid=0.0, low=0.0, sad=0.0, ear_lift=0.0, ear_flop=(0, 0), cy=0,
           mouth="w", cheeks=5, eye_h=30, eye_w=24, pupil=1.0, eyes="open", gap=44, nose_dy=0):
    a = new()
    b_ear(a, "l", ear_lift, cy, ear_flop[0])
    b_ear(a, "r", ear_lift, cy, ear_flop[1])
    ex = (64 - gap / 2 + look[0] * 3, 64 + gap / 2 + look[0] * 3)
    ey = 26 + cy + look[1] * 2
    for i, x in enumerate(ex):
        side = -1 if i == 0 else 1
        if eyes == "open":
            # the eye nearer the look direction is a touch bigger
            per = 1 + side * look[0] * 0.05
            b_eye(a, x, ey, eye_w * per, eye_h * per, look, blink, lid, low, sad, side, pupil)
        elif eyes == "happy":  # ^ ^
            pts = arc_pts(x, ey + 6, 10, 9, 205, 335)
            a[thick(pts, 3.2)] = True
        elif eyes == "sleep":  # ‿ with lashes
            pts = arc_pts(x, ey - 2, 10, 6, 20, 160)
            a[thick(pts, 2.4)] = True
            for lx_ in (-6, 0, 6):
                bx = int(x + lx_)
                a[path1([(bx + 0.5, ey + 4.5), (bx + 0.5 + lx_ * 0.15, ey + 6.5)])] = True
    if cheeks:
        b_cheeks(a, 46 + cy, cheeks)
    b_nose(a, 64, 44 + cy + nose_dy)
    b_mouth(a, 64, 51 + cy + nose_dy, mouth)
    return a


def B_sleep():
    def f(t):
        br = math.sin(t * 2 * math.pi / 3.0)
        a = b_face(t, eyes="sleep", cy=int(round(br + 1)), mouth="flat", cheeks=3, ear_flop=(0.04 * br, -0.04 * br))
        zzz(a, t, 80, 16)
        return a
    return frames_for(6, f, 60)


def B_listen():
    """Wakes on "gâu gâu": eyes pop open (a squash, then an overshoot), ears lift, a voice meter at the bottom."""
    rnd = random.Random(5)
    noise = [rnd.random() for _ in range(200)]

    def level(t, i):
        k = t * 8 + i * 1.3
        p0 = noise[int(k) % 200]
        return lerp(p0, noise[(int(k) + 1) % 200], ease(k - int(k)))

    def f(t):
        bl = keys([(0, 1.0), (0.12, 1.0), (0.3, -0.12), (0.42, 0.0)], t)
        lift = keys([(0, 0.0), (0.25, 0.0), (0.5, 1.0)], t, px.back_out)
        rise = int(round(keys([(0, 0), (0.3, 0), (0.5, -3)], t)))
        h = 30 + 2 * min(1, t / 0.4)
        a = b_face(t, blink=max(0, bl), eye_h=h * (1 - min(0, bl) * 0.8), eye_w=24 + max(0, -bl) * 6,
                   look=(0, -0.3), ear_lift=lift, pupil=1.2, cheeks=5, mouth="w", cy=rise)
        if t > 0.55:  # the voice meter: 7 round-ended bars, tallest in the middle
            for i in range(7):
                v = level(t, i) * (1 - abs(i - 3) * 0.16)
                hgt = 2 + int(v * 5)
                x = 64 + (i - 3) * 4 - 1
                a[63 - hgt:63, x:x + 2] = True
        return a
    return frames_for(4.5, f, 40)


def B_think():
    def f(t):
        lk = keys([(0, (0, 0)), (0.35, (0.9, -0.9)), (1.9, (0.9, -0.9)), (2.25, (-0.7, -0.9)), (3.6, (-0.7, -0.9)),
                   (4.0, (0.9, -0.9))], t)
        a = b_face(t, look=lk, low=0.18, mouth="flat", cheeks=0, blink=blink_at(t, (1.5,), 0.18))
        # three dots between the ears, lit one after another
        for j in range(3):
            on = (t * 2.2 - j * 0.3) % 1.0 < 0.55
            x = 58 + j * 6
            if on:
                a[1:4, x:x + 3] = True
                a[1, x] = a[1, x + 2] = a[3, x] = a[3, x + 2] = False
            else:
                a[2, x + 1] = True
        return a
    return frames_for(4, f, 40)


def B_happy():
    def f(t):
        hop = -abs(math.sin(t * math.pi * 2.0)) * 3
        a = b_face(t, eyes="happy", cy=int(round(hop)) + 1, mouth="tongue", cheeks=8, ear_lift=0.35 + 0.25 * abs(math.sin(t * math.pi * 2)))
        for x, y, p0 in ((24, 52, 0.0), (106, 50, 0.45), (100, 8, 0.7), (26, 8, 0.25)):
            sparkle(a, x, y, (t * 1.3 + p0) % 1.0)
        return a
    return frames_for(3.0, f, 40)


TEAR_B = sprite([
    "  #  ",
    "  #  ",
    " ### ",
    "#####",
    "##.##",
    "#####",
    " ### ",
])


def B_sad():
    def f(t):
        lk = keys([(0, (0, 0)), (0.5, (-0.4, 0.9)), (1.7, (0.4, 0.9)), (2.4, (0, 0.9))], t)
        a = b_face(t, look=lk, sad=0.85, eye_h=28, mouth="frown", cheeks=0, ear_lift=-0.3, cy=2)
        for sgn in (-1, 1):  # worried brows: high inside, low outside
            x_in, x_out = 64 + sgn * 13, 64 + sgn * 29
            a[thick([(x_in, 9), (x_out, 14)], 2.0)] = True
        ty = keys([(0, 36), (0.6, 36), (2.3, 54)], t)
        if 0.6 < t < 2.3:
            blit(a, TEAR_B, 32, int(ty))
        return a
    return frames_for(2.4, f, 40)


def B_idle():
    tl = [(0, (0, 0)), (1.0, (0, 0)), (1.2, (-1, 0)), (2.3, (-1, 0)), (2.5, (0.9, -0.6)), (3.6, (0.9, -0.6)),
          (3.8, (0, 0)), (5.2, (0, 0)), (5.4, (0.2, 0.9)), (6.2, (0.2, 0.9)), (6.4, (0, 0)), (7.5, (0, 0))]

    def f(t):
        bl = blink_at(t, (0.6, 3.0, 4.9, 5.15, 7.1), 0.18)
        tw = keys([(0, 0), (4.0, 0), (4.1, 0.12), (4.2, -0.05), (4.32, 0.06), (4.45, 0)], t)
        br = math.sin(t * 2 * math.pi / 3.75) * 0.6
        a = b_face(t, look=keys(tl, t), blink=bl, ear_flop=(0, tw), cy=int(round(br)))
        return a
    return frames_for(7.5, f, 40)


def B_talk_face(t, mood="talk"):
    """The small face over the reply (rows 0..22): eyes 14x16, nose, a mouth that moves while talking."""
    a = new()
    for sgn in (-1, 1):
        x = 64 + sgn * 15
        if mood == "happy":
            a[thick(arc_pts(x, 11, 6, 5, 205, 335), 2.4)] = True
        else:
            b_eye(a, x, 9, 14, 16, look=(0.2, 0.1), blink=blink_at(t % 3.2, (2.5,), 0.18),
                  sad=0.9 if mood == "sad" else 0, side=sgn)
    b_nose(a, 64, 14, 0.7)
    if mood == "talk" and int(t * 7) % 2 == 0:
        a[17:19, 64] = True
        a[edge(ell(64.5, 20.5, 3, 2.2), False)] = True
    elif mood == "sad":
        a[path1(arc_pts(64.5, 21.5, 3, 2, 205, 335))] = True
    else:
        blit(a, MOUTH_W5, 62, 17)
    return a


B_TALK_BOX = (2, 24, 125, 62)  # where the reply goes: 3 lines of 13 px, the rest scrolls in


def talk_scroll(msg, box, lh=13, hold0=1.0, hold1=1.5, speed=16):
    """Line breaks and the scroll offset over time: the whole reply passes, word by word lines, never cut."""
    x0, y0, x1, y1 = box
    lines = px.wrap(msg, x1 - x0 + 1)
    view = y1 - y0 + 1
    total = len(lines) * lh
    end = max(0, total - view + 1)
    dur = hold0 + end / speed + hold1
    return lines, end, dur, (lambda t: 0 if t < hold0 else min(end, int((t - hold0) * speed)))


def draw_lines(a, lines, box, off, lh=13, path=px.UB, px_=11, anchor="l"):
    x0, y0, x1, y1 = box
    for j, ln in enumerate(lines):
        y = y0 + j * lh - off
        if y + lh < y0 or y > y1:
            continue
        x = x0 if anchor == "l" else (x0 + x1 + 1) / 2
        px.text(a, x, y, ln, path, px_, anchor=anchor, clip=box)


def B_talk_bg(t, md):
    return B_talk_face(t, md)


def B_talk(msg=TALK, mood="talk", end_mood="happy"):
    return talk_scene(B_talk_bg, B_TALK_BOX, msg, mood, end_mood)


def talk_scene(bg, box, msg, mood="talk", end_mood="happy"):
    """The face talks while the whole reply scrolls through the box, then holds on end_mood."""
    lines, end, dur, off = talk_scroll(msg, box)

    def f(t):
        talking = t < dur - 1.1
        a = bg(t, mood if talking else end_mood)
        draw_lines(a, lines, box, off(t))
        return a
    return frames_for(dur, f, 50)


def B_sad_talk():
    return B_sad() + B_talk(SORRY, "sad", "sad")


# =================================================================== C: pixel puppy
C_EYE = sprite([
    " .. ",
    ".#..",
    "....",
    "....",
    " .. ",
])
C_EYE_BIG = sprite([
    " ... ",
    ".##..",
    ".#...",
    ".....",
    ".....",
    " ... ",
])
C_EYE_HAPPY = sprite([
    " ... ",
    ".   .",
])
C_EYE_SHUT = sprite([
    ".   .",
    " ... ",
])
C_EYE_SAD = sprite([
    "..   ",
    "  .. ",
    " ... ",
    ".#...",
    ".....",
    " ... ",
])
C_NOSE = sprite([
    " ..... ",
    ".#.....",
    " ..... ",
    "  ...  ",
])
C_MOUTH = sprite([
    "   .   ",
    ".  .  .",
    " .. .. ",
])
C_MOUTH_OPEN = sprite([
    "   .   ",
    ". ... .",
    " ..... ",
    "  .#.  ",
    "   .   ",
])
C_MOUTH_TONGUE = sprite([
    "   .   ",
    ".  .  .",
    " ..... ",
    "  .#.  ",
    "  .#.  ",
    "   .   ",
])
C_MOUTH_FROWN = sprite([
    "   .   ",
    "  ...  ",
    " .   . ",
])
C_MOUTH_FLAT = sprite([
    "   .   ",
    "  ...  ",
])


class Pup:
    """The chibi puppy. x = the middle, base = the ground row under its paws. Parts are masks built at 1x,
    front parts cut a 1-px black seam into what is behind them; fur shadow is a dither rim on the lower right,
    ears and the eye patch are brown (50% dither)."""

    def __init__(self, x=64, base=58):
        self.x, self.base = x, base

    def draw(self, a, pose="sit", eyes="open", mouth="w", ears=0.0, tilt=0.0, look=(0, 0), tail=0.0, hop=0,
             blink=False, squash=0.0, collar=True, shadow=True):
        x, base = self.x, self.base - hop
        hx, hy = x, base - 31 + (8 if pose == "lie" else 0) + (3 if pose == "sad" else 0)
        sq = squash
        parts = []  # back to front: (mask, kind)
        # tail: a tapered curve behind the body, swinging with `tail` (-1..1)
        if pose != "lie":
            ang = math.radians(-35 + tail * 30) if pose != "sad" else math.radians(40)
            tx0, ty0 = x + 9, base - 6
            tx1, ty1 = tx0 + math.cos(ang) * 10, ty0 + math.sin(ang) * 10
            parts.append((taper(tx0, ty0, 2.6, tx1, ty1, 1.6), "fur"))
        # body
        if pose == "lie":  # lying on its belly, head toward us, the body stretched out behind it to the right
            body = ell(x + 28, base - 8, 23, 8.5)
            parts.append((body, "fur"))
            tl = taper(x + 49, base - 6, 2.4, x + 57, base - 4, 1.6)
            parts.append((tl, "fur"))
            parts.append((ell(x + 41, base - 8, 8, 7), "fur_top"))  # the back leg's haunch: a seam on its top only
            paws = ell(x - 6, base - 2, 5.5, 2.6) | ell(x + 6, base - 2, 5.5, 2.6)
        else:
            body = ell(x, base - 10 + sq * 2, 11 + sq * 2, 11 - sq * 2) | ell(x - 7.5, base - 5, 6.5, 5.5) | ell(x + 7.5, base - 5, 6.5, 5.5)
            parts.append((body, "fur"))
            paws = None
        head_y_extra = 0
        # head with tilt
        c, s_ = math.cos(math.radians(tilt)), math.sin(math.radians(tilt))

        def T(dx, dy):
            return hx + dx * c - dy * s_, hy + dx * s_ + dy * c
        rt = -math.radians(tilt)
        head = ell(*T(0, 0), 15 + sq, 11.5 - sq, rot=rt) | ell(*T(-6, 4), 9.5, 8, rot=rt) | ell(*T(6, 4), 9.5, 8, rot=rt)
        parts.append((head, "fur"))
        # ears: floppy, hanging over the sides of the head; `ears` 0..1 lifts them outward
        for sgn in (-1, 1):
            e = ears if sgn > 0 else ears * (1.0 if tilt == 0 else 0.6)
            ax, ay = T(sgn * 11, -9)
            ang = math.radians(sgn * (18 + e * 62) + tilt)
            ln = 15 - e * 4
            ex, ey = ax + math.sin(ang) * ln, ay + math.cos(ang) * ln
            parts.append((taper(ax, ay, 4.2, ex, ey, 5.6), "ear"))
        # draw: fur white, ears dithered, a seam ring between overlapping parts
        sil = np.zeros_like(a)
        for m, kind in parts:
            a[m] = False
            a[outer(m) & sil & ((px.YC < np.nonzero(m.any(1))[0].mean()) if kind == "fur_top" else True)] = False
            if kind in ("fur", "fur_top"):
                a[m] = True
            else:
                a[m & dith(8)] = True
                a[m & dith(14) & ~px.erode(m)] = True
            sil |= m
        # fur shadow on the lower right of the body (away from a top-left light)
        a[rim(body, 2, 2) & ~head & ~dilate(head) & ~dith(12)] = False
        if pose != "lie":
            # front legs: the gap between them, a short curve where each haunch begins, toes
            a[rect(x, base - 8, x, base - 1)] = False
            for sgn in (-1, 1):
                a[path1([(x + sgn * 5 + 0.5, base - 8.5), (x + sgn * 6 + 0.5, base - 6), (x + sgn * 6 + 0.5, base - 3.5)])] = False
                a[base - 2:base, x + sgn * 3 - (1 if sgn < 0 else 0)] = False
        else:
            a[outer(paws) & sil] = False
            a[paws] = True
            for sgn in (-1, 1):
                for tx in (-2, 0, 2):
                    a[base - 2:base, int(x + sgn * 6 + tx)] = False
            sil |= paws
        # a brown patch round its left eye (our right), the eye kept clear with a white ring
        ex0, ey0 = T(6 + look[0] * 1.2, -0.5 + look[1])
        patch = ell(ex0, ey0, 5.0, 4.4) & head
        a[patch & ~dith(8)] = False
        # collar: a black band with a little white tag
        if collar and pose != "lie":
            cx_, cy_ = T(0, 11)
            band = ell(cx_, cy_, 9, 1.6) & ~head
            a[band] = False
            a[rrect(int(cx_) - 1, int(cy_) + 1, int(cx_) + 1, int(cy_) + 3, 1)] = True
            a[outer(rrect(int(cx_) - 1, int(cy_) + 1, int(cx_) + 1, int(cy_) + 3, 1)) & sil] = False
        # face sprites
        lx, ly = look
        for sgn in (-1, 1):
            exx, eyy = T(sgn * 6 + lx * 1.2, -1 + ly)
            ex_, ey_ = int(round(exx)), int(round(eyy))
            a[outer(rect(ex_ - 2, ey_ - 2, ex_ + 2, ey_ + 3)) & False] = False
            if eyes == "open" and not blink:
                if sgn > 0:
                    a[outer(px.spr_mask(C_EYE, ex_ - 2, ey_ - 2))] = True
                blit(a, C_EYE, ex_ - 2, ey_ - 2)
            elif eyes == "big" and not blink:
                if sgn > 0:
                    a[outer(px.spr_mask(C_EYE_BIG, ex_ - 2, ey_ - 3))] = True
                blit(a, C_EYE_BIG, ex_ - 2, ey_ - 3)
            elif eyes == "happy":
                blit(a, C_EYE_HAPPY, ex_ - 2, ey_ - 1)
            elif eyes == "sad":
                blit(a, C_EYE_SAD, ex_ - 2, ey_ - 4, flip=sgn > 0)
            else:  # shut / blink
                blit(a, C_EYE_SHUT, ex_ - 2, ey_)
        nx, ny = T(0, 4)
        nx, ny = int(round(nx)), int(round(ny))
        blit(a, C_NOSE, nx - 3, ny - 1)
        m = {"w": C_MOUTH, "open": C_MOUTH_OPEN, "tongue": C_MOUTH_TONGUE, "frown": C_MOUTH_FROWN,
             "flat": C_MOUTH_FLAT}[mouth]
        blit(a, m, nx - 3, ny + 3)
        self.sil = sil
        self.head = head
        return sil


def pup_run(a, x, base, ph, facing=1, tongue=True):
    """The same beagle pup seen from the side, running (gallop): ph 0..1 is one stride. x = the middle of the
    body, base = the ground. facing 1 = right, -1 = left. Far legs are dithered (depth), the near legs have seams."""
    tmp = np.zeros_like(a)
    m = new()
    bob = -abs(math.sin(ph * 2 * math.pi)) * 2
    by = base - 12 + bob
    sw = math.sin(ph * 2 * math.pi)

    def leg(hx, hy, ang, ln=8.0):
        ex, ey = hx + math.sin(ang) * ln, hy + math.cos(ang) * ln
        return taper(hx, hy, 2.6, ex, ey, 2.0)
    # facing right while drawing; flipped at the end
    by = by + 1
    far = leg(x + 5, by + 2, -sw * 0.7 + 0.15) | leg(x - 6, by + 2, sw * 0.7 - 0.1)
    near = leg(x + 7, by + 2, sw * 0.7 + 0.15) | leg(x - 4, by + 2, -sw * 0.7 - 0.1)
    body = ell(x, by, 10.5, 6.5) | ell(x + 6, by - 1, 6.5, 6.5)
    head = ell(x + 13, by - 9, 8, 7.5) | ell(x + 20, by - 6, 4.2, 3.2)
    tail = taper(x - 9, by - 2, 2.2, x - 14 + sw, by - 8 + sw * 2, 1.4)
    ear = taper(x + 10, by - 14, 2.6, x + 8 - sw * 1.5, by - 7 - abs(sw) * 2, 3.4)
    sil = np.zeros_like(a)
    for mk, kind in ((far, "far"), (tail, "fur"), (body, "fur"), (near, "leg"), (head, "fur"), (ear, "ear")):
        tmp[mk] = False
        if kind == "leg":  # a seam only where the leg crosses the far legs, never into the body
            tmp[outer(mk) & sil & ~body] = False
        elif kind != "ear":
            tmp[outer(mk) & sil] = False
        if kind in ("fur", "leg"):
            tmp[mk] = True
        elif kind == "far":
            tmp[mk & dith(8)] = True
            tmp[edge(mk, False)] = True
        else:  # the ear: brown, with a light rim where it meets the black
            tmp[mk & dith(8)] = True
            tmp[edge(mk, False) & ~head] = True
        sil |= mk
    # face: a big eye with a shine, nose, mouth, tongue
    ex, ey = int(x + 16), int(by - 11)
    tmp[ey:ey + 3, ex:ex + 2] = False
    tmp[int(by - 8):int(by - 6), int(x + 23):int(x + 25)] = False  # nose
    tmp[int(by - 4), int(x + 19):int(x + 22)] = False  # mouth line
    if tongue:
        tg = rrect(int(x + 19), int(by - 3), int(x + 21), int(by + 1), 1)
        tmp[outer(tg) & ~sil] = False
        tmp[tg] = True
        tmp[edge(tg, False)] = False
        tmp[int(by - 2):int(by + 1), int(x + 20)] = True
        sil |= tg
    # collar
    tmp[path1([(x + 9, by - 6), (x + 11, by + 1)])] = False
    if facing < 0:
        tmp = tmp[:, ::-1]
        sil = sil[:, ::-1]
        shift_x = 2 * x - W + 1
        tmp = shift(tmp, int(round(shift_x)), 0)
        sil = shift(sil, int(round(shift_x)), 0)
    a[dilate(sil) & ~sil] = False
    a[sil] = tmp[sil]
    return sil


def ground(a, y=59, level=3):
    """A floor: a solid line with a dithered band under it (lighter toward the bottom)."""
    a[y, :] = True
    a[y + 1:, :] &= False
    a[y + 2:H, :] |= dith(level)[y + 2:H, :]


def shadow(a, x, y, rx, ry=1.5, level=6):
    m = ell(x, y, rx, ry)
    a[m] = False
    a[m & dith(level)] = True


def cushion(a, x0, x1, y):
    """A round dog bed seen from the front: a soft rim (outline + sparse dither), the cushion's top a band of dots."""
    rim_ = rrect(x0, y - 8, x1, y + 1, 5)
    a[rim_] = False
    a[rim_ & dith(2)] = True
    a[edge(rim_)] = True
    top = rrect(x0 + 5, y - 11, x1 - 5, y - 6, 3)
    a[top & ~rim_] = True
    a[edge(top) & rim_] = True
    # piping along the rim
    a[y - 4, x0 + 3:x1 - 2] = (px.XX[y - 4, x0 + 3:x1 - 2].astype(int) % 2 == 0)


def bowl(a, x, y):
    """A food bowl on the floor (x = middle, y = floor): a rim, a dithered side, a mound of kibble."""
    body = poly([(x - 12, y - 7), (x + 12, y - 7), (x + 8, y), (x - 8, y)])
    a[body] = True
    a[body & (px.XC > x + 2) & ~dith(10)] = False
    a[y - 4, x - 9:x + 10] = False
    mound = ell(x, y - 7, 9, 3.5) & (px.YC < y - 7)
    a[mound] = True
    for dx, dy in ((-5, -8), (-1, -9), (3, -8), (6, -8), (1, -10), (-3, -10)):
        a[y + dy, x + dx] = False
    a[ell(x, y - 7, 12, 1.2) & ~mound] = True


BALL = sprite([
    "   ####   ",
    "  ######  ",
    " ##..#### ",
    "##.##.####",
    "#.####.###",
    "####.##.o#",
    "#####..oo#",
    " ###oooo# ",
    "  #oooo#  ",
    "   ####   ",
])


def ball(a, x, y, frame=0):
    """A ball (x, y = top left), its stripe turning with `frame`."""
    s = [BALL, BALL[:, ::-1], BALL[::-1, :][:, ::-1], BALL[::-1, :]][frame % 4]
    s = BALL if frame % 2 == 0 else BALL[:, ::-1]
    blit(a, s, x, y)


def kennel(a, x, y):
    """A dog house (x = left, y = ground): plank walls, a dithered roof, an arched black door."""
    w = 34
    walls = rect(x + 2, y - 20, x + w - 2, y - 1)
    roof = poly([(x - 2, y - 19), (x + w // 2, y - 33), (x + w + 2, y - 19)])
    a[walls] = True
    for yy in range(y - 17, y - 1, 4):
        a[yy, x + 3:x + w - 2] = False
    a[roof] = False
    a[roof & dith(6)] = True
    a[edge(roof)] = True
    a[outer(roof) & walls] = False
    door = rrect(x + w // 2 - 7, y - 15, x + w // 2 + 7, y - 1, 7) | rect(x + w // 2 - 7, y - 8, x + w // 2 + 7, y - 1)
    a[door] = False
    a[outer(door) & walls] = True
    # a name plate over the door
    a[rect(x + w // 2 - 4, y - 19, x + w // 2 + 4, y - 17)] = False
    a[y - 18, x + w // 2 - 3:x + w // 2 + 4] = True


def bubble(a, x0, y0, x1, y1, tail):
    """A speech bubble with a 1-px rim and rounded corners; tail = (x, y) of its tip on the left."""
    b = rrect(x0, y0, x1, y1, 5)
    a[b] = False
    a[edge(b)] = True
    tx, ty = tail
    t = poly([(x0 + 1, ty - 4), (tx, ty), (x0 + 1, ty + 1)])
    a[t] = False
    a[path1([(x0 + 0.5, ty - 3.5), (tx + 0.5, ty + 0.5)])] = True
    a[path1([(tx + 0.5, ty + 0.5), (x0 + 0.5, ty + 1.5)])] = True
    a[ty - 3:ty + 1, x0] = False


def thought(a, x0, y0, x1, y1, dots):
    """A cloud-edged thought bubble and the trail of dots toward the thinker (dots = [(x, y, r)])."""
    b = rrect(x0, y0, x1, y1, 7)
    for i in range(x0 + 6, x1 - 4, 7):
        b |= circ(i + 0.5, y0 + 1, 3.2) | circ(i + 3.5, y1, 3.2)
    for j in range(y0 + 5, y1 - 2, 6):
        b |= circ(x0 + 1, j, 3.0) | circ(x1, j + 2, 3.0)
    a[b] = False
    a[edge(b)] = True
    for x, y, r in dots:
        m = circ(x, y, r)
        a[m] = False
        a[edge(m, False) if r > 1.5 else m] = True
    return b


BONE = sprite([
    " ##          ## ",
    "####        ####",
    " ############## ",
    " ############## ",
    "####        ####",
    " ##          ## ",
])


def C_sleep():
    def f(t):
        a = new()
        ground(a, 59)
        cushion(a, 14, 114, 58)
        br = math.sin(t * 2 * math.pi / 2.6)
        p = Pup(38, 52)
        p.draw(a, pose="lie", eyes="shut", mouth="flat", ears=0.0, collar=False)
        if br > 0.2:  # breathing: the back rises a pixel
            back = ell(66, 43, 23, 8.5) & ~p.head & ~dilate(p.head)
            a[edge(back) & (px.YC < 43)] = True
        zzz(a, t, 60, 18)
        return a
    return frames_for(5.2, f, 80)


def C_listen():
    def f(t):
        a = new()
        ground(a)
        tilt = keys([(0, 0), (0.3, 0), (0.6, -11), (2.4, -11), (2.7, 9), (4.2, 9), (4.5, 0)], t)
        ears = keys([(0, 0), (0.25, 0), (0.45, 0.55), (4.5, 0.55)], t, px.back_out)
        hop = int(round(keys([(0, 0), (0.15, 0), (0.25, 2), (0.4, 0)], t)))
        p = Pup(64, 58)
        shadow(a, 64, 59, 15)
        p.draw(a, eyes="big", ears=ears, tilt=tilt, tail=math.sin(t * 14), mouth="w", hop=hop)
        if t < 1.0:  # the "!" pops over its head
            k = px.back_out(min(1, t / 0.25))
            y = int(10 - 4 * k)
            if t < 0.9:
                a[rect(63, y, 65, y + 6 * k)] = True
                a[rect(63, y + 8, 65, y + 10)] = True
        # sound arcs reaching its ears from both sides
        for sgn in (-1, 1):
            for j in range(3):
                ph = (t * 1.3 + j / 3) % 1.0
                r = 4 + (1 - ph) * 16
                if ph < 0.85 and t > 0.4:
                    cx = 64 + sgn * 46
                    a0, a1 = (150, 210) if sgn > 0 else (-30, 30)
                    a[path1(arc_pts(cx - sgn * 20 + sgn * 20, 30, r, r, a0, a1))] = True
        return a
    return frames_for(4.5, f, 60)


def C_think():
    def f(t):
        a = new()
        ground(a)
        kennel(a, 90, 59)
        p = Pup(40, 58)
        shadow(a, 40, 59, 15)
        lk = (1, -1) if (t % 4) < 3.3 else (0, 0)
        p.draw(a, eyes="open", ears=0.1, tilt=6, look=lk, mouth="flat", blink=blink_at(t % 4, (3.5,), 0.2) > 0.3)
        n = int(t * 3) % 6
        dots = [(56, 16, 1.2), (61, 11, 2.0)][:max(0, min(2, n))]
        if n >= 2:
            thought(a, 66, 1, 104, 19, dots)
            if n >= 3:
                blit(a, BONE, 77, 7)
        else:
            for x, y, r in dots:
                a[circ(x, y, r)] = True
        return a
    return frames_for(4.0, f, 80)


C_TALK_BOX = (50, 5, 123, 56)  # 4 whole lines of 13 px inside the bubble


def C_talk(msg=TALK, mood="talk", end_mood="happy"):
    return talk_scene(C_talk_bg, C_TALK_BOX, msg, mood, end_mood)


def C_talk_bg(t, md):
    if True:
        a = new()
        ground(a, 60)
        p = Pup(22, 59)
        shadow(a, 22, 60, 14)
        eyes = {"talk": "open", "happy": "happy", "sad": "sad"}[md]
        mouth = ("open" if int(t * 7) % 2 == 0 else "w") if md == "talk" else {"happy": "tongue", "sad": "frown"}[md]
        p.draw(a, eyes=eyes, mouth=mouth, ears=0.05, tail=math.sin(t * 9) if md != "sad" else 0, tilt=0,
               blink=blink_at(t % 3.6, (3.2,), 0.16) > 0.3)
        bubble(a, 46, 1, 127, 58, (39, 33))
        return a


HEART_C = sprite([
    " ## ## ",
    "##o####",
    "#o#####",
    "#######",
    " ##### ",
    "  ###  ",
    "   #   ",
])


def C_happy():
    def f(t):
        a = new()
        ground(a)
        ph = (t * 1.6) % 1.0
        hop = int(round(abs(math.sin(ph * math.pi)) * 9))
        sq = 0.6 if hop == 0 else (-0.3 if hop > 6 else 0)
        shadow(a, 64, 59, 15 - hop * 0.6)
        Pup(64, 58).draw(a, eyes="happy", mouth="tongue", ears=0.2 + hop / 12, tail=math.sin(t * 20), hop=hop, squash=sq)
        for x, p0 in ((22, 0.0), (104, 0.45), (36, 0.7), (94, 0.2)):
            pp = (t * 0.7 + p0) % 1.0
            y = int(56 - pp * 52)
            if pp < 0.9:
                blit(a, HEART_C, x + int(math.sin(pp * 9) * 2) - 3, y)
        return a
    return frames_for(2.5, f, 50)


CLOUD = sprite([
    "        #####          ",
    "      ##ooooo##        ",
    "  ### #ooooooooo# ###  ",
    " #ooo#ooooooooooo#ooo# ",
    "#ooooooooooooooooooooo#",
    "#ooooooooooooooooooooo#",
    " ##################### ",
])


def C_sad():
    def f(t):
        a = new()
        ground(a)
        shadow(a, 64, 59, 15)
        Pup(64, 58).draw(a, pose="sad", eyes="sad", mouth="frown", ears=0.0, tail=0, look=(0, 1))
        cx = 64 + int(round(math.sin(t * 1.5) * 2))
        blit(a, CLOUD, cx - 11, 0)
        for i, dx in enumerate((-7, -2, 3, 8)):
            ph = (t * 2.0 + i * 0.29) % 1.0
            y = int(8 + ph * 10)
            a[y:y + 2, cx + dx] = True
        return a
    return frames_for(2.4, f, 60)


def C_sad_talk():
    return C_sad() + C_talk(SORRY, "sad", "sad")


def C_idle():
    """With its ball and bowl: the ball rolls by, its eyes follow, it wags, blinks."""
    def f(t):
        a = new()
        ground(a)
        bowl(a, 17, 59)
        bx = keys([(0, 104), (1.2, 104), (2.6, 82), (4.4, 82), (5.8, 104), (8, 104)], t)
        rollf = int(bx / 3)
        ball(a, int(bx), 49, rollf)
        a[outer(px.spr_mask(BALL, int(bx), 49)) & (px.YY < 59)] = False
        p = Pup(56, 58)
        shadow(a, 56, 59, 15)
        lk = (1 if bx > 90 else 0.5, 0.6)
        wag = math.sin(t * 16) if 2.2 < t < 4.6 else 0.2
        tilt = keys([(0, 0), (4.4, 0), (4.7, 9), (5.6, 9), (5.9, 0)], t)
        p.draw(a, eyes="open", look=lk, tail=wag, mouth="w", tilt=tilt, blink=blink_at(t, (1.0, 3.9, 6.8), 0.16) > 0.3)
        return a
    return frames_for(8, f, 80)


# =================================================================== D: thin line art
D_EYE = sprite([
    " ## ",
    "####",
    "#.##",
    "####",
    " ## ",
])
D_EYE_BIG = sprite([
    " ### ",
    "#####",
    "#..##",
    "#.###",
    "#####",
    " ### ",
])
D_NOSE = sprite([
    " ##### ",
    "#.#####",
    "#######",
    " ##### ",
    "  ###  ",
])
D_MOUTH_W = sprite([
    "    #    ",
    "    #    ",
    "#  # #  #",
    " ##   ## ",
])
D_MOUTH_HAPPY = sprite([
    "    #    ",
    "#  ###  #",
    " #.....# ",
    " #.ooo.# ",
    "  #ooo#  ",
    "   ###   ",
])
D_MOUTH_OPEN = sprite([
    "    #    ",
    "   ###   ",
    "  #...#  ",
    "  #...#  ",
    "   ###   ",
])
D_MOUTH_FLAT = sprite([
    "    #    ",
    "  #####  ",
])
D_MOUTH_FROWN = sprite([
    "    #    ",
    "   ###   ",
    "  #   #  ",
])
HEART_O = sprite([
    " ## ## ",
    "#  #  #",
    "#     #",
    " #   # ",
    "  # #  ",
    "   #   ",
])


def d_head(a, cx=64, cy=30, k=1.0, eyes="open", look=(0, 0), blink=0.0, ears=0.0, mouth="w", brows=None,
           tilt=0.0, collar=True, twitch=0.0):
    """The line-art dog head. cx, cy = the middle of the skull; k scales it. 1-px outlines everywhere;
    the ear linings and the collar are dithered accents."""
    c, s_ = math.cos(math.radians(tilt)), math.sin(math.radians(tilt))

    def T(dx, dy):
        return cx + (dx * c - dy * s_) * k, cy + (dx * s_ + dy * c) * k
    rt = -math.radians(tilt)
    skull = ell(*T(0, 0), 19 * k, 15 * k, rot=rt)
    cheeks = ell(*T(-9, 7), 11 * k, 10 * k, rot=rt) | ell(*T(9, 7), 11 * k, 10 * k, rot=rt)
    muzzle = ell(*T(0, 11), 10 * k, 7.5 * k, rot=rt)
    head = skull | cheeks | muzzle
    # ears behind the head: long, floppy, a dithered lining; `ears` lifts them outward
    for sgn in (-1, 1):
        e = ears + (twitch if sgn > 0 else 0)
        ax, ay = T(sgn * 14, -10)
        ang = math.radians(sgn * (14 + e * 55) + tilt)
        ln = (27 - e * 6) * k
        ex, ey = ax + math.sin(ang) * ln, ay + math.cos(ang) * ln
        ear = taper(ax, ay, 5.5 * k, ex, ey, 7.5 * k)
        lining = taper(ax + sgn * 0.5, ay + 4 * k, 2.5 * k, ex - math.sin(ang) * 3 * k, ey - math.cos(ang) * 3 * k, 4.5 * k)
        a[ear] = False
        a[(lining & ear & ~head) & dith(5)] = True
        a[edge(ear) & ~head] = True
    a[head] = False
    a[edge(head)] = True
    # whisker dots on the muzzle
    for sgn in (-1, 1):
        for dx, dy in ((7, 10), (9, 12), (6, 13)):
            wx, wy = T(sgn * dx, dy)
            if k >= 0.9:
                a[int(wy), int(wx)] = True
    # collar under the chin
    if collar:
        band = ell(*T(0, 20), 13 * k, 3 * k, rot=rt) & ~head
        a[band] = False
        a[band & dith(8)] = True
        a[edge(band) & ~head] = True
        tx, ty = T(0, 23)
        tag = circ(tx, ty + 1.5 * k, 2.6 * k)
        a[tag] = False
        a[edge(tag, False)] = True
        a[int(ty + 1.5 * k), int(tx)] = True
    # eyes
    lx, ly = look
    for sgn in (-1, 1):
        ex_, ey_ = T(sgn * 8 + lx * 1.5, -2 + ly * 1.2)
        ex_, ey_ = int(round(ex_)), int(round(ey_))
        if eyes == "open" and blink < 0.5:
            blit(a, D_EYE, ex_ - 2, ey_ - 2)
        elif eyes == "big" and blink < 0.5:
            blit(a, D_EYE_BIG, ex_ - 2, ey_ - 3)
        elif eyes == "happy":
            a[path1(arc_pts(ex_ + 0.5, ey_ + 2.5, 3.5 * k, 3 * k, 200, 340))] = True
        elif eyes == "sad":
            blit(a, D_EYE, ex_ - 2, ey_ - 1)
            a[ey_ - 2, ex_ - 2:ex_ + 3] = False
        else:  # shut, blink
            a[path1(arc_pts(ex_ + 0.5, ey_ - 0.5, 3.5 * k, 2.5 * k, 20, 160))] = True
        if brows == "sad":
            a[path1([T(sgn * 4, -8), T(sgn * 11, -6)])] = True
        elif brows == "think" and sgn < 0:
            a[path1([T(sgn * 4, -9), T(sgn * 11, -10)])] = True
        elif brows == "up":
            a[path1(arc_pts(*T(sgn * 8, -6), 3.5 * k, 2 * k, 200, 340))] = True
    # nose and mouth
    nx, ny = T(0, 6.5)
    nx, ny = int(round(nx)), int(round(ny))
    blit(a, D_NOSE, nx - 3, ny - 1)
    my = ny + 4
    spr = {"w": D_MOUTH_W, "open": D_MOUTH_OPEN, "tongue": D_MOUTH_HAPPY, "flat": D_MOUTH_FLAT,
           "frown": D_MOUTH_FROWN}[mouth]
    a[px.spr_mask(spr, nx - 4, my - 1) & px.dith(16)] = False
    blit(a, spr, nx - 4, my - 1)
    return head


Z_O = sprite([
    "#####",
    "#...#",
    "##.##",
    " #.# ",
    "##.##",
    "#...#",
    "#####",
])


def D_sleep():
    def f(t):
        a = new()
        br = math.sin(t * 2 * math.pi / 3.0)
        d_head(a, cy=29 + int(round(br + 1)), eyes="shut", mouth="flat", ears=-0.05, tilt=-4)
        for i, sz in enumerate((0, 1, 2)):
            ph = (t * 0.45 + i / 3.0) % 1.0
            if ph < 0.85:
                x, y = int(90 + i * 8 + ph * 5), int(20 - i * 7 - ph * 6)
                blit(a, (Z_SM, Z_MID, Z_BIG)[sz], x, y)
        return a
    return frames_for(6, f, 60)


def D_listen():
    def f(t):
        a = new()
        ears = keys([(0, 0), (0.2, 0), (0.45, 0.6)], t, px.back_out)
        tilt = keys([(0, 0), (0.5, 0), (0.8, 8), (2.6, 8), (2.9, -6), (4.5, -6)], t)
        d_head(a, eyes="big" if t > 0.15 else "shut", ears=ears, tilt=tilt, brows="up", mouth="w", look=(0, -0.5))
        for sgn in (-1, 1):
            for j in range(3):
                ph = (t * 1.4 + j / 3) % 1.0
                r = 3 + (1 - ph) * 10
                if ph < 0.85 and t > 0.4:
                    cx = 64 + sgn * 54
                    a0, a1 = (150, 210) if sgn > 0 else (-30, 30)
                    a[path1(arc_pts(cx, 30, r, r * 1.2, a0, a1))] = True
        return a
    return frames_for(4.5, f, 40)


QMARK = sprite([
    " ### ",
    "#   #",
    "    #",
    "   # ",
    "  #  ",
    "     ",
    "  #  ",
])


def D_think():
    def f(t):
        a = new()
        lk = keys([(0, (0, 0)), (0.3, (-1, -1)), (2.0, (-1, -1)), (2.3, (1, -1)), (4, (1, -1))], t)
        d_head(a, look=lk, brows="think", mouth="flat", tilt=-5, ears=0.05)
        for j in range(3):
            ph = (t * 2 - j * 0.25) % 1.0
            x, y = 98 + j * 7, 8 - j * 2
            m = circ(x, y, 1.6 + j * 0.4)
            a[edge(m, False) if ph > 0.5 else m] = True
        if (t % 2) < 1.4:
            blit(a, QMARK, 117, 14)
        return a
    return frames_for(4, f, 40)


D_TALK_BOX = (50, 5, 126, 56)  # 4 whole lines of 13 px


def D_talk(msg=TALK, mood="talk", end_mood="happy"):
    return talk_scene(D_talk_bg, D_TALK_BOX, msg, mood, end_mood)


def D_talk_bg(t, md):
    if True:
        a = new()
        eyes = {"talk": "open", "happy": "happy", "sad": "sad"}[md]
        mouth = ("open" if int(t * 7) % 2 == 0 else "w") if md == "talk" else {"happy": "tongue", "sad": "frown"}[md]
        d_head(a, cx=23, cy=26, k=0.78, eyes=eyes, mouth=mouth, brows="sad" if md == "sad" else None,
               blink=blink_at(t % 3.4, (3.0,), 0.16))
        a[rect(46, 6, 46, 58) & px.hlines(2)] = True
        return a


def D_happy():
    def f(t):
        a = new()
        ph = (t * 2.0) % 1.0
        bo = -int(round(abs(math.sin(ph * math.pi)) * 3))
        d_head(a, cy=30 + bo, eyes="happy", mouth="tongue", ears=0.35 + 0.3 * abs(math.sin(ph * math.pi)), tilt=math.sin(t * 4) * 4)
        for x, p0 in ((14, 0.0), (112, 0.5), (24, 0.75), (104, 0.25)):
            pp = (t * 0.8 + p0) % 1.0
            if pp < 0.85:
                blit(a, HEART_O, x - 3 + int(math.sin(pp * 8) * 2), int(54 - pp * 50))
        return a
    return frames_for(3.0, f, 40)


TEAR_O = sprite([
    "  #  ",
    " # # ",
    "#   #",
    "#   #",
    " ### ",
])


def D_sad():
    def f(t):
        a = new()
        lk = keys([(0, (0, 0)), (0.6, (-0.5, 1)), (1.8, (0.5, 1)), (2.4, (0, 1))], t)
        d_head(a, cy=31, eyes="sad", brows="sad", mouth="frown", ears=-0.12, look=lk)
        ty = keys([(0, 33), (0.6, 33), (2.3, 52)], t)
        if 0.6 < t < 2.3:
            blit(a, TEAR_O, 48, int(ty))
        return a
    return frames_for(2.4, f, 40)


def D_sad_talk():
    return D_sad() + D_talk(SORRY, "sad", "sad")


def D_idle():
    tl = [(0, (0, 0)), (1, (0, 0)), (1.25, (-1, 0)), (2.4, (-1, 0)), (2.65, (1, -0.6)), (3.8, (1, -0.6)),
          (4.05, (0, 0)), (7, (0, 0))]

    def f(t):
        a = new()
        tw = keys([(0, 0), (5.0, 0), (5.1, 0.25), (5.25, -0.05), (5.4, 0.12), (5.6, 0)], t)
        tilt = keys([(0, 0), (4.2, 0), (4.5, 7), (5.8, 7), (6.1, 0)], t)
        d_head(a, look=keys(tl, t), blink=blink_at(t, (0.6, 3.1, 6.2), 0.2), twitch=tw, tilt=tilt)
        return a
    return frames_for(7, f, 40)


# =================================================================== registry
LOOKS = {}


def look(key, name, scenes):
    LOOKS[key] = (name, scenes)


look("B", "Mắt to", [("Ngủ", "sleep", B_sleep), ("Gọi gâu gâu", "listen", B_listen), ("Đang nghĩ", "think", B_think),
                     ("Trả lời", "talk", B_talk), ("Vui", "happy", lambda: B_happy() * 2),
                     ("Chưa nghe rõ", "sad", B_sad_talk), ("Chờ", "idle", B_idle)])
look("C", "Chó pixel", [("Ngủ", "sleep", C_sleep), ("Gọi gâu gâu", "listen", C_listen), ("Đang nghĩ", "think", C_think),
                        ("Trả lời", "talk", C_talk), ("Vui", "happy", lambda: C_happy() * 2),
                        ("Chưa nghe rõ", "sad", C_sad_talk), ("Chơi", "idle", C_idle)])
look("D", "Nét mảnh", [("Ngủ", "sleep", D_sleep), ("Gọi gâu gâu", "listen", D_listen), ("Đang nghĩ", "think", D_think),
                       ("Trả lời", "talk", D_talk), ("Vui", "happy", lambda: D_happy() * 2),
                       ("Chưa nghe rõ", "sad", D_sad_talk), ("Chờ", "idle", D_idle)])


# =================================================================== for the firmware: scenes without live text
def face_only(look_key):
    """{scene: [(canvas, ms)]} with no reply text in them, + where the reply goes. sad = the face part only."""
    talk_bg = {"B": B_talk_bg, "C": C_talk_bg, "D": D_talk_bg}[look_key]
    box = {"B": B_TALK_BOX, "C": C_TALK_BOX, "D": D_TALK_BOX}[look_key]
    sc = {k: fn for _, k, fn in LOOKS[look_key][1]}
    sad = {"B": B_sad, "C": C_sad, "D": D_sad}[look_key]
    out = {k: sc[k]() for k in ("sleep", "listen", "think", "happy", "idle")}
    out["sad"] = sad()
    out["talk"] = frames_for(0.6, lambda t: talk_bg(t, "talk"), 50)
    out["talk_happy"] = [(talk_bg(0.0, "happy"), 1000)]
    out["talk_sad"] = [(talk_bg(0.0, "sad"), 1000)]
    return out, box
