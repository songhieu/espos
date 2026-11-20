/* tools/fontconv/zfonts.py: the built-in fonts' bitmaps kept deflated (src/fonts/zfonts.c, src/fonts.cpp). */
#pragma once
#include <stdint.h>
#include "lvgl.h"
#ifdef UB_ZFONTS_RAW  /* -DUB_ZFONTS_RAW: the bitmaps as they were (raw in flash), for before/after checks */
#define UB_ZFONTS 0
#else
#define UB_ZFONTS 1
#endif
#define ZFONTS_N 26
#ifdef __cplusplus
extern "C" {
#endif
/* inplace: the font's own descriptions are in RAM (reached by name); fonts.cpp sets their glyph_bitmap */
typedef struct { const lv_font_t* font; uint32_t off, len; uint8_t inplace; } zfont_t;
extern const zfont_t zfonts[ZFONTS_N];
extern const uint32_t zfonts_raw, zfonts_size;
extern const uint8_t zfonts_z[];
#ifdef __cplusplus
}
#endif
