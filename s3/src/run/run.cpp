// PawBoop Chạy: the game screen (run.h). One 320x480 RGB565 buffer in PSRAM shown by an lv_image, redrawn
// whole at 20 pictures a second: the road (232x240, scaled x2, the 464 px wide world panned by her lane),
// the things far to near with blob shadows, Biscuit (blinking while safe, the two-picture fade on clip
// changes), the things she has passed, the HUD and the cards. The page's present() / hud() / card().
//
// Layout: the OS status bar keeps the top 24 rows (over the road's sky), so the HUD sits 24 px lower than on
// the phone (as the pet game's header). Everything else keeps the page's screen coordinates.
// Small rather than fast: the pixel loops are in draw.cpp; the flash is nearly full.
#pragma GCC optimize("Os")
#include "run.h"

// The speed test, the developer switches and the autopilot (about 5 KB of flash): -DRUN_BENCH=0 leaves them out.
#ifndef RUN_BENCH
#define RUN_BENCH 1
#endif

#include <Arduino.h>
#include <Preferences.h>
#include <lvgl.h>

#include <algorithm>
#include <cmath>

#include "desk.h"
#include "home.h"
#include "draw.h"
#include "logic.h"
#include "media.h"
#include "os.h"
#ifndef SIM
#include "lcd.h"
#endif
#include "trace.h"
#include "uikit.h"
#include "art.h"
#include "i18n.h"
#ifndef NO_PET
#include "pet/app.h"
#include "pet/dogs.h"
#include "pet/game.h"
#include "pet/pet.h"
#include "pet/voice.h"
#endif

