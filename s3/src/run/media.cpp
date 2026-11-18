// The run's pictures (media.h). Formats: the game designer's fw/pbs2.h, pbu.h, pbv.h (src/pet/formats.h). JPEG: on the
// board Espressif's decoder (lib/esp_new_jpeg, about 5x faster: the roads at 20 pictures a second and the sprites
// on time), the pet's private TJpgDec (src/pet/tjpgd_pb.c, RGB565 out) when it refuses a picture and in the
// simulator; mask -> soft edge as fw/pb_ref.c.
#include "media.h"

#include <Arduino.h>

#include <algorithm>
#include <cstring>
#include <new>

#include "i18n.h"
#include "pet/tjpgd_pb.h"
#include "trace.h"

#ifdef SIM
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#define FAST_JPEG 0
extern bool simBoard;  // sim/stubs.cpp: the live simulator at the board's speed (sim/live.cpp)
#else
#define FAST_JPEG 1
#include "esp_jpeg_dec.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#endif

namespace run {

#ifdef SIM
uint32_t nowUs() {
  return (uint32_t)std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
#else
uint32_t nowUs() { return micros(); }
#endif

namespace media {
namespace {

#ifdef SIM
// The live simulator's board timing (simBoard): a decoder thread like the board's core 0 task, and every job takes
// as long as it took on the board (ES3C35P, Espressif's decoder, roads20 and the packs on the memory card,
// 2026-11-17): a road picture 19.6 ms of which 3.3 ms reading the card; a sprite 3.5 ms on average, 14k px 6 ms.
constexpr uint32_t B_ROAD_US = 16300, B_READ_US = 3300;
uint32_t boardSpriteUs(uint32_t px) { return 1000 + px * 36 / 100; }
void pad(uint32_t t0, uint32_t us) {
  if (!simBoard) return;
  // The computer's sleep wakes up late now and then: what a job ran over comes off the next ones, so the
  // decoders keep the board's pace on average.
  thread_local int64_t over = 0;
  const int64_t want = std::max<int64_t>(0, (int64_t)us - over), d = nowUs() - t0;
  if (d + 1500 < want) std::this_thread::sleep_for(std::chrono::microseconds(want - d - 1200));
  while ((int64_t)(nowUs() - t0) < want) std::this_thread::yield();
  over = std::max<int64_t>(0, over + (int64_t)(nowUs() - t0) - (int64_t)us);
}
bool threaded() { return simBoard; }
void yield1() { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
#else
inline void pad(uint32_t, uint32_t) {}
inline uint32_t boardSpriteUs(uint32_t) { return 0; }
constexpr uint32_t B_ROAD_US = 0, B_READ_US = 0;
constexpr bool threaded() { return true; }
void yield1() { vTaskDelay(1); }
#endif

void* psram(size_t n) { return heap_caps_malloc(n ? n : 1, MALLOC_CAP_SPIRAM); }
// Decoded pictures: Espressif's decoder writes them 16 bytes aligned.
void* psram16(size_t n) {
#if FAST_JPEG
  return heap_caps_aligned_alloc(16, n ? n : 16, MALLOC_CAP_SPIRAM);
#else
  return psram(n);
#endif
}
inline uint32_t rd32(const uint8_t* p) {
  uint32_t v;
  memcpy(&v, p, 4);
  return v;
}

constexpr size_t POOL = 16 * 1024;        // TJpgDec work area (fast Huffman tables)
constexpr uint32_t MAX_JPEG = 64 * 1024;  // a road picture is about 14 KB
constexpr int ENTRIES = 320;
constexpr uint32_t BUDGET = 3 * 1024 * 1024;  // decoded sprites kept (bytes), less when PSRAM is short
uint32_t budget = BUDGET;
constexpr int SLOTS = 6;                      // road pictures: the one shown, the planned ones
enum : uint8_t { FREE = 0, QUEUED, BUSY, READY };

Stats st;

// ------------------------------------------------------------------ one decoder (scratch per thread)
struct Ctx {
  JDEC jd;
  uint8_t* pool = nullptr;
  bool poolInternal = false;
  uint8_t* mask = nullptr;  // 0/1 per box pixel
  uint8_t* vsum = nullptr;  // a row of vertical 3-sums
  uint8_t* buf = nullptr;   // a road picture read from the card
  uint32_t bufCap = 0;
  uint16_t* mix = nullptr;  // the second picture of a fade
  uint8_t* fbuf = nullptr;  // a sprite as one JPEG: header, scan, EOI (from the card the mask is read after the scan)
  uint32_t fbufCap = 0;
  uint8_t hdr[1024];        // a pack's JPEG header with this box's size
  uint32_t tjpgd = 0;       // pictures TJpgDec decoded, not yet added to the stats (they are updated under the lock)
};

uint32_t maxPx = 0, maxW = 0, maxBytes = 0;  // the packs' largest box, row, scan + mask
constexpr uint32_t HDR_MAX = 1024;            // a pack's JPEG header (checked when it is loaded)

bool ensure(Ctx& c, bool internalPool) {
  const uint32_t fb = HDR_MAX + maxBytes + 2;
  if (c.fbufCap < fb) {
    heap_caps_free(c.fbuf);
    c.fbuf = (uint8_t*)psram(fb);
    c.fbufCap = c.fbuf ? fb : 0;
    if (!c.fbuf) return false;
  }
  // TJpgDec's work area (with Espressif's decoder only for a picture it refuses: then in PSRAM, made then).
  if (!c.pool && internalPool && !FAST_JPEG) {
    c.pool = (uint8_t*)heap_caps_malloc(POOL, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    c.poolInternal = c.pool != nullptr;
  }
  if (!c.pool && !FAST_JPEG) c.pool = (uint8_t*)psram(POOL);
  if (!c.mask) c.mask = (uint8_t*)psram(maxPx);
  if (!c.vsum) c.vsum = (uint8_t*)psram(maxW + 2);
  return (c.pool || FAST_JPEG) && c.mask && c.vsum;
}

void freeCtx(Ctx& c) {
  heap_caps_free(c.pool), heap_caps_free(c.mask), heap_caps_free(c.vsum), heap_caps_free(c.buf), heap_caps_free(c.mix);
  heap_caps_free(c.fbuf);
  c.pool = c.mask = c.vsum = c.buf = c.fbuf = nullptr;
  c.mix = nullptr;
  c.bufCap = c.fbufCap = 0;
  c.poolInternal = false;
}

struct Src {
  const uint8_t* seg[3];
  size_t len[3];
  int k;
  size_t pos;
  uint16_t* out;
  int w, h, stride;
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
  if (cw <= 0) return 1;
  for (int y = r->top; y <= r->bottom; y++, px += w)
    if (y < s->h) memcpy(s->out + y * s->stride + r->left, px, cw * 2);
  return 1;
}

// A JPEG from up to three pieces into out (w x h, stride px a row) with TJpgDec; false unless it is exactly w x h.
bool jpeg(Ctx& c, Src& s) {
  if (!c.pool) c.pool = (uint8_t*)psram(POOL);
  if (!c.pool) return false;
  s.k = 0, s.pos = 0;
  c.tjpgd++;
  if (jd_prepare(&c.jd, input, c.pool, POOL, &s) != JDR_OK) return false;
  if (c.jd.width != s.w || c.jd.height != s.h) return false;
  return jd_decomp(&c.jd, output, 0) == JDR_OK;
}

#if FAST_JPEG
}  // namespace
uint32_t jpegReserve = 96 * 1024;  // media.h: in practice PSRAM while the game runs (1 ms slower a road picture)
namespace {
#endif
#if FAST_JPEG
// Espressif's decoder: a whole JPEG in one piece into out (exactly w x h RGB565, 16-byte aligned, no stride).
// false: refused (the caller tries TJpgDec).
bool fastJpeg(const uint8_t* in, uint32_t len, uint16_t* out, int w, int h) {
  if ((uintptr_t)out & 15) return false;
  jpeg_dec_config_t cfg = DEFAULT_JPEG_DEC_CONFIG();
  cfg.output_type = JPEG_PIXEL_FORMAT_RGB565_LE;
  jpeg_dec_handle_t d = nullptr;
  if (jpeg_dec_open(&cfg, &d) != JPEG_ERR_OK || !d) return false;
  jpeg_dec_io_t io = {};
  io.inbuf = (uint8_t*)in, io.inbuf_len = (int)len;
  jpeg_dec_header_info_t hi = {};
  int n = 0;
  bool ok = jpeg_dec_parse_header(d, &io, &hi) == JPEG_ERR_OK && hi.width == w && hi.height == h &&
            jpeg_dec_get_outbuf_len(d, &n) == JPEG_ERR_OK && n == w * h * 2;
  if (ok) {
    io.outbuf = (uint8_t*)out;
    ok = jpeg_dec_process(d, &io) == JPEG_ERR_OK;
  }
  jpeg_dec_close(d);
  if (!ok) {
    st.fastFailed++;
    static bool told = false;
    if (!told) told = true, trace::add("run: fast JPEG decoder refused %dx%d (%dx%d, out %d B)", w, h, hi.width, hi.height, n);
  }
  return ok;
}
#endif

// The 1-bit mask of a box: run lengths, transparent first, alternating; a byte 255 = the run goes on.
bool unmask(const pbs2_frame_t* f, const uint8_t* r, uint8_t* m) {
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

// Soft edge: alpha = the mask's 3x3 mean (0..9 set -> 0..255), as fw/pb_ref.c.
void soften(Ctx& c, int w, int h, uint8_t* a) {
  static const uint8_t LUT[10] = {0, 28, 57, 85, 113, 142, 170, 198, 227, 255};  // (s * 255 + 4) / 9
  const uint8_t* m = c.mask;
  for (int y = 0; y < h; y++) {
    const uint8_t* up = y ? m + (y - 1) * w : nullptr;
    const uint8_t* mid = m + y * w;
    const uint8_t* dn = y + 1 < h ? m + (y + 1) * w : nullptr;
    for (int x = 0; x < w; x++) c.vsum[x] = mid[x] + (up ? up[x] : 0) + (dn ? dn[x] : 0);
    uint8_t* o = a + y * w;
    for (int x = 0; x < w; x++) o[x] = LUT[c.vsum[x] + (x ? c.vsum[x - 1] : 0) + (x + 1 < w ? c.vsum[x + 1] : 0)];
  }
}

bool decodeSprite(Ctx& c, const Pack& p, const pbs2_frame_t* f, uint16_t* rgb, uint8_t* a) {
  const pbs2_head_t* h = p.head();
  const uint32_t hl = h->hdr_len;
  if (!f->bw || hl > sizeof c.hdr || !c.fbuf || hl + f->scan_len + f->mask_len + 2 > c.fbufCap) return false;
  memcpy(c.hdr, p.jpegHeader(), hl);
  c.hdr[h->dims_off] = f->bh >> 8, c.hdr[h->dims_off + 1] = f->bh & 255;
  c.hdr[h->dims_off + 2] = f->bw >> 8, c.hdr[h->dims_off + 3] = f->bw & 255;
  static const uint8_t EOI[2] = {0xFF, 0xD9};
  const uint8_t* b = p.bytes(f, c.fbuf + hl);  // from the card: after the header in this decoder's buffer
  if (!b || !unmask(f, b + f->scan_len, c.mask)) return false;
#if FAST_JPEG
  // One piece for Espressif's decoder: header, scan, EOI (over the mask's first bytes, unpacked above).
  uint8_t* j = c.fbuf;
  memcpy(j, c.hdr, hl);
  if (b != j + hl) memcpy(j + hl, b, f->scan_len);
  j[hl + f->scan_len] = 0xFF, j[hl + f->scan_len + 1] = 0xD9;
  bool ok = fastJpeg(j, hl + f->scan_len + 2, rgb, f->bw, f->bh);
  if (!ok) {
    Src s = {{c.hdr, j + hl, EOI}, {hl, f->scan_len, 2}, 0, 0, rgb, f->bw, f->bh, f->bw};
    ok = jpeg(c, s);
  }
  if (!ok) return false;
#else
  Src s = {{c.hdr, b, EOI}, {hl, f->scan_len, 2}, 0, 0, rgb, f->bw, f->bh, f->bw};
  if (!jpeg(c, s)) return false;
#endif
  soften(c, f->bw, f->bh, a);
  return true;
}

// ------------------------------------------------------------------ sprite cache, road slots, jobs
struct Entry {
  const Pack* pack = nullptr;
  const pbs2_frame_t* f = nullptr;
  uint8_t* mem = nullptr;  // colours (w*h*2), then alpha (w*h)
  uint32_t bytes = 0, used = 0, seq = 0;
  uint8_t state = FREE;
  bool pinned = false;  // never dropped
  Sprite spr;
};
Entry* entries = nullptr;  // ENTRIES, PSRAM
uint32_t cacheBytes = 0, clock = 1, seqNo = 0, urgentNo = 0;
// Her run loop, kept for good, is not counted in the budget (it took 450 of the 618 KB from a memory card: the things'
// pictures near her stood on older ones).
uint32_t pinnedBytes = 0;
constexpr uint32_t AHEAD_SLACK = 64 * 1024;  // a picture decoded ahead may take the cache this far past its budget
constexpr uint32_t LATER = 1u << 30;  // wishes in order: the urgent ones (urgentNo) before the rest (LATER + seqNo)

struct Slot {
  int key = -1;
  uint16_t* px = nullptr;
  uint8_t state = FREE;
  int prio = 99;
};
Slot slots[SLOTS];
int shownSlot = -1;
Road* roadTab = nullptr;
int roadN = 0;

void (*postFn)(void*) = nullptr;
void* postArg = nullptr;
volatile bool postBusy = false, started = false;

// Decoders: 0 = the task on core 0 (priority 2, like the pet's), 1 = a helper on core 1 at the idle priority (it
// only runs while the UI thread waits: the LCD's DMA and tear sync, the loop's delay; sprites only, no card reads).
constexpr int WORKERS = 2;
Ctx* ctxOf[WORKERS] = {};
Ctx*& bgCtx = ctxOf[0];
Ctx* syncCtx = nullptr;  // the LVGL thread's
volatile bool helperOn = false;  // measured on the board: it slows core 0's road decoding (shared cache)

#ifndef SIM
SemaphoreHandle_t mtx = nullptr;
TaskHandle_t tasks[WORKERS] = {};
volatile bool quit = false, exited[WORKERS] = {};
void lock() { xSemaphoreTake(mtx, portMAX_DELAY); }
void unlock() { xSemaphoreGive(mtx); }
void wake() {
  for (TaskHandle_t t : tasks)
    if (t) xTaskNotifyGive(t);
}
#else
std::mutex simMtx, simWakeMtx;
std::condition_variable simCv;
std::thread simThread;
std::atomic<bool> simQuit{false};
bool simWake = false;
void lock() {
  if (simBoard) simMtx.lock();
}
void unlock() {
  if (simBoard) simMtx.unlock();
}
void pump();
void wake() {
  if (!simBoard) return pump();
  {
    std::lock_guard<std::mutex> g(simWakeMtx);
    simWake = true;
  }
  simCv.notify_one();
}
#endif

Ctx* newCtx() {
  void* m = psram(sizeof(Ctx));
  return m ? new (m) Ctx() : nullptr;
}
void deleteCtx(Ctx*& c) {
  if (!c) return;
  freeCtx(*c);
  c->~Ctx();
  heap_caps_free(c);
  c = nullptr;
}

int findEntry(const pbs2_frame_t* f) {
  for (int i = 0; i < ENTRIES; i++)
    if (entries[i].state != FREE && entries[i].f == f) return i;
  return -1;
}

void dropEntry(Entry& e) {
  if (e.state == READY || e.state == FREE) {
    cacheBytes -= e.bytes;
    heap_caps_free(e.mem);
    e = Entry();
  }
}

// A picture drawn or wished in this picture or the one before stays: the one before is what stands in for a thing
// whose new picture is not decoded yet (dropped, the thing vanished as it went past her).
inline bool evictable(const Entry& e) { return e.state == READY && !e.pinned && e.used + 1 < clock; }

// PSRAM itself, past the budget's sums: what is free must still hold this picture, the ones being decoded now (counted
// in cacheBytes, not allocated yet) and PS_FLOOR for the JPEG decoder's own memory and the OS. The budget is set when
// the game opens and her run loop, kept for good, comes on top of it: with less PSRAM (the 2.8" board in QEMU, its
// frame buffer in PSRAM) nothing was left, the decoder failed for want of memory at every picture and things vanished
// or stood on far, small pictures as they came near.
constexpr uint32_t PS_FLOOR = 192 * 1024;
bool psramFor(uint32_t need) {
  uint32_t busy = 0;
  for (int i = 0; i < ENTRIES; i++)
    if (entries[i].state == BUSY && !entries[i].mem) busy += entries[i].bytes;
  return heap_caps_get_free_size(MALLOC_CAP_SPIRAM) >= need + busy + PS_FLOOR;
}

// Room for `need` more bytes: the least recently used ready pictures go (never the ones of the last two pictures).
void makeRoom(uint32_t need) {
  while (cacheBytes - pinnedBytes + need > budget || !psramFor(need)) {
    int v = -1;
    for (int i = 0; i < ENTRIES; i++) {
      const Entry& e = entries[i];
      if (evictable(e) && (v < 0 || e.used < entries[v].used)) v = i;
    }
    if (v < 0) return;
    dropEntry(entries[v]);
    st.evicted++;
  }
}

// A picture of `need` bytes may be decoded now (after makeRoom).
bool fits(uint32_t need) { return cacheBytes - pinnedBytes + need <= budget + AHEAD_SLACK && psramFor(need); }

int freeEntry() {
  for (int i = 0; i < ENTRIES; i++)
    if (entries[i].state == FREE) return i;
  int v = -1;  // all taken: the least recently used ready one
  for (int i = 0; i < ENTRIES; i++)
    if (evictable(entries[i]) && (v < 0 || entries[i].used < entries[v].used))
      v = i;
  if (v >= 0) dropEntry(entries[v]), st.evicted++;
  return v;
}

// Decodes entry i (state BUSY, set by the caller under the lock). Called without the lock.
bool runSprite(Ctx& c, int i) {
  Entry& e = entries[i];
  const pbs2_frame_t* f = e.f;
  const uint32_t px = (uint32_t)f->bw * f->bh;
  const uint32_t t0 = nowUs();
  uint8_t* mem = (uint8_t*)psram16(px * 3);
  const bool ok = mem && decodeSprite(c, *e.pack, f, (uint16_t*)mem, mem + px * 2);
  pad(t0, boardSpriteUs(px));
  lock();
  st.tjpgd += c.tjpgd, c.tjpgd = 0;
  if (ok) {
    e.mem = mem, e.bytes = px * 3, e.state = READY;
    if (e.pinned) pinnedBytes += e.bytes;
    st.cacheMaxKB = max(st.cacheMaxKB, cacheBytes / 1024);
    e.spr = {f, f->bw, f->bh, (const uint16_t*)mem, mem + px * 2};
  } else {
    heap_caps_free(mem);
    cacheBytes -= e.bytes;
    e = Entry();
    st.sprFailed++;
  }
  unlock();
  return ok;
}

bool roadPicture(Ctx& c, int fi, int pic, uint16_t* out);

bool mappedKey(int key) {  // the road picture(s) of a key lie in the internal flash
  auto mapped = [](int fi) { return fi >= 0 && fi < roadN && roadTab[fi].file.data(); };
  return key & FADE ? mapped(key >> 26 & 15) && mapped(key >> 14 & 15) : mapped(key >> 12);
}

// Decodes a road picture into slot s (state BUSY): one picture, or a fade's two mixed. Called without the lock.
bool runRoad(Ctx& c, int s) {
  const int key = slots[s].key;
  if (!(key & FADE)) return roadPicture(c, key >> 12, key & 4095, slots[s].px);
  if (!c.mix) c.mix = (uint16_t*)psram16(ROAD_W * ROAD_H * 2);
  if (!c.mix || !roadPicture(c, key >> 26 & 15, key >> 18 & 255, slots[s].px) ||
      !roadPicture(c, key >> 14 & 15, key >> 6 & 255, c.mix))
    return false;
  const int b = (key & 63) * 255 / 63, a = 255 - b;
  uint16_t* d = slots[s].px;
  const uint16_t* m = c.mix;
  for (int i = 0; i < ROAD_W * ROAD_H; i++) {
    const uint16_t p = d[i], q = m[i];
    const int r = ((p >> 11) * a + (q >> 11) * b + 127) / 255, gg = ((p >> 5 & 63) * a + (q >> 5 & 63) * b + 127) / 255,
              bl = ((p & 31) * a + (q & 31) * b + 127) / 255;
    d[i] = (uint16_t)(r << 11 | gg << 5 | bl);
  }
  return true;
}

// Picture pic of road file fi into out (232x240).
bool roadPicture(Ctx& c, int fi, int pic, uint16_t* out) {
  if (fi < 0 || fi >= roadN || pic >= roadTab[fi].n) return false;
  Road& r = roadTab[fi];
  const uint32_t a = r.off[pic], len = r.off[pic + 1] - a;
  const uint8_t* src = r.file.data() ? r.file.data() + a : nullptr;
  if (!src) {
    if (c.bufCap < len) {
      heap_caps_free(c.buf);
      c.buf = (uint8_t*)psram(MAX_JPEG);
      c.bufCap = c.buf ? MAX_JPEG : 0;
    }
    if (c.bufCap < len) return false;
    const uint32_t t0 = nowUs();
    if (!r.file.read(a, c.buf, len)) return false;
    pad(t0, B_READ_US);
    const uint32_t us = nowUs() - t0;
    st.readN++, st.readUs += us, st.readMaxUs = max(st.readMaxUs, us);
    src = c.buf;
  }
#if FAST_JPEG
  if (fastJpeg(src, len, out, ROAD_W, ROAD_H)) return true;
#endif
  const uint32_t t1 = nowUs();
  Src j = {{src, nullptr, nullptr}, {len, 0, 0}, 0, 0, out, ROAD_W, ROAD_H, ROAD_W};
  const bool ok = jpeg(c, j);
  pad(t1, B_ROAD_US);
  return ok;
}

// One job for a decoder: the next road picture when it is needed soon, then the sprites wanted for the next
// picture, then the rest of the road plan. who: 1 = the core 0 task, 2 = the core 1 helper. false: nothing to do.
bool runOne(Ctx& c, int who) {
  lock();
#ifndef SIM
  if (quit) return unlock(), false;
#else
  if (simQuit) return unlock(), false;
#endif
  if (who == 1 && postFn && !postBusy) {
    void (*fn)(void*) = postFn;
    void* arg = postArg;
    postBusy = true;
    unlock();
    fn(arg);
    lock();
    postFn = nullptr;
    postBusy = false;
    unlock();
    return true;
  }
  if (!started) return unlock(), false;
  int road = -1, spr = -1;
  for (int s = 0; s < SLOTS; s++)
    if (slots[s].state == QUEUED && (road < 0 || slots[s].prio < slots[road].prio)) road = s;
  for (int i = 0; i < ENTRIES; i++)
    if (entries[i].state == QUEUED && (spr < 0 || entries[i].seq < entries[spr].seq)) spr = i;
  // No room for it (the cache holds only pictures drawn or wished this picture): it waits for the next picture,
  // when the ones drawn now may go. The cache stays within its budget (PSRAM: what is free beyond it is the OS's).
  if (spr >= 0) {
    const uint32_t need = (uint32_t)entries[spr].f->bw * entries[spr].f->bh * 3;
    makeRoom(need);
    if (!fits(need)) spr = -1, st.sprHeld++;
  }
  // The helper (core 1, lowest priority: often stopped by the UI) takes road pictures further ahead only, never
  // the next two (a picture it holds is held up), and never from the card (its stack is in PSRAM).
  if (who == 2) {
    road = -1;
    for (int s = 0; s < SLOTS; s++)
      if (slots[s].state == QUEUED && slots[s].prio >= 2 && mappedKey(slots[s].key) &&
          (road < 0 || slots[s].prio < slots[road].prio))
        road = s;
  }
  if (road >= 0 && (slots[road].prio <= 1 || spr < 0 || who == 2)) {
    slots[road].state = BUSY;
    unlock();
    const uint32_t t0 = nowUs();
    const bool ok = runRoad(c, road);
    const uint32_t us = nowUs() - t0;
    lock();
    st.tjpgd += c.tjpgd, c.tjpgd = 0;
    slots[road].state = ok ? READY : FREE;
    if (!ok) slots[road].key = -1, st.roadFailed++;
    st.roadDec++, st.roadUs += us, st.roadMaxUs = max(st.roadMaxUs, us);
    unlock();
    return true;
  }
  if (spr >= 0) {
    Entry& e = entries[spr];
    const uint32_t need = (uint32_t)e.f->bw * e.f->bh * 3;
    cacheBytes += need, e.bytes = need;
    e.state = BUSY;
    unlock();
    const uint32_t t0 = nowUs();
    runSprite(c, spr);
    const uint32_t us = nowUs() - t0;
    lock();
    st.sprAhead++;
    if (who == 2) st.sprHelper++;
    st.sprUs += us, st.sprMaxUs = max(st.sprMaxUs, us);
    unlock();
    return true;
  }
  unlock();
  return false;
}

#ifndef SIM
void workerTask(void* arg) {
  const int w = (int)(intptr_t)arg;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    uint32_t since = millis();
    while (!quit && (w == 0 || helperOn) && ctxOf[w] && runOne(*ctxOf[w], w + 1)) {
      // Behind (e.g. roads20 faster than the chip decodes): still let core 0's idle task run (task watchdog).
      if (w == 0 && millis() - since > 100) vTaskDelay(1), since = millis();
    }
    if (quit) {
      exited[w] = true;
      vTaskSuspend(nullptr);
    }
  }
}

bool startTask(int w) {
  if (tasks[w]) return true;
  exited[w] = false;
  const char* name = w ? "rundec1" : "rundec";
  const int core = w ? 1 : 0, prio = w ? 0 : 2;
  BaseType_t r = pdFAIL;
  // The core 0 task reads the card: its stack in internal RAM when there is room; the helper's in PSRAM.
  if (!w) r = xTaskCreatePinnedToCoreWithCaps(workerTask, name, 8192, (void*)(intptr_t)w, prio, &tasks[w], core,
                                              MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (r != pdPASS)
    r = xTaskCreatePinnedToCoreWithCaps(workerTask, name, w ? 6144 : 8192, (void*)(intptr_t)w, prio, &tasks[w], core,
                                        MALLOC_CAP_SPIRAM);
  if (r != pdPASS) {
    tasks[w] = nullptr;
    trace::add("run: no decode task %d", w);
    return false;
  }
  return true;
}
#else
void pump() {  // the simulator: the "background" work happens now, in the same order
  while (runOne(*bgCtx, 1)) {
  }
}
void simWorker() {  // the live simulator at the board's speed: the core 0 task
  for (;;) {
    {
      std::unique_lock<std::mutex> g(simWakeMtx);
      simCv.wait(g, [] { return simWake || simQuit; });
      if (simQuit) return;
      simWake = false;
    }
    while (!simQuit && runOne(*bgCtx, 1)) {
    }
  }
}
#endif

}  // namespace

Stats& stats() { return st; }
Stats snapshot() {
  lock();
  const Stats s = st;
  unlock();
  return s;
}
bool fast() {
#ifdef SIM
  return simBoard;
#else
  return true;
#endif
}

// ------------------------------------------------------------------ packs
const pbs2_clip_t* Pack::clipAt(int i) const {
  return (const pbs2_clip_t*)(jpegHeader() + head()->hdr_len) + i;
}

const pbs2_clip_t* Pack::clip(const char* name) const {
  if (!data || !name) return nullptr;
  for (int i = 0; i < clipCount(); i++)
    if (!strncmp(clipAt(i)->name, name, 16)) return clipAt(i);
  return nullptr;
}

const pbs2_frame_t* Pack::frame(const pbs2_clip_t* c, int i) const {
  if (!c || !c->count) return nullptr;
  if (i < 0) i = 0;
  if (i >= c->count) i = c->loop ? i % c->count : c->count - 1;
  if (recs) return &recs[c->first + i];
  const uint8_t* offs = (const uint8_t*)clipAt(clipCount());
  return (const pbs2_frame_t*)(data + rd32(offs + 4 * (c->first + i)));
}

const uint8_t* Pack::bytes(const pbs2_frame_t* f, uint8_t* buf) const {
  if (!recs) return (const uint8_t*)(f + 1);
  const uint32_t k = f - recs, n = f->scan_len + f->mask_len;
  if (!buf || k >= head()->nframes || n > maxBytes) return nullptr;
  const uint8_t* offs = (const uint8_t*)clipAt(clipCount());
  return file.read(rd32(offs + 4 * k) + sizeof(pbs2_frame_t), buf, n) ? buf : nullptr;
}

bool Pack::load(const char* id, const char* name, String& err) {
  free();
  pkg::File f;
  if (!pkg::open(id, name, f)) return err = i18n::fmt(N_("missing {0}"), name), false;
  size = f.size();
  const char* why = nullptr;
  pbs2_head_t hd;
  uint64_t offsAt = 0, end = 0;
  if (size < sizeof hd || !f.read(0, &hd, sizeof hd)) why = "header read";
  else {
    offsAt = sizeof hd + (uint64_t)hd.hdr_len + 24ull * hd.nclips, end = offsAt + 4ull * (hd.nframes + 1);
    if (memcmp(hd.magic, "PBS2", 4) || hd.hdr_len > 1024 || hd.dims_off + 4u > hd.hdr_len || end > size ||
        !hd.nframes)
      why = "header";
  }
  if (!why) {
    owned = !f.data();  // in the internal flash: read in place, no copy
    if (!owned) data = (uint8_t*)f.data();
    else if (!(data = (uint8_t*)psram(end)) || !(recs = (pbs2_frame_t*)psram(sizeof(pbs2_frame_t) * hd.nframes)))
      why = "no PSRAM (table)";
    else if (!f.read(0, data, end)) why = "table read";
    else file = f;
  }
  // The frames' records from the card, through a small window (they are spread over the file).
  if (!why && recs) {
    constexpr uint32_t WIN = 4096;
    uint8_t* win = (uint8_t*)heap_caps_malloc(WIN, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!win) win = (uint8_t*)psram(WIN);
    uint32_t ws = 0, wl = 0;
    if (!win) why = "no memory";
    for (uint32_t i = 0; !why && i < hd.nframes; i++) {
      const uint32_t o = rd32(data + offsAt + 4 * i);
      if ((uint64_t)o + sizeof(pbs2_frame_t) > size) why = "frame past the end";
      else if (o < ws || o + sizeof(pbs2_frame_t) > ws + wl) {
        ws = o, wl = min(WIN, size - o);
        if (!f.read(ws, win, wl)) why = "frame read";
      }
      if (!why) memcpy(&recs[i], win + (o - ws), sizeof(pbs2_frame_t));
    }
    heap_caps_free(win);
  }
  const pbs2_head_t* h = why ? nullptr : head();
  for (int i = 0; !why && i < h->nclips; i++) {
    const pbs2_clip_t* c = clipAt(i);
    if ((uint64_t)c->first + c->count > h->nframes) why = "clip";
  }
  maxPx = maxW = maxBytes = 0;
  for (uint32_t i = 0; !why && i < h->nframes; i++) {
    const uint32_t o = rd32(data + offsAt + 4 * i);
    if (!recs && (uint64_t)o + sizeof(pbs2_frame_t) > size) {
      why = "frame past the end";
      break;
    }
    const pbs2_frame_t* fr = recs ? &recs[i] : (const pbs2_frame_t*)(data + o);
    if ((uint64_t)o + sizeof(pbs2_frame_t) + fr->scan_len + fr->mask_len > size ||
        (uint32_t)fr->bw * fr->bh > (uint32_t)h->w * h->h || fr->bw > 1024)
      why = "frame";
    maxPx = max(maxPx, (uint32_t)fr->bw * fr->bh), maxW = max(maxW, (uint32_t)fr->bw);
    maxBytes = max(maxBytes, (uint32_t)fr->scan_len + fr->mask_len);
  }
  if (why) {
    trace::add("run: %s: %s (%u B; PSRAM free %u KB, largest %u KB)", name, why, (unsigned)size,
               (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) >> 10),
               (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) >> 10));
    free();
    return err = i18n::fmt(N_("broken {0}"), name), false;
  }
  maxBytes = max(maxBytes, (uint32_t)1);  // the decoders' buffer (a sprite as one JPEG; from the card its mask too)
  return true;
}

void Pack::free() {
  if (owned) heap_caps_free(data);
  heap_caps_free(recs);
  data = nullptr;
  recs = nullptr;
  file = pkg::File();
  owned = false;
  size = maxPx = maxW = maxBytes = 0;
}

const pbu_piece_t* Ui::piece(const char* name) const {
  if (!data) return nullptr;
  const pbu_head_t* h = (const pbu_head_t*)data;
  const pbu_piece_t* p = (const pbu_piece_t*)(h + 1);
  for (int i = 0; i < h->count; i++)
    if (!strncmp(p[i].name, name, 24)) return &p[i];
  return nullptr;
}

bool Ui::load(const char* id, const char* name, String& err) {
  free();
  pkg::File f;
  if (!pkg::open(id, name, f)) return err = i18n::fmt(N_("missing {0}"), name), false;
  size = f.size();
  // The header and the table: in place in the internal flash; on the card only they are read into PSRAM (the
  // pieces go straight to px below, so the file is never held twice).
  pbu_head_t hd;
  const char* why = nullptr;
  if (size < sizeof hd || !f.read(0, &hd, sizeof hd)) why = "header read";
  else if (memcmp(hd.magic, "PBU1", 4) || sizeof hd + (uint64_t)hd.count * sizeof(pbu_piece_t) > size) why = "header";
  const uint32_t tab = why ? 0 : sizeof hd + hd.count * sizeof(pbu_piece_t);
  if (!why) {
    owned = !f.data();
    data = owned ? (uint8_t*)psram(tab) : (uint8_t*)f.data();
    if (!data) why = "no PSRAM (table)";
    else if (owned && !f.read(0, data, tab)) why = "table read";
  }
  const pbu_piece_t* p = why ? nullptr : (const pbu_piece_t*)(data + sizeof hd);
  uint32_t total = 0;
  auto lazyOne = [&](int i) { return !strncmp(p[i].name, "menu_", 5); };  // a start card: when it is shown
  for (int i = 0; !why && i < hd.count; i++) {
    const uint32_t n = (uint32_t)p[i].w * p[i].h * (p[i].fmt ? 3 : 2);
    if ((uint64_t)p[i].off + n > size) why = "piece past the end";
    if (!lazyOne(i)) total += (n + 3) & ~3u;
  }
  // Every piece aligned in PSRAM: the screen reads them each picture with 16-bit loads (most sit at odd offsets
  // in the file, and the flash is slower than PSRAM).
  if (!why) {
    px = (uint8_t*)psram(total);
    at = (uint32_t*)psram(sizeof(uint32_t) * (hd.count + 1));
    if (!px || !at) why = "no PSRAM";
    uint32_t o = 0;
    for (int i = 0; !why && i < hd.count; i++) {
      const uint32_t n = (uint32_t)p[i].w * p[i].h * (p[i].fmt ? 3 : 2);
      if (lazyOne(i)) {
        at[i] = LAZY;
        continue;
      }
      if (owned) {
        if (!f.read(p[i].off, px + o, n)) why = "piece read";
      } else {
        memcpy(px + o, data + p[i].off, n);
      }
      at[i] = o;
      o += (n + 3) & ~3u;
    }
  }
  if (why) {
    trace::add("run: %s: %s (%u B; PSRAM free %u KB, largest %u KB)", name, why, (unsigned)size,
               (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) >> 10),
               (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) >> 10));
    free();
    return err = i18n::fmt(N_("broken {0}"), name), false;
  }
  file = f;
  return true;
}

const uint8_t* Ui::lazy(int i) const {
  if (oneIdx == i) return one;
  heap_caps_free(one);
  one = nullptr, oneIdx = -1;
  const pbu_piece_t* p = (const pbu_piece_t*)(data + sizeof(pbu_head_t)) + i;
  const uint32_t n = (uint32_t)p->w * p->h * (p->fmt ? 3 : 2);
  uint8_t* b = (uint8_t*)psram(n);
  if (!b) return nullptr;
  if (file.data()) memcpy(b, file.data() + p->off, n);
  else if (!file.read(p->off, b, n)) return heap_caps_free(b), nullptr;
  one = b, oneIdx = i;
  return one;
}

void Ui::free() {
  heap_caps_free(one);
  one = nullptr, oneIdx = -1;
  file = pkg::File();
  if (owned) heap_caps_free(data);
  heap_caps_free(px), heap_caps_free(at);
  data = px = nullptr;
  at = nullptr;
  owned = false;
  size = 0;
}

bool Road::open(const char* id, const char* name) {
  free();
  if (!pkg::open(id, name, file)) return false;
  pbv_head_t hd;
  if (file.size() < sizeof hd || !file.read(0, &hd, sizeof hd) || memcmp(hd.magic, "PBV1", 4) || !hd.nframes ||
      hd.nframes >= 4096 || hd.w != ROAD_W || hd.h != ROAD_H) {
    free();
    return false;
  }
  const uint32_t at = sizeof hd + 24u * hd.nclips, bytes = 4 * (hd.nframes + 1);
  off = (uint32_t*)psram(bytes);
  if (!off || (uint64_t)at + bytes > file.size() || !file.read(at, off, bytes)) {
    free();
    return false;
  }
  for (uint32_t i = 0; i < hd.nframes; i++)
    if (off[i + 1] <= off[i] || off[i + 1] - off[i] > MAX_JPEG || off[i + 1] > file.size()) {
      free();
      return false;
    }
  w = hd.w, h = hd.h, n = hd.nframes;
  return true;
}

void Road::free() {
  heap_caps_free(off);
  off = nullptr;
  w = h = n = 0;
  file = pkg::File();
}

// ------------------------------------------------------------------ the decoder
bool begin(const Pack* const* packs, int npacks, bool cardRoads) {
  maxPx = maxW = maxBytes = 0;
  for (int i = 0; i < npacks; i++)
    if (packs[i])
      maxPx = max(maxPx, packs[i]->maxPx), maxW = max(maxW, packs[i]->maxW), maxBytes = max(maxBytes, packs[i]->maxBytes);
  if (!entries) {
    entries = (Entry*)psram(sizeof(Entry) * ENTRIES);
    if (!entries) return false;
    for (int i = 0; i < ENTRIES; i++) new (&entries[i]) Entry();
  }
  for (Slot& s : slots) {
    if (!s.px) s.px = (uint16_t*)psram16(ROAD_W * ROAD_H * 2);
    if (!s.px) return false;
    s.key = -1, s.state = FREE, s.prio = 99;
  }
  if (!bgCtx) bgCtx = newCtx();
  if (!syncCtx) syncCtx = newCtx();
  if (!bgCtx || !syncCtx || !ensure(*syncCtx, false) || !ensure(*bgCtx, true)) return false;
  // Roads on a memory card: their read buffers now, before the sprite cache takes what is left.
  for (Ctx* c : {bgCtx, syncCtx})
    if (cardRoads && c->bufCap < MAX_JPEG) {
      heap_caps_free(c->buf);
      c->buf = (uint8_t*)psram(MAX_JPEG);
      c->bufCap = c->buf ? MAX_JPEG : 0;
      if (!c->buf) return false;
    }
  helper(helperOn);
  if (threaded()) {  // the board, and the live simulator at its speed (its PSRAM as the board's: sim/stubs.cpp)
  // What PSRAM is left after the packs, the screen and the slots, minus 1 MB for the rest of the OS.
  const uint32_t freePs = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  // Short (the packs from a memory card are in PSRAM too): never more than what is free minus 256 KB, at least
  // 128 KB (more pictures are then decoded as they are needed).
  // 1 MB stays the OS's; with less than 1.5 MB free, two thirds of it (the same at 1.5 MB, and never a bigger cache
  // for less PSRAM: before, 1.4 MB free gave 1149 KB and 1.6 MB 618 KB). psramFor() checks PSRAM at every decode too.
  const uint32_t reserve = std::min<uint32_t>(1u << 20, std::max<uint32_t>(256u << 10, freePs / 3 * 2));
  budget = std::min<uint32_t>(BUDGET, std::max<uint32_t>(128u << 10, freePs > reserve ? freePs - reserve : 0));
  trace::add("run: PSRAM free %u KB", (unsigned)(freePs / 1024));
  }
  bool card = false;
  for (int i = 0; i < npacks; i++) card |= packs[i] && packs[i]->recs;
  trace::add("run: JPEG %s, boxes up to %u px, sprite cache %u KB, packs %s",
             FAST_JPEG ? "Espressif" : bgCtx->poolInternal ? "TJpgDec, internal RAM" : "TJpgDec, PSRAM", (unsigned)maxPx,
             (unsigned)(budget / 1024), card ? "read from the card" : "in place");
  started = true;
  return true;
}

// The worker exists from the first post() (loading the packs) to end().
static bool startWorker() {
#ifndef SIM
  if (tasks[0]) return true;
  if (!mtx) mtx = xSemaphoreCreateMutex();
  if (!bgCtx) bgCtx = newCtx();
  if (!mtx || !bgCtx) return false;
  quit = false;
  return startTask(0);
#else
  if (!bgCtx) bgCtx = newCtx();
  if (bgCtx && simBoard && !simThread.joinable()) simQuit = false, simThread = std::thread(simWorker);
  return bgCtx != nullptr;
#endif
}

void post(void (*fn)(void*), void* arg) {
  if (!startWorker()) return fn(arg);
  lock();
  postFn = fn;
  postArg = arg;
  postBusy = false;
  unlock();
  wake();
}

bool posted() {
  lock();
  const bool done = !postFn;
  unlock();
  return done;
}

void end() {
#ifndef SIM
  if (mtx) {
    lock();
    quit = true;
    unlock();
    wake();
    // A job in progress finishes first (a picture: tens of ms; loading the packs from a card: a few seconds).
    for (int w = 0; w < WORKERS; w++) {
      for (int k = 0; k < 2000 && tasks[w] && !exited[w]; k++) vTaskDelay(pdMS_TO_TICKS(5));
      if (tasks[w] && !exited[w]) {
        trace::add("run: decode task %d did not stop, its buffers are kept", w);
        return;
      }
    }
    for (TaskHandle_t& t : tasks)
      if (t) vTaskDeleteWithCaps(t), t = nullptr;
  }
#else
  if (simThread.joinable()) {  // a job in progress finishes first
    simQuit = true;
    {
      std::lock_guard<std::mutex> g(simWakeMtx);
      simWake = true;
    }
    simCv.notify_one();
    simThread.join();
  }
#endif
  started = false;
  postFn = nullptr;
  postBusy = false;
  if (entries) {
    for (int i = 0; i < ENTRIES; i++) heap_caps_free(entries[i].mem);
    heap_caps_free(entries);
    entries = nullptr;
  }
  cacheBytes = 0, pinnedBytes = 0;
  for (Slot& s : slots) {
    heap_caps_free(s.px);
    s = Slot();
  }
  shownSlot = -1;
  roadTab = nullptr, roadN = 0;
  for (Ctx*& c : ctxOf) deleteCtx(c);
  deleteCtx(syncCtx);
  st = Stats();
}

void helper(bool on) {
  helperOn = on;
#ifndef SIM
  if (on && started && !tasks[1]) {  // made the first time it is switched on
    if (!ctxOf[1]) ctxOf[1] = newCtx();
    if (ctxOf[1] && ensure(*ctxOf[1], false)) startTask(1);
  }
#endif
}

void frame() {
  lock();
  clock++;
  // Wishes nobody took in time are dropped (the things moved on).
  for (int i = 0; i < ENTRIES; i++)
    if (entries[i].state == QUEUED && !entries[i].pinned && entries[i].used + 2 < clock) entries[i] = Entry();
  st.cacheKB = cacheBytes / 1024;
  uint32_t pin = 0;
  for (int i = 0; i < ENTRIES; i++)
    if (entries[i].pinned && entries[i].state == READY) pin += entries[i].bytes;
  st.pinnedKB = pin / 1024;
  unlock();
}

const Sprite* sprite(const Pack& p, const pbs2_frame_t* f, bool decode) {
  if (!f || !f->bw || !entries) return nullptr;
  lock();
  int i = findEntry(f);
  if (!decode && !(i >= 0 && entries[i].state == READY)) {
    unlock();
    if (i < 0) want(p, f);
    return nullptr;
  }
  if (threaded() && i >= 0 && entries[i].state == BUSY) {  // the background task is on it: wait
    const uint32_t t0 = nowUs();
    for (int k = 0; k < 400 && i >= 0 && entries[i].state == BUSY; k++) {
      unlock();
      yield1();
      lock();
      i = findEntry(f);
    }
    st.sprWaitUs += nowUs() - t0;
  }
  if (i >= 0 && entries[i].state == READY) {
    entries[i].used = clock;
    const Sprite* s = &entries[i].spr;
    unlock();
    return s;
  }
  if (i >= 0 && entries[i].state == BUSY) return unlock(), nullptr;  // still not done after 400 ms
  // Decoded here only within the budget and with PSRAM to spare, as the decoders do: else the thing's older picture
  // stands in, drawn at this one's size (run.cpp thing()).
  const uint32_t need = (uint32_t)f->bw * f->bh * 3;
  makeRoom(need);
  if (!fits(need)) {
    st.sprHeld++;
    unlock();
    if (i < 0) want(p, f, true);  // the decoders take it when there is room
    return nullptr;
  }
  if (i < 0) i = freeEntry();
  if (i < 0) return unlock(), nullptr;
  Entry& e = entries[i];
  e.pack = &p, e.f = f, e.used = clock;
  cacheBytes += need, e.bytes = need;
  e.state = BUSY;
  unlock();
  const uint32_t t0 = nowUs();
  const bool ok = runSprite(*syncCtx, i);
  const uint32_t us = nowUs() - t0;
  lock();
  st.sprInline++, st.sprUs += us, st.sprMaxUs = max(st.sprMaxUs, us);
  const Sprite* s = ok && entries[i].state == READY ? &entries[i].spr : nullptr;
  if (s) entries[i].used = clock;
  unlock();
  return s;
}

void want(const Pack& p, const pbs2_frame_t* f, bool urgent) {
  if (!f || !f->bw || !entries) return;
  lock();
  int i = findEntry(f);
  if (i >= 0) {
    // Wished again: kept. (A picture decoded ahead for two or three pictures on kept its old time, so the cache
    // dropped it first, before the pictures just drawn, and the thing stood on an older picture when it came.)
    if (entries[i].state == QUEUED || entries[i].state == READY) entries[i].used = clock;
    if (urgent && entries[i].state == QUEUED && entries[i].seq >= LATER) entries[i].seq = ++urgentNo;
    return unlock();
  }
  // All taken: the least recently used ready picture goes. (Refusing wishes then left a thing on its old picture
  // once 320 pictures were kept: it stood still up the road and jumped when it came near.)
  i = freeEntry();
  if (i < 0) return unlock();  // all wished or in use this picture
  Entry& e = entries[i];
  e.pack = &p, e.f = f, e.used = clock, e.seq = urgent ? ++urgentNo : LATER + ++seqNo, e.state = QUEUED;
  unlock();
#ifdef SIM
  wake();  // the simulator decodes now
#endif
}

void keep(const Pack& p, const pbs2_frame_t* f) {
  want(p, f);
  lock();
  const int i = findEntry(f);
  if (i >= 0 && !entries[i].pinned) {
    entries[i].pinned = true;
    if (entries[i].state == READY) pinnedBytes += entries[i].bytes;  // decoded already: counted now
  }
  unlock();
}

void kick() { wake(); }

void roads(Road* table, int n) {
  lock();
  roadTab = table, roadN = n;
  for (Slot& s : slots)
    if (s.state != BUSY) s.key = -1, s.state = FREE;
  shownSlot = -1;
  unlock();
}

void plan(const int* keys, int n) {
  lock();
  for (int s = 0; s < SLOTS; s++) {
    Slot& sl = slots[s];
    sl.prio = 99;
    for (int k = 0; k < n; k++)
      if (keys[k] == sl.key) {
        sl.prio = k;
        break;
      }
    if (sl.prio == 99 && s != shownSlot && sl.state != BUSY) sl.key = -1, sl.state = FREE;
  }
  for (int k = 0; k < n; k++) {
    bool have = false;
    for (const Slot& sl : slots) have |= sl.key == keys[k];
    if (have) continue;
    int f = -1;
    for (int s = 0; s < SLOTS && f < 0; s++)
      if (slots[s].state == FREE) f = s;
    if (f < 0) break;
    slots[f].key = keys[k], slots[f].state = QUEUED, slots[f].prio = k;
  }
  unlock();
  wake();
}

bool roadReady(int key) {
  lock();
  bool r = false;
  for (const Slot& s : slots) r |= s.key == key && s.state == READY;
  unlock();
  return r;
}

uint32_t timeSprite(const Pack& p, const pbs2_frame_t* f) {
  if (!f || !f->bw || !syncCtx) return 0;
  const uint32_t px = (uint32_t)f->bw * f->bh;
  uint8_t* mem = (uint8_t*)psram16(px * 3);
  if (!mem) return 0;
  const uint32_t t0 = nowUs();
  const bool ok = decodeSprite(*syncCtx, p, f, (uint16_t*)mem, mem + px * 2);
  const uint32_t us = nowUs() - t0;
  heap_caps_free(mem);
  return ok ? std::max<uint32_t>(us, 1) : 0;
}

uint32_t timeRoad(int key) {
  if (!syncCtx || !roadTab) return 0;
  uint16_t* px = (uint16_t*)psram16(ROAD_W * ROAD_H * 2);
  if (!px) return 0;
  // A slot of our own for runRoad(): the last one, borrowed when it is free.
  lock();
  int s = -1;
  for (int i = SLOTS - 1; i >= 0 && s < 0; i--)
    if (slots[i].state == FREE && i != shownSlot) s = i;
  Slot keep;
  if (s >= 0) keep = slots[s], slots[s].key = key, slots[s].px = px, slots[s].state = BUSY;
  unlock();
  if (s < 0) return heap_caps_free(px), 0;
  const uint32_t t0 = nowUs();
  const bool ok = runRoad(*syncCtx, s);
  const uint32_t us = nowUs() - t0;
  lock();
  slots[s] = keep;
  unlock();
  heap_caps_free(px);
  return ok ? std::max<uint32_t>(us, 1) : 0;
}

// Bench: Espressif's decoder against TJpgDec on one road picture and one sprite (colours, 0..31/63 a channel).
void compare(int key, const Pack& p, const pbs2_frame_t* f) {
#if FAST_JPEG
  if (!syncCtx || !roadTab || key < 0 || key & FADE || !f || !f->bw) return;
  const int fi = key >> 12, pic = key & 4095;
  if (fi >= roadN || pic >= roadTab[fi].n) return;
  Road& r = roadTab[fi];
  const uint32_t at = r.off[pic], len = r.off[pic + 1] - at;
  uint8_t* jp = (uint8_t*)psram(len);
  uint16_t* a = (uint16_t*)psram16(ROAD_W * ROAD_H * 2);
  uint16_t* b = (uint16_t*)psram16(ROAD_W * ROAD_H * 2);
  auto diff = [](const uint16_t* x, const uint16_t* y, int n, const char* what) {
    uint32_t mx[3] = {}, sum[3] = {};
    for (int i = 0; i < n; i++) {
      const int d[3] = {abs((x[i] >> 11) - (y[i] >> 11)), abs((x[i] >> 5 & 63) - (y[i] >> 5 & 63)), abs((x[i] & 31) - (y[i] & 31))};
      for (int k = 0; k < 3; k++) mx[k] = max(mx[k], (uint32_t)d[k]), sum[k] += d[k];
    }
    Serial.printf("RUNBENCH Espressif vs TJpgDec, %s: max diff r %lu g %lu b %lu, avg %.2f %.2f %.2f\n", what,
                  (unsigned long)mx[0], (unsigned long)mx[1], (unsigned long)mx[2], sum[0] / (double)n, sum[1] / (double)n,
                  sum[2] / (double)n);
  };
  if (jp && a && b && (r.file.data() ? (memcpy(jp, r.file.data() + at, len), true) : r.file.read(at, jp, len))) {
    Src j = {{jp, nullptr, nullptr}, {len, 0, 0}, 0, 0, b, ROAD_W, ROAD_H, ROAD_W};
    const uint32_t keep = jpegReserve;
    jpegReserve = 1u << 30;  // tables in PSRAM
    uint32_t t0 = nowUs();
    fastJpeg(jp, len, a, ROAD_W, ROAD_H);
    const uint32_t ps = nowUs() - t0;
    jpegReserve = 0;  // tables inside (as long as there is any room)
    const uint32_t in0 = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    t0 = nowUs();
    fastJpeg(jp, len, a, ROAD_W, ROAD_H);
    const uint32_t ins = nowUs() - t0;
    jpegReserve = keep;
    Serial.printf("RUNBENCH Espressif road picture, tables in PSRAM %.1f ms, inside %.1f ms (internal free %u KB)\n",
                  ps / 1000.0f, ins / 1000.0f, (unsigned)(in0 >> 10));
    t0 = nowUs();
    const bool fa = fastJpeg(jp, len, a, ROAD_W, ROAD_H);
    const uint32_t t1 = nowUs();
    const bool tj = jpeg(*syncCtx, j);
    const uint32_t t2 = nowUs();
    Serial.printf("RUNBENCH road picture (from memory): Espressif %s %.1f ms, TJpgDec %s %.1f ms\n", fa ? "ok" : "FAILED",
                  (t1 - t0) / 1000.0f, tj ? "ok" : "FAILED", (t2 - t1) / 1000.0f);
    if (fa && tj) diff(a, b, ROAD_W * ROAD_H, "road");
  }
  // The sprite: decodeSprite (Espressif) against the three pieces through TJpgDec.
  const pbs2_head_t* h = p.head();
  const uint32_t px = (uint32_t)f->bw * f->bh;
  uint16_t* sa = (uint16_t*)psram16(px * 3);
  uint16_t* sb = (uint16_t*)psram16(px * 2);
  if (sa && sb && decodeSprite(*syncCtx, p, f, sa, (uint8_t*)(sa + px))) {
    static const uint8_t EOI[2] = {0xFF, 0xD9};
    const uint8_t* scan = syncCtx->fbuf + h->hdr_len;  // left there by decodeSprite (one piece)
    Src s = {{syncCtx->hdr, scan, EOI}, {h->hdr_len, f->scan_len, 2}, 0, 0, sb, f->bw, f->bh, f->bw};
    if (jpeg(*syncCtx, s)) diff(sa, sb, px, "sprite");
  }
  heap_caps_free(sa), heap_caps_free(sb);
  heap_caps_free(jp), heap_caps_free(a), heap_caps_free(b);
#else
  (void)key, (void)p, (void)f;
#endif
}

const uint16_t* road(int key, bool* late) {
  if (late) *late = false;
  lock();
  int s = -1;
  for (int i = 0; i < SLOTS; i++)
    if (slots[i].key == key && slots[i].state != FREE) s = i;
  if (s >= 0 && slots[s].state == READY) {
    shownSlot = s;
    const uint16_t* px = slots[s].px;
    unlock();
    return px;
  }
  if (shownSlot >= 0 && slots[shownSlot].state == READY) {  // late: the picture shown last stands in
    if (late) *late = true;
    const uint16_t* px = slots[shownSlot].px;
    unlock();
    return px;
  }
  // Nothing shown yet: this picture now (decode it here when the worker has not started it).
  if (s < 0) {
    for (int i = 0; i < SLOTS && s < 0; i++)
      if (slots[i].state == FREE) s = i;
    if (s < 0) return unlock(), nullptr;
    slots[s].key = key, slots[s].state = QUEUED, slots[s].prio = 0;
  }
#ifndef SIM
  for (int k = 0; k < 400 && slots[s].state == BUSY; k++) {
    unlock();
    vTaskDelay(1);
    lock();
  }
#endif
  if (slots[s].state == QUEUED && slots[s].key == key) {
    slots[s].state = BUSY;
    unlock();
    const uint32_t t0 = nowUs();
    const bool ok = runRoad(*syncCtx, s);
    const uint32_t us = nowUs() - t0;
    lock();
    slots[s].state = ok ? READY : FREE;
    if (!ok) slots[s].key = -1, st.roadFailed++;
    st.roadDec++, st.roadInline++, st.roadUs += us, st.roadMaxUs = max(st.roadMaxUs, us);
  }
  const uint16_t* px = nullptr;
  if (slots[s].state == READY && slots[s].key == key) shownSlot = s, px = slots[s].px;
  unlock();
  return px;
}

}  // namespace media
}  // namespace run

