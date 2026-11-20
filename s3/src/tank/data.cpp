// Hồ cá cảnh: tank.json and the packs of the package (tank_int.h). Cold code: built for size.
#pragma GCC optimize("Os")
#include <ArduinoJson.h>
#include <Arduino.h>

#include <cmath>
#include <cstring>
#include <new>

#include "fonts.h"
#include "tank_int.h"
#include "trace.h"

namespace tank {

Data* Dp = nullptr;
Pack packs[P_N];
String pkgId;

namespace {

bool isLoaded = false;
struct SprCache {
  Spr** s = nullptr;
  int n = 0;
} cache[P_N];

struct Psram : ArduinoJson::Allocator {
  void* allocate(size_t n) override { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM); }
  void deallocate(void* p) override { heap_caps_free(p); }
  void* reallocate(void* p, size_t n) override { return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM); }
} psram;

// strings of the data live as long as the boot (a few KB)
const char* keep(const char* s) {
  if (!s) s = "";
  const size_t n = strlen(s) + 1;
  char* d = (char*)heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
  if (!d) return "";
  memcpy(d, s, n);
  return d;
}

// ---- tank.json into the structs, by tables (one small loop instead of a call per key: flash is short)
enum Ty : uint8_t { T_S, T_I, T_F, T_B, T_L };  // string, int16, float, bool, int
struct Field {
  const char* path;  // "food.per_hour", "every_s.1": keys and indexes
  Ty ty;
  uint16_t off;
};
JsonVariantConst at(JsonVariantConst v, const char* path) {
  char k[32];
  while (*path && !v.isNull()) {
    int n = 0;
    while (*path && *path != '.' && n < 31) k[n++] = *path++;
    k[n] = 0;
    if (*path == '.') path++;
    v = k[0] >= '0' && k[0] <= '9' ? v[atoi(k)] : v[(const char*)k];
  }
  return v;
}
void fill(JsonVariantConst o, void* base, const Field* f, int n) {
  for (int i = 0; i < n; i++) {
    JsonVariantConst v = at(o, f[i].path);
    uint8_t* p = (uint8_t*)base + f[i].off;
    if (v.isNull()) continue;  // keeps the default
    if (f[i].ty == T_S) *(const char**)p = keep(v.as<const char*>());
    else if (f[i].ty == T_I) *(int16_t*)p = (int16_t)lroundf(v.as<float>());
    else if (f[i].ty == T_F) *(float*)p = v.as<float>();
    else if (f[i].ty == T_L) *(int*)p = (int)lroundf(v.as<float>());
    else *(bool*)p = v.as<bool>();
  }
}
#define FS(T, k, m, ty) {k, ty, (uint16_t)offsetof(T, m)}
const Field SPECIES[] = {
    FS(Species, "id", id, T_S), FS(Species, "vi", vi, T_S), FS(Species, "fins", fins, T_B), FS(Species, "school", school, T_B),
    FS(Species, "nip", nip, T_B), FS(Species, "solo", solo, T_B), FS(Species, "inv", inv, T_B), FS(Species, "inv_eater", invEater, T_B),
    FS(Species, "hide", hide, T_B), FS(Species, "lid", lid, T_B), FS(Species, "plant_eater", plantEater, T_B),
    FS(Species, "nip_coral", nipCoral, T_B), FS(Species, "host", host, T_B), FS(Species, "messy", messy, T_B),
    FS(Species, "puffs", puffs, T_B), FS(Species, "fixed", fixed, T_B), FS(Species, "w", w, T_I), FS(Species, "pred", pred, T_I),
    FS(Species, "price", price, T_I), FS(Species, "lv", lv, T_I), FS(Species, "val", val, T_I), FS(Species, "group", group, T_I),
    FS(Species, "clean", clean, T_I), FS(Species, "min_size", minSize, T_I), FS(Species, "mature", mature, T_I),
    FS(Species, "space", space, T_F)};
