// The built-in art (src/art.c, tools/art/make_art.py) is kept deflated in flash (~95 KB instead of ~667 KB) and
// inflated once into PSRAM, together with its descriptors (so no internal RAM: art.h names each image as
// (*art_dsc(i))). The first call does the work; a C++ constructor makes that happen before setup() (PSRAM is in the
// heap by then), and a table built even earlier simply triggers it. The board uses the ROM's tinfl (no code added),
// the simulator zlib. Without PSRAM or on a bad stream the images are 0 x 0 (drawn as nothing), never a crash.
#include <Arduino.h>

#include "art.h"

#ifdef SIM
#include <zlib.h>

#include <chrono>
#else
#include "esp_timer.h"
#include "miniz.h"  // the ROM's (esp_rom/include)
#endif

namespace {

lv_image_dsc_t* dscs = nullptr;  // art_z_count descriptors, PSRAM
bool done = false, ok = false;
uint32_t tookUs = 0;
const uint32_t zero = 0;
lv_image_dsc_t blank;  // what every name points at when there is no PSRAM

uint64_t nowUs() {
#ifdef SIM
  return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
#else
  return esp_timer_get_time();
#endif
}

uint8_t* inflateAll() {
#ifdef SIM
  if (getenv("SIM_ART_FAIL")) return nullptr;  // tests the path without PSRAM
#endif
  uint8_t* blob = (uint8_t*)heap_caps_malloc(art_raw_size, MALLOC_CAP_SPIRAM);
  if (!blob) return nullptr;
#ifdef SIM
  uLongf n = art_raw_size;
  const bool good = uncompress(blob, &n, art_z, art_z_size) == Z_OK && n == art_raw_size;
#else
  // tinfl's state (~11 KB) on the heap, not on the stack.
  tinfl_decompressor* r = (tinfl_decompressor*)heap_caps_malloc(sizeof(tinfl_decompressor), MALLOC_CAP_SPIRAM);
  if (!r) r = (tinfl_decompressor*)heap_caps_malloc(sizeof(tinfl_decompressor), MALLOC_CAP_8BIT);
  bool good = r != nullptr;
  if (good) {
    tinfl_init(r);
    size_t in = art_z_size, out = art_raw_size;
    const tinfl_status st = tinfl_decompress(r, art_z, &in, blob, blob, &out,
                                             TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    good = st == TINFL_STATUS_DONE && out == art_raw_size;
    heap_caps_free(r);
  }
#endif
  if (!good) heap_caps_free(blob), blob = nullptr;
  return blob;
}

void unpack() {
  done = true;
  const uint64_t t0 = nowUs();
  blank.header.magic = LV_IMAGE_HEADER_MAGIC;
  blank.header.cf = LV_COLOR_FORMAT_A8;
  blank.data = (const uint8_t*)&zero;
  dscs = (lv_image_dsc_t*)heap_caps_calloc(art_z_count, sizeof(lv_image_dsc_t), MALLOC_CAP_SPIRAM);
  uint8_t* blob = dscs ? inflateAll() : nullptr;
  ok = blob != nullptr;
  for (uint32_t i = 0; dscs && i < art_z_count; i++) {
    const art_z_entry_t& e = art_z_table[i];
    lv_image_dsc_t& d = dscs[i];
    d.header.magic = LV_IMAGE_HEADER_MAGIC;
    d.header.cf = e.cf;
    if (ok) {
      d.header.w = e.w, d.header.h = e.h, d.header.stride = e.stride;
      d.data_size = e.size;
      d.data = blob + e.off;
    } else {
      d.data = (const uint8_t*)&zero;  // 0 x 0: nothing to draw, nothing read
    }
  }
  tookUs = (uint32_t)(nowUs() - t0);
}

// Before setup(), before any screen.
struct Boot {
  Boot() {
    if (!done) unpack();
  }
} boot;

}  // namespace

extern "C" lv_image_dsc_t* art_dsc(uint32_t i) {
  if (!done) unpack();
  return dscs && i < art_z_count ? &dscs[i] : &blank;
}

namespace artz {
// For the trace at boot (apps.cpp): what happened, how long it took, how much PSRAM it holds.
const char* report() {
  static char s[112];
  snprintf(s, sizeof s,
           ok ? "art: %u images (+descriptors), %u bytes inflated into PSRAM from %u in %u ms"
              : "art: inflate FAILED (%u images blank; %u bytes from %u, %u ms)",
           (unsigned)art_z_count, (unsigned)art_raw_size, (unsigned)art_z_size, (unsigned)((tookUs + 999) / 1000));
  return s;
}
}  // namespace artz
