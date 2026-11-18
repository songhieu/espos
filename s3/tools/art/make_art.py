"""App icons, weather icons and UI glyphs for EspOS, drawn as SVG/HTML (the same art as
mockups/s3-os.html), rendered by headless Chrome at 4x and scaled down, written as LVGL images:
  src/art.c / src/art.h
App icons and weather icons are RGB565A8 (colour + alpha); glyphs are A8 (alpha only, tinted by
the style's image_recolor). Run from s3/: python3 tools/art/make_art.py

The pixels are stored as ONE zlib stream (every image's data at a 4-byte aligned offset of the inflated blob) and
inflated into PSRAM at boot by src/artz.cpp (the ROM's tinfl on the board, zlib in the simulator), which also builds
the descriptors in PSRAM (no internal RAM); art.h keeps the names (#define ic_news_52 (*art_dsc(17))), so callers do
not change. ~667 KB of pixels take ~95 KB of flash.
  make_art.py --repack   re-emits the current src/art.c (either form) without rendering: the same pixels, byte for byte
"""
import math
import re
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
SCALE = 4
CELL = 80  # css px per item on the render sheet


def svg(body, s=30, vb=32):
    return f'<svg width="{s}" height="{s}" viewBox="0 0 {vb} {vb}">{body}</svg>'


PETALS = "".join(
    f'<ellipse rx="4" ry="9" transform="rotate({i * 45}) translate(0 -6)" fill="{c}" opacity=".85"/>'
    for i, c in enumerate(["#ff9500", "#ffcc00", "#34c759", "#00c7be", "#007aff", "#5856d6", "#af52de", "#ff2d55"]))
GEAR = "".join(f'<rect x="14.6" y="4" width="2.8" height="5" rx="1" fill="#fff" transform="rotate({i * 45} 16 16)"/>'
               for i in range(8))
CALC = "".join(f'<rect x="{x}" y="{y}" width="5" height="4" rx="1" fill="#fff"/>'
               for x, y in [(7, 15), (13.5, 15), (20, 15), (7, 20.5), (13.5, 20.5)])

