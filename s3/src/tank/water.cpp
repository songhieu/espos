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
// one sprite pixel: shade (32 = as it is), the light, the fog, alpha (0..256)
HOT inline __attribute__((always_inline)) void put(uint16_t* d, uint32_t c, int a, int shade, int alpha, const Look_& K) {
  if (shade < 32) c = mix(c, 0, 32 - shade);
  else if (shade > 32) c = mix(c, 0x07E0F81Fu, (shade - 32) >> 1);
  if (K.tr != 256)
    c = ((((c >> 11) & 31) * K.tr >> 8) << 11) | ((((c >> 21) & 63) * K.tg >> 8) << 21) | ((c & 31) * K.tb >> 8);
  if (K.fog) c = mix(c, K.fogc, K.fog);
  a = (a * alpha) >> 8;
  *d = pk(mix(ex(*d), c, (a + 4) >> 3));
}

// THE FISH BLIT (tank_ref.c blit_fish), clipped to the piece t.
HOT void blitFish(const Tgt& t, const Chain& ch, const Look_& K, long& touched) {
  const Spr* s = ch.s;
  const int n = NSEG, dir = ch.dir, alpha = ch.alpha;
  const float vs = ch.vs, hh = s->h * vs, ox = ch.ox, oy = ch.oy;
  const int iv = (int)(65536.0f / vs);  // 16.16: no drift between the pieces of the picture
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
      int sy = (int)(((Y0 + 0.5f - yt) / vs - 0.5f) * 65536.0f);
      uint16_t* d = t.at(X, Y0);
      uint32_t c;
      for (int Y = Y0; Y <= Y1; Y++, sy += iv, d += t.dy) {
        const int a = sample(s, sx, sy >> 8, &c);
        if (a) put(d, c, a, shade, alpha, K), touched++;
      }
    }
  }
}

// THE PLANT BLIT (blit_rows): row by row, each row shifted sideways; a = 0 is the plain scaled blit.
HOT void blitRows(const Tgt& t, const Spr* s, float x0, float ytop, float hs, float vs, float a, float pw, float ph, float k,
              bool flip, int alpha, const Look_& K, long& touched) {
  const float ww = s->w * hs, hh = s->h * vs;
  const int ih = (int)(65536.0f / hs);
  int Y0 = (int)ceilf(ytop - 1), Y1 = (int)floorf(ytop + hh);
  if (Y0 < t.y0) Y0 = t.y0;
  if (Y1 > t.y1 - 1) Y1 = t.y1 - 1;
  for (int Y = Y0; Y <= Y1; Y++) {
    const float yr = Y + 0.5f - ytop, tt = clampf(1 - yr / hh, 0, 1), dx = a != 0 ? a * powf(tt, pw) * sinf(ph - k * tt) : 0;
    const int sy = (int)((yr / vs - 0.5f) * 256.0f);
    int X0 = (int)ceilf(x0 + dx - 1), X1 = (int)floorf(x0 + dx + ww);
    if (X0 < t.x0) X0 = t.x0;
    if (X1 > t.x1 - 1) X1 = t.x1 - 1;
    if (X1 < X0) continue;
    int sx = (int)(((X0 + 0.5f - x0 - dx) / hs - 0.5f) * 65536.0f);
    uint16_t* d = t.at(X0, Y);
    uint32_t c;
    const int fl = (s->w - 1) << 8;
    for (int X = X0; X <= X1; X++, sx += ih, d += t.dx) {
      const int v = sample(s, flip ? fl - (sx >> 8) : sx >> 8, sy, &c);
      if (v) put(d, c, v, 32, alpha, K), touched++;
    }
  }
}

