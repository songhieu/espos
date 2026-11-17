#pragma once

// The fonts a declarative layout may use (s3desk/docs/profiles.md §4.6), one row per face:
// X(device font id, font data, its family's regular data, family, bold, size in px).
// The device (DeclGfx.cpp) reads the id column and test/face_render/HostCanvas.cpp the data columns, so
// the computer draws with the same set; a macro's unused columns are never expanded.
// Sizes are the converter's points at 150 dpi in pixels (src/inkdesk/InkFonts.h).
#define INKDESK_DECL_FONTS(X)                                                                 \
  X(NOTOSANS_12_FONT_ID, notosans_12_regular, notosans_12_regular, Sans, false, 25)           \
  X(NOTOSANS_12_FONT_ID, notosans_12_bold, notosans_12_regular, Sans, true, 25)               \
  X(NOTOSANS_14_FONT_ID, notosans_14_regular, notosans_14_regular, Sans, false, 29)           \
  X(NOTOSANS_14_FONT_ID, notosans_14_bold, notosans_14_regular, Sans, true, 29)               \
  X(NOTOSANS_16_FONT_ID, notosans_16_regular, notosans_16_regular, Sans, false, 33)           \
  X(NOTOSANS_16_FONT_ID, notosans_16_bold, notosans_16_regular, Sans, true, 33)               \
  X(NOTOSANS_18_FONT_ID, notosans_18_regular, notosans_18_regular, Sans, false, 37)           \
  X(NOTOSANS_18_FONT_ID, notosans_18_bold, notosans_18_regular, Sans, true, 37)               \
  X(INK_24_FONT_ID, ink_24_bold, ink_24_bold, Sans, true, 50)                                 \
  X(INK_SERIF_36_FONT_ID, ink_serif_36, ink_serif_36, Serif, true, 75)                        \
  X(INK_30_FONT_ID, ink_30_digits, ink_30_digits, Digits, true, 62)                           \
  X(INK_44_FONT_ID, ink_44_digits, ink_44_digits, Digits, true, 92)                           \
  X(INK_60_FONT_ID, ink_60_digits, ink_60_digits, Digits, true, 125)                          \
  X(UI_10_FONT_ID, ubuntu_10_regular, ubuntu_10_regular, Ui, false, 21)                       \
  X(UI_10_FONT_ID, ubuntu_10_bold, ubuntu_10_regular, Ui, true, 21)                           \
  X(UI_12_FONT_ID, ubuntu_12_regular, ubuntu_12_regular, Ui, false, 25)                       \
  X(UI_12_FONT_ID, ubuntu_12_bold, ubuntu_12_regular, Ui, true, 25)