# id: (background css, inner html)
APPS = {
    "news": ("linear-gradient(160deg,#c8213f,#8f1239)", svg(
        '<rect x="6" y="7" width="20" height="18" rx="2" fill="#fff"/><rect x="9" y="10" width="14" height="3" fill="#c8213f"/>'
        '<rect x="9" y="15" width="6" height="7" fill="#f3c6cf"/><rect x="17" y="15" width="6" height="1.6" fill="#c9ccd3"/>'
        '<rect x="17" y="18" width="6" height="1.6" fill="#c9ccd3"/><rect x="17" y="21" width="4" height="1.6" fill="#c9ccd3"/>')),
    "weather": ("linear-gradient(160deg,#2f7ff0,#5fb6ff)", svg(
        '<circle cx="12" cy="12" r="5" fill="#ffd65a"/><path d="M9 25h14a5 5 0 0 0 0-10 6.5 6.5 0 0 0-12.4 2A4 4 0 0 0 9 25z" fill="#fff"/>')),
    "cal": ("#fff", ""),  # weekday and day are drawn live
    "photos": ("#fff", svg(f'<g transform="translate(16 16)">{PETALS}</g>')),
    "markets": ("#11161f", svg(
        '<polyline points="5,22 11,16 15,19 21,10 27,13" fill="none" stroke="#34d399" stroke-width="2.6" stroke-linecap="round" stroke-linejoin="round"/>'
        '<circle cx="21" cy="10" r="2" fill="#34d399"/>')),
    "clock": ("#11161f", svg(
        '<circle cx="16" cy="16" r="11" fill="#fff"/><line x1="16" y1="16" x2="16" y2="9" stroke="#11161f" stroke-width="2.2" stroke-linecap="round"/>'
        '<line x1="16" y1="16" x2="21" y2="18" stroke="#ff9500" stroke-width="2" stroke-linecap="round"/>')),
    "notes": ("linear-gradient(180deg,#ffd84a 0 30%,#fff 30%)", svg(
        '<rect x="7" y="14" width="18" height="1.8" fill="#d9d4c4"/><rect x="7" y="18.5" width="18" height="1.8" fill="#d9d4c4"/>'
        '<rect x="7" y="23" width="12" height="1.8" fill="#d9d4c4"/>')),
    "settings": ("linear-gradient(160deg,#8e8e93,#636366)", svg(
        f'<circle cx="16" cy="16" r="5" fill="none" stroke="#fff" stroke-width="2.6"/>{GEAR}')),
    "store": ("linear-gradient(160deg,#1d8cf8,#1360d6)", svg(
        '<path d="M9 12h14l-1.4 12H10.4z" fill="#fff"/><path d="M12.5 12a3.5 3.5 0 0 1 7 0" fill="none" stroke="#fff" stroke-width="2"/>')),
    "lunar": ("linear-gradient(160deg,#d7261e,#a3121a)",
              '<div style="position:absolute;top:6px;left:0;right:0;text-align:center;font:700 8.5px Ubuntu;color:#ffd979;letter-spacing:.2px">ÂM LỊCH</div>'),
    "focus": ("linear-gradient(160deg,#ff6b5a,#e0383b)", svg(
        '<circle cx="16" cy="17" r="9" fill="none" stroke="#fff" stroke-width="2.6"/><line x1="16" y1="17" x2="16" y2="12" stroke="#fff" stroke-width="2.4" stroke-linecap="round"/>'
        '<rect x="13" y="5" width="6" height="2.6" rx="1.2" fill="#fff"/>')),
    "radio": ("linear-gradient(160deg,#a259ff,#6d28d9)", svg(
        '<circle cx="16" cy="18" r="3" fill="#fff"/><path d="M10 12a8.5 8.5 0 0 1 12 0M7 9a13 13 0 0 1 18 0" fill="none" stroke="#fff" stroke-width="2.2" stroke-linecap="round"/>')),
    "home": ("linear-gradient(160deg,#ffb020,#f97316)", svg('<path d="M6 15 16 7l10 8v10H19v-6h-6v6H6z" fill="#fff"/>')),
    "calc": ("#1f2329", svg(f'<rect x="7" y="6" width="18" height="6" rx="1.5" fill="#9aa6b6"/>{CALC}'
                            '<rect x="20" y="20.5" width="5" height="4" rx="1" fill="#ff9500"/>')),
    "vocab": ("linear-gradient(160deg,#22c55e,#15803d)", '<div style="font:700 20px Ubuntu;color:#fff">Aa</div>'),
    "remind": ("#fff", svg(
        '<circle cx="9" cy="10" r="2.2" fill="#ff9500"/><circle cx="9" cy="16" r="2.2" fill="#007aff"/><circle cx="9" cy="22" r="2.2" fill="#ff3b30"/>'
        '<rect x="13" y="9" width="12" height="2" fill="#c9ccd3"/><rect x="13" y="15" width="12" height="2" fill="#c9ccd3"/><rect x="13" y="21" width="9" height="2" fill="#c9ccd3"/>')),
    "app": ("linear-gradient(160deg,#64748b,#334155)", svg(
        '<rect x="8" y="8" width="7" height="7" rx="2" fill="#fff"/><rect x="17" y="8" width="7" height="7" rx="2" fill="#fff" opacity=".7"/>'
        '<rect x="8" y="17" width="7" height="7" rx="2" fill="#fff" opacity=".7"/><rect x="17" y="17" width="7" height="7" rx="2" fill="#fff"/>')),
}


def sun(cx, cy, r, ray=True):
    rays = "".join(
        f'<line x1="{cx + math.cos(i * math.pi / 4) * r * 1.55:.2f}" y1="{cy + math.sin(i * math.pi / 4) * r * 1.55:.2f}" '
        f'x2="{cx + math.cos(i * math.pi / 4) * r * 2.1:.2f}" y2="{cy + math.sin(i * math.pi / 4) * r * 2.1:.2f}" '
        f'stroke="#ffd65a" stroke-width="{r * 0.33:.2f}" stroke-linecap="round"/>' for i in range(8)) if ray else ""
    return f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="#ffd65a"/>{rays}'


