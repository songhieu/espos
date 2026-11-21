// The PawBoop game screen: the game designer's 320x480 screen drawn from the packs into one RGB565 buffer in PSRAM,
// shown by an lv_image; each picture redraws only the rows that changed. The behaviour is the page's
// (sim/pet.html): act, feedTier, tapDog, idleNext, chatter, clipEnded, loopEnded, dropCoins, stepCoins, drawSay.
//
// Layout: the OS status bar keeps the top 24 rows, so the header and the needs (rows 0-79 of the game designer's
// screen) sit 24 px lower and cover the stage's top 24 rows (only 30 of 1120 pictures reach there: the pool
// in `bath`, the ball in `ball_fetch` / `ball_ready`). Everything from the stage down keeps its place on the
// screen, so the page's numbers (stage 80-371, buttons at y 412, the sheet at 318) are used as they are.
#include "app.h"

#include <Arduino.h>

#include <cmath>
#include <vector>

#include "brain.h"
#include "chooser.h"
#include "dog.h"
#include "dogs.h"
#include "game.h"
#include "os.h"
#include "packs.h"
#include "pet.h"
#include "trace.h"
#include "uikit.h"
#include "voice.h"
#include "book.h"
#if !ESPOS_ONBOARD
#include "pet_icon.h"
#endif
#include "text.h"
#include "apps.h"
#include "home.h"
#include "i18n.h"
#include "lock.h"
#include "pkg/pkg.h"
#include "run/run.h"
#include "sfx.h"

