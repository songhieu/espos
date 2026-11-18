#include "game.h"

#include <Arduino.h>
#include <Preferences.h>

#include <ctime>

#include "packs.h"
#include "trace.h"

namespace pet {
namespace game {

const Rules G;
// game.json -> rules.act: food, fun, energy, clean, xp
const Effect PLAY = {-5, 30, -10, -6, 8}, PET = {0, 6, 0, 0, 1}, WASH = {0, 4, 0, 70, 8}, NAP = {0, 0, 70, 0, 5},
             TAP = {0, 3, 0, 0, 1};
const char* const TAP_CLIPS[4] = {"nudge", "alert", "high_five", "giggle"};

namespace {

State st;
bool loaded = false;
uint32_t dirtyAt = 0, lastSave = 0;
float poweredAcc = 0;  // seconds not yet counted in poweredMin

constexpr int MOMENTS = 24;
Moment mo[MOMENTS];
int moN = 0;

float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }

uint32_t dayKey() {  // the day the touch XP cap counts for: the calendar day, else 24 powered hours
  const uint32_t t = today();
  return t ? t : 1000000 + st.poweredMin / (24 * 60);
}

uint32_t yesterday(uint32_t d) {
  tm t = {};
  t.tm_year = d / 10000 - 1900, t.tm_mon = d / 100 % 100 - 1, t.tm_mday = d % 100 - 1, t.tm_hour = 12;
  mktime(&t);
  return (t.tm_year + 1900) * 10000 + (t.tm_mon + 1) * 100 + t.tm_mday;
}

float nextFind() { return G.findEvery[0] + random(1000) / 1000.0f * (G.findEvery[1] - G.findEvery[0]); }

String packMoments() {
  String s;
  for (int i = 0; i < moN; i++) {
    char b[40];
    snprintf(b, sizeof b, "%u.%u.%u.%u.%lu;", mo[i].kind, mo[i].arg, mo[i].clip, mo[i].frame, (unsigned long)mo[i].day);
    s += b;
  }
  return s;
}

void unpackMoments(const String& s) {
  moN = 0;
  const char* p = s.c_str();
  while (*p && moN < MOMENTS) {
    unsigned k, a, c, f;
    unsigned long d;
    if (sscanf(p, "%u.%u.%u.%u.%lu;", &k, &a, &c, &f, &d) != 5) break;
    mo[moN++] = {(uint8_t)k, (uint8_t)a, (uint8_t)c, (uint8_t)f, (uint32_t)d};
    p = strchr(p, ';');
    if (!p) break;
    p++;
  }
}

}  // namespace

State& s() {
  if (!loaded) load();
  return st;
}

float speed() {
#ifdef SIM
  static float v = getenv("PAWBOOP_SPEED") ? atof(getenv("PAWBOOP_SPEED")) : 1;
  return v;
#else
  return 1;  // raise to test the needs falling faster (the page's "Nhanh" is 240)
#endif
}

uint32_t today() {
  const time_t now = time(nullptr);
  if (now < 1700000000) return 0;
  tm t;
  localtime_r(&now, &t);
  return (t.tm_year + 1900) * 10000 + (t.tm_mon + 1) * 100 + t.tm_mday;
}

// ------------------------------------------------------------------ saving
void load() {
  loaded = true;
  Preferences p;
  p.begin("pet", true);
  const String v = p.getString("st", "");
  const String m = p.getString("mo", "");
  p.end();
  if (v.length()) {
    long a[24] = {};
    int n = 0;
    const char* c = v.c_str();
    while (*c && n < 24) {
      a[n++] = strtol(c, (char**)&c, 10);
      if (*c == ',') c++;
    }
    if (n >= 20 && a[0] == 1) {  // version 1
      for (int k = 0; k < 4; k++) st.need[k] = a[1 + k] / 100.0f;
      st.coins = a[5], st.xp = a[6], st.touchXp = a[7], st.touchDay = a[8], st.poweredMin = a[9];
      st.giftDay = a[10], st.giftPowered = a[11], st.found = a[12], st.findIn = a[13];
      st.streak = a[14], st.best = a[15], st.careDay = a[16], st.muted = a[17], st.notify = a[18];
      st.lv = levelOf(st.xp);
    }
  } else {  // a new Biscuit: start.board (50 coins); the first day's gift comes the next day
    st.findIn = nextFind();
    st.giftDay = today(), st.giftPowered = 1;
  }
  unpackMoments(m);
  trace::add("pet: loaded lv %d xp %d coins %d needs %.0f/%.0f/%.0f/%.0f", st.lv, st.xp, st.coins, st.need[0],
             st.need[1], st.need[2], st.need[3]);
}

void save() {
  char b[200];
  snprintf(b, sizeof b, "1,%d,%d,%d,%d,%d,%d,%d,%lu,%lu,%lu,%lu,%d,%d,%d,%d,%lu,%d,%d", (int)(st.need[0] * 100),
           (int)(st.need[1] * 100), (int)(st.need[2] * 100), (int)(st.need[3] * 100), st.coins, st.xp, st.touchXp,
           (unsigned long)st.touchDay, (unsigned long)st.poweredMin, (unsigned long)st.giftDay,
           (unsigned long)st.giftPowered, st.found, (int)st.findIn, st.streak, st.best, (unsigned long)st.careDay,
           st.muted, st.notify);
  Preferences p;
  p.begin("pet", false);
  p.putString("st", b);
  p.end();
  dirtyAt = 0;
  lastSave = millis();
}

void saveSoon() {
  if (!dirtyAt) dirtyAt = millis() | 1;
}

// ------------------------------------------------------------------ time passes
void advance(uint32_t ms, bool alone) {
  s();
  const float dt = ms / 1000.0f * speed();
  for (int k = 0; k < 4; k++) st.need[k] = max(0.0f, st.need[k] - G.drift[k] / 3600 * dt);
  poweredAcc += ms / 1000.0f;
  if (poweredAcc >= 60) st.poweredMin += (uint32_t)(poweredAcc / 60), poweredAcc -= 60 * (int)(poweredAcc / 60);
  const uint32_t d = dayKey();
  if (d != st.touchDay) st.touchDay = d, st.touchXp = 0;
  // A happy dog on her own brings a coin every 8 to 12 minutes; at most find_max wait.
  if (alone && !strcmp(moodRole(), "idle") && happy() && st.found < G.findMax) {
    st.findIn -= dt;
    if (st.findIn <= 0) {
      st.found++;
      st.findIn = nextFind();
      trace::add("pet: found a coin (%d waiting)", st.found);
      if (!hasMoment(4, 0)) addMoment(4, 0, "happy", 0);
      saveSoon();
    }
  }
  if (dirtyAt && millis() - dirtyAt > 1000) save();
  else if (millis() - lastSave > 5 * 60 * 1000) save();
}

// ------------------------------------------------------------------ rules
int levelOf(int xp) {
  int l = 1;
  while (l < 12 && xp >= G.xpAt[l]) l++;
  return l;
}

float xpFrac() {
  const int a = G.xpAt[st.lv - 1], b = st.lv < 12 ? G.xpAt[st.lv] : a + 1;
  return clampf((float)(st.xp - a) / (b - a), 0, 1);
}

int taste() { return min(3, (s().lv - 1) / 2); }
int verdict(int t) { return t < taste() ? 0 : t > taste() ? 2 : 1; }

const char* moodRole() {
  const State& v = s();
  if (v.need[FOOD] < G.low) return "hungry";
  if (v.need[ENERGY] < G.sleepy) return "sleepy";
  if (v.need[FUN] < G.low) return "bored";
  return "idle";
}

bool happy() {
  for (float v : s().need)
    if (v < G.happyAt) return false;
  return true;
}

void addXp(int n) {
  st.xp += n;
  const int l = levelOf(st.xp);
  if (l > st.lv) {
    st.lv = l;
    st.coins += G.levelGift;
    st.levelUp = true;
    trace::add("pet: level %d", l);
  }
}

void addCoins(int n) { st.coins += n; }

void apply(const Effect& e, bool touch) {
  s();
  const int d[4] = {e.food, e.fun, e.energy, e.clean};
  for (int k = 0; k < 4; k++) st.need[k] = clampf(st.need[k] + d[k], 0, 100);
  if (!touch) {
    addXp(e.xp);
  } else {
    const int n = min(e.xp, G.touchXpDay - st.touchXp);
    if (n > 0) st.touchXp += n, addXp(n);
  }
  cared();
  saveSoon();
}

void feedEffect(int v) {
  const Effect e = {G.feed[v][0], G.feed[v][1], 0, 0, G.feed[v][2]};
  apply(e, false);
}

void cared() {
  const uint32_t d = today();
  if (!d || d == st.careDay) return;
  st.streak = st.careDay && st.careDay == yesterday(d) ? st.streak + 1 : 1;
  st.careDay = d;
  st.best = max(st.best, st.streak);
  for (int n : {3, 7, 30, 100})
    if (st.streak == n && !hasMoment(5, n)) addMoment(5, n, "high_five", 20);
}

bool giftDue() {
  s();
  const uint32_t d = today();
  bool due;
  if (d) due = d != st.giftDay;
  else due = st.poweredMin - st.giftPowered >= 6 * 60 || !st.giftPowered;  // no clock: 6 powered hours
  if (!due) return false;
  st.giftDay = d, st.giftPowered = max<uint32_t>(1, st.poweredMin);
  st.coins += G.dayGift;
  st.gift = true;
  saveSoon();
  trace::add("pet: day's gift");
  return true;
}

int playDrop() {
  for (auto& p : G.playCoins)
    if (s().need[FUN] < p[0]) return p[1];
  return 1;
}

// ------------------------------------------------------------------ moments
int moments(Moment* out, int max) {
  s();
  const int n = min(max, moN);
  for (int i = 0; i < n; i++) out[i] = mo[moN - 1 - i];  // newest first
  return n;
}

bool hasMoment(int kind, int arg) {
  s();
  for (int i = 0; i < moN; i++)
    if (mo[i].kind == kind && mo[i].arg == arg) return true;
  return false;
}

void addMoment(int kind, int arg, const char* clip, int frame) {
  s();
  int ci = 0;
  for (int i = 0; i < pk::clipCount(); i++)
    if (!strncmp(pk::clipAt(i)->name, clip, 16)) ci = i;
  if (moN == MOMENTS) memmove(mo, mo + 1, sizeof(Moment) * --moN);
  mo[moN++] = {(uint8_t)kind, (uint8_t)arg, (uint8_t)ci, (uint8_t)frame, today()};
  Preferences p;
  p.begin("pet", false);
  p.putString("mo", packMoments());
  p.end();
  trace::add("pet: moment %d/%d", kind, arg);
}

}  // namespace game
}  // namespace pet