CLOUD = "M10 38h26a9 9 0 0 0 0-18 12 12 0 0 0-23 4 7 7 0 0 0-3 14z"
WEATHER = {
    "sun": sun(24, 24, 9),
    "moon": '<path d="M30 6a15 15 0 1 0 12 22A13 13 0 0 1 30 6z" fill="#f5e7a8"/>',
    "cloudsun": sun(17, 17, 7) + '<path d="M14 42h22a8 8 0 0 0 0-16 11 11 0 0 0-21 3 7 7 0 0 0-1 13z" fill="#eef2f8"/>',
    "cloudmoon": '<path d="M28 6a12 12 0 1 0 12 15A10 10 0 0 1 28 6z" fill="#f5e7a8"/>'
                 '<path d="M12 40h22a8 8 0 0 0 0-16 11 11 0 0 0-21 3 7 7 0 0 0-1 13z" fill="#e9ecf6"/>',
    "cloud": '<path d="M18 30h20a7 7 0 0 0 0-14 9.5 9.5 0 0 0-18 3z" fill="#b8c2d3"/>'
             f'<path d="{CLOUD}" fill="#e8edf6"/>',
    "rain": '<path d="M10 30h26a9 9 0 0 0 0-18 12 12 0 0 0-23 4 7 7 0 0 0-3 14z" fill="#e2e8f0"/>'
            '<g stroke="#7cb8ff" stroke-width="3" stroke-linecap="round"><line x1="16" y1="35" x2="13" y2="42"/>'
            '<line x1="25" y1="35" x2="22" y2="42"/><line x1="34" y1="35" x2="31" y2="42"/></g>',
    "storm": '<path d="M10 30h26a9 9 0 0 0 0-18 12 12 0 0 0-23 4 7 7 0 0 0-3 14z" fill="#aab4c4"/>'
             '<path d="M25 28l-7 10h6l-3 8 9-12h-6l3-6z" fill="#ffd040"/>',
    "fog": '<path d="M10 28h26a9 9 0 0 0 0-18 12 12 0 0 0-23 4 7 7 0 0 0-3 14z" fill="#e2e8f0"/>'
           '<g stroke="#b8c2d3" stroke-width="3.2" stroke-linecap="round"><line x1="8" y1="35" x2="40" y2="35"/>'
           '<line x1="13" y1="42" x2="35" y2="42"/></g>',
    "snow": '<path d="M10 30h26a9 9 0 0 0 0-18 12 12 0 0 0-23 4 7 7 0 0 0-3 14z" fill="#e2e8f0"/>'
            '<g fill="#ffffff"><circle cx="15" cy="38" r="2.6"/><circle cx="24" cy="42" r="2.6"/><circle cx="33" cy="38" r="2.6"/></g>',
}