namespace pet {
namespace app {
namespace {

using namespace kit;

constexpr int W = 320, SB = 24, BH = 480 - SB;  // the buffer: screen rows 24..479
constexpr int STAGE_Y = 80, STAGE_VIS = 104, STAGE_B = 372, SHEET_Y = 318, FPS = 10;  // screen px, as the page
constexpr int CANVAS = STAGE_Y - SB;                                // buffer row of the stage's row 0
constexpr int BTN_Y = 412, BTN_R = 25;
constexpr int BX[5] = {36, 98, 160, 222, 284};
const char* const BTN[5] = {"feed", "play", "pet", "wash", "nap"};
constexpr int COUNTER_X = 248, COUNTER_Y = 20 + SB;  // where flying coins go (screen)
constexpr int FADE = 2;
constexpr uint16_t WHITE = 0xFFFF;

inline int bufY(int screenY) { return screenY - SB; }  // the lower part keeps its screen place
inline float frand() { return random(1000000) / 1000000.0f; }
inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }

struct Coin {
  float x0, y0, x1, y1, x, y, t, life;
  uint8_t st;  // 0 pop, 1 rest, 2 fly
  bool kept;   // she found it (waits for ever)
};

// Landscape: the game keeps its one portrait picture (every piece where the packs put it, the code unchanged), and
// the screen shows parts of it: the stage on the left at full size, on the right the header in two halves, the four
// needs in two halves and the five care buttons 3 + 2; the food sheet, when open, at the bottom right over them.
// Touches go back through the same parts (screen px of the portrait page).
struct Part {
  int16_t sx, sy, w, h, dx, dy;  // the part of the portrait page (screen px), and where the landscape screen shows it
};
constexpr int PANEL_X = 308;  // the stage shows x 6..314 of 320 (she stands in the middle)
constexpr Part PARTS[] = {
    {6, STAGE_VIS, 308, STAGE_B - STAGE_VIS, 0, 38},                                  // the stage (and the splash)
    {0, 26, 150, 34, PANEL_X + 11, 26},    {150, 26, 166, 34, PANEL_X + 3, 62},      // paw, name, level | Run, coins
    {0, 64, 160, 34, PANEL_X + 6, 98},     {160, 64, 160, 34, PANEL_X + 6, 134},     // the needs, two and two
    {BX[0] - 28, 384, 56, 74, PANEL_X + 2, 172},  {BX[1] - 28, 384, 56, 74, PANEL_X + 58, 172},
    {BX[2] - 28, 384, 56, 74, PANEL_X + 114, 172}, {BX[3] - 28, 384, 56, 74, PANEL_X + 30, 246},
    {BX[4] - 28, 384, 56, 74, PANEL_X + 86, 246},
    {0, SHEET_Y, W, 480 - SHEET_Y, 160, 320 - (480 - SHEET_Y)},                       // the food sheet, when open
};
constexpr int NPARTS = sizeof PARTS / sizeof PARTS[0], SHEET_PART = NPARTS - 1;

struct Game {
  lv_obj_t *scr = nullptr, *img = nullptr, *body = nullptr, *runPill = nullptr;
  lv_obj_t* trialChip = nullptr;  // "Trial · 3 days left" at the stage's lower left (lock.h); none outside a trial
  lv_obj_t* part[NPARTS] = {};  // landscape: the shown parts (img is not used then)
  lv_image_dsc_t partDsc[NPARTS];
  int arrived = 0;  // coins from the run, to show when she is in front again
  String dogPkg;    // the dog's package (its sounds: sfx.h)
  lv_image_dsc_t dsc;
  uint16_t *buf = nullptr, *snap = nullptr;  // the screen; the stage before a clip change (rows 80..347)
  lv_timer_t* timer = nullptr;
  // the clip
  const pbs2_clip_t* clip = nullptr;
  const brain::ClipInfo* info = nullptr;
  int i = 0, loops = 0, fade = 0;
  const pbs2_frame_t* shown = nullptr;  // the picture on screen now
  int snap0 = 0, snap1 = 0;             // stage rows the snapshot holds
  // the game's screen state
  bool sheet = false, sheetDrawn = false;
  float dropT = 0;
  int dropN = 0;
  std::vector<Coin> co;
  std::vector<float> ft;  // "+50" floats (their age)
  const char* next = nullptr;  // a line event to say once she is quiet
  // the bubble (drawn from the line's text with the page's font: every dog, every line)
  const pba_clip_t* line = nullptr;
  bool say = false;
  text::Bubble bub;
  float sayT = 0;
  // the two beats (caught_1 then caught_2): the answer waits for the first line to end
  const pba_clip_t* beat2 = nullptr;
  const char* beat2Clip = nullptr;
  const char* beat1Clip = nullptr;
  int sayY = 0;
  bool sayLow = false, sayPlaced = false;
  // header as drawn
  int hCoins = -1, hLv = -1, hFill = -1, hW[4] = {-1, -1, -1, -1};
  // redraw
  int d0 = 0, d1 = BH, prev0 = 0, prev1 = 0;
  uint32_t last = 0;
  float acc = 0, splash = 0;
  int press = -1;
  float pressT = 0;
  bool full = true;  // redraw every row at the next picture
  int downX = 0, downY = 0;
  // measures (trace every 10 s)
  uint32_t statAt = 0, pics = 0, composeUs = 0, composeMax = 0, rows = 0, lateSteps = 0, tapAt = 0;
};
Game* g = nullptr;
bool bootSplash = true;
uint32_t closedAt = 0;

// ------------------------------------------------------------------ drawing on the buffer
void mark(int a, int b) {
  a = max(0, a), b = min(BH, b);
  if (a >= b) return;
  if (g->d0 >= g->d1) g->d0 = a, g->d1 = b;
  else g->d0 = min(g->d0, a), g->d1 = max(g->d1, b);
}

// A piece's columns [c0, c1) with column c0 at x; buffer row by; rows clipped to [y0, y1).
void blit(const pbu_piece_t* p, int x, int by, int y0, int y1, int c0 = 0, int c1 = -1, int opa = 255) {
  if (!p) return;
  if (c1 < 0) c1 = p->w;
  const uint8_t* px = pk::pixels(p);
  const uint8_t* al = pk::alpha(p);
  const int r0 = max(0, max(y0, 0) - by), r1 = min((int)p->h, min(y1, BH) - by);
  for (int r = r0; r < r1; r++) {
    uint16_t* d = g->buf + (by + r) * W;
    for (int c = c0; c < c1; c++) {
      const int X = x + c - c0;
      if (X < 0 || X >= W) continue;
      const int k = r * p->w + c;
      int a = al ? al[k] : 255;
      if (opa < 255) a = a * opa / 255;
      if (a) d[X] = dog::blend(d[X], pk::rd16(px + 2 * k), a);
    }
  }
}
void blitAt(const char* name, int y0, int y1) {  // a piece at its own place (header pieces: rows as given)
  const pbu_piece_t* p = pk::piece(name);
  if (p) blit(p, p->x, p->y, y0, y1);
}
void blitLow(const char* name, int y0, int y1) {  // a piece of the lower part at its own place
  const pbu_piece_t* p = pk::piece(name);
  if (p) blit(p, p->x, bufY(p->y), y0, y1);
}

// What never changes: the game designer's "screen" piece (rows 0-79 for the header, 104-479 for the rest).
void base(int y0, int y1) {
  const pbu_piece_t* s = pk::piece("screen");
  const uint8_t* px = pk::pixels(s);
  for (int y = y0; y < y1; y++) memcpy(g->buf + y * W, px + (y < 80 ? y : y + SB) * W * 2, W * 2);
}

// The dog's name where the page writes it: the pack's own picture of it (name_<dog>, Lexend bold 14, pen at x 42,
// middle 20.5), else drawn with the same font. PawBoop 1.0's pack has "Biscuit" baked in: drawn over (only Biscuit
// plays with that pack).
void name(int y0, int y1) {
  if (y0 >= 34 || y1 <= 6) return;
  char nm[24];
  snprintf(nm, sizeof nm, "name_%s", pk::dogKey());
  if (const pbu_piece_t* np = pk::piece(nm)) return blit(np, np->x, np->y, y0, y1);
  if (!pk::piece("wake_corgi")) {  // PawBoop 1.0's pack (another dog may already be installed): cover "Biscuit"
    const uint16_t bg = pk::rd16(pk::pixels(pk::piece("screen")) + (20 * W + 100) * 2);
    for (int y = max(y0, 6); y < min(y1, 34); y++)
      for (int x = 40; x < 102; x++) g->buf[y * W + x] = bg;
  }
  const text::Target t = {g->buf, W, BH, W, y0, y1};
  text::draw(t, pk::font(1), dogs::name(), 42, 20.5f, 0x2967);
}

void header(int y0, int y1) {
  game::State& st = game::s();
  name(y0, y1);
  const pbu_piece_t* f = pk::piece("lv_fill");
  blit(f, f->x, f->y, y0, y1, 0, (int)lroundf(f->w * game::xpFrac()));
  char nm[24];
  snprintf(nm, sizeof nm, "lv_%d", st.lv);
  blitAt(nm, y0, y1);
  char num[12];
  snprintf(num, sizeof num, "%d", st.coins);
  int pen = 304;
  for (int i = (int)strlen(num) - 1; i >= 0; i--) {
    snprintf(nm, sizeof nm, "d%c", num[i]);
    const pbu_piece_t* d = pk::piece(nm);
    pen -= d->adv;
    blit(d, pen + d->x, d->y, y0, y1);
  }
  static const char* KIND[4] = {"food", "fun", "energy", "clean"};
  for (int k = 0; k < 4; k++) {
    const bool low = st.need[k] < game::G.low;
    snprintf(nm, sizeof nm, low ? "g_%s_low" : "g_%s", KIND[k]);
    blitAt(nm, y0, y1);
    snprintf(nm, sizeof nm, low ? "gf_%s_low" : "gf_%s", KIND[k]);
    const pbu_piece_t* gp = pk::piece(nm);
    const int w = (int)lroundf(fmaxf(6.0f, gp->w * st.need[k] / 100.0f));
    blit(gp, gp->x, gp->y, y0, y1, 0, w - 3);
    blit(gp, gp->x + w - 3, gp->y, y0, y1, gp->w - 3, gp->w);
  }
}

// Did the header change since it was drawn? (coins, level, the XP fill, a gauge's width or colour)
bool headerChanged() {
  game::State& st = game::s();
  const pbu_piece_t* f = pk::piece("lv_fill");
  const int fill = (int)lroundf(f->w * game::xpFrac());
  bool ch = st.coins != g->hCoins || st.lv != g->hLv || fill != g->hFill;
  g->hCoins = st.coins, g->hLv = st.lv, g->hFill = fill;
  for (int k = 0; k < 4; k++) {
    const int w = (int)lroundf(fmaxf(6.0f, 38 * st.need[k] / 100.0f)) * 2 + (st.need[k] < game::G.low);
    ch |= w != g->hW[k];
    g->hW[k] = w;
  }
  return ch;
}

void stageRows(const pbs2_frame_t* f, int& a, int& b) {
  int y0, y1;
  dog::extent(f, y0, y1);
  a = CANVAS + y0, b = CANVAS + y1;
}

// The dog layer as it is now, kept for the blend into the next clip (the page's snapshot).
void snapshot() {
  if (!g->shown || !g->snap) return;
  const Pic* p = dog::get(g->shown, 0);
  int a, b;
  stageRows(g->shown, a, b);
  a = max(a, STAGE_VIS - SB), b = min(b, STAGE_B - SB);
  g->snap0 = a, g->snap1 = b;
  if (a >= b) return;
  const int top = STAGE_VIS - SB;  // snap row 0 = buffer row 80
  for (int y = a; y < b; y++) {
    uint16_t* r = g->snap + (y - top) * W;
    for (int x = 0; x < W; x++) r[x] = WHITE;
  }
  Canvas c = {g->snap, W, STAGE_B - STAGE_VIS, W, a - top, b - top};
  dog::shadow(c, g->shown, 0, CANVAS - top, 0);
  if (p) dog::draw(c, *p, 0, CANVAS - top);
}

void stage(int y0, int y1) {
  y0 = max(y0, STAGE_VIS - SB), y1 = min(y1, STAGE_B - SB);
  if (y0 >= y1 || !g->shown) return;
  Canvas c = {g->buf, W, BH, W, y0, y1};
  dog::shadow(c, g->shown, 0, CANVAS, 0);
  const Pic* p = dog::get(g->shown, 0);
  if (p) dog::draw(c, *p, 0, CANVAS);
  if (g->fade > 0) {  // the old clip's last picture over the new one: 2/3, then 1/3
    const int a = 255 * g->fade / (FADE + 1), top = STAGE_VIS - SB;
    for (int y = y0; y < y1; y++) {
      const uint16_t* s = g->snap + (y - top) * W;
      uint16_t* d = g->buf + y * W;
      const bool held = y >= g->snap0 && y < g->snap1;
      for (int x = 0; x < W; x++) d[x] = dog::blend(d[x], held ? s[x] : WHITE, a);
    }
  }
}

void coins(int y0, int y1, bool flying) {
  const pbu_piece_t* p = pk::piece("coin");
  for (const Coin& k : g->co) {
    if ((k.st == 2) != flying || (k.st == 0 && k.t < 0)) continue;
    if (k.st == 1 && !k.kept && k.life - k.t < 2 && (int)floorf(k.t * 5) % 2) continue;  // about to go: blinks
    const int lo = flying ? y0 : max(y0, STAGE_VIS - SB), hi = flying ? y1 : min(y1, STAGE_B - SB);
    blit(p, (int)lroundf(k.x) + p->x, bufY((int)lroundf(k.y) + p->y), lo, hi);
  }
  if (!flying) return;
  const pbu_piece_t* pg = pk::piece("plus_gift");
  for (float a : g->ft) {
    const int dy = (int)lroundf(8 * (1 - fminf(1, a * 2)));
    blit(pg, pg->x, pg->y + dy, y0, y1, 0, -1, (int)(clampf(1.4f - a * 1.2f, 0, 1) * 255));
  }
}

void bubble(int y0, int y1) {
  if (!(g->sayT > 0 && g->say)) return;
  const int x = (W - g->bub.w + 1) / 2, by = bufY(g->sayY);
  const int lo = max(y0, STAGE_VIS - SB), hi = min(y1, STAGE_B - SB);
  if (lo >= hi) return;
  const text::Target t = {g->buf, W, BH, W, lo, hi};
  text::bubble(t, pk::font(0), g->bub, x, by);
  if (g->sayLow) blit(pk::piece("tail_up"), 154, by - 7, lo, hi);
  else blit(pk::piece("tail_down"), 154, by + g->bub.h - 1, lo, hi);
}

void sheet(int y0, int y1) {
  if (!g->sheet) return;
  const game::State& st = game::s();
  char nm[24];
  snprintf(nm, sizeof nm, "sheet_%d", st.lv >= 6 ? 6 : st.lv >= 3 ? 3 : 1);
  blitLow(nm, y0, y1);
  snprintf(nm, sizeof nm, "likes_%d", game::taste());
  blitLow(nm, y0, y1);
  static const char* V[3] = {"meh", "ok", "love"};
  for (int k = 0; k < 4; k++) {
    if (st.lv < game::G.foodLevel[k]) continue;
    if (game::G.price[k]) {
      snprintf(nm, sizeof nm, st.coins >= game::G.price[k] ? "price_%d_ok" : "price_%d_no", k);
      blitLow(nm, y0, y1);
    }
    snprintf(nm, sizeof nm, "v%d_%s", k, V[game::verdict(k)]);
    blitLow(nm, y0, y1);
  }
}

void pressed(int y0, int y1) {  // a pressed button looks a little darker for 0.15 s
  if (g->press < 0 || g->pressT <= 0) return;
  const int cx = BX[g->press], cy = bufY(BTN_Y);
  for (int y = max(y0, cy - BTN_R); y < min(y1, cy + BTN_R + 1); y++)
    for (int x = cx - BTN_R; x <= cx + BTN_R; x++) {
      const int dx = x - cx, dy = y - cy;
      if (dx * dx + dy * dy <= BTN_R * BTN_R) g->buf[y * W + x] = dog::blend(g->buf[y * W + x], 0x0000, 34);
    }
}

void render(int y0, int y1) {
  if (y0 >= y1) return;
  base(y0, y1);
  if (y0 < 80) header(y0, min(y1, 80));
  stage(y0, y1);
  coins(y0, y1, false);
  bubble(y0, y1);
  sheet(y0, y1);
  pressed(y0, y1);
  coins(y0, y1, true);
}

// Buffer rows [a, b) changed: their place on the screen is drawn again (landscape: each part showing them).
void shown(int a, int b) {
  if (a >= b) return;
  if (!g->part[0]) {
    lv_area_t ar;
    lv_obj_get_coords(g->img, &ar);
    ar.y2 = ar.y1 + b - 1, ar.y1 += a;
    lv_obj_invalidate_area(g->img, &ar);
    return;
  }
  for (int i = 0; i < NPARTS; i++) {
    const Part& p = PARTS[i];
    const int r0 = max(a, p.sy - SB), r1 = min(b, p.sy - SB + p.h);
    if (r0 >= r1 || lv_obj_has_flag(g->part[i], LV_OBJ_FLAG_HIDDEN)) continue;
    lv_area_t ar;
    lv_obj_get_coords(g->part[i], &ar);
    ar.y2 = ar.y1 + (r1 - (p.sy - SB)) - 1, ar.y1 += r0 - (p.sy - SB);
    lv_obj_invalidate_area(g->part[i], &ar);
  }
}

// Landscape: which parts show (the splash: only the stage's; the food sheet only while open).
void showParts() {
  for (int i = 0; g->part[0] && i < NPARTS; i++) {
    const bool on = g->splash > 0 ? i == 0 : i != SHEET_PART || g->sheet;
    on ? lv_obj_remove_flag(g->part[i], LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(g->part[i], LV_OBJ_FLAG_HIDDEN);
  }
}

void splashScreen() {
  const pbu_piece_t* s = pk::piece("splash");
  const uint8_t* px = pk::pixels(s);
  for (int y = 0; y < BH; y++) memcpy(g->buf + y * W, px + (y + SB / 2) * W * 2, W * 2);  // centred
  // The page's line under the logo with this dog's name, translated (semibold 11, centred, middle at y 266 of the
  // piece). PawBoop 1.0's splash has Biscuit's line baked in: covered first.
  const int mid = 266 - SB / 2;
  if (!pk::piece("wake_corgi")) {
    const uint16_t bg = pk::rd16(px + (265 * W + 4) * 2);
    for (int y = mid - 10; y < mid + 11; y++)
      for (int x = 8; x < W - 8; x++) g->buf[y * W + x] = bg;
  }
  const String t = i18n::fmt(N_("{0} is waking up"), dogs::name());  // i18n: the splash, the dog's name
  const text::Target tg = {g->buf, W, BH, W, 0, BH};
  text::draw(tg, pk::font(2), t.c_str(), (W - text::width(pk::font(2), t.c_str())) / 2, mid + 0.5f, 0x8C94);
  showParts();
  shown(0, BH);
}

// The rows that move this picture: the dog, the coins, the bubble, the floats, a pressed button.
void movingRows(int& a, int& b) {
  a = BH, b = 0;
  auto add = [&](int y0, int y1) { a = min(a, y0), b = max(b, y1); };
  if (g->shown) {
    int y0, y1;
    stageRows(g->shown, y0, y1);
    add(y0, y1);
  }
  if (g->fade > 0) add(g->snap0, g->snap1);
  for (const Coin& k : g->co) add(bufY((int)k.y - 10), bufY((int)k.y + 10));
  if (g->sayT > 0 && g->say) add(bufY(g->sayY) - 8, bufY(g->sayY) + g->bub.h + 8);
  if (!g->ft.empty()) add(30, 50);
  if (g->press >= 0) add(bufY(BTN_Y - BTN_R), bufY(BTN_Y + BTN_R + 1));
  a = max(a, 0), b = min(b, BH);
}

// ------------------------------------------------------------------ the dog's script (the page's brain)
bool alone() { return g->info && brain::ownRole(g->info->role); }

void play(const char* name) {
  const pbs2_clip_t* c = pk::clip(name);
  if (!c) return;
  snapshot();
  g->clip = c, g->info = brain::info(name), g->i = 0, g->loops = 0, g->fade = g->shown ? FADE : 0;
  dog::want(pk::frame(c, 0), 0);
  sfx::clip(name);  // its sounds follow its pictures
}

bool say(const pba_clip_t* l, bool force) {
  if (!voice::speak(l, force)) return false;
  g->line = l, g->say = true, g->bub = text::layout(pk::font(0), pk::text(l));
  g->sayT = pk::seconds(l) + 0.45f, g->sayPlaced = false;
  return true;
}
void hush() {
  voice::hush();
  g->sayT = 0;
}

void chatter(const char* clip, const char* role) {  // her own talk: never over a line, never a stream
  const float quiet = voice::quietMs() / 1000.0f;
  if (!strcmp(role, "idle") ? (quiet > 6 && frand() < 0.5f) : quiet > 3.5f) say(voice::lineFor(clip), false);
}

void idleNext() {
  const char* r = game::moodRole();
  const char* n = brain::byRole(r);
  if (!n) n = brain::byRole("idle");
  play(n);
  chatter(n, r);
}

// The answer of the two beats, once the first line is said: giggle (or sulk) with caught_2.
void beatTwo() {
  const pba_clip_t* l = g->beat2;
  play(g->beat2Clip);
  g->beat2 = nullptr;
  say(l, true);
}

bool clipEnded() {
  if (g->beat2) {  // the innocent face lasts as long as the first line
    if (voice::busy()) play(g->beat1Clip);
    else beatTwo();
    return true;
  }
  if (g->info && *g->info->next && pk::clip(g->info->next)) {
    const char* n = g->info->next;
    play(n);
    say(voice::lineFor(n), false);
    return true;
  }
  idleNext();
  return true;
}

void loopEnded() {
  g->loops++;
  if (g->beat2) {
    if (!voice::busy()) beatTwo();
    return;
  }
  if (strcmp(g->info ? g->info->role : "", game::moodRole()) || g->loops >= 2) idleNext();
}

void dropCoins(int n, bool keep) {
  const pbs2_frame_t* f = g->shown;
  const float ox = f && f->sr ? f->sx : 160, oy = STAGE_Y + (f && f->sr ? f->sy : 228);
  for (int k = 0; k < n; k++) {
    int live = 0;
    for (const Coin& c : g->co) live += c.st != 2;
    if (live >= 8) break;
    const float x1 = clampf(ox + (frand() * 2 - 1) * 112, 20, 300);
    const float y1 = clampf(oy + 14 + frand() * 24, STAGE_Y + 40, STAGE_B - 12);
    g->co.push_back({ox, oy - 44, x1, y1, ox, oy - 44, -k * 0.2f, keep ? 1e9f : (float)game::G.coinLife, 0, keep});
  }
}

void collect(Coin& c) {
  c.st = 2, c.t = 0, c.x0 = c.x, c.y0 = c.y;
  sfx::event("coin");  // it flies to her purse
}

void stepCoins(float dt) {
  for (int k = (int)g->co.size() - 1; k >= 0; k--) {
    Coin& c = g->co[k];
    c.t += dt;
    if (c.st == 0) {
      if (c.t < 0) continue;
      const float u = fminf(1, c.t / 0.5f);
      c.x = c.x0 + (c.x1 - c.x0) * u;
      c.y = c.y0 + (c.y1 - c.y0) * u - sinf(PI * u) * 46;
      if (u >= 1) c.st = 1, c.t = 0;
    } else if (c.st == 1) {
      if (c.t > c.life) g->co.erase(g->co.begin() + k);
    } else {
      const float u = fminf(1, c.t / 0.4f);
      c.x = c.x0 + (COUNTER_X - c.x0) * u;
      c.y = c.y0 + (COUNTER_Y - c.y0) * u;
      if (u >= 1) {
        game::addCoins(game::G.coin);
        if (c.kept && game::s().found > 0) game::s().found--;
        game::saveSoon();
        g->co.erase(g->co.begin() + k);
      }
    }
  }
  for (int k = (int)g->ft.size() - 1; k >= 0; k--)
    if ((g->ft[k] += dt) > 1.2f) g->ft.erase(g->ft.begin() + k);
}

int keptOnStage() {
  int n = 0;
  for (const Coin& c : g->co) n += c.kept && c.st != 2;
  return n;
}

// ------------------------------------------------------------------ what the player does
void moment(int kind, int arg) {
  if (!game::hasMoment(kind, arg)) game::addMoment(kind, arg, g->clip ? g->clip->name : "happy", g->i);
}

void act(int b) {
  if (g->splash > 0) return;
  const char* a = BTN[b];
  sfx::event("tap");  // every round button (the phone game's ui_tap)
  if (!strcmp(a, "feed")) {
    g->sheet = !g->sheet, g->sayPlaced = false;
    if (g->sheet) sfx::event("sheet");  // the pantry opens
    return;
  }
  if (g->sheet) g->sheet = false, g->sayPlaced = false;
  game::State& st = game::s();
  if (!strcmp(a, "play")) {
    if (st.need[game::ENERGY] < game::G.playNo) {
      say(voice::lineFor("@no_tired"), true);
      sfx::event("no_tired");
      const char* n = brain::byRole("sleepy");
      play(n ? n : brain::byRole("idle"));
      return;
    }
    if (st.need[game::FOOD] < game::G.playNo) {
      say(voice::lineFor("@no_hungry"), true);
      sfx::event("no_hungry");
      const char* n = brain::byRole("hungry");
      play(n ? n : brain::byRole("idle"));
      return;
    }
    g->dropN = game::playDrop(), g->dropT = 1.4f;  // the coins fall a moment into the game
  }
  const char* n = brain::byRole(a);
  if (!n) return;
  const game::Effect& e = !strcmp(a, "play") ? game::PLAY : !strcmp(a, "pet") ? game::PET : !strcmp(a, "wash") ? game::WASH : game::NAP;
  game::apply(e, !strcmp(a, "pet"));
  play(n);
  if (!say(voice::lineFor(n), true)) hush();
  if (!strcmp(a, "wash")) moment(3, 0);
  if (!strcmp(a, "nap")) moment(7, 0);
}

void feedTier(int t) {
  game::State& st = game::s();
  if (st.lv < game::G.foodLevel[t]) {
    say(voice::lineFor("@locked"), true);
    return;
  }
  if (st.coins < game::G.price[t]) {
    g->sheet = false;
    say(voice::lineFor("@no_coins"), true);
    sfx::event("no_coins");
    return;
  }
  st.coins -= game::G.price[t];
  g->sheet = false;
  const int v = game::verdict(t);
  game::feedEffect(v);
  sfx::event("feed");  // the bowl lands, the food pours
  if (v == 2) sfx::event("love");
  if (v == 0) {  // under her taste: she turns it down (and still nibbles)
    play("yuck");
    say(voice::lineFor("yuck"), true);
  } else {
    play("eat");
    say(voice::lineFor(v == 2 ? "@love" : "eat"), true);
    if (v == 2) moment(2, 0);
  }
}

void tapDog() {
  if (!alone() || g->beat2) return;
  for (Coin& c : g->co)
    if (c.st == 1) collect(c);  // the dog hands over what lies around it
  // Caught busy on its own (pounce, nudge, look_up): the two beats, the innocent face (alert or plead) with
  // caught_1, then giggle (or sulk) with the caught_2 that answers it (the same index).
  static const char* const BUSY[3] = {"pounce", "nudge", "look_up"};
  for (const char* b : BUSY) {
    if (!g->clip || strncmp(g->clip->name, b, 16)) continue;
    const pba_clip_t* l1 = voice::lineFor("@caught_1");
    const pba_clip_t* l2 = voice::answerTo(l1, "@caught_2");
    const char* face = random(2) ? "alert" : "plead";
    const char* answer = random(10) < 7 ? "giggle" : "sulk";
    if (!l1 || !l2 || !pk::clip(face) || !pk::clip(answer)) break;
    game::apply(game::TAP, true);
    play(face);
    say(l1, true);
    g->beat1Clip = face, g->beat2Clip = answer, g->beat2 = l2;
    return;
  }
  const char* r = game::moodRole();
  if (strcmp(r, "idle")) {  // she tells you what she needs
    const char* n = brain::byRole(r);
    play(n);
    say(voice::lineFor(n), true);
    return;
  }
  const char* pick[4];
  int k = 0;
  for (const char* n : game::TAP_CLIPS)
    if (pk::clip(n) && strncmp(n, g->clip->name, 16)) pick[k++] = n;
  if (!k) return;
  const char* n = pick[random(k)];
  game::apply(game::TAP, true);
  play(n);
  say(voice::lineFor(n), true);
  if (!strcmp(n, "high_five")) moment(6, 0);
}

void onTouch(int x, int y) {  // screen px
  g->tapAt = millis();
  if (g->splash > 0) return;
  if (y >= SB && y < SB + 40 && x < 100) {  // the paw badge: the dog's book; its name: choose a dog
    if (x < 38) book::open();
    else chooser::open();
    return;
  }
  for (Coin& c : g->co)
    if (c.st == 1 && hypotf(x - c.x, y - c.y) < 20 && !(g->sheet && c.y > SHEET_Y)) return collect(c);
  if (g->sheet) {
    if (y >= SHEET_Y + 28 && y < SHEET_Y + 152) feedTier(constrain((x - 8) / 77, 0, 3));
    else g->sheet = false, g->sayPlaced = false;
    return;
  }
  if (y >= STAGE_B) {
    int k = 0;
    for (int i = 1; i < 5; i++)
      if (abs(x - BX[i]) < abs(x - BX[k])) k = i;
    g->press = k, g->pressT = 0.15f;
    act(k);
  } else if (y > STAGE_Y + 30) {
    tapDog();
  }
}

// ------------------------------------------------------------------ the clock: 10 pictures a second
void placeBubble() {  // over the dog when the next pictures leave room, else where it covers less
  if (g->sayPlaced || !g->say || !g->clip) return;
  g->sayPlaced = true;
  const int h = g->bub.h, floorY = g->sheet ? SHEET_Y : STAGE_B;
  int top, bot;
  dog::span(g->clip, g->i, (int)ceilf(g->sayT * FPS) + 2, top, bot);
  const int over = (STAGE_VIS - STAGE_Y) + 3 + h + 4 - top, under = bot - (floorY - STAGE_Y - h - 5 - 4);
  g->sayLow = !(over <= 0 || over <= under);
  g->sayY = g->sayLow ? floorY - h - 5 : STAGE_VIS + 3;
}

// One picture: coins move, the picture is drawn, then the clip goes on (the page's advance()).
void picture(bool draw) {
  stepCoins(1.0f / FPS);
  if (g->pressT > 0) g->pressT -= 1.0f / FPS;
  const pbs2_frame_t* f = pk::frame(g->clip, g->i);
  sfx::picture(g->i);
  if (draw) {
    const uint32_t t0 = nowUs();
    int a, b;
    movingRows(a, b);  // where things were
    g->shown = f;
    placeBubble();
    int c, d;
    movingRows(c, d);  // where they are now
    g->d0 = BH, g->d1 = 0;
    if (g->full) mark(0, BH), g->full = false;
    mark(g->prev0, g->prev1);
    mark(a, b);
    mark(c, d);
    if (headerChanged()) mark(0, 80);
    if (g->sheet != g->sheetDrawn) mark(bufY(SHEET_Y - 4), BH), g->sheetDrawn = g->sheet;
    render(g->d0, g->d1);
    g->prev0 = c, g->prev1 = d;
    if (g->d0 < g->d1) {
      showParts();  // landscape: the food sheet's part comes and goes with it
      shown(g->d0, g->d1);
      g->rows += g->d1 - g->d0;
    }
    const uint32_t us = nowUs() - t0;
    g->pics++, g->composeUs += us, g->composeMax = max(g->composeMax, us);
    if (g->tapAt) {
      trace::add("pet: tap to picture %lu ms", (unsigned long)(millis() - g->tapAt));
      g->tapAt = 0;
    }
  } else {
    g->shown = f;
  }
  if (g->fade > 0) g->fade--;
  if (g->press >= 0 && g->pressT <= 0) g->press = -1;
  int ni = g->i + 1;
  if (ni >= g->clip->count) {
    if (!g->clip->loop) return (void)clipEnded();  // the next clip starts at its first picture
    ni = 0;
    loopEnded();  // may start another clip (its first picture is 0 too)
  }
  g->i = ni;
  dog::want(pk::frame(g->clip, g->i), 0);
}

bool onScreen() { return g && lv_screen_active() == g->scr && !os::saverOn() && !pet::summoned(); }

// ------------------------------------------------------------------ the run game: coins for her food
constexpr const char* RUN_ID = "vn.pawboop.run";

void openRun(lv_event_t*) {
  if (!g || g->splash > 0) return;
  if (!pkg::find(RUN_ID)) return apps::open("store", RUN_ID);  // the add-on, in the store
  // Coin Run 2.0 has its roads in the maps (each its own add-on): none yet, its shelf in the store.
  bool roads = false;
  for (const pkg::Package& p : pkg::list()) roads |= p.id.startsWith("vn.pawboop.map.") && p.available;
  for (const String& part : pkg::find(RUN_ID)->parts) roads |= part.startsWith("roads");  // run 1.0's own roads
  if (!roads) {
    os::toast(N_("Add a map for Coin Run first"));
    return apps::open("store", "vn.pawboop.map.bazaar");
  }
  hush();
  g->sheet = false, g->sayPlaced = false;
  // Her pictures go first (decoded again when she is back): the run sizes its sprite cache from the PSRAM left,
  // and from a memory card it also holds its packs in PSRAM.
  const uint32_t before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  if (!pet::summoned()) dog::release();
  pk::trim();  // the screen's big pieces when they came from the card (read again when she is back)
  trace::add("pet: to the run, PSRAM free %u KB (+%d KB from her pictures)",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) >> 10),
             (int)((int32_t)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) - before) / 1024));
  pkg::run(RUN_ID, "pet");  // over this screen: its end comes back here ("Cần thẻ nhớ" when its card is out)
}