const Field PLANT[] = {FS(PlantK, "id", id, T_S), FS(PlantK, "vi", vi, T_S), FS(PlantK, "price", price, T_I),
                       FS(PlantK, "lv", lv, T_I), FS(PlantK, "comfort", comfort, T_I)};
const Field DECOR[] = {FS(DecorK, "id", id, T_S), FS(DecorK, "vi", vi, T_S), FS(DecorK, "price", price, T_I), FS(DecorK, "lv", lv, T_I),
                       FS(DecorK, "comfort", comfort, T_I), FS(DecorK, "w", w, T_I), FS(DecorK, "coins", coins, T_B)};
const Field LOOK[] = {FS(Look, "id", id, T_S), FS(Look, "vi", vi, T_S), FS(Look, "price", price, T_I), FS(Look, "lv", lv, T_I)};
const Field RULES[] = {
    FS(Rules, "start.coins", startCoins, T_L), FS(Rules, "start.food", startFood, T_F), FS(Rules, "start.clean", startClean, T_F),
    FS(Rules, "start.algae", startAlgae, T_F), FS(Rules, "xp.feed", xpFeed, T_L), FS(Rules, "xp.wipe", xpWipe, T_L),
    FS(Rules, "xp.water", xpWater, T_L), FS(Rules, "xp.coin", xpCoin, T_L), FS(Rules, "xp.new_species", xpNew, T_L),
    FS(Rules, "xp.birth", xpBirth, T_L), FS(Rules, "food.per_hour", foodHour, T_F), FS(Rules, "food.per_fish_hour", foodFishHour, T_F),
    FS(Rules, "food.pellet_food", pelletFood, T_F), FS(Rules, "food.pinch_wait_s", pinchWait, T_F), FS(Rules, "food.pellets", pellets, T_L),
    FS(Rules, "leftover.rest_s", leftRest, T_F), FS(Rules, "leftover.dirt", leftDirt, T_F), FS(Rules, "clean.per_hour", cleanHour, T_F),
    FS(Rules, "clean.per_space_hour", cleanSpaceHour, T_F), FS(Rules, "clean.messy", messy, T_F),
    FS(Rules, "clean.water_change_to", waterTo, T_F), FS(Rules, "clean.water_wait_h", waterWait, T_F),
    FS(Rules, "algae.per_hour_lamp", algaeLamp, T_F), FS(Rules, "algae.per_hour_dark", algaeDark, T_F),
    FS(Rules, "algae.eater_per_hour", algaeEater, T_F), FS(Rules, "comfort.base", cBase, T_F),
    FS(Rules, "comfort.crowd_from", crowdFrom, T_F), FS(Rules, "comfort.crowd_penalty", crowdPenalty, T_F),
    FS(Rules, "comfort.algae_penalty", algaePenalty, T_F), FS(Rules, "comfort.lamp_at_night", lampNight, T_F),
    FS(Rules, "comfort.play_max", playMax, T_F), FS(Rules, "comfort.play_per_s", playPerS, T_F),
    FS(Rules, "comfort.play_fade_h", playFadeH, T_F), FS(Rules, "mood.happy", moodHappy, T_F), FS(Rules, "mood.low", moodLow, T_F),
    FS(Rules, "coin.every_s.0", coinEvery0, T_F), FS(Rules, "coin.every_s.1", coinEvery1, T_F), FS(Rules, "coin.away_hours", awayHours, T_F),
    FS(Rules, "coin.young", coinYoung, T_F), FS(Rules, "coin.max_lying", maxLying, T_L), FS(Rules, "grow.adult_h", adultH, T_F),
    FS(Rules, "grow.young_scale", youngScale, T_F), FS(Rules, "grow.fry_scale", fryScale, T_F), FS(Rules, "breed.mood", breedMood, T_F),
    FS(Rules, "breed.after_h", breedAfter, T_F), FS(Rules, "breed.rest_h", breedRest, T_F), FS(Rules, "sell.adult", sellAdult, T_F),
    FS(Rules, "sell.young", sellYoung, T_F), FS(Rules, "night.0", night0, T_L), FS(Rules, "night.1", night1, T_L),
    FS(Rules, "diet.vi.flake", dietVi[0], T_S), FS(Rules, "diet.vi.sink", dietVi[1], T_S), FS(Rules, "diet.vi.live", dietVi[2], T_S),
    FS(Rules, "diet.cost.flake", dietCost[0], T_L), FS(Rules, "diet.cost.sink", dietCost[1], T_L), FS(Rules, "diet.cost.live", dietCost[2], T_L),
    FS(Rules, "diet.hungry", hungry, T_F), FS(Rules, "fish.sad_comfort", sadComfort, T_L), FS(Rules, "fish.sad_comfort_max", sadComfortMax, T_L),
    FS(Rules, "fish.dirty", dirty, T_L), FS(Rules, "fish.nip_calm_group", nipCalm, T_L), FS(Rules, "fish.host_comfort", hostComfort, T_L),
    FS(Rules, "fish.afraid_px", afraidPx, T_F), FS(Rules, "hide.plants", hidePlants, T_L), FS(Rules, "sick.clean_below", sickBelow, T_F),
    FS(Rules, "sick.per_hour", sickHour, T_F), FS(Rules, "sick.cure_cost", cureCost, T_L), FS(Rules, "sick.name", sickName, T_S),
    FS(Rules, "salt_cost", saltCost, T_L), FS(Rules, "tasks.a_day", tasksADay, T_L), FS(Rules, "tasks.xp", tasksXp, T_L)};
