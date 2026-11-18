#include "dog.h"

#include <Arduino.h>

#include <cmath>
#include <new>

#include "packs.h"
#include "tjpgd_pb.h"
#include "trace.h"

#ifndef SIM
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#endif

namespace pet {

#ifndef SIM
uint32_t nowUs() { return micros(); }
#endif

namespace dog {
namespace {

constexpr int MAX_PX = 320 * 304;  // a box is never larger than the canvas (largest seen: 320x230)
constexpr size_t POOL = 16 * 1024;  // TJpgDec work area with the fast Huffman tables

Stats st;
uint8_t* spanBuf = nullptr;  // span()'s masks read from the memory card (UI thread)

// ------------------------------------------------------------------ one decoder (scratch per thread)
struct Ctx {
  JDEC jd;
  uint8_t* pool = nullptr;
  uint8_t* mask = nullptr;   // 0/1 per box pixel
  uint8_t* vsum = nullptr;   // a row of vertical 3-sums
  uint16_t* full = nullptr;  // full-size colours when the picture is wanted at half size
  uint8_t* fullA = nullptr;
  uint8_t* data = nullptr;   // a picture's bytes read from the memory card (pk::dataMax())
  uint8_t hdr[1024];         // the shared JPEG header with this box's size
};

struct Src {
  const uint8_t* seg[3];
  size_t len[3];
  int k;
  size_t pos;
  uint16_t* out;
  int w, h;
};

size_t input(JDEC* jd, uint8_t* buf, size_t n) {
  Src* s = (Src*)jd->device;
  size_t done = 0;
  while (n && s->k < 3) {
    const size_t m = min(n, s->len[s->k] - s->pos);
    if (buf) memcpy(buf + done, s->seg[s->k] + s->pos, m);
    s->pos += m, done += m, n -= m;
    if (s->pos == s->len[s->k]) s->k++, s->pos = 0;
  }
  return done;
}

int output(JDEC* jd, void* bitmap, JRECT* r) {
  Src* s = (Src*)jd->device;
  const uint16_t* px = (const uint16_t*)bitmap;
  const int w = r->right - r->left + 1;
  const int cw = min(w, s->w - (int)r->left);
  for (int y = r->top; y <= r->bottom; y++, px += w)
    if (y < s->h && cw > 0) memcpy(s->out + y * s->w + r->left, px, cw * 2);
  return 1;
}

void* psram(size_t n) { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM); }

bool ensure(Ctx& c) {
  if (!c.pool) c.pool = (uint8_t*)psram(POOL);
  if (!c.mask) c.mask = (uint8_t*)psram(MAX_PX);
  if (!c.vsum) c.vsum = (uint8_t*)psram(320);
  if (!c.data && pk::dataMax()) c.data = (uint8_t*)psram(pk::dataMax());
  return c.pool && c.mask && c.vsum && (c.data || !pk::dataMax());
}

void freeCtx(Ctx& c) {
  heap_caps_free(c.pool), heap_caps_free(c.mask), heap_caps_free(c.vsum), heap_caps_free(c.full), heap_caps_free(c.fullA);
  heap_caps_free(c.data);
  c.pool = c.mask = c.vsum = c.fullA = c.data = nullptr;
  c.full = nullptr;
}

// The picture's colours: the pack's ONE JPEG header with this box's height / width, the scan (d), then FF D9.
bool jpeg(Ctx& c, const pbs2_frame_t* f, const uint8_t* d8, uint16_t* out) {
  const pbs2_head_t* d = pk::dog();
  if (d->hdr_len > sizeof c.hdr) return false;
  memcpy(c.hdr, pk::jpegHeader(), d->hdr_len);
  c.hdr[d->dims_off] = f->bh >> 8, c.hdr[d->dims_off + 1] = f->bh & 255;
  c.hdr[d->dims_off + 2] = f->bw >> 8, c.hdr[d->dims_off + 3] = f->bw & 255;
  static const uint8_t EOI[2] = {0xFF, 0xD9};
  Src s = {{c.hdr, d8, EOI}, {d->hdr_len, f->scan_len, 2}, 0, 0, out, f->bw, f->bh};
  if (jd_prepare(&c.jd, input, c.pool, POOL, &s) != JDR_OK) return false;
  return jd_decomp(&c.jd, output, 0) == JDR_OK;
}

// The 1-bit mask of a picture's box: run lengths, transparent first, alternating; a byte 255 = the run goes on.
bool unmask(const pbs2_frame_t* f, const uint8_t* d8, uint8_t* m) {
  const uint8_t* r = d8 + f->scan_len;
  const uint32_t n = (uint32_t)f->bw * f->bh;
  uint32_t at = 0, run = 0;
  uint8_t v = 0;
  for (uint32_t k = 0; k < f->mask_len; k++) {
    run += r[k];
    if (r[k] == 255) continue;
    if (at + run > n) return false;
    memset(m + at, v, run);
    at += run, v ^= 1, run = 0;
  }
  if (at < n) memset(m + at, v, n - at);
  return true;
}

// Soft edge: alpha = the mask's 3x3 mean (0..9 set -> 0..255), as pb_ref.c.
void soften(Ctx& c, int w, int h, uint8_t* a) {
  static const uint8_t LUT[10] = {0, 28, 57, 85, 113, 142, 170, 198, 227, 255};  // (s * 255 + 4) / 9
  const uint8_t* m = c.mask;
  for (int y = 0; y < h; y++) {
    const uint8_t* up = y ? m + (y - 1) * w : nullptr;
    const uint8_t* mid = m + y * w;
    const uint8_t* dn = y + 1 < h ? m + (y + 1) * w : nullptr;
    for (int x = 0; x < w; x++) c.vsum[x] = mid[x] + (up ? up[x] : 0) + (dn ? dn[x] : 0);
    uint8_t* o = a + y * w;
    for (int x = 0; x < w; x++) {
      const int s = c.vsum[x] + (x ? c.vsum[x - 1] : 0) + (x + 1 < w ? c.vsum[x + 1] : 0);
      o[x] = LUT[s];
    }
  }
}

bool fit(Pic& p, int px) {
  if (p.cap >= px) return true;
  heap_caps_free(p.rgb), heap_caps_free(p.a);
  p.rgb = (uint16_t*)psram(px * 2);
  p.a = (uint8_t*)psram(px);
  p.cap = p.rgb && p.a ? px : 0;
  return p.cap;
}

bool decodeInto(Ctx& c, const pbs2_frame_t* f, int shift, Pic& p) {
  p.f = nullptr;
  p.shift = shift;
  p.x = f->bx >> shift, p.y = f->by >> shift, p.w = f->bw >> shift, p.h = f->bh >> shift;
  if (!f->bw) return p.f = f, true;  // shadow only
  if (!ensure(c) || (uint32_t)f->bw * f->bh > MAX_PX || f->bw > 320) return false;
  if (!fit(p, shift ? MAX_PX / 4 : MAX_PX)) return false;
  const int w = f->bw, h = f->bh;
  const uint8_t* d8 = pk::data(f, c.data);  // in place, or read from the memory card
  if (!d8) return false;
  if (!shift) {
    if (!jpeg(c, f, d8, p.rgb) || !unmask(f, d8, c.mask)) return false;
    soften(c, w, h, p.a);
    return p.f = f, true;
  }
  if (!c.full) c.full = (uint16_t*)psram(MAX_PX * 2);
  if (!c.fullA) c.fullA = (uint8_t*)psram(MAX_PX);
  if (!c.full || !c.fullA || !jpeg(c, f, d8, c.full) || !unmask(f, d8, c.mask)) return false;
  soften(c, w, h, c.fullA);
  // Half size: 2x2 means, colours weighted by alpha so the edge does not pick up the JPEG's background.
  const int hw = w >> 1, hh = h >> 1;
  for (int y = 0; y < hh; y++)
    for (int x = 0; x < hw; x++) {
      int r = 0, g = 0, b = 0, sa = 0;
      for (int k = 0; k < 4; k++) {
        const int i = (2 * y + (k >> 1)) * w + 2 * x + (k & 1);
        const int a = c.fullA[i] + 1;
        const uint16_t v = c.full[i];
        r += (v >> 11) * a, g += ((v >> 5) & 63) * a, b += (v & 31) * a, sa += a;
      }
      p.rgb[y * hw + x] = (uint16_t)(((r / sa) << 11) | ((g / sa) << 5) | (b / sa));
      p.a[y * hw + x] = (uint8_t)((sa - 4 + 2) / 4);
    }
  return p.f = f, true;
}

// ------------------------------------------------------------------ three slots, one background decoder
struct Slot {
  Pic pic;
  const pbs2_frame_t* f = nullptr;
  int shift = 0;
  uint8_t state = 0;  // 0 empty, 1 busy (being decoded), 2 ready
  uint32_t used = 0;
};
constexpr int SLOTS = 3;
Slot slots[SLOTS];
int current = -1;
uint32_t useClock = 0;
// The decoder contexts (JDEC and a 1 KB header copy each) live in PSRAM: internal RAM is scarce.
Ctx& syncCtx() {
  static Ctx* c = new (heap_caps_malloc(sizeof(Ctx), MALLOC_CAP_SPIRAM)) Ctx();
  return *c;
}

#ifndef SIM
Ctx& bgCtx() {
  static Ctx* c = new (heap_caps_malloc(sizeof(Ctx), MALLOC_CAP_SPIRAM)) Ctx();
  return *c;
}
SemaphoreHandle_t mtx;
TaskHandle_t worker;
volatile int job = -1;

// No mutex before the decode worker has started (the pet opened and closed at once): then nothing runs beside
// the UI and there is nothing to lock. Taking a null mutex asserted on the board (2026-11-17, release() on close).
void lock() {
  if (mtx) xSemaphoreTake(mtx, portMAX_DELAY);
}
void unlock() {
  if (mtx) xSemaphoreGive(mtx);
}

void workerTask(void*) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    lock();
    const int j = job;
    const pbs2_frame_t* f = j >= 0 ? slots[j].f : nullptr;
    const int sh = j >= 0 ? slots[j].shift : 0;
    unlock();
    if (j < 0) continue;
    const uint32_t t0 = nowUs();
    const bool ok = decodeInto(bgCtx(), f, sh, slots[j].pic);
    const uint32_t us = nowUs() - t0;
    lock();
    slots[j].state = ok ? 2 : 0;
    job = -1;
    st.decUs += us, st.decMaxUs = max(st.decMaxUs, us), st.ahead++;
    if (!ok) st.failed++;
    unlock();
  }
}