// "+N xu" rising from her purse and fading.
void showArrived(int n) {
  char s[16];
  snprintf(s, sizeof s, "+%d xu", n);
  lv_obj_t* l = label(g->body, s, 15, true, 0xe07b12);
  lv_obj_update_layout(l);
  lv_obj_set_pos(l, 274 - lv_obj_get_width(l) / 2, 36);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, l);
  lv_anim_set_values(&a, 36, 10);
  lv_anim_set_duration(&a, 1600);
  lv_anim_set_exec_cb(&a, [](void* o, int32_t y) {
    lv_obj_set_y((lv_obj_t*)o, y);
    lv_obj_set_style_opa((lv_obj_t*)o, y > 22 ? LV_OPA_COVER : (y - 10) * 255 / 12, 0);
  });
  lv_anim_set_completed_cb(&a, [](lv_anim_t* an) { lv_obj_delete((lv_obj_t*)an->var); });
  lv_anim_start(&a);
}

void onTimer(lv_timer_t*) {
  const uint32_t now = millis();
  const float dt = fminf(0.25f, (now - g->last) / 1000.0f);
  g->last = now;
  game::advance((uint32_t)(dt * 1000), alone());
  if (onScreen() && g->splash <= 0) {  // in front (again: the run had the sounds): her sounds and the home tune
    sfx::open(g->dogPkg.c_str(), false);
    sfx::home();
  }
  sfx::frame(false, onScreen() && g->splash <= 0);
  if (!onScreen()) {
    g->acc = 0;
    return;
  }
  if (g->splash > 0) {
    if ((g->splash -= dt) <= 0) {
      g->full = true;
      idleNext();
      say(voice::lineFor("@hello"), true);
      g->acc = 1.0f / FPS;
      if (g->runPill) lv_obj_remove_flag(g->runPill, LV_OBJ_FLAG_HIDDEN);
      if (g->trialChip) lv_obj_remove_flag(g->trialChip, LV_OBJ_FLAG_HIDDEN);
      showParts();
    }
    return;
  }
  if (g->trialChip && g->sheet != lv_obj_has_flag(g->trialChip, LV_OBJ_FLAG_HIDDEN))  // not over the food sheet
    g->sheet ? lv_obj_add_flag(g->trialChip, LV_OBJ_FLAG_HIDDEN) : lv_obj_remove_flag(g->trialChip, LV_OBJ_FLAG_HIDDEN);
  if (g->arrived > 0) {  // back from the run: the coins are in her purse already (the header shows them)
    showArrived(g->arrived);
    g->arrived = 0;
    sfx::event("arrived");
    say(voice::lineFor("@coin"), true);  // the dog's own @coin line ("Vàng nè khanh!...")
  }
  game::State& st = game::s();
  if (g->dropT > 0 && (g->dropT -= dt) <= 0) dropCoins(g->dropN, false);
  // A happy dog brings a coin (counted by game::advance): it appears by her.
  if (keptOnStage() < st.found) {
    dropCoins(1, true);
    if (voice::quietMs() > 3500) say(voice::lineFor("@coin"), false);
  }
  if (st.levelUp) st.levelUp = false, g->ft.push_back(0), g->next = "@levelup", moment(1, st.lv), sfx::event("levelup");
  if (st.gift) st.gift = false, g->ft.push_back(0), g->next = "@gift", sfx::event("gift");
  if (g->sayT > 0) g->sayT -= dt;
  if (g->next && !voice::busy()) {
    say(voice::lineFor(g->next), false);
    g->next = nullptr;
  }
  g->acc += dt;
  const float fd = 1.0f / FPS;
  int k = 0;
  while (g->acc >= fd && k < 3) {
    g->acc -= fd;
    k++;
    picture(g->acc < fd || k == 3);  // late: step the clock, draw only the last
  }
  if (k > 1) g->lateSteps += k - 1;
  if (g->acc > fd) g->acc = 0;
  if (now - g->statAt > 10000 && g->pics) {
    dog::Stats& ds = dog::stats();
    trace::add("pet: %lu pics/10s, compose avg %lu max %lu us, %lu rows/pic, late %lu | decode %lu (ahead %lu, inline %lu, held %lu) avg %lu max %lu us",
               (unsigned long)g->pics, (unsigned long)(g->composeUs / g->pics), (unsigned long)g->composeMax,
               (unsigned long)(g->rows / g->pics), (unsigned long)g->lateSteps, (unsigned long)ds.pictures,
               (unsigned long)ds.ahead, (unsigned long)ds.late, (unsigned long)ds.held,
               (unsigned long)(ds.ahead + ds.late ? ds.decUs / (ds.ahead + ds.late) : 0), (unsigned long)ds.decMaxUs);
    voice::Stats& vs = voice::stats();
    trace::add("pet: voice %lu lines, %lu packets, decode avg %lu max %lu us, underruns %lu, first sound %lu us",
               (unsigned long)vs.lines, (unsigned long)vs.packets, (unsigned long)(vs.packets ? vs.decUs / vs.packets : 0),
               (unsigned long)vs.decMaxUs, (unsigned long)vs.underruns, (unsigned long)vs.firstSoundUs);
    g->statAt = now, g->pics = g->composeUs = g->composeMax = g->rows = g->lateSteps = 0;
    ds = dog::Stats();
  }
}