const char* const STYLE_K[] = {"v", "vmax", "wn", "om", "f0", "f1", "A", "e0", "p", "k", "glide", "drag", "pauseP", "pitch", "lift",
                               "lap", "bob", "wob", "dy", "mea", "pause.0", "pause.1"};
const char* const STYLES[ST_N] = {"cruise", "dart", "hover", "waddle", "long", "floor", "crawl", "puff", "perch", "upright", "drift"};
const char* const ZONES[] = {"mid", "top", "low", "floor"};
const char* const FOODS[] = {"flake", "sink", "live"};
const char* const ROLES[] = {"easy", "school", "cleaner", "show", "rare"};
const char* const UPS[NUP] = {"size", "filter", "light", "air", "feeder", "heater", "lid", "skimmer"};
uint8_t enumOf(JsonVariantConst v, const char* const* names, int n) {
  const char* s = v | "";
  for (int i = 0; i < n; i++)
    if (!strcmp(s, names[i])) return i;
  return 0;
}
bool isMarine(JsonVariantConst o) { return !strcmp(o["sys"] | "", "marine"); }
void geoOf(JsonVariantConst o, JsonVariantConst base, Geo& g) {  // the wide picture's places; what it lacks: the upright's
  float v[1 + 6 * 3 + 3 * 2 + 2 + 6];
  static const char* const K[] = {"floor", "slots", "decor", "air", "water", "light"};
  JsonVariantConst a[6];
  for (int k = 0; k < 6; k++) a[k] = o[K[k]].isNull() ? base[K[k]] : o[K[k]];
  g.floor0 = a[0][0] | 400, g.floor1 = a[0][1] | 470;
  for (int i = 0; i < 6; i++)
    for (int k = 0; k < 3; k++) g.slots[i][k] = a[1][i][k] | 0.0f;
  for (int i = 0; i < 3; i++)
    for (int k = 0; k < 2; k++) g.decor[i][k] = a[2][i][k] | 0.0f;
  for (int i = 0; i < 2; i++) g.air[i] = a[3][i] | 0.0f;
  for (int i = 0; i < 3; i++) g.water[i] = a[4][i] | 0, g.light[i] = a[5][i] | 0;
  (void)v;
}

