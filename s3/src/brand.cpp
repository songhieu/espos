#pragma GCC optimize("Os")  // not per frame: built small (0.5.7 flash diet)
// The EspOS mark and wordmark (brand.h). brand_art.c keeps them deflated in flash (~1.6 KB); the first call
// inflates them into PSRAM (~16 KB) the way artz.cpp does the built-in art: the ROM's tinfl on the board (no code
// added), zlib in the simulator.
#include "brand.h"

#include <Arduino.h>

#ifdef SIM
#include <zlib.h>
#else
#include "miniz.h"  // the ROM's (esp_rom/include)
#endif

namespace brand {
namespace {

lv_image_dsc_t* dscs = nullptr;  // brand_z_count descriptors, PSRAM
bool tried = false;
const uint32_t zero = 0;
lv_image_dsc_t blank;

uint8_t* inflateAll() {
  uint8_t* blob = (uint8_t*)heap_caps_malloc(brand_raw_size, MALLOC_CAP_SPIRAM);
  if (!blob) return nullptr;
#ifdef SIM
  uLongf n = brand_raw_size;
  const bool good = uncompress(blob, &n, brand_z, brand_z_size) == Z_OK && n == brand_raw_size;
#else
  tinfl_decompressor* r = (tinfl_decompressor*)heap_caps_malloc(sizeof(tinfl_decompressor), MALLOC_CAP_SPIRAM);
  if (!r) r = (tinfl_decompressor*)heap_caps_malloc(sizeof(tinfl_decompressor), MALLOC_CAP_8BIT);
  bool good = r != nullptr;
  if (good) {
    tinfl_init(r);
    size_t in = brand_z_size, out = brand_raw_size;
    const tinfl_status st = tinfl_decompress(r, brand_z, &in, blob, blob, &out,
                                             TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    good = st == TINFL_STATUS_DONE && out == brand_raw_size;
    heap_caps_free(r);
  }
#endif
  if (!good) heap_caps_free(blob), blob = nullptr;
  return blob;
}

void unpack() {
  tried = true;
  blank.header.magic = LV_IMAGE_HEADER_MAGIC;
  blank.header.cf = LV_COLOR_FORMAT_A8;
  blank.data = (const uint8_t*)&zero;
  dscs = (lv_image_dsc_t*)heap_caps_calloc(brand_z_count, sizeof(lv_image_dsc_t), MALLOC_CAP_SPIRAM);
  uint8_t* blob = dscs ? inflateAll() : nullptr;
  for (uint32_t i = 0; dscs && i < brand_z_count; i++) {
    const brand_z_entry_t& e = brand_z_table[i];
    lv_image_dsc_t& d = dscs[i];
    d.header.magic = LV_IMAGE_HEADER_MAGIC;
    d.header.cf = LV_COLOR_FORMAT_A8;
    if (blob) {
      d.header.w = e.w, d.header.h = e.h, d.header.stride = e.w;
      d.data_size = e.size;
      d.data = blob + e.off;
    } else {
      d.data = (const uint8_t*)&zero;  // 0 x 0: nothing drawn, nothing read
    }
  }
}

lv_obj_t* tinted(lv_obj_t* parent, const lv_image_dsc_t* d, uint32_t rgb) {
  lv_obj_t* i = lv_image_create(parent);
  lv_image_set_src(i, d);
  lv_obj_remove_flag(i, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_image_recolor(i, lv_color_hex(rgb), 0);
  lv_obj_set_style_image_recolor_opa(i, LV_OPA_COVER, 0);
  return i;
}

}  // namespace

const lv_image_dsc_t* dsc(brand_pic p) {
  if (!tried) unpack();
  return dscs && (uint32_t)p < brand_z_count ? &dscs[p] : &blank;
}

const lv_image_dsc_t* markDsc(int px) {
  return dsc(px >= 64 ? BRAND_MARK_96 : px >= 24 ? BRAND_MARK_32 : BRAND_MARK_16);
}

lv_obj_t* mark(lv_obj_t* parent, int px, uint32_t rgb) { return tinted(parent, markDsc(px), rgb); }

lv_obj_t* word(lv_obj_t* parent, uint32_t rgb) { return tinted(parent, dsc(BRAND_WORD_32), rgb); }

}  // namespace brand
