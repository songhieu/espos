#!/usr/bin/env python3
"""Standby screens (màn chờ) for the 128x64 OLED, art2.

A Screen is a looping background (pre-rendered frames, no live text) + live fields the firmware draws on top
(time, date, weather, headlines...) + an optional overlay drawn after the live fields (the flip clock's split line).
Every live field is declared in LIVE (machine-readable, see art2.py -> art2_spec.json):

  digits  {set, x, y, align, color, colon: "blink"|"on"}   a string of a digit set from digits2.sets()
  text    {font: ub11|tiny5, box: [x0,y0,x1,y1], align, color, upper, wrap, overflow: marquee|scroll|none}
          one line that does not fit always scrolls sideways (marquee), never "..."; wrap: lines broken at
          spaces, centred in the box vertically, scrolling up when there are more lines than room
  hands   {cx, cy, h_len, m_len, s_len, h_w, m_w}             analog hands from hh, mm, ss
  progress {x0, x1, y, of: "ss"}                              a lit run along a line: seconds of the minute
  month   {box, cell_w, cell_h, first_row_y}                  the month grid, today inverted

Sample values (SAMPLE) are used for the previews and the board demo.
"""
import math

import numpy as np

import px
import digits2
from px import W, H, new, ell, circ, rrect, rect, poly, edge, outer, rim, dith, shift, dilate, blit, sprite, keys, \
    taper, ease, lerp, frames_for, blink_at, path1, thick, arc_pts, tiny, tiny_w

SAMPLE = dict(
    time="21:30", hh="21", mm="30", ss="42", day="20", month="11", year="2026",
    weekday="Thứ Sáu", weekday_short="T6", date_line="Thứ Sáu, 20 tháng 11", date_short="20/11",
    date_mid="Thứ Sáu 20/11",
    month_line="Tháng 11 · 2026", lunar="12/10", lunar_year="Bính Ngọ", lunar_line="Ngày 12 tháng 10 Bính Ngọ",
    temp="28°", temp_n="28", hi_lo="31° / 22°", weather="Nắng nhẹ", city="Hà Nội", rain="Chiều mưa rào",
    headline="Hà Nội: chiều nay có mưa rào rải rác, nhiệt độ giảm còn 22 độ vào ban đêm",
    quote="Có công mài sắt, có ngày nên kim.",
    gold_buy="148,5", gold_sell="150,5", gold_unit="triệu/lượng", usd="26.350", usd_label="USD bán ra",
    tet_days="78", tet_name="Tết Đinh Mùi", month_first_wd=6, month_days=30, today=20,
)


# ------------------------------------------------------------------ live fields
def digits(field, set_, x, y, align="l", color=True, colon="on"):
    return dict(kind="digits", field=field, set=set_, x=x, y=y, align=align, color=int(color), colon=colon)


def textf(field, font, box, align="l", color=True, upper=False, wrap=False, overflow="marquee"):
    return dict(kind="text", field=field, font=font, box=list(box), align=align, color=int(color), upper=upper,
                wrap=wrap, overflow=overflow)


def hands(cx, cy, h_len, m_len, s_len=0, h_w=2.6, m_w=1.6):
    return dict(kind="hands", field="hh:mm:ss", cx=cx, cy=cy, h_len=h_len, m_len=m_len, s_len=s_len, h_w=h_w, m_w=m_w)


def progress(x0, x1, y, of="ss"):
    return dict(kind="progress", field=of, x0=x0, x1=x1, y=y)


def font_h(font):
    return 13 if font == "ub11" else 9


def text_w(s, font):
    return px.tiny_w(s) if font == "tiny5" else px.text_w(s)


def draw_text(a, s, font, x, y, anchor="l", color=True, clip=None):
    if font == "tiny5":
        tiny(a, int(x), int(y), s, anchor, color, clip)
    else:
        px.text(a, x, y, s, anchor=anchor, color=color, clip=clip)


def draw_live(a, f, vals, t):
    k = f["kind"]
    if k == "digits":
        g = digits2.sets()[f["set"]]
        s = vals[f["field"]]
        skip = ":" if f.get("colon") == "blink" and (t % 1.0) >= 0.5 else ""
        g.draw(a, s, f["x"], f["y"], f.get("align", "l"), bool(f.get("color", 1)), skip)
    elif k == "text":
        s = vals[f["field"]]
        if f.get("upper"):
            s = s.upper()
        x0, y0, x1, y1 = f["box"]
        font, col = f["font"], bool(f.get("color", 1))
        bw = x1 - x0 + 1
        if f.get("wrap"):
            lines = px.wrap(s, bw, measure=lambda q: text_w(q, font))
            lh = font_h(font)
            total = len(lines) * lh
            room = y1 - y0 + 1
            if total <= room:
                yy = y0 + (room - total) // 2
                off = 0
            else:  # scroll up, holding at both ends
                end = total - room
                cyc = 1.5 + end / 12 + 2.0
                tt = t % cyc
                off = 0 if tt < 1.5 else min(end, int((tt - 1.5) * 12))
                yy = y0
            for j, ln in enumerate(lines):
                ax = {"l": x0, "m": (x0 + x1 + 1) / 2, "r": x1 + 1}[f.get("align", "l")]
                draw_text(a, ln, font, ax, yy + j * lh - off + (2 if font == "tiny5" else 0), f.get("align", "l"), col,
                          (x0, y0, x1, y1))
            return
        tw = text_w(s, font)
        ty = y0 + (2 if font == "tiny5" else 0)
        if tw <= bw or f.get("overflow") == "none":
            ax = {"l": x0, "m": (x0 + x1 + 1) / 2, "r": x1 + 1}[f.get("align", "l")]
            draw_text(a, s, font, ax, ty, f.get("align", "l"), col, (x0, y0, x1, y1))
        else:  # marquee: the whole line passes, a gap, again
            gap = 24
            span = tw + gap
            off = (t * 20) % span
            for rep in (0, 1):
                draw_text(a, s, font, x0 - off + rep * span, ty, "l", col, (x0, y0, x1, y1))
    elif k == "hands":
        hh, mm, ss = int(vals["hh"]), int(vals["mm"]), int(vals["ss"])
        cx, cy = f["cx"], f["cy"]
        am = (mm + ss / 60) / 60 * 2 * math.pi
        ah = ((hh % 12) + mm / 60) / 12 * 2 * math.pi
        for ang, ln, w in ((ah, f["h_len"], f["h_w"]), (am, f["m_len"], f["m_w"])):
            ex, ey = cx + math.sin(ang) * ln, cy - math.cos(ang) * ln
            m = thick([(cx, cy), (ex, ey)], w) if w > 1.2 else path1([(cx, cy), (ex, ey)])
            a[outer(m)] = False
            a[m] = True
        if f.get("s_len"):
            as_ = ss / 60 * 2 * math.pi
            a[path1([(cx - math.sin(as_) * 4, cy + math.cos(as_) * 4), (cx + math.sin(as_) * f["s_len"], cy - math.cos(as_) * f["s_len"])])] = True
        hub = circ(cx, cy, 2.6)
        a[hub] = True
        a[int(cy), int(cx)] = False
    elif k == "progress":
        v = int(vals[f["field"]]) + t % 1.0
        n = int((f["x1"] - f["x0"] + 1) * v / 60)
        a[f["y"], f["x0"]:f["x0"] + n] = True
    elif k == "month":
        draw_month(a, f, vals)