bool parse(const uint8_t* js, size_t len, String& err) {
  JsonDocument d(&psram);
  if (deserializeJson(d, (const char*)js, len)) return err = "tank.json", false;
  JsonVariantConst sizes = d["sizes"];
  auto aspOf = [&](const char* id) {
    JsonVariantConst a = sizes[id];
    const float w = a[0] | 0.0f;
    return w > 0 ? (a[1] | 0.0f) / w : 1.0f;
  };
  for (int s = 0; s < ST_N; s++) {  // with the powers of the body worked out once
    StyleK& k = D.style[s];
    float* f = &k.v;
    for (int i = 0; i < 22; i++) f[i] = at(d["styles"][STYLES[s]], STYLE_K[i]) | 0.0f;
    for (int i = 0; i <= NSEG; i++) {
      const float u = (float)i / NSEG, um = (i + 0.5f) / NSEG;
      k.env[i] = k.e0 + (1 - k.e0) * powf(u, k.p), k.u13[i] = powf(u, 1.3f);
      if (i < NSEG) k.envM[i] = k.e0 + (1 - k.e0) * powf(um, k.p), k.um13[i] = powf(um, 1.3f);
    }
  }
  D.nsp = D.nplant = D.ndecor = D.nlook = 0;
  for (JsonVariantConst o : d["species"].as<JsonArrayConst>()) {
    if (D.nsp >= NSP_MAX) break;
    Species& s = D.sp[D.nsp++];
    memset(&s, 0, sizeof s);
    s.w = 50, s.lv = 1, s.space = 1, s.pic = s.half = s.thumb = -1;
    fill(o, &s, SPECIES, sizeof SPECIES / sizeof *SPECIES);
    s.marine = isMarine(o), s.role = enumOf(o["role"], ROLES, 5), s.style = enumOf(o["style"], STYLES, ST_N);
    s.zone = enumOf(o["zone"], ZONES, 4), s.food = enumOf(o["food"], FOODS, 3);
    static const char* const EATS[] = {"", "left", "algae"}, *const BREED[] = {"", "easy", "no"}, *const TEMP[] = {"", "warm", "cool"};
    s.eats = enumOf(o["eats"], EATS, 3), s.breed = enumOf(o["breed"], BREED, 3), s.temp = enumOf(o["temp"], TEMP, 3);
    s.asp = aspOf(s.id);
  }
  for (JsonVariantConst o : d["plants"].as<JsonArrayConst>()) {
    if (D.nplant >= NPLANT_MAX) break;
    PlantK& p = D.plant[D.nplant++];
    memset(&p, 0, sizeof p);
    p.lv = 1;
    fill(o, &p, PLANT, sizeof PLANT / sizeof *PLANT);
    p.marine = isMarine(o), p.w = d["plant_w"][p.id] | 46, p.asp = aspOf(p.id);
    JsonVariantConst sw = d["sway"][p.id];
    p.a = sw[0] | 0.0f, p.pw = sw[1] | 1.0f, p.k = sw[2] | 1.0f, p.hz = sw[3] | 0.2f;
  }
  for (JsonVariantConst o : d["decor"].as<JsonArrayConst>()) {
    if (D.ndecor >= NDECOR_MAX) break;
    DecorK& k = D.decor[D.ndecor++];
    memset(&k, 0, sizeof k);
    k.lv = 1, k.w = 40;
    fill(o, &k, DECOR, sizeof DECOR / sizeof *DECOR);
    k.asp = aspOf(k.id);
  }
  for (JsonVariantConst o : d["tanks"].as<JsonArrayConst>()) {
    if (D.nlook >= NLOOK_MAX) break;
    Look& l = D.look[D.nlook++];
    memset(&l, 0, sizeof l);
    l.lv = 1;
    fill(o, &l, LOOK, sizeof LOOK / sizeof *LOOK);
    l.marine = isMarine(o);
    for (int i = 0; i < 3; i++) l.plants0[i] = (int8_t)plantOf(o["plants0"][i] | "");
    geoOf(o, o, l.up);
    geoOf(o["wide"].isNull() ? o : o["wide"], o, l.wide);
    l.nslots = o["slots"].size() ? o["slots"].size() : 6, l.ndecor = o["decor"].size() ? o["decor"].size() : 3;
  }
  static const char* const LV[NUP] = {"space", "dirt", "comfort", "comfort", "floor", "comfort", nullptr, "dirt"};
  for (int u = 0; u < NUP; u++) {
    UpK& k = D.up[u];
    memset(&k, 0, sizeof k);
    JsonVariantConst o;
    for (JsonVariantConst x : d["upgrades"].as<JsonArrayConst>())
      if (!strcmp(x["id"] | "", UPS[u])) o = x;
    k.id = UPS[u], k.vi = keep(o["vi"]), k.what = keep(o["what"]);
    k.ncost = std::min<int>(6, o["cost"].size());
    for (int i = 0; i < 6; i++) {
      k.cost[i] = o["cost"][i] | 0;
      const float d0 = u == U_filter || u == U_skimmer ? 1 : 0;
      k.lv[i] = LV[u] ? (o[LV[u]][i] | (i ? k.lv[i - 1] : d0)) : 0;
      k.algae[i] = o["algae"][i] | 1.0f;
    }
    k.sw = o["switch"] | false, k.marineOnly = isMarine(o);
  }
  JsonVariantConst r = d["rules"];
  Rules& R = D.R;
  fill(r, &R, RULES, sizeof RULES / sizeof *RULES);
  R.nStartFish = 0;
  for (JsonVariantConst f : r["start"]["fish"].as<JsonArrayConst>())
    if (R.nStartFish < 8) R.startFish[R.nStartFish++] = speciesOf(f | "");
  R.nXp = std::min<int>(12, r["xp_at"].size());
  for (int i = 0; i < R.nXp; i++) R.xpAt[i] = r["xp_at"][i] | 0;
  for (int i = 0; i < NDECOR_MAX; i++) R.hideDecor[i] = false;
  for (JsonVariantConst h : r["hide"]["decor"].as<JsonArrayConst>()) {
    const int i = decorOf(h | "");
    if (i >= 0) R.hideDecor[i] = true;
  }
  R.ntasks = 0;
  for (JsonVariantConst t : r["tasks"]["list"].as<JsonArrayConst>()) {
    if (R.ntasks >= 10) break;
    TaskK& q = R.tasks[R.ntasks++];
    q.id = keep(t["id"]), q.vi = keep(t["vi"]), q.n = t["n"] | 1, q.coins = t["coins"] | 0;
  }
  R.marinePrice = d["marine"]["price"] | 800, R.marineLv = d["marine"]["lv"] | 4;
  if (!D.nsp || !D.nlook) return err = "tank.json", false;
  return true;
}

