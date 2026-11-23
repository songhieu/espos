#pragma GCC optimize("Os")  // lookups and file reads, no per-pixel work: built for size (the app slot is full)
#include "packs.h"

#include <Arduino.h>

#include <map>
#include <string>

#include "dogs.h"
#include "fonts.h"
#include "trace.h"

namespace pet {
namespace pk {
namespace {

// The game's own package (screen pieces, bubble fonts) and the chosen dog's package (pictures, lines). In the
// internal flash files are read in place (memory-mapped). On the memory card PSRAM keeps what every picture needs:
// the dog's header, clips and offsets and each picture's 22-byte record (frame() points there), the pieces' table,
// and the voice file's table and texts; a picture's bytes are read each time it is decoded (data(), mask(): dog.cpp),
// a line's packets when it is said (voice.cpp), a piece's pixels once, then kept.
constexpr const char* PKG = "vn.pawboop.pet";

void* psram(size_t n) { return heap_caps_malloc(n ? n : 1, MALLOC_CAP_SPIRAM); }

struct DogPack {
  const uint8_t* b = nullptr;  // mapped file, or our PSRAM copy of its index
  size_t n = 0;
  uint8_t* own = nullptr;
  pkg::File f;                 // the card's file
  pbs2_frame_t* recs = nullptr;  // card: every picture's record (nframes), PSRAM, read when first asked for
  uint8_t* have = nullptr;       // card: which records are read (a bit each)
  uint32_t* offs = nullptr;      // card: nframes + 1 offsets
  uint32_t dataMaxB = 0;         // card: the largest scan_len + mask_len (from the offsets)
  void free() {
    heap_caps_free(own), heap_caps_free(recs), heap_caps_free(offs), heap_caps_free(have);
    *this = DogPack();
  }
};
DogPack dp, oldDp;  // the dog's pictures; the ones before the last switch (kept one more switch)
Pba vp, oldVp;      // the dog's lines

pkg::File uiF;
std::map<const pbu_piece_t*, uint8_t*> pix;  // card: pieces read so far (kept)
const uint8_t* uiB = nullptr;
size_t uiN = 0;
bool uiOk = false;
std::map<std::string, const pbu_piece_t*> pieces;
std::map<const pbu_piece_t*, lv_image_dsc_t*> images;
const lv_font_t* fnt[3] = {};
bool fontsTried = false;

bool tried = false, ok = false;
uint32_t triedVer = 0, gen = 0;
const char* miss = "";
uint32_t tookMs = 0;
String key;           // the dog loaded
const char* want = nullptr;  // the dog to load (dogs::chosen())

// The dog pack on the card: its index into PSRAM (header, clips, offsets: a few KB, one read). Each picture's
// 22-byte record lies before the picture, spread over the whole 5-7 MB file: it is read the first time the picture
// is asked for (frame()). Reading them all at once read the whole file, 836 ms on the UI thread for a 5.6 MB dog.
bool dogIndex(const pkg::File& f, DogPack& d) {
  pbs2_head_t h;
  if (f.size() < sizeof h || !f.read(0, &h, sizeof h) || !h.nframes || h.nframes > 65536) return false;
  const uint32_t tab = 4 * (h.nframes + 1);
  const uint64_t idx = sizeof h + (uint64_t)h.hdr_len + 24ull * h.nclips + tab;
  if (idx > f.size()) return false;
  d.own = (uint8_t*)psram(idx);
  d.recs = (pbs2_frame_t*)heap_caps_calloc(h.nframes, sizeof(pbs2_frame_t), MALLOC_CAP_SPIRAM);
  d.have = (uint8_t*)heap_caps_calloc(h.nframes / 8 + 1, 1, MALLOC_CAP_SPIRAM);
  d.offs = (uint32_t*)psram(tab);
  bool good = d.own && d.recs && d.have && d.offs && f.read(0, d.own, idx);
  if (good) memcpy(d.offs, d.own + idx - tab, tab);
  for (uint32_t i = 0; good && i < h.nframes; i++) {  // every picture inside the file, in order
    good = d.offs[i] >= idx && d.offs[i] + sizeof(pbs2_frame_t) <= d.offs[i + 1] && d.offs[i + 1] <= f.size();
    if (good) d.dataMaxB = max<uint32_t>(d.dataMaxB, d.offs[i + 1] - d.offs[i] - sizeof(pbs2_frame_t));
  }
  if (!good) {
    d.free();
    return false;
  }
  d.f = f;
  d.b = d.own;
  d.n = idx;
  return true;
}

// A picture's record from the card, the first time (an empty picture if it cannot be read or does not fit).
const pbs2_frame_t* record(DogPack& d, uint32_t k) {
  if (!(d.have[k >> 3] & (1 << (k & 7)))) {
    pbs2_frame_t r;
    if (d.f.read(d.offs[k], &r, sizeof r) && (uint64_t)r.scan_len + r.mask_len <= d.offs[k + 1] - d.offs[k] - sizeof r)
      d.recs[k] = r;
    else
      memset(&d.recs[k], 0, sizeof r);
    d.have[k >> 3] |= 1 << (k & 7);
  }
  return &d.recs[k];
}

bool loadDog(const char* id, const char* name, DogPack& d) {
  pkg::File f;
  if (!pkg::open(id, name, f)) return false;
  if (f.data()) {
    d.b = f.data(), d.n = f.size();
    return true;
  }
  return dogIndex(f, d);
}

bool dogOk(const DogPack& d) {
  const pbs2_head_t* h = (const pbs2_head_t*)d.b;
  if (!h || memcmp(h->magic, "PBS2", 4) || h->w != 320 || h->h != 304) return false;
  const uint8_t* off = (const uint8_t*)(h + 1) + h->hdr_len + h->nclips * sizeof(pbs2_clip_t);
  if (off + 4 * (h->nframes + 1) > d.b + d.n) return false;
  uint32_t end;
  memcpy(&end, off + 4 * h->nframes, 4);
  return end <= (d.recs ? d.f.size() : d.n);
}

// The pieces pack on the card: its table into PSRAM (pixels() reads a piece when it is first drawn).
bool loadUi() {
  if (uiOk) return true;
  pkg::File f;
  if (!pkg::open(PKG, "pawboop_ui.pbu", f)) return false;
  if (f.data()) {
    uiB = f.data(), uiN = f.size();
  } else {
    pbu_head_t h;
    if (f.size() < sizeof h || !f.read(0, &h, sizeof h)) return false;
    const uint32_t len = sizeof h + h.count * sizeof(pbu_piece_t);
    uint8_t* b = len <= f.size() ? (uint8_t*)psram(len) : nullptr;
    if (!b || !f.read(0, b, len)) return heap_caps_free(b), false;
    uiF = f, uiB = b, uiN = len;
  }
  const pbu_head_t* u = (const pbu_head_t*)uiB;
  if (memcmp(u->magic, "PBU1", 4) || 8 + u->count * sizeof(pbu_piece_t) > uiN) return false;
  const pbu_piece_t* p = (const pbu_piece_t*)(u + 1);
  for (int i = 0; i < u->count; i++) pieces[std::string(p[i].name, strnlen(p[i].name, 24))] = &p[i];
  return uiOk = true;
}

void loadFonts() {
  if (fontsTried) return;
  fontsTried = true;
  static const char* const FILES[3] = {"font/b12.bin", "font/b14.bin", "font/s11.bin"};
  static const uint8_t PX[3] = {12, 14, 12};
  for (int k = 0; k < 3; k++) {
    pkg::File f;
    if (!pkg::open(PKG, FILES[k], f)) continue;
    const uint8_t* b = f.data();
    if (!b || ((uintptr_t)b & 3)) b = pkg::loadAll(f);  // the card: a copy in PSRAM (kept)
    fnt[k] = fonts::fromFile(b, f.size(), PX[k], true);
  }
}

}  // namespace

// ------------------------------------------------------------------ Pba
bool Pba::load(const char* id, const char* name) {
  free();
  pkg::File f;
  if (!pkg::open(id, name, f)) return false;
  pba_head_t h;
  if (f.size() < sizeof h) return false;
  if (f.data()) memcpy(&h, f.data(), sizeof h);
  else if (!f.read(0, &h, sizeof h)) return false;
  if (memcmp(h.magic, "PBA1", 4) || h.channels != 1 || !h.rate || h.data_off > f.size() || h.text_off > h.data_off ||
      sizeof h + sizeof(pba_clip_t) * (uint64_t)h.nclips > h.text_off)
    return false;
  if (f.data()) {
    map_ = head_ = f.data();
  } else {
    own_ = (uint8_t*)psram(h.data_off);
    if (!own_ || !f.read(0, own_, h.data_off)) {
      free();
      return false;
    }
    head_ = own_, len_ = h.data_off;
  }
  file_ = f;
  return true;
}

void Pba::free() {
  heap_caps_free(own_);
  own_ = nullptr, head_ = map_ = nullptr, len_ = 0;
  file_ = pkg::File();
}

const pba_clip_t* Pba::find(const char* name) const {
  for (int i = 0; name && i < count(); i++)
    if (!strncmp(at(i)->name, name, 16)) return at(i);
  return nullptr;
}

const uint8_t* Pba::packets(const pba_clip_t* l, uint8_t* buf) const {
  if (!l || !ok()) return nullptr;
  const uint64_t at = (uint64_t)head()->data_off + l->off;
  if (at + l->bytes > file_.size()) return nullptr;
  if (map_) return map_ + at;
  if (!buf || !file_.read((uint32_t)at, buf, l->bytes)) return nullptr;
  return buf;
}

// ------------------------------------------------------------------ the packs
bool open() {
  const dogs::Info* d = dogs::chosen();
  // Once found the packs stay for the run (a package removed while in use keeps its bytes until a restart);
  // another dog is loaded only through reload(). Until found, look again whenever the packages change.
  if (ok || (tried && triedVer == pkg::version() && want == (d ? d->key : nullptr))) return ok;
  tried = true;
  triedVer = pkg::version();
  want = d ? d->key : nullptr;
  const uint32_t t0 = millis();
  oldDp.free(), oldVp.free();
  oldDp = dp, oldVp = vp;  // still valid until the next switch
  dp = DogPack(), vp = Pba();
  key = "";
  miss = "";
  if (!pkg::find(PKG) || !loadUi()) {
    miss = "pet";
  } else if (d) {
    const String id = dogs::pkgId(d->key);
    if (!loadDog(id.c_str(), "pet.pbs", dp) || !dogOk(dp)) miss = "pet.pbs";
    // Every line with the dog's game part (a memory card), else the core's lite voice (2 lines a moment).
    else if (!vp.load(id.c_str(), "voice_full.pba") && !vp.load(id.c_str(), "voice.pba")) miss = "voice.pba";
    else key = d->key;
  } else if (loadDog(PKG, "biscuit_M.pbs", dp) && dogOk(dp) && vp.load(PKG, "biscuit_voice_12k.pba")) {
    key = "corgi";  // PawBoop 1.0 (Biscuit's packs in its own package): she plays until a dog is installed
  } else {
    miss = "dog";
  }
  ok = !*miss;
  if (!ok) dp.free(), vp.free();
  else gen++, loadFonts();
  tookMs = millis() - t0;
  trace::add("pet: packs %s%s (dog %s, %u bytes, voice %u lines, %lu ms)", ok ? "ok" : "missing ", miss, key.c_str(),
             (unsigned)dp.n, (unsigned)vp.count(), (unsigned long)tookMs);
  return ok;
}

void reload() { ok = false, tried = false; }

bool ready() { return open(); }
const char* missing() { return miss; }
uint32_t mapMs() { return tookMs; }
const char* dogKey() { return key.c_str(); }
uint32_t generation() { return gen; }

const pbs2_head_t* dog() { return (const pbs2_head_t*)dp.b; }
const uint8_t* jpegHeader() { return (const uint8_t*)(dog() + 1); }
int clipCount() { return ok ? dog()->nclips : 0; }
const pbs2_clip_t* clipAt(int i) { return (const pbs2_clip_t*)(jpegHeader() + dog()->hdr_len) + i; }

const pbs2_clip_t* clip(const char* name) {
  if (!ok || !name) return nullptr;
  for (int i = 0; i < dog()->nclips; i++)
    if (!strncmp(clipAt(i)->name, name, 16)) return clipAt(i);
  return nullptr;
}

const pbs2_frame_t* frame(const pbs2_clip_t* c, int i) {
  if (!c || !c->count) return nullptr;
  i %= c->count;
  if (dp.recs) return record(dp, c->first + i);  // on the card
  // The offsets table is not 4-byte aligned in the file (the board faults on unaligned 32-bit loads).
  uint32_t at;
  memcpy(&at, (const uint8_t*)clipAt(dog()->nclips) + 4 * (c->first + i), 4);
  return (const pbs2_frame_t*)(dp.b + at);
}

const pbu_piece_t* piece(const char* name) {
  if (!uiOk) return nullptr;
  auto it = pieces.find(name);
  return it == pieces.end() ? nullptr : it->second;
}
const uint8_t* pixels(const pbu_piece_t* p) {
  if (!uiF.ok()) return uiB + p->off;
  auto it = pix.find(p);  // on the card: read once, then kept (a missing card: blank, not tried again)
  if (it != pix.end()) return it->second;
  const uint32_t n = (uint32_t)p->w * p->h * (p->fmt ? 3 : 2);
  uint8_t* b = (uint8_t*)psram(n);
  if (b && ((uint64_t)p->off + n > uiF.size() || !uiF.read(p->off, b, n))) {
    memset(b, 0, n);
    trace::add("pet: piece %.24s not read from the card", p->name);
  }
  pix[p] = b;
  return b;
}
const uint8_t* alpha(const pbu_piece_t* p) { return p->fmt ? pixels(p) + 2u * p->w * p->h : nullptr; }

void trim() {
  uint32_t freed = 0;
  for (auto it = pix.begin(); it != pix.end();) {
    const pbu_piece_t* p = it->first;
    const uint32_t n = (uint32_t)p->w * p->h * (p->fmt ? 3 : 2);
    if (n >= 64 * 1024 && !images.count(p)) {
      heap_caps_free(it->second);
      freed += n;
      it = pix.erase(it);
    } else {
      ++it;
    }
  }
  if (freed) trace::add("pet: %u KB of pieces let go", (unsigned)(freed >> 10));
}

// A picture's bytes, from the pack it belongs to (the dog's now, or the one before a switch).
const uint8_t* data(const pbs2_frame_t* f, uint8_t* buf) {
  const DogPack& d = oldDp.recs && f >= oldDp.recs && f < oldDp.recs + ((const pbs2_head_t*)oldDp.b)->nframes ? oldDp : dp;
  if (!d.recs) return (const uint8_t*)(f + 1);
  const uint32_t i = f - d.recs, n = f->scan_len + f->mask_len;
  if (!buf || n > d.dataMaxB || !d.f.read(d.offs[i] + sizeof(pbs2_frame_t), buf, n)) return nullptr;
  return buf;
}
const uint8_t* mask(const pbs2_frame_t* f, uint8_t* buf) {
  const DogPack& d = oldDp.recs && f >= oldDp.recs && f < oldDp.recs + ((const pbs2_head_t*)oldDp.b)->nframes ? oldDp : dp;
  if (!d.recs) return (const uint8_t*)(f + 1) + f->scan_len;
  const uint32_t i = f - d.recs;
  if (!buf || f->mask_len > d.dataMaxB || !d.f.read(d.offs[i] + sizeof(pbs2_frame_t) + f->scan_len, buf, f->mask_len))
    return nullptr;
  return buf;
}
uint32_t dataMax() { return max(dp.recs ? dp.dataMaxB : 0, oldDp.recs ? oldDp.dataMaxB : 0); }

const lv_image_dsc_t* image(const pbu_piece_t* p) {
  if (!p) return nullptr;
  auto it = images.find(p);
  if (it != images.end()) return it->second;
  lv_image_dsc_t* d = new lv_image_dsc_t();
  d->header.magic = LV_IMAGE_HEADER_MAGIC;
  d->header.cf = p->fmt ? LV_COLOR_FORMAT_RGB565A8 : LV_COLOR_FORMAT_RGB565;
  d->header.w = p->w;
  d->header.h = p->h;
  d->header.stride = p->w * 2;
  d->data_size = p->w * p->h * (p->fmt ? 3 : 2);
  const uint8_t* px = pixels(p);
  if (!px) {
    delete d;
    return nullptr;
  }
  if ((uintptr_t)px & 3) {
    // Most pieces start at odd offsets: LVGL reads RGB565 as 16-bit words, which the board cannot do at an
    // odd address, so those get an aligned copy in PSRAM (a bubble is about 20 KB). From the card: already.
    uint8_t* copy = (uint8_t*)heap_caps_malloc(d->data_size, MALLOC_CAP_SPIRAM);
    if (!copy) {
      delete d;
      return nullptr;
    }
    memcpy(copy, px, d->data_size);
    d->data = copy;
  } else {
    d->data = px;
  }
  images[p] = d;
  return d;
}

const lv_image_dsc_t* crop(const char* name, int x, int y, int w, int h) {
  static std::map<std::string, lv_image_dsc_t*> crops;
  char k[64];
  snprintf(k, sizeof k, "%s/%d/%d/%d/%d", name, x, y, w, h);
  auto it = crops.find(k);
  if (it != crops.end()) return it->second;
  const pbu_piece_t* p = piece(name);
  if (!p || x < 0 || y < 0 || x + w > p->w || y + h > p->h) return nullptr;
  uint8_t* px = (uint8_t*)heap_caps_malloc(w * h * 2, MALLOC_CAP_SPIRAM);
  if (!px) return nullptr;
  for (int r = 0; r < h; r++) memcpy(px + r * w * 2, pixels(p) + ((y + r) * p->w + x) * 2, w * 2);
  lv_image_dsc_t* d = new lv_image_dsc_t();
  d->header.magic = LV_IMAGE_HEADER_MAGIC;
  d->header.cf = LV_COLOR_FORMAT_RGB565;
  d->header.w = w;
  d->header.h = h;
  d->header.stride = w * 2;
  d->data_size = w * h * 2;
  d->data = px;
  crops[k] = d;
  return d;
}

const Pba& lines() { return vp; }
const pba_head_t* voice() { return vp.head(); }
int lineCount() { return ok ? vp.count() : 0; }
const pba_clip_t* lineAt(int i) { return vp.at(i); }
const pba_clip_t* line(const char* name) { return ok ? vp.find(name) : nullptr; }
const char* text(const pba_clip_t* l) { return vp.text(l); }
String shown(const pba_clip_t* l) {
  if (!l) return "";
  String s = text(l);
  s.replace("\xE2\x80\xA6", "...");
  return s;
}
float seconds(const pba_clip_t* l) { return vp.seconds(l); }

const lv_font_t* font(int which) {
  static const uint8_t PX[3] = {12, 14, 12};
  which = constrain(which, 0, 2);
  if (fnt[which]) return fnt[which];
  return fonts::text(PX[which], true);
}

}  // namespace pk
}  // namespace pet
