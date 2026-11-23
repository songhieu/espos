#!/usr/bin/env python3
"""Builds the Sessions app's main.js (the file apps/pack.py packs) from src/app.js and i18n.json, and its icon files.

    python3 apps/sessions/tools/build.py          (from s3desk; --check: only report missing translations;
                                                  --dev: English where a translation is missing)

- main.js = "var TR = {<language>: {<English>: <translation>}};" + src/app.js. Every t('...') text of app.js must have
  a translation in each of the device's 10 other languages (src/i18n.cpp LANGS); a missing one is an error.
- pkg/icons/mic.a8: the microphone of the Talk buttons (an app icon file of jsui.cpp: u16 w, u16 h, then w*h alpha).
"""
import json
import re
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
LANGS = ["vi", "zh-Hans", "es", "fr", "de", "ja", "ko", "pt-BR", "ru", "id"]


def keys(js):
    out = []
    for m in re.finditer(r"\bt\(\s*('(?:[^'\\]|\\.)*'|\"(?:[^\"\\]|\\.)*\")", js):
        k = m.group(1)[1:-1].replace("\\'", "'").replace('\\"', '"')
        if k not in out:
            out.append(k)
    return out


def mic_a8(size=18):
    """A microphone drawn at 4x and averaged down: the alpha bytes."""
    s = size * 4
    px = [[0] * s for _ in range(s)]

    def disk(cx, cy, r):
        for y in range(s):
            for x in range(s):
                if (x - cx) ** 2 + (y - cy) ** 2 <= r * r:
                    px[y][x] = 1

    def rect(x0, y0, x1, y1):
        for y in range(max(0, y0), min(s, y1)):
            for x in range(max(0, x0), min(s, x1)):
                px[y][x] = 1

    def ring(cx, cy, r, w, below):
        for y in range(s):
            for x in range(s):
                d = ((x - cx) ** 2 + (y - cy) ** 2) ** 0.5
                if r - w <= d <= r and y >= below:
                    px[y][x] = 1

    u = s / 18
    rect(int(6.5 * u), int(4 * u), int(11.5 * u), int(10 * u))   # the capsule
    disk(9 * u, 4 * u, 2.5 * u)
    disk(9 * u, 10 * u, 2.5 * u)
    ring(9 * u, 9.5 * u, 5.2 * u, 1.4 * u, int(9.5 * u))        # the holder
    rect(int(8.3 * u), int(14.5 * u), int(9.7 * u), int(16.5 * u))  # the stand
    rect(int(6 * u), int(16 * u), int(12 * u), int(17.3 * u))
    out = bytearray()
    for y in range(size):
        for x in range(size):
            n = sum(px[y * 4 + j][x * 4 + i] for j in range(4) for i in range(4))
            out.append(min(255, n * 16))
    return struct.pack("<HH", size, size) + bytes(out)


def arrow_a8(direction, size=16):
    """A plain arrow (up, down, left, right), drawn at 4x and averaged down."""
    s = size * 4
    px = [[0] * s for _ in range(s)]
    u = s / 16
    for y in range(s):
        for x in range(s):
            # an up arrow: a stem and a head; turned for the other directions
            X, Y = x / u, y / u
            if direction == "down":
                Y = 16 - Y
            elif direction == "left":
                X, Y = Y, X
            elif direction == "right":
                X, Y = Y, 16 - X
            stem = 6.8 <= X <= 9.2 and 4.5 <= Y <= 14
            head = Y >= 2 and Y <= 9 and abs(X - 8) <= (Y - 2) * 0.95 and abs(X - 8) >= (Y - 2) * 0.95 - 2.6
            px[y][x] = 1 if stem or head else 0
    out = bytearray()
    for y in range(size):
        for x in range(size):
            n = sum(px[y * 4 + j][x * 4 + i] for j in range(4) for i in range(4))
            out.append(min(255, n * 16))
    return struct.pack("<HH", size, size) + bytes(out)


def close_a8(size=16):
    """An ×."""
    s = size * 4
    u = s / 16
    out = bytearray()
    for y in range(size):
        for x in range(size):
            n = 0
            for j in range(4):
                for i in range(4):
                    X, Y = (x * 4 + i) / u, (y * 4 + j) / u
                    if 3 <= X <= 13 and 3 <= Y <= 13 and (abs(X - Y) <= 1.2 or abs(X + Y - 16) <= 1.2):
                        n += 1
            out.append(min(255, n * 16))
    return struct.pack("<HH", size, size) + bytes(out)


def main():
    js = (HERE / "src" / "app.js").read_text(encoding="utf-8")
    tr = json.loads((HERE / "i18n.json").read_text(encoding="utf-8"))
    ks = keys(js)
    missing = [(lang, k) for lang in LANGS for k in ks if not tr.get(lang, {}).get(k)]
    unused = sorted({k for lang in tr for k in tr[lang] if k not in ks})
    for lang, k in missing[:40]:
        print(f"missing {lang}: {k}")
    if unused:
        print(f"{len(unused)} unused: " + "; ".join(unused[:10]))
    if "--check" in sys.argv:
        sys.exit(1 if missing else 0)
    if missing and "--dev" not in sys.argv:
        sys.exit(f"{len(missing)} translations missing")
    # The English texts once, each language's as a list in the same order; only the device's language is expanded.
    vals = {lang: [tr.get(lang, {}).get(k, "") for k in ks] for lang in LANGS}
    head = "// Built by apps/sessions/tools/build.py from src/app.js and i18n.json: edit those, not this file.\n"
    tr_js = ("var TR = (function () {\n  var K = " + json.dumps(ks, ensure_ascii=False, separators=(",", ":")) +
             ";\n  var V = " + json.dumps(vals, ensure_ascii=False, separators=(",", ":")) +
             ";\n  var out = {}, v = V[app.lang];\n  if (v) { out[app.lang] = {}; K.forEach(function (k, i) { if (v[i]) out[app.lang][k] = v[i]; }); }\n"
             "  return out;\n})();\n")
    (HERE / "main.js").write_text(head + tr_js + js, encoding="utf-8")
    icons = HERE / "pkg" / "icons"
    icons.mkdir(parents=True, exist_ok=True)
    (icons / "mic.a8").write_bytes(mic_a8())
    for d in ("up", "down", "left", "right"):
        (icons / f"{d}.a8").write_bytes(arrow_a8(d))
    (icons / "close.a8").write_bytes(close_a8())
    print(f"main.js: {len(ks)} texts in {len(LANGS) + 1} languages, {(HERE / 'main.js').stat().st_size} B")


if __name__ == "__main__":
    main()