uint16_t rd16(const uint8_t* p) { return p[0] | p[1] << 8; }
uint32_t rd32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

}  // namespace

// RGB565 resampled (sw x sh -> dw x dh): a box average when it shrinks (the 2.8" board's 2/3: as src/scale.h does it),
// between pixels when it grows. Once per look and way of holding.
void resample565(const uint16_t* s, int sw, int sh, uint16_t* d, int dw, int dh) {
  const float kx = (float)sw / dw, ky = (float)sh / dh;
  for (int y = 0; y < dh; y++)
    for (int x = 0; x < dw; x++) {
      float r = 0, g = 0, b = 0, wsum = 0;
      if (kx > 1) {
        const float x0 = x * kx, x1 = x0 + kx, y0 = y * ky, y1 = y0 + ky;
        for (int j = (int)y0; j < (int)ceilf(y1) && j < sh; j++)
          for (int i = (int)x0; i < (int)ceilf(x1) && i < sw; i++) {
            const float w = (fminf(x1, i + 1) - fmaxf(x0, i)) * (fminf(y1, j + 1) - fmaxf(y0, j));
            const uint16_t c = s[j * sw + i];
            r += (c >> 11) * w, g += (c >> 5 & 63) * w, b += (c & 31) * w, wsum += w;
          }
      } else {
        const float fx = fmaxf(0, (x + 0.5f) * kx - 0.5f), fy = fmaxf(0, (y + 0.5f) * ky - 0.5f);
        const int i = std::min((int)fx, sw - 2), j = std::min((int)fy, sh - 2);
        const float ax = fx - i, ay = fy - j;
        for (int q = 0; q < 4; q++) {
          const float w = (q & 1 ? ax : 1 - ax) * (q & 2 ? ay : 1 - ay);
          const uint16_t c = s[(j + (q >> 1)) * sw + i + (q & 1)];
          r += (c >> 11) * w, g += (c >> 5 & 63) * w, b += (c & 31) * w, wsum += w;
        }
      }
      if (wsum <= 0) wsum = 1;
      d[y * dw + x] = (uint16_t)((int)(r / wsum + 0.5f) << 11 | (int)(g / wsum + 0.5f) << 5 | (int)(b / wsum + 0.5f));
    }
}