# Glyphs: white strokes on a 24x24 box (alpha only on the device).
S = 'fill="none" stroke="#fff" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"'
GLYPHS = {
    "wifi3": f'<path d="M12 19.5l-2.6-2.7a3.7 3.7 0 0 1 5.2 0z" fill="#fff"/><path d="M6.6 14a7.6 7.6 0 0 1 10.8 0M3.2 10.6a12.4 12.4 0 0 1 17.6 0" {S}/>',
    "wifi2": f'<path d="M12 19.5l-2.6-2.7a3.7 3.7 0 0 1 5.2 0z" fill="#fff"/><path d="M6.6 14a7.6 7.6 0 0 1 10.8 0" {S}/><path d="M3.2 10.6a12.4 12.4 0 0 1 17.6 0" {S} opacity=".35"/>',
    "wifi1": f'<path d="M12 19.5l-2.6-2.7a3.7 3.7 0 0 1 5.2 0z" fill="#fff"/><path d="M6.6 14a7.6 7.6 0 0 1 10.8 0M3.2 10.6a12.4 12.4 0 0 1 17.6 0" {S} opacity=".35"/>',
    "wifi0": f'<path d="M12 19.5l-2.6-2.7a3.7 3.7 0 0 1 5.2 0z" fill="#fff" opacity=".35"/><path d="M6.6 14a7.6 7.6 0 0 1 10.8 0M3.2 10.6a12.4 12.4 0 0 1 17.6 0" {S} opacity=".35"/>',
    "sync": f'<path d="M19 9a7.5 7.5 0 0 0-13.4-1.5M5 15a7.5 7.5 0 0 0 13.4 1.5" {S}/><path d="M19.5 4v5h-5M4.5 20v-5h5" {S}/>',
    "moon": '<path d="M15.5 3.5a8.6 8.6 0 1 0 5 13.9A7.2 7.2 0 0 1 15.5 3.5z" fill="#fff"/>',
    "rotate": f'<path d="M4.5 12a7.5 7.5 0 0 1 13-5.1M19.5 12a7.5 7.5 0 0 1-13 5.1" {S}/><path d="M18 3.5v4h-4M6 20.5v-4h4" {S}/>',
    "alarm": f'<circle cx="12" cy="13" r="7" {S}/><path d="M12 9.5V13l2.4 1.8M4 6.5 7 4M20 6.5 17 4" {S}/>',
    "timer": f'<circle cx="12" cy="13.5" r="7" {S}/><path d="M12 13.5V10M9.5 3.5h5M18.2 7.3l1.3-1.3" {S}/>',
    "gear": f'<circle cx="12" cy="12" r="3.2" {S}/><path d="M12 2.8v2.6M12 18.6v2.6M2.8 12h2.6M18.6 12h2.6M5.5 5.5l1.8 1.8M16.7 16.7l1.8 1.8M5.5 18.5l1.8-1.8M16.7 7.3l1.8-1.8" {S}/>',
    "sun": f'<circle cx="12" cy="12" r="4" fill="#fff"/><path d="M12 2.5v2.4M12 19.1v2.4M2.5 12h2.4M19.1 12h2.4M5.3 5.3 7 7M17 17l1.7 1.7M5.3 18.7 7 17M17 7l1.7-1.7" {S}/>',
    "speaker": f'<path d="M4 9.5h3.5L12 5.5v13l-4.5-4H4z" fill="#fff"/><path d="M15.5 9a4 4 0 0 1 0 6M18 6.5a7.5 7.5 0 0 1 0 11" {S}/>',
    "play": '<path d="M7 4.5v15l12.5-7.5z" fill="#fff"/>',
    "pause": '<rect x="6" y="4.5" width="4.2" height="15" rx="1.2" fill="#fff"/><rect x="13.8" y="4.5" width="4.2" height="15" rx="1.2" fill="#fff"/>',
    "next": '<path d="M4 5.5v13L13 12zM12 5.5v13L21 12z" fill="#fff"/>',
    "prev": '<path d="M20 5.5v13L11 12zM12 5.5v13L3 12z" fill="#fff"/>',
    "back": '<path d="M15 4.5 7.5 12l7.5 7.5" fill="none" stroke="#fff" stroke-width="2.8" stroke-linecap="round" stroke-linejoin="round"/>',
    "chev": '<path d="M9.5 5.5 16 12l-6.5 6.5" fill="none" stroke="#fff" stroke-width="2.4" stroke-linecap="round" stroke-linejoin="round"/>',
    "plus": f'<path d="M12 5v14M5 12h14" fill="none" stroke="#fff" stroke-width="2.6" stroke-linecap="round"/>',
    "check": '<path d="M5 12.5l4.5 4.5L19 7.5" fill="none" stroke="#fff" stroke-width="2.8" stroke-linecap="round" stroke-linejoin="round"/>',
    "bell": f'<path d="M6 16.5V11a6 6 0 0 1 12 0v5.5l1.5 2h-15z" fill="#fff"/><path d="M10 20.5a2 2 0 0 0 4 0" {S}/>',
    "trash": f'<path d="M4.5 6.5h15M9.5 6.5V4.5h5v2M6.5 6.5l1 13h9l1-13" {S}/>',
    "edit": f'<path d="M4.5 19.5l1-4.5L15.5 5l3.5 3.5-10 10z" {S}/>',
    "photo": f'<rect x="3.5" y="5.5" width="17" height="13" rx="2.5" {S}/><circle cx="9" cy="10" r="1.8" fill="#fff"/><path d="M4.5 17l5-4.5 3.5 3 2.5-2 4 3.5" {S}/>',
    "drop": '<path d="M12 3.5c3.5 4.6 6 7.8 6 10.7a6 6 0 0 1-12 0c0-2.9 2.5-6.1 6-10.7z" fill="#fff"/>',
    "wind": f'<path d="M3.5 9h11a3 3 0 1 0-3-3M3.5 13h15a3 3 0 1 1-3 3M3.5 17h7" {S}/>',
    "uv": f'<circle cx="12" cy="12" r="4.2" {S}/><path d="M12 3v2M12 19v2M3 12h2M19 12h2" {S}/>',
    "leaf": f'<path d="M5 19c0-8 5-13 14-14 0 9-5 14-13 14zM5 19l7-7" {S}/>',
    "sunrise": f'<path d="M6 16.5a6 6 0 0 1 12 0M3 19.5h18M12 3.5v5M9 6l3-2.5L15 6" {S}/>',
    "search": f'<circle cx="10.5" cy="10.5" r="6" {S}/><path d="M15 15l5 5" {S}/>',
    "home": '<path d="M3.5 11.5 12 4l8.5 7.5V20h-6v-5.5h-5V20h-6z" fill="#fff"/>',
    "grid": '<rect x="4" y="4" width="7" height="7" rx="2" fill="#fff"/><rect x="13" y="4" width="7" height="7" rx="2" fill="#fff"/><rect x="4" y="13" width="7" height="7" rx="2" fill="#fff"/><rect x="13" y="13" width="7" height="7" rx="2" fill="#fff"/>',
    "list": f'<path d="M8.5 6.5h11M8.5 12h11M8.5 17.5h11" {S}/><circle cx="4.5" cy="6.5" r="1.4" fill="#fff"/><circle cx="4.5" cy="12" r="1.4" fill="#fff"/><circle cx="4.5" cy="17.5" r="1.4" fill="#fff"/>',
    "flag": f'<path d="M5.5 21V4.5M5.5 5h11l-2.5 4 2.5 4h-11" {S}/>',
    "lap": f'<circle cx="12" cy="12" r="8" {S}/><path d="M12 7v5h4" {S}/>',
    "globe": f'<circle cx="12" cy="12" r="8.5" {S}/><path d="M3.5 12h17M12 3.5c2.5 2.3 3.6 5.2 3.6 8.5s-1.1 6.2-3.6 8.5c-2.5-2.3-3.6-5.2-3.6-8.5S9.5 5.8 12 3.5z" {S}/>',
    "pin": f'<path d="M12 21s-6.5-6.2-6.5-11a6.5 6.5 0 0 1 13 0c0 4.8-6.5 11-6.5 11z" {S}/><circle cx="12" cy="10" r="2.3" fill="#fff"/>',
    "power": f'<path d="M12 3.5v8M7 6.5a7.5 7.5 0 1 0 10 0" {S}/>',
    "battery": f'<rect x="2.5" y="7" width="17" height="10" rx="2.6" {S}/><rect x="20.5" y="10" width="1.8" height="4" rx=".9" fill="#fff"/>',
    "font": '<path d="M4 19 9.5 5h1.6L16.6 19h-2.4l-1.4-3.8H7.8L6.4 19zm4.6-6h3.6L10.3 8z" fill="#fff"/><path d="M16.5 11h5.5" stroke="#fff" stroke-width="2" stroke-linecap="round"/>',
}