void startWorker() {
  if (worker) return;
  mtx = xSemaphoreCreateMutex();
  // Core 0 next to WiFi and sound (the UI runs on core 1); the stack lives in PSRAM (internal RAM is scarce).
  if (xTaskCreatePinnedToCoreWithCaps(workerTask, "petdec", 4096, nullptr, 2, &worker, 0, MALLOC_CAP_SPIRAM) != pdPASS)
    worker = nullptr, trace::add("pet: no decode task, decoding inline");
}
#else
void lock() {}
void unlock() {}
#endif

int victim() {
  int v = -1;
  for (int i = 0; i < SLOTS; i++) {
    if (i == current || slots[i].state == 1) continue;
    if (v < 0 || slots[i].state == 0 || (slots[v].state && slots[i].used < slots[v].used)) v = i;
    if (slots[i].state == 0) break;
  }
  return v;
}

int find(const pbs2_frame_t* f, int shift) {
  for (int i = 0; i < SLOTS; i++)
    if (slots[i].state && slots[i].f == f && slots[i].shift == shift) return i;
  return -1;
}

}  // namespace

Stats& stats() { return st; }

const Pic* get(const pbs2_frame_t* f, int shift) {
  if (!f) return nullptr;
#ifndef SIM
  startWorker();
#endif
  lock();
  int i = find(f, shift);
#ifndef SIM
  while (i >= 0 && slots[i].state == 1) {  // the background decoder is on it: wait for it
    unlock();
    vTaskDelay(1);
    lock();
    i = find(f, shift);
  }
#endif
  if (i >= 0) {
    current = i;
    slots[i].used = ++useClock;
    unlock();
    st.pictures++;
    return &slots[i].pic;
  }
  i = victim();
  if (i < 0) {
    unlock();
    return nullptr;
  }
  slots[i].f = f, slots[i].shift = shift, slots[i].state = 1;
  unlock();
  const uint32_t t0 = nowUs();
  const bool ok = decodeInto(syncCtx(), f, shift, slots[i].pic);
  const uint32_t us = nowUs() - t0;
  lock();
  slots[i].state = ok ? 2 : 0;
  if (ok) current = i, slots[i].used = ++useClock;
  st.decUs += us, st.decMaxUs = max(st.decMaxUs, us), st.late++, st.pictures++;
  if (!ok) st.failed++;
  unlock();
  if (!ok) trace::add("pet: picture %ux%u failed to decode", f->bw, f->bh);
  return ok ? &slots[i].pic : nullptr;
}