namespace run {
namespace {

using namespace kit;

constexpr int W = 320, H = 480;
// Landscape: the road pictures are portrait (pre-rendered 320 x 480), so the game shows their middle at full size,
// rows CROP_Y .. CROP_Y + 320 (the horizon at ~190 and her feet at 385 with room for her jump), centred, full screen,
// the HUD at the top of that window; only those rows are built. Portrait: all of it, the HUD under the status bar.
constexpr int CROP_Y = 112;
int HUD_DY = os::SB_H;
int offX = 0, offY = 0;  // where the picture's (0, 0) is on the screen (landscape: 80, -CROP_Y)
constexpr int LOOK = 5;           // road pictures planned ahead (the one shown included)
constexpr int SWIPE = 22;         // px of finger travel = a move (the page)

struct Acc {  // timing sums (us) for one stats window
  uint32_t n = 0, sum = 0, max = 0;
  void add(uint32_t us) { n++, sum += us, max = std::max(max, us); }
  uint32_t avg() const { return n ? sum / n : 0; }
};

struct Win {  // one stats window (10 s trace, or a bench phase)
  uint32_t at = 0;    // millis() when it started
  uint32_t pics = 0, steps = 0, late = 0, roadNew = 0, roadLate = 0;
  uint32_t dogs = 0, dogOld = 0, things = 0, thingOld = 0, thingNone = 0;  // pictures not decoded in time
  uint32_t thingNear = 0, thingBack = 0;  // stand-ins under 6 m; the furthest a stand-in was recorded behind (cm)
  uint32_t thingUp = 0, thingUpMax = 100;  // stand-ins drawn 20 %+ taller than their picture; the most (%)
  Acc compose, road, sprites, hud, push, swipe, spr, shade, pieces;
};

struct Screen {
  String id, arg;
  lv_obj_t *scr = nullptr, *img = nullptr, *spin = nullptr;
  lv_image_dsc_t dsc;
  uint16_t* buf = nullptr;      // the whole screen for LVGL (overlays, leaving) and the status bar's rows
  draw::Smooth sm;              // the smooth scale's rows across (3 x 320 words, internal RAM when there is room)
  std::vector<struct Op> ops;   // this picture, as drawing steps (drawn into DMA bands or the buffer)
  bool viaPanel = false;        // this picture goes straight to the panel (lcd::direct)
  bool wasOn = false;           // the game was in front at the last timer run
  const pbs2_frame_t* lastDog = nullptr;  // her picture drawn last (stands in while the next is decoded)
  lv_timer_t* timer = nullptr;
  media::Pack dog, th;
  media::Ui ui;
  World world;
  Game game;
  volatile bool loaded = false, failed = false;
  String err;
  bool ready = false, shown = false;  // loaded; the first picture shown
  uint32_t last = 0, readyAt = 0;
  double acc = 0;
  int start = -1;
  // input
  bool down = false, done = false;
  int px0 = 0, py0 = 0;
  // opened from PawBoop's pet screen ("Chạy lấy xu"): a way back under the cards, a word on where coins go
  bool fromPet = false;
  String dogPkg;  // the chosen dog's package (its run.pbs); "": run 1.0's core/run_dog.pbs
  media::Sprite homeBtn, coinNote;  // drawn once by LVGL (the game draws past LVGL)
  media::Sprite prevBtn, nextBtn;   // the start card's "‹ ›": the map the run starts in (more than one installed)
  int arrowY = -1;                  // where they are this picture (-1: not shown)
  // the dog's run lines (its voice pack): start card, a heart lost, the dizzy end, a new best
  int heardHearts = -1;
  State heardState = OVER;  // (so the first start card is heard)
  bool newBest = false, bestSaid = false;
  int homeX = 0, homeY = -1;        // where "Về nhà" is this picture (-1: not shown)
  // switches
  bool smooth = true, autopilot = false, god = false;
  double autoWait = 0;
  // stats
  Win win, bw;
  uint32_t renderRoadUs = 0, renderUs = 0, renderSprUs = 0, renderBlobUs = 0, renderPieceUs = 0;  // this picture
  int lastRoadKey = -1;
  bool pushPending = false, pushing = false;
  uint32_t refrAt = 0, swipeAt = 0;
  bool swipeShown = false;
  // bench
  int benchPhase = 0;  // 0 off, else PHASES[benchPhase - 1]
  uint32_t benchAt = 0;
};
Screen* g = nullptr;

// ------------------------------------------------------------------ the pet game: coins and health
bool petHealthy() {
#ifndef NO_PET
  const pet::game::State& s = pet::game::s();
  return (s.need[0] + s.need[1] + s.need[2] + s.need[3]) / 4 >= 70;
#else
  return false;
#endif
}

void petPaid(int coins) {
#ifndef NO_PET
  pet::game::addCoins(coins);
  pet::game::save();
  pet::app::arrived(coins);  // she shows them arriving when her screen is in front again
#endif
  trace::add("run: +%d coins to PawBoop", coins);
}

int loadBest() {
  Preferences p;
  p.begin("run", true);
  const int b = p.getInt("best", 0);
  p.end();
  return b;
}

void saveBest(double m) {
  Preferences p;
  p.begin("run", false);
  p.putInt("best", (int)(m * 100));
  p.end();
}

// ------------------------------------------------------------------ one picture as drawing steps
// The page's present() becomes a list of steps; the list is drawn band by band straight into the panel's DMA
// buffers (lcd::direct), or into the whole PSRAM buffer that LVGL shows (an overlay is up, or no direct path).
struct Op {
  enum Kind : uint8_t { ROAD, SAND, BLOB, SPRITE, PIECE, BAR } kind;
  int16_t x = 0, y = 0;
  int16_t opa = 255;
  int16_t w = 0, h = 0;  // SPRITE: drawn into this box (a stand-in at the right picture's size); 0: as it is
  const void* p = nullptr;
  float cx = 0, cy = 0, rx = 0, ry = 0, a = 0;
};
using draw::rgb565;
constexpr uint16_t SAND_565 = (0xe8 >> 3) << 11 | (0xc9 >> 2) << 5 | 0x8a >> 3;

void addPiece(const pbu_piece_t* p, int x, int y) {
  if (!p) return;
  Op o;
  o.kind = Op::PIECE, o.p = p, o.x = x, o.y = y;
  g->ops.push_back(o);
}
void addPieceAt(const char* name, int dy = 0) {
  const pbu_piece_t* p = g->ui.piece(name);
  if (p) addPiece(p, p->x, p->y + dy);
}
void addBlob(double cx, double cy, double rx, double ry, double a) {
  Op o;
  o.kind = Op::BLOB, o.cx = cx, o.cy = cy, o.rx = rx, o.ry = ry, o.a = a;
  g->ops.push_back(o);
}
void addSprite(const media::Sprite* sp, int x, int y, int opa, int w = 0, int h = 0) {
  if (!sp) return;
  Op o;
  o.kind = Op::SPRITE, o.p = sp, o.x = x, o.y = y, o.opa = opa, o.w = w, o.h = h;
  g->ops.push_back(o);
}

void render(const draw::Canvas& c) {
  const uint32_t t0 = nowUs();
  uint32_t t = t0;
  for (const Op& o : g->ops) {
    switch (o.kind) {
      case Op::ROAD: g->smooth ? draw::roadSmooth(c, g->sm) : draw::roadPlain(c, (const uint16_t*)o.p, o.x); break;
      case Op::SAND: draw::fill(c, SAND_565); break;
      case Op::BLOB: draw::blob(c, o.cx, o.cy, o.rx, o.ry, o.a); break;
      case Op::SPRITE:
        if (o.w) draw::spriteScaled(c, (const media::Sprite*)o.p, o.x, o.y, o.w, o.h, o.opa);
        else draw::sprite(c, (const media::Sprite*)o.p, o.x, o.y, o.opa);
        break;
      case Op::PIECE: draw::piece(c, g->ui, (const pbu_piece_t*)o.p, o.x, o.y); break;
      case Op::BAR:  // the OS's home line, which the panel would not get from LVGL now
        if (g->viaPanel) draw::bar(c, o.x, o.y, 110, 4, rgb565(0x16, 0x18, 0x1d), 128);
        break;
    }
    const uint32_t now = nowUs();
    (o.kind <= Op::SAND ? g->renderRoadUs : o.kind == Op::BLOB ? g->renderBlobUs : o.kind == Op::SPRITE ? g->renderSprUs : g->renderPieceUs) += now - t;
    t = now;
  }
  g->renderUs += nowUs() - t0;
}

// A number line from the pieces rd0..rd9, r_m ("m"), r_plus ("+"); a space is 4 px (ui/manifest.json).
const pbu_piece_t* glyph(char c) {
  char nm[8];
  if (c >= '0' && c <= '9') snprintf(nm, sizeof nm, "rd%c", c);
  else if (c == 'm') strcpy(nm, "r_m");
  else if (c == '+') strcpy(nm, "r_plus");
  else return nullptr;
  return g->ui.piece(nm);
}
int textW(const char* s) {
  int w = 0;
  for (; *s; s++) {
    const pbu_piece_t* p = glyph(*s);
    w += p ? p->adv : *s == ' ' ? 4 : 0;
  }
  return w;
}
void text(const char* s, int x, int dy) {  // dy: the line's middle - 20.5 (0 for the HUD's line)
  for (; *s; s++) {
    const pbu_piece_t* p = glyph(*s);
    if (!p) {
      x += *s == ' ' ? 4 : 0;
      continue;
    }
    addPiece(p, x + p->x, p->y + dy);
    x += p->adv;
  }
}

void hud() {
  const Game& G = g->game;
  addPieceAt("hud", HUD_DY);
  const pbu_piece_t* on = g->ui.piece("heart_on");
  const pbu_piece_t* off = g->ui.piece("heart_off");
  for (int k = 0; k < g->world.R.heartsMax && on && off; k++) {
    const pbu_piece_t* p = k < G.hearts ? on : off;
    addPiece(p, p->x + 22 * k, p->y + HUD_DY);
  }
  char s[16];
  snprintf(s, sizeof s, "%d m", (int)floor(G.dist));
  text(s, 20, HUD_DY);
  snprintf(s, sizeof s, "%d", G.coins);
  text(s, 302 - textW(s), HUD_DY);
}

void card() {
  const Game& G = g->game;
  g->homeY = -1;
  g->arrowY = -1;
  if (G.state == MENU) {
    char nm[32];
    snprintf(nm, sizeof nm, "menu_%s", g->world.maps[G.map].id.c_str());
    if (const pbu_piece_t* mp = g->ui.piece(nm)) g->ui.pixels(mp);  // read now (once a map), not while drawing
    addPieceAt(nm);
    if (g->prevBtn.rgb && g->nextBtn.rgb && g->world.nextMap(G.map) != G.map) {  // more than one map: choose
      const pbu_piece_t* p = g->ui.piece(nm);
      // Centred on the card's middle, each in the margin beside it (the card: x 34..285).
      const int cx = p ? p->x : 34, cw = p ? p->w : 252;
      g->arrowY = (p ? p->y + p->h / 2 : 220) - g->prevBtn.h / 2;
      addSprite(&g->prevBtn, (cx - g->prevBtn.w) / 2, g->arrowY, 255);
      addSprite(&g->nextBtn, cx + cw + (W - cx - cw - g->nextBtn.w + 1) / 2, g->arrowY, 255);
    }
    if (g->fromPet && g->coinNote.rgb) {  // under the card: where the coins go, and the way back to her
      const pbu_piece_t* p = g->ui.piece(nm);
      const int y = p ? p->y + p->h : 293;
      addSprite(&g->coinNote, (W - g->coinNote.w) / 2, y + 8, 255);
      g->homeX = (W - g->homeBtn.w) / 2, g->homeY = y + 8 + g->coinNote.h + 10;
    }
  } else if (G.state == OVER && G.overT > 1.2) {
    const bool four = G.last.bonus > 0;
    addPieceAt(four ? "over4" : "over3");
    if (g->fromPet) {
      const pbu_piece_t* p = g->ui.piece(four ? "over4" : "over3");
      g->homeX = (W - g->homeBtn.w) / 2, g->homeY = (p ? p->y + p->h : 314) + 14;
    }
    char v[4][16];
    int n = 0;
    snprintf(v[n++], 16, "%d m", (int)floor(G.last.dist));
    snprintf(v[n++], 16, "+%d", G.last.base);
    if (four) snprintf(v[n++], 16, "+%d", G.last.bonus);
    snprintf(v[n++], 16, "%d m", (int)floor(G.best));
    for (int k = 0; k < n; k++) text(v[k], 262 - textW(v[k]), 180 + 21 * k);  // rows at y 200, 221, 242, 263
  }
  if (g->homeY >= 0 && g->homeBtn.rgb) addSprite(&g->homeBtn, g->homeX, g->homeY, 255);
  else g->homeY = -1;
}

// An LVGL object drawn once into colours + alpha (PSRAM), then deleted: kit:: looks inside the game's pictures.
void bake(lv_obj_t* o, media::Sprite& out) {
  lv_obj_update_layout(o);
  lv_draw_buf_t* d = lv_snapshot_take(o, LV_COLOR_FORMAT_ARGB8888);
  lv_obj_delete(o);
  if (!d) return;
  const int w = d->header.w, h = d->header.h;
  uint8_t* mem = (uint8_t*)heap_caps_malloc(w * h * 3, MALLOC_CAP_SPIRAM);
  if (mem) {
    uint16_t* rgb = (uint16_t*)mem;
    uint8_t* a = mem + w * h * 2;
    for (int y = 0; y < h; y++) {
      const uint8_t* s = d->data + y * d->header.stride;  // B, G, R, A
      for (int x = 0; x < w; x++, s += 4) rgb[y * w + x] = rgb565(s[2], s[1], s[0]), a[y * w + x] = s[3];
    }
    out = {nullptr, w, h, rgb, a};
  }
  lv_draw_buf_destroy(d);
}

// The start card's "‹ ›" (dark round buttons over the road, either side of the card).
void bakeArrows() {
  for (int k = 0; k < 2; k++) {
    lv_obj_t* b = box(g->scr, 0x2b2f3a, 14);  // 28 px: centred in the 34 px beside the card, never over it
    lv_obj_set_style_bg_opa(b, 200, 0);
    lv_obj_set_size(b, 28, 28);
    lv_obj_t* gl = kit::glyph(b, k ? &gl_chev_16 : &gl_back_16, 0xffffff);
    lv_obj_center(gl);
    bake(b, k ? g->nextBtn : g->prevBtn);
  }
}

void bakeFromPet() {
  lv_obj_t* b = box(g->scr, 0xffffff, 18);  // "‹ Back home": white like the game's cards
  lv_obj_set_style_bg_opa(b, 240, 0);
  lv_obj_set_size(b, 120, 36);
  lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  kit::glyph(b, &gl_back_22, 0x2b2f3a);
  label(b, N_("Back home"), 15, true, 0x2b2f3a);  // i18n: back to the dog's home (the pet game); short
  bake(b, g->homeBtn);
  lv_obj_t* n = box(g->scr, 0x2b2f3a, 12);  // where the coins go
  lv_obj_set_style_bg_opa(n, 184, 0);
  lv_obj_set_size(n, 240, 24);
  lv_obj_t* t = label(n, "", 12, true, 0xffffff);
#ifndef NO_PET
  fitText(t, i18n::fmt(N_("The coins you collect go to {0}'s purse"), pet::name()).c_str(), 228, 20, 12, 9, true, 0);
#endif
  lv_obj_center(t);
  bake(n, g->coinNote);
}

constexpr uint32_t SMALL_PX = 2048;  // a picture this small may be decoded while drawing (about 2 ms)
constexpr double NEAR_M = 6;         // a thing this near is big and moves fast: its picture now, decoded here if need be
constexpr uint32_t NEAR_PX = 24000;  // ... up to this size (about 10 ms on the board; a crate stack by her, 96k px,
                                     // took 36 ms and made the picture late: the decoders do those)
int inlineLeft = 0;                  // near pictures that may still be decoded while drawing this picture

void thing(Thing& t, double vx, int X0) {
  const Game& G = g->game;
  const double d = t.z - G.dist;
  const pbs2_frame_t* f = G.thingFrame(t, d);
  if (!f || !f->bw) return;
  // Its picture for this distance; not decoded yet: decoded here when small, or near and not too big (one a
  // picture), else the one drawn last stands in (the decoders catch up).
  const uint32_t px = (uint32_t)f->bw * f->bh;
  const media::Sprite* sp = media::sprite(g->th, f, false);
  if (!sp && (px <= SMALL_PX || (d < NEAR_M && px <= NEAR_PX && inlineLeft > 0))) {
    if (px > SMALL_PX) inlineLeft--;
    sp = media::sprite(g->th, f, true);
  }
  const pbs2_frame_t* use = f;
  for (Win* w : {&g->win, &g->bw}) w->things++;
  if (!sp && t.shown) {
    sp = media::sprite(g->th, t.shown, false);
    if (sp) {
      use = t.shown;
      const uint32_t back = use->sx > f->sx ? use->sx - f->sx : 0;
      for (Win* w : {&g->win, &g->bw})
        w->thingOld++, w->thingNear += d < NEAR_M, w->thingBack = std::max(w->thingBack, back);
      const uint32_t up = use->bh ? 100u * f->bh / use->bh : 100;  // how much bigger it is drawn than recorded
      for (Win* w : {&g->win, &g->bw}) w->thingUp += up >= 120, w->thingUpMax = std::max(w->thingUpMax, up);
    }
  }
  if (!sp) {
    for (Win* w : {&g->win, &g->bw}) w->thingNone++;
    return;
  }
  t.shown = use;
#ifdef SIM
  // QC (docs/run.md): RUN_SIZES=FILE logs every thing drawn: picture, thing (its place on the road), distance, the
  // height of its picture for that distance, the height of the picture drawn (a stand-in's own), stand-in or not.
  static FILE* sizes = getenv("RUN_SIZES") ? fopen(getenv("RUN_SIZES"), "w") : nullptr;
  if (sizes) fprintf(sizes, "%lu %.2f %.2f %d %d %d\n", (unsigned long)G.pic, t.z, d, f->bh, use->bh, use != f);
#endif
  // Where it stands and how big it is come from its distance now, also when an older picture stands in: that one is
  // drawn into this picture's box. (Drawn where it was recorded, it went back up the road while the road came on;
  // moved but at its own size, a near thing shrank as it came on: a far picture is smaller.)
  const double cx = f->bx + 2.0 * f->flags - vx, bot = f->sy;
  if (t.kind == COIN || t.kind == HEART) {
    const double hh = bot - f->by;
    addBlob(cx, bot + 0.55 * hh, std::max(2.0, hh * 0.5), std::max(1.0, hh * 0.14), 0.3);
  } else {
    addBlob(cx, bot - 1, std::max(3.0, f->bw * 0.5), std::max(1.5, f->bw * 0.13), 0.42);
  }
  if (use == f) addSprite(sp, f->bx - X0, f->by, 255);
  else addSprite(sp, f->bx - X0, f->by, 255, f->bw, f->bh);
}

// The page's present() as drawing steps; also plans the road and wishes the next pictures to the decoders.
void build() {
  Game& G = g->game;
  const World& Wd = g->world;
  g->ops.clear();
  media::frame();
  const double vx = (Wd.R.wide - W) / 2.0 + G.x * Wd.R.panPx;
  const int X0 = std::max(0, std::min(Wd.R.wide - W, (int)floor(vx + 0.5)));
  // 1. the road picture for this distance; the next ones planned
  const int key = G.track();
  int keys[LOOK];
  int n = G.ahead(G.dist, keys, LOOK);
  if (!n || keys[0] != key) keys[0] = key, n = std::max(n, 1);
  media::plan(keys, n);
  bool late = false;
  const uint16_t* road = media::road(key, &late);
  Op r;
  r.kind = road ? Op::ROAD : Op::SAND, r.p = road, r.x = X0;
  g->ops.push_back(r);
  if (road) g->sm.start(road, X0);
  if (late) g->win.roadLate++, g->bw.roadLate++;
  else if (key != g->lastRoadKey) g->win.roadNew++, g->bw.roadNew++, g->lastRoadKey = key;
  // 2. things in front of her, far to near; 3. Biscuit; 4. the things she has passed
  std::vector<int> order(G.things.size());
  for (size_t i = 0; i < order.size(); i++) order[i] = i;
  std::sort(order.begin(), order.end(), [&](int a, int b) { return G.things[a].z > G.things[b].z; });
  inlineLeft = 1;
  for (int i : order)
    if (G.things[i].z - G.dist > 0.15) thing(G.things[i], vx, X0);
  const pbs2_frame_t* f = G.dogFrame();
  const double gx = Wd.R.dogX + G.x * Wd.R.lanePx - vx, gy = Wd.R.dogY;
  if (f->sr) addBlob(gx, gy + 2, f->sr, 0.3 * f->sr, 0.5);
  if (!(G.safe > 0 && G.pic % 2) && f->bw) {
    // Her pose now, decoded here when the decoders have not done it yet (at most 14k px, about 6 ms): an older pose
    // standing in made her leap start late on the screen. Her picture drawn last only when that fails.
    const pbs2_frame_t* use = f;
    const media::Sprite* sp = media::sprite(g->dog, f);
    if (!sp && g->lastDog) sp = media::sprite(g->dog, g->lastDog, false), use = g->lastDog;
    for (Win* w : {&g->win, &g->bw}) w->dogs++, w->dogOld += use != f;
    if (sp) {
      g->lastDog = use;
      addSprite(sp, (int)floor(use->bx - use->sx + gx + 0.5), (int)floor(use->by - use->sy + gy + 0.5), 255);
    }
    if (G.ghost && G.ghost->bw) {
      const pbs2_frame_t* h = G.ghost;
      addSprite(media::sprite(g->dog, h, false), (int)floor(h->bx - h->sx + gx + 0.5),
                (int)floor(h->by - h->sy + gy + 0.5), 255 * G.ghostLeft / 3);
    }
  }
  if (G.ghost && --G.ghostLeft <= 0) G.ghost = nullptr;
  for (int i : order)
    if (G.things[i].z - G.dist <= 0.15) thing(G.things[i], vx, X0);
  // 5. HUD and cards; the home line (drawn only when the picture goes straight to the panel)
  hud();
  card();
  Op bar;
  bar.kind = Op::BAR, bar.x = (W - 110) / 2, bar.y = H - 5 - 4;
  g->ops.push_back(bar);
  // The next pictures, decoded ahead in the background: her next two poses, each thing one and two steps on.
  const double dt = 1.0 / Wd.fps;
  media::want(g->dog, G.dogFrameAt(dt));
  media::want(g->dog, G.dogFrameAt(2 * dt));
  for (int k = 1; k <= 3; k++)
    for (int i = (int)order.size() - 1; i >= 0; i--) {  // near first: those are the big ones
      const Thing& t = G.things[order[i]];
      const double d = t.z - G.dist - G.v * dt * k;
      media::want(g->th, G.thingFrame(t, d), d < 2 * NEAR_M);
    }
  media::kick();
}

// Anything of the OS over the game (a toast, a notification, Control Center): then LVGL draws the picture.
bool overlays() {
  for (lv_obj_t* layer : {lv_layer_top(), lv_layer_sys()}) {
    for (uint32_t i = 0; i < lv_obj_get_child_count(layer); i++) {
      lv_obj_t* o = lv_obj_get_child(layer, i);
      if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) continue;
      lv_area_t a;
      lv_obj_get_coords(o, &a);
      const bool clear = lv_obj_get_style_bg_opa(o, 0) == LV_OPA_TRANSP;
      if (a.y2 < os::SB_H) continue;                                         // the status bar (LVGL draws it)
      if (clear && a.x2 < 24 && !lv_obj_get_child_count(o)) continue;       // the back strip
      if (clear && a.y1 >= H - 30 && a.x1 <= 0 && a.x2 >= W - 1) continue;  // the home strip (its line: BAR)
      static lv_obj_t* told = nullptr;
      if (told != o) {
        told = o;
        trace::add("run: drawn by LVGL while an overlay is up (%d,%d %dx%d, %d children)", (int)a.x1, (int)a.y1,
                   (int)lv_area_get_width(&a), (int)lv_area_get_height(&a), (int)lv_obj_get_child_count(o));
      }
      return true;
    }
  }
  return false;
}

#ifdef SIM
// The simulator's driver may give the direct path (tools/pawboop_run/simdrv.cpp); else LVGL draws everything.
}  // namespace
}  // namespace run
namespace lcd {
__attribute__((weak)) bool direct(int, int, void (*)(uint16_t*, int, int, void*), void*, bool) { return false; }
}  // namespace lcd
namespace run {
namespace {
#endif

void fillBand(uint16_t* dst, int y, int rows, void*) { render(draw::Canvas{dst, y, y + rows}); }

// One picture: built, then sent straight to the panel below the status bar (LVGL draws the status bar's rows
// from the buffer first, in the same tear period), or drawn whole into the buffer for LVGL.
void present(bool viaLvgl = false) {
  const uint32_t t0 = nowUs();
  build();
  const uint32_t t1 = nowUs();
  g->renderRoadUs = g->renderUs = g->renderSprUs = g->renderBlobUs = g->renderPieceUs = 0;
  g->viaPanel = !viaLvgl && !os::landscape() && !overlays();
  bool sent = false;
  if (g->viaPanel) {
    render(draw::Canvas{g->buf, 0, os::SB_H});
    lv_area_t a;
    lv_obj_get_coords(g->img, &a);
    a.y2 = a.y1 + os::SB_H - 1;
    lv_obj_invalidate_area(g->img, &a);
    lv_refr_now(nullptr);  // the status bar's rows: LVGL waits for the tear signal and sends them
    sent = lcd::direct(os::SB_H, H, fillBand, nullptr, false);
  }
  if (!sent) {
    g->viaPanel = false;
    if (os::landscape()) render(draw::Canvas{g->buf + CROP_Y * W, CROP_Y, CROP_Y + os::H()});
    else render(draw::Canvas{g->buf, 0, H});
    lv_obj_invalidate(g->img);
    g->pushPending = true;
  }
  const uint32_t t2 = nowUs();
  const uint32_t compose = (t1 - t0) + g->renderUs;
  for (Win* w : {&g->win, &g->bw}) {
    w->pics++;
    w->compose.add(compose), w->road.add(g->renderRoadUs), w->sprites.add(t1 - t0), w->hud.add(g->renderUs - g->renderRoadUs);
    w->spr.add(g->renderSprUs), w->shade.add(g->renderBlobUs), w->pieces.add(g->renderPieceUs);
    if (sent) w->push.add(t2 - t1 - g->renderUs);  // LVGL's status bar rows + waiting for DMA and the tear signal
  }
  if (g->swipeAt && !g->swipeShown) {
    g->swipeShown = true;
    if (sent) {  // on the panel now
      const uint32_t lat = nowUs() - g->swipeAt;
      g->win.swipe.add(lat), g->bw.swipe.add(lat);
      g->swipeAt = 0, g->swipeShown = false;
    }
  }
}

// ------------------------------------------------------------------ she plays by herself (dev, bench)
#if RUN_BENCH
void autopilot(double dt) {
  Game& G = g->game;
  if (G.state != RUN) return;
  if ((g->autoWait -= dt) > 0) return;
  const World& Wd = g->world;
  const bool busy = G.clip == Wd.cJump || G.clip == Wd.cSlide || G.clip == Wd.cStumble;
  auto blocked = [&](int lane, double from, double to) {
    for (const Thing& t : G.things) {
      const double d = t.z - G.dist;
      if (t.lane == lane && !t.hit && t.kind <= BLOCK && d > from && d < to) return true;
    }
    return false;
  };
  for (const Thing& t : G.things) {
    const double d = t.z - G.dist;
    if (t.lane != G.lane || t.hit || t.kind > BLOCK || d < -0.2) continue;
    const bool wall = blocked(-1, d - 1, d + 1) && blocked(0, d - 1, d + 1) && blocked(1, d - 1, d + 1);
    if (t.kind == JUMP && !busy && d < 0.8 + G.v * 0.15) {
      G.act(UP), g->autoWait = 0.2;
      return;
    }
    if (t.kind == SLIDE && !busy && d < 0.8 + G.v * 0.1) {
      G.act(DOWN), g->autoWait = 0.2;
      return;
    }
    if ((t.kind == BLOCK || (busy && !wall)) && d < 3 + G.v * 0.25) {
      for (int l : {G.lane - 1, G.lane + 1})
        if (l >= -1 && l <= 1 && !blocked(l, -1.5, d + 4)) {
          G.act(l < G.lane ? LEFT : RIGHT), g->autoWait = 0.25;
          return;
        }
    }
    break;
  }
  // nothing in the way: go for coins and hearts
  for (const Thing& t : G.things) {
    const double d = t.z - G.dist;
    if ((t.kind == COIN || t.kind == HEART) && d > 2 && d < 12 && t.lane != G.lane && !blocked(t.lane, -1, d + 2)) {
      G.act(t.lane < G.lane ? LEFT : RIGHT), g->autoWait = 0.3;
      return;
    }
  }
}

#else
void autopilot(double) {}
#endif

// ------------------------------------------------------------------ stats
void report(const char* tag, const Win& w) {
  const media::Stats m = media::snapshot();
  const float secs = std::max<uint32_t>(1, millis() - w.at) / 1000.0f;
  char line[900];
  snprintf(line, sizeof line,
           "%s %.1f pics/s (late steps %lu), road %.1f/s (late %lu) | compose avg %.1f max %.1f ms [road scale %.1f, "
           "build %.1f, sprites %.1f, shadows %.1f, HUD %.1f] | push avg %.1f max %.1f ms (%s) | move->picture avg %.0f max %.0f ms | decode road "
           "%lu avg %.1f max %.1f ms (inline %lu), sprites ahead %lu (helper %lu) inline %lu avg %.1f max %.1f ms (waited %lu ms) "
           "| card read %lu avg %.1f max %.1f ms | cache %lu KB (max %lu, her run %lu), evicted %lu, held %lu | old pictures: dog %lu/%lu, things %lu/%lu, "
           "not drawn %lu, near %lu, back up to %lu cm, enlarged %lu (up to %lu %%) | TJpgDec %lu (refused %lu)",
           tag, w.pics / secs, (unsigned long)w.late, w.roadNew / secs, (unsigned long)w.roadLate,
           w.compose.avg() / 1000.0f, w.compose.max / 1000.0f, w.road.avg() / 1000.0f, w.sprites.avg() / 1000.0f,
           w.spr.avg() / 1000.0f, w.shade.avg() / 1000.0f, w.pieces.avg() / 1000.0f, w.push.avg() / 1000.0f,
           w.push.max / 1000.0f, g->viaPanel ? "panel" : "LVGL",
           w.swipe.avg() / 1000.0f,
           w.swipe.max / 1000.0f, (unsigned long)m.roadDec, m.roadDec ? m.roadUs / 1000.0f / m.roadDec : 0.0f,
           m.roadMaxUs / 1000.0f, (unsigned long)m.roadInline, (unsigned long)m.sprAhead, (unsigned long)m.sprHelper,
           (unsigned long)m.sprInline,
           m.sprAhead + m.sprInline ? m.sprUs / 1000.0f / (m.sprAhead + m.sprInline) : 0.0f, m.sprMaxUs / 1000.0f,
           (unsigned long)(m.sprWaitUs / 1000), (unsigned long)m.readN, m.readN ? m.readUs / 1000.0f / m.readN : 0.0f,
           m.readMaxUs / 1000.0f, (unsigned long)m.cacheKB, (unsigned long)m.cacheMaxKB, (unsigned long)m.pinnedKB, (unsigned long)m.evicted, (unsigned long)m.sprHeld, (unsigned long)w.dogOld,
           (unsigned long)w.dogs, (unsigned long)w.thingOld, (unsigned long)w.things, (unsigned long)w.thingNone,
           (unsigned long)w.thingNear, (unsigned long)w.thingBack, (unsigned long)w.thingUp, (unsigned long)w.thingUpMax,
           (unsigned long)m.tjpgd, (unsigned long)m.fastFailed);
  trace::add("%s", line);
  if (strncmp(tag, "run:", 4)) Serial.printf("%s\n", line);
}

void resetMedia() {
  media::Stats& m = media::stats();
  const uint32_t kb = m.cacheKB;
  m = media::Stats();
  m.cacheKB = kb;
}

void onRefr(lv_event_t* e) {
  if (!g) return;
  const lv_event_code_t c = lv_event_get_code(e);
  if (c == LV_EVENT_REFR_START) {
    g->pushing = g->pushPending, g->pushPending = false;
    g->refrAt = nowUs();
  } else if (c == LV_EVENT_REFR_READY && g->pushing) {
    g->pushing = false;
    const uint32_t us = nowUs() - g->refrAt;
    g->win.push.add(us), g->bw.push.add(us);
    if (g->swipeShown) {
      const uint32_t lat = nowUs() - g->swipeAt;
      g->win.swipe.add(lat), g->bw.swipe.add(lat);
      g->swipeAt = 0, g->swipeShown = false;
    }
  }
}

// ------------------------------------------------------------------ the bench (run.h)
#if RUN_BENCH
void microBench() {
  // Decoding on this thread (the worker's pool may be faster: internal RAM), scaling, one composed picture.
  const World& Wd = g->world;
  const Game& G = g->game;
  uint32_t t = 0, n = 0, mx = 0;
  int keys[LOOK];
  const int nk = G.ahead(G.dist, keys, LOOK);
  for (int pass = 0; pass < 2 && nk; pass++) {
    const uint16_t* r = media::road(keys[0], nullptr);
    if (!r) break;
    t = 0, mx = 0;
    for (int k = 0; k < 10; k++) {
      const uint32_t t0 = nowUs();
      const draw::Canvas c{g->buf, 0, H};
      g->sm.start(r, 72);
      pass ? draw::roadPlain(c, r, 72) : draw::roadSmooth(c, g->sm);
      const uint32_t us = nowUs() - t0;
      t += us, mx = std::max(mx, us);
    }
    Serial.printf("RUNBENCH scale %s: avg %.1f max %.1f ms\n", pass ? "plain x2" : "smooth x2", t / 10000.0f,
                  mx / 1000.0f);
  }
  // Sprite decode, the hardest case: each thing's 12 nearest pictures in the middle lane.
  t = 0, n = 0, mx = 0;
  uint64_t px = 0;
  for (const ThingType& tt : Wd.things) {
    const pbs2_clip_t* c = tt.clip[1];
    for (int i = std::max(0, c->count - 12); i < c->count; i++) {
      const pbs2_frame_t* f = g->th.frame(c, i);
      if (!f->bw) continue;
      const uint32_t us = media::timeSprite(g->th, f);
      t += us, n++, mx = std::max(mx, us), px += (uint32_t)f->bw * f->bh;
    }
  }
  Serial.printf("RUNBENCH sprite decode (near things): %lu pictures avg %.1f max %.1f ms, %.2f us a pixel\n",
                (unsigned long)n, n ? t / 1000.0f / n : 0.0f, mx / 1000.0f, px ? (double)t / px : 0.0);
  t = 0, n = 0, mx = 0;
  for (int i = 0; i < Wd.cRun->count; i++) {
    const uint32_t us = media::timeSprite(g->dog, g->dog.frame(Wd.cRun, i));
    t += us, n++, mx = std::max(mx, us);
  }
  Serial.printf("RUNBENCH sprite decode (the dog's run): %lu pictures avg %.1f max %.1f ms\n", (unsigned long)n,
                n ? t / 1000.0f / n : 0.0f, mx / 1000.0f);
  t = 0, n = 0, mx = 0;
  for (int k = 0; k < nk; k++) {
    const uint32_t us = media::timeRoad(keys[k]);
    if (!us) continue;
    t += us, n++, mx = std::max(mx, us);
  }
  if (nk) {
    const pbs2_clip_t* c = Wd.things.size() ? Wd.things[0].clip[1] : nullptr;
    media::compare(keys[0], g->th, c ? g->th.frame(c, c->count - 4) : nullptr);
  }
  Serial.printf("RUNBENCH road decode (%s, %s): %lu pictures avg %.1f max %.1f ms\n", Wd.part,
                Wd.roads[0].file.data() ? "flash" : "card", (unsigned long)n, n ? t / 1000.0f / n : 0.0f,
                mx / 1000.0f);
}

// The live part: she runs by herself (no hits), 10 s in each set-up.
struct Phase {
  const char* tag;
  bool smooth, helper;
};
const Phase PHASES[3] = {{"RUNBENCH live smooth (default):", true, false},
                         {"RUNBENCH live plain scale:", false, false},
                         {"RUNBENCH live smooth + core 1 helper:", true, true}};

void phase(int k) {  // k = 1..3
  g->benchPhase = k, g->benchAt = millis();
  g->smooth = PHASES[k - 1].smooth;
  media::helper(PHASES[k - 1].helper);
  g->bw = Win();
  g->bw.at = millis();
  resetMedia();
}

void benchStep() {
  if (!g->benchPhase || millis() - g->benchAt < 10000) return;
  report(PHASES[g->benchPhase - 1].tag, g->bw);
  if (g->benchPhase < 3) return phase(g->benchPhase + 1);
  g->benchPhase = 0, g->smooth = true, g->autopilot = false, g->god = false, g->game.god = false;
  media::helper(false);
  Serial.println("RUNBENCH done");
  trace::add("run: bench done");
}

void startBench() {
  Serial.printf("RUNBENCH %s, %s, files in %s\n", g->id.c_str(), g->world.part,
                g->world.roads[0].file.data() ? "the internal flash" : "the memory card");
  microBench();
  g->autopilot = true, g->god = true, g->game.god = true, g->game.noPay = true;
  if (g->game.state != RUN) g->game.reset();
  phase(1);
}

#else
void benchStep() {}
void startBench() {}
#endif

// ------------------------------------------------------------------ the screen
bool onScreen() { return g && g->scr && lv_screen_active() == g->scr && !os::saverOn(); }

void act(Act a) {
  if (!g || !g->shown) return;
  Game& G = g->game;
  const pbs2_clip_t* before = G.clip;
  G.act(a);
  g->swipeAt = nowUs(), g->swipeShown = false;
  if (G.clip != before) {  // her new move's first poses, to the decoder now (the next picture shows the 2nd)
    for (int k = 1; k <= 3; k++) media::want(g->dog, g->dog.frame(G.clip, k));
    media::kick();
  }
}

// The start card's arrows: the run starts in the previous / next installed map (kept for the next time).
void pickMap(int dir) {
  Game& G = g->game;
  if (G.state != MENU) return;
  const World& Wd = g->world;
  int m = G.map;
  if (dir > 0) {
    m = Wd.nextMap(m);
  } else {
    for (int k = 1; k < (int)Wd.maps.size(); k++) {  // the previous installed one
      const int i = (G.map - k + (int)Wd.maps.size()) % (int)Wd.maps.size();
      if (Wd.maps[i].road >= 0) {
        m = i;
        break;
      }
    }
  }
  if (m == G.map) return;
  G.begin(&g->world, m);
  Preferences p;
  p.begin("run", false);
  p.putString("map", Wd.maps[m].id);
  p.end();
  trace::add("run: start map %s", Wd.maps[m].id.c_str());
}

void onTouch(lv_event_t* e) {
  if (!g) return;
  const lv_event_code_t code = lv_event_get_code(e);
  lv_point_t p;
  lv_indev_get_point(lv_indev_active(), &p);
  p.x -= offX, p.y -= offY;  // into the picture's own coordinates (the Back home button is drawn in it)
  if (code == LV_EVENT_PRESSED) {
    g->down = true, g->done = false, g->px0 = p.x, g->py0 = p.y;
  } else if (code == LV_EVENT_PRESSING) {
    if (!g->down || g->done) return;
    const int dx = p.x - g->px0, dy = p.y - g->py0;
    if (std::max(abs(dx), abs(dy)) < SWIPE) return;  // 22 px on the 320 px screen = 3.4 mm of finger travel
    g->done = true;
    act(abs(dx) > abs(dy) ? (dx < 0 ? LEFT : RIGHT) : (dy < 0 ? UP : DOWN));
  } else if (code == LV_EVENT_RELEASED) {
    const bool home = g->homeY >= 0 && g->px0 >= g->homeX - 6 && g->px0 < g->homeX + g->homeBtn.w + 6 &&
                      g->py0 >= g->homeY - 6 && g->py0 < g->homeY + g->homeBtn.h + 6;
    const bool arrow = g->arrowY >= 0 && g->py0 >= g->arrowY - 14 && g->py0 < g->arrowY + 42 && (g->px0 < 46 || g->px0 >= W - 46);
    if (g->down && !g->done) {
      if (home) os::back();  // back to the dog (the coins are in its purse already)
      else if (arrow) pickMap(g->px0 < 50 ? -1 : 1);
      else act(TAP);
    }
    g->down = false;
  } else if (code == LV_EVENT_PRESS_LOST) {
    g->down = false;
  }
}

void message(const char* title, const char* body) {
  lv_obj_t* b;
  lv_obj_t* s = os::screen(N_("PawBoop Run"), &b, os::NOSCROLL);
  lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_t* t = label(b, "", 22, true, pal().fg);
  lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
  wrapText(t, title, os::W() - 48, 22, true);
  lv_obj_t* m = label(b, "", 16, false, pal().dim);
  lv_obj_set_style_text_align(m, LV_TEXT_ALIGN_CENTER, 0);
  wrapText(m, body, os::W() - 48, 16);
  os::open(s);
}

void loadJob(void* arg) {  // on the decoder task: the packs into PSRAM, run.json, the road files
  Screen* s = (Screen*)arg;
  String err;
  const char* id = s->id.c_str();
  // The chosen dog's run clips (its package's run.pbs); run 1.0 carried Biscuit's as core/run_dog.pbs.
  bool ok = (s->dogPkg.length() && s->dog.load(s->dogPkg.c_str(), "run.pbs", err)) || s->dog.load(id, "core/run_dog.pbs", err);
  if (ok) err = "";
  ok = ok && s->th.load(id, "core/run_things.pbs", err) && s->ui.load(id, "core/run_ui.pbu", err);
  if (ok) {
    pkg::File f;
    uint8_t* js = nullptr;
    ok = pkg::open(id, "run.json", f) && (js = pkg::loadAll(f)) != nullptr;
    if (!ok) err = i18n::fmt(N_("missing {0}"), "run.json");
    s->world.dog = &s->dog, s->world.th = &s->th;
    ok = ok && s->world.parse((const char*)js, f.size(), err) && s->world.openRoads(id, err);
    heap_caps_free(js);
  }
  s->err = err;
  s->failed = !ok;
  s->loaded = true;
}

// The dog's run lines (its voice pack: run_start, run_hit, run_over, run_best), never over each other's start.
void say(const char* moment, bool force) {
#ifndef NO_PET
  if (const pba_clip_t* l = pet::voice::lineFor(moment)) pet::voice::speak(l, force);
#endif
}
void heard(const Game& G) {
  if (G.state == RUN && g->heardState != RUN) g->newBest = g->bestSaid = false, g->heardHearts = G.hearts;
  if (G.state == RUN && G.hearts < g->heardHearts && G.hearts > 0) say("@run_hit", true);
  if (G.hearts == 0 && g->heardHearts > 0) say("@run_over", true);  // dizzy
  if (G.state == OVER && G.overT > 1.2 && g->newBest && !g->bestSaid) g->bestSaid = true, say("@run_best", true);
  if (G.state == MENU && g->heardState != MENU) say("@run_start", false);
  g->heardHearts = G.hearts, g->heardState = G.state;
}

void begin() {  // the packs are in: the game starts (the start card)
  Screen* s = g;
  const media::Pack* packs[2] = {&s->dog, &s->th};
  bool cardRoads = false;
  for (int i = 0; i < s->world.nroads; i++) cardRoads |= !s->world.roads[i].file.data();
  if (!media::begin(packs, 2, cardRoads)) {
    s->failed = true, s->err = T_("not enough memory");
    return;
  }
  media::roads(s->world.roads, s->world.nroads);
  int start = s->world.firstMap();
  {
    Preferences p;
    p.begin("run", true);
    const int m = s->world.mapIndex(p.getString("map", "").c_str());
    p.end();
    if (m >= 0 && s->world.maps[m].road >= 0) start = m;
  }
  if (s->arg.length()) {
    const int m = s->world.mapIndex(s->arg.c_str());
    if (m >= 0 && s->world.maps[m].road >= 0) start = m;
  }
  s->game.healthy = petHealthy, s->game.paid = petPaid;
  s->game.begin(&s->world, start);
  s->game.best = loadBest() / 100.0;
  s->ready = true;
  s->readyAt = millis();
  s->win = Win();
  s->win.at = millis();
  int links = 0, fades = 0;
  for (const LinkInfo& l : s->world.links) links += l.road >= 0, fades += l.fade;
  trace::add("run: ready, %s, %d road files, %d links, %d fades, start %s", s->world.part, s->world.nroads, links,
             fades, s->world.maps[start].id.c_str());
  for (int i = 0; i < s->world.cRun->count; i++) media::keep(s->dog, s->dog.frame(s->world.cRun, i));  // her run
  int keys[LOOK];  // the first road pictures, decoded in the background while the spinner turns (onTimer waits)
  media::plan(keys, s->game.ahead(s->game.dist, keys, LOOK));
  media::kick();
}

void onTimer(lv_timer_t*) {
  if (!g) return;
  const uint32_t now = millis();
  const double dt = std::min(0.25, (now - g->last) / 1000.0);
  g->last = now;
  if (g->ready && !g->shown) {  // the first picture once its road is decoded (the UI thread never waits for it)
    int keys[LOOK];
    const int n = g->game.ahead(g->game.dist, keys, LOOK);
    if (n && !media::roadReady(keys[0]) && millis() - g->readyAt < 3000) return;
    if (g->spin) lv_obj_add_flag(g->spin, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(g->img, LV_OBJ_FLAG_HIDDEN);
    g->shown = true;
    present(true);
    if (g->arg == "bench") startBench();
    g->last = now;
    return;
  }
  if (!g->ready) {
    // posted() first: its lock makes what the loading job wrote (loaded, failed, err) visible here.
    if (!media::posted() || !g->loaded) return;
    if (!g->failed) begin();
    if (g->failed) {
      const String err = g->err;
      trace::add("run: cannot start: %s", err.c_str());
      os::back();  // closes this screen (onDelete frees everything)
      const bool noRoads = err == T_("no roads installed");
      message(noRoads ? N_("No maps yet") : N_("Game data missing"),
              noRoads ? String(T_("Add a map from the Store: PawBoop, Run maps.")).c_str()
                      : i18n::fmt(N_("Reinstall PawBoop Run from the Store ({0})."), err).c_str());
    }
    return;
  }
  if (!onScreen()) {
    // Going away: the whole picture into LVGL's buffer once (what shows of this screen from now is LVGL's).
    if (g->wasOn) present(true);
    g->wasOn = false, g->acc = 0;
    return;
  }
  const bool back = !g->wasOn;  // back in front: one picture through LVGL first (it redraws the whole screen)
  g->wasOn = true;
  Game& G = g->game;
  const double before = G.best;
  g->acc += dt;
  const double fd = 1.0 / g->world.fps;
  int k = 0;
  while (g->acc >= fd && k < 3) {
    g->acc -= fd;
    if (g->autopilot) autopilot(fd);
    G.step(fd);
    k++;
  }
  if (k) {
    if (k > 1) g->win.late += k - 1, g->bw.late += k - 1;
    g->win.steps += k, g->bw.steps += k;
    present(back);
  }
  if (g->acc > fd) g->acc = 0;
  if (G.best > before && !G.noPay) saveBest(G.best);  // not a developer's run
  if (G.best > before && G.state != MENU) g->newBest = true;
  heard(G);
  if (g->autopilot && G.state == OVER && G.overT > 2) G.act(TAP);
  benchStep();
  if (now - g->win.at > 10000 && g->win.pics) {
    report("run:", g->win);
    g->win = Win();
    g->win.at = now;
    if (!g->benchPhase) resetMedia();
  }
}

// Everything of one game screen. Its screen may be deleted later (the OS deletes closed screens asynchronously):
// a new game can start meanwhile, so only what belongs to `s` is freed here.
void onDelete(lv_event_t* e);

void teardown(Screen* s, bool deleting) {
  if (s->timer) lv_timer_delete(s->timer);
  s->timer = nullptr;
  if (!deleting && lv_obj_is_valid(s->scr)) {  // deleted later by the OS: not by us twice
    lv_obj_remove_event_cb_with_user_data(s->scr, onDelete, s);
  }
  if (g == s) lv_display_remove_event_cb_with_user_data(lv_display_get_default(), onRefr, nullptr);
  if (s->ready) s->game.payNow();
  media::end();  // waits for the decoder task (it may still be loading the packs)
  s->world.freeRoads();
  s->dog.free(), s->th.free(), s->ui.free();
  if (s->img && !deleting && lv_obj_is_valid(s->img)) lv_obj_add_flag(s->img, LV_OBJ_FLAG_HIDDEN);  // drawn no more
  if (s->img) lv_image_cache_drop(&s->dsc);
  heap_caps_free(s->buf);
  heap_caps_free(s->sm.rows);
  heap_caps_free((void*)s->homeBtn.rgb), heap_caps_free((void*)s->coinNote.rgb);
  heap_caps_free((void*)s->prevBtn.rgb), heap_caps_free((void*)s->nextBtn.rgb);
  if (g == s) g = nullptr;
  delete s;
  trace::add("run: closed");
}

void onDelete(lv_event_t* e) { teardown((Screen*)lv_event_get_user_data(e), true); }

}  // namespace

bool isOpen() { return g != nullptr; }

void open(const char* pkgId, const char* arg) {
  if (g && lv_obj_is_valid(g->scr) && lv_screen_active() == g->scr) {  // already in front
    if (arg && !strcmp(arg, "bench") && g->shown) startBench();
    return;
  }
  if (g) teardown(g, false);  // its screen is on its way out (apps::open -> os::closeApps deletes it later)
  const bool land = os::landscape();
  HUD_DY = land ? CROP_Y : os::SB_H;
  offX = land ? (os::W() - W) / 2 : 0, offY = land ? -CROP_Y : 0;
#ifndef SIM
  desk::releaseConnection();  // the kept HTTPS connection holds internal RAM the decoders want
#endif
  home::releasePictures();  // PSRAM for the decoders' cache (landscape's pictures are bigger)
  uint16_t* buf = (uint16_t*)heap_caps_malloc(W * H * 2, MALLOC_CAP_SPIRAM);
  uint32_t* hrow = (uint32_t*)heap_caps_malloc(3 * W * 4, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!hrow) hrow = (uint32_t*)heap_caps_malloc(3 * W * 4, MALLOC_CAP_SPIRAM);
  if (!buf || !hrow) {
    heap_caps_free(buf), heap_caps_free(hrow);
    os::toast(N_("Not enough memory to open PawBoop Run"));
    return;
  }
  draw::blobInit();
  g = new Screen();
  g->id = pkgId ? pkgId : "vn.pawboop.run";
  g->arg = arg ? arg : "";
  g->fromPet = g->arg == "pet";
#ifndef NO_PET
  if (const pet::dogs::Info* d = pet::dogs::chosen()) g->dogPkg = pet::dogs::pkgId(d->key);
#endif
  g->buf = buf, g->sm.rows = hrow;
  for (int i = 0; i < W * H; i++) buf[i] = 0xFFFF;
  lv_obj_t* body;
  g->scr = os::screen(N_("PawBoop Run"), &body, os::BARE);
  lv_obj_set_style_bg_color(g->scr, lv_color_white(), 0);
  g->spin = spinner(body, 44);
  lv_obj_align(g->spin, LV_ALIGN_CENTER, 0, -24);
  const int shownH = land ? os::H() : H;  // landscape: the window of rows CROP_Y ..
  g->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  g->dsc.header.cf = LV_COLOR_FORMAT_RGB565;
  g->dsc.header.w = W;
  g->dsc.header.h = shownH;
  g->dsc.header.stride = W * 2;
  g->dsc.data_size = W * shownH * 2;
  g->dsc.data = (const uint8_t*)(buf + (land ? CROP_Y * W : 0));
  g->img = lv_image_create(g->scr);  // the whole screen, under the status bar and the OS gesture strips
  lv_image_set_src(g->img, &g->dsc);
  lv_obj_set_pos(g->img, offX, 0);
  if (land) {  // dark sides, no status bar; a swipe on the sides plays too
    lv_obj_set_style_bg_color(g->scr, lv_color_hex(0x1d1712), 0);
    os::fullScreenFor(g->scr, true);
    for (lv_event_code_t c : {LV_EVENT_PRESSED, LV_EVENT_PRESSING, LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST})
      lv_obj_add_event_cb(g->scr, onTouch, c, nullptr);
  }
  lv_obj_add_flag(g->img, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(g->img, LV_OBJ_FLAG_HIDDEN);
  for (lv_event_code_t c : {LV_EVENT_PRESSED, LV_EVENT_PRESSING, LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST})
    lv_obj_add_event_cb(g->img, onTouch, c, nullptr);
  lv_obj_add_event_cb(g->scr, onDelete, LV_EVENT_DELETE, g);
  os::barDarkFor(g->scr, true);  // dark status bar text over the road's light sky
  if (g->fromPet) bakeFromPet();
  bakeArrows();
  lv_display_add_event_cb(lv_display_get_default(), onRefr, LV_EVENT_REFR_START, nullptr);
  lv_display_add_event_cb(lv_display_get_default(), onRefr, LV_EVENT_REFR_READY, nullptr);
  g->last = millis();
  g->timer = lv_timer_create(onTimer, 5, nullptr);
  os::open(g->scr);
  trace::add("run: open %s %s", g->id.c_str(), g->arg.c_str());
  media::post(loadJob, g);
}

void bench(const char* pkgId) {
  if (g && g->shown) return startBench();
  if (!g) open(pkgId, "bench");
  else g->arg = "bench";  // still loading: starts once ready
}

bool dev(const char* cmd) {
#if !RUN_BENCH
  (void)cmd;
  return false;
#else
  if (!cmd) return false;
  if (!strcmp(cmd, "stats")) {
    if (g && g->ready) report("RUNSTATS", g->win);
    return true;
  }
  if (!g || !g->ready) return false;
  Game& G = g->game;
  if (strcmp(cmd, "manual") && strcmp(cmd, "frame") && strncmp(cmd, "smooth", 6) && strncmp(cmd, "helper", 6))
    G.noPay = true;  // a run helped by a developer pays nothing to the pet
  if (!strcmp(cmd, "auto")) g->autopilot = true;
  else if (!strcmp(cmd, "manual")) g->autopilot = false;
  else if (!strcmp(cmd, "god")) g->god = G.god = !G.god;
  else if (!strncmp(cmd, "smooth ", 7)) g->smooth = atoi(cmd + 7) != 0;
  else if (!strncmp(cmd, "helper ", 7)) media::helper(atoi(cmd + 7) != 0);
  else if (!strcmp(cmd, "end")) G.endRun();
  else if (!strcmp(cmd, "frame")) present(true);  // one picture whole through LVGL: a SHOT shows it right
  else if (!strncmp(cmd, "warp ", 5)) {
    const double m = atof(cmd + 5);
    G.dist += m, G.next += m, G.nextHeart += m;
    for (Thing& t : G.things) t.z += m;
  } else return false;
  return true;
#endif
}

}  // namespace run
