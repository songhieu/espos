#pragma once
// Hồ cá cảnh, inside (docs/tank.md). The engine's parts share this header:
//   data.cpp   tank.json and the packs of the package (species, looks, rules; pictures copied padded into PSRAM)
//   water.cpp  the water: motion (step) and picture (prepare + draw), the hand-off's fw/tank_ref.c and ref/engine.js
//   gfx.cpp    what the screens draw over the water: rounded glass, text, icons, at the screen's scale
//   game.cpp   the keeper's rules (ref/game.js): two tanks, gauges, coins, levels, the shop's rows, the save
//   ui.cpp     the screens (ref/ui.js): top bar, dock, food chooser, sponge, fish card, shop sheet, toasts
//   screen.cpp the app screen, the frame loop, the panel, the saver's background, the dev console
// Units: screen units (a pixel of the 3.5" board), the screen is VW x VH = 320 x 480 or 480 x 320; the picture is FW x FH
// = the screen times SC (1, or 2/3 on the 2.8" board: drawn straight at the panel's size).
#include <Arduino.h>
#include <lvgl.h>

#include <vector>

#include "pkg/pkg.h"

// The screens and knobs for shots and tests (the simulator; TANK_QA=1 on a board build): not in the release image.
#if defined(SIM) && !defined(TANK_QA)
#define TANK_QA 1
#endif