namespace {

struct FontFile {
  int px;
  bool bold;
  const lv_font_t* f;
};
std::vector<FontFile> fontFiles;

void loadFonts(const char* id) {
  static const int SZ[][2] = {{12, 0}, {16, 0}, {12, 1}, {13, 1}, {14, 1}, {15, 1}, {16, 1}};
  for (auto& z : SZ) {
    char name[24];
    snprintf(name, sizeof name, "font/%c%d.bin", z[1] ? 'b' : 'r', z[0]);
    pkg::File f;
    if (!pkg::open(id, name, f)) continue;
    uint8_t* b = pkg::loadAll(f);  // a PSRAM copy: aligned, and it stays (the font points into it)
    const lv_font_t* lf = b ? fonts::fromFile(b, f.size(), z[0], z[1]) : nullptr;
    if (lf) fontFiles.push_back({z[0], (bool)z[1], lf});
    else heap_caps_free(b);
  }
}

}  // namespace

// ---------------------------------------------------------------- packs
bool Pack::open(const char* id, const char* file) {
  close();
  uint8_t h[8];
  if (!pkg::open(id, file, f) || !f.read(0, h, 8) || memcmp(h, "PBU1", 4)) return f = pkg::File(), false;
  n = rd16(h + 4);
  tab = (uint8_t*)heap_caps_malloc(8 + n * 40, MALLOC_CAP_SPIRAM);
  if (!tab || !f.read(0, tab, 8 + n * 40)) {
    close();
    return false;
  }
  return true;
}
void Pack::close() {
  heap_caps_free(tab);
  tab = nullptr, n = 0, f = pkg::File();
}
int Pack::find(const char* name) const {
  for (int i = 0; i < n; i++)
    if (!strncmp((const char*)tab + 8 + i * 40, name, 24)) return i;
  return -1;
}
void Pack::info(int i, int& w, int& h, int& fmt, uint32_t& off) const {
  const uint8_t* p = tab + 8 + i * 40;
  w = rd16(p + 28), h = rd16(p + 30), fmt = p[32], off = rd32(p + 36);
}

int speciesOf(const char* id) {
  for (int i = 0; i < D.nsp; i++)
    if (!strcmp(D.sp[i].id, id)) return i;
  return -1;
}
int lookOf(const char* id) {
  for (int i = 0; i < D.nlook; i++)
    if (!strcmp(D.look[i].id, id)) return i;
  return -1;
}
int plantOf(const char* id) {
  for (int i = 0; i < D.nplant; i++)
    if (!strcmp(D.plant[i].id, id)) return i;
  return -1;
}
int decorOf(const char* id) {
  for (int i = 0; i < D.ndecor; i++)
    if (!strcmp(D.decor[i].id, id)) return i;
  return -1;
}