void want(const pbs2_frame_t* f, int shift) {
#ifndef SIM
  if (!f || !worker) return;
  lock();
  if (job >= 0 || find(f, shift) >= 0) return unlock();
  const int i = victim();
  if (i < 0) return unlock();
  slots[i].f = f, slots[i].shift = shift, slots[i].state = 1;
  job = i;
  unlock();
  xTaskNotifyGive(worker);
#else
  (void)f, (void)shift;
#endif
}

void release() {
#ifndef SIM
  if (worker) {
    for (int k = 0; k < 200 && job >= 0; k++) vTaskDelay(1);  // let a running decode finish
    if (job >= 0) return;
  }
#endif
  lock();
  for (Slot& s : slots) {
    heap_caps_free(s.pic.rgb), heap_caps_free(s.pic.a);
    s = Slot();
  }
  current = -1;
  unlock();
  heap_caps_free(spanBuf);
  spanBuf = nullptr;
  freeCtx(syncCtx());
#ifndef SIM
  freeCtx(bgCtx());
#endif
}

// ------------------------------------------------------------------ drawing
void shadow(Canvas& c, const pbs2_frame_t* f, int ox, int oy, int shift) {
  if (!f->sr) return;
  const float k = 1.0f / (1 << shift);
  const float R = f->sr * k, sx = f->sx * k, sy = f->sy * k, ry = 0.34f * R;
  static const uint16_t SH = rgb565(40, 44, 60);
  for (int y = (int)floorf(sy - ry); y <= (int)ceilf(sy + ry); y++) {
    const int cy = oy + y;
    if (cy < c.y0 || cy >= c.y1 || cy < 0 || cy >= c.h || y >= (STAGE_ROWS >> shift)) continue;
    const float dy = (y + 0.5f - sy) / 0.34f;
    uint16_t* row = c.px + cy * c.stride;
    for (int x = (int)(sx - R); x <= (int)(sx + R); x++) {
      const int cx = ox + x;
      if (cx < 0 || cx >= c.w) continue;
      const float dx = x + 0.5f - sx;
      float t = (sqrtf(dx * dx + dy * dy) / R - 0.2f) / 0.8f;
      t = t < 0 ? 0 : t > 1 ? 1 : t;
      const int a = (int)lroundf(0.30f * (1 - t) * 255);
      if (a) row[cx] = blend(row[cx], SH, a);
    }
  }
}