namespace tank {

constexpr int ZD = 90, NSEG = 20, TOPY = 42, MAXF = 64, MAXFOOD = 64, MAXBUB = 64, MAXCOIN = 16, MAXRING = 8;
constexpr int NSP_MAX = 48, NLOOK_MAX = 6, NPLANT_MAX = 12, NDECOR_MAX = 6, NUP = 8;

// ---------------------------------------------------------------- data (tank.json)
enum Style : uint8_t { ST_cruise, ST_dart, ST_hover, ST_waddle, ST_long, ST_floor, ST_crawl, ST_puff, ST_perch, ST_upright, ST_drift, ST_N };
enum Zone : uint8_t { Z_mid, Z_top, Z_low, Z_floor };
enum Food : uint8_t { F_flake, F_sink, F_live, F_N };
enum Role : uint8_t { R_easy, R_school, R_cleaner, R_show, R_rare };

struct StyleK {
  float v, vmax, wn, om, f0, f1, A, e0, p, k, glide, drag, pauseP, pitch, lift, lap, bob, wob, dy, mea, pause0, pause1;
  float env[NSEG + 1], envM[NSEG], u13[NSEG + 1], um13[NSEG];  // the body's powers, worked out once
};

struct Spr {  // a picture: RGB565 + alpha (straight), padded with one clear pixel round it (stride w + 2)
  int16_t w = 0, h = 0, st = 0;
  const uint16_t* c = nullptr;  // pixel (0, 0); (-1 .. w) x (-1 .. h) may be read
  const uint8_t* a = nullptr;
  const uint16_t* rs = nullptr;  // per row: its first and last column with any alpha (first > last: clear); or none
  const uint16_t* cs = nullptr;  // per column: its first and last row with any alpha (the fish, drawn by columns)
  // The fish's pictures again by columns, colour | alpha << 16 a pixel (stride th = h + 2, padded as c): the fish are
  // drawn column by column, and read by rows each pixel took its own PSRAM cache lines (the board, 2026-11-20)
  const uint32_t* t = nullptr;
  int16_t th = 0;
};

struct Species {
  const char *id, *vi;
  bool marine, fins, school, nip, solo, inv, invEater, hide, lid, plantEater, nipCoral, host, messy, puffs, fixed;
  uint8_t role, style, zone, food, eats /* 0 -, 1 leftovers, 2 algae */, breed /* 0, 1 easy, 2 no */, temp /* 0, 1 warm, 2 cool */;
  int16_t w, pred, price, lv, val, group, clean, minSize, mature;
  float space, asp;
  int16_t pic = -1, half = -1, thumb = -1;  // piece indexes in their packs
};
struct Geo {  // a look's places for one way of holding the board
  int16_t floor0, floor1;
  uint8_t water[3], light[3];
  float slots[6][3], decor[3][2], air[2];
};
struct Look {
  const char *id, *vi;
  bool marine;
  int16_t price, lv;
  int8_t plants0[3];
  Geo up, wide;
  int nslots, ndecor;
};
struct PlantK {
  const char *id, *vi;
  bool marine;
  int16_t price, lv, comfort, w;
  float a, pw, k, hz, asp;
};
struct DecorK {
  const char *id, *vi;
  int16_t price, lv, comfort, w;
  bool coins;
  float asp;
};
enum UpId : uint8_t { U_size, U_filter, U_light, U_air, U_feeder, U_heater, U_lid, U_skimmer };
struct UpK {
  const char *id, *vi, *what;
  int ncost;
  int16_t cost[6];
  float lv[6];  // per level: space / dirt / comfort / floor (by id)
  float algae[6];  // the lamp's algae factor
  bool sw, marineOnly;
};
struct TaskK {
  const char *id, *vi;
  int16_t n, coins;
};
struct Rules {
  int startCoins, startFish[8], nStartFish;
  float startFood, startClean, startAlgae;
  int xpAt[12], nXp;
  int xpFeed, xpWipe, xpWater, xpCoin, xpNew, xpBirth;
  float foodHour, foodFishHour, pelletFood, pinchWait;
  int pellets;
  float leftRest, leftDirt;
  float cleanHour, cleanSpaceHour, messy, waterTo, waterWait;
  float algaeLamp, algaeDark, algaeEater;
  float cBase, crowdFrom, crowdPenalty, algaePenalty, lampNight, playMax, playPerS, playFadeH;
  float moodHappy, moodLow;
  float coinEvery0, coinEvery1, awayHours, coinYoung;
  int maxLying;
  float adultH, youngScale, fryScale;
  float breedMood, breedAfter, breedRest;
  float sellAdult, sellYoung;
  int night0, night1;
  const char* dietVi[F_N];
  int dietCost[F_N];
  float hungry;
  int sadComfort, sadComfortMax, dirty, nipCalm, hostComfort;
  float afraidPx;
  bool hideDecor[NDECOR_MAX];
  int hidePlants;
  float sickBelow, sickHour;
  int cureCost;
  const char* sickName;
  int saltCost;
  int tasksADay, tasksXp;
  TaskK tasks[10];
  int ntasks;
  int marinePrice, marineLv;
};
struct Data {
  Species sp[NSP_MAX];
  int nsp = 0;
  Look look[NLOOK_MAX];
  int nlook = 0;
  PlantK plant[NPLANT_MAX];
  int nplant = 0;
  DecorK decor[NDECOR_MAX];
  int ndecor = 0;
  UpK up[NUP];
  StyleK style[ST_N];
  Rules R;
};
extern Data* Dp;  // in PSRAM (11 KB: internal RAM is the board's scarce one), made by load(); never at static init
#define D (*::tank::Dp)

// The packs of the package: their tables in PSRAM, pieces read when wanted (memory-mapped flash or the card).
enum PackId : uint8_t { P_FRESH, P_MARINE, P_THINGS, P_THUMBS, P_UI, P_N };
struct Pack {
  pkg::File f;
  uint8_t* tab = nullptr;  // header + table
  int n = 0;
  bool open(const char* id, const char* file);
  void close();
  int find(const char* name) const;
  void info(int i, int& w, int& h, int& fmt, uint32_t& off) const;
};
extern Pack packs[P_N];
extern String pkgId;

bool load(const char* id, String& err);  // tank.json, the packs' tables, the fonts (once a boot)
bool loaded();
int speciesOf(const char* id);
int lookOf(const char* id);
int plantOf(const char* id);
int decorOf(const char* id);
const Spr* spr(PackId p, int piece);       // the padded copy (made on first use, kept); nullptr if missing
const Spr* sprNamed(PackId p, const char* name);
int sprTrim(bool keepThumbs);              // frees every picture made (the water must be gone); how many
// The look's picture at the frame's size (FW x FH, as held): RGB565 into dst; false when its pack is missing.
bool background(const char* look, bool land, int fw, int fh, uint16_t* dst);
const lv_font_t* font(int px, bool bold);  // Ubuntu with every Vietnamese letter (the package's, else the OS's)
void resample565(const uint16_t* s, int sw, int sh, uint16_t* d, int dw, int dh);

// ---------------------------------------------------------------- drawing target
// A piece of the picture being drawn: pixel (x, y) of [x0, x1) x [y0, y1) is at p + (x - x0) * dx + (y - y0) * dy.
// Straight rows (dx 1) for the 3.5" board upright and the 2.8" one; the 3.5" board on its side draws columns of
// the picture straight into the panel's rows (dx = +-320).
struct Tgt {
  uint16_t* p;
  int dx, dy;
  int x0, y0, x1, y1;
  __attribute__((always_inline)) inline uint16_t* at(int x, int y) const { return p + (x - x0) * dx + (y - y0) * dy; }
};
// a 565 colour spread out so its three parts scale in one multiply; w = 0..32
__attribute__((always_inline)) inline uint32_t ex(uint16_t c) { return (c | ((uint32_t)c << 16)) & 0x07E0F81Fu; }
__attribute__((always_inline)) inline uint16_t pk(uint32_t e) { return (uint16_t)(e | (e >> 16)); }
__attribute__((always_inline)) inline uint32_t mix(uint32_t a, uint32_t b, int w) { return (a + (((b - a) * (uint32_t)w) >> 5)) & 0x07E0F81Fu; }
// sinf, cosf and powf of the board's C library work in double precision, which the S3 does in software: about 7 µs
// a call on the board (2026-11-20), and the swim and the plants' sway want thousands a picture. These stay in float:
// fsin within 2e-4, fpow within 1e-4 on [0, 1].
#ifndef TANK_OLD_MATH
float fsin(float x);
float fcos(float x);
float fpow(float x, float p);  // x >= 0
float fsqrt(float x);          // x >= 0 (the library's goes bit by bit)
#else  // a measuring build only (-DTANK_OLD_MATH): the library's maths again, for a before/after in QEMU
#define fsin sinf
#define fcos cosf
#define fpow powf
#define fsqrt sqrtf
#endif
inline uint16_t rgb565(int r, int g, int b) {
  r = r < 0 ? 0 : r > 255 ? 255 : r, g = g < 0 ? 0 : g > 255 ? 255 : g, b = b < 0 ? 0 : b > 255 ? 255 : b;
  return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}
inline uint16_t hex565(uint32_t c) { return rgb565(c >> 16 & 255, c >> 8 & 255, c & 255); }

// ---------------------------------------------------------------- the water (water.cpp)
struct Look;
struct School {
  float x, y, z, tx, ty, tz, vx, vy, vz, wait;
  int sp, n;
};
struct Fish {
  int sp;
  uint32_t uid;  // the game's fish (its record's uid), 0 in a free tank
  const Species* S;
  const StyleK* st;
  const Spr *pic, *half;
  float asp, size;
  float x, y, z, yaw, pitch, spd, om, ph, amp, tx, ty, tz, sd, wait, flee, fx, fy, kx, ky, vyS, lift, seed, mw, vk, full,
      gulp, puff, ox, oy, oz, ot, zp, hop, hx, hy;
  bool beat, sad, sick;
  int school;  // index in schools[], -1
  int host;    // plant index, -1
  int nafraid;
  uint32_t afraid[8];  // uids
};
struct PlantI {
  int k;  // D.plant index
  const Spr* pic;
  float x, z, s, ph;
  bool flip;
  int slot;
};
struct DecorI {
  int k;
  const Spr* pic;
  float x, z;
  int w, slot;
  bool bubbler;
};
struct FoodI {
  float x, y, z, vy, s, rest, food;
  uint8_t kind;
  bool lying;
};
struct CoinI {
  float x, y, z, vy, s, t;
  bool rest;
  int val;
};
struct Bub {
  float x, y, r, v, s;
};
struct Ring {
  float x, y, t;
  bool gold;
};

struct Scene {
  // the screen and the frame
  int VW = 320, VH = 480, FW = 320, FH = 480;
  float SC = 1;
  bool land = false;
  int AC = 20, AR = 30;
  // the look
  int look = 0;
  const Geo* geo = nullptr;
  uint16_t* bgRaw = nullptr;  // the look's picture at the frame's size (PSRAM)
  uint16_t* bgc = nullptr;    // as it is now: murkier when dirty, dimmer and bluer when the lamp is off
  bool bgBad = true;          // bgc must be made again
  bool bgLoad = true;         // bgRaw must be read again (another look, size or way of holding)
  float bgTint[3] = {-1, -1, -1}, bgHaze = -1;
  // things in the water
  Fish fish[MAXF];
  int nfish = 0;
  School schools[NSP_MAX];
  PlantI plants[6];
  int nplants = 0;
  DecorI decor[3];
  int ndecor = 0;
  FoodI food[MAXFOOD];
  int nfood = 0;
  CoinI coins[MAXCOIN];
  int ncoins = 0;
  Bub bub[MAXBUB];
  int nbub = 0;
  struct {
    float x, y, z, s, r;
  } specks[16];
  Ring rings[MAXRING];
  int nrings = 0;
  float algae[30 * 30] = {0};
  bool algaeOn = false;
  // The algae film, baked (2026-11-19, the board's numbers: a pass over the whole frame took 34 ms): its weight a
  // pixel (0..32, PSRAM) is made from the grid when the grid changes, the background (bgc) carries it, and the things
  // drawn over the background take it as they are drawn (put()). filmX0.. = the part to make again (frame pixels).
  uint8_t* filmW = nullptr;
  uint8_t* filmT = nullptr;  // the same weights by columns (the fish are drawn down columns); made with the algae
  // bgc kept by columns (bgc[X * FH + Y]) when the pictures go out a column at a time (the 3.5" on its side: the
  // panel's rows are the screen's columns), so the background goes out as whole runs (memcpy) read straight.
  bool bgCols = false, bgColsWant = false;
  int filmX0 = 0, filmY0 = 0, filmX1 = 0, filmY1 = 0;
  bool filmSoft = false;  // the part to make again may take a few pictures (makeBg: the haze or the light moved)
  uint32_t filmG = 0;
  void filmDirty(int x0, int y0, int x1, int y1);  // frame pixels; the whole frame: filmDirty(0, 0, 1 << 14, 1 << 14)
  // the state shown
  float Tm = 0, tint[3] = {1, 1, 1}, tintNow[3] = {1, 1, 1}, haze = 0, hazeNow = 0, air = 1, rays = 1;
  bool sleeping = false, lamp = true, night = false, wag = true;
  bool appetite[F_N] = {true, true, true};
  float foodRest = 40;
  bool fingerOn = false;
  float fingerX = 0, fingerY = 0, played = 0;
  // the game listens
  void (*onEat)(int kind, float food) = nullptr;
  void (*onRot)() = nullptr;