const Spr* spr(PackId p, int i) {
  Pack& k = packs[p];
  if (i < 0 || i >= k.n) return nullptr;
  SprCache& c = cache[p];
  if (c.n != k.n) {
    heap_caps_free(c.s);
    c.s = (Spr**)heap_caps_calloc(k.n, sizeof(Spr*), MALLOC_CAP_SPIRAM);
    c.n = c.s ? k.n : 0;
    if (!c.s) return nullptr;
  }
  if (c.s[i]) return c.s[i];
  int w, h, fmt;
  uint32_t off;
  k.info(i, w, h, fmt, off);
  if (w <= 0 || h <= 0 || w > 640 || h > 640) return nullptr;
  const int st = w + 2, n = st * (h + 2);
  // one block: the Spr, the colours, the alphas, the spans (and for the fish, the columns)
  const bool fish = p == P_FRESH || p == P_MARINE;
  const size_t base = (sizeof(Spr) + n * 3 + 4 + (h + w) * 4 + 4 + 3) & ~(size_t)3;
  uint8_t* m = (uint8_t*)heap_caps_malloc(base + (fish ? (size_t)n * 4 : 0), MALLOC_CAP_SPIRAM);
  uint8_t* tmp = (uint8_t*)heap_caps_malloc(w * h * (fmt ? 3 : 2), MALLOC_CAP_SPIRAM);
  if (!m || !tmp || !k.f.read(off, tmp, w * h * (fmt ? 3 : 2))) {
    heap_caps_free(m), heap_caps_free(tmp);
    return nullptr;
  }
  Spr* s = (Spr*)m;
  uint16_t* C = (uint16_t*)(m + sizeof(Spr));
  uint8_t* A = (uint8_t*)(C + n);
  for (int y = -1; y <= h; y++)
    for (int x = -1; x <= w; x++) {
      const int sx = x < 0 ? 0 : x >= w ? w - 1 : x, sy = y < 0 ? 0 : y >= h ? h - 1 : y, o = (y + 1) * st + x + 1;
      const bool in = sx == x && sy == y;
      C[o] = rd16(tmp + 2 * (sy * w + sx));  // the edge colour spread into the clear border
      A[o] = !in ? 0 : fmt ? tmp[2 * w * h + sy * w + sx] : 255;
    }
  heap_caps_free(tmp);
  // each row's span of colour (the blits skip the clear sides of a plant's leaves: the board reads PSRAM slowly)
  uint16_t* RS = (uint16_t*)(((uintptr_t)(A + n) + 3) & ~(uintptr_t)3);
  for (int y = 0; y < h; y++) {
    const uint8_t* row = A + (y + 1) * st + 1;
    int lo = 0, hi = w - 1;
    while (lo < w && !row[lo]) lo++;
    while (hi >= lo && !row[hi]) hi--;
    RS[2 * y] = (uint16_t)lo, RS[2 * y + 1] = (uint16_t)(hi < lo ? 0 : hi);
    if (hi < lo) RS[2 * y] = (uint16_t)w;
  }
  uint16_t* CS = RS + 2 * h;  // and each column's
  for (int x = 0; x < w; x++) {
    int lo = 0, hi = h - 1;
    while (lo < h && !A[(lo + 1) * st + x + 1]) lo++;
    while (hi >= lo && !A[(hi + 1) * st + x + 1]) hi--;
    CS[2 * x] = (uint16_t)(hi < lo ? h : lo), CS[2 * x + 1] = (uint16_t)(hi < lo ? 0 : hi);
  }
  s->w = w, s->h = h, s->st = st, s->c = C + st + 1, s->a = A + st + 1, s->rs = RS, s->cs = CS, s->t = nullptr, s->th = 0;
  if (fish) {
    const int th = h + 2;
    uint32_t* T = (uint32_t*)(m + base);
    for (int x = 0; x < st; x++)
      for (int y = 0; y < th; y++) T[x * th + y] = C[y * st + x] | (uint32_t)A[y * st + x] << 16;
    s->t = T + th + 1, s->th = (int16_t)th;
  }
  return c.s[i] = s;
}