// a soft disc (food, specks; shadows are squashed dark discs)
HOT void disc(const Tgt& t, float cx, float cy, float rx, float ry, uint16_t col, float al, float e) {
  int X0 = (int)floorf(cx - rx - e), X1 = (int)ceilf(cx + rx + e), Y0 = (int)floorf(cy - ry - e), Y1 = (int)ceilf(cy + ry + e);
  X0 = std::max(X0, t.x0), Y0 = std::max(Y0, t.y0), X1 = std::min(X1, t.x1 - 1), Y1 = std::min(Y1, t.y1 - 1);
  if (X1 < X0 || Y1 < Y0) return;
  const uint32_t c = ex(col);
  const float m = (rx < ry ? rx : ry) / e, irx = 1 / rx, iry = 1 / ry;
  for (int Y = Y0; Y <= Y1; Y++) {
    const float uy = (Y + 0.5f - cy) * iry, uy2 = uy * uy;
    uint16_t* d = t.at(X0, Y);
    for (int X = X0; X <= X1; X++, d += t.dx) {
      const float ux = (X + 0.5f - cx) * irx, a = clampf((1 - sqrtf(ux * ux + uy2)) * m + 0.5f, 0, 1) * al;
      const int w = (int)(a * 32.0f + 0.5f);
      if (w) *d = pk(mix(ex(*d), c, w));
    }
  }
}
HOT void bubble(const Tgt& t, float cx, float cy, float r, float al) {
  int X0 = (int)floorf(cx - r - 1), X1 = (int)ceilf(cx + r + 1), Y0 = (int)floorf(cy - r - 1), Y1 = (int)ceilf(cy + r + 1);
  X0 = std::max(X0, t.x0), Y0 = std::max(Y0, t.y0), X1 = std::min(X1, t.x1 - 1), Y1 = std::min(Y1, t.y1 - 1);
  if (X1 < X0 || Y1 < Y0) return;
  const uint32_t c = ex(rgb565(236, 248, 255));
  const float out2 = (r + 0.45f) * (r + 0.45f), gr = r * 0.34f + 0.5f, gr2 = gr * gr;
  for (int Y = Y0; Y <= Y1; Y++) {
    uint16_t* p = t.at(X0, Y);
    for (int X = X0; X <= X1; X++, p += t.dx) {
      const float dx = X + 0.5f - cx, dy = Y + 0.5f - cy, d2 = dx * dx + dy * dy;
      if (d2 > out2) continue;  // past the rim: nothing (the glint is inside)
      const float d = sqrtf(d2);
      float a = clampf(1 - fabsf(d - r + 0.5f) / 0.9f, 0, 1) * 0.62f + (d < r ? 0.07f : 0);
      const float gx = dx + r * 0.38f, gy = dy + r * 0.38f, g2 = gx * gx + gy * gy;
      if (g2 < gr2) a += (1 - sqrtf(g2) / gr) * 0.5f;
      if (a > 1) a = 1;
      const int w = (int)(a * al * 32.0f + 0.5f);
      if (w) *p = pk(mix(ex(*p), c, w));
    }
  }
}