def draw_month(a, f, vals):
    """Monday first; up to 6 rows of 7 px; today in a white box with black digits."""
    x0, y0 = f["box"][0], f["first_row_y"]
    cw, ch = f["cell_w"], f["cell_h"]
    first, days, today = int(vals["month_first_wd"]), int(vals["month_days"]), int(vals["today"])
    for d in range(1, days + 1):
        i = first + d - 1
        col, row = i % 7, i // 7
        cx = x0 + col * cw + cw // 2
        y = y0 + row * ch
        s = str(d)
        if d == today:
            box = rrect(cx - 7, y - 1, cx + 7, y + 5, 2)
            a[box] = True
            tiny(a, cx + 1, y, s, "m", False)
        else:
            tiny(a, cx + 1, y, s, "m", True)


# ------------------------------------------------------------------ the screen type
class Screen:
    def __init__(self, sid, name, bg, seconds, ms, live=(), overlay=None, drift=False, note="", group="clock"):
        self.id, self.name, self.bg, self.seconds, self.ms = sid, name, bg, seconds, ms
        self.live, self.overlay, self.drift, self.note, self.group = list(live), overlay, drift, note, group

    def frames(self):
        """The background loop (no live text)."""
        return frames_for(self.seconds, self.bg, self.ms)

    def render(self, t, vals=SAMPLE):
        a = self.bg(t % self.seconds).copy()
        for f in self.live:
            draw_live(a, f, vals, t)
        if self.overlay:
            self.overlay(a, t)
        return a

    def preview(self, vals=SAMPLE, seconds=None, ms=None):
        ms = ms or self.ms
        n = max(1, int(round((seconds or self.seconds) * 1000 / ms)))
        return [(self.render(i * ms / 1000.0, vals), ms) for i in range(n)]


SCREENS = []


def screen(*args, **kw):
    s = Screen(*args, **kw)
    SCREENS.append(s)
    return s


# ------------------------------------------------------------------ small shared art
SUN_ICON = sprite([
    "    #    ",
    " #  #  # ",
    "  #####  ",
    "  #####  ",
    "#########",
    "  #####  ",
    "  #####  ",
    " #  #  # ",
    "    #    ",
])
DROP = sprite([
    " # ",
    "###",
    "###",
])


def dotted_h(a, x0, x1, y, step=2):
    a[y, x0:x1 + 1] = (np.arange(x0, x1 + 1) % step == 0)


# =================================================================== 1. big bold clock
def _bold_bg(t):
    a = new()
    dotted_h(a, 2, 125, 47)
    blit(a, SUN_ICON, 95, 52)
    return a


screen("clock_bold", "Đồng hồ đậm", _bold_bg, 1.0, 1000, [
    digits("time", "bold40", 64, 3, "m", colon="blink"),
    progress(2, 125, 47),
    textf("date_mid", "ub11", (2, 50, 90, 63)),
    textf("temp", "ub11", (106, 50, 127, 63), "r"),
], drift=True, note="Bold geometric digits; the dotted rule fills with the seconds.")


# =================================================================== 2. thin clock
def _thin_bg(t):
    a = new()
    dotted_h(a, 20, 107, 48, 3)
    return a


screen("clock_thin", "Đồng hồ mảnh", _thin_bg, 1.0, 1000, [
    digits("time", "thin40", 64, 2, "m", colon="blink"),
    progress(20, 107, 48),
    textf("date_line", "ub11", (0, 51, 127, 63), "m"),
], drift=True, note="1-px geometric digits, a hairline seconds rule.")


# =================================================================== 3. LED 7-segment
def _seg_bg(t):
    a = new()
    frame = rrect(0, 0, 127, 63, 5)
    a[edge(frame, False)] = True
    a[rect(2, 47, 125, 47) & px.vlines(2)] = True
    if (t % 1.0) < 0.5:  # the colon blinks in the loop itself
        a[17:20, 49:52] = True
        a[31:34, 49:52] = True
    return a


digits2.sets()["seg16"] = digits2.seg7_set("seg16", 9, 16, 2, ghost=False)

screen("clock_7seg", "Đồng hồ LED", _seg_bg, 1.0, 500, [
    digits("hh", "seg40", 3, 4),
    digits("mm", "seg40", 54, 4),
    digits("ss", "seg16", 125, 25, "r"),
    textf("weekday_short", "tiny5", (101, 7, 125, 15), "m", upper=True),
    textf("date_line", "tiny5", (3, 50, 124, 60), "m", upper=True),
],
    note="Bevelled segments with faint unlit segments, like a real LCD; seconds small at the right.")


# =================================================================== 4. dot matrix
def _dots_bg(t):
    a = new()
    return a


screen("clock_dots", "Đồng hồ chấm", _dots_bg, 1.0, 1000, [
    digits("time", "dots", 64, 5, "m", colon="blink"),
    textf("date_line", "tiny5", (0, 50, 127, 60), "m", upper=True),
], drift=True, note="Round-dot 5x7 matrix.")


