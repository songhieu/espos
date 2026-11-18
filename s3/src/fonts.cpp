#include "fonts.h"

#include <cstdlib>
#include <cstring>
#include <vector>

#include <Arduino.h>  // heap_caps_* (the simulator's shim too)

#include "pkg/pkg.h"

// Pre-rendered Ubuntu bitmap fonts (tools/fontconv/make_fonts.sh): Latin-1 at the text sizes (Vietnamese letters
// come from a font package, below); the big sizes hold digits (and : . , + - % °) only and borrow
// letters from the largest text font of the same weight. LVGL symbols fall back to the
// built-in Montserrat.
extern "C" {
extern const lv_font_t ub_r12, ub_r13, ub_r14, ub_r15, ub_r16, ub_r18, ub_r20;
extern const lv_font_t ub_b12, ub_b13, ub_b14, ub_b15, ub_b16, ub_b17, ub_b18, ub_b20, ub_b22, ub_b24;
extern const lv_font_t ub_b28, ub_b34, ub_b44, ub_b56, ub_b64, ub_b96, ub_r40, ub_r64, ub_r84, ub_r96;
extern const lv_font_t lang_names;  // the languages' own names in the letters the fonts lack (15 px, i18n.cpp)
}

namespace fonts {
namespace {

struct Entry {
  int px;
  bool bold;
  bool digits;  // digits only
  const lv_font_t* src;
  lv_font_t font;  // copy with the fallback chain set
  lv_font_t* bare;  // text sizes: Vietnamese letters without their marks (PSRAM)
};

Entry table[] = {
    {12, false, false, &ub_r12, {}}, {13, false, false, &ub_r13, {}}, {14, false, false, &ub_r14, {}},
    {15, false, false, &ub_r15, {}}, {16, false, false, &ub_r16, {}}, {18, false, false, &ub_r18, {}},
    {20, false, false, &ub_r20, {}}, {12, true, false, &ub_b12, {}},  {13, true, false, &ub_b13, {}},
    {14, true, false, &ub_b14, {}},  {15, true, false, &ub_b15, {}},  {16, true, false, &ub_b16, {}},
    {17, true, false, &ub_b17, {}},  {18, true, false, &ub_b18, {}},  {20, true, false, &ub_b20, {}},
    {22, true, false, &ub_b22, {}},  {24, true, false, &ub_b24, {}},  {28, true, true, &ub_b28, {}},
    {34, true, true, &ub_b34, {}},   {44, true, true, &ub_b44, {}},   {56, true, true, &ub_b56, {}},
    {64, true, true, &ub_b64, {}},   {96, true, true, &ub_b96, {}},   {40, false, true, &ub_r40, {}},  {64, false, true, &ub_r64, {}},
    {84, false, true, &ub_r84, {}},  {96, false, true, &ub_r96, {}},
};

const lv_font_t* symbols(int px) {
  if (px >= 26) return &lv_font_montserrat_28;
  if (px >= 18) return &lv_font_montserrat_20;
  return &lv_font_montserrat_14;
}

// Nearest size in the weight asked; text sizes only when `text` (the digit fonts lack letters).
Entry* find(int px, bool bold, bool text = false) {
  Entry* best = nullptr;
  for (Entry& e : table) {
    if (e.bold != bold || (text && e.digits)) continue;
    if (!best || abs(e.px - px) < abs(best->px - px) || (abs(e.px - px) == abs(best->px - px) && !e.digits)) best = &e;
  }
  return best;
}

// Vietnamese letters are not built in (the font package dev.espos.font.vi has them): without it they show without
// their marks ("Hà Nội" -> "Ha Noi"), from a copy of the text font that folds the letter first.
uint32_t fold(uint32_t c) {
  if (c >= 0x1EA0 && c <= 0x1EF9) {
    const int k = (c - 0x1EA0) / 2;
    const char b = k < 12 ? 'A' : k < 20 ? 'E' : k < 22 ? 'I' : k < 34 ? 'O' : k < 41 ? 'U' : 'Y';
    return (c & 1) ? b + 32 : b;
  }
  switch (c) {
    case 0x102: case 0x103: return c == 0x102 ? 'A' : 'a';
    case 0x110: case 0x111: return c == 0x110 ? 'D' : 'd';
    case 0x128: case 0x129: return c == 0x128 ? 'I' : 'i';
    case 0x168: case 0x169: return c == 0x168 ? 'U' : 'u';
    case 0x1A0: case 0x1A1: return c == 0x1A0 ? 'O' : 'o';
    case 0x1AF: case 0x1B0: return c == 0x1AF ? 'U' : 'u';
  }
  return 0;
}
bool bareDsc(const lv_font_t* f, lv_font_glyph_dsc_t* d, uint32_t letter, uint32_t next) {
  const uint32_t b = fold(letter);
  return b && lv_font_get_glyph_dsc_fmt_txt(f, d, b, next);
}

// A language's font (a font package's r<px>.bin / b<px>.bin, written by tools/i18n.py font), little endian:
//   "ESF1", u8 px, u8 bold, u8 bpp, u8 format (0 plain), i16 line height, i16 base line, i8 underline position,
//   i8 underline thickness, u16 0, u32 first code point, u32 glyphs, u32 offset of the code points (u16 each, minus
//   the first, sorted), u32 offset of the glyph descriptions ((glyphs + 1) x 8 bytes, LVGL's lv_font_fmt_txt_glyph_dsc_t,
//   the first unused), u32 offset of the bitmaps, u32 range length.
// Read in place from the memory-mapped flash, or copied into PSRAM (card, LittleFS); never in internal RAM.
struct LangFont {
  lv_font_t font;
  lv_font_fmt_txt_dsc_t dsc;
  lv_font_fmt_txt_cmap_t cmap;
  int px;
  bool bold;
};
struct LangSet {
  String id;
  std::vector<LangFont*> fonts;
};
std::vector<LangSet*> sets;  // kept until the next restart: labels drawn with them may still be on screen
lv_font_t names;             // lang_names, in the 15 px regular chain

LangFont* parse(const uint8_t* b, uint32_t len) {
  struct Head {
    char magic[4];
    uint8_t px, bold, bpp, format;
    int16_t lineH, base;
    int8_t ulPos, ulThick;
    uint16_t pad;
    uint32_t first, n, offList, offDsc, offBmp, range;
  } h;
  if (len < sizeof h) return nullptr;
  memcpy(&h, b, sizeof h);
  if (memcmp(h.magic, "ESF1", 4) || h.format != 0 || h.offList + h.n * 2 > len || h.offDsc + (h.n + 1) * 8 > len ||
      h.offBmp > len || (h.offDsc & 3) || (h.offList & 1) || h.range > 0xffff)
    return nullptr;
  LangFont* f = (LangFont*)heap_caps_calloc(1, sizeof(LangFont), MALLOC_CAP_SPIRAM);
  if (!f) return nullptr;
  f->px = h.px, f->bold = h.bold;
  f->cmap.range_start = h.first;
  f->cmap.range_length = h.range;
  f->cmap.glyph_id_start = 1;
  f->cmap.unicode_list = (const uint16_t*)(b + h.offList);
  f->cmap.list_length = h.n;
  f->cmap.type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY;
  f->dsc.glyph_bitmap = b + h.offBmp;
  f->dsc.glyph_dsc = (const lv_font_fmt_txt_glyph_dsc_t*)(b + h.offDsc);
  f->dsc.cmaps = &f->cmap;
  f->dsc.cmap_num = 1;
  f->dsc.bpp = h.bpp;
  f->font.get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt;
  f->font.get_glyph_bitmap = lv_font_get_bitmap_fmt_txt;
  f->font.line_height = h.lineH;
  f->font.base_line = h.base;
  f->font.underline_position = h.ulPos;
  f->font.underline_thickness = h.ulThick;
  f->font.dsc = &f->dsc;
  f->font.fallback = symbols(h.px);
  return f;
}

LangSet* loadSet(const char* id) {
  for (LangSet* s : sets)
    if (s->id == id) return s;
  LangSet* s = new LangSet;
  s->id = id;
  static const uint8_t SIZES[] = {12, 13, 14, 15, 16, 17, 18, 20, 22, 24};
  for (int bold = 0; bold < 2; bold++)
    for (uint8_t px : SIZES) {
      char name[12];
      snprintf(name, sizeof name, "%c%u.bin", bold ? 'b' : 'r', px);
      pkg::File file;
      if (!pkg::open(id, name, file)) continue;
      const uint8_t* b = file.data();
      if (!b || ((uintptr_t)b & 3)) b = pkg::loadAll(file);  // the card or LittleFS: a copy in PSRAM
      if (!b) continue;
      if (LangFont* f = parse(b, file.size())) s->fonts.push_back(f);
    }
  if (s->fonts.empty()) {
    delete s;
    return nullptr;
  }
  sets.push_back(s);
  return s;
}

// The language font for a text size: the same weight and the nearest size, else the nearest size.
LangFont* pick(LangSet* s, int px, bool bold) {
  LangFont* best = nullptr;
  for (LangFont* f : s->fonts) {
    if (!best) {
      best = f;
      continue;
    }
    const int d = abs(f->px - px) + (f->bold != bold ? 100 : 0), bd = abs(best->px - px) + (best->bold != bold ? 100 : 0);
    if (d < bd) best = f;
  }
  return best;
}

void chain(LangSet* s) {
  for (Entry& e : table) {
    if (e.digits) {
      e.font.fallback = &find(e.px, e.bold, true)->font;
      continue;
    }
    // Text size -> [Vietnamese letters without marks] -> the language's font -> symbols. A font package that has
    // the Vietnamese letters (dev.espos.font.vi) comes first instead: their marks are kept.
    LangFont* f = s ? pick(s, e.px, e.bold) : nullptr;
    const lv_font_t* next = f ? &f->font : symbols(e.px);
    lv_font_glyph_dsc_t g;
    const bool vi = f && lv_font_get_glyph_dsc_fmt_txt(&f->font, &g, 0x1EA1, 0);
    if (e.bare && !vi) {
      e.bare->fallback = next;
      next = e.bare;
    }
    if (e.px == 15 && !e.bold) {  // the language list's names
      names.fallback = next;
      next = &names;
    }
    e.font.fallback = next;
  }
}

}  // namespace

void begin() {
  names = lang_names;
  for (Entry& e : table) {
    e.font = *e.src;
    if (e.digits) continue;
    e.bare = (lv_font_t*)heap_caps_malloc(sizeof(lv_font_t), MALLOC_CAP_SPIRAM);
    if (!e.bare) continue;
    *e.bare = *e.src;
    e.bare->get_glyph_dsc = bareDsc;
    e.bare->fallback = symbols(e.px);
  }
  chain(nullptr);
}

bool useLang(const char* fontPkg) {
  LangSet* s = fontPkg ? loadSet(fontPkg) : nullptr;
  chain(s);
  return !fontPkg || s;
}

const lv_font_t* fromFile(const uint8_t* b, uint32_t len, int px, bool bold) {
  LangFont* f = b && !((uintptr_t)b & 3) ? parse(b, len) : nullptr;
  if (!f) return nullptr;
  f->font.fallback = &find(px, bold, true)->font;
  return &f->font;
}

const lv_font_t* get(int px, bool bold) { return &find(px, bold)->font; }
const lv_font_t* text(int px, bool bold) { return &find(px, bold, true)->font; }

}  // namespace fonts
