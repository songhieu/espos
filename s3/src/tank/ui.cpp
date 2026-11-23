// Hồ cá cảnh: what lies over the water (the hand-off's ref/ui.js): the top bar (coins, level, three gauges), the
// dock (feed, wipe, change water, lamp, shop), the food chooser, the sponge, the fish card, the shop sheet, toasts.
// The bars hide by themselves after 9 quiet seconds: the tank is the picture. Everything is placed from W and H
// (lay()), so upright and on its side share one description; sizes and colours are the page's. Text: Ubuntu, bold
// 12-16 and regular 12/16, never cut with "…" (whole items or whole words go).
#pragma GCC optimize("Os")
#include <chrono>
#include <cmath>

#include "tank_int.h"
#include "../lock.h"

#define HOT __attribute__((optimize("O2")))  // the per-pixel loops (the file is -Os)

namespace tank {
namespace ui {

bool saver = false, homeLine = false;
#if TANK_QA
bool layersOff = false;
uint32_t us();  // (below: the stats' clock)
#endif

namespace {

// The trial's chip under the top bar (lock.h): drawn once by LVGL (every language's letters), padded like the packs'
// pictures; made again when its words change (the days left), freed with the screen.
Spr chip;
uint8_t* chipMem = nullptr;
String chipFor;
float chipT = 99;
void chipFree() { heap_caps_free(chipMem), chipMem = nullptr, chip = Spr(), chipFor = ""; }
void chipMake() {
  const String t = lock::chipText();
  if (t == chipFor) return;
  chipFree();
  chipFor = t;
  uint16_t* rgb;
  uint8_t* al;
  int w, h;
  if (!t.length() || !lock::chipPicture(true, rgb, al, w, h)) return;
  const int st = w + 2, n = st * (h + 2);
  chipMem = (uint8_t*)heap_caps_calloc(n * 3, 1, MALLOC_CAP_SPIRAM);
  if (chipMem) {
    uint16_t* c = (uint16_t*)chipMem;
    uint8_t* a = chipMem + n * 2;
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++) c[(y + 1) * st + x + 1] = rgb[y * w + x], a[(y + 1) * st + x + 1] = al[y * w + x];
    chip.w = w, chip.h = h, chip.st = st, chip.c = c + st + 1, chip.a = a + st + 1;
  }
  heap_caps_free(rgb);
}

using namespace gfx;
constexpr float ROW_H = 66;
const char* const TABS[7] = {"Cá", "Cây", "Đồ", "Máy", "Hồ", "Sổ", "Việc"};

struct Toast {
  String s;
  float t;
  String l0, l1;  // its lines and box, measured at its first picture (draw() runs once a piece)
  float w = 0, h = 0;
};
struct Float {
  String s;
  float x, y, t;
};
struct Press {
  bool on = false;
  float x = 0, y = 0, t = 0, sy = 0, lastY = 0, vel = 0;
  bool moved = false, wasHidden = false;
  int k = 0, i = 0, row = -1, kind = 0;  // k: what was hit
  bool btn = false;
};
enum Hit { H_NONE, H_CLOSE, H_TAB, H_LIST, H_CARD_NONE, H_SELL, H_CURE, H_CARDCLOSE, H_FOOD, H_FOODCLOSE, H_SPONGEOFF, H_WIPE, H_DOCK, H_WATER };

std::vector<Toast> toasts;
std::vector<Float> floats;
bool sheet = false, card = false, sponge = false, foodPick = false;
uint32_t cardUid = 0;
float quiet = 0, show = 1, pulse = 0, scroll = 0, fling = 0;
int tab = 0, pressed = -1;
float spX = 0, spY = 0, lastTapX = 0, lastTapY = 0;
std::vector<game::Row> rows;
struct Fit3 {
  String name, sub, note;
};
std::vector<Fit3> fitted;
Press P;
struct Fx {
  int k = 0;  // 0 none, 1 water, 2 level
  float t = 0;
} fxNow;

inline uint16_t H(uint32_t c) { return hex565(c); }
int A(float a) { return (int)(a * 256 + 0.5f); }

uint32_t fnv(uint32_t h, const void* p, size_t n) {
  for (const uint8_t* b = (const uint8_t*)p; n--; b++) h = (h ^ *b) * 16777619u;
  return h;
}
std::vector<uint32_t> rowKeys;  // what each row says (its words and numbers): its cached picture is kept while it holds

void refreshRows() {
  rows = game::shop(tab);
  rowKeys.clear();
  for (const game::Row& r : rows) {
    uint32_t h = 2166136261u;
    for (const String* x : {&r.name, &r.sub, &r.note, &r.tag}) h = fnv(h, x->c_str(), x->length() + 1);
    const int v[] = {r.art, (int)r.artPack, r.up, r.count, r.price, r.bad, r.warn, r.can, r.dim, r.shade, r.look, r.paid, r.act, r.arg};
    rowKeys.push_back(fnv(h, v, sizeof v) | 1);
  }
  fitted.clear();
  const Lay L = lay();
  for (const game::Row& r : rows) {
    const bool hasBtn = (r.price >= 0 && !r.tag.length()) || r.tag.length() || r.paid;
    const float room = L.shW - 74 - (hasBtn ? 98 : 14);
    fitted.push_back({fit1(r.name.c_str(), 13, true, room), fit1(r.sub.c_str(), 12, false, room), fit1(r.note.c_str(), 12, false, L.shW - 74 - 14)});
  }
}
float clampScroll(float v) {
  const Lay L = lay();
  return std::max(0.0f, std::min(std::max(0.0f, rows.size() * ROW_H - (L.shY + L.shH - L.listY) + 8), v));
}
struct TabR {
  float x, y, w, h;
  const char* name;
};
void tabRects(const Lay& L, TabR* out) {  // the seven tabs' pills
  const bool marine = game::T().marine;
  float ws[7];
  const char* names[7];
  for (int i = 0; i < 7; i++) names[i] = i == 1 && marine ? "San hô" : TABS[i], ws[i] = textW(names[i], 12, true);
  int pad = 7;
  const float gap = 4, room = L.shW - 12;
  auto need = [&]() {
    float s = 0;
    for (float w : ws) s += w + pad * 2;
    return s + 6 * gap;
  };
  while (need() > room && pad > 3) pad--;
  float x = L.shX + std::max(6.0f, (L.shW - need()) / 2);
  for (int i = 0; i < 7; i++) out[i] = {x, L.shY + 42, ws[i] + pad * 2, 26, names[i]}, x += ws[i] + pad * 2 + gap;
}
const FishRec* cardFish() { return card ? game::fishRec(cardUid) : nullptr; }
void needs(const char** out, int& n) {
  const TankRec& t = game::T();
  n = 0;
  if (t.food < 35) out[n++] = "n_food";
  if (t.clean < 45) out[n++] = "n_bucket";
  if (t.algae > 45) out[n++] = "n_sponge";
}
const Spr* thing(const char* name) { return sprNamed(P_THINGS, name); }

int hit(float x, float y, int& i, int& row, bool& btn) {
  const Lay L = lay();
  if (sheet) {
    if (x < L.shX || y < L.shY) return H_CLOSE;
    if (y < L.shY + 38 && x > L.shX + L.shW - 42) return H_CLOSE;
    // The tabs take taps from 14 px over their pills to 10 px into the first row (owner, 2026-11-21, the 2.8" in
    // landscape: "điểm cảm ứng bé quá"): a finger a little high or low still picks the tab.
    if (y < L.shY + 28) return H_NONE;
    if (y < L.listY + 10) {
      TabR tr[7];
      tabRects(L, tr);
      for (int k = 0; k < 7; k++)
        if (x >= tr[k].x - 4 && x < tr[k].x + tr[k].w + 4) return i = k, H_TAB;
      if (y < L.listY - 4) return H_NONE;
    }
    row = (int)floorf((y - L.listY + scroll) / ROW_H), btn = x > L.shX + L.shW - 94;
    return H_LIST;
  }
  if (card) {
    if (y > L.cardY && x >= L.cardX && x < L.cardX + L.cardW) {
      if (y > L.cardY + 78) {
        const FishRec* d = cardFish();
        const float u = x - L.cardX;
        if (d && d->sick) return u < 106 ? H_SELL : u < 204 ? H_CURE : H_CARDCLOSE;
        return u < 152 ? H_SELL : H_CARDCLOSE;
      }
      return H_CARD_NONE;
    }
    return H_CARDCLOSE;
  }
  if (foodPick) {
    int ds[F_N];
    const int n = game::diets(game::T(), ds);
    for (int k = 0; k < n; k++)
      if (fabsf(y - L.foodY[k]) < 26 && x >= L.foodX - 4 && x < L.foodX + 158) return i = ds[k], H_FOOD;
    return H_FOODCLOSE;
  }
  if (sponge) return y > L.dy - L.dr && fabsf(x - L.W / 2) < 46 ? H_SPONGEOFF : H_WIPE;
  if (show > 0.5f && !saver)
    for (int k = 0; k < 5; k++)
      if (hypotf(x - L.dx[k], y - L.dy) < L.dr + 4) return i = k, H_DOCK;
  return H_WATER;
}

// ---------------------------------------------------------------- prepared layers
// Drawn piece by piece every picture, the top bar and the dock took 15-32 ms a picture on the board (0.5.6,
// 2026-11-20: glass with its soft shadow, icons shrunk from their pictures, pills, text). Now each is made when what it
// shows changes: drawn once over black and once over white, which gives every pixel's colour and how much it covers;
// kept (PSRAM) as runs of covered pixels, line by line. A picture only mixes those runs onto the water. What changes
// all the time (the coins, the bars' fill, the badges) is drawn on top as before. The fish card is one too.
struct LRun {
  uint16_t at, n;  // from the layer's left, pixels; n | 0x8000: one colour and cover for all n (one entry)
  uint32_t off;    // into c / a
};
struct Layer {
  uint32_t key = 0;
  int x0 = 0, y0 = 0, h = 0;  // frame pixels
  uint8_t* mem = nullptr;     // one block (PSRAM): line, runs, c, a
  uint32_t* line = nullptr;   // h + 1: each line's first run
  LRun* runs = nullptr;
  uint16_t* c = nullptr;  // the layer over black
  uint8_t* a = nullptr;   // cover 0..32
};
Layer barL, numL, dockL[5], cardL, needL[3];
#if TANK_QA
int layerMade = 0, layerNoRoom = 0;
#endif
void layerFree(Layer& m) {
  heap_caps_free(m.mem);
  m = Layer();
}
void hudFree() {
  layerFree(barL), layerFree(numL), layerFree(cardL);
  for (Layer& m : needL) layerFree(m);
  for (Layer& m : dockL) layerFree(m);
}

// Drawn over black (k) and over white (w): k stays as it is (the layer over black: its colour, already weighted by its
// cover), w becomes the cover 0..32 (how much less of white shows through than black's 0).
void unmix(const uint16_t* k, uint16_t* w, int n) {
  for (int i = 0; i < n; i++) {
    const uint16_t b = k[i], v = w[i];
    if (b == v) {  // the same over both: nothing shows through
      w[i] = 32;
      continue;
    }
    if (!b && v == 0xFFFF) {  // untouched
      w[i] = 0;
      continue;
    }
    const int dr = (int)(v >> 11) - (b >> 11), dg = (int)(v >> 5 & 63) - (b >> 5 & 63), db = (int)(v & 31) - (b & 31);
    const float see = (dr / 31.0f + 2 * dg / 63.0f + db / 31.0f) / 4;  // green has the finer steps
    w[i] = (uint16_t)std::max(0, std::min(32, (int)lroundf((1 - see) * 32)));
  }
}
using LayerDraw = void (*)(const Tgt& t, int a, void* ctx);
// Made 16 rows at a time (small buffers: the board's PSRAM is mostly taken by then), counted once, then kept in one
// block. Six or more pixels alike (the glass's inside) are one entry: putting them reads nothing.
bool layerMake(Layer& m, float x, float y, float w, float h, LayerDraw draw, void* ctx) {
  layerFree(m);
  const float S = sc->SC;
  const int X0 = std::max(0, (int)floorf(x * S)), Y0 = std::max(0, (int)floorf(y * S));
  const int X1 = std::min(sc->FW, (int)ceilf((x + w) * S)), Y1 = std::min(sc->FH, (int)ceilf((y + h) * S));
  const int W = X1 - X0, H = Y1 - Y0;
  constexpr int SH = 16;
  if (W <= 0 || H <= 0) return false;
  uint16_t* k = (uint16_t*)heap_caps_malloc(W * SH * 4, MALLOC_CAP_SPIRAM);
  if (!k) return false;
  uint16_t* wt = k + W * SH;
  uint32_t nr = 0, np = 0;
  for (int pass = 0; pass < 2; pass++) {
    uint32_t r = 0, o = 0;
    for (int s0 = 0; s0 < H; s0 += SH) {
      const int sh = std::min(SH, H - s0);
      memset(k, 0, W * sh * 2), memset(wt, 0xFF, W * sh * 2);
      Tgt c = {k, 1, W, X0, Y0 + s0, X1, Y0 + s0 + sh};
      draw(c, 256, ctx);
      c.p = wt;
      draw(c, 256, ctx);
      unmix(k, wt, W * sh);
      for (int y = 0; y < sh; y++) {
        if (pass) m.line[s0 + y] = r;
        const uint16_t *K = k + y * W, *V = wt + y * W;
        for (int X = 0; X < W;) {
          if (!V[X]) {
            X++;
            continue;
          }
          int e = X + 1;
          while (e < W && V[e] == V[X] && K[e] == K[X]) e++;
          if (e - X >= 6) {  // alike: one entry
            if (pass) m.runs[r] = {(uint16_t)X, (uint16_t)(0x8000 | (e - X)), o}, m.c[o] = K[X], m.a[o] = (uint8_t)V[X];
            r++, o++, X = e;
            continue;
          }
          int b = e;  // pixel by pixel, up to a gap or the next six alike
          while (b < W && V[b]) {
            int q = b + 1;
            while (q < W && V[q] == V[b] && K[q] == K[b]) q++;
            if (q - b >= 6) break;
            b = q;
          }
          if (pass) {
            m.runs[r] = {(uint16_t)X, (uint16_t)(b - X), o};
            for (int i = X; i < b; i++) m.c[o + i - X] = K[i], m.a[o + i - X] = (uint8_t)V[i];
          }
          r++, o += b - X, X = b;
        }
      }
    }
    if (!pass) {
      nr = r, np = o;
      const size_t lb = (H + 1) * 4, rb = nr * sizeof(LRun), cb = (np * 2 + 3) & ~3u;
      m.mem = (uint8_t*)heap_caps_malloc(lb + rb + cb + np + 4, MALLOC_CAP_SPIRAM);
      if (!m.mem) {
        heap_caps_free(k);
#if TANK_QA
        layerNoRoom++;
#endif
        return false;
      }
      m.line = (uint32_t*)m.mem, m.runs = (LRun*)(m.mem + lb), m.c = (uint16_t*)(m.mem + lb + rb), m.a = m.mem + lb + rb + cb;
    }
  }
  m.line[H] = nr;
  m.x0 = X0, m.y0 = Y0, m.h = H;
  heap_caps_free(k);
#if TANK_QA
  layerMade++;
#endif
  return true;
}
// what is behind at 32 - w parts in 32, plus the layer's own (weighted) colour; a part pushed one step over its top
// (the cover is rounded) is held at the top
__attribute__((always_inline)) inline uint16_t over(uint32_t own, uint16_t d, int w) {
  uint32_t s = own + ((ex(d) * (uint32_t)(32 - w)) >> 5 & 0x07E0F81Fu);
  if (const uint32_t o = s & 0x08010020u) s |= (o & 0x20 ? 0x1Fu : 0) | (o & 0x10000 ? 0xF800u : 0) | (o & 0x08000000 ? 0x07E00000u : 0);
  return pk(s & 0x07E0F81Fu);
}
#if TANK_QA
uint32_t putUs = 0, putPx = 0;  // what layerPut costs a pixel on the board (the stats line)
#endif
HOT void layerPut(const Tgt& t, const Layer& m, int ga) {
  const int Y0 = std::max(t.y0, m.y0), Y1 = std::min(t.y1, m.y0 + m.h), g = (ga + 4) >> 3;
  if (g <= 0) return;
#if TANK_QA
  const uint32_t u0 = us();
  struct Done {
    uint32_t u0;
    ~Done() { putUs += us() - u0; }
  } done{u0};
#endif
  for (int Y = Y0; Y < Y1; Y++)
    for (uint32_t r = m.line[Y - m.y0], e = m.line[Y - m.y0 + 1]; r < e; r++) {
      const LRun& R = m.runs[r];
      const int a0 = m.x0 + R.at, X0 = std::max(t.x0, a0), X1 = std::min(t.x1, a0 + (R.n & 0x7FFF));
      if (X0 >= X1) continue;
#if TANK_QA
      putPx += X1 - X0;
#endif
      uint16_t* d = t.at(X0, Y);
      if (R.n & 0x8000) {  // one colour and cover: nothing to read
        const uint16_t c = m.c[R.off];
        int w = m.a[R.off];
        uint32_t own = ex(c);
        if (g < 32) own = (own * (uint32_t)g) >> 5 & 0x07E0F81Fu, w = (w * g + 16) >> 5;
        if (g >= 32 && w >= 32)
          for (int X = X0; X < X1; X++, d += t.dx) *d = c;
        else
          for (int X = X0; X < X1; X++, d += t.dx) *d = over(own, *d, w);
        continue;
      }
      const uint16_t* C = m.c + R.off + (X0 - a0);
      const uint8_t* A = m.a + R.off + (X0 - a0);
      if (g >= 32)
        for (int X = X0; X < X1; X++, d += t.dx, C++, A++) *d = *A >= 32 ? *C : over(ex(*C), *d, *A);
      else  // the needs pulsing: the layer's colour and cover both at g / 32
        for (int X = X0; X < X1; X++, d += t.dx, C++, A++) *d = over((ex(*C) * (uint32_t)g) >> 5 & 0x07E0F81Fu, *d, (*A * g + 16) >> 5);
    }
}
// The layer for key (made when the key changes), put at cover a; drawn straight when there is no room for it.
// A layer at part cover is the whole group seen through (its icon solid on its glass), where the page fades each
// piece by itself (the glass shows through the icon): while a < 250 the pieces are drawn straight, unless the layer
// holds one kind of piece only (one: the needs' glasses, icons, dots), which fades the same either way.
void layerAt(const Tgt& t, Layer& m, uint32_t key, float x, float y, float w, float h, int a, LayerDraw draw, void* ctx, bool one) {
#if TANK_QA
  if (layersOff) return draw(t, a, ctx);
#endif
  if (a < 250 && !one) return draw(t, a, ctx);
  if (m.key != key) layerMake(m, x, y, w, h, draw, ctx), m.key = key;  // (no room: tried once per key)
  if (m.line) layerPut(t, m, a);
  else draw(t, a, ctx);
}
template <class F>
inline void layer(const Tgt& t, Layer& m, uint32_t key, float x, float y, float w, float h, int a, const F& draw, bool one = false) {
  layerAt(t, m, key, x, y, w, h, a, [](const Tgt& c, int ca, void* p) { (*(const F*)p)(c, ca); }, (void*)&draw, one);
}

// ---------------------------------------------------------------- what the bars show, once a picture
// The board on its side asks for a picture in six pieces and draw() runs for each: the game's numbers are read once a
// picture, and the ones that cost (the comfort looks at every fish, the jobs and the lamp at the clock) twice a second.
struct HudNow {
  uint32_t at = 0xFFFFFFFF, slowAt = 0;
  int coins = -1, level = -1, comfort = 0, tasks = 0;
  float span = 0, water = 0;
  bool lamp = true;
  String coinsS, levelS;
} hn;
const HudNow& hud() {
  if (hn.at == gfx::frameNo()) return hn;
  hn.at = gfx::frameNo();
  const TankRec& T = game::T();
  if (hn.coins != game::coins()) hn.coins = game::coins(), hn.coinsS = String(hn.coins);
  if (hn.level != game::level()) hn.level = game::level(), hn.levelS = "Cấp " + String(hn.level);
  hn.span = game::levelSpan();
  if (millis() - hn.slowAt > 500 || !hn.slowAt)
    hn.slowAt = millis() | 1, hn.comfort = game::comfort(T), hn.tasks = game::tasksReady(), hn.water = game::waterWait(), hn.lamp = game::lampOn(T);
  return hn;
}
void hudSoon() { hn.slowAt = 0; }  // after a tap that changes them (feeding, the lamp, a job paid)

// ---------------------------------------------------------------- drawing pieces
void barTrack(const Tgt& t, float x, float y, float w, int a) { rrect(t, x, y, w, 7, 3.5f, H(0x142840), a * 41 / 256); }  // rgba(20,40,60,.16)
void barFill(const Tgt& t, float x, float y, float w, float v, uint32_t col, int a) {
  const float k = std::max(0.08f, std::min(1.0f, v / 100));
  rrect(t, x, y, w * k, 7, 3.5f, v < 30 ? (fsin(pulse * 6) > 0 ? H(0xe5484d) : H(0xf08a8d)) : H(col), a);
}
void drawDock(const Tgt& t, const Lay& L, int a) {
  static const char* const ICON[5] = {"i_food", "i_sponge", "i_bucket", "i_lamp", "i_shop"};
  const TankRec& T = game::T();
  for (int i = 0; i < 5; i++) {
    const float x = L.dx[i], y = L.dy, k = pressed == i ? 0.9f : 1, r = L.dr * k;
    bool dim = false, badge = false, good = false;
    if (i == 4) good = hud().tasks > 0;
    if (i == 0) badge = T.food < 35;
    else if (i == 1) badge = T.algae > 45;
    else if (i == 2) badge = T.clean < 45, dim = hud().water > 0 && !badge;
    else if (i == 3) dim = !hud().lamp;
    const int v[] = {i, pressed == i, dim, (int)L.W, (int)L.H, (int)lroundf(sc->SC * 100)};
    layer(t, dockL[i], fnv(2166136261u, v, sizeof v) | 1, x - r - 12, y - r - 12, 2 * r + 24, 2 * r + 26, a,
          [&](const Tgt& c, int ca) {
            dockButton(c, x, y, r, ca);
            fit(c, thing(ICON[i]), x - 18 * k, y - 18 * k, 36 * k, 36 * k, dim ? ca * 115 / 256 : ca);
          });
    if (badge || good) {
      const float rr = 5.5f + fsin(pulse * 5);
      circle(t, x + 17, y - 17, rr + 0.6f, 0xFFFF, a);
      circle(t, x + 17, y - 17, rr - 0.6f, badge ? H(0xe5484d) : H(0x2fae5b), a);
    }
  }
}
void drawToasts(const Tgt& t, const Lay& L) {
  float y = 44;
  for (Toast& q : toasts) {
    const float a = std::min(1.0f, std::min(q.t * 6, (2.8f - q.t) * 3));
    if (!q.w) {
      wrap2(q.s.c_str(), 12, true, L.W - 44, q.l0, q.l1);
      q.w = std::min(L.W - 16, std::max(textW(q.l0.c_str(), 12, true), textW(q.l1.c_str(), 12, true)) + 26), q.h = q.l1.length() ? 40 : 26;
    }
    const String &l0 = q.l0, &l1 = q.l1;
    const float w = q.w, h = q.h;
    rrect(t, L.W / 2 - w / 2, y, w, h, 13, H(0x081420), A(a * 0.8f));
    text(t, l0.c_str(), L.W / 2, y + 13.5f, 12, true, 0xFFFF, 1, A(a));
    if (l1.length()) text(t, l1.c_str(), L.W / 2, y + 28.5f, 12, true, 0xFFFF, 1, A(a));
    y += h + 4;
  }
}
// The chooser's icon: a few pieces of that food (water.cpp foodPiece), bigger, untinted: a heap of flakes, a heap of
// granules, three bloodworms.
void foodArt(const Tgt& t, int kind, float x, float y) {
  static const int8_t AT[3][7][2] = {{{-7, -7}, {6, -8}, {-1, -1}, {-9, 5}, {8, 3}, {1, 9}, {-2, -9}},
                                     {{-8, 6}, {0, 7}, {8, 6}, {-4, -1}, {4, -1}, {0, -8}, {0, 0}},
                                     {{-1, -7}, {1, 0}, {-1, 7}}};
  const float S = sc->SC;
  for (int i = 0; i < (kind == F_live ? 3 : kind == F_sink ? 6 : 7); i++)  // (seeds: the worms lie across, the flakes mostly warm)
    foodPiece(t, kind, (x + AT[kind][i][0]) * S, (y + AT[kind][i][1]) * S, (kind == F_live ? 2.0f : 2.3f) * S, 0.6f,
              kind == F_live ? i * 1.5708f + 0.71f : kind == F_flake ? i * 0.56f + 0.01f : i * 1.37f + 0.41f, plain, 1);
}
void drawFoodPick(const Tgt& t, const Lay& L) {
  const TankRec& T = game::T();
  int ds[F_N];
  const int n = game::diets(T, ds);
  const float x0 = L.foodX;
  dim(t, 0, 0, L.W, L.H, H(0x040e18), A(0.28f));
  for (int i = 0; i < n; i++) {
    const float y = L.foodY[i];
    const int k = ds[i], cost = D.R.dietCost[k];
    glass(t, x0, y - 24, 154, 48, 24, 240);
    circle(t, x0 + 24, y, 20, H(0xe4f1f8), 256);
    foodArt(t, k, x0 + 24, y);
    text(t, (String(D.R.dietVi[k]) + (cost ? "  " + String(cost) + " xu" : "")).c_str(), x0 + 52, y - 7, 13, true, H(0x1c2a38));
    barTrack(t, x0 + 52, y + 6, 88, 256), barFill(t, x0 + 52, y + 6, 88, T.fed[k], 0x4fb84a, 256);
  }
  text(t, "Mỗi loài ăn một loại", x0 + 77, L.foodY[n - 1] - 38, 12, true, 0xFFFF, 1);
}
void btn(const Tgt& t, float bx, float y, float w, const String& label, uint32_t col, uint32_t ink) {
  rrect(t, bx, y + 82, w, 30, 15, H(col), 256);
  text(t, label.c_str(), bx + w / 2, y + 97.5f, 13, true, H(ink), 1);
}
void cardBody(const Tgt& t, const Lay& L, const FishRec* d);
void drawCard(const Tgt& t, const Lay& L) {
  const FishRec* d = cardFish();
  if (!d) return;
  const bool adult = game::isAdult(*d);
  const std::vector<String>& why = game::reasons(d->uid);
  const int v[] = {(int)d->uid, d->sp, adult, adult ? 0 : (int)lroundf(d->age / D.R.adultH * 100), d->sick, game::sellPrice(*d), D.R.cureCost,
                   (int)L.W, (int)L.H, (int)lroundf(sc->SC * 100)};
  uint32_t key = fnv(2166136261u, v, sizeof v);
  for (const String& w : why) key = fnv(key, w.c_str(), w.length() + 1);
  layer(t, cardL, key | 1, L.cardX - 12, L.cardY - 12, L.cardW + 24, L.cardH + 26, 256, [&](const Tgt& c, int) { cardBody(c, L, d); });
}
void cardBody(const Tgt& t, const Lay& L, const FishRec* d) {
  const Species& s = D.sp[d->sp];
  const bool adult = game::isAdult(*d);
  const std::vector<String>& why = game::reasons(d->uid);
  const int val = std::max(1, (int)lroundf(s.val * (adult ? 1 : D.R.coinYoung)));
  const float x = L.cardX, y = L.cardY;
  glass(t, x, y, L.cardW, L.cardH, 16, 245);
  rrect(t, x + 8, y + 8, 76, 54, 10, H(0xd9ecf5), 256);
  fit(t, spr(s.marine ? P_MARINE : P_FRESH, s.pic), x + 11, y + 11, 70, 48);
  text(t, s.vi, x + 94, y + 18, 15, true, H(0x1c2a38));
  const String age = adult ? String("Trưởng thành") : "Cá non, lớn " + String((int)lroundf(d->age / D.R.adultH * 100)) + "%";
  text(t, fit1((age + " \xC2\xB7 " + game::roleVi(s.role)).c_str(), 12, false, 204).c_str(), x + 94, y + 35, 12, false, H(0x566575));
  if (why.size()) {
    String all = "Buồn: ";
    for (size_t i = 0; i < why.size(); i++) {
      String w = why[i];
      // the page lowers the first letter; the reasons start with a capital (Đ, B, N, C, T, S)
      if (w.startsWith("\xC4\x90")) w = "\xC4\x91" + w.substring(2);
      else if (w.length() && w[0] >= 'A' && w[0] <= 'Z') w.setCharAt(0, w[0] + 32);
      all += (i ? ", " : "") + w;
    }
    String l0, l1;
    wrap2(all.c_str(), 12, false, 204, l0, l1);
    text(t, l0.c_str(), x + 94, y + 51, 12, false, H(0xd6453d));
    if (l1.length()) text(t, l1.c_str(), x + 94, y + 66, 12, false, H(0xd6453d));
  } else {
    text(t, s.val ? ("Đang vui \xC2\xB7 nhả " + String(val) + " xu mỗi lần").c_str() : s.eats == 2 ? "Đang vui \xC2\xB7 ăn rêu trên kính" : "Đang vui \xC2\xB7 nhặt thức ăn thừa",
         x + 94, y + 51, 12, false, H(0x2b8a4b));
    text(t, fit1(game::traits(s).c_str(), 12, false, 204).c_str(), x + 94, y + 66, 12, false, H(0x667585));
  }
  if (d->sick) {
    btn(t, x + 10, y, 92, "Bán +" + String(game::sellPrice(*d)), 0xf2b824, 0x3a2c06);
    btn(t, x + 106, y, 92, "Chữa " + String(D.R.cureCost) + " xu", 0x2fae5b, 0xffffff);
    btn(t, x + 202, y, 92, "Đóng", 0xdfe6ee, 0x243140);
  } else {
    btn(t, x + 12, y, 136, "Bán +" + String(game::sellPrice(*d)) + " xu", 0xf2b824, 0x3a2c06);
    btn(t, x + 156, y, 136, "Đóng", 0xdfe6ee, 0x243140);
  }
}
// the corners of a box, rounded off by painting the page's colour over what lies outside them
void corners(const Tgt& t, float x, float y, float w, float h, float r, uint16_t bg) {
  Tgt c = t;
  for (int k = 0; k < 4; k++) {
    const float cx = k & 1 ? x + w - r : x, cy = k & 2 ? y + h - r : y;
    // a square r x r: paint where it lies outside the circle of radius r round the inner corner point
    const float ox = k & 1 ? x + w - r : x + r, oy = k & 2 ? y + h - r : y + r;
    for (float py = cy; py < cy + r; py += 1)
      for (float px = cx; px < cx + r; px += 1) {
        const float dx = px + 0.5f - ox, dy = py + 0.5f - oy, out = sqrtf(dx * dx + dy * dy) - r + 0.5f;
        if (out > 0) rrect(c, px, py, 1, 1, 0, bg, (int)(std::min(1.0f, out) * 256));
      }
  }
}
void drawSheetHead(const Tgt& t, const Lay& L) {
  const float sx = L.shX, sy = L.shY, sw = L.shW;
  const TankRec& T = game::T();
  if (L.land) rrect(t, sx, sy, sw, L.shH, 0, H(0xf6f8fb), 256);
  else rrect(t, sx, sy, sw, L.shH + 20, 16, H(0xf6f8fb), 256);
  int have, all;
  game::albumCount(have, all);
  const String names[7] = {!T.marine ? "Cá nước ngọt" : "Cá nước mặn", !T.marine ? "Cây thủy sinh" : "San hô", "Đồ trang trí", "Máy móc",
                           "Kiểu hồ", "Sổ cá " + String(have) + " / " + String(all), "Việc hôm nay"};
  text(t, names[tab].c_str(), sx + 14, sy + 20, 15, true, H(0x1c2a38));
  const String cs = String(game::coins());
  const float cw = textW(cs.c_str(), 13, true);
  text(t, cs.c_str(), sx + sw - 46, sy + 20.5f, 13, true, H(0x3a2c06), 2);
  fit(t, thing("s_coin"), sx + sw - 68 - cw, sy + 11, 18, 18);
  line(t, sx + sw - 27, sy + 13, sx + sw - 13, sy + 27, 2, H(0x6b7a8a), 256);
  line(t, sx + sw - 13, sy + 13, sx + sw - 27, sy + 27, 2, H(0x6b7a8a), 256);
  if (tab == 0) {
    String u = game::used(T) == (int)game::used(T) ? String((int)game::used(T)) : String(game::used(T), 1);
    text(t, ("Chỗ " + u + "/" + String(game::capacity(T))).c_str(), sx + sw - 76 - cw, sy + 20.5f, 12, false, H(0x566575), 2);
  }
  TabR tr[7];
  tabRects(L, tr);
  for (int i = 0; i < 7; i++) {
    const bool on = i == tab;
    rrect(t, tr[i].x, tr[i].y, tr[i].w, tr[i].h, 13, on ? H(0x2f6fb5) : H(0xe3e9f0), 256);
    text(t, tr[i].name, tr[i].x + tr[i].w / 2, tr[i].y + 13.5f, 12, true, on ? 0xFFFF : H(0x3c4a5a), 1);
    if (i == 6 && !on && hud().tasks) circle(t, tr[i].x + tr[i].w - 3, tr[i].y + 3, 4, H(0x2fae5b), 256);
  }
}

// One row of the list, its top at y (logical).
void drawRow(const Tgt& c, const Lay& L, size_t i, float y) {
  const float sx = L.shX, sw = L.shW;
  {
    const game::Row& r = rows[i];
    const Fit3& f = fitted[i];
    const float tx = sx + 74;
    rrect(c, sx + 8, y + 3, sw - 16, ROW_H - 6, 12, H(0xe3e9f0), 256);
    rrect(c, sx + 9, y + 4, sw - 18, ROW_H - 8, 11, 0xFFFF, 256);
    rrect(c, sx + 14, y + 8, 52, ROW_H - 16, 9, r.look ? H(0x0c2233) : H(0xdcedf6), 256);
    const int pa = r.dim && !r.shade ? 115 : 256;
    if (r.up >= 0) {
      static const char* const ALT[NUP] = {nullptr, nullptr, "i_lamp", "bubbler", "i_food", nullptr, nullptr, nullptr};
      if (ALT[r.up]) fit(c, thing(ALT[r.up]), sx + 18, y + 11, 44, ROW_H - 22, pa);
      else fit(c, sprNamed(P_UI, (String("g_") + D.up[r.up].id).c_str()), sx + 14, y + 8, 52, ROW_H - 16, pa);
    } else if (r.look) {
      blitScaled(c, spr(P_THUMBS, r.art), sx + 14, y + 8, 52, ROW_H - 16, pa);
      corners(c, sx + 14, y + 8, 52, ROW_H - 16, 9, 0xFFFF);
    } else fit(c, spr(r.artPack, r.art), sx + 17, y + 11, 46, ROW_H - 22, pa, r.shade);
    if (r.count) {
      const String s = "\xC3\x97" + String(r.count);
      const float w = textW(s.c_str(), 12, true) + 10;
      rrect(c, sx + 66 - w, y + ROW_H - 26, w, 17, 8.5f, H(0x2f6fb5), 256);
      text(c, s.c_str(), sx + 66 - w / 2, y + ROW_H - 17, 12, true, 0xFFFF, 1);
    }
    text(c, f.name.c_str(), tx, y + 17, 13, true, r.dim ? H(0x8a96a3) : H(0x1c2a38));
    text(c, f.sub.c_str(), tx, y + 33, 12, false, H(0x667585));
    if (f.note.length()) text(c, f.note.c_str(), tx, y + 50, 12, false, r.bad ? H(0xd6453d) : r.warn ? H(0xc77700) : H(0x8a96a3));
    const float bx = sx + sw - 90;
    if (r.paid) {  // locked after the trial (lock.h): a dark pill with a padlock, pressing shows the way to unlock
      rrect(c, bx, y + 9, 78, 28, 14, H(0x1d1f24), 256);
      const float cx = bx + 39, cy = y + 23;
      ring(c, cx, cy - 3, 4.2f, 1.7f, 0xFFFF, 256);
      rrect(c, cx - 6, cy - 2, 12, 9, 2, 0xFFFF, 256);
      circle(c, cx, cy + 2, 1.3f, H(0x1d1f24), 256);
    } else if (r.price >= 0 && !r.tag.length()) {
      rrect(c, bx, y + 9, 78, 28, 14, r.can ? H(0xf2b824) : H(0xe6dcc0), 256);
      fit(c, thing("s_coin"), bx + 6, y + 14, 18, 18);
      text(c, String(r.price).c_str(), bx + 28, y + 23.5f, 13, true, r.can ? H(0x3a2c06) : H(0x9a8c66));
    } else if (r.tag.length()) {
      if (r.can) rrect(c, bx, y + 9, 78, 28, 14, r.tag == "Nhận" ? H(0x2fae5b) : H(0x2f6fb5), 256);
      text(c, r.tag.c_str(), bx + 39, y + 23.5f, 12, true, r.can ? 0xFFFF : H(0x8a96a3), 1);
    }
  }
}

// The whole sheet drawn straight (when no PSRAM for its pictures): the head, then the list clipped to its window.
void drawSheet(const Tgt& t, const Lay& L) {
  drawSheetHead(t, L);
  Tgt c = t;
  const int S0 = (int)lroundf((L.listY - 2) * sc->SC), S1 = (int)lroundf((L.shY + L.shH) * sc->SC);
  c.y0 = std::max(t.y0, S0), c.y1 = std::min(t.y1, S1);
  if (c.y0 >= c.y1) return;
  c.p = t.at(t.x0, c.y0);
  for (size_t i = 0; i < rows.size() && i < fitted.size(); i++) {
    const float y = L.listY + i * ROW_H - scroll;
    if (y <= L.H && y >= L.listY - ROW_H) drawRow(c, L, i, y);
  }
}

}  // namespace

// ---------------------------------------------------------------- where things are, for the screen as it is held
Lay lay() {
  Lay L;
  L.W = sc ? sc->VW : 320, L.H = sc ? sc->VH : 480, L.land = L.W > L.H;
  L.barX = roundf((L.W - 304) / 2), L.barY = 6, L.barW = 304, L.barH = 30;
  L.dy = L.H - 36, L.dr = 25;
  for (int i = 0; i < 5; i++) L.dx[i] = L.W / 2 + (i - 2) * 62;
  if (L.land) L.shX = L.W - 320, L.shY = 0, L.shW = 320, L.shH = L.H;
  else L.shX = 0, L.shY = 62, L.shW = 320, L.shH = L.H - 62;
  L.listY = L.shY + 76;
  L.cardX = roundf((L.W - 304) / 2), L.cardY = L.H - 126, L.cardW = 304, L.cardH = 120;
  L.foodX = L.dx[0] - 24, L.foodY[0] = L.dy - 62, L.foodY[1] = L.dy - 118, L.foodY[2] = L.dy - 174;
  return L;
}

void say(const String& s) {
  if (!s.length() || (toasts.size() && toasts.back().s == s)) return;
  toasts.push_back({s, 0});
  if (toasts.size() > 3) toasts.erase(toasts.begin());
}
void fx(const char* k, int v) {
  if (!strcmp(k, "water")) fxNow = {1, 0};
  else if (!strcmp(k, "level")) fxNow = {2, 0};
  else if (!strcmp(k, "coin")) floats.push_back({"+" + String(v), lastTapX, lastTapY, 0});
}
void wake() { quiet = 0; }
#if TANK_QA
void devOpen(const char* what, int arg) {  // the dev console: a screen straight away (shots)
  if (!strcmp(what, "scroll")) {
    scroll = clampScroll(arg);
    return;
  }
  reset();
  wake();
  show = 1;
  if (!strcmp(what, "card") && sc && sc->nfish) {
    const Fish* f = &sc->fish[0];
    for (int i = 0; i < sc->nfish; i++)
      if (game::fishRec(sc->fish[i].uid) && (arg < 0 || game::fishRec(sc->fish[i].uid)->sick)) f = &sc->fish[i];
    card = true, cardUid = f->uid;
  } else if (!strcmp(what, "tab")) sheet = true, tab = std::max(0, std::min(6, arg)), scroll = 0, refreshRows();
  else if (!strcmp(what, "food")) foodPick = true;
  else if (!strcmp(what, "sponge")) sponge = true;
}
#endif
bool busy() { return sheet || card || sponge || foodPick; }
void sheetFree();
void fzFree();
void reset() { sheet = card = sponge = foodPick = false, P.on = false, pressed = -1, chipFree(), chipT = 99, sheetFree(), fzFree(), hudFree(); }

void cover(float& x0, float& y0, float& x1, float& y1) {  // the sheet hides the water under it
  x0 = y0 = x1 = y1 = 0;
  if (!sheet) return;
  const Lay L = lay();
  if (L.land) x0 = L.shX, y0 = 0, x1 = L.W, y1 = L.H;
  else x0 = 0, y0 = L.shY + 16, x1 = L.W, y1 = L.H;
}

// ---------------------------------------------------------------- the pointer
void down(float x, float y) {
  const bool wasHidden = show < 0.5f && !sheet && !card;
  wake();
  lastTapX = x, lastTapY = y;
  P = Press();
  P.on = true, P.x = x, P.y = y, P.lastY = y, P.sy = scroll, P.wasHidden = wasHidden;
  P.k = hit(x, y, P.i, P.row, P.btn);
  fling = 0;
  if (P.k == H_DOCK) pressed = P.i;
  if (P.k == H_WIPE) game::wipeAt(x, y), spX = x, spY = y;
}
void move(float x, float y) {
  if (!P.on) return;
  if (hypotf(x - P.x, y - P.y) > 6) P.moved = true;
  wake();
  if (P.k == H_LIST) {
    P.vel = P.vel * 0.5f + (P.lastY - y) * 0.5f, P.lastY = y;
    scroll = clampScroll(P.sy - (y - P.y));
  } else if (P.k == H_WIPE) game::wipeAt(x, y), spX = x, spY = y;
  else if (P.k == H_WATER && sc && sc->fingerOn) sc->fingerX = x, sc->fingerY = y;
}
void up(float x, float y) {
  hudSoon();  // a tap may feed, light, pay a job: the bars read the game again
  if (!P.on) return;
  P.on = false, pressed = -1;
  if (sc && sc->fingerOn) {
    sc->fingerOn = false;
    return;
  }
  const int k = P.k;
  if (k == H_LIST && P.moved) fling = P.vel * 30;  // let the list run on a little (units a second)
  if (k == H_WIPE) {
    if (game::T().algae < 6) game::wipeDone(), sponge = false;
    return;
  }
  if (P.moved && k != H_DOCK) return;
  switch (k) {
    case H_CLOSE: sheet = false; return;
    case H_CARDCLOSE: card = false; return;
    case H_SPONGEOFF: sponge = false; return;
    case H_FOOD: foodPick = false, game::feed(P.i); return;
    case H_FOODCLOSE: foodPick = false; return;
    case H_CURE: game::cure(cardUid); return;
    case H_TAB: tab = P.i, scroll = 0, refreshRows(); return;
    case H_LIST: {
      if (P.row < 0 || P.row >= (int)rows.size() || !P.btn) return;
      const game::Row r = rows[P.row];
      if (r.act > 0 && r.can) game::press(r);
      else if (r.act > 0 && r.tag != "")
        say(r.bad ? r.note : r.tag.startsWith("Cấp") ? "Cần " + String("c") + r.tag.substring(1)
            : r.price >= 0 && game::coins() < r.price ? String("Chưa đủ xu") : r.tag == "Xong" ? String("Đã nhận rồi") : String("Chưa được"));
      refreshRows();
      return;
    }
    case H_SELL:
      if (card) game::sellFish(cardUid), card = false;
      return;
    case H_DOCK: {
      const int i = P.i;
      const TankRec& T = game::T();
      if (i == 0) {
        int ds[F_N];
        if (game::diets(T, ds) > 1) foodPick = true;
        else game::feed(F_flake);
      } else if (i == 1) {
        if (T.algae < 1.5f) say("Kính đang sạch");
        else sponge = true, spX = sc->VW / 2, spY = sc->VH / 2;
      } else if (i == 2) game::changeWater();
      else if (i == 3) game::setLamp(!T.lamp);
      else sheet = true, scroll = 0, refreshRows();
      return;
    }
    case H_WATER: {
      if (game::collect(x, y)) return;
      if (P.wasHidden) return;
      Fish* f = sc ? sc->fishAt(x, y) : nullptr;
      if (f && f->uid && game::fishRec(f->uid)) {
        card = true, cardUid = f->uid;
        return;
      }
      if (sc) sc->tapGlass(x, y);
      game::tapped();
      return;
    }
  }
}

// ---------------------------------------------------------------- time
void tick(float dt) {
  pulse += dt;
  if ((chipT += dt) > 2 && !saver) chipT = 0, chipMake();
  if (P.on) {
    P.t += dt;
    if (P.k == H_WATER && !P.moved && P.t > 0.4f && sc && !sc->fingerOn && !saver) sc->fingerOn = true, sc->fingerX = P.x, sc->fingerY = P.y;
    quiet = 0;
  }
  if (!busy()) quiet += dt;
  else quiet = 0;
  const float want = saver ? 0 : quiet > 9 ? 0 : 1;
  show += (want - show) * std::min(1.0f, dt * (want ? 9 : 2.5f));
  for (Toast& q : toasts) q.t += dt;
  while (toasts.size() && toasts.front().t >= 2.8f) toasts.erase(toasts.begin());
  for (Float& f : floats) f.t += dt;
  while (floats.size() && floats.front().t >= 1) floats.erase(floats.begin());
  if (fxNow.k) {
    fxNow.t += dt;
    if (fxNow.t > (fxNow.k == 1 ? 2.6f : 1.4f)) fxNow.k = 0;
    else if (fxNow.k == 1 && sc && sc->nbub < MAXBUB && (float)rand() / RAND_MAX < dt * 40)
      sc->bub[sc->nbub++] = {(float)rand() / RAND_MAX * sc->VW, 60 + (float)rand() / RAND_MAX * (sc->VH - 120),
                             0.8f + (float)rand() / RAND_MAX * 2, 40 + (float)rand() / RAND_MAX * 40, (float)rand() / RAND_MAX * 6};
  }
  if (card && !cardFish()) card = false;
  if (sheet && !P.on && fling != 0) {
    scroll = clampScroll(scroll + fling * dt);
    fling *= std::max(0.0f, 1 - dt * 4);
    if (fabsf(fling) < 5) fling = 0;
  }
  if (sheet && (int)(pulse * 2) != (int)((pulse - dt) * 2)) refreshRows(), scroll = clampScroll(scroll);
}

// ---------------------------------------------------------------- the shop sheet, cached
// Drawn every picture, band by band, the sheet took 30-160 ms on the board (2026-11-19), and scrolling drew it all
// again. Now its head (title, coins, tabs) and each row are pictures in PSRAM, made when what they show changes; a
// picture of the sheet is copies of them at the scroll's place (whole runs). Rows: the few last used are kept.
constexpr int NROWPIC = 8;
struct RowPic {
  uint32_t key = 0, used = 0;
  uint16_t* p = nullptr;
};
RowPic rowPics[NROWPIC];
uint16_t* hdBuf = nullptr;
uint32_t hdKey = 0, picClock = 0;
int shX0 = 0, shY0 = 0, shW = 0, hdH = 0, rowH = 0;  // frame pixels
constexpr uint32_t SHEET_BG = 0xf6f8fb;
void sheetFree() {
  heap_caps_free(hdBuf), hdBuf = nullptr, hdKey = 0;
  for (RowPic& r : rowPics) heap_caps_free(r.p), r = RowPic();
}

// The picture of row i (made when missing), nullptr when there is no room for it.
const uint16_t* rowPic(const Lay& L, size_t i) {
  const uint32_t key = rowKeys[i];
  RowPic* slot = &rowPics[0];
  for (RowPic& r : rowPics) {
    if (r.p && r.key == key) return r.used = ++picClock, r.p;
    if (!r.p || r.used < slot->used) slot = &r;
  }
  if (!slot->p && !(slot->p = (uint16_t*)heap_caps_malloc(shW * rowH * 2, MALLOC_CAP_SPIRAM))) return nullptr;
  const uint16_t bg = H(SHEET_BG);
  for (int k = 0; k < shW * rowH; k++) slot->p[k] = bg;
  const float S = sc->SC;
  Tgt c;  // the row drawn with its top at the list's top
  c.p = slot->p, c.dx = 1, c.dy = shW, c.x0 = shX0, c.x1 = shX0 + shW;
  c.y0 = (int)lroundf(L.listY * S), c.y1 = c.y0 + rowH;
  drawRow(c, L, i, L.listY);
  slot->key = key, slot->used = ++picClock;
  return slot->p;
}

void drawSheetCached(const Tgt& t, const Lay& L) {
  const float S = sc->SC;
  const int x0 = (int)lroundf(L.shX * S), y0 = (int)lroundf(L.shY * S), w = sc->FW - x0;
  const int lt = (int)lroundf((L.listY - 2) * S), rh = (int)lroundf(ROW_H * S);
  if (w <= 0 || lt <= y0) return;
  if (hdBuf && (shX0 != x0 || shY0 != y0 || shW != w || hdH != lt - y0 || rowH != rh)) sheetFree();
  shX0 = x0, shY0 = y0, shW = w, hdH = lt - y0, rowH = rh;
  if (!hdBuf && !(hdBuf = (uint16_t*)heap_caps_malloc(w * hdH * 2, MALLOC_CAP_SPIRAM))) return drawSheet(t, L);
  const TankRec& T = game::T();
  int have, all;
  game::albumCount(have, all);
  const int v[] = {tab, hud().coins, (int)(game::used(T) * 10), game::capacity(T), have, all, hud().tasks, T.marine, w};
  const uint32_t key = fnv(2166136261u, v, sizeof v) | 1;
  if (key != hdKey) {
    const uint16_t behind = H(0x0f2b33);  // the dimmed water, under the rounded corners' edge
    for (int i = 0; i < w * hdH; i++) hdBuf[i] = behind;
    Tgt c;
    c.p = hdBuf, c.dx = 1, c.dy = w, c.x0 = x0, c.y0 = y0, c.x1 = x0 + w, c.y1 = lt;
    drawSheetHead(c, L);
    hdKey = key;
  }
  const uint16_t bg = H(SHEET_BG);
  const int r = L.land ? 0 : (int)lroundf(16 * S);  // upright, the top corners keep the water drawn behind them
  const int Y0 = std::max(t.y0, y0), Y1 = std::min(t.y1, sc->FH), X0 = std::max(t.x0, x0), X1 = std::min(t.x1, x0 + w);
  if (X0 >= X1) return;
  const int scr = (int)lroundf(scroll * S), top = (int)lroundf(L.listY * S);
  size_t ri = (size_t)-1;
  const uint16_t* rp = nullptr;
  for (int Y = Y0; Y < Y1; Y++) {
    int a = X0, b = X1;
    const uint16_t* src = nullptr;
    if (Y < lt) {
      const int dy = Y - y0;
      if (dy < r) {
        const float q = r - dy - 0.5f;
        const int in = r - (int)sqrtf(std::max(0.0f, (float)r * r - q * q));
        a = std::max(a, x0 + in), b = std::min(b, x0 + w - in);
      }
      src = hdBuf + dy * w;
    } else {  // the list: the row under this line, at the scroll's place
      const int ly = Y - top + scr;
      const size_t i = ly >= 0 ? (size_t)(ly / rh) : (size_t)-1;
      if (i < rows.size() && i < fitted.size() && i < rowKeys.size()) {
        if (i != ri) ri = i, rp = rowPic(L, i);
        if (rp) src = rp + (ly - (int)i * rh) * w;
      }
    }
    if (a >= b) continue;
    uint16_t* d = t.at(a, Y);
    if (src) {
      src += a - x0;
      if (t.dx == 1) memcpy(d, src, (b - a) * 2);
      else
        for (int X = a; X < b; X++, d += t.dx) *d = *src++;
    } else {
      for (int X = a; X < b; X++, d += t.dx) *d = bg;
    }
  }
}

// ---------------------------------------------------------------- the water standing still under the sheet
// While the shop sheet is open, what shows beside it (the dimmed water, the top bar) is drawn once and kept (PSRAM):
// a picture is then that copy and the sheet, the water is neither worked out nor drawn (the board: smooth scrolling).
// Drawn again when the bar's numbers change; toasts and coin numbers go on top.
uint16_t* fzBuf = nullptr;
int fzState = 0, fzW = 0, fzH = 0;  // 0 off, 1 being captured this picture, 2 kept
uint32_t fzKey = 0;
void fzFree() { heap_caps_free(fzBuf), fzBuf = nullptr, fzState = 0; }
void fzRect(const Lay& L, int& w, int& h) {
  const float S = sc->SC;
  if (L.land) w = (int)lroundf(L.shX * S), h = sc->FH;
  else w = sc->FW, h = std::min(sc->FH, (int)lroundf((L.shY + 16) * S));
}
void fzCopy(const Tgt& t, bool in) {  // in: t -> the copy (capture), else the copy -> t
  const int X1 = std::min(t.x1, fzW), Y1 = std::min(t.y1, fzH);
  for (int Y = t.y0; Y < Y1; Y++) {
    if (t.x0 >= X1) break;
    uint16_t* d = t.at(t.x0, Y);
    uint16_t* m = fzBuf + Y * fzW + t.x0;
    if (t.dx == 1) in ? memcpy(m, d, (X1 - t.x0) * 2) : memcpy(d, m, (X1 - t.x0) * 2);
    else
      for (int X = t.x0; X < X1; X++, d += t.dx, m++) in ? (void)(*m = *d) : (void)(*d = *m);
  }
}

// ---------------------------------------------------------------- the drawing
#if TANK_QA
uint32_t prof[6];
#ifdef SIM
uint32_t us() { return (uint32_t)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
#else
uint32_t us() { return (uint32_t)esp_timer_get_time(); }
#endif
#endif
void draw(const Tgt& t) {
#if TANK_QA  // the screens' parts for the stats line
  uint32_t pt = us();
  auto mark = [&](int k) {
    const uint32_t n = us();
    prof[k] += n - pt, pt = n;
  };
#else
  auto mark = [](int) {};
#endif
  const Lay L = lay();
  const float W = L.W, Hh = L.H;
  if (fxNow.k == 1) {
    const float k = fxNow.t / 2.6f, yl = 50 + (Hh - 110) * (k < 0.5f ? k * 2 : 2 - k * 2);
    dim(t, 0, 0, W, yl, H(0xd2f0ff), A(0.16f));
    dim(t, 0, yl - 1, W, 2, 0xFFFF, A(0.7f));
  }
  const char* n[3];
  int nn;
  if (saver) {  // the standby face: a soft dark fade under the page's clock, the needs at the bottom left
    for (int y = 0; y < 190; y += 2) dim(t, 0, y, W, 2, H(0x000a18), A(0.5f * (1 - y / 190.0f)));
    needs(n, nn);
    for (int i = 0; i < nn; i++) {
      const int a = A(0.7f + 0.3f * fsin(pulse * 3));
      glass(t, 10 + i * 40, Hh - 42, 34, 34, 17, 204, a);
      fit(t, thing(n[i]), 14 + i * 40, Hh - 38, 26, 26, a);
    }
    return;
  }
  const TankRec& T = game::T();
  const int a = A(show);
  mark(5);
  if (show > 0.02f) {
    const float bx = L.barX, by = L.barY;
    const int v[] = {(int)L.W, (int)L.H, (int)lroundf(sc->SC * 100)};
    layer(t, barL, fnv(2166136261u, v, sizeof v) | 1, bx - 12, by - 12, L.barW + 24, L.barH + 26, a, [&](const Tgt& c, int ca) {
      glass(c, bx, by, L.barW, L.barH, 15, 219, ca);
      fit(c, thing("s_coin"), bx + 5, by + 5, 20, 20, ca);
      fit(c, thing("s_food"), bx + 126, by + 6, 17, 18, ca);
      barTrack(c, bx + 146, by + 12, 32, ca);
      fit(c, thing("s_drop"), bx + 184, by + 6, 16, 18, ca);
      barTrack(c, bx + 203, by + 12, 32, ca);
      fit(c, thing("s_heart"), bx + 241, by + 7, 17, 16, ca);
      barTrack(c, bx + 261, by + 12, 32, ca);
    });
    mark(0);
    // the numbers: a layer of their own, made again when one of them shows differently (a gauge under 30 blinks, so
    // it is drawn each picture instead)
    const HudNow& h = hud();
    const float lx = bx + 74, S = sc->SC, gv[3] = {T.food, T.clean, (float)h.comfort};
    static const float GX[3] = {146, 203, 261};
    static const uint32_t GC[3] = {0x4fb84a, 0x2f9be8, 0xe5484d};
    int nv[] = {h.coins, h.level, (int)lroundf((lx + 46 * h.span) * S), 0, 0, 0, (int)L.W, (int)lroundf(S * 100)};
    for (int g = 0; g < 3; g++) nv[3 + g] = gv[g] < 30 ? -1 : (int)lroundf(32 * std::min(1.0f, gv[g] / 100) * S * 4);
    layer(t, numL, fnv(2166136261u, nv, sizeof nv) | 1, bx + 20, by, 280, L.barH, a, [&](const Tgt& c, int ca) {
      text(c, h.coinsS.c_str(), bx + 28, by + 15.5f, 14, true, H(0x3a2c06), 0, ca);
      rrect(c, lx, by + 5, 46, 20, 10, H(0x2f6fb5), ca);
      Tgt part = c;  // the experience as the pill's fill
      part.x1 = std::min(c.x1, (int)lroundf((lx + 46 * h.span) * S));
      if (part.x1 > part.x0) rrect(part, lx, by + 5, 46, 20, 10, H(0x4f9ae8), ca);
      text(c, h.levelS.c_str(), lx + 23, by + 15.5f, 12, true, 0xFFFF, 1, ca);
      for (int g = 0; g < 3; g++)
        if (gv[g] >= 30) barFill(c, bx + GX[g], by + 12, 32, gv[g], GC[g], ca);
    });
    for (int g = 0; g < 3; g++)
      if (gv[g] < 30) barFill(t, bx + GX[g], by + 12, 32, gv[g], GC[g], a);
    if (chip.c && !sheet) blitScaled(t, &chip, bx + L.barW - chip.w, by + L.barH + 6, chip.w, chip.h, a);
    mark(1);
    if (!sponge) drawDock(t, L, a);
    mark(2);
  }
  if (show < 0.6f && !sheet && !card) {  // what the tank needs, when the bars are away: one layer, pulsing
    needs(n, nn);
    const int b = A((1 - show) * (0.75f + 0.25f * fsin(pulse * 4)));
    int v[] = {nn, 0, 0, 0, (int)L.W, (int)L.H, (int)lroundf(sc->SC * 100)};
    for (int i = 0; i < nn; i++) v[1 + i] = (int)(intptr_t)n[i][2];  // n_food, n_bucket, n_sponge: f, b, s
    const uint32_t key = fnv(2166136261u, v, sizeof v) | 1;
    for (int k = 0; k < 3 && nn; k++)  // the glasses, the icons, the dots: each kind its own layer (the page fades each)
      layer(t, needL[k], key, 0, Hh - 52, 40 * nn + 24, 52, b, [&](const Tgt& c, int ca) {
        for (int i = 0; i < nn; i++)
          if (k == 0) glass(c, 10 + i * 40, Hh - 42, 34, 34, 17, 204, ca);
          else if (k == 1) fit(c, thing(n[i]), 14 + i * 40, Hh - 38, 26, 26, ca);
          else circle(c, 40 + i * 40, Hh - 38, 4.5f, H(0xe5484d), ca);
      }, true);
  } else if (needL[0].key)
    for (Layer& m : needL) layerFree(m);
  if (sponge) {
    glass(t, W / 2 - 100, 46, 200, 26, 13, 224);
    text(t, "Kéo miếng bọt để lau kính", W / 2, 59.5f, 12, true, H(0x243140), 1);
    glass(t, W / 2 - 42, L.dy - 17, 84, 34, 17, 230);
    text(t, "Xong", W / 2, L.dy + 0.5f, 14, true, H(0x243140), 1);
    if (P.on && P.k == H_WIPE) fit(t, thing("c_sponge"), spX - 22, spY - 20, 44, 40);
  }
  for (const Float& f : floats) {
    const int fa = A(1 - f.t);
    text(t, f.s.c_str(), f.x + 1, f.y - 14 - f.t * 26 + 1, 16, true, H(0x3c1e00), 1, fa * 7 / 10);
    text(t, f.s.c_str(), f.x, f.y - 14 - f.t * 26, 16, true, H(0xffd84a), 1, fa);
  }
  if (fxNow.k == 2) {
    const float k = fxNow.t / 1.4f;
    ring(t, W / 2, Hh / 2 - 10, 30 + 150 * k, 5 * (1 - k) + 1, H(0xffd84a), A(1 - k));
  }
  if (foodPick) drawFoodPick(t, L);
  mark(5);
  if (card) drawCard(t, L);
  else if (cardL.key) layerFree(cardL);
  mark(3);
  if (sheet) {  // the water beside it dimmed, the sheet copied from its picture
    if (L.land) dim(t, 0, 0, L.shX, Hh, H(0x040e18), A(0.45f));
    else dim(t, 0, 0, W, L.shY + 16, H(0x040e18), A(0.45f));
    drawSheetCached(t, L);
  } else if (hdBuf) {
    sheetFree();  // (its pictures' PSRAM back)
  }
  mark(4);
  if (fzState == 1 && fzBuf) fzCopy(t, true);  // (no toast, no number flying: checked by frameBegin())
  drawToasts(t, L);
  if (homeLine) rrect(t, (W - 110) / 2, Hh - 9, 110, 4, 2, 0xFFFF, 128);  // as os.cpp's, white at half
  mark(5);
}

void frameBegin() {
  if (!sheet || saver) return fzFree();
  const Lay L = lay();
  const TankRec& T = game::T();
  const int v[] = {hud().coins, hud().level, (int)T.food, (int)T.clean, hud().comfort, (int)L.W, (int)(sc->SC * 100)};
  const uint32_t key = fnv(2166136261u, v, sizeof v);
  if (fzState && key != fzKey) fzState = 0;
  if (fzState || !toasts.empty() || !floats.empty() || fxNow.k || fabsf(show - 1) > 0.01f) return;
  int w, h;
  fzRect(L, w, h);
  if (fzBuf && (w != fzW || h != fzH)) heap_caps_free(fzBuf), fzBuf = nullptr;
  if (!fzBuf) fzBuf = (uint16_t*)heap_caps_malloc(w * h * 2, MALLOC_CAP_SPIRAM);
  if (fzBuf) fzW = w, fzH = h, fzKey = key, fzState = 1;
}
void frameEnd() {
  if (fzState == 1) fzState = 2;
}
bool frozen() { return fzState == 2 && sheet && !saver; }
#if TANK_QA
void layerStats(int& made, int& noRoom, uint32_t& us_, uint32_t& px) {
  made = layerMade, noRoom = layerNoRoom, us_ = putUs, px = putPx, putUs = putPx = 0;
}
#endif
void drawFrozen(const Tgt& t) {
  const Lay L = lay();
  fzCopy(t, false);
  drawSheetCached(t, L);
  for (const Float& f : floats) {
    const int fa = A(1 - f.t);
    text(t, f.s.c_str(), f.x + 1, f.y - 14 - f.t * 26 + 1, 16, true, H(0x3c1e00), 1, fa * 7 / 10);
    text(t, f.s.c_str(), f.x, f.y - 14 - f.t * 26, 16, true, H(0xffd84a), 1, fa);
  }
  drawToasts(t, L);
  if (homeLine) rrect(t, (L.W - 110) / 2, L.H - 9, 110, 4, 2, 0xFFFF, 128);
}

void sleep() { hudFree(), sheetFree(), fzFree(); }

}  // namespace ui
}  // namespace tank