# =================================================================== 5. analog dial
def _analog_bg(t):
    a = new()
    cx, cy, r = 32, 32, 30.5
    a[px.ring(cx, cy, r)] = True
    for i in range(60):
        ang = i / 60 * 2 * math.pi
        if i % 5 == 0:
            l0 = r - (6 if i % 15 == 0 else 4)
            w = 2.0 if i % 15 == 0 else 1
            pts = [(cx + math.sin(ang) * l0, cy - math.cos(ang) * l0), (cx + math.sin(ang) * (r - 2), cy - math.cos(ang) * (r - 2))]
            a[thick(pts, w) if w > 1 else path1(pts)] = True
        else:
            x, y = cx + math.sin(ang) * (r - 2.5), cy - math.cos(ang) * (r - 2.5)
            if i % 2 == 0:
                a[int(y), int(x)] = True
    tiny(a, cx + 1, 9, "12", "m")
    # the right: a calendar card
    a[rect(70, 2, 70, 61) & px.hlines(2)] = True
    return a


screen("clock_analog", "Đồng hồ kim", _analog_bg, 1.0, 1000, [
    hands(32, 32, 15, 23, 25),
    textf("weekday", "tiny5", (74, 4, 127, 12), "m", upper=True),
    digits("day", "bold24", 100, 16, "m"),
    textf("month_short", "tiny5", (74, 44, 127, 52), "m", upper=True),
    textf("lunar_short", "tiny5", (74, 55, 127, 63), "m", upper=True),
], note="A dial with minute dots; hands are live (hour, minute, second).")
SAMPLE["lunar_short"] = "ÂL 12/10"
SAMPLE["month_short"] = "Tháng 11"


# =================================================================== 6. flip clock
def _card(a, x0, y0, x1, y1):
    c = rrect(x0, y0, x1, y1, 4)
    a[c] = False
    a[c & (px.YC > (y0 + y1) / 2 + 1) & dith(2)] = True
    a[edge(c)] = True
    return c


def _flip_bg(t):
    a = new()
    for x0 in (3, 66):
        _card(a, x0, 4, x0 + 58, 55)
        # the stack of cards under it
        a[57, x0 + 3:x0 + 56] = True
        a[59, x0 + 6:x0 + 53] = True
    return a


def _flip_over(a, t):
    for x0 in (3, 66):
        a[29:31, x0 + 1:x0 + 58] = False
        a[29:31, x0] = False
        a[29:31, x0 + 58] = False
        a[28, x0 - 1:x0 + 1] = True
        a[31, x0 - 1:x0 + 1] = True
        a[28, x0 + 58:x0 + 60] = True
        a[31, x0 + 58:x0 + 60] = True


screen("clock_flip", "Lịch lật", _flip_bg, 1.0, 1000, [
    digits("hh", "bold40", 32, 10, "m"),
    digits("mm", "bold40", 95, 10, "m"),
], overlay=_flip_over, note="Two flip cards; the split line is drawn over the digits. On a minute change the "
                             "top half can fold down (rows squashed toward the split over ~300 ms).")


# =================================================================== 7. tear-off calendar (lịch bloc)
def _bloc_bg(t):
    a = new()
    page = rrect(1, 5, 126, 63, 3)
    a[edge(page)] = True
    band = rect(2, 6, 125, 15)
    a[band] = True
    # binder rings
    for x in (20, 107):
        r = rrect(x - 2, 0, x + 2, 9, 2)
        a[outer(r)] = False
        a[r] = True
        a[2:7, x] = False
    # the corner of the page lifts in a breeze now and then
    k = math.sin(max(0.0, (t % 6.0) - 4.6) / 1.4 * math.pi)
    if k > 0:
        s = int(2 + k * 6)
        tri = poly([(126 - s, 63), (126, 63 - s), (126, 63)])
        a[tri] = False
        flap = poly([(126 - s, 63), (126 - s, 63 - s), (126, 63 - s)])
        a[flap] = False
        a[edge(flap, False)] = True
        a[flap & dith(4)] = True
    a[rect(71, 19, 71, 59) & px.hlines(2)] = True
    a[28, 78:122] = (np.arange(78, 122) % 2 == 0)
    return a