  void setView(bool land, float sc);  // the way it is held and the frame's scale
  void setLook(int look);
  void setPlants(const int* ids, const int* slots, int n);
  void setDecor(const int* ids, const int* slots, int n);
  Fish* addFish(int sp, float size, uint32_t uid);
  void removeFish(uint32_t uid);
  void clearFish();
  Fish* find(uint32_t uid);
  void dropFood(float x, int n, int kind, float food);
  void tapGlass(float x, float y);
  void addCoin(float x, float y, float z, int val, float vy = -1);
  int takeCoins(float x, float y, float r);
  float wipe(float x, float y, float r);
  void setAlgae(float level);
  float algaeLevel() const;
  void clearAlgae();
  void sleep();  // the big pictures freed (the dog's pop-up); prepare() makes them again
  float floorY(float z) const;
  float coinY(float z) const;
  Fish* fishAt(float x, float y);
  void step(float dt);
  // the picture: prepare() once a frame (motion to drawing records), then draw() for each piece of it
  void prepare();
  void draw(const Tgt& t, bool rays = true);
  void band(const Fish* f, float* b0, float* b1) const;
  void pick(Fish* f);
  void reoffset(Fish* f);
  long touched = 0;  // sprite pixels drawn (the bench)
  uint32_t prof[8] = {0};
  uint32_t prep[3] = {0};  // us inside prepare(): the background made again (tint, haze, film), the fish, the beams  // us: background, plants+ornaments, fish, food+coins, specks+bubbles, beams, rings+algae, screens
};
extern Scene* sc;  // the one water (the app and the saver share it)
void freeScene();

// A picture at frame pixels (x, y) = its top left, scaled hs x vs, between pixels (water.cpp's plant blit, no sway);
// alpha 0..256; mode 0 as it is, 1 mirrored, 2 its dark shadow (the album's species not kept yet).
void spriteBlit(const Tgt& t, const Spr* s, float x, float y, float hs, float vs, int alpha, int mode);
// One piece of food at frame pixels (x, y), r = its scale in pixels (the water's, or bigger for the chooser), tm the
// clock (the flake turning, the worm wriggling), s its seed, T the light's tint (plain: none), al 0..1.
void foodPiece(const Tgt& t, int kind, float x, float y, float r, float tm, float s, const float* T, float al);
extern const float plain[3];

// ---------------------------------------------------------------- the screens' drawing (gfx.cpp)
namespace gfx {
void begin(float sc);  // the scale the screens are drawn at (the caches are made again when it changes)
void end();
// Drawn at the screen's scale: x, y, w, h in screen units.
void rrect(const Tgt& t, float x, float y, float w, float h, float r, uint16_t c, int a /*0..256*/);
void circle(const Tgt& t, float cx, float cy, float r, uint16_t c, int a);
void ring(const Tgt& t, float cx, float cy, float r, float lw, uint16_t c, int a);
void line(const Tgt& t, float x0, float y0, float x1, float y1, float lw, uint16_t c, int a);
// Glass: a frosted panel with a soft shadow and a white rim (ui.js glass()); made once per size and kept.
void glass(const Tgt& t, float x, float y, float w, float h, float r, int fill /*0..255*/, int a = 256);
void dockButton(const Tgt& t, float cx, float cy, float r, int a);
void dim(const Tgt& t, float x, float y, float w, float h, uint16_t c, int a);  // a flat veil
// Pictures of the packs fitted inside a box (ui.js fit()), at alpha a; shade: as a dark shadow (album).
void fit(const Tgt& t, const Spr* s, float x, float y, float w, float h, int a = 256, bool shade = false);
void blitScaled(const Tgt& t, const Spr* s, float x, float y, float w, float h, int a, bool flip = false);
// Text: one line, vertical middle at y (ui.js text()); align 0 left, 1 centre, 2 right. Returns its width.
float text(const Tgt& t, const char* s, float x, float y, int px, bool bold, uint16_t c, int align = 0, int a = 256);
float textW(const char* s, int px, bool bold);
String fit1(const char* s, int px, bool bold, float w);              // whole items / words only, never "…"
void wrap2(const char* s, int px, bool bold, float w, String& a, String& b);
void frameStart();  // the text cache's clock
uint32_t frameNo();  // which picture this is (frameStart() counts them)
}  // namespace gfx

// ---------------------------------------------------------------- the rules (game.cpp)
struct FishRec {
  uint8_t sp;
  bool sick, fry;
  uint32_t uid;
  float age, next;
};
struct TankRec {
  bool marine = false;
  uint8_t look = 0;
  uint32_t looks = 0;  // owned looks (bits)
  std::vector<FishRec> fish;
  std::vector<uint8_t> plants, plantSlot, decor, decorSlot;
  uint8_t up[NUP] = {0};
  bool heat = true, lamp = true;
  float fed[F_N] = {0}, food = 0, clean = 0, algae = 0, play = 0, breedT = 0, waterAt = -99, idle = 0, age = 0,
        owed = 0, chestT = 0;
  std::vector<int> pile;
  String news;
};
namespace game {
struct Row {  // a shop row (ui.js draws it, game.js G.shop() says it)
  String name, sub, note, tag;
  int art = -1;  // a thumb piece (P_THUMBS), or an icon (artPack)
  PackId artPack = P_THUMBS;
  int up = -1;   // a machine (its glyph)
  int count = 0, price = -1;
  bool bad = false, warn = false, can = false, dim = false, shade = false, look = false;
  bool paid = false;     // locked after the trial (lock.h): a padlock instead of the price, pressing shows the way
  int act = 0, arg = 0;  // what pressing does
};
extern float timeK;     // the game's clock speed (dev)
extern int nightForce;  // -1 the real hours, 0 day, 1 night (dev)
bool start();           // load the save (or a new game), catch up the time away, fill the water
void stop();            // save
void tick(float dt);
void save();
TankRec& T();
TankRec* tankOf(bool marine);
bool hasMarine();
int coins();
int level();
float levelSpan();
int capacity(const TankRec& t);
float used(const TankRec& t);
bool isNight();
bool lampOn(const TankRec& t);
int comfort(const TankRec& t);
int mood(const TankRec& t);
float fishSize(const FishRec& f);
bool isAdult(const FishRec& f);
int diets(const TankRec& t, int* out);  // the kinds of food in use, flake first
const std::vector<String>& reasons(uint32_t uid);
int tasksReady();
float waterWait();
int sellPrice(const FishRec& f);
const FishRec* fishRec(uint32_t uid);
void albumCount(int& have, int& all);
// what the player does
bool feed(int kind);
float wipeAt(float x, float y);
void wipeDone();
bool changeWater();
void setLamp(bool on);
int collect(float x, float y);
void cure(uint32_t uid);
void sellFish(uint32_t uid);
void countTask(const char* id, float n = 1);
void tapped();  // the glass (a daily job)
std::vector<Row> shop(int tab);  // 0 fish, 1 plant, 2 decor, 3 up, 4 look, 5 album, 6 task
void press(const Row& r);
String traits(const Species& s);
const char* roleVi(int role);
// dev
void debugTime(float h);
void debugCoins(int n);
void debugSet(const char* k, const char* v);
void debugXp(int n);
void debugLook(const char* id);
void reset();
void fillFree(const char* look, const char* list);  // a free tank (no rules): the bench and the shots
bool debugKeep(bool restore);  // the save copied aside (tank.qa) / put back and loaded: tests on someone's own tank
}  // namespace game

// ---------------------------------------------------------------- the screens (ui.cpp)
#if TANK_QA
extern uint32_t fishQa[4];  // water.cpp: the fish part: µs in shadows, bodies, spots; body pixels
extern bool qaPlain;  // water.cpp: the fish by rows, the sway per row (the old paths, for shots)
#endif

namespace ui {
struct Lay {
  float W, H;
  bool land;
  float barX, barY, barW, barH;
  float dy, dr, dx[5];
  float shX, shY, shW, shH, listY;
  float cardX, cardY, cardW, cardH;
  float foodX, foodY[3];
};
Lay lay();
extern bool saver;     // the standby face (no bars; the saver page's own clock is the OS's)
extern bool homeLine;  // draw the OS's home line (the picture goes past LVGL, which would draw it)
#if TANK_QA
extern uint32_t prof[6];  // µs in draw(): the bar's layer, the bar's moving parts, the dock, the card, the sheet, the rest
extern bool layersOff;  // the bar, the dock and the card drawn straight, not from their prepared layers (shots)
void layerStats(int& made, int& noRoom, uint32_t& putUs, uint32_t& putPx);  // layers made, no room; layerPut's µs and pixels
#endif
void say(const String& s);
void fx(const char* k, int v);
void wake();
void tick(float dt);
void down(float x, float y);
void move(float x, float y);
void up(float x, float y);
// What covers the water this frame: rows/columns of the screen the water must not be drawn under (opaque sheet).
void cover(float& x0, float& y0, float& x1, float& y1);
void draw(const Tgt& t);
// The water standing still under the shop sheet: frameBegin()/frameEnd() around each picture; while frozen(), a
// piece is drawFrozen() alone (no water worked out or drawn).
void frameBegin();
void frameEnd();
bool frozen();
void drawFrozen(const Tgt& t);
void reset();  // closes the sheet, the card, the chooser
void sleep();  // the screens' cached pictures freed (the dog's pop-up), made again when drawn
void devOpen(const char* what, int arg);
bool busy();   // a sheet, a card, the sponge, the chooser is up
}  // namespace ui

}  // namespace tank