const Spr* sprNamed(PackId p, const char* name) { return spr(p, packs[p].find(name)); }

void sprTrim(bool keepThumbs) {
  for (int p = 0; p < P_N; p++) {
    if (p == P_THUMBS && keepThumbs) continue;
    if (p != P_THUMBS) continue;  // the water's pictures stay (the tank changes them seldom)
    for (int i = 0; i < cache[p].n; i++) heap_caps_free(cache[p].s[i]), cache[p].s[i] = nullptr;
  }
}

bool background(const char* look, bool land, int fw, int fh, uint16_t* dst) {
  // the pack drawn for this frame's size; else the other board's, resampled once
  const char* want[2] = {fw == 320 && fh == 480 ? "tank_bg.pbu" : fw == 480 ? "tank_bg_wide.pbu" : land ? "tank_bg_28_wide.pbu" : "tank_bg_28.pbu",
                         fw == 320 && fh == 480 ? "tank_bg_28.pbu" : fw == 480 ? "tank_bg_28_wide.pbu" : land ? "tank_bg_wide.pbu" : "tank_bg.pbu"};
  for (const char* file : want) {
    Pack p;
    if (!p.open(pkgId.c_str(), file)) continue;
    const int i = p.find(look);
    int w, h, fmt;
    uint32_t off;
    if (i < 0) continue;
    p.info(i, w, h, fmt, off);
    bool ok;
    if (w == fw && h == fh) {
      ok = p.f.read(off, dst, w * h * 2);
    } else {
      uint16_t* t = (uint16_t*)heap_caps_malloc(w * h * 2, MALLOC_CAP_SPIRAM);
      ok = t && p.f.read(off, t, w * h * 2);
      if (ok) resample565(t, w, h, dst, fw, fh);
      heap_caps_free(t);
      trace::add("tank: %s %dx%d from %s %dx%d", look, fw, fh, file, w, h);
    }
    p.close();
    if (ok) return true;
  }
  return false;
}

const lv_font_t* font(int px, bool bold) {
  for (const FontFile& f : fontFiles)
    if (f.px == px && f.bold == bold) return f.f;
  return fonts::text(px, bold);
}

bool loaded() { return isLoaded; }

bool load(const char* id, String& err) {
  if (isLoaded && pkgId == id) return true;
  if (!Dp) {
    void* m = heap_caps_calloc(1, sizeof(Data), MALLOC_CAP_SPIRAM);
    if (!m) return err = "memory", false;
    Dp = new (m) Data();
  }
  pkgId = id;
  pkg::File f;
  uint8_t* js = nullptr;
  if (!pkg::open(id, "tank.json", f) || !(js = pkg::loadAll(f))) return err = "tank.json", false;
  const bool ok = parse(js, f.size(), err);
  heap_caps_free(js);
  if (!ok) return false;
  static const char* const FILES[P_N] = {"tank_fish_fresh.pbu", "tank_fish_marine.pbu", "tank_things.pbu", "tank_thumbs.pbu", "tank_ui.pbu"};
  for (int p = 0; p < P_N; p++)
    if (!packs[p].open(id, FILES[p]) && p != P_UI) return err = FILES[p], false;
  for (int i = 0; i < D.nsp; i++) {
    Species& s = D.sp[i];
    Pack& k = packs[s.marine ? P_MARINE : P_FRESH];
    char hn[32];
    snprintf(hn, sizeof hn, "%s.h", s.id);
    s.pic = k.find(s.id), s.half = k.find(hn);
    snprintf(hn, sizeof hn, "t_%s", s.id);
    s.thumb = packs[P_THUMBS].find(hn);
  }
  loadFonts(id);
  isLoaded = true;
  trace::add("tank: data %d species, %d looks; packs %d/%d/%d/%d pieces, %d fonts", D.nsp, D.nlook, packs[0].n,
             packs[1].n, packs[2].n, packs[3].n, (int)fontFiles.size());
  return true;
}

}  // namespace tank