void onEvent(lv_event_t* e) {
  const lv_event_code_t code = lv_event_get_code(e);
  lv_point_t p;
  lv_indev_get_point(lv_indev_active(), &p);
  p.x -= os::colX(), p.y -= os::colY();  // a bigger screen than the 3.5" board's: the page sits in its middle
  const intptr_t k = (intptr_t)lv_event_get_user_data(e);  // landscape: the part touched, 1-based
  if (k > 0) {  // back to the portrait page's screen px
    const Part& pt = PARTS[k - 1];
    p.x += pt.sx - pt.dx, p.y += pt.sy - pt.dy;
  }
  if (code == LV_EVENT_PRESSED) {
    g->downX = p.x, g->downY = p.y;
  } else if (code == LV_EVENT_SHORT_CLICKED) {
    if (abs(p.x - g->downX) > 12 || abs(p.y - g->downY) > 12) return;  // taps only
    onTouch(g->downX, g->downY);
    g->acc = max(g->acc, 1.0f / FPS - 0.02f);  // the answer shows at the next timer run
  }
}

void onDelete(lv_event_t*) {
  if (!g) return;
  if (g->timer) lv_timer_delete(g->timer);
  hush();
  sfx::close(false);
  game::save();
  lv_image_cache_drop(&g->dsc);
  if (g->part[0])
    for (lv_image_dsc_t& d : g->partDsc) lv_image_cache_drop(&d);
  heap_caps_free(g->buf);
  heap_caps_free(g->snap);
  delete g;
  g = nullptr;
  closedAt = millis();
  if (!pet::summoned()) dog::release();
  pk::trim();
  trace::add("pet: game closed");
}