#if FAST_JPEG
// Espressif's decoder asks for its tables in internal RAM first (about 10 KB a picture, two decoders at once). The
// OS needs that RAM (WiFi, TLS): internal only while jpegReserve stays free after it, else PSRAM. These replace
// esp_jpeg_memory.c of lib/esp_new_jpeg (the linker then leaves that object out).
namespace {
constexpr uint32_t INT8 = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT, PS8 = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
bool roomInside(size_t n) { return heap_caps_get_free_size(INT8) > n + run::media::jpegReserve; }
}  // namespace
extern "C" {
void* jpeg_calloc(size_t n, size_t size) { return heap_caps_calloc_prefer(n, size, 2, PS8, INT8); }
void* jpeg_calloc_inner(size_t size) {
  void* p = roomInside(size) ? heap_caps_calloc(1, size, INT8) : nullptr;
  return p ? p : heap_caps_calloc(1, size, PS8);
}
void* jpeg_calloc_align(size_t size, int align) {
  void* p = heap_caps_aligned_calloc(align, 1, size, PS8);
  return p ? p : heap_caps_aligned_calloc(align, 1, size, INT8);
}
void* jpeg_calloc_align_inner(size_t size, int align) {
  void* p = roomInside(size) ? heap_caps_aligned_calloc(align, 1, size, INT8) : nullptr;
  return p ? p : heap_caps_aligned_calloc(align, 1, size, PS8);
}
void jpeg_free(void* p) { heap_caps_free(p); }
void jpeg_free_align(void* p) { heap_caps_free(p); }
}
#endif
