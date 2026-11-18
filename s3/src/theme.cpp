#include "theme.h"

#include <ctime>
#include <vector>

#include "desk.h"
#include "photo.h"
#include "trace.h"
#include "uikit.h"

namespace theme {

const Wall WALLS[] = {
    {"tim", N_("Night violet"), 0x151b44, 0x211650, 0x4a35b8, 0xa3266f},
    {"bien", N_("Sea"), 0x061936, 0x0a3550, 0x1d6fd8, 0x0fa59a},
    {"hoang-hon", N_("Sunset"), 0x2a0f3d, 0x3a1030, 0xff6a3d, 0xd4337a},
    {"rung", N_("Forest"), 0x06231c, 0x0b3326, 0x1f8a5a, 0x8a7a1f},
    {"than", N_("Graphite"), 0x101318, 0x1a1d24, 0x3a4252, 0x30283a},
};
const int WALL_COUNT = sizeof(WALLS) / sizeof(WALLS[0]);

namespace {

const Wall LIGHT_WALL = {"sang", N_("Light"), 0xe9edf6, 0xdde3f0, 0xc7d2fe, 0xfbcfe8};
const Wall DARK_WALL = {"toi", N_("Dark"), 0x05070c, 0x0a0d14, 0x1c2547, 0x2a1533};

Kind cur = Kind::Color;
const Wall* curWall = &WALLS[0];
bool appsDark = false;
Home homeCols;

struct Cache {
  lv_image_dsc_t dsc = {};
  uint16_t* px = nullptr;
  int w = 0, h = 0;
  String key;
} wp;

uint32_t mix(uint32_t a, uint32_t b, float t) {
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  const int r = ((a >> 16) & 255) + (((int)((b >> 16) & 255) - (int)((a >> 16) & 255)) * t);
  const int g = ((a >> 8) & 255) + (((int)((b >> 8) & 255) - (int)((a >> 8) & 255)) * t);
  const int bl = (a & 255) + (((int)(b & 255) - (int)(a & 255)) * t);
  return (r << 16) | (g << 8) | bl;
}

// 4x4 Bayer thresholds, centred on zero (in 1/16 steps).
const int8_t BAYER[16] = {-8, 0, -6, 2, 4, -4, 6, -2, -5, 3, -7, 1, 7, -1, 5, -3};

inline uint16_t dither565(float r, float g, float b, int x, int y) {
  const float d = BAYER[(y & 3) * 4 + (x & 3)] / 16.0f;
  int R = (int)(r / 8.0f + d + 0.5f), G = (int)(g / 4.0f + d + 0.5f), B = (int)(b / 8.0f + d + 0.5f);
  R = R < 0 ? 0 : R > 31 ? 31 : R;
  G = G < 0 ? 0 : G > 63 ? 63 : G;
  B = B < 0 ? 0 : B > 31 ? 31 : B;
  return (R << 11) | (G << 5) | B;
}

// The look of the mockup: a diagonal base gradient and two soft elliptical glows
// (CSS radial-gradient(120% 70% at 15% 0%) and (90% 60% at 100% 100%), fading out at 62%).
void paint(uint16_t* px, int w, int h, const Wall& wl) {
  const float ar = ((wl.blobA >> 16) & 255), ag = ((wl.blobA >> 8) & 255), ab = (wl.blobA & 255);
  const float br = ((wl.blobB >> 16) & 255), bg = ((wl.blobB >> 8) & 255), bb = (wl.blobB & 255);
  const bool tall = h >= w;
  // Glow sizes follow the portrait design; in landscape the long side swaps.
  const float aRx = (tall ? 1.2f * w : 0.8f * w), aRy = (tall ? 0.7f * h : 1.0f * h);
  const float bRx = (tall ? 0.9f * w : 0.6f * w), bRy = (tall ? 0.6f * h : 0.9f * h);
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      const float t = (0.35f * x / w + (float)y / h) / 1.35f;
      const uint32_t base = mix(wl.top, wl.bottom, t);
      float r = (base >> 16) & 255, g = (base >> 8) & 255, b = base & 255;
      // Glow A at (15%, 0)
      float dx = (x - 0.15f * w) / aRx, dy = (float)y / aRy;
      float d = sqrtf(dx * dx + dy * dy) / 0.62f;
      if (d < 1) {
        const float k = (1 - d) * (1 - d) * 0.95f + (1 - d) * 0.05f;
        r += (ar - r) * k, g += (ag - g) * k, b += (ab - b) * k;
      }
      dx = (x - w) / bRx, dy = (y - h) / bRy;
      d = sqrtf(dx * dx + dy * dy) / 0.62f;
      if (d < 1) {
        const float k = (1 - d) * (1 - d) * 0.9f + (1 - d) * 0.05f;
        r += (br - r) * k, g += (bg - g) * k, b += (bb - b) * k;
      }
      px[y * w + x] = dither565(r, g, b, x, y);
    }
  }
}

void setDsc(Cache& c) {
  c.dsc = {};
  c.dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  c.dsc.header.cf = LV_COLOR_FORMAT_RGB565;
  c.dsc.header.w = c.w;
  c.dsc.header.h = c.h;
  c.dsc.header.stride = c.w * 2;
  c.dsc.data_size = c.w * c.h * 2;
  c.dsc.data = (const uint8_t*)c.px;
}

bool ensure(Cache& c, int w, int h) {
  if (c.px && c.w == w && c.h == h) return true;
  heap_caps_free(c.px);
  c.px = (uint16_t*)heap_caps_malloc(w * h * 2, MALLOC_CAP_SPIRAM);
  c.w = w, c.h = h;
  c.key = "";
  return c.px != nullptr;
}

