// The run's rules (logic.h): the reference page's JavaScript carried over line by line.
// Small rather than fast (20 steps a second, nothing heavy): the flash is nearly full.
#pragma GCC optimize("Os")
#include "logic.h"

#include <ArduinoJson.h>

#include <cmath>

#include "i18n.h"
#include "trace.h"

#ifndef SIM
#include "esp_random.h"
#endif

namespace run {

double rnd() {
#ifdef SIM
  return (rand() & 0xFFFFFF) / 16777216.0;
#else
  return (esp_random() >> 8) / 16777216.0;
#endif
}

namespace {

template <class T>
const T& pickOf(const std::vector<T>& a) {
  return a[(size_t)(rnd() * a.size())];
}

double jsRound(double v) { return floor(v + 0.5); }  // Math.round

// run.json numbers (kept out of line: each lookup inlined costs flash)
__attribute__((noinline)) double num(JsonObjectConst o, const char* key, double def) { return o[key] | def; }
__attribute__((noinline)) double num(JsonVariantConst a, int i, double def) { return a[i] | def; }

// The page's dog.js heights (cm) of the jump clip's 21 pictures; the pack keeps only "in the air" (flags bit 1),
// and the coin rule needs "below 1.1 m". Other clips stay under 0.5 m.
const uint8_t JUMP_CM[21] = {14, 51, 82, 107, 126, 139, 146, 148, 143, 132, 115, 93, 64, 29, 0, 0, 0, 0, 0, 0, 0};

}  // namespace

// ------------------------------------------------------------------ the world: run.json + installed roads
bool World::parse(const char* json, size_t len, String& err) {
  JsonDocument doc;
  if (deserializeJson(doc, json, len)) return err = i18n::fmt(N_("broken {0}"), "run.json"), false;
  JsonObjectConst r = doc["rules"], g = doc["geometry"];
  R.speed0 = num(r, "speed0", R.speed0), R.speedMax = num(r, "speed_max", R.speedMax);
  R.speedPer100 = num(r, "speed_per_100m", R.speedPer100), R.laneTime = num(r, "lane_time", R.laneTime);
  R.hearts = num(r, "hearts", R.hearts), R.heartsMax = num(r, "hearts_max", R.heartsMax);
  R.safeAfterHit = num(r, "safe_after_hit", R.safeAfterHit);
  R.heartEvery[0] = num(r["heart_every"], 0, R.heartEvery[0]), R.heartEvery[1] = num(r["heart_every"], 1, R.heartEvery[1]);
  R.firstRow = num(r, "first_row", R.firstRow), R.gap0 = num(r, "gap0", R.gap0), R.gapMin = num(r, "gap_min", R.gapMin);
  R.gapPer100 = num(r, "gap_per_100m", R.gapPer100), R.loopsAMap = num(r, "loops_a_map", R.loopsAMap);
  R.metresACoin = num(r, "metres_a_coin", R.metresACoin), R.healthyBonus = num(r, "healthy_bonus", R.healthyBonus);
  R.hitWindow = num(r, "hit_window_m", R.hitWindow);
  R.wide = num(g["wide"], 0, R.wide);
  R.dogX = num(g["dog_anchor"], 0, R.dogX), R.dogY = num(g["dog_anchor"], 1, R.dogY);
  R.lanePx = num(g, "lane_px", R.lanePx), R.panPx = num(g, "pan_px", R.panPx);
  R.step = num(g, "metres_a_road_picture", R.step);
  fps = dog && dog->data ? dog->head()->fps : 20;
  if (fps <= 0) fps = 20;

  // things: run.json "things" (kind) + rules.half_len, pictures from the things pack ("<id><lane>")
  things.clear();
  JsonObjectConst kinds = doc["kinds"];
  for (JsonPairConst p : doc["things"].as<JsonObjectConst>()) {
    ThingType t;
    t.id = p.key().c_str();
    const char* k = p.value()["kind"] | "block";
    t.kind = (Kind)(kinds[k] | (int)BLOCK);
    t.halfLen = r["half_len"][t.id.c_str()] | 0.4;
    bool ok = true;
    for (int l = 0; l < 3; l++) {
      char nm[20];
      snprintf(nm, sizeof nm, "%s%d", t.id.c_str(), l);
      t.clip[l] = th ? th->clip(nm) : nullptr;
      ok &= t.clip[l] && t.clip[l]->count > 0;
    }
    if (!ok) continue;  // no pictures: never placed
    if (t.kind == COIN) coin = things.size();
    if (t.kind == HEART) heart = things.size();
    things.push_back(t);
  }
  auto typeOf = [&](const char* id) {
    for (size_t i = 0; i < things.size(); i++)
      if (things[i].id == id) return (int)i;
    return -1;
  };

  maps.clear();
  for (JsonVariantConst o : doc["order"].as<JsonArrayConst>()) {
    MapInfo m;
    m.id = o.as<const char*>();
    JsonObjectConst mt = doc["maps"][m.id.c_str()]["things"];
    static const char* KIND[3] = {"jump", "slide", "block"};
    for (int k = 0; k < 3; k++)
      for (JsonVariantConst id : mt[KIND[k]].as<JsonArrayConst>()) {
        const int ti = typeOf(id.as<const char*>());
        if (ti >= 0) m.kinds[k].push_back(ti);
      }
    m.pictures = doc["roads"][m.id.c_str()]["pictures"] | 160;
    maps.push_back(m);
  }
  links.clear();
  for (JsonPairConst p : doc["roads"].as<JsonObjectConst>()) {
    const char* key = p.key().c_str();
    const char* gt = strchr(key, '>');
    if (!gt) continue;
    LinkInfo l;
    const String a(String(key).substring(0, gt - key));
    l.from = mapIndex(a.c_str()), l.to = mapIndex(gt + 1);
    l.pictures = p.value()["pictures"] | 0, l.at = p.value()["at"] | 0;
    if (!(p.value()["real"] | true) && l.pictures > 0) fadeN = l.pictures, fadeAt = l.at;  // the fade link
    if (l.from >= 0 && l.to >= 0 && l.pictures > 0) links.push_back(l);
  }
  if (!dog || !dog->data) return err = T_("dog pictures missing"), false;
  cRun = dog->clip("run"), cJump = dog->clip("jump"), cSlide = dog->clip("slide");
  cLaneL = dog->clip("lane_l"), cLaneR = dog->clip("lane_r"), cStumble = dog->clip("stumble");
  cDizzy = dog->clip("dizzy"), cDizzyLoop = dog->clip("dizzy_loop");
  if (!cRun || !cJump || !cSlide || !cLaneL || !cLaneR || !cStumble || !cDizzy || !cDizzyLoop)
    return err = T_("dog pictures broken"), false;
  if (maps.empty() || coin < 0) return err = i18n::fmt(N_("broken {0}"), "run.json"), false;
  // The road files' names: run.json roads.<key>.file, kept for openRoads().
  roadFiles.clear();
  for (MapInfo& m : maps) roadFiles.push_back(doc["roads"][m.id.c_str()]["file"] | "");
  for (LinkInfo& l : links) {
    const String k = maps[l.from].id + ">" + maps[l.to].id;
    roadFiles.push_back(doc["roads"][k.c_str()]["file"] | "");
  }
  return true;
}

bool World::openRoads(const char* id, String& err) {
  freeRoads();
  auto openOne = [&](const String& pk, const String& file, int pictures) {
    if (!file.length() || nroads >= (int)(sizeof roads / sizeof roads[0])) return -1;
    media::Road& r = roads[nroads];
    if (!r.open(pk.c_str(), file.c_str())) return -1;
    const int want = half ? (pictures + 1) / 2 : pictures;
    if (r.n < want) {  // fewer pictures than run.json says: not used
      r.free();
      return -1;
    }
    return nroads++;
  };
  // Each map its own package (2.0): its road at 20 pictures a second, and the gate to the next map of the ring,
  // used when that map is installed too ("a link needs both of its maps").
  int maps_ = 0;  // (pkg::open, not pkg::find: this runs on the decoder task)
  for (size_t i = 0; i < maps.size(); i++) {
    pkg::File f;
    maps_ += roadFiles[i].length() && pkg::open(("vn.pawboop.map." + maps[i].id).c_str(), roadFiles[i].c_str(), f);
  }
  if (maps_) {
    part = "maps", half = false;
    skip = !media::fast();
    for (size_t i = 0; i < maps.size(); i++)
      maps[i].road = openOne("vn.pawboop.map." + maps[i].id, roadFiles[i], maps[i].pictures);
    for (size_t i = 0; i < links.size(); i++) {
      LinkInfo& l = links[i];
      l.road = maps[l.from].road >= 0 && maps[l.to].road >= 0
                   ? openOne("vn.pawboop.map." + maps[l.from].id, roadFiles[maps.size() + i], l.pictures)
                   : -1;
    }
  } else {
    // Run 1.0: one roads part in the run's own package (the store installs one of the group): roads20 (20 pictures a
    // second), roads10 (every 2nd picture), roads10s (roads10's maps without the link files).
    static const char* const PARTS[3] = {"roads20", "roads10", "roads10s"};
    part = "";
    for (const char* p : PARTS) {
      for (size_t i = 0; i < maps.size() && !*part; i++) {
        pkg::File f;
        if (roadFiles[i].length() && pkg::open(id, (String(p) + "/" + roadFiles[i]).c_str(), f)) part = p;
      }
      if (*part) break;
    }
    if (!*part) return err = T_("no roads installed"), false;
    half = strcmp(part, "roads20") != 0;
    skip = !half && !media::fast();
    for (size_t i = 0; i < maps.size(); i++) maps[i].road = openOne(id, String(part) + "/" + roadFiles[i], maps[i].pictures);
    for (size_t i = 0; i < links.size(); i++) {
      LinkInfo& l = links[i];
      l.road = maps[l.from].road >= 0 && maps[l.to].road >= 0
                   ? openOne(id, String(part) + "/" + roadFiles[maps.size() + i], l.pictures)
                   : -1;
    }
  }
  if (firstMap() < 0) return err = T_("no roads installed"), false;
  // The next map installed but no link file between them (roads10s, a missing file, a map left out): the fade.
  for (size_t i = 0; i < maps.size(); i++) {
    const int nx = nextMap(i);
    if (maps[i].road < 0 || nx == (int)i || link(i, nx)) continue;
    LinkInfo l;
    l.from = i, l.to = nx, l.pictures = fadeN, l.at = fadeAt, l.fade = true;
    links.push_back(l);
  }
  return true;
}

void World::freeRoads() {
  for (media::Road& r : roads) r.free();
  nroads = 0;
  size_t k = 0;  // the fades are made again by openRoads()
  for (const LinkInfo& l : links)
    if (!l.fade) links[k++] = l;
  links.resize(k);
}

int World::mapIndex(const char* id) const {
  for (size_t i = 0; i < maps.size(); i++)
    if (maps[i].id == id) return i;
  return -1;
}

int World::firstMap() const {
  for (size_t i = 0; i < maps.size(); i++)
    if (maps[i].road >= 0) return i;
  return -1;
}

int World::nextMap(int m) const {  // the page's nextMap over the installed maps
  const int n = maps.size();
  for (int k = 1; k <= n; k++) {
    const int i = (m + k) % n;
    if (maps[i].road >= 0) return i;
  }
  return m;
}

const LinkInfo* World::link(int a, int b) const {
  for (const LinkInfo& l : links)
    if (l.from == a && l.to == b && (l.road >= 0 || l.fade)) return &l;
  return nullptr;
}

int World::key(int road, int pic) const {
  if (road < 0) return -1;
  int i = half ? pic / 2 : skip ? pic & ~1 : pic;
  if (i >= roads[road].n) i = roads[road].n - 1;
  return road * 4096 + max(0, i);
}

int World::fadeKey(int a, int b, int i, int n) const {
  const MapInfo &A = maps[a], &B = maps[b];
  if (A.road < 0 || B.road < 0 || n <= 0) return -1;
  if (half || skip) i &= ~1;  // one picture for two steps: the same mix for both
  i = max(0, min(n - 1, i));
  const int pa = i % A.pictures, pb = ((B.pictures - n + i) % B.pictures + B.pictures) % B.pictures;
  const int ka = key(A.road, pa) & 4095, kb = key(B.road, pb) & 4095;
  const int alpha = (255 * (i + 1) + (n + 1) / 2) / (n + 1);
  return media::fadeKey(A.road, ka, B.road, kb, alpha);
}

bool World::tooHigh(const pbs2_clip_t* c, int i) const {
  if (c == cJump && c->count == 21) return JUMP_CM[i] >= 110;
  const pbs2_frame_t* f = dog->frame(c, i);  // another pack: the shadow shrinks with her height (sr = 30 x
  return f && f->sr && (1.0 - f->sr / 30.0) / 0.3 >= 1.1;  // (1 - 0.3 x height))
}

// ------------------------------------------------------------------ the game
void Game::begin(const World* w, int start) {
  W = w;
  startMap = start;
  map = start;
  seg = loopSeg(start);
  segD0 = 0;
  clip = W->cRun;
  hearts = W->R.hearts;
  v = W->R.speed0;
  next = W->R.firstRow;
  state = MENU;
}

Seg Game::loopSeg(int m) const {
  Seg s;
  s.link = false, s.map = m, s.to = m, s.road = W->maps[m].road, s.n = W->maps[m].pictures;
  return s;
}

Seg Game::linkSeg(const LinkInfo& l) const {
  Seg s;
  s.link = true, s.fade = l.fade, s.map = l.from, s.to = l.to, s.road = l.road, s.n = l.pictures, s.at = l.at;
  return s;
}

int Game::segKey(const Seg& g, int i) const {
  return g.fade ? W->fadeKey(g.map, g.to, i, g.n) : W->key(g.road, max(0, i));
}

// [start, end, the map after] in metres of the link in front of her (false: no link)
bool Game::linkAhead(double& a, double& b, int& to) const {
  const double st = W->R.step;
  if (seg.link) {
    a = segD0, b = segD0 + seg.n * st, to = seg.to;
    return true;
  }
  const int nx = W->nextMap(seg.map);
  const LinkInfo* L = W->link(seg.map, nx);
  if (!seamless || !L) return false;
  a = segD0 + (double)seg.n * W->R.loopsAMap * st;
  b = a + L->pictures * st, to = nx;
  return true;
}

int Game::track() {
  const double st = W->R.step;
  for (int guard = 0; guard < 16; guard++) {
    const int i = (int)floor((dist - segD0) / st);
    if (!seg.link) {
      const int nx = W->nextMap(seg.map);
      const LinkInfo* L = W->link(seg.map, nx);
      if (seamless && state == RUN && L && i >= seg.n * W->R.loopsAMap) {
        segD0 += (double)seg.n * W->R.loopsAMap * st;
        seg = linkSeg(*L);
        trace::add("run: %s %s>%s at %.0f m", seg.fade ? "fade" : "link", W->maps[seg.map].id.c_str(),
                   W->maps[seg.to].id.c_str(), segD0);
        continue;
      }
      return W->key(seg.road, ((i % seg.n) + seg.n) % seg.n);
    }
    if (i >= seg.n) {
      segD0 += seg.n * st;
      seg = loopSeg(seg.to);
      map = seg.map;
      trace::add("run: map %s at %.0f m", W->maps[map].id.c_str(), segD0);
      continue;
    }
    if (i >= seg.at) map = seg.to;  // through the gate: the next map's things from here
    return segKey(seg, i);
  }
  return W->key(seg.road, 0);
}

int Game::ahead(double d, int* keys, int n) const {
  const double st = W->R.step;
  Seg g = seg;
  double d0 = segD0;
  int got = 0;
  for (int guard = 0; got < n && guard < 64; guard++, d += st) {
    int key = -1;
    for (int k = 0; k < 8; k++) {  // as track(), on copies
      const int i = (int)floor((d - d0) / st);
      if (!g.link) {
        const int nx = W->nextMap(g.map);
        const LinkInfo* L = W->link(g.map, nx);
        if (seamless && state == RUN && L && i >= g.n * W->R.loopsAMap) {
          d0 += (double)g.n * W->R.loopsAMap * st;
          g = linkSeg(*L);
          continue;
        }
        key = W->key(g.road, ((i % g.n) + g.n) % g.n);
        break;
      }
      if (i >= g.n) {
        d0 += g.n * st;
        g = loopSeg(g.to);
        continue;
      }
      key = segKey(g, i);
      break;
    }
    if (key >= 0 && (!got || keys[got - 1] != key)) keys[got++] = key;
  }
  return got;
}

void Game::reset() {
  const Rules& R = W->R;
  state = RUN, dist = 0, v = R.speed0, t = 0, lane = 0, x = 0, x0 = 0, xt = 1, clip = W->cRun, ct = 0;
  hearts = R.hearts, safe = 0, coins = 0, things.clear(), next = R.firstRow;
  nextHeart = R.heartEvery[0] + rnd() * (R.heartEvery[1] - R.heartEvery[0]);
  overT = 0, ghost = nullptr, ghostLeft = 0, map = startMap, seg = loopSeg(startMap), segD0 = 0;
  paidOut = false;
}

const pbs2_frame_t* Game::dogFrame() const { return dogFrameAt(0); }

const pbs2_frame_t* Game::dogFrameAt(double ahead) const {
  const int n = clip->count;
  if (clip == W->cRun) return W->dog->frame(clip, (int)floor((runT + ahead) * W->fps) % n);
  int i = (int)floor((ct + ahead) * W->fps);
  if (clip->loop) i %= n;
  return W->dog->frame(clip, min(n - 1, i));
}

void Game::setClip(const pbs2_clip_t* c) {  // the pose before fades over two pictures
  if (!c || c == clip) return;
  ghost = dogFrame(), ghostLeft = 2;
  clip = c, ct = 0;
}

void Game::act(Act a) {
  if (state == MENU || (state == OVER && overT > 1.2)) {
    if (a == TAP) reset();
    return;
  }
  if (state != RUN) return;
  const bool busy = clip == W->cJump || clip == W->cSlide || clip == W->cStumble;
  if (a == LEFT || a == RIGHT) {
    const int l = constrain(lane + (a == LEFT ? -1 : 1), -1, 1);
    if (l == lane) return;
    lane = l, x0 = x, xt = 0;
    if (!busy) {
      clip = W->cRun;
      setClip(a == LEFT ? W->cLaneL : W->cLaneR);
    }
  } else if (a == UP && !busy) {
    setClip(W->cJump);
  } else if (a == DOWN && !busy) {
    setClip(W->cSlide);
  }
}

void Game::put(int type, int ln, double z) {
  if (type < 0) return;
  Thing t;
  t.type = type, t.lane = ln, t.z = z, t.kind = W->things[type].kind;
  things.push_back(t);
}

void Game::spawnRow() {
  const Rules& R = W->R;
  const double z = next;
  double la0 = 0, la1 = 0;
  int laTo = 0;
  const bool la = linkAhead(la0, la1, laTo);
  const bool inLink = la && z > la0 - 10 && z < la1 + 6;  // the gate's stretch: coins only
  const MapInfo& K = W->maps[la && z >= la1 ? laTo : map];
  const double r = inLink ? 1 : rnd();
  int lanes[3] = {-1, 0, 1};
  for (int i = 2; i > 0; i--) {  // shuffled
    const int j = (int)(rnd() * (i + 1));
    std::swap(lanes[i], lanes[j]);
  }
  auto kind = [&](std::initializer_list<int> a) {  // a random thing of a random kind from a (weights by repeats)
    std::vector<int> ok;
    for (int k : a)
      if (!K.kinds[k].empty()) ok.push_back(k);
    if (ok.empty()) return -1;
    return pickOf(K.kinds[pickOf(ok)]);
  };
  std::vector<int> fr(lanes, lanes + 3);
  if (r < 0.42) {
    put(kind({JUMP, JUMP, SLIDE, BLOCK, BLOCK}), lanes[0], z);
    fr.assign(lanes + 1, lanes + 3);
  } else if (r < 0.76) {
    put(kind({JUMP, BLOCK, BLOCK}), lanes[0], z);
    put(kind({JUMP, SLIDE, BLOCK}), lanes[1], z);
    fr.assign(lanes + 2, lanes + 3);
  } else if (r < 0.88) {  // a wall: over it or under it
    const int n = kind({JUMP, JUMP, SLIDE});
    for (int l : lanes) put(n, l, z);
    fr.clear();
  }
  const std::vector<int> all(lanes, lanes + 3);
  if (hearts < R.heartsMax && z >= nextHeart && W->heart >= 0) {  // a life on the road, in a lane she can run
    put(W->heart, fr.size() ? pickOf(fr) : pickOf(all), z + (fr.size() ? 0 : 4.5));
    nextHeart = z + R.heartEvery[0] + rnd() * (R.heartEvery[1] - R.heartEvery[0]);
  } else if (rnd() < 0.65) {
    const int l = fr.size() ? pickOf(fr) : pickOf(all);
    for (int k = 0; k < 5; k++) put(W->coin, l, z + (fr.size() ? -3.2 : 4) + k * 1.6);
  }
  next = z + max(R.gapMin, R.gap0 - R.gapPer100 * dist / 100) + rnd() * 3;
}

Game::Pay Game::pay() const {
  const int base = coins + (int)floor(dist / W->R.metresACoin);
  const int bonus = healthy && healthy() ? (int)ceil(base * W->R.healthyBonus) : 0;
  return {base, bonus, base + bonus};
}

void Game::payNow() {
  if (paidOut || noPay || state != RUN) return;
  paidOut = true;
  const Pay p = pay();
  if (paid && p.total > 0) paid(p.total);
}

void Game::endRun() {
  if (state != RUN) return;
  hearts = 1, safe = 0;
  hit();
}

void Game::hit() {
  hearts--;
  if (hearts > 0) {
    safe = W->R.safeAfterHit;
    setClip(W->cStumble);
  } else {
    state = OVER;
    overT = 0;
    setClip(W->cDizzy);
    const Pay p = pay();
    last.dist = dist, last.base = p.base, last.bonus = p.bonus, last.total = p.total;
    best = max(best, dist);
    if (!paidOut && !noPay) {
      paidOut = true;
      if (paid && p.total > 0) paid(p.total);
    }
  }
}

// One picture's worth of game: the same step drives the pictures and the rules.
void Game::step(double dt) {
  const Rules& R = W->R;
  t += dt, pic++, runT += dt;
  if (state == OVER) {
    overT += dt;
    v = max(0.0, v - 16 * dt);
  } else if (state == RUN) {
    v = ramp ? min(R.speedMax, R.speed0 + R.speedPer100 * dist / 100) : R.speed0;
  } else {
    v = R.speed0 * 0.75;  // the start card: she jogs along
  }
  dist += v * dt;
  if (xt < 1) {
    xt = min(1.0, xt + dt / R.laneTime);
    const double u = xt * xt * (3 - 2 * xt);
    x = x0 + (lane - x0) * u;
  }
  ct += dt;
  if (!clip->loop && ct * W->fps >= clip->count) setClip(clip == W->cDizzy ? W->cDizzyLoop : W->cRun);
  if (safe > 0) safe -= dt;
  if (state != RUN) {
    if (state == OVER) {
      size_t k = 0;
      for (const Thing& th : things)
        if (th.z - dist > -4.2) things[k++] = th;
      things.resize(k);
    }
    return;
  }
  while (next - dist < 46) spawnRow();
  const pbs2_frame_t* f = dogFrame();
  const int fi = clip == W->cRun ? (int)floor(runT * W->fps) % clip->count
                                 : (clip->loop ? (int)floor(ct * W->fps) % clip->count
                                               : min(clip->count - 1, (int)floor(ct * W->fps)));
  const bool air = f->flags & 2, low = f->flags & 4, high = W->tooHigh(clip, fi);
  const int dogLane = (int)jsRound(x);
  for (Thing& th : things) {
    const double d = th.z - dist;
    if (th.hit || th.lane != dogLane || fabs(d) > W->things[th.type].halfLen + R.hitWindow) continue;
    if (th.kind == COIN || th.kind == HEART) {
      if (!high) {
        th.hit = true, th.gone = true;
        if (th.kind == COIN) coins++;
        else hearts = min(R.heartsMax, hearts + 1);
      }
      continue;
    }
    if ((th.kind == JUMP && air) || (th.kind == SLIDE && low)) continue;  // over it / under it: checked again
    th.hit = true;
    if (safe <= 0 && !god) {
      trace::add("run: hit by %s at %.2f m (%.16s picture %d%s%s)", W->things[th.type].id.c_str(), d, clip->name, fi,
                 air ? ", in the air" : "", low ? ", low" : "");
      hit();
    }
    if (state != RUN) break;
  }
  size_t k = 0;
  for (const Thing& th : things)
    if (th.z - dist > -4.2 && !th.gone) things[k++] = th;
  things.resize(k);
}

// The picture of a thing at its distance: the nearest recorded one (pictures go from far to near).
const pbs2_frame_t* Game::thingFrame(const Thing& t, double d) const {
  const pbs2_clip_t* c = W->things[t.type].clip[1 - t.lane];
  const media::Pack* P = W->th;
  const int n = c->count;
  if (!n || d * 100 > P->frame(c, 0)->sx + 50) return nullptr;
  int a = 0, b = n - 1;
  while (a < b) {
    const int m = (a + b) >> 1;
    if (P->frame(c, m)->sx > d * 100) a = m + 1;
    else b = m;
  }
  if (d * 100 < P->frame(c, n - 1)->sx - 30) return nullptr;  // past its last picture: it left the screen
  const pbs2_frame_t* fa = P->frame(c, a);
  if (a > 0) {
    const pbs2_frame_t* fp = P->frame(c, a - 1);
    if (fabs(fp->sx - d * 100) < fabs(fa->sx - d * 100)) return fp;
  }
  return fa;
}

}  // namespace run
