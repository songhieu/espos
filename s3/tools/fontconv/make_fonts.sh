#!/bin/sh
# Pre-renders the Ubuntu fonts into LVGL bitmap fonts at the sizes the UI uses. Run from s3/:
# sh tools/fontconv/make_fonts.sh
# Built in: English and the Latin-1 letters (Spanish, French, German, Portuguese, Indonesian). The Vietnamese letters
# (Ubuntu + the Ubuntu-Vietnamese supplement) are the font package dev.espos.font.vi since 2026-11-18 (tools/i18n.py
# font, docs/i18n.md): 159 KB out of the firmware. The text fonts keep the line box they had with them (the stacked
# Vietnamese marks need it, and every layout was made with it): METRICS below.
set -e
cd "$(dirname "$0")/../.."
CONV=tools/fontconv/node_modules/.bin/lv_font_conv
OUT=src/fonts
mkdir -p $OUT
F=data/fonts
# Latin-1 and punctuation.
# Currency signs the Ubuntu font has (euro, tugrik, hryvnia, rupee): money in a store's own format (the POS sample).
BASE="0x20-0x7E,0xA0-0xFF,0x2013-0x2014,0x2018-0x2019,0x201C-0x201D,0x2022,0x2212,0x20AC,0x20AE,0x20B4,0x20B9"
# name line_height base_line, as the fonts were with the Vietnamese letters.
METRICS="ub_b12 19 4;ub_b13 20 4;ub_b14 21 4;ub_b15 22 4;ub_b16 23 4;ub_b17 24 4;ub_b18 26 4;ub_b20 29 5;ub_b22 31 5;ub_b24 34 5;ub_r12 17 3;ub_r13 18 3;ub_r14 20 4;ub_r15 21 4;ub_r16 22 4;ub_r18 24 4;ub_r20 27 5"
DIGITS="0x20,0x25,0x2B-0x3A,0xB0,0x2212"   # space % + , - . / 0-9 : degree, minus sign

text() {  # name size bold
  w=Regular; [ "$3" = 1 ] && w=Bold
  $CONV --bpp 4 --size $2 --no-compress --no-kerning --format lvgl --lv-include lvgl.h \
    --font $F/Ubuntu-$w.ttf -r $BASE \
    -o $OUT/$1.c --lv-font-name $1 >/dev/null
  m=$(echo "$METRICS" | tr ';' '\n' | grep "^$1 ")
  set -- $m
  sed -i.bak -e "s/\.line_height = [0-9]*,/.line_height = $2,/" -e "s/\.base_line = -*[0-9]*,/.base_line = $3,/" $OUT/$1.c
  rm -f $OUT/$1.c.bak
}
digits() {  # name size bold
  w=Regular; [ "$3" = 1 ] && w=Bold
  $CONV --bpp 4 --size $2 --no-compress --no-kerning --format lvgl --lv-include lvgl.h \
    --font $F/Ubuntu-$w.ttf -r $DIGITS -o $OUT/$1.c --lv-font-name $1 >/dev/null
}

for s in 12 13 14 15 16 18 20; do text ub_r$s $s 0; done
for s in 12 13 14 15 16 17 18 20 22 24; do text ub_b$s $s 1; done
digits ub_b28 28 1
digits ub_b34 34 1
digits ub_b44 44 1
digits ub_b56 56 1
digits ub_r40 40 0
digits ub_r64 64 0
digits ub_b64 64 1
digits ub_b96 96 1   # the date sheet's day number (src/wid/cal.cpp)
digits ub_r84 84 0
digits ub_r96 96 0
ls -la $OUT

# The glyph bitmaps of most fonts deflated (src/fonts/zfonts.c, inflated at boot by src/fonts.cpp): the app slot is full.
python3 tools/fontconv/zfonts.py