// A plain screen with a message (no packs, landscape).
void message(const char* title, const char* text, const char* small) {
  lv_obj_t* body;
  lv_obj_t* s = os::screen("PawBoop", &body, os::NOSCROLL);
  lv_obj_set_flex_align(body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
#if ESPOS_ONBOARD
  lv_obj_t* ic = apps::packageIcon(body, "vn.pawboop.pet", 52);
#else
  lv_obj_t* ic = img(body, &ic_pet_52);
#endif
  lv_obj_set_style_pad_top(ic, 40, 0);
  lv_obj_t* t = label(body, "", 22, true, pal().fg);
  lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
  wrapText(t, title, os::W() - 48, 22, true);
  lv_obj_t* m = label(body, "", 16, false, pal().fg);
  lv_obj_set_style_text_align(m, LV_TEXT_ALIGN_CENTER, 0);
  wrapText(m, text, os::W() - 48, 16);
  if (small) {
    lv_obj_t* sm = label(body, "", 13, false, pal().dim);
    lv_obj_set_style_text_align(sm, LV_TEXT_ALIGN_CENTER, 0);
    wrapText(sm, small, os::W() - 48, 13);
  }
  os::open(s);
}

}  // namespace

bool isOpen() { return g != nullptr; }
void devSay(const char* line) {
  if (g) say(pk::line(line), true);
}
void devClip(const char* clip) {
  if (g && pk::clip(clip)) play(clip);
}
void stop() {
  if (g && g->timer) lv_timer_delete(g->timer), g->timer = nullptr;
  if (g) hush();
}
void arrived(int coins) {
  if (g && coins > 0) g->arrived += coins;
}
bool active() { return onScreen(); }