screen("date_page", "Lịch bloc", _bloc_bg, 6.0, 100, [
    textf("month_line", "tiny5", (3, 8, 124, 14), "m", color=False, upper=True),
    digits("day", "bold40", 37, 19, "m"),
    textf("weekday", "tiny5", (73, 19, 125, 27), "m", upper=True),
    textf("lunar_label", "tiny5", (73, 32, 125, 40), "m", upper=True),
    digits("lunar_day", "bold16", 99, 43, "m"),
], note="The Vietnamese wall calendar page: month band, the day huge, weekday, the lunar date (âm lịch).")
SAMPLE["lunar_label"] = "Bính Ngọ"
SAMPLE["lunar_day"] = "12/10"
digits2.sets()["bold16"].g["/"] = np.array([[((15 - r) // 3) <= c <= ((15 - r) // 3) + 1 for c in range(7)] for r in range(16)], bool)


# =================================================================== 8. month calendar
def _month_bg(t):
    a = new()
    for i, wd in enumerate(("T2", "T3", "T4", "T5", "T6", "T7", "CN")):
        cx = 1 + i * 18 + 9
        if wd == "CN":  # Sunday's column head is inverted
            a[rrect(cx - 7, 10, cx + 7, 16, 2)] = True
            tiny(a, cx + 1, 11, wd, "m", False)
        else:
            tiny(a, cx + 1, 11, wd, "m")
    a[18, 1:127] = (np.arange(1, 127) % 2 == 0)
    return a


screen("month", "Lịch tháng", _month_bg, 1.0, 1000, [
    textf("month_line", "tiny5", (1, 0, 84, 8), "l", upper=True),
    textf("lunar_short", "tiny5", (84, 0, 127, 8), "r", upper=True),
    dict(kind="month", field="month", box=[1, 20, 126, 63], cell_w=18, cell_h=7, first_row_y=21, font="tiny5"),
], note="Monday first, up to 6 rows of 7 px; today in a white box, the CN column head inverted. The firmware "
         "lays the days out from month_first_wd (0 = Monday) and month_days.")


# =================================================================== illustrated screens
import looks2  # noqa: E402  (the puppy, the eyes, z's, sparkles)
from looks2 import Pup, sparkle, zzz, b_eye, kennel, bowl, shadow, pup_run  # noqa: E402


def sun_art(a, cx, cy, t, r=9):
    """A sun: a solid disc, a black gap, 12 tapered rays (long/short) turning slowly, a dithered halo breathing."""
    rot = t * 0.35
    for i in range(12):
        ang = i / 12 * 2 * math.pi + rot
        l0, l1 = r + 3, r + (9 if i % 2 == 0 else 6)
        p0 = (cx + math.cos(ang) * l0, cy + math.sin(ang) * l0)
        p1 = (cx + math.cos(ang) * l1, cy + math.sin(ang) * l1)
        a[taper(*p0, 1.3, *p1, 0.6) if i % 2 == 0 else path1([p0, p1])] = True
    d = circ(cx, cy, r)
    a[dilate(d)] = False
    a[d] = True
    a[d & rim(d, 2, 2) & ~dith(12)] = False  # a little shade on the lower right


def _sun_bg(t):
    a = new()
    sun_art(a, 31, 30, t)
    a[rect(66, 31, 125, 31) & px.vlines(2)] = True
    return a


screen("weather_sun", "Trời nắng", _sun_bg, 6.0, 120, [
    digits("temp", "bold24", 96, 4, "m"),
    textf("weather", "ub11", (64, 34, 127, 47), "m"),
    textf("city_hilo", "tiny5", (64, 53, 127, 61), "m", upper=True),
], group="weather", note="Rays turn slowly. Other skies: weather_cloudsun, weather_cloud, weather_rain, night.")
SAMPLE["city_hilo"] = "Hà Nội 31°/22°"


def cloud(cx, cy, w=1.0):
    m = ell(cx - 9 * w, cy + 2, 8 * w, 6) | ell(cx + 1 * w, cy - 3, 10 * w, 9) | ell(cx + 11 * w, cy + 2, 8 * w, 6) | \
        rrect(int(cx - 16 * w), int(cy + 1), int(cx + 18 * w), int(cy + 8), 4)
    return m


def draw_cloud(a, m, level=4):
    a[m] = False
    a[m & (px.YC > np.nonzero(m.any(1))[0].mean() + 1) & dith(level)] = True
    a[edge(m, False)] = True


def _rain_bg(t):
    a = new()
    # far cloud drifts slower, behind
    x1 = 44 + math.sin(t * 2 * math.pi / 6) * 4
    x2 = 22 + math.sin(t * 2 * math.pi / 6 + 2) * 3
    c2 = cloud(x2, 12, 0.8)
    draw_cloud(a, c2, 2)
    c1 = cloud(x1, 18, 1.0)
    a[outer(c1)] = False
    draw_cloud(a, c1, 6)
    # rain: streaks of 3 px falling at a slant, from under the clouds
    rng = np.random.RandomState(7)
    cols = rng.randint(4, 62, 26)
    ph = rng.rand(26)
    for c, p in zip(cols, ph):
        y = 22 + ((t * 1.6 + p) % 1.0) * 36
        x = c - (y - 22) * 0.25
        if y < 56:
            a[path1([(x, y), (x - 0.75, y + 3)]) & ~c1] = True
    # splashes on the ground
    a[58, 2:62] = (np.arange(2, 62) % 2 == 0)
    for i, x in enumerate((10, 24, 37, 51)):
        k = (t * 1.6 + i * 0.3) % 1.0
        if k < 0.25:
            a[56, x - 1] = a[56, x + 1] = True
            a[55, x - 2] = a[55, x + 2] = k > 0.12
    a[rect(66, 31, 125, 31) & px.vlines(2)] = True
    return a


screen("weather_rain", "Trời mưa", _rain_bg, 6.0, 60, [
    digits("temp_rain", "bold24", 96, 4, "m"),
    textf("rain_s", "ub11", (64, 34, 127, 47), "m"),
    textf("city_hum", "tiny5", (64, 53, 127, 61), "m", upper=True),
], group="weather", note="Two clouds drift, rain falls at a slant, drops splash on the ground.")
SAMPLE["temp_rain"] = "24°"
SAMPLE["rain_s"] = "Mưa rào"
SAMPLE["city_hum"] = "Hà Nội ẩm 90%"


# The other skies of the "weather" screen (2026-11-25: four weather screens did not follow the sky; now one does):
# the firmware picks sun, sun behind a cloud, clouds, rain or night from the real weather (src/bot/ui.cpp resolve).
def _cloudsun_bg(t):
    a = new()
    sun_art(a, 24, 21, t, r=8)
    c = cloud(39 + math.sin(t * 2 * math.pi / 6) * 3, 36, 1.0)
    a[outer(c)] = False
    draw_cloud(a, c, 6)
    a[rect(66, 31, 125, 31) & px.vlines(2)] = True
    return a


screen("weather_cloudsun", "Ít mây", _cloudsun_bg, 6.0, 120, [
    digits("temp", "bold24", 96, 4, "m"),
    textf("sky_part", "ub11", (64, 34, 127, 47), "m"),
    textf("city_hilo", "tiny5", (64, 53, 127, 61), "m", upper=True),
], group="weather", note="The sun's rays turn behind a cloud that drifts a little.")
SAMPLE["sky_part"] = "Ít mây"


def _cloud_bg(t):
    a = new()
    w = 2 * math.pi / 8
    c3 = cloud(46 + math.sin(t * w + 1) * 3, 11, 0.6)
    draw_cloud(a, c3, 1)
    c2 = cloud(17 + math.sin(t * w + 3) * 3, 17, 0.8)
    a[outer(c2)] = False
    draw_cloud(a, c2, 2)
    c1 = cloud(35 + math.sin(t * w) * 4, 36, 1.0)
    a[outer(c1)] = False
    draw_cloud(a, c1, 6)
    a[rect(66, 31, 125, 31) & px.vlines(2)] = True
    return a


screen("weather_cloud", "Nhiều mây", _cloud_bg, 8.0, 120, [
    digits("temp", "bold24", 96, 4, "m"),
    textf("sky_cloud", "ub11", (64, 34, 127, 47), "m"),
    textf("city_hilo", "tiny5", (64, 53, 127, 61), "m", upper=True),
], group="weather", note="Three clouds drift at their own pace (cloudy, fog).")
SAMPLE["sky_cloud"] = "Nhiều mây"


STARS = [(8, 6, 0.0), (48, 6, 0.6), (52, 24, 0.15), (40, 34, 0.9), (6, 34, 0.45), (120, 44, 0.7)]


def moon(a, cx, cy, r=10):
    m = circ(cx, cy, r) & ~circ(cx + r * 0.55, cy - r * 0.35, r * 0.85)
    a[m] = True
    for dx, dy, rr in ((-5, 2, 1.6), (-2, 6, 1.2), (-6, -3, 1.0)):
        a[circ(cx + dx, cy + dy, rr) & m & dith(8)] = False
    return m


def hills(a, base=63):
    h1 = ell(30, base + 18, 52, 26)
    h2 = ell(104, base + 20, 46, 28)
    a[h2] = False
    a[h2 & dith(2)] = True
    a[edge(h2, True)] = True
    a[outer(h1) & h2] = False
    a[h1] = False
    a[h1 & dith(1)] = True
    a[edge(h1, True)] = True
    return h1 | h2


def _night_bg(t):
    a = new()
    for x, y, p in STARS:
        a[y, x] = True
        sparkle(a, x, y, (t / 3.0 + p) % 1.0)
    moon(a, 26, 18, 11)
    hl = hills(a)
    # a little house with a lit window on the far hill
    hx, hy = 96, 41
    house = rect(hx, hy, hx + 9, hy + 6) | poly([(hx - 1, hy), (hx + 5, hy - 5), (hx + 11, hy)])
    a[dilate(house)] = False
    a[edge(house, False)] = True
    a[hy + 2:hy + 5, hx + 3:hx + 6] = int(t * 2) % 7 != 0
    # a shooting star now and then
    k = (t % 8.0) / 0.8
    if k < 1:
        x0, y0 = 70 + k * 40, 4 + k * 12
        a[path1([(x0, y0), (x0 - 8, y0 - 2.4)]) & ~hl] = True
    return a


screen("night", "Đêm trăng", _night_bg, 8.0, 100, [
    digits("time_n", "thin20", 124, 4, "r", colon="blink"),
    textf("temp_night", "tiny5", (66, 28, 125, 36), "r", upper=True),
], group="weather", note="Moon, twinkling stars, a shooting star every 8 s, a lit window on the hill.")
SAMPLE["time_n"] = "21:30"
SAMPLE["temp_night"] = "22° trời quang"


# =================================================================== dog vignettes
GRASS = sprite([
    " #   ",
    " # # ",
    "## # ",
])
FLOWER = sprite([
    " # ",
    "#.#",
    " # ",
    " # ",
])


def meadow(a, y=58, t=0.0, flowers=True):
    a[y, :] = True
    a[y + 1:, :] = False
    a[y + 2:H, :] |= dith(2)[y + 2:H, :]
    for i, x in enumerate(range(3, 128, 13)):
        blit(a, GRASS, x + (i * 5) % 7, y - 3)
    if flowers:
        for x in (18, 47, 99, 120):
            blit(a, FLOWER, x, y - 4)


def _kennel_bg(t):
    a = new()
    for x, y in ((8, 4), (30, 3), (88, 4), (124, 30), (58, 6)):
        a[y, x] = True
    for i, (x, y) in enumerate(((16, 12), (78, 6), (122, 38))):
        sparkle(a, x, y, (t / 3.0 + i * 0.37) % 1.0)
    moon(a, 106, 13, 8)
    meadow(a, 59, t, flowers=False)
    # a big kennel: plank walls, a hatched roof, an arched door the pup fills
    kx, ky, w = 14, 59, 66
    walls = rect(kx + 3, ky - 30, kx + w - 3, ky - 1)
    roof = poly([(kx - 4, ky - 28), (kx + w // 2, ky - 47), (kx + w + 4, ky - 28)])
    a[walls] = True
    for yy in range(ky - 26, ky - 1, 4):
        a[yy, kx + 4:kx + w - 3] = False
    a[roof] = False
    a[roof & px.hatch(3, 1)] = True
    a[edge(roof)] = True
    a[outer(roof) & walls] = False
    dx0, dx1 = kx + w // 2 - 18, kx + w // 2 + 18
    door = rrect(dx0, ky - 27, dx1, ky - 1, 14) | rect(dx0, ky - 12, dx1, ky - 1)
    a[door] = False
    a[outer(door) & walls] = True
    # the pup asleep in the doorway, chin on its paws, clipped to the door
    tmp = new()
    p = Pup(kx + w // 2 - 6, ky - 1)
    sil = p.draw(tmp, pose="lie", eyes="shut", mouth="flat", collar=False)
    inside = door & ~outer(door)
    a[inside] = tmp[inside]
    br = 1 if math.sin(t * 2 * math.pi / 2.8) > 0 else 0
    zzz(a, t, 70, 24 - br)
    bowl(a, 106, 59)
    return a


screen("kennel_night", "Cún ngủ", _kennel_bg, 5.6, 80, [], group="dog", note="The puppy asleep in its kennel door under the moon; z's float, stars twinkle.")


BUTTERFLY = [sprite([
    " #     # ",
    "  #   #  ",
    "###   ###",
    "#oo#.#oo#",
    "#ooo#ooo#",
    " ###.### ",
    " #o#.#o# ",
    " ##   ## ",
]), sprite([
    " #     # ",
    "  #   #  ",
    "         ",
    "  ##.##  ",
    "  #o#o#  ",
    "  ##.##  ",
    "  ## ##  ",
    "         ",
])]


def _butterfly_bg(t):
    a = new()
    meadow(a, 58, t)
    # the butterfly flies a lazy figure eight; the pup runs after it, a little late
    u = t / 6.0 * 2 * math.pi
    bx = 64 + math.sin(u) * 46
    by = 18 + math.sin(2 * u) * 9 - abs(math.sin(u * 6)) * 3
    px_ = 64 + math.sin(u - 0.7) * 34
    vel = math.cos(u - 0.7)
    if abs(vel) > 0.28:  # running, facing the way it goes
        shadow(a, int(px_), 59, 13)
        pup_run(a, int(px_), 58, (t * 2.6) % 1.0, 1 if vel > 0 else -1)
    else:  # turning round: it stops, sits and looks up at the butterfly
        shadow(a, int(px_), 59, 13)
        lk = (1 if bx > px_ + 4 else -1 if bx < px_ - 4 else 0, -1)
        Pup(int(px_), 58).draw(a, eyes="big", mouth="tongue", ears=0.3, tail=math.sin(t * 18), look=lk)
    blit(a, BUTTERFLY[int(t * 8) % 2], int(bx) - 4, int(by) - 4)
    return a


screen("butterfly", "Đuổi bướm", _butterfly_bg, 6.0, 60, [], group="dog",
       note="The puppy runs after a butterfly (side-view gallop), stops to look up when it turns round.")


def _news_bg(t):
    a = new()
    # the puppy (look C) over the paper: its round brown ear lobes hang beside it, eyes follow the lines
    lk = (math.sin(t * 2 * math.pi / 2.5) * 0.9, 0.8)
    Pup(64, 50).draw(a, eyes="open", look=lk, collar=False, mouth="w", blink=blink_at(t % 5.0, (3.9,), 0.18) > 0.3)
    paper = rect(30, 33, 97, 50)
    a[dilate(paper)] = False
    a[paper] = False
    a[edge(paper, False)] = True
    a[rect(64, 34, 64, 49) & px.hlines(2)] = True
    tiny(a, 47, 36, "TIN", "m")
    tiny(a, 81, 36, "SÁNG", "m")
    a[42, 32:96] = True
    for x0 in (33, 67):
        for i, y in enumerate(range(44, 49, 2)):
            ln = 28 if (i + x0) % 3 else 20
            a[y, x0:x0 + ln] = (np.arange(x0, x0 + ln) % 3 != 2)
    box = rect(84, 44, 95, 48)
    a[box] = False
    a[edge(box, False)] = True
    # paws on the top corners of the paper
    for x in (31, 89):
        pw = rrect(x, 30, x + 7, 35, 2)
        a[outer(pw)] = False
        a[pw] = True
        a[33:36, x + 2] = False
        a[33:36, x + 5] = False
    a[rect(0, 52, 127, 63)] = True
    return a


screen("newspaper", "Đọc báo", _news_bg, 5.0, 50, [
    textf("headline", "ub11", (2, 52, 125, 63), "l", color=False, overflow="marquee"),
], group="dog", note="The dog reads the morning paper (its eyes follow the lines); the headline runs along the "
                     "white band at the bottom (live, marquee, black on white).")


def _coffee_bg(t):
    """A Vietnamese phin dripping into a glass of cà phê sữa, drawn as steel line art."""
    a = new()
    gx, gy = 25, 62  # the glass: middle, bottom
    glass = poly([(gx - 12, gy - 22), (gx + 12, gy - 22), (gx + 10, gy), (gx - 10, gy)])
    a[edge(glass, False)] = True
    inner = glass & ~edge(glass, False)
    a[inner & (px.YC > gy - 3)] = True  # a thick glass bottom
    level = gy - 9 - (t % 8.0) / 8.0 * 8
    a[inner & (px.YC > gy - 7) & (px.YC <= gy - 3) & dith(12)] = True  # condensed milk
    a[inner & (px.YC > level) & (px.YC <= gy - 7) & dith(6)] = True    # coffee
    a[inner & (px.YC > level - 1) & (px.YC <= level)] = True           # crema
    a[rect(gx - 8, gy - 20, gx - 8, gy - 12) & (px.YC < level - 1)] = True  # the glass shine
    # the phin: plate, cup, lid, all 1-px steel outlines with vertical highlights
    plate = rrect(gx - 16, gy - 25, gx + 16, gy - 23, 1)
    a[plate] = True
    a[gy - 24, gx - 14:gx + 15] = False
    cup = poly([(gx - 10, gy - 39), (gx + 10, gy - 39), (gx + 8, gy - 26), (gx - 8, gy - 26)])
    a[edge(cup, False)] = True
    for hx in (gx - 6, gx - 4):  # highlights
        a[rect(hx, gy - 37, hx, gy - 29)] = True
    a[rect(gx + 6, gy - 37, gx + 6, gy - 28) & px.hlines(2)] = True  # the shaded side
    a[rect(gx - 12, gy - 40, gx + 12, gy - 39)] = True  # the rim
    lid = ell(gx, gy - 41, 9, 3.5) & (px.YC < gy - 40.5)
    a[edge(lid, False)] = True
    a[rrect(gx - 2, gy - 47, gx + 2, gy - 44, 1)] = True  # the knob
    # a drip every 0.8 s, falling from the phin into the glass
    k = (t % 0.8) / 0.8
    dy = gy - 22 + k * (level - (gy - 22))
    if k < 0.9:
        a[int(dy):int(dy) + 2, gx] = True
    # steam curling up over the lid
    for i, dx in enumerate((-5, 1, 7)):
        pts = [(gx + dx + math.sin(j * 0.7 + t * 3 + i * 2) * 1.6, gy - 49 - j * 1.3) for j in range(10)]
        ph = (t * 0.6 + i * 0.33) % 1.0
        n0 = int(ph * 6)
        a[path1(pts[n0:n0 + 4])] = True
    a[rect(53, 2, 53, 61) & px.hlines(2)] = True
    return a


screen("coffee", "Cà phê sáng", _coffee_bg, 8.0, 80, [
    digits("time_m", "bold24", 91, 6, "m", colon="blink"),
    textf("date_mid", "tiny5", (56, 37, 127, 45), "m", upper=True),
    textf("greet", "tiny5", (56, 51, 127, 59), "m", upper=True),
], group="dog", note="A phin dripping into a glass of cà phê sữa: the coffee rises, steam curls.")
SAMPLE["time_m"] = "06:45"
SAMPLE["greet"] = "Chào buổi sáng"


def _window_bg(t):
    """We are outside in the rain; the puppy looks out at us through the window, chin on the sill."""
    a = new()
    # the room inside is dark; the pup sits behind the glass
    p = Pup(64, 62)
    lk = (math.sin(t * 2 * math.pi / 6) * 0.8, -0.3)
    p.draw(a, eyes="big", mouth="w", look=lk, tilt=math.sin(t * 2 * math.pi / 6) * 4, collar=True,
           blink=blink_at(t % 6.0, (4.2,), 0.18) > 0.3)
    glass = rect(13, 13, 114, 51)
    a[~glass] = False
    # rain on the glass, in front of everything inside: streaks with a dark edge so they read over the fur
    rng = np.random.RandomState(3)
    for c, p_ in zip(rng.randint(14, 114, 24), rng.rand(24)):
        y = 13 + ((t * 1.2 + p_) % 1.0) * 40
        x = c - (y - 13) * 0.18
        m = path1([(x, y), (x - 0.8, y + 4)]) & glass
        a[dilate(m) & glass & ~m] = False
        a[m] = True
    for x, p_ in ((24, 0.0), (102, 0.4), (40, 0.7), (88, 0.15)):  # drops sliding down, leaving a trail
        y = 16 + ((t * 0.22 + p_) % 1.0) * 32
        d = circ(x, y, 1.7)
        a[outer(d) & glass] = False
        a[d] = True
        a[int(y) - 7:int(y) - 2, int(x)] = (np.arange(5) % 2 == 0)
    # the window: frame, a transom over the pup's head, the sill
    a[rect(10, 0, 117, 55) & ~rect(13, 13, 114, 51)] = True
    a[rect(13, 3, 114, 10)] = False
    a[rect(13, 3, 114, 10) & dith(2)] = True
    a[rect(63, 3, 64, 10)] = True
    a[rect(4, 52, 123, 55)] = True
    a[56, 6:122] = True
    a[rect(0, 58, 127, 63) & dith(2)] = True
    # rain outside the window too, on the wall
    for c, p_ in zip(rng.randint(0, 128, 14), rng.rand(14)):
        if 9 < c < 118:
            continue
        y = ((t * 1.5 + p_) % 1.0) * 56
        a[path1([(c, y), (c - 0.8, y + 4)])] = True
    return a


screen("window_rain", "Ngắm mưa", _window_bg, 6.0, 60, [], group="dog",
       note="Rain on the window; the puppy looks out through it, chin on the sill, eyes following the drops.")


def fish(a, cx, cy, t, facing=1, size=1.0, kind="clown"):
    """A fish drawn at 1x: an oval body with a forked tail that beats, a top fin, an eye with a shine.
    clown: white with two dithered bands; tang: dithered all over with a light belly."""
    k = size
    beat = math.sin(t * 9) * 2.2 * k
    body = ell(cx, cy, 10 * k, 6 * k)
    tx = cx - facing * 9 * k
    tail = poly([(tx, cy), (tx - facing * 7 * k, cy - 6 * k + beat), (tx - facing * 5 * k, cy + beat * 0.3),
                 (tx - facing * 7 * k, cy + 6 * k + beat)])
    fin = poly([(cx - facing * 4 * k, cy - 5 * k), (cx + facing * 2 * k, cy - 5 * k), (cx - facing * 5 * k, cy - 10 * k)])
    sil = body | tail | fin
    a[dilate(sil)] = False
    a[(tail | fin) & dith(6)] = True
    a[edge(tail | fin, False)] = True
    a[outer(body) & (tail | fin)] = False
    if kind == "clown":
        a[body] = True
        for off in (-2.5, 3.5):
            band = body & (abs(px.XC - (cx + facing * off * k)) < 1.8 * k)
            a[band] = False
            a[band & dith(8)] = True
    else:
        a[body] = False
        a[body & dith(6)] = True
        a[body & (px.YC > cy + 2 * k) & dith(12)] = True
        a[edge(body, False)] = True
    ex, ey = int(cx + facing * 6 * k), int(cy - 2 * k)
    eye = rect(ex - 1, ey - 1, ex + 1, ey + 1) if -2 < ex < W + 1 else np.zeros_like(a)
    a[eye] = False
    a[rect(ex - 1 if facing < 0 else ex, ey - 1, ex - 1 if facing < 0 else ex, ey - 1) if 0 <= ex < W else eye & False] = True
    my, mx = int(cy + 2 * k), int(cx + facing * 8 * k)
    if 0 <= my < H and 0 <= mx < W:
        a[my, mx] = False  # the mouth
    return sil


def plant(a, x0, hgt, t, phase=0.0, leaf_every=3):
    pts = [(x0 + math.sin(j * 0.4 + t * 2.0 + phase) * (j / 7), 62 - j * 2) for j in range(hgt // 2)]
    stem = thick(pts, 1.8)
    a[stem] = True
    for j in range(2, hgt // 2, leaf_every):
        x, y = pts[j]
        sgn = 1 if j % 2 else -1
        leaf = taper(x, y, 1.0, x + sgn * 5, y - 4 + math.sin(t * 2 + j) * 0.8, 1.8)
        a[outer(leaf) & ~stem] = False
        a[leaf & ~dith(4)] = True
        a[edge(leaf, False)] = True


def _aqua_bg(t):
    a = new()
    # sand, pebbles, a rock arch with a bubbler
    sand = ell(64, 76, 92, 16)
    a[sand & dith(4)] = True
    a[edge(sand, True)] = True
    rock = ell(104, 60, 15, 11) & ~ell(104, 64, 5, 6)
    a[rock] = False
    a[rock & dith(6)] = True
    a[edge(rock, True)] = True
    for x, y in ((44, 59), (50, 60), (62, 58), (70, 60), (30, 60)):
        pb = ell(x, y, 2.6, 1.7)
        a[outer(pb)] = False
        a[pb] = True
    # plants: a tall kelp, a bushy clump, one in the middle, a short one by the rock
    plant(a, 10, 46, t, 0.0)
    plant(a, 18, 30, t, 1.3, 2)
    plant(a, 84, 34, t, 2.1)
    plant(a, 122, 26, t, 0.7, 2)
    # two fish: a big clownfish crossing slowly, a small tang the other way
    u = (t / 10.0) % 1.0
    fx, fy = -24 + u * 176, 24 + math.sin(t * 1.3) * 4
    fish(a, fx, fy, t, 1, 1.0, "clown")
    v = (t / 10.0 + 0.45) % 1.0
    gx, gy = 150 - v * 176, 40 + math.sin(t * 1.9) * 3
    fish(a, gx, gy, t + 0.4, -1, 0.7, "tang")
    # bubbles: a stream from the rock, now and then one from the clownfish
    for i in range(6):
        ph = (t * 0.45 + i / 6) % 1.0
        y = 52 - ph * 50
        x = 102 + math.sin(ph * 10 + i) * 2.2
        r = 0.9 + ph * 2.0
        m = circ(x, y, r)
        a[outer(m)] = False
        a[edge(m, False) if r > 1.6 else m] = True
        if r > 2.2:
            a[int(y - r * 0.4), int(x - r * 0.4)] = True
    ph = (t / 2.5) % 1.0
    if ph < 0.6:
        a[circ(fx + 11, fy - 2 - ph * 14, 1.2)] = True
    a[1, :] = (np.arange(128) + int(t * 8)) % 6 < 3  # the waterline
    return a


screen("aquarium", "Bể cá", _aqua_bg, 10.0, 80, [], group="dog",
       note="A small aquarium: two fish, swaying weed, bubbles from the rock (a nod to the Hồ cá game).")


def ingot(a, x, y):
    """A gold bar seen from the front-top: a light top face, a dithered front, a stamped mark."""
    top = poly([(x + 3, y), (x + 17, y), (x + 20, y + 4), (x, y + 4)])
    front = rect(x, y + 5, x + 20, y + 10)
    a[outer(top | front)] = False
    a[top] = True
    a[front & dith(8)] = True
    a[edge(front, False)] = True
    a[y + 1:y + 3, x + 7:x + 14] = False
    a[y + 2, x + 8:x + 13] = True


def _gold_bg(t):
    a = new()
    ingot(a, 1, 33)
    ingot(a, 19, 33)
    ingot(a, 10, 22)
    sparkle(a, 14, 22, (t / 2.0) % 1.0)
    sparkle(a, 34, 34, (t / 2.0 + 0.5) % 1.0)
    tiny(a, 21, 12, "SJC", "m")
    a[rect(43, 2, 43, 61) & px.hlines(2)] = True
    a[rect(47, 46, 126, 46) & px.vlines(2)] = True
    tiny(a, 47, 3, "BÁN RA", "l")
    dollar = sprite([
        "  #  ",
        " ####",
        "# #  ",
        " ### ",
        "  # #",
        "#### ",
        "  #  ",
    ])
    blit(a, dollar, 48, 51)
    return a


screen("gold", "Giá vàng", _gold_bg, 2.0, 100, [
    digits("gold_sell", "bold24", 126, 11, "r"),
    textf("gold_buy_line", "tiny5", (47, 37, 126, 45), "r", upper=True),
    textf("usd_line", "ub11", (56, 49, 126, 62), "r"),
    textf("gold_unit_s", "tiny5", (0, 50, 42, 58), "m", upper=True),
], group="info", note="SJC gold: the sell price big (million đồng a lượng), the buy price, the USD rate.")
SAMPLE["usd_line"] = "26.350 đ"
SAMPLE["gold_unit_s"] = "tr/lượng"
SAMPLE["gold_buy_line"] = "Mua 148,5"
_c = np.zeros((24, 5), bool)
_c[19:22, 0:3] = True
_c[22, 1:3] = True
_c[23, 0:2] = True
digits2.sets()["bold24"].g[","] = _c


ORN = sprite([
    "####   ",
    "#      ",
    "# ##   ",
    "  #    ",
    "       ",
])


def _proverb_bg(t):
    a = new()
    box = rrect(1, 4, 126, 62, 3)
    a[edge(box)] = True
    inner = rrect(4, 7, 123, 59, 2)
    a[edge(inner) & px.hlines(2) & px.vlines(2) | edge(inner) & False] = True
    for (x, y, fx, fy) in ((6, 9, False, False), (115, 9, True, False), (6, 52, False, True), (115, 52, True, True)):
        s = ORN[:, ::-1] if fx else ORN
        s = s[::-1, :] if fy else s
        blit(a, s, x, y)
    # the label tab
    tab = rrect(40, 0, 87, 9, 3)
    a[tab] = True
    tiny(a, 64, 3, "CA DAO", "m", False)
    return a


screen("proverb", "Ca dao", _proverb_bg, 1.0, 1000, [
    textf("quote", "ub11", (10, 12, 117, 56), "m", wrap=True),
], group="info", note="A proverb or ca dao of the day in a framed card; long ones scroll up slowly.")


def blossom(a, x, y, open_=1.0):
    """A five-petal hoa mai: round petals with a black seam between them, a dark heart with a light dot."""
    union = np.zeros_like(a)
    for i in range(5):
        ang = i / 5 * 2 * math.pi - math.pi / 2
        p = circ(x + math.cos(ang) * 3.1 * open_, y + math.sin(ang) * 3.1 * open_, 2.5 * open_)
        a[outer(p) & union] = False
        a[p & ~union] = True
        union |= p
    a[outer(union)] = False
    a[circ(x, y, 1.6)] = False
    a[int(y), int(x)] = True


def _tet_bg(t):
    a = new()
    br = [(0, 4), (14, 12), (26, 24), (33, 40), (36, 60)]
    a[thick(br, 3.0)] = True
    a[thick([(14, 12), (26, 7), (40, 6)], 1.8)] = True
    a[thick([(26, 24), (40, 24), (48, 19)], 1.8)] = True
    a[thick([(33, 40), (44, 44)], 1.6)] = True
    for (x, y) in ((20, 19), (30, 33), (8, 12)):  # buds
        a[circ(x, y, 1.6)] = True
    for i, (x, y) in enumerate(((27, 6), (41, 6), (40, 23), (48, 16), (14, 13), (44, 45), (31, 47), (22, 27))):
        blossom(a, x, y, 0.9 + 0.1 * math.sin(t * 2 + i))
    for i in range(4):  # petals falling
        ph = (t / 4.0 + i / 4) % 1.0
        x = 14 + i * 10 + math.sin(ph * 8 + i) * 4
        y = 8 + ph * 56
        a[int(y), int(x)] = True
        if ph % 0.2 < 0.1:
            a[int(y), int(x) + 1] = True
    a[rect(56, 2, 56, 61) & px.hlines(2)] = True
    tiny(a, 92, 55, "NGÀY", "m")
    return a


screen("tet", "Đếm ngược Tết", _tet_bg, 4.0, 100, [
    textf("tet_name", "tiny5", (58, 3, 127, 11), "m", upper=True),
    digits("tet_days", "bold40", 92, 13, "m"),
], group="info", note="Days to Tết under a branch of hoa mai, petals falling.")


def _eyes_bg(t):
    a = new()
    # the eyes wander slowly round the screen (kind to the panel), look about, blink
    u = t / 12.0 * 2 * math.pi
    cx = 64 + math.sin(u) * 26
    cy = 32 + math.sin(2 * u) * 8
    lk = (math.cos(u) * 0.9, math.cos(2 * u) * 0.5)
    bl = blink_at(t % 12.0, (2.0, 6.5, 6.75, 10.0), 0.2)
    for sgn in (-1, 1):
        b_eye(a, cx + sgn * 16, cy, 22, 27, look=lk, blink=bl, side=sgn)
    return a


screen("eyes", "Chỉ có mắt", _eyes_bg, 12.0, 60, [], group="dog",
       note="Just the eyes, drifting round the whole screen so no pixel stays lit for long.")
