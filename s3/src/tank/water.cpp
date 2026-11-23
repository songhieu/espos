// Hồ cá cảnh: the water (tank_int.h Scene). The hand-off's fw/tank_ref.c carried over as it is (its functions, its
// numbers), with what ref/engine.js adds for the game: the finger, taps, rings, coins taken, the sponge, turning the
// board, the lamp and the dirt fading in. Two changes for the board:
//   - the picture is drawn in pieces (the panel's bands, or columns on the 3.5" board held on its side): prepare()
//     works out each thing's drawing once a frame (the fish's body chain, boxes), draw() clips to the piece;
//   - every pixel is integer work on 565 colours (as the reference); the pictures are padded by one clear pixel so
//     the sample between pixels has one bounds test, not four.
// Built for size; the pixel loops (the blits, discs, bubbles, draw) are -O2 (HOT).
#pragma GCC optimize("Os")
#include <Arduino.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "tank_int.h"

#define HOT __attribute__((optimize("O2")))

namespace tank {
#ifndef TANK_OLD_MATH  // (tank_int.h)
HOT float fsin(float x) {
  const float k = x * 0.15915494f;
  x -= 6.2831853f * (float)(int)(k + (k >= 0 ? 0.5f : -0.5f));  // -pi..pi
  if (x > 1.5707964f) x = 3.1415927f - x;
  else if (x < -1.5707964f) x = -3.1415927f - x;
  const float x2 = x * x;
  return x * (1 + x2 * (-0.16666667f + x2 * (0.0083333310f + x2 * -0.00019840874f)));
}
float fcos(float x) { return fsin(x + 1.5707964f); }
HOT float fsqrt(float x) {  // x >= 0: 1/sqrt by the bit guess and two Newton steps (multiplies only), times x
  if (x <= 0) return 0;
  union {
    float f;
    uint32_t i;
  } v = {x};
  v.i = 0x5F3759DFu - (v.i >> 1);
  float y = v.f;
  const float h = 0.5f * x;
  y *= 1.5f - h * y * y, y *= 1.5f - h * y * y;
  return x * y;
}
HOT float fpow(float x, float p) {
  if (x <= 0) return 0;
  union {
    float f;
    uint32_t i;
  } v = {x};
  int e = (int)(v.i >> 23 & 255) - 127;
  v.i = (v.i & 0x007FFFFF) | 0x3F800000;  // 1..2
  float m = v.f;
  if (m > 1.4142135f) m *= 0.5f, e++;
  const float t = (m - 1) / (m + 1), t2 = t * t, l = e + t * (2.8853901f + t2 * (0.96179669f + t2 * (0.57707801f + t2 * 0.41219858f)));
  float y = p * l;  // 2^y
  if (y < -126) return 0;
  if (y > 127) y = 127;
  int i = (int)y;
  if (y < i) i--;
  const float f = y - i, q = 1 + f * (0.69314718f + f * (0.24022650f + f * (0.055504109f + f * (0.0096181291f + f * 0.0013333558f))));
  v.i = (uint32_t)(i + 127) << 23;
  return q * v.f;
}
#endif
#if TANK_QA
uint32_t fishQa[4];  // the fish part: µs in shadows, bodies, spots; body pixels
bool qaPlain = false;  // the fish read by rows and the sway worked out per row, as before (tank plaincmp)
#else
constexpr bool qaPlain = false;
#endif

Scene* sc = nullptr;

namespace {

constexpr float K_PI = 3.14159265f, THICK = 0.17f, PIVOT = 0.36f;
uint32_t rs = 2463534242u;
float rnd(float a, float b) {
  rs ^= rs << 13, rs ^= rs >> 17, rs ^= rs << 5;
  return a + (b - a) * (float)(rs >> 8) / 16777216.0f;
}
inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
float wrapPi(float a) {
  while (a > K_PI) a -= 2 * K_PI;
  while (a <= -K_PI) a += 2 * K_PI;
  return a;
}
inline bool lowStyle(const Fish* f) { return f->S->style == ST_floor || f->S->style == ST_crawl; }

// ---------------------------------------------------------------- drawing records (prepare -> draw)
struct Look_ {  // the light on the sprites
  int fog;
  uint32_t fogc;
  int tr, tg, tb;
};
struct Chain {
  float PX[NSEG + 1], PY[NSEG + 1], SH[NSEG + 1];
  const Spr* s;
  float ox, oy, vs;
  int dir, alpha;
  // white spot
  bool sick;
  float face, pivot, hgt, puff, dsc, seed, bobwy;
  // the shadow on the sand
  float shX, shY, shRx, shRy, shA, shE;
};
struct Item {
  float z;
  uint8_t kind;  // 0 plant, 1 decor, 2 fish, 3 food, 4 coin
  int16_t i;
  int16_t x0, y0, x1, y1;  // its box on the frame (inclusive), shadows included
  int fog;
};
struct Frame {  // what prepare() works out once a frame
  Item items[6 + 3 + MAXF + MAXFOOD + MAXCOIN];
  int n = 0;
  Chain chains[MAXF];
  uint32_t fogc;
  int tr, tg, tb;
  uint16_t rayLut[480 + 480 + 2];
  float lut[480 + 480 + 2];  // (here, not on the loop task's stack)
  float sway[6][480];        // each plant's sideways shift per frame row (blitRows asked it for every piece)
  int rayX0[5], rayX1[5], nray = 0;  // where the beams are (lut index ranges)
  int rayK[3];
  bool raysOn = false;
};
Frame* fr = nullptr;
uint8_t* rayFade = nullptr;  // per frame row: the beams fade downward (0..255)
int16_t algX[32];             // the algae grid: where cell centre i is on the frame (column)
uint8_t* algF = nullptr;      // per column: how far past its cell centre (0..255)
int rayY0 = 0, rayY1 = 0;

// a sample between pixels of a padded straight-alpha 565 picture (x, y in 1/256 pixel); 0 = clear
HOT inline __attribute__((always_inline)) int sample(const Spr* s, int x, int y, uint32_t* col) {
  const int x0 = x >> 8, y0 = y >> 8;
  if (x0 < -1 || x0 >= s->w || y0 < -1 || y0 >= s->h) return 0;
  const int fx = (x & 255) >> 3, fy = (y & 255) >> 3, o = y0 * s->st + x0;
  const uint8_t* A = s->a + o;
  const int a00 = A[0], a10 = A[1], a01 = A[s->st], a11 = A[s->st + 1];
  const int at = a00 + (((a10 - a00) * fx) >> 5), ab = a01 + (((a11 - a01) * fx) >> 5), a = at + (((ab - at) * fy) >> 5);
  if (a < 3) return 0;
  const uint16_t* C = s->c + o;
  const uint32_t t = mix(ex(C[0]), ex(C[1]), fx), b = mix(ex(C[s->st]), ex(C[s->st + 1]), fx);
  *col = mix(t, b, fy);
  return a;
}
// the same from a fish's picture by columns (Spr::t): the same numbers, read down the column
HOT inline __attribute__((always_inline)) int sampleT(const Spr* s, int x, int y, uint32_t* col) {
  const int x0 = x >> 8, y0 = y >> 8;
  if (x0 < -1 || x0 >= s->w || y0 < -1 || y0 >= s->h) return 0;
  const int fx = (x & 255) >> 3, fy = (y & 255) >> 3;
  const uint32_t* T = s->t + x0 * s->th + y0;
  const uint32_t p00 = T[0], p01 = T[1], p10 = T[s->th], p11 = T[s->th + 1];
  const int a00 = p00 >> 16, a10 = p10 >> 16, a01 = p01 >> 16, a11 = p11 >> 16;
  const int at = a00 + (((a10 - a00) * fx) >> 5), ab = a01 + (((a11 - a01) * fx) >> 5), a = at + (((ab - at) * fy) >> 5);
  if (a < 3) return 0;
  const uint32_t t = mix(ex((uint16_t)p00), ex((uint16_t)p10), fx), b = mix(ex((uint16_t)p01), ex((uint16_t)p11), fx);
  *col = mix(t, b, fy);
  return a;
}
// The algae film as the things are drawn: their colour veiled by the film's weight under them (the background
// carries it already: film(thing over bg) = film(thing) over film(bg)). Set by draw(); nullptr: no film.
const uint8_t* filmNow = nullptr;
const uint8_t* filmTNow = nullptr;  // the same by columns (when made)
uint32_t filmGNow = 0;
int filmFW = 0, filmFH = 0;

// one sprite pixel: shade (32 = as it is), the light, the fog, the film (0..32), alpha (0..256)
HOT inline __attribute__((always_inline)) void put(uint16_t* d, uint32_t c, int a, int shade, int alpha, const Look_& K, int fw) {
  if (shade < 32) c = mix(c, 0, 32 - shade);
  else if (shade > 32) c = mix(c, 0x07E0F81Fu, (shade - 32) >> 1);
  if (K.tr != 256)
    c = ((((c >> 11) & 31) * K.tr >> 8) << 11) | ((((c >> 21) & 63) * K.tg >> 8) << 21) | ((c & 31) * K.tb >> 8);
  if (K.fog) c = mix(c, K.fogc, K.fog);
  if (fw) c = mix(c, filmGNow, fw);
  a = (a * alpha) >> 8;
  *d = pk(mix(ex(*d), c, (a + 4) >> 3));
}

// THE FISH BLIT (tank_ref.c blit_fish), clipped to the piece t.
HOT void blitFish(const Tgt& t, const Chain& ch, const Look_& K, long& touched) {
  const Spr* s = ch.s;
#if !TANK_QA
  if (!s->t) return;
#endif
  const int n = NSEG, dir = ch.dir, alpha = ch.alpha;
  const float vs = ch.vs, hh = s->h * vs, ox = ch.ox, oy = ch.oy;
  const int iv = (int)(65536.0f / vs);  // 16.16: no drift between the pieces of the picture
  const float ivs = 1 / vs;
  for (int i = 0; i < n; i++) {
    float xa = ox + dir * ch.PX[i], xb = ox + dir * ch.PX[i + 1], ya = ch.PY[i], yb = ch.PY[i + 1], sa = ch.SH[i],
          sb = ch.SH[i + 1], ua = (float)i / n, ub = (float)(i + 1) / n, tt;
    if (xa > xb) tt = xa, xa = xb, xb = tt, tt = ya, ya = yb, yb = tt, tt = sa, sa = sb, sb = tt, tt = ua, ua = ub, ub = tt;
    int X0 = (int)ceilf(xa - 0.5f), X1 = (int)ceilf(xb - 0.5f) - 1;
    if (X0 < t.x0) X0 = t.x0;
    if (X1 > t.x1 - 1) X1 = t.x1 - 1;
    if (X1 < X0) continue;
    const float inv = 1.0f / (xb - xa);
    for (int X = X0; X <= X1; X++) {
      const float k = (X + 0.5f - xa) * inv, yt = oy + ya + (yb - ya) * k - hh * 0.5f;
      const int sx = (int)(((ua + (ub - ua) * k) * s->w - 0.5f) * 256.0f), shade = (int)((sa + (sb - sa) * k) * 32.0f);
      int Y0 = (int)ceilf(yt - 1), Y1 = (int)floorf(yt + hh);
      if (Y0 < t.y0) Y0 = t.y0;
      if (Y1 > t.y1 - 1) Y1 = t.y1 - 1;
      if (Y1 < Y0) continue;
      if (s->cs) {  // only between the first and the last coloured row of the two columns sampled (one more each side)
        const int c0 = sx >> 8;
        int lo = s->h, hi = -1;
        for (int c = c0 < 0 ? 0 : c0; c <= c0 + 1 && c < s->w; c++)
          if (s->cs[2 * c] <= s->cs[2 * c + 1]) lo = std::min(lo, (int)s->cs[2 * c]), hi = std::max(hi, (int)s->cs[2 * c + 1]);
        if (lo > hi) continue;
        Y0 = std::max(Y0, (int)floorf((lo - 1 + 0.5f) * vs + yt - 0.5f));
        Y1 = std::min(Y1, (int)ceilf((hi + 1 + 0.5f) * vs + yt - 0.5f));
        if (Y1 < Y0) continue;
      }
      int sy = (int)(((Y0 + 0.5f - yt) * ivs - 0.5f) * 65536.0f);
      uint16_t* d = t.at(X, Y0);
      const bool fc = filmTNow != nullptr;  // the film by columns: read straight down
      const uint8_t* w = fc ? filmTNow + X * filmFH + Y0 : filmNow ? filmNow + Y0 * filmFW + X : nullptr;
      const int ws = fc ? 1 : filmFW;
      const bool hw = w != nullptr;
      uint32_t c;
#if TANK_QA
      if (!s->t || qaPlain) {  // the pictures by rows (shots: tank plaincmp)
        for (int Y = Y0; Y <= Y1; Y++, sy += iv, d += t.dy, w += ws) {
          const int a = sample(s, sx, sy >> 8, &c);
          if (a) put(d, c, a, shade, alpha, K, hw ? *w : 0), touched++;
        }
        continue;
      }
#endif
      for (int Y = Y0; Y <= Y1; Y++, sy += iv, d += t.dy, w += ws) {  // (spr() gives every fish its columns)
        const int a = sampleT(s, sx, sy >> 8, &c);
        if (a) put(d, c, a, shade, alpha, K, hw ? *w : 0), touched++;
      }
    }
  }
}

// THE PLANT BLIT (blit_rows): row by row, each row shifted sideways; a = 0 is the plain scaled blit.
HOT void blitRows(const Tgt& t, const Spr* s, float x0, float ytop, float hs, float vs, float a, float pw, float ph, float k,
              bool flip, int alpha, const Look_& K, long& touched, const float* sway = nullptr) {
  const float ww = s->w * hs, hh = s->h * vs, ihh = 1 / hh, ivs = 1 / vs, ihs = 1 / hs;  // (the S3 divides in software)
  const int ih = (int)(65536.0f / hs);
  int Y0 = (int)ceilf(ytop - 1), Y1 = (int)floorf(ytop + hh);
  if (Y0 < t.y0) Y0 = t.y0;
  if (Y1 > t.y1 - 1) Y1 = t.y1 - 1;
  for (int Y = Y0; Y <= Y1; Y++) {
    const float yr = Y + 0.5f - ytop,
                dx = sway && !qaPlain ? sway[Y] : a != 0 ? a * fpow(clampf(1 - yr * ihh, 0, 1), pw) * fsin(ph - k * clampf(1 - yr * ihh, 0, 1)) : 0;
    const int sy = (int)((yr * ivs - 0.5f) * 256.0f);
    int X0 = (int)ceilf(x0 + dx - 1), X1 = (int)floorf(x0 + dx + ww);
    if (X0 < t.x0) X0 = t.x0;
    if (X1 > t.x1 - 1) X1 = t.x1 - 1;
    if (X1 < X0) continue;
    if (s->rs) {  // only between the first and the last coloured column of the two rows sampled (one more each side)
      const int r0 = sy >> 8;
      int lo = s->w, hi = -1;
      for (int r = r0 < 0 ? 0 : r0; r <= r0 + 1 && r < s->h; r++)
        if (s->rs[2 * r] <= s->rs[2 * r + 1]) lo = std::min(lo, (int)s->rs[2 * r]), hi = std::max(hi, (int)s->rs[2 * r + 1]);
      if (lo > hi) continue;
      const float qa = flip ? s->w - 1 - (hi + 1) : lo - 1, qb = flip ? s->w - 1 - (lo - 1) : hi + 1;
      X0 = std::max(X0, (int)floorf((qa + 0.5f) * hs + x0 + dx - 0.5f));
      X1 = std::min(X1, (int)ceilf((qb + 0.5f) * hs + x0 + dx - 0.5f));
      if (X1 < X0) continue;
    }
    int sx = (int)(((X0 + 0.5f - x0 - dx) * ihs - 0.5f) * 65536.0f);
    uint16_t* d = t.at(X0, Y);
    const uint8_t* w = filmNow ? filmNow + Y * filmFW + X0 : nullptr;
    uint32_t c;
    const int fl = (s->w - 1) << 8;
    for (int X = X0; X <= X1; X++, sx += ih, d += t.dx, w++) {
      const int v = sample(s, flip ? fl - (sx >> 8) : sx >> 8, sy, &c);
      if (v) put(d, c, v, 32, alpha, K, filmNow ? *w : 0), touched++;
    }
  }
}

// a soft disc (food, specks; shadows are squashed dark discs)
HOT void disc(const Tgt& t, float cx, float cy, float rx, float ry, uint16_t col, float al, float e) {
  // (no size: nothing. A zero radius made every weight NaN, which the S3 turns into a huge integer: the mix then
  // painted saturated green and blue blocks; arm64, the simulator's, turns it into 0 and showed nothing)
  if (!(rx > 0 && ry > 0)) return;
  int X0 = (int)floorf(cx - rx - e), X1 = (int)ceilf(cx + rx + e), Y0 = (int)floorf(cy - ry - e), Y1 = (int)ceilf(cy + ry + e);
  X0 = std::max(X0, t.x0), Y0 = std::max(Y0, t.y0), X1 = std::min(X1, t.x1 - 1), Y1 = std::min(Y1, t.y1 - 1);
  if (X1 < X0 || Y1 < Y0) return;
  const uint32_t c = ex(col);
  const float m = (rx < ry ? rx : ry) / e, irx = 1 / rx, iry = 1 / ry;
  for (int Y = Y0; Y <= Y1; Y++) {
    const float uy = (Y + 0.5f - cy) * iry, uy2 = uy * uy;
    uint16_t* d = t.at(X0, Y);
    for (int X = X0; X <= X1; X++, d += t.dx) {
      const float ux = (X + 0.5f - cx) * irx, a = clampf((1 - fsqrt(ux * ux + uy2)) * m + 0.5f, 0, 1) * al;
      const int w = (int)(a * 32.0f + 0.5f);
      if (w > 0) *d = pk(mix(ex(*d), c, w > 32 ? 32 : w));
    }
  }
}
HOT void bubble(const Tgt& t, float cx, float cy, float r, float al) {
  int X0 = (int)floorf(cx - r - 1), X1 = (int)ceilf(cx + r + 1), Y0 = (int)floorf(cy - r - 1), Y1 = (int)ceilf(cy + r + 1);
  X0 = std::max(X0, t.x0), Y0 = std::max(Y0, t.y0), X1 = std::min(X1, t.x1 - 1), Y1 = std::min(Y1, t.y1 - 1);
  if (X1 < X0 || Y1 < Y0) return;
  const uint32_t c = ex(rgb565(236, 248, 255));
  const float out2 = (r + 0.45f) * (r + 0.45f), gr = r * 0.34f + 0.5f, gr2 = gr * gr, igr = 1 / gr;
  for (int Y = Y0; Y <= Y1; Y++) {
    uint16_t* p = t.at(X0, Y);
    for (int X = X0; X <= X1; X++, p += t.dx) {
      const float dx = X + 0.5f - cx, dy = Y + 0.5f - cy, d2 = dx * dx + dy * dy;
      if (d2 > out2) continue;  // past the rim: nothing (the glint is inside)
      const float d = fsqrt(d2);
      float a = clampf(1 - fabsf(d - r + 0.5f) * (1 / 0.9f), 0, 1) * 0.62f + (d < r ? 0.07f : 0);
      const float gx = dx + r * 0.38f, gy = dy + r * 0.38f, g2 = gx * gx + gy * gy;
      if (g2 < gr2) a += (1 - fsqrt(g2) * igr) * 0.5f;
      if (a > 1) a = 1;
      const int w = (int)(a * al * 32.0f + 0.5f);
      if (w) *p = pk(mix(ex(*p), c, w));
    }
  }
}

int fogOf(float z, float haze) { return (int)(clampf((1 - z / ZD) * 0.36f + haze * 0.25f, 0, 0.7f) * 32); }

}  // namespace

// ---------------------------------------------------------------- the food (the water's pieces and the chooser's icons)
// The owner, 2026-11-21: the green squares did not look like fish food. Now flakes are thin uneven chips in warm
// colours that turn over as they sink, pellets are round brown granules with a light on them, and the fresh bait
// is a bloodworm: a short red worm of beads that wriggles. A few soft shapes each, a few hundred pixels at most.
namespace {
float hash(float s, int k) {  // 0..1 from a piece's seed (s >= 0)
  const float v = s * (7.13f + k * 3.71f) + k * 0.37f;
  return v - (int)v;
}
uint16_t tinted(int r, int g, int b, const float* T, float k) { return rgb565(r * T[0] * k, g * T[1] * k, b * T[2] * k); }
// a soft convex pentagon, its corners going round as foodPiece makes them (a few dozen pixels: -Os is enough)
void poly(const Tgt& t, const float* px, const float* py, uint16_t col, float al) {
  constexpr int n = 5;
  float x0 = px[0], x1 = px[0], y0 = py[0], y1 = py[0], ux[5], uy[5], uc[5];
  for (int i = 0; i < n; i++) {  // its box; each edge: the pixel's distance inside it
    const int j = (i + 1) % n;
    x0 = std::min(x0, px[i]), x1 = std::max(x1, px[i]), y0 = std::min(y0, py[i]), y1 = std::max(y1, py[i]);
    const float dx = px[j] - px[i], dy = py[j] - py[i], l = fsqrt(dx * dx + dy * dy) + 1e-4f;
    ux[i] = -dy / l, uy[i] = dx / l, uc[i] = -(ux[i] * px[i] + uy[i] * py[i]);
  }
  const int X0 = std::max(t.x0, (int)floorf(x0)), X1 = std::min(t.x1 - 1, (int)ceilf(x1)), Y0 = std::max(t.y0, (int)floorf(y0)),
            Y1 = std::min(t.y1 - 1, (int)ceilf(y1));
  const uint32_t c = ex(col);
  for (int Y = Y0; Y <= Y1; Y++) {
    uint16_t* d = t.at(X0, Y);
    for (int X = X0; X <= X1; X++, d += t.dx) {
      float in = 1;
      for (int i = 0; i < n; i++) in = std::min(in, ux[i] * (X + 0.5f) + uy[i] * (Y + 0.5f) + uc[i] + 0.5f);
      const int w = (int)(in * al * 32 + 0.5f);
      if (w > 0) *d = pk(mix(ex(*d), c, w > 32 ? 32 : w));
    }
  }
}
}  // namespace

const float plain[3] = {1, 1, 1};
// one piece at frame pixels (x, y): r = its scale in pixels, tm = the clock (the turning, the wriggle), s = its seed
void foodPiece(const Tgt& t, int kind, float x, float y, float r, float tm, float s, const float* T, float al) {
  if (kind == F_flake) {  // a thin chip of five corners (four uneven shapes), turning over: narrow edge on, and darker
    static const int8_t SHAPE[4][5][2] = {{{16, 0}, {6, 14}, {-12, 11}, {-15, -4}, {3, -15}},
                                         {{14, 4}, {-2, 16}, {-16, 6}, {-9, -13}, {10, -12}},
                                         {{16, -3}, {9, 12}, {-10, 14}, {-16, -2}, {-1, -16}},
                                         {{12, 8}, {-5, 15}, {-15, 0}, {-6, -14}, {13, -9}}};
    // only warm colours: at the 2.8" board's size a yellow or green chip, dimmed by the lamp being off, read as a
    // little green square on the green water (the owner, 2026-11-21)
    static const uint8_t PAL[6][3] = {{240, 138, 40}, {232, 112, 36}, {212, 66, 44}, {246, 162, 70}, {200, 84, 40}, {222, 96, 52}};
    const uint8_t* c = PAL[(int)(hash(s, 9) * 5.99f)];
    const int8_t(*P)[2] = SHAPE[(int)(hash(s, 4) * 3.99f)];
    const float turn = 0.25f + 0.75f * fabsf(fcos(tm * 2.3f + s * 3)), a = s * 5 + tm * 0.9f, k = std::max(2.7f * r, 2.2f) / 16,
                cu = fcos(a) * k * turn, su = fsin(a) * k * turn, cv = fcos(a) * k * 0.7f, sv = fsin(a) * k * 0.7f;
    float px[5], py[5];
    for (int i = 0; i < 5; i++) px[i] = x + P[i][0] * cu - P[i][1] * sv, py[i] = y + P[i][0] * su + P[i][1] * cv;
    poly(t, px, py, tinted(c[0], c[1], c[2], T, 0.8f + 0.2f * turn), al);
  } else if (kind == F_sink) {  // a round granule (brown, or reddish brown): dark under, its body, a light on top
    static const float B[3][5] = {{0, 0, 1, 1, 1}, {-0.18f, -0.2f, 0.74f, 0.8f, 1}, {-0.42f, -0.46f, 0.28f, 0.6f, 0.85f}};  // x, y, size, edge, alpha
    static const uint8_t C[2][3][3] = {{{92, 54, 28}, {160, 100, 52}, {246, 214, 160}}, {{104, 44, 28}, {172, 82, 48}, {246, 214, 160}}};
    const uint8_t(*c)[3] = C[hash(s, 3) > 0.6f];
    const float R = std::max(1.9f * r, 1.7f);  // (at least a few pixels round on the 2.8")
    for (int i = 0; i < 3; i++)
      disc(t, x + B[i][0] * R, y + B[i][1] * R, B[i][2] * R, B[i][2] * R, tinted(c[i][0], c[i][1], c[i][2], T, 1), al * B[i][4], B[i][3]);
  } else {  // a bloodworm: overlapping beads along a wriggle (its rings: two reds in turn), thinner at the ends
    const uint16_t c0 = tinted(168, 24, 34, T, 1), c1 = tinted(222, 64, 56, T, 1);
    const float a = s * 4 + tm * 0.5f, ca = fcos(a), sa = fsin(a), L = std::max(10 * r, 8.0f);
    for (int j = 0; j < 12; j++) {
      const float k = j / 11.0f, u = (k - 0.5f) * L, w = r * fsin(k * 5.2f + tm * 6 + s), rr = std::max(r * (0.68f - 0.28f * fabsf(k * 2 - 1)), 0.55f);
      disc(t, x + u * ca - w * sa, y + u * sa + w * ca, rr, rr, j & 1 ? c1 : c0, al, 0.7f);
    }
  }
}

void spriteBlit(const Tgt& t, const Spr* s, float x, float y, float hs, float vs, int alpha, int mode) {
  if (!s || hs <= 0 || vs <= 0) return;
  const Look_ K = {mode == 2 ? 32 : 0, 0, 256, 256, 256};
  long n = 0;
  blitRows(t, s, x, y, hs, vs, 0, 1, 0, 0, mode == 1, alpha, K, n);
}

// ---------------------------------------------------------------- the scene
float Scene::floorY(float z) const { return lerpf(geo->floor0, geo->floor1, z / ZD); }
float Scene::coinY(float z) const { return std::min(floorY(z) - 5, (float)VH - 76); }

void Scene::band(const Fish* f, float* b0, float* b1) const {
  const float fl = geo->floor0, top = TOPY + 12, hgt = f->S->w * f->size * f->asp * 0.5f;
  float a, b;
  const int z = f->S->zone;
  if (z == Z_top) a = top, b = top + (fl - top) * 0.42f;
  else if (z == Z_low) a = top + (fl - top) * 0.5f, b = fl - 14;
  else if (z == Z_floor) a = fl - 30, b = fl;
  else a = top + (fl - top) * 0.12f, b = fl - 34;
  *b0 = a + hgt * 0.6f, *b1 = b - hgt * 0.6f;
  if (*b1 < *b0 + 8) *b1 = *b0 + 8;
}
void Scene::reoffset(Fish* f) {
  const float n = f->school >= 0 ? schools[f->school].n : 1, L = f->S->w;
  f->ox = rnd(-1, 1) * (L * 0.7f + n * 3.2f), f->oy = rnd(-1, 1) * (L * 0.4f + n * 2.0f), f->oz = rnd(-1, 1) * 24;
  f->ot = rnd(9, 22);
}
void Scene::pick(Fish* f) {  // the next place to swim to
  const StyleK* st = f->st;
  float b0, b1, tx, ty;
  const float m = f->S->w * 0.45f + 8;
  band(f, &b0, &b1);
  if (f->host >= 0 && f->host < nplants) {  // home is the anemone
    const PlantI* p = &plants[f->host];
    const float hh = D.plant[p->k].w * p->s * (0.74f + 0.26f * p->z / ZD) * D.plant[p->k].asp;
    f->tx = clampf(p->x + rnd(-24, 24), m, VW - m), f->ty = floorY(p->z) - hh * rnd(0.5f, 1.2f);
    f->tz = clampf(p->z + rnd(2, 14), 4, ZD - 4), f->sd = st->v * f->vk * rnd(0.4f, 0.8f);
    return;
  }
  if (f->S->style == ST_perch) tx = f->hx + rnd(-38, 38), ty = f->hy + rnd(-26, 26);
  else if (rnd(0, 1) < st->lap) tx = f->x < VW / 2 ? rnd(VW * 0.56f, VW - m) : rnd(m, VW * 0.44f), ty = f->y + rnd(-55, 55);
  else {
    const float r = st->lap > 0.4f ? 90 : 60, on = fcos(f->yaw) >= 0 ? 1 : -1;
    tx = rnd(0, 1) < 0.72f ? f->x + on * rnd(r * 0.3f, r) : f->x + rnd(-r, r);
    ty = f->y + rnd(-r * 0.6f, r * 0.6f);
    if (tx < m || tx > VW - m) tx = f->x - on * rnd(r * 0.3f, r);
  }
  f->tx = clampf(tx, m, VW - m), f->ty = clampf(ty, b0, b1), f->tz = clampf(f->z + rnd(-36, 36), 6, ZD - 6);
  f->sd = st->v * f->vk * rnd(0.65f, 1.05f);
  if (lowStyle(f)) {
    f->tz = clampf(f->z + rnd(-30, 30), 4, ZD - 4);
    f->hop = f->S->style == ST_floor && rnd(0, 1) < 0.22f ? rnd(8, !strcmp(f->S->id, "cory") && rnd(0, 1) < 0.2f ? 200 : 34) : 0;
  }
}

Fish* Scene::addFish(int sp, float size, uint32_t uid) {
  if (sp < 0 || sp >= D.nsp || nfish >= MAXF) return nullptr;
  const Species* S = &D.sp[sp];
  const PackId pk = S->marine ? P_MARINE : P_FRESH;
  const Spr *pic = spr(pk, S->pic), *half = spr(pk, S->half);
  if (!pic) return nullptr;
  Fish* f = &fish[nfish++];
  memset(f, 0, sizeof *f);
  f->sp = sp, f->uid = uid, f->S = S, f->st = &D.style[S->style], f->pic = pic, f->half = half ? half : pic;
  f->asp = (float)pic->h / pic->w, f->size = size, f->school = f->host = -1;
  f->yaw = rnd(0, 1) < 0.5f ? 0 : K_PI, f->ph = rnd(0, 6), f->amp = 0.2f, f->seed = rnd(0, 100), f->mw = rnd(0.25f, 0.6f);
  f->vk = rnd(0.85f, 1.15f);
  float b0, b1;
  band(f, &b0, &b1);
  f->x = rnd(40, VW - 40), f->z = rnd(8, ZD - 8), f->y = rnd(b0, b1);
  if (lowStyle(f)) f->y = floorY(f->z) - S->w * size * f->asp * 0.5f;
  f->hx = f->x, f->hy = f->y;
  if (S->school) {
    School* s = &schools[sp];
    if (!s->n) s->sp = sp, s->x = s->tx = f->x, s->y = s->ty = f->y, s->z = s->tz = f->z, s->vx = s->vy = s->vz = s->wait = 0;
    s->n++, f->school = sp;
    reoffset(f);
  }
  pick(f);
  return f;
}
void Scene::removeFish(uint32_t uid) {
  for (int i = 0; i < nfish; i++)
    if (fish[i].uid == uid) {
      if (fish[i].school >= 0) schools[fish[i].school].n--;
      fish[i] = fish[--nfish];
      for (int k = 0; k < nfish; k++) fish[k].nafraid = 0;  // the game's census tells them again
      return;
    }
}
void Scene::clearFish() {
  nfish = 0;
  for (School& s : schools) s.n = 0;
}
Fish* Scene::find(uint32_t uid) {
  for (int i = 0; i < nfish; i++)
    if (fish[i].uid == uid) return &fish[i];
  return nullptr;
}

void Scene::setView(bool l, float s) {
  const int vw = l ? 480 : 320, vh = l ? 320 : 480;
  const bool turn = l != land;
  const float kx = (float)vw / VW, ky = (float)vh / VH;
  VW = vw, VH = vh, land = l, SC = s, FW = (int)(VW * s + 0.5f), FH = (int)(VH * s + 0.5f);
  AC = (VW + 15) / 16, AR = (VH + 15) / 16;
  freeBg();  // (a part left from the other size: made again whole at the next prepare)
  allocBg();
  free(rayFade);
  rayFade = (uint8_t*)malloc(FH);
  rayY0 = (int)(TOPY * 0.5f * SC), rayY1 = (int)(FH * 0.66f);
  for (int Y = 0; Y < FH; Y++)
    rayFade[Y] = Y < rayY0 || Y >= rayY1 ? 0 : (uint8_t)(255 * fpow(1 - (float)(Y - rayY0) / (rayY1 - rayY0), 1.3f));
  free(algF);
  algF = (uint8_t*)malloc(FW);
  const float cell = 16 * SC;
  for (int i = 0; i < 32; i++) algX[i] = (int16_t)ceilf((i + 0.5f) * cell - 0.5f);  // first column at or past centre i
  for (int X = 0; X < FW; X++) {
    const float gx = clampf((X + 0.5f) / cell - 0.5f, 0, AC - 1.001f);
    algF[X] = (uint8_t)((gx - (int)gx) * 256);
  }
  if (!fr) fr = (Frame*)heap_caps_calloc(1, sizeof(Frame), MALLOC_CAP_SPIRAM);
  // the drifting specks: depth, sway and size as the hand-off's engine.js gives them (they were left 0: radius 0, the
  // 2.8" board's blinking green and blue blocks, 2026-11-21), placed again when the view turns
  for (auto& p : specks)
    if (p.r <= 0 || turn) p.x = rnd(0, VW), p.y = rnd(TOPY, VH - 60), p.z = rnd(0, 1), p.s = rnd(0, 6), p.r = rnd(0.5f, 1.1f);
  if (!turn) return;
  memset(algae, 0, sizeof algae), algaeOn = false, nbub = 0;
  for (int i = 0; i < nfish; i++) {
    Fish* f = &fish[i];
    f->x *= kx, f->y *= ky, f->hx *= kx, f->hy *= ky, f->flee = 0;
  }
  for (School& q : schools) q.x *= kx, q.y *= ky, q.tx = q.x, q.ty = q.y;
  for (int i = 0; i < nfood; i++) food[i].x *= kx;
  for (int i = 0; i < ncoins; i++) coins[i].x *= kx, coins[i].rest = false;
  setLook(look);
}

void Scene::setLook(int l) {
  const bool other = l != look || !geo;
  look = l;
  geo = land ? &D.look[l].wide : &D.look[l].up;
  if (other) bgLoad = bgBad = true, bgTint[0] = -1;
  // the plants and ornaments to this picture's places
  int ids[6], slots[6], n = nplants;
  for (int i = 0; i < n; i++) ids[i] = plants[i].k, slots[i] = plants[i].slot;
  setPlants(ids, slots, n);
  int dids[3], ds[3], m = ndecor;
  for (int i = 0; i < m; i++) dids[i] = decor[i].k, ds[i] = decor[i].slot;
  setDecor(dids, ds, m);
  const float fl = geo->floor0;
  for (int i = 0; i < nfish; i++) {
    Fish* f = &fish[i];
    f->x = clampf(f->x, 20, VW - 20), f->y = std::min(f->y, fl - 30), f->hx = clampf(f->hx, 30, VW - 30);
    f->hy = std::min(f->hy, fl - 30), f->wait = 0;
    pick(f);
  }
}
void Scene::setPlants(const int* ids, const int* slots, int n) {
  nplants = 0;
  for (int i = 0; i < n && nplants < 6; i++) {
    if (ids[i] < 0 || slots[i] >= D.look[look].nslots || slots[i] >= 6) continue;
    PlantI& p = plants[nplants++];
    const float* s = geo->slots[slots[i]];
    p.k = ids[i], p.slot = slots[i], p.pic = sprNamed(P_THINGS, D.plant[ids[i]].id), p.x = s[0], p.z = s[1] * ZD, p.s = s[2];
    p.ph = rnd(0, 6), p.flip = (slots[i] * 7 + (int)strlen(D.plant[ids[i]].id)) % 2;
    if (!p.pic) nplants--;
  }
  for (int i = 0; i < nfish; i++) fish[i].host = -1;
}
void Scene::setDecor(const int* ids, const int* slots, int n) {
  ndecor = 0;
  for (int i = 0; i < n && ndecor < 3; i++) {
    if (ids[i] < 0 || slots[i] >= D.look[look].ndecor || slots[i] >= 3) continue;
    DecorI& d = decor[ndecor++];
    d.k = ids[i], d.slot = slots[i], d.pic = sprNamed(P_THINGS, D.decor[ids[i]].id), d.w = D.decor[ids[i]].w;
    d.x = geo->decor[slots[i]][0], d.z = geo->decor[slots[i]][1] * ZD, d.bubbler = !strcmp(D.decor[ids[i]].id, "bubbler");
    if (!d.pic) ndecor--;
  }
}

void Scene::dropFood(float x, int n, int kind, float val) {
  for (int i = 0; i < n && nfood < MAXFOOD; i++) {
    FoodI& p = food[nfood++];
    p.kind = kind, p.food = val, p.x = clampf(x + rnd(-16, 16), 10, VW - 10), p.y = TOPY + rnd(-4, 6), p.z = rnd(20, ZD - 6);
    p.vy = kind == F_sink ? rnd(42, 58) : kind == F_live ? rnd(10, 16) : rnd(15, 24), p.s = rnd(0, 6), p.rest = 0, p.lying = false;
  }
}
void Scene::tapGlass(float x, float y) {  // nearby fish start away, a puffer puffs up, a shrimp flicks backward
  if (nrings < MAXRING) rings[nrings++] = {x, y, 0, false};
  for (int i = 0; i < nfish; i++) {
    Fish* f = &fish[i];
    if (hypotf(f->x - x, f->y - y) > 95) continue;
    if (f->S->puffs) f->puff = 2.6f;
    if (f->S->style == ST_crawl) {
      if (strcmp(f->S->id, "snail") && !f->S->fixed) f->kx = -fcos(f->yaw) * 70, f->ky = -34;
      continue;
    }
    float b0, b1;
    band(f, &b0, &b1);
    const float a = atan2f(f->y - y, f->x - x) + rnd(-0.4f, 0.4f);
    f->flee = rnd(0.55f, 0.9f), f->fx = clampf(f->x + fcos(a) * 130, 20, VW - 20), f->fy = clampf(f->y + fsin(a) * 70, b0, b1);
    if (f->S->style == ST_perch) f->fx = f->hx, f->fy = f->hy;
    f->beat = true;
  }
}
void Scene::addCoin(float x, float y, float z, int val, float vy) {
  if (ncoins >= MAXCOIN) return;
  CoinI& c = coins[ncoins++];
  c.x = x, c.y = y, c.z = z, c.vy = vy >= 0 ? vy : rnd(20, 26), c.s = rnd(0, 6), c.t = 0, c.rest = false, c.val = val ? val : 1;
}
int Scene::takeCoins(float x, float y, float r) {
  int got = 0;
  for (int i = ncoins - 1; i >= 0; i--)
    if (hypotf(coins[i].x - x, coins[i].y - y) < r) {
      got += coins[i].val;
      if (nrings < MAXRING) rings[nrings++] = {coins[i].x, coins[i].y, 0, true};
      coins[i] = coins[--ncoins];
    }
  return got;
}
float Scene::wipe(float x, float y, float r) {  // the sponge clears the algae cells under it
  float got = 0;
  for (int j = 0; j < AR; j++)
    for (int i = 0; i < AC; i++) {
      float& a = algae[j * AC + i];
      const float d = hypotf(i * 16 + 8 - x, j * 16 + 8 - y);
      if (d < r && a > 0) {
        const float k = std::min(a, 1 - d / r + 0.35f);
        a -= k, got += k;
      }
    }
  if (got > 0) {
    float s = 0;
    for (int i = 0; i < AC * AR; i++) s += algae[i];
    algaeOn = s > 0.5f;
    filmDirty((int)((x - r - 16) * SC), (int)((y - r - 16) * SC), (int)((x + r + 16) * SC) + 1, (int)((y + r + 16) * SC) + 1);
  }
  return got;
}
void Scene::setAlgae(float level) {  // 0..100: the cells' cover grows from the edges and corners
  const int n = AC * AR;
  float sum = 0;
  for (int i = 0; i < n; i++) sum += algae[i];
  const float want = level / 100 * n * 0.5f;
  int guard = 0, i0 = AC, j0 = AR, i1 = -1, j1 = -1;
  while (sum < want && guard++ < 400) {
    const int i = (int)rnd(0, AC - 0.01f), j = (int)rnd(0, AR - 0.01f);
    const float edge = std::min(std::min(i, AC - 1 - i), std::min(j > 2 ? AR - 1 - j : 9, 9)) / 9.0f;
    if (rnd(0, 1) < edge * 0.85f) continue;
    float& a = algae[j * AC + i];
    const float add = std::min(1 - a, 0.25f);
    if (add <= 0) continue;
    a += add, sum += add;
    i0 = std::min(i0, i), j0 = std::min(j0, j), i1 = std::max(i1, i), j1 = std::max(j1, j);
  }
  const bool was = algaeOn;
  algaeOn = sum > 0.5f;
  // The game asks every tick while the tank has less than its level; the film is made again only where cells grew
  // (a cell's weight reaches the next cell's centre), all of it when it comes or goes.
  if (algaeOn != was) filmDirty(0, 0, 1 << 14, 1 << 14);
  else if (i1 >= 0) filmDirty((int)((i0 - 1) * 16 * SC), (int)((j0 - 1) * 16 * SC), (int)((i1 + 2) * 16 * SC) + 1, (int)((j1 + 2) * 16 * SC) + 1);
}
void Scene::filmDirty(int x0, int y0, int x1, int y1) {
  x0 = std::max(0, x0), y0 = std::max(0, y0), x1 = std::min(FW, x1), y1 = std::min(FH, y1);
  if (x0 >= x1 || y0 >= y1) return;
  if (filmX0 >= filmX1) filmX0 = x0, filmY0 = y0, filmX1 = x1, filmY1 = y1;
  else filmX0 = std::min(filmX0, x0), filmY0 = std::min(filmY0, y0), filmX1 = std::max(filmX1, x1), filmY1 = std::max(filmY1, y1);
}
float Scene::algaeLevel() const {
  float s = 0;
  const int n = AC * AR;
  for (int i = 0; i < n; i++) s += algae[i];
  return s / n * 200;
}
void Scene::clearAlgae() { memset(algae, 0, sizeof algae), algaeOn = false, filmDirty(0, 0, 1 << 14, 1 << 14); }

Fish* Scene::fishAt(float x, float y) {
  Fish* best = nullptr;
  float bz = -1;
  for (int i = 0; i < nfish; i++) {
    Fish* f = &fish[i];
    const float w = f->S->w * f->size * 0.5f + 4, h = w * f->asp + 4;
    if (fabsf(f->x - x) < w && fabsf(f->y - y) < h && f->z > bz) best = f, bz = f->z;
  }
  return best;
}

// ---------------------------------------------------------------- motion (tank_ref.c step, engine.js stepFish)
namespace {
void stepSchool(Scene& s, School* q, float dt) {
  const Species* S = &D.sp[q->sp];
  const float v = D.style[S->style].v * (s.sleeping ? 0.3f : 0.66f), dx = q->tx - q->x, dy = q->ty - q->y, dz = q->tz - q->z,
              d = sqrtf(dx * dx + dy * dy + dz * dz) + 0.01f;
  float wx = 0, wy = 0, wz = 0;
  if (q->wait > 0) q->wait -= dt;
  else if (d < 20) {
    const float fl = s.geo->floor0, top = TOPY + 24, b0 = S->zone == Z_top ? top : top + (fl - top) * 0.15f,
                b1 = S->zone == Z_top ? top + (fl - top) * 0.4f : fl - 50;
    q->tx = q->x < s.VW / 2 ? rnd(s.VW * 0.6f, s.VW - 50) : rnd(50, s.VW * 0.4f);
    if (rnd(0, 1) < 0.25f) q->tx = clampf(q->x + rnd(-70, 70), 50, s.VW - 50);
    q->ty = clampf(q->y + rnd(-70, 70), b0, b1), q->tz = rnd(18, ZD - 18);
    if (rnd(0, 1) < 0.3f) q->wait = rnd(0.8f, 3.5f);
  } else wx = dx / d * v, wy = dy / d * v * 0.7f, wz = dz / d * v * 0.6f;
  float k = dt * 1.4f;
  if (k > 1) k = 1;
  q->vx += (wx - q->vx) * k, q->vy += (wy - q->vy) * k, q->vz += (wz - q->vz) * k;
  q->x += q->vx * dt, q->y += q->vy * dt, q->z = clampf(q->z + q->vz * dt, 8, ZD - 8);
}

int nearestFood(Scene& s, const Fish* f) {
  if (!s.nfood || f->full >= 3 || f->S->eats == 2) return -1;
  const bool low = lowStyle(f), scraps = f->S->eats == 1;
  int best = -1;
  float bd = low ? 160 : 230;
  for (int i = 0; i < s.nfood; i++) {
    const FoodI* p = &s.food[i];
    const bool mine = p->kind == f->S->food && s.appetite[p->kind];
    if (!(mine || (scraps && p->lying))) continue;
    if (low && !p->lying) continue;
    if (!low && p->lying && p->rest > 1.5f && f->S->zone != Z_low) continue;
    const float d = hypotf(p->x - f->x, (p->y - f->y) * 1.2f);
    if (d < bd) bd = d, best = i;
  }
  return best;
}

void stepFish(Scene& s, Fish* f, float dt) {
  const StyleK* st = f->st;
  const Species* S = f->S;
  const bool low = lowStyle(f);
  bool rush = false;
  const float L = S->w * f->size, hgt = L * f->asp, calm = (s.sleeping ? 0.35f : 1) * (f->sick ? 0.55f : f->sad ? 0.78f : 1);
  float tx, ty, tz, sd, b0, b1;
  f->full -= dt / 30;
  if (f->full < 0) f->full = 0;
  if (f->gulp > 0) f->gulp -= dt;
  if (f->puff > 0) f->puff -= dt;
  if (f->nafraid && f->flee <= 0)
    for (int i = 0; i < f->nafraid; i++) {  // a hunter comes near: away from it
      const Fish* o = s.find(f->afraid[i]);
      if (!o) continue;
      const float ax = f->x - o->x, ay = f->y - o->y;
      if (ax * ax + ay * ay > D.R.afraidPx * D.R.afraidPx || fabsf(f->z - o->z) > 60) continue;
      if (S->style == ST_crawl) {
        if (fabsf(f->kx) < 5) f->kx = (ax >= 0 ? 1 : -1) * 60, f->ky = -22;
        break;
      }
      const float a = atan2f(ay, ax) + rnd(-0.35f, 0.35f);
      s.band(f, &b0, &b1);
      f->flee = rnd(0.5f, 0.85f), f->fx = clampf(f->x + fcos(a) * 115, 20, s.VW - 20), f->fy = clampf(f->y + fsin(a) * 60, b0, b1);
      f->beat = true;
      break;
    }
  int pi = -1;
  if (f->flee > 0) {
    f->flee -= dt, tx = f->fx, ty = f->fy, tz = f->z, sd = st->vmax * f->vk, rush = true, f->wait = 0;
  } else if ((pi = nearestFood(s, f)) >= 0) {
    FoodI* p = &s.food[pi];
    tx = p->x, ty = p->y, tz = p->z, sd = st->vmax * 0.72f, rush = true, f->wait = 0;
    const float dsc = 0.74f + 0.26f * f->z / ZD, nx = f->x + fcos(f->yaw) * L * dsc * 0.36f, ny = f->y - fsin(f->pitch) * L * dsc * 0.36f;
    if (fabsf(nx - p->x) < 5 + L * 0.06f && fabsf(ny - p->y) < 5 + hgt * 0.12f) {
      const int kind = p->kind;
      const float val = p->food;
      s.food[pi] = s.food[--s.nfood];
      f->full += 1, f->gulp = 0.3f;
      if (s.onEat) s.onEat(kind, val);
    }
  } else if (s.fingerOn && !low && S->style != ST_upright && hypotf(s.fingerX - f->x, s.fingerY - f->y) < 170) {
    tx = s.fingerX + fsin(f->seed * 7) * 22, ty = s.fingerY + fcos(f->seed * 5) * 16, tz = ZD - 10 - fmodf(f->seed, 20);
    sd = st->v * 1.5f, f->wait = 0, s.played += dt;
  } else if (f->school >= 0 && s.schools[f->school].n) {  // keep station on the school
    School* q = &s.schools[f->school];
    f->ot -= dt;
    if (f->ot <= 0) s.reoffset(f);
    const float px = q->x + f->ox, py = q->y + f->oy, sv = hypotf(q->vx, q->vy);
    tz = clampf(q->z + f->oz, 4, ZD - 4);
    if (sv > 3) {
      const float ux = q->vx / sv, uy = q->vy / sv, e = (px - f->x) * ux + (py - f->y) * uy;
      tx = px + ux * 48, ty = py + uy * 30, sd = sv * clampf(1 + e / 26, 0.45f, 2.1f);
    } else {
      tx = px, ty = py;
      const float d = hypotf(tx - f->x, ty - f->y);
      sd = d < 30 ? 0 : st->v * clampf(d / 50, 0.3f, 1) * calm;
    }
    tx = clampf(tx, 12, s.VW - 12);
  } else {
    if (f->wait > 0) {
      f->wait -= dt, tx = f->tx, ty = f->ty, tz = f->tz, sd = 0;
    } else {
      const float ex_ = f->tx - f->x, ez = (f->tz - f->z) * 0.5f, ey = low ? 0 : (f->ty - f->y) * 0.6f,
                  d = sqrtf(ex_ * ex_ + ey * ey + ez * ez);
      if (d < L * 0.3f + 7) {
        if (rnd(0, 1) < st->pauseP + (s.sleeping ? 0.3f : 0)) f->wait = rnd(st->pause0, st->pause1) * (s.sleeping ? 3 : 1);
        s.pick(f);
      }
      tx = f->tx, ty = f->ty, tz = f->tz, sd = f->sd * clampf(d / (L * 1.1f + 12), 0.3f, 1) * calm;
    }
    if (s.sleeping && !low) {
      const float m = lerpf(TOPY, s.geo->floor0, 0.55f);
      if (ty < m) ty = m;
    }
  }
  if (low) ty = s.floorY(tz) - hgt * 0.5f - (rush ? 0 : f->hop);
  else ty += fsin(s.Tm * f->mw + f->seed) * st->mea;
  tz = clampf(tz + f->zp * 26, 3, ZD - 3);
  if (S->fixed) {  // a starfish: creeps, never turns
    const float dx = tx - f->x, dz = tz - f->z, d = hypotf(dx, dz) + 0.01f, v = (st->v < d ? st->v : d) * (sd > 0 ? 1 : 0);
    f->x += dx / d * v * dt, f->z += dz / d * v * dt, f->y = s.floorY(f->z) - hgt * 0.42f, f->yaw = K_PI, f->amp = 0.4f;
    f->ph += dt * 1.2f;
    return;
  }
  // heading: turn toward the target, but stay side-on to the glass
  const float hx = fcos(f->yaw), dy = ty - f->y, dzw = tz - f->z;
  float dx = tx - f->x;
  if (dx > -7 && dx < 7) dx = hx >= 0 ? 7 : -7;
  if (sd <= 0.01f && !rush) dx = hx >= 0 ? 40 : -40;
  const float lim = fabsf(dx) * 0.5f, want = atan2f(clampf(dzw * 0.6f, -lim, lim), dx), err = wrapPi(want - f->yaw),
              wn = st->wn * (rush ? 1.3f : 1), om = st->om * (rush ? 1.25f : 1);
  f->om += (wn * wn * err - 2 * wn * f->om) * dt;
  f->om = clampf(f->om, -om, om);
  f->yaw = wrapPi(f->yaw + f->om * dt);
  const float flat = hypotf(dx, dzw * 0.4f), wp = sd > 0.01f ? clampf(atan2f(-dy, flat + 12) * 0.9f, -st->pitch, st->pitch) : 0;
  float k = dt * 3.2f;
  f->pitch += (wp - f->pitch) * (k > 1 ? 1 : k);
  // effort and speed
  float aligned = fcos(err), eff;
  if (aligned < 0) aligned = 0;
  if (st->glide > 0 && !rush) {
    if (f->beat) {
      if (f->spd >= sd) f->beat = false;
    } else if (f->spd < sd * st->glide) f->beat = true;
    eff = f->beat && sd > 0.5f ? 1 : 0.06f;
  } else eff = clampf(sd / st->vmax + (sd - f->spd) / (st->vmax * 0.35f), 0.1f, 1);
  if (f->gulp > 0 && eff < 0.9f) eff = 0.9f;
  k = dt * (eff > f->amp ? 7 : 3.2f);
  f->amp += (eff - f->amp) * (k > 1 ? 1 : k);
  f->spd += (f->amp * st->vmax * st->drag * (0.35f + 0.65f * aligned) - st->drag * f->spd) * dt;
  if (f->spd < 0) f->spd = 0;
  f->ph += dt * 6.2832f * (st->f0 + st->f1 * f->amp) * (s.sleeping ? 0.6f : 1);
  // move
  const float cp = fcos(f->pitch), lm = st->lift * (f->hop > 40 ? 3.5f : 1),
              wl = clampf(dy * 1.2f, -lm, lm) * (low ? 1 : sd > 0.01f || f->wait > 0 ? 1 : 0.3f);
  k = dt * 2.5f;
  f->lift += (wl - f->lift) * (k > 1 ? 1 : k);
  const float vy = -f->spd * fsin(f->pitch) + f->lift + f->ky;
  f->x += (f->spd * cp * fcos(f->yaw) + f->kx) * dt, f->z += f->spd * cp * fsin(f->yaw) * 0.6f * dt, f->y += vy * dt;
  k = dt * 4;
  if (k > 1) k = 1;
  f->kx -= f->kx * k, f->ky -= f->ky * k, f->vyS += (vy - f->vyS) * k;
  const float m = L * 0.3f + 4;
  if (f->x < m) {
    f->x = m;
    if (f->flee <= 0 && f->tx < m + 30) f->tx = rnd(s.VW * 0.4f, s.VW - m);
  } else if (f->x > s.VW - m) {
    f->x = s.VW - m;
    if (f->flee <= 0 && f->tx > s.VW - m - 30) f->tx = rnd(m, s.VW * 0.6f);
  }
  f->z = clampf(f->z, 2, ZD - 2);
  const float fy = s.floorY(f->z) - hgt * 0.5f - (low ? 0 : 2), top = TOPY + hgt * 0.5f;
  if (f->y > fy) f->y = fy;
  if (f->y < top) {
    f->y = top;
    if (low) f->hop = 0;
  }
  if (low && f->hop > 0 && f->y <= fy - f->hop + 3) f->hop = 0;
}
}  // namespace

void Scene::step(float dt) {
  Tm += dt;
  for (int i = 0; i < D.nsp; i++)
    if (schools[i].n > 0) stepSchool(*this, &schools[i], dt);
  for (int i = 0; i < nfish; i++) {  // fish that would cover one another slip in front or behind
    Fish* a = &fish[i];
    a->zp = 0;
    for (int j = 0; j < nfish; j++) {
      if (i == j) continue;
      const Fish* b = &fish[j];
      const float dz = a->z - b->z;
      if (dz > 18 || dz < -18) continue;
      const float dx = a->x - b->x, dy = a->y - b->y, rx = (a->S->w + b->S->w) * 0.36f,
                  ry = (a->S->w * a->asp + b->S->w * b->asp) * 0.3f;
      if (dx > -rx && dx < rx && dy > -ry && dy < ry) a->zp += (dz > 0 || (dz == 0 && a->uid > b->uid) ? 1 : -1) * (1 - fabsf(dz) / 18);
    }
  }
  for (int i = 0; i < nfish; i++) stepFish(*this, &fish[i], dt);
  for (int i = nfood - 1; i >= 0; i--) {
    FoodI* p = &food[i];
    if (!p->lying) {
      p->y += p->vy * dt, p->x += fsin(Tm * 2.1f + p->s) * 5 * dt;
      const float fy = floorY(p->z) - 1.5f;
      if (p->y >= fy) p->y = fy, p->lying = true, p->rest = 0;
    } else if ((p->rest += dt) > foodRest) {
      food[i] = food[--nfood];
      if (onRot) onRot();
    }
  }
  for (int i = 0; i < ncoins; i++) {
    CoinI* c = &coins[i];
    c->t += dt;
    if (!c->rest) {
      c->y += c->vy * dt, c->x += fsin(Tm * 1.7f + c->s) * 7 * dt;
      const float fy = coinY(c->z);
      if (c->y >= fy) c->y = fy, c->rest = true;
    }
  }
  if (air > 0 && rnd(0, 1) < dt * (2.2f + 3.5f * air) && nbub < MAXBUB) {
    float x = geo->air[0], y = floorY(geo->air[1] * ZD) - 2;
    for (int i = 0; i < ndecor; i++)
      if (decor[i].bubbler) x = decor[i].x + 1, y = floorY(decor[i].z) - decor[i].w * 1.25f;
    bub[nbub++] = {x + rnd(-3, 3), y, rnd(0.9f, 2.3f), rnd(30, 52), rnd(0, 6)};
  }
  if (rnd(0, 1) < dt * 0.25f && nfish && nbub < MAXBUB) {
    const Fish* f = &fish[(int)rnd(0, nfish - 0.01f)];
    if (f->S->style != ST_crawl) bub[nbub++] = {f->x + fcos(f->yaw) * f->S->w * 0.35f, f->y, rnd(0.6f, 1.1f), rnd(24, 36), rnd(0, 6)};
  }
  for (int i = nbub - 1; i >= 0; i--) {
    Bub* b = &bub[i];
    b->y -= b->v * dt, b->x += fsin(Tm * 3.1f + b->s) * 7 * dt, b->r += dt * 0.12f;
    if (b->y <= TOPY - 8) bub[i] = bub[--nbub];
  }
  for (auto& p : specks) {
    p.x += fsin(Tm * 0.3f + p.s) * 2.2f * dt + 0.8f * dt, p.y += fcos(Tm * 0.23f + p.s * 1.7f) * 1.6f * dt;
    if (p.x > VW + 4) p.x = -4;
  }
  for (int i = nrings - 1; i >= 0; i--)
    if ((rings[i].t += dt) >= 0.7f) rings[i] = rings[--nrings];
  for (int i = 0; i < nplants; i++) plants[i].ph += dt * 6.2832f * D.plant[plants[i].k].hz;
  const float kt = std::min(1.0f, dt * 2.2f), kh = std::min(1.0f, dt * 1.5f);
  for (int i = 0; i < 3; i++) tintNow[i] += (tint[i] - tintNow[i]) * kt;
  hazeNow += (haze - hazeNow) * kh;
}

// ---------------------------------------------------------------- the picture
namespace {
// the background as it is now; made again when the light or the dirt moved by a visible step (not every frame)
// The background as it is now: per channel the picture's value to the one shown (murk, light), made again with the
// tint or the haze; then baked (bake()) with the algae film.
uint8_t lr[32], lg[64], lb[32];
void makeBg(Scene& s) {
  const bool marine = D.look[s.look].marine;
  const int murk[3] = {marine ? 96 : 98, marine ? 128 : 112, marine ? 104 : 52};
  const float h = s.hazeNow * 0.34f;
  uint8_t r[32], g6[64], b[32];
  for (int v = 0; v < 64; v++) {
    const int g = (v * 255 + 31) / 63, c = (std::min(v, 31) * 255 + 15) / 31;
    g6[v] = (uint8_t)std::min(63, (int)((g * (1 - h) + murk[1] * h) * s.tintNow[1]) >> 2);
    if (v < 32) {
      r[v] = (uint8_t)std::min(31, (int)((c * (1 - h) + murk[0] * h) * s.tintNow[0]) >> 3);
      b[v] = (uint8_t)std::min(31, (int)((c * (1 - h) + murk[2] * h) * s.tintNow[2]) >> 3);
    }
  }
  const uint32_t green = ex(rgb565(44 * s.tintNow[0], 96 * s.tintNow[1], 30 * s.tintNow[2]));
  const bool fresh = s.bgBad;  // a new picture: all of it now; the haze or the light drifting: a few rows a picture
  for (int i = 0; i < 3; i++) s.bgTint[i] = s.tintNow[i];
  s.bgHaze = s.hazeNow, s.bgBad = false;
  // the water clearing or the lamp fading move the tables a step now and then: the same tables, nothing to make
  if (!fresh && green == s.filmG && !memcmp(r, lr, 32) && !memcmp(g6, lg, 64) && !memcmp(b, lb, 32)) return;
  memcpy(lr, r, 32), memcpy(lg, g6, 64), memcpy(lb, b, 32);
  s.filmG = green;
  s.filmDirty(0, 0, 1 << 14, 1 << 14);
  s.filmSoft = !fresh;
}

// The part filmX0.. of the background made again: the film's weight a pixel from the algae grid (the grid smoothed,
// a little grain: as the page), then bgc = the picture as it is now under that film. Integers; a row's empty cells
// are skipped whole.
HOT void bake(Scene& s) {
  const int X0 = s.filmX0, X1 = s.filmX1, FW = s.FW, FH = s.FH;
  const uint32_t green = s.filmG;
  const float cell = 16 * s.SC;
  int rowA[33];
  // Blocks of 16 rows: each column's 16 pixels of the block are one run in the column-wise copies (bgc when bgCols,
  // filmT), not 16 cache lines. A soft change makes 48 rows a picture.
  const int Yend = s.filmSoft ? std::min(s.filmY1, s.filmY0 + 48) : s.filmY1;
  if (s.algaeOn && !s.filmT) s.filmT = (uint8_t*)heap_caps_calloc(1, FW * FH, MALLOC_CAP_SPIRAM);
  for (int B = s.filmY0; B < Yend; B += 16) {
    const int BE = std::min(Yend, B + 16);
    for (int Y = B; Y < BE; Y++) {
      uint8_t* w = s.filmW + Y * FW;
      memset(w + X0, 0, X1 - X0);
      bool any = false;
      if (s.algaeOn) {
        const float gy = clampf((Y + 0.5f) / cell - 0.5f, 0, s.AR - 1.001f);
        const int j = (int)gy, fy = (int)((gy - j) * 256);
        for (int i = 0; i < s.AC; i++) {
          const float a = s.algae[j * s.AC + i] * (256 - fy) / 256 + s.algae[(j + 1) * s.AC + i] * fy / 256;
          rowA[i] = (int)(a * 1024), any |= rowA[i] > 20;
        }
        rowA[s.AC] = rowA[s.AC - 1];
      }
      for (int i = 0; any && i < s.AC; i++) {  // between cell centres i and i + 1 (the edges: clamped as the reference)
        if (rowA[i] < 20 && rowA[i + 1] < 20) continue;
        const int xa = std::max(X0, i == 0 ? 0 : (int)algX[i]), xb = std::min(X1, i == s.AC - 1 ? FW : (int)algX[i + 1]);
        const int a0 = rowA[i], da = rowA[i + 1] - rowA[i];
        for (int X = xa; X < xb; X++) {
          const int a = a0 + ((da * algF[X]) >> 8);                                // 0..1024
          const int grain = 154 + 12 * ((((X * 37 + Y * 91) ^ (X * Y)) & 3));     // (0.6 + 0.14 * k / 3) * 256
          const int v = (a * grain) >> 13;                                         // 0..32
          w[X] = v > 32 ? 32 : v < 0 ? 0 : v;
        }
      }
      if (!s.bgCols) {
        const uint16_t* src = s.bgRaw + Y * FW;
        uint16_t* d = s.bgc + Y * FW;
        for (int X = X0; X < X1; X++) {
          const uint16_t c = src[X];
          const uint16_t o = (uint16_t)(lr[c >> 11] << 11 | lg[c >> 5 & 63] << 5 | lb[c & 31]);
          d[X] = w[X] ? pk(mix(ex(o), green, w[X])) : o;
        }
      }
    }
    if (s.bgCols)
      for (int X = X0; X < X1; X++) {
        uint16_t* d = s.bgc + X * FH + B;
        for (int Y = B; Y < BE; Y++) {
          const uint16_t c = s.bgRaw[Y * FW + X];
          const uint16_t o = (uint16_t)(lr[c >> 11] << 11 | lg[c >> 5 & 63] << 5 | lb[c & 31]);
          const int w = s.filmW[Y * FW + X];
          *d++ = w ? pk(mix(ex(o), green, w)) : o;
        }
      }
    if (s.filmT)
      for (int X = X0; X < X1; X++) {
        uint8_t* d = s.filmT + X * FH + B;
        for (int Y = B; Y < BE; Y++) *d++ = s.filmW[Y * FW + X];
      }
  }
  if (Yend < s.filmY1) s.filmY0 = Yend;
  else s.filmX0 = s.filmX1 = 0, s.filmSoft = false;
}

void chainOf(Scene& s, const Fish* f, float dsc, Chain& ch, Look_& K) {
  const StyleK* st = f->st;
  const int n = NSEG;
  const float S = s.SC, L = f->S->w * f->size * dsc * S;
  const Spr* sp = f->half->w >= L * 0.98f ? f->half : f->pic;
  const float vs0 = L / sp->w, hgt = sp->h * vs0;
  const float wob = st->wob != 0 ? st->wob * fsin(f->ph) * (0.35f + 0.65f * f->amp) : 0, yaw = f->yaw + wob;
  const int face = fcos(yaw) >= 0 ? 1 : -1;
  const float A = s.wag ? st->A * (0.22f + 0.78f * f->amp) : 0, bend = clampf(-f->om * 0.22f, -1.1f, 1.1f),
              tp = tanf(clampf(f->pitch, -0.6f, 0.6f)), ds = L / n,
              drape = clampf(-f->vyS * 0.05f, -0.35f, 0.35f) * hgt * (st->A > 0.5f ? 0.5f : 0.25f), sy = fsin(yaw) * face;
  float x = 0, pivot = 0;
  const int ip = (int)(n * PIVOT + 0.5f);
  for (int i = 0; i <= n; i++) {
    const float u = (float)i / n, env = st->env[i], wave = fsin(f->ph - st->k * u), local = A * env * wave + bend * st->u13[i];
    ch.PX[i] = x;
    if (i == ip) pivot = x;
    ch.SH[i] = clampf(1 + 0.28f * fsin(local) + 0.06f * sy, 0.72f, 1.3f);
    ch.PY[i] = (s.wag ? hgt * st->dy * env * (0.2f + 0.8f * f->amp) * fsin(f->ph - st->k * u - 0.9f) : 0) + drape * u * u;
    if (i < n) {
      const float um = (i + 0.5f) / n, am = yaw + A * st->envM[i] * fsin(f->ph - st->k * um) + bend * st->um13[i],
                  c = fcos(am), sn = fsin(am);
      x += ds * sqrtf(c * c + THICK * THICK * sn * sn);
    }
  }
  for (int i = 0; i <= n; i++) ch.PY[i] += (ch.PX[i] - pivot) * tp;
  const float bob = st->bob != 0 ? fsin(s.Tm * 1.25f + f->seed) * st->bob * S * (1.2f - f->amp) : 0,
              wy = st->wob != 0 ? fsin(f->ph * 2) * 0.6f * S * f->amp : 0;
  float puff = 1;
  if (f->puff > 0) {
    float k = f->puff * 2.5f, k2 = (2.6f - f->puff) * 6;
    if (k > 1) k = 1;
    if (k2 < k) k = k2;
    puff = 1 + 0.42f * k;
  }
  ch.s = sp, ch.ox = f->x * S + face * pivot, ch.oy = f->y * S + bob + wy, ch.dir = -face, ch.vs = vs0 * puff;
  ch.alpha = !strcmp(f->S->id, "glasscat") ? 218 : 256;
  ch.sick = f->sick, ch.face = face, ch.pivot = pivot, ch.hgt = hgt, ch.puff = puff, ch.dsc = dsc, ch.seed = f->seed, ch.bobwy = bob + wy;
  // the shadow on the sand when it is near it
  ch.shA = 0;
  const bool low = lowStyle(f);
  const float fl = s.floorY(f->z);
  if (low || f->y > fl - hgt / S * 0.5f - 14) {
    const float near_ = clampf(1 - (fl * S - (f->y * S + hgt * 0.5f)) / (16 * S), 0, 1);
    if (near_ > 0) ch.shX = f->x * S, ch.shY = (fl - 1.5f) * S, ch.shRx = x * 0.42f, ch.shRy = 2.6f * S * dsc, ch.shA = 0.26f * near_, ch.shE = 3 * S;
  }
  if (f->sad) K.fog = std::min(22, K.fog + 3);
}

void boxOf(Item& it, float x0, float y0, float x1, float y1) {
  it.x0 = (int16_t)std::max(-32000.0f, floorf(x0) - 2), it.y0 = (int16_t)std::max(-32000.0f, floorf(y0) - 2);
  it.x1 = (int16_t)std::min(32000.0f, ceilf(x1) + 2), it.y1 = (int16_t)std::min(32000.0f, ceilf(y1) + 2);
}
}  // namespace

// The dog's pop-up over the tank (screen.cpp): its big pictures go, so the call has room to record (the 2.8" board,
// 2026-11-21: "no room to record"); the next prepare() makes them again from the look's picture.
void Scene::sleep() { freeBg(); }
void Scene::freeBg() {
  if (bgc != bgRaw) heap_caps_free(bgc);
  heap_caps_free(bgRaw), heap_caps_free(filmW), heap_caps_free(filmT);
  bgRaw = bgc = nullptr, filmW = filmT = nullptr, filmX0 = filmX1 = 0, filmSoft = false, lite = false;
}
void Scene::allocBg() {
  bgRaw = (uint16_t*)heap_caps_malloc(FW * FH * 2, MALLOC_CAP_SPIRAM);
  bgc = bgRaw ? (uint16_t*)heap_caps_malloc(FW * FH * 2, MALLOC_CAP_SPIRAM) : nullptr;
  filmW = bgc ? (uint8_t*)heap_caps_calloc(1, FW * FH, MALLOC_CAP_SPIRAM) : nullptr;
#ifdef SIM
  if (const char* e = getenv("SIM_TANK_LITE"); e && *e == '1') heap_caps_free(filmW), filmW = nullptr;  // (QA: the light water)
#endif
  if ((lite = bgRaw && !filmW)) heap_caps_free(bgc), bgc = bgRaw, bgCols = false;
  bgLoad = bgBad = true, bgTint[0] = -1;
}
void Scene::prepare() {
  if (!fr || !geo) return;
  const uint32_t p0 = micros();
  if (!bgRaw) allocBg();  // after sleep()
  if (bgLoad && bgRaw) {
    if (!background(D.look[look].id, land, FW, FH, bgRaw)) memset(bgRaw, 0, FW * FH * 2);
    bgLoad = false, bgBad = true;
  }
  if (bgBad || fabsf(bgHaze - hazeNow) > 0.01f || fabsf(bgTint[0] - tintNow[0]) > 0.006f ||
      fabsf(bgTint[1] - tintNow[1]) > 0.006f || fabsf(bgTint[2] - tintNow[2]) > 0.006f)
    if (bgRaw && bgc) makeBg(*this);
  if (bgColsWant != bgCols && !lite) bgCols = bgColsWant, filmDirty(0, 0, 1 << 14, 1 << 14), filmSoft = false;  // the other way round: all again, now
  if (filmX0 < filmX1 && bgRaw && bgc && filmW) bake(*this);
  const uint32_t p1 = micros();
  prep[0] += p1 - p0;
  Frame& F = *fr;
  const float S = SC, hz = hazeNow;
  const bool marine = D.look[look].marine;
  const int murk[3] = {marine ? 96 : 98, marine ? 128 : 112, marine ? 104 : 52};
  F.fogc = ex(rgb565((int)(lerpf(geo->water[0], murk[0], hz * 0.6f) * tintNow[0]), (int)(lerpf(geo->water[1], murk[1], hz * 0.6f) * tintNow[1]),
                     (int)(lerpf(geo->water[2], murk[2], hz * 0.6f) * tintNow[2])));
  F.tr = (int)(tintNow[0] * 256), F.tg = (int)(tintNow[1] * 256), F.tb = (int)(tintNow[2] * 256);
  if (F.tr > 250 && F.tg > 250 && F.tb > 250) F.tr = F.tg = F.tb = 256;
  int n = 0;
  auto add = [&](float z, int kind, int i) {
    Item& it = F.items[n++];
    it.z = z, it.kind = kind, it.i = i, it.fog = fogOf(z, hz);
    return &it;
  };
  for (int i = 0; i < nplants; i++) {
    const PlantI& p = plants[i];
    const PlantK& k = D.plant[p.k];
    Item* it = add(p.z, 0, i);
    const float dsc = 0.74f + 0.26f * p.z / ZD, w = k.w * p.s * dsc * S, kk = w / p.pic->w, by = (floorY(p.z) + 3) * S,
                sw = fabsf(k.a * p.s * dsc * S) + 1;
    boxOf(*it, p.x * S - w / 2 - sw, by - p.pic->h * kk, p.x * S + w / 2 + sw, by + 3 * S);
    // its sway, row by row (as blitRows worked it out)
    const float a = k.a * p.s * dsc * S, ytop = by - p.pic->h * kk, hh = p.pic->h * kk, ihh = 1 / hh;
    const int Y0 = std::max(0, (int)ceilf(ytop - 1)), Y1 = std::min(FH - 1, (int)floorf(ytop + hh));
    float* T = F.sway[i];
    for (int Y = Y0; Y <= Y1; Y++) {
      const float tt = clampf(1 - (Y + 0.5f - ytop) * ihh, 0, 1);
      T[Y] = a != 0 ? a * fpow(tt, k.pw) * fsin(p.ph - k.k * tt) : 0;
    }
  }
  for (int i = 0; i < ndecor; i++) {
    const DecorI& d = decor[i];
    Item* it = add(d.z, 1, i);
    it->fog = it->fog * 7 / 10;
    const float dsc = 0.74f + 0.26f * d.z / ZD, w = d.w * dsc * S, kk = w / d.pic->w, by = (floorY(d.z) + 2) * S;
    boxOf(*it, d.x * S - w / 2 - 3 * S, by - d.pic->h * kk, d.x * S + w / 2 + 3 * S, by + 4 * S);
  }
  for (int i = 0; i < nfish; i++) {
    Item* it = add(fish[i].z, 2, i);
    const float dsc = 0.74f + 0.26f * fish[i].z / ZD;
    Look_ K = {it->fog, F.fogc, F.tr, F.tg, F.tb};
    Chain& ch = F.chains[i];
    chainOf(*this, &fish[i], dsc, ch, K);
    it->fog = K.fog;
    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
    for (int k = 0; k <= NSEG; k++) {
      const float x = ch.ox + ch.dir * ch.PX[k];
      x0 = std::min(x0, x), x1 = std::max(x1, x), y0 = std::min(y0, ch.PY[k]), y1 = std::max(y1, ch.PY[k]);
    }
    const float hh = ch.s->h * ch.vs * 0.5f + 1;
    y0 = ch.oy + y0 - hh, y1 = ch.oy + y1 + hh;
    if (ch.shA > 0) x0 = std::min(x0, ch.shX - ch.shRx - ch.shE), x1 = std::max(x1, ch.shX + ch.shRx + ch.shE), y1 = std::max(y1, ch.shY + ch.shRy + ch.shE);
    boxOf(*it, x0, y0, x1, y1);
  }
  for (int i = 0; i < nfood; i++) {
    Item* it = add(food[i].z, 3, i);
    boxOf(*it, food[i].x * S - 6 * S, food[i].y * S - 6 * S, food[i].x * S + 6 * S, food[i].y * S + 6 * S);
  }
  for (int i = 0; i < ncoins; i++) {
    Item* it = add(coins[i].z, 4, i);
    it->fog = it->fog * 4 / 10;
    boxOf(*it, coins[i].x * S - 9 * S, coins[i].y * S - 9 * S, coins[i].x * S + 9 * S, coins[i].y * S + 9 * S);
  }
  // far to near, equals kept in order: an insertion sort (a few dozen items; std::stable_sort cost 1.9 KB of flash
  // and a buffer each picture)
  for (int a = 1; a < n; a++) {
    const Item it = F.items[a];
    int b = a - 1;
    for (; b >= 0 && F.items[b].z > it.z; b--) F.items[b + 1] = F.items[b];
    F.items[b + 1] = it;
  }
  F.n = n;
  const uint32_t p2 = micros();
  prep[1] += p2 - p1;
  // the beams of light (draw_rays' table, worked out once a frame)
  F.raysOn = rays > 0 && lamp;
  F.nray = 0;
  if (F.raysOn) {
    const int nl = FW + FH;
    float* lut = F.lut;
    memset(lut, 0, sizeof(float) * (nl + 1));
    for (int k = 0; k < 5; k++) {
      const float c = (0.12f + k * 0.21f + 0.06f * fsin(Tm * 0.13f * (1 + k * 0.37f) + k * 2.1f)) * nl * 0.82f,
                  w = (10 + 9 * ((k * 7) % 3)) * S, a = (0.45f + 0.55f * fsin(Tm * 0.21f * (1 + k * 0.23f) + k * 1.9f)) * 26 * rays;
      if (a <= 0) continue;
      const int i0 = std::max(0, (int)floorf(c - w)), i1 = std::min(nl - 1, (int)ceilf(c + w));
      for (int i = i0; i <= i1; i++) {
        const float d = (i - c) / w;
        lut[i] += a * (1 - d * d) * (1 - d * d);
      }
    }
    int in = -1;
    for (int i = 0; i <= nl; i++) {
      const float v = i < nl ? lut[i] : 0;
      F.rayLut[i] = (uint16_t)std::min(65535.0f, v * 16);
      const bool on = v >= 0.5f;
      if (on && in < 0) in = i;
      if (!on && in >= 0) {
        if (F.nray < 5) F.rayX0[F.nray] = in, F.rayX1[F.nray] = i, F.nray++;
        else F.rayX1[4] = i;
        in = -1;
      }
    }
    for (int c = 0; c < 3; c++) F.rayK[c] = (int)(geo->light[c] / 255.0f * tintNow[c] * (c == 1 ? 63 : 31) / 255.0f * 4096 + 0.5f);
  }
  prep[2] += micros() - p2;
}

HOT void Scene::draw(const Tgt& t, bool withRays) {
  if (!fr || !geo) return;
  Frame& F = *fr;
  const float S = SC;
  uint32_t pt = micros();
  auto lap = [&](int k) {
    const uint32_t n = micros();
    prof[k] += n - pt, pt = n;
  };
  filmNow = algaeOn ? filmW : nullptr, filmTNow = algaeOn ? filmT : nullptr, filmGNow = filmG, filmFW = FW, filmFH = FH;
  // 1. the background
  if (bgc) {
    bgColsWant = t.dx != 1 && t.dy == 1;  // (made so at the next prepare())
    if (!bgCols) {
      const int w = t.x1 - t.x0;
      for (int Y = t.y0; Y < t.y1; Y++) {
        const uint16_t* s = bgc + Y * FW + t.x0;
        uint16_t* d = t.at(t.x0, Y);
        if (t.dx == 1) memcpy(d, s, w * 2);
        else
          for (int X = 0; X < w; X++, d += t.dx) *d = s[X];
      }
    } else {
      const int h = t.y1 - t.y0;
      for (int X = t.x0; X < t.x1; X++) {
        const uint16_t* s = bgc + X * FH + t.y0;
        uint16_t* d = t.at(X, t.y0);
        if (t.dy == 1) memcpy(d, s, h * 2);
        else
          for (int Y = 0; Y < h; Y++, d += t.dy) *d = s[Y];
      }
    }
  }
  lap(0);
  // 2. plants, ornaments, fish, food, coins, far to near
  for (int e = 0; e < F.n; e++) {
    const Item& it = F.items[e];
    if (it.x1 < t.x0 || it.x0 >= t.x1 || it.y1 < t.y0 || it.y0 >= t.y1) continue;
    const float dsc = 0.74f + 0.26f * it.z / ZD;
    const Look_ K = {it.fog, F.fogc, F.tr, F.tg, F.tb};
    const int i = it.i;
    if (it.kind == 0) {
      const PlantI& p = plants[i];
      const PlantK& k = D.plant[p.k];
      const float w = k.w * p.s * dsc * S, kk = w / p.pic->w, by = (floorY(p.z) + 3) * S;
      disc(t, p.x * S, by - 2 * S, w * 0.42f, 3.2f * S * dsc, 0, 0.22f, 3 * S);
      blitRows(t, p.pic, p.x * S - w / 2, by - p.pic->h * kk, kk, kk, k.a * p.s * dsc * S, k.pw, p.ph, k.k, p.flip, 256, K, touched,
               F.sway[i]);
    } else if (it.kind == 1) {
      const DecorI& d = decor[i];
      const float w = d.w * dsc * S, kk = w / d.pic->w, by = (floorY(d.z) + 2) * S;
      disc(t, d.x * S, by - 2 * S, w * 0.5f, 3.6f * S * dsc, 0, 0.28f, 3 * S);
      blitRows(t, d.pic, d.x * S - w / 2, by - d.pic->h * kk, kk, kk, 0, 1, 0, 0, false, 256, K, touched);
    } else if (it.kind == 2) {
      const Chain& ch = F.chains[i];
#if TANK_QA  // the fish part's pieces (the stats line): shadows, the body and its pixels, the spots
      const uint32_t q0 = micros();
      const long px0 = touched;
#endif
      if (ch.shA > 0) disc(t, ch.shX, ch.shY, ch.shRx, ch.shRy, 0, ch.shA, ch.shE);
#if TANK_QA
      const uint32_t q1 = micros();
#endif
      blitFish(t, ch, K, touched);
#if TANK_QA
      const uint32_t q2 = micros();
      fishQa[0] += q1 - q0, fishQa[1] += q2 - q1, fishQa[3] += touched - px0;
      struct Spots {
        uint32_t t0;
        ~Spots() { fishQa[2] += micros() - t0; }
      } spots{q2};
#endif
      if (ch.sick)  // white spot: six dots that ride on the body
        for (int k = 1; k <= 6; k++) {
          const float u = 0.14f + fmodf(ch.seed * k * 0.377f, 1) * 0.6f, v = (fmodf(ch.seed * k * 0.731f, 1) - 0.5f) * 0.46f;
          int j = (int)(u * NSEG);
          if (j > NSEG - 1) j = NSEG - 1;
          const float tt = u * NSEG - j;
          disc(t, fish[i].x * S + ch.face * ch.pivot - ch.face * (ch.PX[j] + (ch.PX[j + 1] - ch.PX[j]) * tt),
               fish[i].y * S + ch.bobwy + ch.PY[j] + (ch.PY[j + 1] - ch.PY[j]) * tt + v * ch.hgt * ch.puff, 1.15f * S * ch.dsc,
               1.15f * S * ch.dsc, 0xFFFF, 0.92f, 1);
        }
    } else if (it.kind == 3) {
      const FoodI& p = food[i];  // (lying on the sand: a flake lies flat, a worm still wriggles)
      foodPiece(t, p.kind, p.x * S, p.y * S, S * dsc, p.lying && p.kind == F_flake ? 0 : Tm, p.s, tintNow, 0.95f);
    } else {
      const Spr* coin = sprNamed(P_THINGS, "coin");  // (not kept: the pictures are freed when the tank closes)
      const CoinI& c = coins[i];
      if (!coin) continue;
      const float w = 11 * dsc * S, kk = w / coin->w, tw = 0.82f + 0.18f * fabsf(fsin(c.t * 2.2f + c.s));
      blitRows(t, coin, c.x * S - w * tw / 2, c.y * S - w / 2, kk * tw, kk, 0, 1, 0, 0, false, 256, K, touched);
      if (fsin(c.t * 3 + c.s) > 0.93f) disc(t, (c.x - 2) * S, (c.y - 2) * S, 2.2f * S, 2.2f * S, rgb565(255, 255, 235), 0.8f, 2 * S);
    }
    lap(it.kind <= 1 ? 1 : it.kind == 2 ? 2 : 3);
  }
  // 3. the drifting specks, the bubbles
  const uint16_t speck = rgb565(220 * tintNow[0], 235 * tintNow[1], 225 * tintNow[2]);
  for (const auto& p : specks)
    if (p.y * S + 4 >= t.y0 && p.y * S - 4 < t.y1) disc(t, p.x * S, p.y * S, p.r * S, p.r * S, speck, 0.16f + 0.1f * p.z, 1.2f);
  const float ba = 0.85f * std::max(tintNow[1], 0.45f);
  // (at least 1.5 px round: on the 2.8" a smaller ring was a 3-pixel block, a little green square on the water)
  for (int i = 0; i < nbub; i++) bubble(t, bub[i].x * S, bub[i].y * S, std::max(bub[i].r * S, 1.5f), ba);
  lap(4);
  // 4. the light: five slow beams over the top two thirds
  if (withRays && F.raysOn && F.nray) {
    const int y0 = std::max(t.y0, rayY0), y1 = std::min(t.y1, rayY1);
    for (int Y = y0; Y < y1; Y++) {
      const int fade = rayFade[Y], sh = (int)(Y * 0.3f);
      if (!fade) continue;
      for (int r = 0; r < F.nray; r++) {
        const int xa = std::max(t.x0, F.rayX0[r] - sh), xb = std::min(t.x1, F.rayX1[r] - sh);
        if (xa >= xb) continue;
        uint16_t* d = t.at(xa, Y);
        const uint16_t* L = F.rayLut + sh;
        for (int X = xa; X < xb; X++, d += t.dx) {
          const int v = L[X] * fade >> 8;
          if (v < 8) continue;
          const int c = *d, rr = (c >> 11) + ((v * F.rayK[0] + 32768) >> 16), g = ((c >> 5) & 63) + ((v * F.rayK[1] + 32768) >> 16),
                    b = (c & 31) + ((v * F.rayK[2] + 32768) >> 16);
          *d = (uint16_t)((rr > 31 ? 31 : rr) << 11 | (g > 63 ? 63 : g) << 5 | (b > 31 ? 31 : b));
        }
      }
    }
  }
  lap(5);
  // 5. rings: a tap on the glass, a coin taken
  for (int i = 0; i < nrings; i++) {
    const Ring& r = rings[i];
    const float k = r.t / 0.7f, rr = (6 + 30 * k) * S;
    const int al = (int)((1 - k) * 0.55f * 32), m = (int)ceilf(rr * 6.3f);
    const uint32_t c = ex(r.gold ? rgb565(255, 220, 90) : 0xFFFF);
    for (int j = 0; j < m; j++) {
      const float a = (float)j / m * 6.2832f;
      const int X = (int)lroundf(r.x * S + fcos(a) * rr), Y = (int)lroundf(r.y * S + fsin(a) * rr);
      if (X < t.x0 || Y < t.y0 || X >= t.x1 || Y >= t.y1) continue;
      uint16_t* d = t.at(X, Y);
      *d = pk(mix(ex(*d), c, al));
    }
  }
  // 6. (the algae film: baked into the background and given to the things as they are drawn, see bake())
  filmNow = nullptr;  // (the screens over the water are on the glass: no film)
  lap(6);
}

void freeScene() {
  if (!sc) return;
  sc->freeBg();
  sc->~Scene();
  heap_caps_free(sc);
  sc = nullptr;
  heap_caps_free(fr), fr = nullptr;
  free(rayFade), rayFade = nullptr;
  free(algF), algF = nullptr;
}

}  // namespace tank