void draw(Canvas& c, const Pic& p, int ox, int oy) {
  if (!p.f || !p.w || !p.rgb) return;
  const int rows = STAGE_ROWS >> p.shift;  // the 12 canvas rows under the stage never show
  for (int y = 0; y < p.h && p.y + y < rows; y++) {
    const int cy = oy + p.y + y;
    if (cy < c.y0 || cy >= c.y1 || cy < 0 || cy >= c.h) continue;
    const uint16_t* s = p.rgb + y * p.w;
    const uint8_t* a = p.a + y * p.w;
    uint16_t* d = c.px + cy * c.stride + ox + p.x;
    const int x0 = max(0, -(ox + p.x)), x1 = min(p.w, c.w - (ox + p.x));
    for (int x = x0; x < x1; x++) {
      const int al = a[x];
      if (al == 255) d[x] = s[x];
      else if (al) d[x] = blend(d[x], s[x], al);
    }
  }
}

void extent(const pbs2_frame_t* f, int& y0, int& y1) {
  y0 = 1 << 30, y1 = -1;
  if (f->bw) y0 = f->by, y1 = f->by + f->bh;
  if (f->sr) {
    const int ry = (int)ceilf(0.34f * f->sr) + 1;
    y0 = min(y0, f->sy - ry), y1 = max(y1, f->sy + ry + 1);
  }
  y0 = max(0, y0), y1 = min(STAGE_ROWS, y1);
}

void span(const pbs2_clip_t* c, int i0, int n, int& top, int& bot) {
  top = 1 << 30, bot = 0;
  for (int k = 0; k < n; k++) {
    int i = i0 + k;
    if (i >= c->count) {
      if (!c->loop) break;
      i %= c->count;
    }
    const pbs2_frame_t* f = pk::frame(c, i);
    if (!f->bw) continue;
    if (pk::dataMax() && !spanBuf && !(spanBuf = (uint8_t*)psram(pk::dataMax()))) return;
    const uint8_t* r = pk::mask(f, spanBuf);  // in place, or read from the memory card
    if (!r) continue;
    uint32_t at = 0, run = 0;
    long a = -1, b = -1;
    uint8_t v = 0;
    for (uint32_t q = 0; q < f->mask_len; q++) {
      run += r[q];
      if (r[q] == 255) continue;
      if (v && run) {
        if (a < 0) a = at;
        b = at + run - 1;
      }
      at += run, v ^= 1, run = 0;
    }
    if (a >= 0) {
      const int t = f->by + (int)(a / f->bw), u = f->by + (int)(b / f->bw) + 1;
      top = min(top, t), bot = max(bot, u);
    }
  }
}

}  // namespace dog
}  // namespace pet