int fogOf(float z, float haze) { return (int)(clampf((1 - z / ZD) * 0.36f + haze * 0.25f, 0, 0.7f) * 32); }

}  // namespace

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
    const float r = st->lap > 0.4f ? 90 : 60, on = cosf(f->yaw) >= 0 ? 1 : -1;
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
  heap_caps_free(bgRaw), heap_caps_free(bgc);
  bgRaw = (uint16_t*)heap_caps_malloc(FW * FH * 2, MALLOC_CAP_SPIRAM);
  bgc = (uint16_t*)heap_caps_malloc(FW * FH * 2, MALLOC_CAP_SPIRAM);
  bgLoad = bgBad = true, bgTint[0] = -1;
  free(rayFade);
  rayFade = (uint8_t*)malloc(FH);
  rayY0 = (int)(TOPY * 0.5f * SC), rayY1 = (int)(FH * 0.66f);
  for (int Y = 0; Y < FH; Y++)
    rayFade[Y] = Y < rayY0 || Y >= rayY1 ? 0 : (uint8_t)(255 * powf(1 - (float)(Y - rayY0) / (rayY1 - rayY0), 1.3f));
  free(algF);
  algF = (uint8_t*)malloc(FW);
  const float cell = 16 * SC;
  for (int i = 0; i < 32; i++) algX[i] = (int16_t)ceilf((i + 0.5f) * cell - 0.5f);  // first column at or past centre i
  for (int X = 0; X < FW; X++) {
    const float gx = clampf((X + 0.5f) / cell - 0.5f, 0, AC - 1.001f);
    algF[X] = (uint8_t)((gx - (int)gx) * 256);
  }
  if (!fr) fr = (Frame*)heap_caps_calloc(1, sizeof(Frame), MALLOC_CAP_SPIRAM);
  if (!turn) return;
  for (auto& p : specks) p.x = rnd(0, VW), p.y = rnd(TOPY, VH - 60);
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
      if (strcmp(f->S->id, "snail") && !f->S->fixed) f->kx = -cosf(f->yaw) * 70, f->ky = -34;
      continue;
    }
    float b0, b1;
    band(f, &b0, &b1);
    const float a = atan2f(f->y - y, f->x - x) + rnd(-0.4f, 0.4f);
    f->flee = rnd(0.55f, 0.9f), f->fx = clampf(f->x + cosf(a) * 130, 20, VW - 20), f->fy = clampf(f->y + sinf(a) * 70, b0, b1);
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
  }
  return got;
}
void Scene::setAlgae(float level) {  // 0..100: the cells' cover grows from the edges and corners
  const int n = AC * AR;
  float sum = 0;
  for (int i = 0; i < n; i++) sum += algae[i];
  const float want = level / 100 * n * 0.5f;
  int guard = 0;
  while (sum < want && guard++ < 400) {
    const int i = (int)rnd(0, AC - 0.01f), j = (int)rnd(0, AR - 0.01f);
    const float edge = std::min(std::min(i, AC - 1 - i), std::min(j > 2 ? AR - 1 - j : 9, 9)) / 9.0f;
    if (rnd(0, 1) < edge * 0.85f) continue;
    float& a = algae[j * AC + i];
    const float add = std::min(1 - a, 0.25f);
    a += add, sum += add;
  }
  algaeOn = sum > 0.5f;
}
float Scene::algaeLevel() const {
  float s = 0;
  const int n = AC * AR;
  for (int i = 0; i < n; i++) s += algae[i];
  return s / n * 200;
}
void Scene::clearAlgae() { memset(algae, 0, sizeof algae), algaeOn = false; }

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
      f->flee = rnd(0.5f, 0.85f), f->fx = clampf(f->x + cosf(a) * 115, 20, s.VW - 20), f->fy = clampf(f->y + sinf(a) * 60, b0, b1);
      f->beat = true;
      break;
    }
  int pi = -1;
  if (f->flee > 0) {
    f->flee -= dt, tx = f->fx, ty = f->fy, tz = f->z, sd = st->vmax * f->vk, rush = true, f->wait = 0;
  } else if ((pi = nearestFood(s, f)) >= 0) {
    FoodI* p = &s.food[pi];
    tx = p->x, ty = p->y, tz = p->z, sd = st->vmax * 0.72f, rush = true, f->wait = 0;
    const float dsc = 0.74f + 0.26f * f->z / ZD, nx = f->x + cosf(f->yaw) * L * dsc * 0.36f, ny = f->y - sinf(f->pitch) * L * dsc * 0.36f;
    if (fabsf(nx - p->x) < 5 + L * 0.06f && fabsf(ny - p->y) < 5 + hgt * 0.12f) {
      const int kind = p->kind;
      const float val = p->food;
      s.food[pi] = s.food[--s.nfood];
      f->full += 1, f->gulp = 0.3f;
      if (s.onEat) s.onEat(kind, val);
    }
  } else if (s.fingerOn && !low && S->style != ST_upright && hypotf(s.fingerX - f->x, s.fingerY - f->y) < 170) {
    tx = s.fingerX + sinf(f->seed * 7) * 22, ty = s.fingerY + cosf(f->seed * 5) * 16, tz = ZD - 10 - fmodf(f->seed, 20);
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
  else ty += sinf(s.Tm * f->mw + f->seed) * st->mea;
  tz = clampf(tz + f->zp * 26, 3, ZD - 3);
  if (S->fixed) {  // a starfish: creeps, never turns
    const float dx = tx - f->x, dz = tz - f->z, d = hypotf(dx, dz) + 0.01f, v = (st->v < d ? st->v : d) * (sd > 0 ? 1 : 0);
    f->x += dx / d * v * dt, f->z += dz / d * v * dt, f->y = s.floorY(f->z) - hgt * 0.42f, f->yaw = K_PI, f->amp = 0.4f;
    f->ph += dt * 1.2f;
    return;
  }
  // heading: turn toward the target, but stay side-on to the glass
  const float hx = cosf(f->yaw), dy = ty - f->y, dzw = tz - f->z;
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
  float aligned = cosf(err), eff;
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
  const float cp = cosf(f->pitch), lm = st->lift * (f->hop > 40 ? 3.5f : 1),
              wl = clampf(dy * 1.2f, -lm, lm) * (low ? 1 : sd > 0.01f || f->wait > 0 ? 1 : 0.3f);
  k = dt * 2.5f;
  f->lift += (wl - f->lift) * (k > 1 ? 1 : k);
  const float vy = -f->spd * sinf(f->pitch) + f->lift + f->ky;
  f->x += (f->spd * cp * cosf(f->yaw) + f->kx) * dt, f->z += f->spd * cp * sinf(f->yaw) * 0.6f * dt, f->y += vy * dt;
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
      p->y += p->vy * dt, p->x += sinf(Tm * 2.1f + p->s) * 5 * dt;
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
      c->y += c->vy * dt, c->x += sinf(Tm * 1.7f + c->s) * 7 * dt;
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
    if (f->S->style != ST_crawl) bub[nbub++] = {f->x + cosf(f->yaw) * f->S->w * 0.35f, f->y, rnd(0.6f, 1.1f), rnd(24, 36), rnd(0, 6)};
  }
  for (int i = nbub - 1; i >= 0; i--) {
    Bub* b = &bub[i];
    b->y -= b->v * dt, b->x += sinf(Tm * 3.1f + b->s) * 7 * dt, b->r += dt * 0.12f;
    if (b->y <= TOPY - 8) bub[i] = bub[--nbub];
  }
  for (auto& p : specks) {
    p.x += sinf(Tm * 0.3f + p.s) * 2.2f * dt + 0.8f * dt, p.y += cosf(Tm * 0.23f + p.s * 1.7f) * 1.6f * dt;
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
void makeBg(Scene& s) {
  const bool marine = D.look[s.look].marine;
  const int murk[3] = {marine ? 96 : 98, marine ? 128 : 112, marine ? 104 : 52};
  const float h = s.hazeNow * 0.34f;
  uint8_t lr[32], lg[64], lb[32];  // per channel: the picture's value to the one shown
  for (int v = 0; v < 64; v++) {
    const int g = (v * 255 + 31) / 63, c = (std::min(v, 31) * 255 + 15) / 31;
    lg[v] = (uint8_t)std::min(63, (int)((g * (1 - h) + murk[1] * h) * s.tintNow[1]) >> 2);
    if (v < 32) {
      lr[v] = (uint8_t)std::min(31, (int)((c * (1 - h) + murk[0] * h) * s.tintNow[0]) >> 3);
      lb[v] = (uint8_t)std::min(31, (int)((c * (1 - h) + murk[2] * h) * s.tintNow[2]) >> 3);
    }
  }
  const int n = s.FW * s.FH;
  const uint16_t* src = s.bgRaw;
  uint16_t* d = s.bgc;
  for (int i = 0; i < n; i++) {
    const uint16_t c = src[i];
    d[i] = (uint16_t)(lr[c >> 11] << 11 | lg[c >> 5 & 63] << 5 | lb[c & 31]);
  }
  for (int i = 0; i < 3; i++) s.bgTint[i] = s.tintNow[i];
  s.bgHaze = s.hazeNow, s.bgBad = false;
}

void chainOf(Scene& s, const Fish* f, float dsc, Chain& ch, Look_& K) {
  const StyleK* st = f->st;
  const int n = NSEG;
  const float S = s.SC, L = f->S->w * f->size * dsc * S;
  const Spr* sp = f->half->w >= L * 0.98f ? f->half : f->pic;
  const float vs0 = L / sp->w, hgt = sp->h * vs0;
  const float wob = st->wob != 0 ? st->wob * sinf(f->ph) * (0.35f + 0.65f * f->amp) : 0, yaw = f->yaw + wob;
  const int face = cosf(yaw) >= 0 ? 1 : -1;
  const float A = s.wag ? st->A * (0.22f + 0.78f * f->amp) : 0, bend = clampf(-f->om * 0.22f, -1.1f, 1.1f),
              tp = tanf(clampf(f->pitch, -0.6f, 0.6f)), ds = L / n,
              drape = clampf(-f->vyS * 0.05f, -0.35f, 0.35f) * hgt * (st->A > 0.5f ? 0.5f : 0.25f), sy = sinf(yaw) * face;
  float x = 0, pivot = 0;
  const int ip = (int)(n * PIVOT + 0.5f);
  for (int i = 0; i <= n; i++) {
    const float u = (float)i / n, env = st->env[i], wave = sinf(f->ph - st->k * u), local = A * env * wave + bend * st->u13[i];
    ch.PX[i] = x;
    if (i == ip) pivot = x;
    ch.SH[i] = clampf(1 + 0.28f * sinf(local) + 0.06f * sy, 0.72f, 1.3f);
    ch.PY[i] = (s.wag ? hgt * st->dy * env * (0.2f + 0.8f * f->amp) * sinf(f->ph - st->k * u - 0.9f) : 0) + drape * u * u;
    if (i < n) {
      const float um = (i + 0.5f) / n, am = yaw + A * st->envM[i] * sinf(f->ph - st->k * um) + bend * st->um13[i],
                  c = cosf(am), sn = sinf(am);
      x += ds * sqrtf(c * c + THICK * THICK * sn * sn);
    }
  }
  for (int i = 0; i <= n; i++) ch.PY[i] += (ch.PX[i] - pivot) * tp;
  const float bob = st->bob != 0 ? sinf(s.Tm * 1.25f + f->seed) * st->bob * S * (1.2f - f->amp) : 0,
              wy = st->wob != 0 ? sinf(f->ph * 2) * 0.6f * S * f->amp : 0;
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

void Scene::prepare() {
  if (!fr || !geo) return;
  if (bgLoad && bgRaw) {
    if (!background(D.look[look].id, land, FW, FH, bgRaw)) memset(bgRaw, 0, FW * FH * 2);
    bgLoad = false, bgBad = true;
  }
  if (bgBad || fabsf(bgHaze - hazeNow) > 0.01f || fabsf(bgTint[0] - tintNow[0]) > 0.006f ||
      fabsf(bgTint[1] - tintNow[1]) > 0.006f || fabsf(bgTint[2] - tintNow[2]) > 0.006f)
    if (bgRaw && bgc) makeBg(*this);
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
  std::stable_sort(F.items, F.items + n, [](const Item& a, const Item& b) { return a.z < b.z; });
  F.n = n;
  // the beams of light (draw_rays' table, worked out once a frame)
  F.raysOn = rays > 0 && lamp;
  F.nray = 0;
  if (F.raysOn) {
    const int nl = FW + FH;
    float* lut = F.lut;
    memset(lut, 0, sizeof(float) * (nl + 1));
    for (int k = 0; k < 5; k++) {
      const float c = (0.12f + k * 0.21f + 0.06f * sinf(Tm * 0.13f * (1 + k * 0.37f) + k * 2.1f)) * nl * 0.82f,
                  w = (10 + 9 * ((k * 7) % 3)) * S, a = (0.45f + 0.55f * sinf(Tm * 0.21f * (1 + k * 0.23f) + k * 1.9f)) * 26 * rays;
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
  // 1. the background
  if (bgc) {
    const int w = t.x1 - t.x0;
    for (int Y = t.y0; Y < t.y1; Y++) {
      const uint16_t* s = bgc + Y * FW + t.x0;
      uint16_t* d = t.at(t.x0, Y);
      if (t.dx == 1) memcpy(d, s, w * 2);
      else
        for (int X = 0; X < w; X++, d += t.dx) *d = s[X];
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
      blitRows(t, p.pic, p.x * S - w / 2, by - p.pic->h * kk, kk, kk, k.a * p.s * dsc * S, k.pw, p.ph, k.k, p.flip, 256, K, touched);
    } else if (it.kind == 1) {
      const DecorI& d = decor[i];
      const float w = d.w * dsc * S, kk = w / d.pic->w, by = (floorY(d.z) + 2) * S;
      disc(t, d.x * S, by - 2 * S, w * 0.5f, 3.6f * S * dsc, 0, 0.28f, 3 * S);
      blitRows(t, d.pic, d.x * S - w / 2, by - d.pic->h * kk, kk, kk, 0, 1, 0, 0, false, 256, K, touched);
    } else if (it.kind == 2) {
      const Chain& ch = F.chains[i];
      if (ch.shA > 0) disc(t, ch.shX, ch.shY, ch.shRx, ch.shRy, 0, ch.shA, ch.shE);
      blitFish(t, ch, K, touched);
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
      const FoodI& p = food[i];
      const float r = S * dsc;
      if (p.kind == F_sink) disc(t, p.x * S, p.y * S, 2.4f * r, 1.7f * r, rgb565(74 * tintNow[0], 92 * tintNow[1], 40 * tintNow[2]), 0.95f, 1);
      else if (p.kind == F_live) {
        const float a = Tm * 7 + p.s, wx = cosf(a) * 1.6f * r, wy = sinf(a * 1.3f) * 1.2f * r;
        disc(t, p.x * S - wx, p.y * S - wy, 1.2f * r, 1.2f * r, rgb565(205 * tintNow[0], 48 * tintNow[1], 44 * tintNow[2]), 0.95f, 1);
        disc(t, p.x * S + wx, p.y * S + wy, 1.2f * r, 1.2f * r, rgb565(225 * tintNow[0], 70 * tintNow[1], 60 * tintNow[2]), 0.95f, 1);
      } else disc(t, p.x * S, p.y * S, 1.5f * r, 1.5f * r, rgb565(150 * tintNow[0], 96 * tintNow[1], 44 * tintNow[2]), 0.95f, 1);
    } else {
      static const Spr* coin = nullptr;
      if (!coin) coin = sprNamed(P_THINGS, "coin");
      const CoinI& c = coins[i];
      if (!coin) continue;
      const float w = 11 * dsc * S, kk = w / coin->w, tw = 0.82f + 0.18f * fabsf(sinf(c.t * 2.2f + c.s));
      blitRows(t, coin, c.x * S - w * tw / 2, c.y * S - w / 2, kk * tw, kk, 0, 1, 0, 0, false, 256, K, touched);
      if (sinf(c.t * 3 + c.s) > 0.93f) disc(t, (c.x - 2) * S, (c.y - 2) * S, 2.2f * S, 2.2f * S, rgb565(255, 255, 235), 0.8f, 2 * S);
    }
    lap(it.kind <= 1 ? 1 : it.kind == 2 ? 2 : 3);
  }
  // 3. the drifting specks, the bubbles
  const uint16_t speck = rgb565(220 * tintNow[0], 235 * tintNow[1], 225 * tintNow[2]);
  for (const auto& p : specks)
    if (p.y * S + 4 >= t.y0 && p.y * S - 4 < t.y1) disc(t, p.x * S, p.y * S, p.r * S, p.r * S, speck, 0.16f + 0.1f * p.z, 1.2f);
  const float ba = 0.85f * std::max(tintNow[1], 0.45f);
  for (int i = 0; i < nbub; i++) bubble(t, bub[i].x * S, bub[i].y * S, bub[i].r * S, ba);
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
      const int X = (int)lroundf(r.x * S + cosf(a) * rr), Y = (int)lroundf(r.y * S + sinf(a) * rr);
      if (X < t.x0 || Y < t.y0 || X >= t.x1 || Y >= t.y1) continue;
      uint16_t* d = t.at(X, Y);
      *d = pk(mix(ex(*d), c, al));
    }
  }
  // 6. the algae film on the front glass: the grid smoothed, a little grain. Integers; a row's empty cells are
  //    skipped whole (the film is there most of the time: a float a pixel over the screen cost 80 ms in QEMU).
  if (algaeOn) {
    const uint32_t green = ex(rgb565(44 * tintNow[0], 96 * tintNow[1], 30 * tintNow[2]));
    const float cell = 16 * S;
    int rowA[32];
    for (int Y = t.y0; Y < t.y1; Y++) {
      const float gy = clampf((Y + 0.5f) / cell - 0.5f, 0, AR - 1.001f);
      const int j = (int)gy, fy = (int)((gy - j) * 256);
      bool any = false;
      for (int i = 0; i < AC; i++) {
        const float a = algae[j * AC + i] * (256 - fy) / 256 + algae[(j + 1) * AC + i] * fy / 256;
        rowA[i] = (int)(a * 1024), any |= rowA[i] > 20;
      }
      rowA[AC] = rowA[AC - 1];
      if (!any) continue;
      for (int i = 0; i < AC; i++) {  // between cell centres i and i + 1 (the edges: clamped as the reference)
        if (rowA[i] < 20 && rowA[i + 1] < 20) continue;
        const int xa = i == 0 ? 0 : algX[i], xb = i == AC - 1 ? FW : algX[i + 1];
        const int X0 = std::max(t.x0, xa), X1 = std::min(t.x1, xb);
        if (X0 >= X1) continue;
        const int a0 = rowA[i], da = rowA[i + 1] - rowA[i];
        uint16_t* d = t.at(X0, Y);
        for (int X = X0; X < X1; X++, d += t.dx) {
          const int a = a0 + ((da * algF[X]) >> 8);  // 0..1024
          const int grain = 154 + 12 * ((((X * 37 + Y * 91) ^ (X * Y)) & 3));  // (0.6 + 0.14 * k / 3) * 256
          const int w = (a * grain) >> 13;                                         // 0..32
          if (w > 0) *d = pk(mix(ex(*d), green, w > 32 ? 32 : w));
        }
      }
    }
  }
  lap(6);
}

void freeScene() {
  if (!sc) return;
  heap_caps_free(sc->bgRaw), heap_caps_free(sc->bgc);
  sc->~Scene();
  heap_caps_free(sc);
  sc = nullptr;
  heap_caps_free(fr), fr = nullptr;
  free(rayFade), rayFade = nullptr;
  free(algF), algF = nullptr;
}

}  // namespace tank