APP_SIZES = [52, 40, 28]
WX_SIZES = [20, 28, 40, 56, 80]
GLYPH_SIZES = [16, 22]


def page():
    ub = (ROOT / "data/fonts/Ubuntu-Bold.ttf").as_uri()
    ur = (ROOT / "data/fonts/Ubuntu-Regular.ttf").as_uri()
    vb = (ROOT / "data/fonts/Ubuntu-Vietnamese-Bold.ttf").as_uri()
    css = (f"@font-face{{font-family:Ubuntu;font-weight:700;src:url({vb}) format('truetype');unicode-range:U+1EA0-1EF9}}"
           f"@font-face{{font-family:Ubuntu;font-weight:700;src:url({ub}) format('truetype')}}"
           f"@font-face{{font-family:Ubuntu;font-weight:400;src:url({ur}) format('truetype')}}"
           "html,body{margin:0;background:transparent}"
           ".c{position:absolute;width:80px;height:80px;display:flex;align-items:center;justify-content:center}"
           ".ic{position:relative;width:52px;height:52px;border-radius:14px;display:flex;align-items:center;justify-content:center;overflow:hidden}"
           ".sh .ic{box-shadow:0 2px 5px rgba(0,0,0,.28)}")
    items, cells = [], []
    for shadow in (True, False):
        for k, (bg, inner) in APPS.items():
            items.append(("app", k, shadow))
            cells.append(f'<div class="ic" style="background:{bg}">{inner}</div>')
    for k, body in WEATHER.items():
        items.append(("wx", k, False))
        cells.append(svg(body, 64, 48))
    for k, body in GLYPHS.items():
        items.append(("gl", k, False))
        cells.append(svg(body, 64, 24))
    per_row = 10
    html = [f"<html><head><meta charset='utf-8'><style>{css}</style></head><body>"]
    for i, c in enumerate(cells):
        x, y = (i % per_row) * CELL, (i // per_row) * CELL
        cls = "c sh" if items[i][0] == "app" and items[i][2] else "c"
        html.append(f'<div class="{cls}" style="left:{x}px;top:{y}px">{c}</div>')
    html.append("</body></html>")
    rows = (len(cells) + per_row - 1) // per_row
    return "".join(html), items, per_row * CELL, rows * CELL


def render():
    html, items, w, h = page()
    tmp = Path(tempfile.mkdtemp())
    (tmp / "art.html").write_text(html)
    out = tmp / "art.png"
    subprocess.run([CHROME, "--headless=new", "--disable-gpu", "--hide-scrollbars", "--allow-file-access-from-files",
                    f"--force-device-scale-factor={SCALE}", "--default-background-color=00000000",
                    f"--window-size={w},{h}", f"--screenshot={out}", (tmp / "art.html").as_uri()],
                   check=True, capture_output=True)
    return _pil().open(out).convert("RGBA"), items


def cell(sheet, i, per_row=10):
    x, y = (i % per_row) * CELL * SCALE, (i // per_row) * CELL * SCALE
    return sheet.crop((x, y, x + CELL * SCALE, y + CELL * SCALE))


def center_crop(im, css):
    """The middle css x css px of a cell (in sheet pixels)."""
    n = CELL * SCALE
    s = int(css * SCALE)
    o = (n - s) // 2
    return im.crop((o, o, o + s, o + s))


def _pil():
    from PIL import Image  # only for rendering (not for --repack)
    return Image


def rgb565a8(im):
    px = im.load()
    w, h = im.size
    color, alpha = bytearray(), bytearray()
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a:  # un-premultiply against nothing: Chrome gives straight alpha
                pass
            v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
            color += bytes((v & 0xFF, v >> 8))
            alpha.append(a)
    return bytes(color + alpha), w * 2


def a8(im):
    return bytes(im.getchannel("A").tobytes()), im.size[0]


IMAGES = []  # (name, data, w, h, stride, cf) in output order


def emit(out, name, data, w, h, stride, cf):
    IMAGES.append((name, bytes(data), w, h, stride, cf))


def write(images):
    """src/art.c (the deflated blob and a table of the images, both in flash) and src/art.h (the names)."""
    blob, table = bytearray(), []
    for name, data, w, h, stride, cf in images:
        blob += b"\0" * (-len(blob) % 4)  # LVGL reads RGB565 as 16-bit words: keep every image aligned
        table.append((name, len(blob), len(data), w, h, stride, cf))
        blob += data
    z = zlib.compress(bytes(blob), 9)
    out = ["// Generated by tools/art/make_art.py: app icons and weather (RGB565A8), glyphs (A8). The pixels are one zlib",
           f"// stream ({len(blob)} bytes inflated, {len(z)} here). src/artz.cpp inflates it into PSRAM and builds the",
           "// descriptors there too (no internal RAM): art.h names them through art_dsc(i).",
           "#include \"art.h\"", "", f"const uint8_t art_z[{len(z)}] = {{"]
    for i in range(0, len(z), 32):
        out.append("  " + ",".join(str(b) for b in z[i:i + 32]) + ",")
    out += ["};", f"const uint32_t art_z_size = {len(z)}, art_raw_size = {len(blob)}, art_z_count = {len(images)};", "",
            "// off, size, w, h, stride, colour format", "const art_z_entry_t art_z_table[] = {"]
    out += [f"  {{{off}, {size}, {w}, {h}, {stride}, {cf}}},  // {i} {name}"
            for i, (name, off, size, w, h, stride, cf) in enumerate(table)]
    out.append("};")
    (ROOT / "src/art.c").write_text("\n".join(out) + "\n")
    hdr = ["#pragma once", "// Generated by tools/art/make_art.py. The pixels are deflated in flash; src/artz.cpp inflates them into",
           "// PSRAM before setup() and keeps the descriptors in PSRAM too. Each name is that descriptor: &ic_news_52 is a",
           "// const lv_image_dsc_t* as before (a call, so it also works in tables built before setup()).",
           "#include <lvgl.h>", "#ifdef __cplusplus", 'extern "C" {', "#endif",
           "typedef struct {", "  uint32_t off, size;", "  uint16_t w, h, stride;", "  uint8_t cf;", "} art_z_entry_t;",
           "extern const uint8_t art_z[];", "extern const uint32_t art_z_size, art_raw_size, art_z_count;",
           "extern const art_z_entry_t art_z_table[];",
           "// Image i of the table above (unpacked on the first call; 0 x 0 images when there is no PSRAM).",
           "lv_image_dsc_t* art_dsc(uint32_t i);", "#ifdef __cplusplus", "}", "#endif", ""]
    hdr += ["// Each image's number: for tables kept in flash (store the number, call art_dsc(n) when drawing);",
            "// a table of &name pointers has to be filled at run time and so lives in internal RAM.",
            "enum art_id {"]
    hdr += [f"  ART_{name} = {i}," for i, (name, *_) in enumerate(table)]
    hdr += ["};", ""]
    hdr += [f"#define {name} (*art_dsc(ART_{name}))" for i, (name, *_) in enumerate(table)]
    (ROOT / "src/art.h").write_text("\n".join(hdr) + "\n")
    print(f"{len(images)} images, {len(blob)} bytes of pixels, {len(z)} bytes deflated")


def read_current():
    """The images of the current src/art.c, in order: the old form (one array per image), the first deflated form
    (descriptors in art.c) or this one (a table)."""
    s = (ROOT / "src/art.c").read_text()
    hdr = r"\.header = \{\.magic = LV_IMAGE_HEADER_MAGIC, \.cf = (\w+), \.flags = 0, \.w = (\d+), \.h = (\d+), \.stride = (\d+)"
    if "art_z[" in s:
        z = bytes(int(x) for x in re.search(r"art_z\[\d+\] = \{(.*?)\};", s, re.S).group(1).replace("\n", "").split(",") if x.strip())
        blob = zlib.decompress(z)
        rows = re.findall(r"\{(\d+), (\d+), (\d+), (\d+), (\d+), (\w+)\},  // \d+ (\w+)", s)
        if rows:
            return [(name, blob[int(o):int(o) + int(n)], int(w), int(h), int(st), cf) for o, n, w, h, st, cf, name in rows]
        offs = dict(re.findall(r"\{&(\w+), (\d+)\}", s))
        out = []
        for m in re.finditer(r"lv_image_dsc_t (\w+) = \{" + hdr + r".*?\.data_size = (\d+)", s):
            name, cf, w, h, stride, size = m.groups()
            o = int(offs[name])
            out.append((name, blob[o:o + int(size)], int(w), int(h), int(stride), cf))
        return out
    arrs = {m.group(1): bytes(int(x) for x in m.group(2).replace("\n", "").split(",") if x.strip())
            for m in re.finditer(r"static const uint8_t (\w+)_px\[\] = \{(.*?)\};", s, re.S)}
    out = []
    for m in re.finditer(r"const lv_image_dsc_t (\w+) = \{\s*" + hdr + r".*?\.data_size = (\d+), \.data = (\w+)_px", s, re.S):
        name, cf, w, h, stride, size, data = m.groups()
        assert len(arrs[data]) == int(size), name
        out.append((name, arrs[data], int(w), int(h), int(stride), cf))
    return out


def main():
    Image = _pil()
    sheet, items = render()
    out = []
    names = []
    contact = []
    for i, (kind, key, shadow) in enumerate(items):
        c = cell(sheet, i)
        if kind == "app":
            if shadow:  # home and dock: 52 px icon with its shadow in a 60 px image
                im = center_crop(c, 60).resize((60, 60), Image.LANCZOS)
                data, stride = rgb565a8(im)
                name = f"ic_{key}_52"
                emit(out, name, data, 60, 60, stride, "LV_COLOR_FORMAT_RGB565A8")
                names.append(name)
                contact.append(im)
            else:
                base = center_crop(c, 52)
                for n in APP_SIZES[1:]:
                    im = base.resize((n, n), Image.LANCZOS)
                    data, stride = rgb565a8(im)
                    name = f"ic_{key}_{n}"
                    emit(out, name, data, n, n, stride, "LV_COLOR_FORMAT_RGB565A8")
                    names.append(name)
        elif kind == "wx":
            base = center_crop(c, 64)
            for n in WX_SIZES:
                im = base.resize((n, n), Image.LANCZOS)
                data, stride = rgb565a8(im)
                name = f"wx_{key}_{n}"
                emit(out, name, data, n, n, stride, "LV_COLOR_FORMAT_RGB565A8")
                names.append(name)
                if n == 56:
                    contact.append(im)
        else:
            base = center_crop(c, 64)
            for n in GLYPH_SIZES:
                im = base.resize((n, n), Image.LANCZOS)
                data, stride = a8(im)
                name = f"gl_{key}_{n}"
                emit(out, name, data, n, n, stride, "LV_COLOR_FORMAT_A8")
                names.append(name)
                if n == 22:
                    contact.append(im)
    write(IMAGES)
    # Contact sheet on the default wallpaper colour, to eyeball.
    cols = 12
    rows = (len(contact) + cols - 1) // cols
    sh = Image.new("RGBA", (cols * 66, rows * 66), (33, 26, 84, 255))
    for i, im in enumerate(contact):
        sh.alpha_composite(im, ((i % cols) * 66 + (66 - im.size[0]) // 2, (i // cols) * 66 + (66 - im.size[1]) // 2))
    sh.save(ROOT / "tools/art/sheet.png")


if __name__ == "__main__":
    if sys.argv[1:] == ["--repack"]:
        write(read_current())
    else:
        main()