void open() {
  if (g) return;
  if (!pk::ready()) {
    trace::add("pet: packs missing (%s)", pk::missing());
    if (!strcmp(pk::missing(), "dog")) return chooser::open();  // no dog yet: pick one (the store brings it)
    message(N_("PawBoop's data is missing"), N_("Install PawBoop again from the Store."),
            !strcmp(pk::missing(), "pet") ? nullptr : N_("If the dog is on the memory card, put the card back in."));
    return;
  }
  home::releasePictures();  // PSRAM for her pictures and the run game
  uint16_t* buf = (uint16_t*)heap_caps_malloc(W * BH * 2, MALLOC_CAP_SPIRAM);
  uint16_t* snap = (uint16_t*)heap_caps_malloc(W * (STAGE_B - STAGE_VIS) * 2, MALLOC_CAP_SPIRAM);
  if (!buf || !snap) {
    heap_caps_free(buf), heap_caps_free(snap);
    os::toast(N_("Not enough memory to open PawBoop"));
    return;
  }
  g = new Game();
  g->buf = buf, g->snap = snap;
  g->dogPkg = dogs::pkgId(pk::dogKey());
  sfx::open(g->dogPkg.c_str(), false);
  lv_obj_t* body;
  g->scr = os::screen("PawBoop", &body, os::BARE);
  os::column(body);  // laid out for 320 x 480 (480 x 320): in the middle of a bigger screen
  lv_obj_set_style_bg_color(g->scr, lv_color_white(), 0);
  g->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  g->dsc.header.cf = LV_COLOR_FORMAT_RGB565;
  g->dsc.header.w = W;
  g->dsc.header.h = BH;
  g->dsc.header.stride = W * 2;
  g->dsc.data_size = W * BH * 2;
  g->dsc.data = (const uint8_t*)buf;
  g->img = lv_image_create(body);
  lv_image_set_src(g->img, &g->dsc);
  lv_obj_set_pos(g->img, 0, 0);
  lv_obj_add_flag(g->img, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(g->img, onEvent, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(g->img, onEvent, LV_EVENT_SHORT_CLICKED, nullptr);
  const bool land = os::landscape();
  if (land) {  // the parts (PARTS), each a window into the same picture
    lv_obj_add_flag(g->img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(g->scr, lv_color_hex(0xf6f4f0), 0);
    for (int i = 0; i < NPARTS; i++) {
      const Part& pt = PARTS[i];
      lv_image_dsc_t& d = g->partDsc[i];
      d = g->dsc;
      d.header.w = pt.w, d.header.h = pt.h;
      d.data = (const uint8_t*)(buf + (pt.sy - SB) * W + pt.sx);
      d.data_size = W * 2 * pt.h;  // LVGL checks stride x height (every part ends inside the picture)
      g->part[i] = lv_image_create(body);
      lv_image_set_src(g->part[i], &d);
      lv_obj_set_pos(g->part[i], pt.dx, pt.dy - SB);
      lv_obj_add_flag(g->part[i], LV_OBJ_FLAG_CLICKABLE);
      lv_obj_add_event_cb(g->part[i], onEvent, LV_EVENT_PRESSED, (void*)(intptr_t)(i + 1));
      lv_obj_add_event_cb(g->part[i], onEvent, LV_EVENT_SHORT_CLICKED, (void*)(intptr_t)(i + 1));
    }
  }
  // "Coin Run" (Chạy lấy xu) by her purse: the run game earns the coins for her food (docs/run.md). Between the level chip
  // and the coin chip of the header (x 148..235), in the run's orange.
  g->body = body;
  g->runPill = box(body, 0xff9a1f, 11);
  grad(g->runPill, 0xffb347, 0xff7a00);
  lv_obj_set_size(g->runPill, 80, 22);
  // Landscape: where the second header part shows x 152, y 33.
  lv_obj_set_pos(g->runPill, land ? PARTS[2].dx + 152 - PARTS[2].sx : 152, (land ? PARTS[2].dy + 33 - PARTS[2].sy : 33) - SB);
  lv_obj_add_flag(g->runPill, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(g->runPill, 6);
  lv_obj_set_style_opa(g->runPill, LV_OPA_70, LV_STATE_PRESSED);
  lv_obj_t* runLabel = label(g->runPill, "", 12, true, 0xffffff);
  fitText(runLabel, N_("Coin Run"), 74, 20, 12, 9, true, 0);  // i18n: the run game's name on a small orange pill
  lv_obj_center(runLabel);
  lv_obj_add_event_cb(g->runPill, openRun, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(g->runPill, [](lv_event_t*) { sfx::event("tap"); }, LV_EVENT_CLICKED, nullptr);
  // The trial's days (lock.h): the stage's lower left corner, clear of her and of the buttons.
  if ((g->trialChip = lock::chip(body, true))) {
    lv_obj_update_layout(g->trialChip);
    lv_obj_set_pos(g->trialChip, land ? 8 : 12, (land ? PARTS[0].dy + (STAGE_B - STAGE_VIS) - 30 : STAGE_B - 30) - SB);
  }
  lv_obj_add_event_cb(g->scr, onDelete, LV_EVENT_DELETE, nullptr);
  lv_obj_add_event_cb(g->scr, [](lv_event_t*) { os::barDark(true); }, LV_EVENT_SCREEN_LOADED, nullptr);
  voice::setMuted(game::s().muted);
  // The first picture: the splash once after power-on, else the game at once.
  if (bootSplash) {
    bootSplash = false;
    g->splash = 1.8f;
    splashScreen();
    lv_obj_add_flag(g->runPill, LV_OBJ_FLAG_HIDDEN);  // shown with the game
    if (g->trialChip) lv_obj_add_flag(g->trialChip, LV_OBJ_FLAG_HIDDEN);
  } else {
    render(0, BH);
    idleNext();
    if (!closedAt || millis() - closedAt > 5 * 60 * 1000) say(voice::lineFor("@hello"), true);
    g->acc = 1.0f / FPS;
  }
  game::giftDue();
  g->last = millis();
  g->statAt = millis();
  g->timer = lv_timer_create(onTimer, 20, nullptr);
  os::open(g->scr);
  os::barDark(true);
  trace::add("pet: game open, lv %d, coins %d", game::s().lv, game::s().coins);
}

}  // namespace app
}  // namespace pet