bool nightNow() {
  const time_t t = time(nullptr);
  tm l;
  localtime_r(&t, &l);
  const int m = l.tm_hour * 60 + l.tm_min;
  return m >= 19 * 60 || m < 6 * 60;
}

Kind parse(const String& s) {
  if (s == "light") return Kind::Light;
  if (s == "dark") return Kind::Dark;
  if (s == "photo") return Kind::Photo;
  return Kind::Color;
}

void computeHome() {
  if (cur == Kind::Light) homeCols = {0x16181d, 0x4b5160, 0xffffff, LV_OPA_70, 0xffffff, LV_OPA_60, true};
  else homeCols = {0xffffff, 0xd4d8e4, 0xffffff, 38, 0xffffff, 46, false};
}

}  // namespace

void begin() { refresh(); }

bool refresh() {
  const Kind k = parse(desk::setting("s3.theme", "color"));
  const String wid = desk::setting("s3.wall", "tim");
  const Wall* w = &WALLS[0];
  for (int i = 0; i < WALL_COUNT; i++)
    if (wid == WALLS[i].id) w = &WALLS[i];
  const bool dark = k == Kind::Dark || (desk::settingInt("s3.theme_auto", 1) && nightNow());
  const bool changed = k != cur || w != curWall || dark != appsDark;
  cur = k;
  curWall = w;
  appsDark = dark;
  kit::setDark(dark);
  computeHome();
  if (changed) trace::add("theme %s, wall %s, apps %s", kindId(), w->id, dark ? "dark" : "light");
  return changed;
}

Kind kind() { return cur; }
const char* kindId() {
  switch (cur) {
    case Kind::Light: return "light";
    case Kind::Dark: return "dark";
    case Kind::Photo: return "photo";
    default: return "color";
  }
}
const Wall& wall() { return cur == Kind::Light ? LIGHT_WALL : cur == Kind::Dark ? DARK_WALL : *curWall; }
const Home& home() { return homeCols; }
bool darkApps() { return appsDark; }

const lv_image_dsc_t* wallpaper(int w, int h) {
  String key = String(kindId()) + wall().id;
  const lv_image_dsc_t* pic = nullptr;
  if (cur == Kind::Photo) {
    pic = photo::screen(0, w, h);
    if (pic) key += String((uint32_t)(uintptr_t)pic->data);
  }
  if (!ensure(wp, w, h)) return nullptr;
  if (wp.key == key) return &wp.dsc;
  const uint32_t t0 = millis();
  if (pic && pic->header.w == w && pic->header.h == h) {
    // A photo, darkened a little so white text and glass widgets stay readable.
    const uint16_t* src = (const uint16_t*)pic->data;
    for (int i = 0; i < w * h; i++) {
      const uint16_t v = src[i];
      const int r = (v >> 11) * 7 / 10, g = ((v >> 5) & 63) * 7 / 10, b = (v & 31) * 7 / 10;
      wp.px[i] = (r << 11) | (g << 5) | b;
    }
  } else {
    paint(wp.px, w, h, cur == Kind::Photo ? WALLS[0] : wall());
  }
  wp.key = key;
  setDsc(wp);
  trace::add("wallpaper %s %dx%d in %lu ms", key.c_str(), w, h, millis() - t0);
  return &wp.dsc;
}

// Pictures of wallpaperOf, by id and size, with the number of holders.
struct Held {
  String key;
  lv_image_dsc_t* d;
  int holds;
};
std::vector<Held> held;

const lv_image_dsc_t* wallpaperOf(const char* id, int w, int h) {
  const Wall* wl = nullptr;
  for (int i = 0; i < WALL_COUNT; i++)
    if (id && !strcmp(id, WALLS[i].id)) wl = &WALLS[i];
  if (!wl) return wallpaper(w, h);
  const String key = String(id) + w + "x" + h;
  for (Held& x : held)
    if (x.key == key) return x.holds++, x.d;
  const uint32_t t0 = millis();
  lv_image_dsc_t* d = makePreview(*wl, w, h);
  if (!d) return wallpaper(w, h);
  held.push_back({key, d, 1});
  trace::add("wall %s painted in %lu ms (%d KB held)", key.c_str(), millis() - t0, heldWallKB());
  return d;
}

void releaseWall(const lv_image_dsc_t* d) {
  for (size_t i = 0; i < held.size(); i++) {
    if (held[i].d != d) continue;
    if (--held[i].holds > 0) return;
    lv_image_cache_drop(held[i].d);
    freePreview(held[i].d);
    held.erase(held.begin() + i);
    return;
  }
}

int heldWallKB() {
  int kb = 0;
  for (const Held& x : held) kb += x.d->data_size / 1024;
  return kb;
}

lv_image_dsc_t* makePreview(const Wall& w, int pw, int ph) {
  Cache c;
  c.px = (uint16_t*)heap_caps_malloc(pw * ph * 2, MALLOC_CAP_SPIRAM);
  if (!c.px) return nullptr;
  c.w = pw, c.h = ph;
  paint(c.px, pw, ph, w);
  setDsc(c);
  lv_image_dsc_t* d = new lv_image_dsc_t(c.dsc);
  return d;
}

void freePreview(lv_image_dsc_t* d) {
  if (!d) return;
  heap_caps_free((void*)d->data);
  delete d;
}

}  // namespace theme
