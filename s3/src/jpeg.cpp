#include "jpeg.h"

#include <Arduino.h>
#include <lvgl.h>

#include "trace.h"

#ifdef SIM
#include "src/libs/tjpgd/tjpgd.h"
#else
#include "rom/tjpgd.h"
#endif

namespace jpeg {
namespace {

struct Src {
  const uint8_t* data;
  size_t len, pos;
  uint16_t* out;
  int w, h;  // of out
};

#ifdef SIM
size_t input(JDEC* jd, uint8_t* buf, size_t n) {
#else
UINT input(JDEC* jd, BYTE* buf, UINT n) {
#endif
  Src* s = (Src*)jd->device;
  n = min<size_t>(n, s->len - s->pos);
  if (buf) memcpy(buf, s->data + s->pos, n);
  s->pos += n;
  return n;
}

#ifdef SIM
int output(JDEC* jd, void* bitmap, JRECT* r) {
#else
UINT output(JDEC* jd, void* bitmap, JRECT* r) {
#endif
  Src* s = (Src*)jd->device;
  const uint8_t* rgb = (const uint8_t*)bitmap;
  for (int y = r->top; y <= r->bottom; y++)
    for (int x = r->left; x <= r->right; x++, rgb += 3)
#ifdef SIM  // the simulator decodes with LVGL's tjpgd copy, which writes B, G, R
      if (x < s->w && y < s->h) s->out[y * s->w + x] = ((rgb[2] >> 3) << 11) | ((rgb[1] >> 2) << 5) | (rgb[0] >> 3);
#else
      if (x < s->w && y < s->h) s->out[y * s->w + x] = ((rgb[0] >> 3) << 11) | ((rgb[1] >> 2) << 5) | (rgb[2] >> 3);
#endif
  return 1;
}

constexpr size_t WORK = 8192;

}  // namespace

bool size(const uint8_t* data, size_t len, int& w, int& h) {
  void* work = heap_caps_malloc(WORK, MALLOC_CAP_SPIRAM);
  if (!work) return false;
  JDEC jd;
  Src s = {data, len, 0, nullptr, 0, 0};
  const bool ok = jd_prepare(&jd, input, work, WORK, &s) == JDR_OK;
  if (ok) w = jd.width, h = jd.height;
  heap_caps_free(work);
  return ok;
}

uint16_t* cover(const uint8_t* data, size_t len, int w, int h) {
  void* work = heap_caps_malloc(WORK, MALLOC_CAP_SPIRAM);
  if (!work) return nullptr;
  JDEC jd;
  Src s = {data, len, 0, nullptr, 0, 0};
  if (jd_prepare(&jd, input, work, WORK, &s) != JDR_OK) {
    heap_caps_free(work);
    return nullptr;
  }
  const int sw = jd.width, sh = jd.height;
  // The largest 1/2^n step that still covers the box.
  int scale = 0;
  while (scale < 3 && (sw >> (scale + 1)) >= w && (sh >> (scale + 1)) >= h) scale++;
#ifdef SIM
  scale = 0;  // LVGL's TJpgDec copy is built without scaling
#endif
  const int dw = (sw + (1 << scale) - 1) >> scale, dh = (sh + (1 << scale) - 1) >> scale;
  uint16_t* mid = (uint16_t*)heap_caps_calloc(dw * dh, 2, MALLOC_CAP_SPIRAM);
  uint16_t* out = (uint16_t*)heap_caps_malloc(w * h * 2, MALLOC_CAP_SPIRAM);
  bool ok = mid && out;
  if (ok) {
    s.pos = 0;
    s.out = mid, s.w = dw, s.h = dh;
    ok = jd_prepare(&jd, input, work, WORK, &s) == JDR_OK && jd_decomp(&jd, output, scale) == JDR_OK;
  }
  heap_caps_free(work);
  if (!ok) {
    heap_caps_free(mid);
    heap_caps_free(out);
    return nullptr;
  }
  // Cover: the centred part of mid with the box's aspect, averaged down to w x h.
  const float k = min((float)dw / w, (float)dh / h);  // source pixels per output pixel (>= 1)
  const float x0 = (dw - w * k) / 2, y0 = (dh - h * k) / 2;
  for (int y = 0; y < h; y++) {
    const int sy0 = (int)(y0 + y * k), sy1 = max(sy0 + 1, min(dh, (int)(y0 + (y + 1) * k)));
    for (int x = 0; x < w; x++) {
      const int sx0 = (int)(x0 + x * k), sx1 = max(sx0 + 1, min(dw, (int)(x0 + (x + 1) * k)));
      uint32_t r = 0, g = 0, b = 0, n = 0;
      for (int yy = sy0; yy < sy1; yy++)
        for (int xx = sx0; xx < sx1; xx++) {
          const uint16_t v = mid[yy * dw + xx];
          r += v >> 11, g += (v >> 5) & 63, b += v & 31, n++;
        }
      out[y * w + x] = ((r / n) << 11) | ((g / n) << 5) | (b / n);
    }
  }
  heap_caps_free(mid);
  return out;
}

}  // namespace jpeg
