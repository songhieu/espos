#!/usr/bin/env python3
"""Generate the InkDesk display fonts (big titles and hero digits) with the repo's fontconvert.py.

  ink_24_bold  NotoSans Bold 24pt  (Latin + Vietnamese)  titles, dates, flashcard word
  ink_44_digits NotoSans Bold 44pt (digits, : . / ° -)    temperature, clock, counters
  ink_60_digits NotoSans Bold 60pt (digits)               landscape sleep-screen clock and day
  ink_30_digits NotoSans Bold 30pt (digits)               landscape sleep-screen temperature
  ink_serif_36 NotoSerif Bold 36pt (Latin + Vietnamese letters only)  word clock, widget headings

Run from anywhere: python3 firmware/scripts/inkdesk_fonts.py
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
FONTS = os.path.join(HERE, "..", "lib", "EpdFont")
CONVERT = os.path.join(FONTS, "scripts", "fontconvert.py")
SRC = os.path.join(FONTS, "builtinFonts", "source", "NotoSans", "NotoSans-Bold.ttf")
SERIF = os.path.join(FONTS, "builtinFonts", "source", "NotoSerif", "NotoSerif-Bold.ttf")
OUT = os.path.join(HERE, "..", "src", "inkdesk", "fonts")

# Digits plus what prices and clocks need: space % + , - . / : and the degree sign.
DIGITS_ONLY = "intervals = [(0x0020, 0x0020), (0x0025, 0x0025), (0x002B, 0x003A), (0x00B0, 0x00B0)]\n"
# Basic Latin and the letters Vietnamese uses, nothing else (keeps a big font small).
VIETNAMESE = ("intervals = [(0x0020, 0x007E), (0x00C0, 0x00C3), (0x00C8, 0x00CA), (0x00CC, 0x00CD), (0x00D2, 0x00D5), "
              "(0x00D9, 0x00DA), (0x00DD, 0x00DD), (0x00E0, 0x00E3), (0x00E8, 0x00EA), (0x00EC, 0x00ED), (0x00F2, 0x00F5), "
              "(0x00F9, 0x00FA), (0x00FD, 0x00FD), (0x0102, 0x0103), (0x0110, 0x0111), (0x0128, 0x0129), (0x0168, 0x0169), "
              "(0x01A0, 0x01A1), (0x01AF, 0x01B0), (0x1EA0, 0x1EF9), (0x00B7, 0x00B7)]\n")


def run(name, size, digits=False, intervals=None, src_font=SRC):
    os.makedirs(OUT, exist_ok=True)
    out = os.path.join(OUT, name + ".h")
    if digits:
        intervals = DIGITS_ONLY
    if intervals:
        # fontconvert always adds its default Latin/Vietnamese intervals; for the hero digits
        # only a handful of glyphs are needed, so run a copy with the table replaced.
        src = open(CONVERT, encoding="utf-8").read()
        start = src.index("intervals = [")
        end = src.index("]\n", start) + 2
        src = src[:start] + intervals + src[end:]
        code = "import sys; sys.argv=" + repr(["fontconvert.py", name, str(size), os.path.relpath(src_font, os.path.dirname(CONVERT)), "--2bit", "--compress"]) + "\n" + src
        res = subprocess.run([sys.executable, "-c", code], capture_output=True, cwd=os.path.dirname(CONVERT))
    else:
        res = subprocess.run([sys.executable, "fontconvert.py", name, str(size), os.path.relpath(SRC, os.path.dirname(CONVERT)), "--2bit", "--compress", "--pnum"],
                             capture_output=True, cwd=os.path.dirname(CONVERT))
    if res.returncode != 0:
        sys.stderr.write(res.stderr.decode())
        raise SystemExit(f"{name}: fontconvert failed")
    open(out, "wb").write(res.stdout)
    print(f"{name}: {len(res.stdout)} bytes of header -> {os.path.relpath(out)}")


run("ink_24_bold", 24)
run("ink_44_digits", 44, digits=True)
run("ink_60_digits", 60, digits=True)
run("ink_30_digits", 30, digits=True)
run("ink_serif_36", 36, intervals=VIETNAMESE, src_font=SERIF)
