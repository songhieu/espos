// Hồ cá cảnh: the keeper's rules, carried over from the hand-off's ref/game.js (every number from tank.json, every
// sentence as the page says it). Two tanks: freshwater, and once bought, saltwater; a fish lives ONLY in its own
// water; three gauges; coins from content fish; levels; the shop's rows. Fish never die. Time is real time (hours);
// away time (up to 72 h) is caught up a quarter of an hour at a time when the game opens.
// The rules step 10 times a second (the page steps them every picture: the same sums, hour-scale).
// Saved as the page's game (newGame() / newTank()) in a small binary file on LittleFS, /tank.sav (wear: once a minute
// and on what matters, not every 5 s as the page does in its browser).
#pragma GCC optimize("Os")
#include <Arduino.h>
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <ctime>

#include "tank_int.h"
#include "trace.h"
#include "../lock.h"
#include "i18n.h"

#ifdef SIM
#include <string>
extern std::string simDir;
#endif

namespace tank {
namespace game {

float timeK = 1;
int nightForce = -1;

namespace {

#define R (D.R)  // (not a reference made at static init: D is made later)
struct State {
  int coins = 0, xp = 0;
  bool cur = false;  // marine in front
  uint64_t album = 0;
  double last = 0, clock = 0;  // ms since 1970; game hours
  uint32_t uid = 1;
  String taskDay;
  struct Task {
    int k;
    float got;
    bool paid;
  } tasks[4];
  int ntasks = 0;
  TankRec fresh, marine;
  bool hasMarine = false;
} st;

struct Census {
  uint8_t count[NSP_MAX];
  bool warm, hide, anemone;
  uint8_t hunters[NSP_MAX], invEaters[NSP_MAX], nippers[NSP_MAX], grazers[NSP_MAX], ids[NSP_MAX];
  int nh = 0, ni = 0, nn = 0, ng = 0, nids = 0;
} C;  // the tank in front, counted again every second
std::vector<std::pair<uint32_t, std::vector<String>>> bad;  // each fish's reasons (the card)
uint32_t fedAt = 0;
bool fedOnce = false;
float wiped = 0, cenT = 0, saveT = 0, stepAcc = 0;

uint32_t rs = 88172645u;
float rnd(float a, float b) {
  rs ^= rs << 13, rs ^= rs >> 17, rs ^= rs << 5;
  return a + (b - a) * (float)(rs >> 8) / 16777216.0f;
}
template <class T>
T clampv(T v, T a, T b) { return v < a ? a : v > b ? b : v; }

// ---- Vietnamese capitals for the first letter (the page's toLowerCase / toUpperCase on a sentence)
uint32_t caseOf(uint32_t c, bool up) {
  if (c < 128) return up ? toupper(c) : tolower(c);
  if ((c >= 0xC0 && c <= 0xDE && c != 0xD7) || (c >= 0xE0 && c <= 0xFE && c != 0xF7)) return up ? (c & ~0x20u) : (c | 0x20u);
  if ((c >= 0x100 && c <= 0x17F) || (c >= 0x1EA0 && c <= 0x1EF9)) return up ? (c & ~1u) : (c | 1u);
  if (c == 0x1A0 || c == 0x1A1) return up ? 0x1A0 : 0x1A1;
  if (c == 0x1AF || c == 0x1B0) return up ? 0x1AF : 0x1B0;
  return c;
}
String utf8(uint32_t c) {
  char b[5] = {0};
  if (c < 0x80) b[0] = c;
  else if (c < 0x800) b[0] = 0xC0 | c >> 6, b[1] = 0x80 | (c & 63);
  else b[0] = 0xE0 | c >> 12, b[1] = 0x80 | (c >> 6 & 63), b[2] = 0x80 | (c & 63);
  return b;
}
String recase(const String& s, bool up, bool all) {
  String o;
  for (unsigned i = 0; i < s.length();) {
    const uint8_t c = s[i];
    const int n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
    uint32_t v = n == 1 ? c : n == 2 ? c & 0x1F : n == 3 ? c & 0x0F : c & 7;
    for (int k = 1; k < n && i + k < s.length(); k++) v = v << 6 | (s[i + k] & 63);
    o += (all || i == 0) ? utf8(caseOf(v, up)) : s.substring(i, i + n);
    i += n;
  }
  return o;
}
String lowerFirst(const String& s) { return recase(s, false, false); }
String fmt(const char* f, ...) {  // one sentence of the page, made in one go (String sums cost flash)
  char b[200];
  va_list a;
  va_start(a, f);
  vsnprintf(b, sizeof b, f, a);
  va_end(a);
  return b;
}
String num(float v) {  // as the page prints a number: 3, 3.5
  char b[16];
  if (fabsf(v - lroundf(v)) < 0.001f) snprintf(b, sizeof b, "%ld", lroundf(v));
  else snprintf(b, sizeof b, "%.1f", v);
  return b;
}

void say(const String& s) { ui::say(s); }
void fx(const char* k, int v = 0) { ui::fx(k, v); }

double nowMs() {
  const time_t t = time(nullptr);
  return t > 1600000000 ? (double)t * 1000 : 0;  // 0: the clock is not set yet (no network since the start)
}
int hourNow() {
  const time_t t = time(nullptr);
  struct tm tm;
  localtime_r(&t, &tm);
  return tm.tm_hour;
}
String today() {
  const time_t t = time(nullptr);
  struct tm tm;
  localtime_r(&t, &tm);
  char b[16];
  snprintf(b, sizeof b, "%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
  return b;
}

const Species& SP(int i) { return D.sp[i]; }
bool feeds(int sp) { return SP(sp).eats != 2; }  // grazers feed themselves
int dietOf(int sp) { return SP(sp).food; }

void newTank(TankRec& t, bool marine) {
  t = TankRec();
  t.marine = marine;
  for (int i = 0; i < D.nlook; i++)
    if (D.look[i].marine == marine && D.look[i].price == 0) {
      t.look = i, t.looks = 1u << i;
      break;
    }
  for (int k = 0; k < 3; k++)
    if (D.look[t.look].plants0[k] >= 0) t.plants.push_back(D.look[t.look].plants0[k]), t.plantSlot.push_back(k);
  for (int k = 0; k < F_N; k++) t.fed[k] = R.startFood;
  t.food = R.startFood, t.clean = R.startClean, t.algae = R.startAlgae, t.waterAt = -99;
}
void newGame() {
  st = State();
  st.coins = R.startCoins, st.last = nowMs();
  newTank(st.fresh, false);
  for (int i = 0; i < R.nStartFish; i++) {
    const int sp = R.startFish[i];
    if (sp < 0) continue;
    st.fresh.fish.push_back({(uint8_t)sp, false, false, st.uid++, R.adultH, rnd(60, 200)});
    st.album |= 1ull << sp;
  }
}

// ---------------------------------------------------------------- the free part (lock.h, 2026-11-19)
// After the trial, without "vn.pawboop.tank+full": the catalog's free_ids say what stays (the planted look and six
// starter fish). Nothing in the save changes: fish of a locked kind live on (only buying more is closed), and a
// locked look or the sea tank is shown as the free look / the fresh tank until it opens again, exactly as it was.
struct Gate {
  uint32_t ver = 0;
  bool full = true, marine = true;
  uint64_t sp = ~0ull;
  uint32_t looks = ~0u;
} gate_;
const Gate& gate() {
  if (gate_.ver == lock::version()) return gate_;
  gate_.ver = lock::version();
  gate_.full = lock::may("vn.pawboop.tank+full");
  auto ok = [](const char* id) { return gate_.full || lock::may((String("vn.pawboop.tank+") + id).c_str()); };
  gate_.marine = ok("marine");
  gate_.sp = 0, gate_.looks = 0;
  for (int i = 0; i < D.nsp && i < 64; i++) gate_.sp |= (uint64_t)ok(SP(i).id) << i;
  for (int i = 0; i < D.nlook && i < 32; i++) gate_.looks |= (uint32_t)ok(D.look[i].id) << i;
  return gate_;
}
bool spFree(int sp) { return gate().sp >> sp & 1; }
bool lookFree(int k) { return gate().looks >> k & 1; }
int lookShown(const TankRec& t) {
  if (lookFree(t.look)) return t.look;
  for (int i = 0; i < D.nlook; i++)
    if (D.look[i].marine == t.marine && D.look[i].price == 0) return i;
  return t.look;
}
void lockSheet(const char* what) { lock::sheet(T_(what), lock::Look::Tank); }

TankRec& cur() { return st.cur && st.hasMarine && gate().marine ? st.marine : st.fresh; }
bool curMarine() { return &cur() == &st.marine; }

// ---------------------------------------------------------------- what a tank holds, counted once
void census(const TankRec& t, Census& c) {
  memset(c.count, 0, sizeof c.count);
  c.nh = c.ni = c.nn = c.ng = c.nids = 0;
  for (const FishRec& f : t.fish)
    if (!c.count[f.sp]++) c.ids[c.nids++] = f.sp;
  c.warm = t.up[U_heater] > 0 && t.heat;
  c.hide = (int)t.plants.size() >= R.hidePlants;
  for (uint8_t d : t.decor) c.hide |= R.hideDecor[d];
  c.anemone = false;
  for (uint8_t p : t.plants) c.anemone |= !strcmp(D.plant[p].id, "anemone");
  for (int i = 0; i < c.nids; i++) {
    const Species& s = SP(c.ids[i]);
    if (s.pred) c.hunters[c.nh++] = c.ids[i];
    if (s.invEater) c.invEaters[c.ni++] = c.ids[i];
    if (s.nip && !(s.group && c.count[c.ids[i]] >= R.nipCalm)) c.nippers[c.nn++] = c.ids[i];
    if (s.plantEater || s.nipCoral) c.grazers[c.ng++] = c.ids[i];
  }
}
int fears(int sp, const Census& c) {
  const Species& s = SP(sp);
  if (!(s.fixed || !strcmp(s.id, "snail")))
    for (int i = 0; i < c.nh; i++)
      if (c.hunters[i] != sp && s.w <= SP(c.hunters[i]).pred && !s.pred) return c.hunters[i];
  if (s.inv)
    for (int i = 0; i < c.ni; i++)
      if (c.invEaters[i] != sp) return c.invEaters[i];
  return -1;
}
bool warm(const TankRec& t) { return t.up[U_heater] > 0 && t.heat; }

// why this fish is unhappy (empty = content); text = false: only whether
int reasons(const TankRec& t, const FishRec& d, const Census& c, std::vector<String>* out) {
  const Species& s = SP(d.sp);
  int n = 0;
  auto add = [&](const String& w) {
    n++;
    if (out) out->push_back(w);
  };
  if (d.sick) add(fmt("Bệnh %s", R.sickName));
  if (feeds(d.sp) && t.fed[dietOf(d.sp)] < R.hungry) add(fmt("Đói, cần %s", recase(R.dietVi[dietOf(d.sp)], false, true).c_str()));
  if (t.clean < R.dirty) add("Nước dơ");
  else if (s.clean && t.clean < s.clean) add("Cần nước sạch hơn");
  if (s.group && c.count[d.sp] < s.group) add(fmt("Thiếu đàn, cần %d con", s.group));
  if (s.temp == 1 && !c.warm) add("Cần nước ấm");
  else if (s.temp == 2 && c.warm) add("Nước quá ấm");
  if (s.hide && !c.hide) add("Thiếu chỗ trốn");
  const int h = fears(d.sp, c);
  if (h >= 0) add(fmt("Sợ %s", SP(h).vi));
  if (s.fins)
    for (int i = 0; i < c.nn; i++)
      if (c.nippers[i] != d.sp) {
        add(fmt("Bị %s cắn vây", SP(c.nippers[i]).vi));
        break;
      }
  return n;
}

float levelOf(const UpK& u, int lvl) { return u.lv[clampv(lvl, 0, 5)]; }
int capOf(const TankRec& t) { return (int)levelOf(D.up[U_size], t.up[U_size]); }
float usedOf(const TankRec& t) {
  float u = 0;
  for (const FishRec& f : t.fish) u += SP(f.sp).space;
  return u;
}
bool nightNow() { return nightForce >= 0 ? nightForce : hourNow() >= R.night0 || hourNow() < R.night1; }
bool lamp(const TankRec& t) { return t.up[U_light] >= 3 ? !nightNow() : t.lamp; }

int comfortOf(const TankRec& t, const Census& c) {
  float v = R.cBase + levelOf(D.up[U_light], t.up[U_light]) + levelOf(D.up[U_air], t.up[U_air]);
  if (!c.ng)  // uprooted plants and nibbled corals comfort nobody
    for (uint8_t p : t.plants) v += D.plant[p].comfort;
  for (uint8_t d : t.decor) v += D.decor[d].comfort;
  const int clown = speciesOf("clown");
  if (c.anemone && clown >= 0 && c.count[clown]) v += R.hostComfort;
  const float full = usedOf(t) / std::max(1, capOf(t));
  if (full > R.crowdFrom) v -= R.crowdPenalty * clampv((full - R.crowdFrom) / (1 - R.crowdFrom), 0.0f, 1.0f);
  v -= t.algae * R.algaePenalty;
  if (nightNow() && lamp(t)) v -= R.lampNight;
  v += t.play;
  int sad = 0;
  for (const FishRec& f : t.fish) sad += reasons(t, f, c, nullptr) > 0;
  v -= std::min(R.sadComfortMax, sad * R.sadComfort);
  return clampv((int)lroundf(v), 0, 100);
}
int moodOf(const TankRec& t, const Census& c) { return (int)lroundf(0.4f * t.food + 0.3f * t.clean + 0.3f * comfortOf(t, c)); }

int levelNow() {
  int l = 1;
  for (int i = 0; i < R.nXp; i++)
    if (st.xp >= R.xpAt[i]) l = i + 1;
  return l;
}
void addXp(int n) {
  const int before = levelNow();
  st.xp += n;
  const int now = levelNow();
  if (now > before) say(fmt("Lên cấp %d!", now)), fx("level");
}
bool pay(int n) {
  if (st.coins < n) return say("Chưa đủ xu"), false;
  st.coins -= n;
  return true;
}

// ---------------------------------------------------------------- the scene follows the state
void spawn(const FishRec& d, bool baby) {
  if (!sc) return;
  sc->addFish(d.sp, baby ? R.fryScale * 0.6f : fishSize(d), d.uid);
}
void sceneCensus() {
  TankRec& t = cur();
  census(t, C);
  bad.clear();
  for (const FishRec& d : t.fish) {
    std::vector<String> w;
    reasons(t, d, C, &w);
    bad.push_back({d.uid, w});
  }
  if (!sc) return;
  int anemone = -1;
  for (int i = 0; i < sc->nplants; i++)
    if (!strcmp(D.plant[sc->plants[i].k].id, "anemone")) anemone = i;
  for (int i = 0; i < sc->nfish; i++) {
    Fish* f = &sc->fish[i];
    const FishRec* d = fishRec(f->uid);
    if (!d) continue;
    const Species& s = SP(d->sp);
    f->sick = d->sick;
    f->sad = false;
    for (auto& b : bad)
      if (b.first == d->uid) f->sad = !b.second.empty();
    f->nafraid = 0;
    if (fears(d->sp, C) >= 0)
      for (int j = 0; j < sc->nfish && f->nafraid < 8; j++) {
        const Fish* o = &sc->fish[j];
        if (o == f) continue;
        const Species& os = *o->S;
        if ((s.inv && os.invEater) || (os.pred && s.w <= os.pred && !s.pred)) f->afraid[f->nafraid++] = o->uid;
      }
    f->host = s.host && C.anemone ? anemone : -1;
  }
}
void light() {
  if (!sc) return;
  TankRec& t = cur();
  const bool night = nightNow(), lp = lamp(t);
  sc->night = night, sc->lamp = lp, sc->sleeping = (night && !lp) || moodOf(t, C) < R.moodLow;
  static const float TINT[4][3] = {{1, 1, 1}, {0.6f, 0.65f, 0.72f}, {0.92f, 0.92f, 0.97f}, {0.24f, 0.31f, 0.5f}};
  const float* k = TINT[(night ? 2 : 0) + (lp ? 0 : 1)];
  for (int i = 0; i < 3; i++) sc->tint[i] = k[i];
  sc->rays = lp ? 1 + 0.25f * t.up[U_light] : 0;
  static const float AIR[] = {0.3f, 1, 1.9f};
  sc->air = AIR[clampv((int)t.up[U_air], 0, 2)];
}
void sync(bool all) {
  if (!sc) return;
  TankRec& t = cur();
  if (all) sc->clearFish(), sc->nfood = 0, sc->ncoins = 0, sc->clearAlgae();
  if (sc->look != lookShown(t) || all || !sc->geo) sc->setLook(lookShown(t));
  std::vector<int> ids(t.plants.begin(), t.plants.end()), sl(t.plantSlot.begin(), t.plantSlot.end());
  sc->setPlants(ids.data(), sl.data(), ids.size());
  std::vector<int> dd(t.decor.begin(), t.decor.end()), ds(t.decorSlot.begin(), t.decorSlot.end());
  sc->setDecor(dd.data(), ds.data(), dd.size());
  if (all) {
    for (const FishRec& d : t.fish) spawn(d, false);
    sc->setAlgae(t.algae);
    for (int v : t.pile) {
      const float z = rnd(20, 80);
      sc->addCoin(rnd(30, sc->VW - 30), 0, z, v, 0);
      CoinI& c = sc->coins[sc->ncoins - 1];
      c.y = sc->coinY(z), c.rest = true, c.t = rnd(0, 9);
    }
    t.pile.clear();
    sc->hazeNow = sc->haze = powf(clampv(1 - t.clean / 100, 0.0f, 1.0f), 1.4f) * 0.95f;
  }
  sceneCensus();
  light();
  if (all)
    for (int i = 0; i < 3; i++) sc->tintNow[i] = sc->tint[i];
}

// ---------------------------------------------------------------- time
void birth(TankRec& t, bool live, const std::vector<int>& kinds) {
  std::vector<int> able, easy;
  for (int k : kinds)
    if (usedOf(t) + SP(k).space <= capOf(t)) able.push_back(k);
  if (able.empty()) return;
  for (int k : able)
    if (SP(k).breed == 1) easy.push_back(k);
  const std::vector<int>& pool = easy.size() ? easy : able;
  const int id = pool[(int)rnd(0, pool.size() - 0.001f)];
  FishRec f = {(uint8_t)id, false, true, st.uid++, 0, rnd(R.coinEvery0, R.coinEvery1)};
  t.fish.push_back(f);
  addXp(R.xpBirth);
  if (live && &t == &cur()) {
    spawn(f, true);
    say(fmt("Bể có cá con: %s!", SP(id).vi)), fx("birth");
    sceneCensus();
  } else t.news = fmt("Lúc bạn vắng, bể có cá con: %s", SP(id).vi);
}
void advance(TankRec& t, float h, bool live) {
  if (h <= 0) return;
  const bool lp = lamp(t);
  const float floor_ = levelOf(D.up[U_feeder], t.up[U_feeder]);
  t.age += h;
  float low = 100;
  bool any = false;
  for (int k = 0; k < F_N; k++) {
    float sp = 0;
    for (const FishRec& f : t.fish)
      if (feeds(f.sp) && dietOf(f.sp) == k) sp += SP(f.sp).space;
    if (!sp) continue;
    any = true;
    t.fed[k] = std::max(t.fed[k] - (R.foodHour + R.foodFishHour * sp) * h, std::min(t.fed[k], floor_));
    low = std::min(low, t.fed[k]);
  }
  t.food = any ? low : 100;
  float dirt = R.cleanHour;
  for (const FishRec& f : t.fish) dirt += R.cleanSpaceHour * SP(f.sp).space * (SP(f.sp).messy ? R.messy : 1);
  dirt *= levelOf(D.up[U_filter], t.up[U_filter]) * (t.marine ? levelOf(D.up[U_skimmer], t.up[U_skimmer]) : 1);
  t.clean = clampv(t.clean - dirt * h, 0.0f, 100.0f);
  int eaters = 0;
  for (const FishRec& f : t.fish) eaters += SP(f.sp).eats == 2;
  t.algae = clampv(t.algae + ((lp ? R.algaeLamp * D.up[U_light].algae[t.up[U_light]] : R.algaeDark) - eaters * R.algaeEater) * h, 0.0f, 100.0f);
  t.play = std::max(0.0f, t.play - R.playMax / R.playFadeH * h);
  if (t.clean < R.sickBelow && t.fish.size() && rnd(0, 1) < R.sickHour * h) {
    std::vector<FishRec*> well;
    for (FishRec& f : t.fish)
      if (!f.sick) well.push_back(&f);
    if (well.size()) {
      FishRec* f = well[(int)rnd(0, well.size() - 0.001f)];
      f->sick = true;
      if (live && &t == &cur()) say(fmt("%s bị %s vì nước dơ", SP(f->sp).vi, R.sickName)), sceneCensus();
    }
  }
  Census c;
  census(t, c);
  const int mood = moodOf(t, c);
  const bool happy = mood >= R.moodHappy;
  float coins = 0;
  bool easy = false;
  const float mean = (R.coinEvery0 + R.coinEvery1) / 2 / 3600;
  uint8_t adults[NSP_MAX] = {0};
  for (FishRec& f : t.fish) {
    if (!(happy && !reasons(t, f, c, nullptr))) continue;
    f.age += h;
    coins += SP(f.sp).val * (isAdult(f) ? 1 : R.coinYoung) * h / mean;
    if (isAdult(f) && SP(f.sp).breed != 2 && !SP(f.sp).solo) {
      if (++adults[f.sp] >= 2 && SP(f.sp).breed == 1) easy = true;
    }
  }
  t.idle += h;  // hours since the coins were last picked up
  if (!live && t.idle < R.awayHours + h) {  // away: the coins that fell meanwhile lie in a few piles
    t.owed += coins;
    int sum = (int)floorf(t.owed);
    t.owed -= sum;
    while (sum > 0) {
      if ((int)t.pile.size() < R.maxLying) {
        const int v = std::max(1, (int)ceilf((float)sum / (R.maxLying - t.pile.size())));
        t.pile.push_back(v), sum -= v;
      } else t.pile[(int)rnd(0, t.pile.size() - 0.001f)] += sum, sum = 0;
    }
  }
  if (mood >= R.breedMood) {
    t.breedT += h * (easy ? 2.5f : 1);
    if (t.breedT >= R.breedAfter) {
      t.breedT = -R.breedRest;
      std::vector<int> kinds;
      for (int i = 0; i < D.nsp; i++)
        if (adults[i] >= 2) kinds.push_back(i);
      birth(t, live, kinds);
    }
  }
}
void away(TankRec& t, float h) {
  for (float left = h; left > 0.0001f; left -= 0.25f) advance(t, std::min(0.25f, left), false);
}

// ---------------------------------------------------------------- the save
String path() {
#ifdef SIM
  return String(simDir.c_str()) + "/fs/tank.sav";
#else
  return "/littlefs/tank.sav";
#endif
}
// The save: newGame() / newTank() of the page, as a small binary record (ids as words, so a new tank.json never
// mixes species up). "TNK1", then the game, then each tank.
struct Out {
  String b;
  void raw(const void* p, int n) {
    for (int i = 0; i < n; i++) b += (char)((const uint8_t*)p)[i];
  }
  template <class T>
  void put(T v) { raw(&v, sizeof v); }
  void str(const char* s) {
    const uint8_t n = strlen(s);
    put(n), raw(s, n);
  }
};
struct In {
  const uint8_t *p, *end;
  bool ok = true;
  void raw(void* d, int n) {
    if (p + n > end) ok = false, n = 0;
    memcpy(d, p, n), p += n;
  }
  template <class T>
  T get() {
    T v{};
    raw(&v, sizeof v);
    return v;
  }
  String str() {
    const uint8_t n = get<uint8_t>();
    String s;
    for (int i = 0; i < n && p < end; i++) s += (char)*p++;
    return s;
  }
};
void putTank(Out& o, const TankRec& t) {
  o.str(D.look[t.look].id);
  uint8_t n = 0;
  for (int i = 0; i < D.nlook; i++) n += t.looks >> i & 1;
  o.put(n);
  for (int i = 0; i < D.nlook; i++)
    if (t.looks >> i & 1) o.str(D.look[i].id);
  o.put((uint8_t)t.fish.size());
  for (const FishRec& f : t.fish) o.str(SP(f.sp).id), o.put((uint8_t)(f.sick | f.fry << 1)), o.put(f.uid), o.put(f.age), o.put(f.next);
  o.put((uint8_t)t.plants.size());
  for (size_t i = 0; i < t.plants.size(); i++) o.str(D.plant[t.plants[i]].id), o.put(t.plantSlot[i]);
  o.put((uint8_t)t.decor.size());
  for (size_t i = 0; i < t.decor.size(); i++) o.str(D.decor[t.decor[i]].id), o.put(t.decorSlot[i]);
  o.raw(t.up, NUP), o.put((uint8_t)(t.heat | t.lamp << 1)), o.raw(t.fed, sizeof t.fed);
  const float v[] = {t.food, t.clean, t.algae, t.play, t.breedT, t.waterAt, t.idle, t.age, t.owed, t.chestT};
  o.raw(v, sizeof v);
  o.put((uint8_t)std::min<size_t>(t.pile.size(), 255));
  for (size_t i = 0; i < t.pile.size() && i < 255; i++) o.put((int32_t)t.pile[i]);
  o.str(t.news.c_str());
}
void getTank(In& in, TankRec& t, bool marine) {
  newTank(t, marine);
  const int l = lookOf(in.str().c_str());
  if (l >= 0 && D.look[l].marine == marine) t.look = l;
  t.looks = 1u << t.look;
  for (int n = in.get<uint8_t>(); n > 0; n--) {
    const int k = lookOf(in.str().c_str());
    if (k >= 0 && D.look[k].marine == marine) t.looks |= 1u << k;
  }
  t.fish.clear(), t.plants.clear(), t.plantSlot.clear();
  for (int n = in.get<uint8_t>(); n > 0; n--) {
    const int sp = speciesOf(in.str().c_str());
    const uint8_t fl = in.get<uint8_t>();
    const uint32_t uid = in.get<uint32_t>();
    const float age = in.get<float>(), next = in.get<float>();
    if (sp >= 0 && SP(sp).marine == marine) t.fish.push_back({(uint8_t)sp, (bool)(fl & 1), (bool)(fl & 2), uid, age, next});
  }
  for (int n = in.get<uint8_t>(); n > 0; n--) {
    const int p = plantOf(in.str().c_str());
    const uint8_t slot = in.get<uint8_t>();
    if (p >= 0) t.plants.push_back(p), t.plantSlot.push_back(slot);
  }
  for (int n = in.get<uint8_t>(); n > 0; n--) {
    const int k = decorOf(in.str().c_str());
    const uint8_t slot = in.get<uint8_t>();
    if (k >= 0) t.decor.push_back(k), t.decorSlot.push_back(slot);
  }
  in.raw(t.up, NUP);
  for (int i = 0; i < NUP; i++) t.up[i] = std::min<int>(t.up[i], D.up[i].ncost);
  const uint8_t hl = in.get<uint8_t>();
  t.heat = hl & 1, t.lamp = hl >> 1 & 1;
  in.raw(t.fed, sizeof t.fed);
  float v[10];
  in.raw(v, sizeof v);
  t.food = v[0], t.clean = v[1], t.algae = v[2], t.play = v[3], t.breedT = v[4], t.waterAt = v[5], t.idle = v[6], t.age = v[7];
  t.owed = v[8], t.chestT = v[9];
  for (int n = in.get<uint8_t>(); n > 0; n--) t.pile.push_back(in.get<int32_t>());
  t.news = in.str();
}
bool loadSave() {
  const String p = path();
  const int fd = ::open(p.c_str(), O_RDONLY);
  if (fd < 0) return false;
  const off_t n = lseek(fd, 0, SEEK_END);
  lseek(fd, 0, SEEK_SET);
  uint8_t* b = n > 8 && n < 64000 ? (uint8_t*)heap_caps_malloc(n, MALLOC_CAP_SPIRAM) : nullptr;
  const bool got = b && read(fd, b, n) == n;
  ::close(fd);
  In in = {b, b + (got ? n : 0)};
  bool ok = got && !memcmp(b, "TNK1", 4);
  if (ok) {
    in.p += 4;
    st = State();
    st.coins = in.get<int32_t>(), st.xp = in.get<int32_t>(), st.cur = in.get<uint8_t>();
    for (int k = in.get<uint8_t>(); k > 0; k--) {
      const int sp = speciesOf(in.str().c_str());
      if (sp >= 0) st.album |= 1ull << sp;
    }
    st.last = in.get<double>(), st.clock = in.get<double>(), st.uid = in.get<uint32_t>(), st.taskDay = in.str();
    st.ntasks = 0;
    for (int k = in.get<uint8_t>(); k > 0; k--) {
      const String id = in.str();
      const float got_ = in.get<float>();
      const bool paid = in.get<uint8_t>();
      for (int q = 0; q < R.ntasks; q++)
        if (id == R.tasks[q].id && st.ntasks < 4) st.tasks[st.ntasks++] = {q, got_, paid};
    }
    getTank(in, st.fresh, false);
    st.hasMarine = in.get<uint8_t>();
    if (st.hasMarine) getTank(in, st.marine, true);
    ok = in.ok;
  }
  heap_caps_free(b);
  if (!ok) return false;
  if (!st.hasMarine) st.cur = false;
  for (const TankRec* t : {&st.fresh, &st.marine})
    for (const FishRec& f : t->fish) st.uid = std::max(st.uid, f.uid + 1);
  return true;
}

// ---------------------------------------------------------------- the day's small jobs
void tasks() {
  const String day = today();
  if (st.ntasks && st.taskDay == day) return;
  int ids[10];
  for (int i = 0; i < R.ntasks; i++) ids[i] = i;
  for (int i = R.ntasks - 1; i > 0; i--) std::swap(ids[i], ids[(int)rnd(0, i + 0.999f)]);
  st.taskDay = day, st.ntasks = std::min(std::min(R.tasksADay, R.ntasks), 4);
  for (int i = 0; i < st.ntasks; i++) st.tasks[i] = {ids[i], 0, false};
}
void claim(int k) {
  for (int i = 0; i < st.ntasks; i++) {
    State::Task& t = st.tasks[i];
    if (t.k != k || t.paid || t.got < R.tasks[k].n) continue;
    t.paid = true, st.coins += R.tasks[k].coins;
    addXp(R.tasksXp), fx("coin", R.tasks[k].coins), say(fmt("Thưởng +%d xu", R.tasks[k].coins));
  }
}

// ---------------------------------------------------------------- purchases
const char* blocked(const TankRec& t, const Species& s, String& why) {
  if (s.marine != t.marine) return (why = s.marine ? "Cá nước mặn không sống được ở nước ngọt" : "Cá nước ngọt không sống được ở nước mặn").c_str();
  if (s.lv > levelNow()) return (why = fmt("Cần cấp %d", s.lv)).c_str();
  const int sp = &s - D.sp;
  if (s.solo)
    for (const FishRec& f : t.fish)
      if (f.sp == sp) return (why = "Chỉ nuôi một con, hai con sẽ đánh nhau").c_str();
  if (s.minSize && t.up[U_size] < s.minSize) return (why = fmt("Cần bể cỡ %d trở lên", s.minSize)).c_str();
  if (s.lid && !t.up[U_lid]) return (why = "Hay nhảy, cần nắp bể").c_str();
  if (s.mature && t.age < s.mature) {
    const float left = s.mature - t.age;
    return (why = fmt("Bể còn mới, chờ thêm %d %s", left >= 24 ? (int)ceilf(left / 24) : (int)ceilf(left), left >= 24 ? "ngày" : "giờ")).c_str();
  }
  if (usedOf(t) + s.space > capOf(t)) return (why = "Bể hết chỗ, hãy nâng cỡ bể").c_str();
  return nullptr;
}
const char* warns(const TankRec& t, const Species& s, String& why) {
  Census c;
  census(t, c);
  const int sp = &s - D.sp, n = c.count[sp] + 1;
  if (s.temp == 1 && !c.warm) return (why = "Cần nước ấm: hãy bật máy sưởi").c_str();
  if (s.temp == 2 && c.warm) return (why = "Bể đang ấm, cá này ưa nước mát").c_str();
  const int h = fears(sp, c);
  if (h >= 0) return (why = fmt("Sẽ sợ %s trong bể", SP(h).vi)).c_str();
  if (s.pred || s.invEater) {
    int prey = 0;
    for (const FishRec& f : t.fish) {
      const Species& o = SP(f.sp);
      if (f.sp != sp && !o.fixed && strcmp(o.id, "snail") && ((s.pred && o.w <= s.pred && !o.pred) || (s.invEater && o.inv))) prey++;
    }
    if (prey) return (why = fmt("Sẽ dọa %d con cá nhỏ trong bể", prey)).c_str();
  }
  if (s.nip && !(s.group && n >= R.nipCalm))
    for (int i = 0; i < c.nids; i++)
      if (SP(c.ids[i]).fins && c.ids[i] != sp) return (why = fmt("Sẽ cắn vây %s", SP(c.ids[i]).vi)).c_str();
  if (s.fins)
    for (int i = 0; i < c.nn; i++)
      if (c.nippers[i] != sp) return (why = fmt("Sẽ bị %s cắn vây", SP(c.nippers[i]).vi)).c_str();
  if (s.hide && !c.hide) return (why = "Cần chỗ trốn: đặt hang hoặc trồng 3 cây").c_str();
  if (s.plantEater && t.plants.size()) return (why = "Sẽ nhổ cây trong bể").c_str();
  if (s.nipCoral && t.plants.size()) return (why = "Sẽ rỉa san hô trong bể").c_str();
  if (s.group && n < s.group) return (why = fmt("Cần đủ đàn %d con mới vui", s.group)).c_str();
  if (s.clean && t.clean < s.clean) return (why = "Cần nước sạch hơn").c_str();
  return nullptr;
}

void buyFish(int sp) {
  if (!spFree(sp)) return lockSheet(N_("This fish"));
  TankRec& t = cur();
  const Species& s = SP(sp);
  String why;
  if (blocked(t, s, why)) return say(why);
  if (!pay(s.price)) return;
  FishRec d = {(uint8_t)sp, false, false, st.uid++, 0, rnd(R.coinEvery0, R.coinEvery1)};
  t.fish.push_back(d);
  spawn(d, false);
  countTask("buy");
  sceneCensus();
  const std::vector<String>& w = game::reasons(d.uid);
  if (!(st.album >> sp & 1)) {
    st.album |= 1ull << sp;
    addXp(R.xpNew);
    say(fmt("Loài mới: %s!", s.vi));
  } else if (w.empty()) say(fmt("Chào %s mới", s.vi));
  if (w.size()) say(fmt("%s: %s", s.vi, lowerFirst(w[0]).c_str()));
  fx("buy");
  save();
}
int firstFree(const std::vector<uint8_t>& used) {
  int slot = 0;
  while (std::find(used.begin(), used.end(), slot) != used.end()) slot++;
  return slot;
}
void buyPlant(int k) {
  TankRec& t = cur();
  const PlantK& p = D.plant[k];
  if (p.lv > levelNow()) return say(fmt("Cần cấp %d", p.lv));
  if ((int)t.plants.size() >= D.look[lookShown(t)].nslots) return say("Hết chỗ trồng. Nhổ bớt một cây trước");
  if (!pay(p.price)) return;
  t.plantSlot.push_back(firstFree(t.plantSlot)), t.plants.push_back(k);
  sync(false);
  fx("buy");
  say(C.ng ? fmt("Đã trồng, nhưng %s %s", SP(C.grazers[0]).vi, !t.marine ? "hay nhổ cây" : "hay rỉa san hô") : fmt("Đã trồng %s", p.vi));
  save();
}
void pullPlant(int slot) {
  TankRec& t = cur();
  for (size_t i = 0; i < t.plants.size(); i++)
    if (t.plantSlot[i] == slot) {
      const PlantK& p = D.plant[t.plants[i]];
      t.plants.erase(t.plants.begin() + i), t.plantSlot.erase(t.plantSlot.begin() + i);
      st.coins += (int)lroundf(p.price * 0.5f);
      sync(false);
      say(fmt("Đã nhổ %s", p.vi));
      save();
      return;
    }
}
void buyDecor(int k) {
  TankRec& t = cur();
  const DecorK& d = D.decor[k];
  if (d.lv > levelNow()) return say(fmt("Cần cấp %d", d.lv));
  if (std::find(t.decor.begin(), t.decor.end(), k) != t.decor.end()) return say(fmt("Bể đã có %s", d.vi));
  if ((int)t.decor.size() >= D.look[lookShown(t)].ndecor) return say("Hết chỗ đặt. Cất bớt một món trước");
  if (!pay(d.price)) return;
  t.decorSlot.push_back(firstFree(t.decorSlot)), t.decor.push_back(k);
  sync(false);
  fx("buy");
  say(fmt("Đã đặt %s", d.vi));
  save();
}
void pullDecor(int k) {
  TankRec& t = cur();
  for (size_t i = 0; i < t.decor.size(); i++)
    if (t.decor[i] == k) {
      t.decor.erase(t.decor.begin() + i), t.decorSlot.erase(t.decorSlot.begin() + i);
      st.coins += (int)lroundf(D.decor[k].price * 0.5f);
      sync(false);
      say(fmt("Đã cất %s", D.decor[k].vi));
      save();
      return;
    }
}
void buyUp(int u) {
  TankRec& t = cur();
  const UpK& k = D.up[u];
  const int l = t.up[u];
  if (l >= k.ncost) {
    if (k.sw) {
      t.heat = !t.heat;
      sceneCensus();
      say(t.heat ? "Máy sưởi bật: nước ấm" : "Máy sưởi tắt: nước mát");
      save();
    } else say("Đã cao nhất");
    return;
  }
  if (!pay(k.cost[l])) return;
  t.up[u] = l + 1;
  if (k.sw) t.heat = true;
  sceneCensus();
  light();
  fx("buy");
  say(k.ncost > 1 ? fmt("%s lên mức %d", k.vi, l + 1) : fmt("Đã lắp %s", recase(k.vi, false, true).c_str()));
  save();
}
void buyLook(int k) {
  if (!lookFree(k)) return lockSheet(N_("This tank look"));
  TankRec& t = cur();
  const Look& L = D.look[k];
  if (t.looks >> k & 1) {
    t.look = k;
    for (size_t i = 0; i < t.plants.size();)
      if (t.plantSlot[i] >= L.nslots) t.plants.erase(t.plants.begin() + i), t.plantSlot.erase(t.plantSlot.begin() + i);
      else i++;
    sync(false);
    if (sc)
      for (int i = 0; i < sc->nfish; i++) sc->fish[i].hx = sc->fish[i].x, sc->fish[i].hy = sc->fish[i].y;
    say(fmt("Đổi sang %s", L.vi));
    save();
    return;
  }
  if (L.lv > levelNow()) return say(fmt("Cần cấp %d", L.lv));
  if (!pay(L.price)) return;
  t.looks |= 1u << k;
  buyLook(k);
}
void switchTank() {
  if (!st.hasMarine) return;
  if (!gate().marine) return lockSheet(N_("The sea tank"));
  TankRec& t = cur();
  t.pile.clear();
  if (sc)
    for (int i = 0; i < sc->ncoins; i++) t.pile.push_back(sc->coins[i].val);
  st.cur = !st.cur;
  sync(true);
  say(!st.cur ? "Bể nước ngọt" : "Bể nước mặn");
  save();
}
void buyMarine() {
  if (st.hasMarine) return;
  if (!gate().marine) return lockSheet(N_("The sea tank"));
  if (R.marineLv > levelNow()) return say(fmt("Cần cấp %d", R.marineLv));
  if (!pay(R.marinePrice)) return;
  newTank(st.marine, true);
  st.hasMarine = true;
  fx("buy");
  switchTank();
  say("Đã có bể nước mặn!");
}

}  // namespace

// ---------------------------------------------------------------- what the numbers say
TankRec& T() { return cur(); }
TankRec* tankOf(bool marine) { return marine ? (st.hasMarine ? &st.marine : nullptr) : &st.fresh; }
bool hasMarine() { return st.hasMarine; }
int coins() { return st.coins; }
int level() { return levelNow(); }
float levelSpan() {
  const int l = levelNow();
  if (l >= R.nXp) return 1;
  const int a = R.xpAt[l - 1], b = R.xpAt[l];
  return (float)(st.xp - a) / (b - a);
}
int capacity(const TankRec& t) { return capOf(t); }
float used(const TankRec& t) { return usedOf(t); }
bool isNight() { return nightNow(); }
bool lampOn(const TankRec& t) { return lamp(t); }
int comfort(const TankRec& t) {
  if (&t == &cur()) return comfortOf(t, C);
  Census c;
  census(t, c);
  return comfortOf(t, c);
}
int mood(const TankRec& t) {
  if (&t == &cur()) return moodOf(t, C);
  Census c;
  census(t, c);
  return moodOf(t, c);
}
float fishSize(const FishRec& f) {
  const float a = clampv(f.age / R.adultH, 0.0f, 1.0f), from = f.fry ? R.fryScale : R.youngScale;
  return from + (1 - from) * a;
}
bool isAdult(const FishRec& f) { return f.age >= R.adultH; }
int diets(const TankRec& t, int* out) {
  int n = 0;
  for (int k = 0; k < F_N; k++) {
    bool use = k == F_flake;
    for (const FishRec& f : t.fish) use |= feeds(f.sp) && dietOf(f.sp) == k;
    if (use) out[n++] = k;
  }
  return n;
}
const std::vector<String>& reasons(uint32_t uid) {
  static const std::vector<String> none;
  for (auto& b : bad)
    if (b.first == uid) return b.second;
  return none;
}
int tasksReady() {
  tasks();
  int n = 0;
  for (int i = 0; i < st.ntasks; i++) n += !st.tasks[i].paid && st.tasks[i].got >= R.tasks[st.tasks[i].k].n;
  return n;
}
float waterWait() { return std::max(0.0f, R.waterWait - (float)(st.clock - cur().waterAt)); }
int sellPrice(const FishRec& d) { return (int)lroundf(SP(d.sp).price * (isAdult(d) ? R.sellAdult : R.sellYoung)); }
const FishRec* fishRec(uint32_t uid) {
  for (const FishRec& f : cur().fish)
    if (f.uid == uid) return &f;
  return nullptr;
}
void albumCount(int& have, int& all) {
  have = 0, all = D.nsp;
  for (int i = 0; i < D.nsp; i++) have += st.album >> i & 1;
}
const char* roleVi(int r) {
  static const char* const ROLE[] = {"Dễ nuôi", "Cá đàn", "Dọn bể", "Cá đẹp", "Cá quý"};
  return ROLE[clampv(r, 0, 4)];
}
String traits(const Species& s) {
  std::vector<const char*> t;
  const String sz = fmt("bể cỡ %d", s.minSize), grp = fmt("đàn %d", s.group);
  if (s.minSize) t.push_back(sz.c_str());
  if (s.lid) t.push_back("cần nắp");
  if (s.group) t.push_back(grp.c_str());
  if (s.solo) t.push_back("nuôi 1 con");
  if (s.pred) t.push_back("cá dữ");
  if (s.nip) t.push_back("cắn vây");
  if (s.temp == 2) t.push_back("ưa mát");
  if (s.temp == 1) t.push_back("ưa ấm");
  if (s.food == F_sink) t.push_back("viên chìm");
  if (s.food == F_live) t.push_back("mồi tươi");
  if (s.hide) t.push_back("cần hang");
  if (s.clean) t.push_back("nước sạch");
  if (s.plantEater) t.push_back("nhổ cây");
  if (s.nipCoral) t.push_back("rỉa san hô");
  if (s.host) t.push_back("thích hải quỳ");
  if (s.eats == 1) t.push_back("dọn thừa");
  if (s.eats == 2) t.push_back("ăn rêu");
  if (s.breed == 1) t.push_back("dễ đẻ");
  String j;
  for (size_t i = 0; i < t.size(); i++) j += (i ? ", " : "") + String(t[i]);
  return j.length() ? recase(j, true, false) : j;
}

// ---------------------------------------------------------------- start, tick
bool start() {
  rs ^= (uint32_t)millis() * 2654435761u ^ (uint32_t)time(nullptr);
  if (!loadSave()) newGame();
  const double now = nowMs();
  const float gone = now > 0 && st.last > 0 ? clampv((float)((now - st.last) / 3600000.0), 0.0f, 72.0f) : 0;
  if (gone > 0.01f) {
    st.clock += gone;
    away(st.fresh, gone);
    if (st.hasMarine) away(st.marine, gone);
  }
  if (sc) {
    sc->onEat = [](int k, float food) {
      TankRec& t = cur();
      t.fed[k] = std::min(100.0f, t.fed[k] + (food > 0 ? food : R.pelletFood));
    };
    sc->onRot = []() {
      TankRec& t = cur();
      t.clean = std::max(0.0f, t.clean - R.leftDirt);
    };
    sc->foodRest = R.leftRest;
  }
  sync(true);
  TankRec& t = cur();
  if (t.news.length()) say(t.news), t.news = "";
  else if (gone > 1) say(moodOf(t, C) >= R.moodHappy ? "Cá mừng bạn về!" : "Bể cần bạn chăm rồi");
  trace::add("tank: game %d coins, xp %d, %s, %d fish, away %.2f h", st.coins, st.xp, st.cur ? "marine" : "fresh", (int)t.fish.size(), gone);
  return true;
}
void stop() { save(); }

void save() {
  if (!Dp || !D.nsp) return;
  st.last = nowMs() > 0 ? nowMs() : st.last;
  TankRec& t = cur();
  t.pile.clear();
  if (sc)
    for (int i = 0; i < sc->ncoins; i++) t.pile.push_back(sc->coins[i].val);
  Out o;
  o.raw("TNK1", 4), o.put((int32_t)st.coins), o.put((int32_t)st.xp), o.put((uint8_t)st.cur);
  uint8_t na = 0;
  for (int i = 0; i < D.nsp; i++) na += st.album >> i & 1;
  o.put(na);
  for (int i = 0; i < D.nsp; i++)
    if (st.album >> i & 1) o.str(SP(i).id);
  o.put(st.last), o.put(st.clock), o.put(st.uid), o.str(st.taskDay.c_str());
  o.put((uint8_t)st.ntasks);
  for (int i = 0; i < st.ntasks; i++) o.str(R.tasks[st.tasks[i].k].id), o.put(st.tasks[i].got), o.put((uint8_t)st.tasks[i].paid);
  putTank(o, st.fresh);
  o.put((uint8_t)st.hasMarine);
  if (st.hasMarine) putTank(o, st.marine);
  t.pile.clear();
  const String p = path(), tmp = p + ".tmp";
  const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) return;
  const bool ok = write(fd, o.b.c_str(), o.b.length()) == (ssize_t)o.b.length();
  ::close(fd);
  if (ok) rename(tmp.c_str(), p.c_str());
  saveT = 0;
}

void tick(float dt) {
  if (!sc || !Dp || !D.nsp) return;
  // The trial ended (or a purchase came) while the tank is open: the tank and look shown follow (lock.h).
  static uint32_t seenGate = 0;
  static TankRec* shown = nullptr;
  if (seenGate != gate().ver) {
    TankRec* now = &cur();
    if (seenGate && (now != shown || sc->look != lookShown(*now))) {
      if (shown && now != shown) {  // the coins lying in the water stay that tank's
        shown->pile.clear();
        for (int i = 0; i < sc->ncoins; i++) shown->pile.push_back(sc->coins[i].val);
      }
      sync(true);
    }
    seenGate = gate().ver, shown = now;
  }
  stepAcc += dt;
  if (stepAcc < 0.1f) return;
  dt = stepAcc, stepAcc = 0;
  TankRec& t = cur();
  const float h = dt / 3600 * timeK;
  st.clock += h;
  advance(t, h, true);
  if (st.hasMarine) advance(curMarine() ? st.fresh : st.marine, h, false);
  if ((cenT += dt) > 1) cenT = 0, sceneCensus();
  const int m = moodOf(t, C);
  const bool happy = m >= R.moodHappy;
  if (sc->played > 0) {
    t.play = std::min(R.playMax, t.play + sc->played * R.playPerS);
    countTask("play", sc->played);
    sc->played = 0;
  }
  // coins: a content fish drops one now and then; when too many lie about, the next one lands in a pile
  if (happy && t.idle < R.awayHours)
    for (int i = 0; i < sc->nfish; i++) {
      Fish* f = &sc->fish[i];
      FishRec* d = nullptr;
      for (FishRec& r : t.fish)
        if (r.uid == f->uid) d = &r;
      if (!d || !SP(d->sp).val || f->sad) continue;
      d->next -= dt * timeK * (m >= 80 ? 1.25f : 1);
      if (d->next <= 0) {
        d->next = rnd(R.coinEvery0, R.coinEvery1);
        const int v = std::max(1, (int)lroundf(SP(d->sp).val * (isAdult(*d) ? 1 : R.coinYoung)));
        if (sc->ncoins < R.maxLying) sc->addCoin(f->x, f->y, f->z, v);
        else sc->coins[(int)rnd(0, sc->ncoins - 0.001f)].val += v;
      }
    }
  for (int i = 0; i < sc->ndecor; i++)
    if (D.decor[sc->decor[i].k].coins && happy && t.idle < R.awayHours) {  // the treasure chest
      if (!t.chestT) t.chestT = rnd(500, 900);
      t.chestT -= dt * timeK;
      if (t.chestT <= 0) {
        t.chestT = rnd(700, 1100);
        const DecorI& ch = sc->decor[i];
        if (sc->ncoins < R.maxLying) sc->addCoin(ch.x, sc->floorY(ch.z) - 30, ch.z + 2, 3, 14);
      }
    }
  for (int i = 0; i < sc->nfish; i++) {
    const FishRec* d = fishRec(sc->fish[i].uid);
    if (d) sc->fish[i].size += (fishSize(*d) - sc->fish[i].size) * std::min(1.0f, dt * 0.5f);
  }
  // the water shows the state
  sc->haze = powf(clampv(1 - t.clean / 100, 0.0f, 1.0f), 1.4f) * 0.95f;
  for (int k = 0; k < F_N; k++) sc->appetite[k] = t.fed[k] < 96;
  const float lvl = sc->algaeLevel();
  if (t.algae > lvl + 1.5f) sc->setAlgae(t.algae);
  else if (t.algae < lvl - 6) sc->clearAlgae(), sc->setAlgae(t.algae);
  light();
  if ((saveT += dt) > 60) save();
}

// ---------------------------------------------------------------- what the player does
void countTask(const char* id, float n) {
  tasks();
  for (int i = 0; i < st.ntasks; i++) {
    State::Task& t = st.tasks[i];
    const TaskK& def = R.tasks[t.k];
    if (strcmp(def.id, id) || t.paid) continue;
    const float was = t.got;
    t.got = std::min((float)def.n, t.got + n);
    if (was < def.n && t.got >= def.n) say(fmt("Xong việc: %s. Vào giỏ nhận thưởng", def.vi));
  }
}
void tapped() { countTask("glass"); }

bool feed(int kind) {
  TankRec& t = cur();
  const uint32_t now = millis();
  if (fedOnce && now - fedAt < R.pinchWait * 1000) return say("Từ từ, cá đang ăn"), false;
  const int cost = R.dietCost[kind];
  if (cost && !pay(cost)) return false;
  fedAt = now, fedOnce = true;
  int eaters = 0;
  for (const FishRec& f : t.fish) eaters += feeds(f.sp) && dietOf(f.sp) == kind;
  const int n = clampv(eaters * 2, 2, R.pellets);  // a pinch fits the mouths: fewer, richer pieces for a few fish
  if (sc) sc->dropFood(rnd(sc->VW * 0.28f, sc->VW * 0.72f), n, kind, R.pelletFood * R.pellets / n);
  countTask("feed");
  if (!eaters) say(fmt("Bể không có cá ăn %s, thức ăn thừa làm dơ nước", recase(R.dietVi[kind], false, true).c_str()));
  else if (t.fed[kind] >= 96) say("Cá no rồi, thức ăn thừa làm dơ nước");
  else addXp(R.xpFeed);
  return true;
}
float wipeAt(float x, float y) {
  if (!sc) return 0;
  const float got = sc->wipe(x, y, 24);
  if (got > 0) {
    cur().algae = sc->algaeLevel();
    if ((wiped += got) > 12) wiped = 0, addXp(R.xpWipe);
  }
  return got;
}
void wipeDone() {
  if (sc) sc->clearAlgae();
  cur().algae = 0;
  countTask("wipe");
  say("Kính sạch bong");
}
bool changeWater() {
  TankRec& t = cur();
  const float w = waterWait();
  if (w > 0) return say(fmt("Mới thay nước. Chờ thêm %d %s", w >= 1 ? (int)ceilf(w) : (int)ceilf(w * 60), w >= 1 ? "giờ" : "phút")), false;
  if (t.clean > 92) return say("Nước còn sạch"), false;
  if (t.marine && !pay(R.saltCost)) return false;
  t.clean = R.waterTo, t.waterAt = st.clock;
  addXp(R.xpWater);
  countTask("water");
  fx("water");
  say(t.marine ? fmt("Nước trong veo (muối biển -%d xu)", R.saltCost) : String("Nước trong veo"));
  sceneCensus();
  save();
  return true;
}
void setLamp(bool on) {
  TankRec& t = cur();
  if (t.up[U_light] >= 3) return say("Đèn tự bật tắt theo giờ");
  t.lamp = on;
  light();
}
int collect(float x, float y) {
  if (!sc) return 0;
  const int got = sc->takeCoins(x, y, 26);
  if (got) {
    st.coins += got, cur().idle = 0;
    addXp(R.xpCoin);
    countTask("coin", got);
    fx("coin", got);
  }
  return got;
}
void cure(uint32_t uid) {
  TankRec& t = cur();
  for (FishRec& d : t.fish)
    if (d.uid == uid && d.sick) {
      if (!pay(R.cureCost)) return;
      d.sick = false;
      sceneCensus();
      say(fmt("%s đã khỏi bệnh%s", SP(d.sp).vi, t.clean < R.sickBelow ? ". Thay nước kẻo bệnh lại" : ""));
      return;
    }
}
void sellFish(uint32_t uid) {
  TankRec& t = cur();
  for (size_t i = 0; i < t.fish.size(); i++)
    if (t.fish[i].uid == uid) {
      const FishRec d = t.fish[i];
      const int v = sellPrice(d);
      t.fish.erase(t.fish.begin() + i);
      if (sc) sc->removeFish(uid);
      st.coins += v;
      sceneCensus();
      say(fmt("Bán %s +%d xu", SP(d.sp).vi, v));
      fx("coin", v);
      save();
      return;
    }
}

// ---------------------------------------------------------------- the shop's lists (the screen only draws what these say)
int thumbOf(const char* id) {
  char b[28];
  snprintf(b, sizeof b, "t_%s", id);
  return packs[P_THUMBS].find(b);
}
std::vector<Row> shop(int tab) {
  std::vector<Row> rows;
  TankRec& t = cur();
  const int lv = levelNow(), coins = st.coins;
  Row r;
  if (tab == 0) {
    std::vector<int> ids;
    for (int i = 0; i < D.nsp; i++)
      if (SP(i).marine == t.marine) ids.push_back(i);
    for (size_t a = 1; a < ids.size(); a++) {  // by level, then price, equals kept in order (insertion: std::stable_sort cost 1.3 KB)
      const int v = ids[a];
      size_t b = a;
      for (; b > 0 && (SP(ids[b - 1]).lv != SP(v).lv ? SP(ids[b - 1]).lv > SP(v).lv : SP(ids[b - 1]).price > SP(v).price); b--) ids[b] = ids[b - 1];
      ids[b] = v;
    }
    for (int i : ids) {
      const Species& s = SP(i);
      r = Row();
      int have = 0;
      for (const FishRec& f : t.fish) have += f.sp == i;
      const bool lock = s.lv > lv;
      String sw, ww;
      const char* stop = lock ? nullptr : blocked(t, s, sw);
      const char* warn = lock || stop ? nullptr : warns(t, s, ww);
      r.art = s.thumb, r.name = s.vi, r.sub = fmt("%s \xC2\xB7 chỗ %s", roleVi(s.role), num(s.space).c_str()), r.count = have;
      r.note = stop ? sw : warn ? ww : traits(s), r.bad = stop, r.warn = warn, r.price = s.price;
      r.tag = lock ? fmt("Cấp %d", s.lv) : String(), r.can = !lock && !stop && coins >= s.price, r.dim = lock, r.act = 1, r.arg = i;
      if (!spFree(i)) r.paid = true, r.can = true, r.tag = "", r.bad = r.warn = false, r.note = traits(s);
      rows.push_back(r);
    }
  } else if (tab == 1) {
    String gr;
    if (C.ng) gr = fmt("%s %s", SP(C.grazers[0]).vi, !t.marine ? "hay nhổ cây" : "hay rỉa san hô");
    for (int i = 0; i < D.nplant; i++) {
      const PlantK& p = D.plant[i];
      if (p.marine != t.marine) continue;
      r = Row();
      const bool lock = p.lv > lv;
      r.art = thumbOf(p.id), r.name = p.vi, r.sub = fmt("Thoải mái +%d", p.comfort);
      r.note = gr.length() ? gr : !strcmp(p.id, "anemone") ? String("Cá hề thích ở trong") : fmt("Chỗ trốn khi đủ %d cây", R.hidePlants);
      r.warn = gr.length(), r.price = p.price, r.tag = lock ? fmt("Cấp %d", p.lv) : String(), r.can = !lock && coins >= p.price, r.dim = lock;
      r.act = 2, r.arg = i;
      rows.push_back(r);
    }
    for (size_t i = 0; i < t.plants.size(); i++) {
      const PlantK& p = D.plant[t.plants[i]];
      r = Row();
      r.art = thumbOf(p.id), r.name = fmt("Nhổ %s", p.vi), r.sub = "Đang trồng";
      r.note = fmt("Nhổ thì trả lại %d xu", (int)lroundf(p.price * 0.5f)), r.tag = "Nhổ", r.can = true, r.act = 3, r.arg = t.plantSlot[i];
      rows.push_back(r);
    }
  } else if (tab == 2) {
    for (int i = 0; i < D.ndecor; i++) {
      const DecorK& d = D.decor[i];
      r = Row();
      const bool lock = d.lv > lv, have = std::find(t.decor.begin(), t.decor.end(), i) != t.decor.end(), cave = R.hideDecor[i];
      r.art = thumbOf(d.id), r.name = d.vi, r.arg = i;
      if (have) r.sub = "Đang đặt", r.note = fmt("Cất thì trả lại %d xu", (int)lroundf(d.price * 0.5f)), r.tag = "Cất", r.can = true, r.act = 5;
      else {
        r.sub = fmt("Thoải mái +%d", d.comfort);
        r.note = d.coins ? "Thỉnh thoảng nhả xu" : cave ? "Là hang cho cá nhát" : !strcmp(d.id, "bubbler") ? "Bong bóng sủi từ đây" : "";
        r.price = d.price, r.tag = lock ? fmt("Cấp %d", d.lv) : String(), r.can = !lock && coins >= d.price, r.dim = lock, r.act = 4;
      }
      rows.push_back(r);
    }
  } else if (tab == 3) {
    for (int u = 0; u < NUP; u++) {
      const UpK& k = D.up[u];
      if (k.marineOnly && !t.marine) continue;
      r = Row();
      const int l = t.up[u];
      const bool max = l >= k.ncost, sw = k.sw && max;
      r.up = u, r.name = k.ncost > 1 ? fmt("%s  %d/%d", k.vi, l, k.ncost) : String(k.vi);
      r.sub = u == U_light && l == k.ncost - 1 ? "Mức cuối: tự bật tắt" : k.what;
      r.note = sw ? (t.heat ? "Đang bật: nước ấm" : "Đang tắt: nước mát")
               : u == U_size ? fmt("Đang chứa %s / %d", num(usedOf(t)).c_str(), capOf(t))
               : u == U_heater ? "Cá vàng, koi không ưa nước ấm" : "";
      r.price = max ? -1 : k.cost[l];
      r.tag = sw ? (t.heat ? "Tắt" : "Bật") : max ? (k.ncost > 1 ? "Cao nhất" : "Đã có") : "";
      r.can = sw || (!max && coins >= k.cost[l]), r.act = 6, r.arg = u;
      rows.push_back(r);
    }
  } else if (tab == 4) {
    for (int i = 0; i < D.nlook; i++) {
      const Look& k = D.look[i];
      if (k.marine != t.marine) continue;
      r = Row();
      const bool own = t.looks >> i & 1, on = lookShown(t) == i, lock = !own && k.lv > lv;
      r.look = true, r.art = thumbOf(k.id), r.name = k.vi;
      r.sub = on ? "Đang dùng" : own ? "Đã có" : "Kiểu hồ mới", r.note = !t.marine ? "Bể nước ngọt" : "Bể nước mặn";
      r.price = own ? -1 : k.price, r.tag = on ? String("Đang dùng") : own ? String("Dùng") : lock ? fmt("Cấp %d", k.lv) : String();
      r.can = !on && (own || (!lock && coins >= k.price)), r.dim = lock, r.act = 7, r.arg = i;
      if (!lookFree(i)) r.paid = true, r.can = true, r.tag = "", r.price = -1, r.sub = own ? "Đã có" : "Kiểu hồ mới";
      rows.push_back(r);
    }
    r = Row();
    r.look = true;
    if (!st.hasMarine) {
      const bool lock = R.marineLv > lv;
      r.art = packs[P_THUMBS].find("t_reef"), r.name = "Bể nước mặn", r.sub = "Bể thứ hai: cá biển, san hô";
      r.note = "Cá biển chỉ sống ở nước mặn", r.price = R.marinePrice, r.tag = lock ? fmt("Cấp %d", R.marineLv) : String();
      r.can = !lock && coins >= R.marinePrice, r.dim = lock, r.act = 8;
      if (!gate().marine) r.paid = true, r.can = true, r.tag = "", r.price = -1;
    } else {
      const TankRec& o = curMarine() ? st.fresh : st.marine;
      r.art = thumbOf(D.look[o.look].id);
      r.name = !t.marine ? "Sang bể nước mặn" : "Sang bể nước ngọt", r.sub = "Bạn có hai bể", r.tag = "Sang", r.can = true, r.act = 9;
      if (!gate().marine) r.paid = true, r.tag = "";
    }
    rows.push_back(r);
  } else if (tab == 5) {
    for (int i = 0; i < D.nsp; i++) {
      const Species& s = SP(i);
      const bool have = st.album >> i & 1;
      r = Row();
      r.art = s.thumb, r.name = have ? s.vi : "Chưa nuôi", r.sub = fmt("%s \xC2\xB7 %s", !s.marine ? "Nước ngọt" : "Nước mặn", roleVi(s.role));
      r.note = traits(s), r.tag = have ? String("Đã nuôi") : fmt("Cấp %d", s.lv), r.dim = !have, r.shade = !have;
      rows.push_back(r);
    }
  } else if (tab == 6) {
    tasks();
    for (int i = 0; i < st.ntasks; i++) {
      const State::Task& q = st.tasks[i];
      const TaskK& k = R.tasks[q.k];
      const bool done = q.got >= k.n;
      r = Row();
      r.artPack = P_THINGS, r.art = packs[P_THINGS].find("t_coin"), r.name = k.vi;
      r.sub = q.paid ? String("Đã nhận thưởng") : fmt("Thưởng %d xu", k.coins);
      r.note = q.paid ? String() : fmt("Đã làm %d / %d", (int)floorf(q.got), k.n);
      r.tag = q.paid ? String("Xong") : done ? String("Nhận") : fmt("%d/%d", (int)floorf(q.got), k.n);
      r.can = done && !q.paid, r.dim = q.paid, r.act = 10, r.arg = q.k;
      rows.push_back(r);
    }
    r = Row();
    r.artPack = P_THINGS, r.art = packs[P_THINGS].find("t_album"), r.name = "Việc mới mỗi ngày", r.sub = "Ba việc, qua ngày đổi việc khác";
    r.tag = "", r.act = -1;
    rows.push_back(r);
  }
  return rows;
}

void press(const Row& r) {
  switch (r.act) {
    case 1: return buyFish(r.arg);
    case 2: return buyPlant(r.arg);
    case 3: return pullPlant(r.arg);
    case 4: return buyDecor(r.arg);
    case 5: return pullDecor(r.arg);
    case 6: return buyUp(r.arg);
    case 7: return buyLook(r.arg);
    case 8: return buyMarine();
    case 9: return switchTank();
    case 10: return claim(r.arg);
  }
}

// ---------------------------------------------------------------- the test panel's tools (dev console)
#if TANK_QA  // not in the release image (the app slot is full): the simulator, or a board built with TANK_QA=1
void debugTime(float h) {
  TankRec& t = cur();
  t.pile.clear();
  st.clock += h;
  away(st.fresh, h);
  if (st.hasMarine) away(st.marine, h);
  if (t.news.length()) say(t.news), t.news = "", sync(true);
  for (int v : t.pile) {
    if (!sc) break;
    if (sc->ncoins < R.maxLying) {
      const float z = rnd(20, 80);
      sc->addCoin(rnd(30, sc->VW - 30), 0, z, v, 0);
      CoinI& c = sc->coins[sc->ncoins - 1];
      c.y = sc->coinY(z), c.rest = true;
    } else sc->coins[0].val += v;
  }
  t.pile.clear();
  sceneCensus();
  say(fmt("Đã qua %s giờ", num(h).c_str()));
}
void debugCoins(int n) { st.coins += n; }
void debugSet(const char* k, const char* v) {  // the test panel's state knobs: add ID [N], sick, algae N, clean N, fed N, young
  TankRec& t = cur();
  if (!strcmp(k, "add")) {
    String s = v;
    const int sp_ = s.indexOf(' ');
    const int sp = speciesOf((sp_ < 0 ? s : s.substring(0, sp_)).c_str()), n = sp_ < 0 ? 1 : s.substring(sp_ + 1).toInt();
    if (sp < 0 || SP(sp).marine != t.marine) return;
    for (int i = 0; i < n; i++) {
      FishRec d = {(uint8_t)sp, false, false, st.uid++, R.adultH, rnd(R.coinEvery0, R.coinEvery1)};
      t.fish.push_back(d), spawn(d, false);
    }
    st.album |= 1ull << sp;
  } else if (!strcmp(k, "sick")) {
    for (FishRec& f : t.fish)
      if (!f.sick) {
        f.sick = true;
        break;
      }
  } else if (!strcmp(k, "algae")) t.algae = atof(v);
  else if (!strcmp(k, "clean")) t.clean = atof(v);
  else if (!strcmp(k, "fed")) {
    for (int i = 0; i < F_N; i++) t.fed[i] = atof(v);
    t.food = atof(v);
  } else if (!strcmp(k, "young")) {
    for (FishRec& f : t.fish) f.age = R.adultH * 0.3f;
  }
  sceneCensus();
}
void debugXp(int n) { addXp(n); }
void debugLook(const char* id) {
  const int k = lookOf(id);
  if (k < 0) return;
  if (D.look[k].marine && !st.hasMarine) newTank(st.marine, true), st.hasMarine = true;
  if (st.cur != D.look[k].marine) {
    cur().pile.clear();
    st.cur = D.look[k].marine;
    sync(true);
  }
  cur().looks |= 1u << k;
  buyLook(k);
}
void reset() {
  remove(path().c_str());
  newGame();
  sync(true);
  say("Bể mới tinh");
}
#endif
#if TANK_QA  // the bench and the shots only (not in the release image since 2026-11-19)
bool debugKeep(bool restore) {  // keep: saved now, then copied to tank.qa; restore: copied back and loaded
  if (!restore) save();
  const String p = path(), q = p.substring(0, p.length() - 3) + "qa";
  FILE* in = fopen((restore ? q : p).c_str(), "rb");
  if (!in) return false;
  const String tmp = p + ".tmp";
  FILE* out = fopen((restore ? tmp : q).c_str(), "wb");
  bool ok = out != nullptr;
  char b[512];
  for (size_t n; ok && (n = fread(b, 1, sizeof b, in)) > 0;) ok = fwrite(b, 1, n, out) == n;
  fclose(in);
  if (out) ok = fclose(out) == 0 && ok;
  if (!ok) return false;
  if (restore) {
    if (rename(tmp.c_str(), p.c_str())) return false;
    saveT = 0;
    start();  // the kept game, with the time since as time away
  }
  trace::add("tank: save %s", restore ? "put back" : "kept");
  return true;
}
void fillFree(const char* look, const char* list) {  // the page's showcase: a look and its fish, no rules
  if (!sc) return;
  const int l = lookOf(look);
  if (l < 0) return;
  sc->clearFish(), sc->nfood = 0, sc->ncoins = 0, sc->nplants = 0, sc->ndecor = 0;
  sc->setLook(l);
  int ids[3], slots[3], n = 0;
  for (int k = 0; k < 3; k++)
    if (D.look[l].plants0[k] >= 0) ids[n] = D.look[l].plants0[k], slots[n] = k, n++;
  sc->setPlants(ids, slots, n);
  String s = list;
  uint32_t uid = 900000;
  while (s.length()) {
    int k = s.indexOf(',');
    String part = k < 0 ? s : s.substring(0, k);
    s = k < 0 ? "" : s.substring(k + 1);
    int m = 1;
    const int star = part.indexOf('*');
    if (star >= 0) m = part.substring(star + 1).toInt(), part = part.substring(0, star);
    const int sp = speciesOf(part.c_str());
    if (sp < 0 || D.sp[sp].marine != D.look[l].marine) continue;
    for (int i = 0; i < m; i++) sc->addFish(sp, 1, uid++);
  }
  for (int i = 0; i < 3; i++) sc->tint[i] = sc->tintNow[i] = 1;
  sc->lamp = true, sc->rays = 1, sc->sleeping = false, sc->haze = sc->hazeNow = 0, sc->air = 1;
}
#endif

}  // namespace game
}  // namespace tank
