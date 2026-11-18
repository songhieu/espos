#pragma once
// The run's rules and state: a port of the reference page (pawboop_run_handoff/sim/run.html: R, act,
// spawnRow, step, hit, pay, track, linkAhead, dogFrame, setClip, thingFrame), numbers from run.json.
// Plain logic, no drawing. Doubles as the page's JavaScript numbers (same rounding, e.g. the +10 % bonus).
#include <Arduino.h>

#include <vector>

#include "media.h"

namespace run {

enum Kind : uint8_t { JUMP = 0, SLIDE = 1, BLOCK = 2, COIN = 3, HEART = 4 };

struct Rules {  // run.json -> rules, geometry (the page's R)
  double speed0 = 8, speedMax = 12, speedPer100 = 0.5, laneTime = 0.17;
  int hearts = 2, heartsMax = 3;
  double safeAfterHit = 1.5, heartEvery[2] = {130, 200};
  double firstRow = 26, gap0 = 15, gapMin = 9.5, gapPer100 = 0.6;
  int loopsAMap = 3;
  double metresACoin = 25, healthyBonus = 0.1, hitWindow = 0.35;
  double step = 0.4;  // metres a road picture (20 a second)
  int wide = 464;
  double dogX = 232, dogY = 385.5, lanePx = 153, panPx = 69;
};

struct ThingType {
  String id;  // "crate_wood"
  Kind kind = BLOCK;
  double halfLen = 0.4;
  const pbs2_clip_t* clip[3] = {};  // by clip lane: 0 = screen right, 1 middle, 2 left
};

struct MapInfo {
  String id;
  std::vector<int> kinds[3];  // JUMP, SLIDE, BLOCK -> ThingType indexes (run.json maps.<id>.things)
  int road = -1;              // installed road file (index in World::roads), -1 = not installed
  int pictures = 160;         // its loop, in 0.4 m pictures
};

struct LinkInfo {
  int from = -1, to = -1, road = -1, pictures = 0, at = 0;
  bool fade = false;  // no file: the road fades from one map's pictures into the next one's (as carnival>bazaar)
};

struct World {  // run.json + the packs + the installed roads
  Rules R;
  std::vector<ThingType> things;
  std::vector<MapInfo> maps;  // run.json "order"
  std::vector<LinkInfo> links;
  std::vector<String> roadFiles;  // run.json roads.<key>.file: the maps in order, then the links
  media::Road roads[16];  // the maps' roads, then the gates between them
  int nroads = 0;
  bool half = false;  // roads10 / roads10s: one picture for two 0.4 m steps
  // roads20 with TJpgDec (the simulator): every 2nd picture too (a road picture took it about 57 ms plus 9 ms from
  // the card on the board: 20 a second never kept up). Espressif's decoder (the board): every picture, 24 ms each.
  bool skip = false;
  const char* part = "";  // "maps" (each map its own package), or a run 1.0 roads part: "roads20", "roads10", "roads10s"
  int fadeN = 20, fadeAt = 10;  // a link with no file: run.json's fade (carnival>bazaar), 20 pictures = 1 s
  const media::Pack* dog = nullptr;
  const media::Pack* th = nullptr;
  int coin = -1, heart = -1;  // ThingType indexes
  const pbs2_clip_t *cRun = nullptr, *cJump = nullptr, *cSlide = nullptr, *cLaneL = nullptr, *cLaneR = nullptr,
                    *cStumble = nullptr, *cDizzy = nullptr, *cDizzyLoop = nullptr;
  int fps = 20;  // the dog's and the things' pictures were cut for 20 a second

  // run.json; then the roads: every installed map package (vn.pawboop.map.<map>: its road and the gate to the next
  // map, used when that map is installed too), else a run 1.0 roads part (roads20, else roads10). err: what is missing.
  bool parse(const char* json, size_t len, String& err);
  bool openRoads(const char* pkgId, String& err);
  void freeRoads();
  int mapIndex(const char* id) const;
  int nextMap(int m) const;  // the next installed map in the order (itself when alone)
  const LinkInfo* link(int a, int b) const;
  int firstMap() const;
  int key(int road, int pic) const;  // media key of a 0.4 m picture of a road file
  // Media key of picture i (0.4 m steps) of the fade from map a to map b: a's picture i into b's picture
  // (b's loop - n + i), weight (i + 1) / (n + 1); it ends into b's first picture.
  int fadeKey(int a, int b, int i, int n) const;
  bool tooHigh(const pbs2_clip_t* c, int i) const;  // above 1.1 m: she flies over coins and hearts
};

struct Thing {
  int type;
  int lane;  // screen lane -1 left, 0, +1 right
  double z;  // metres from the run's start
  Kind kind;
  bool hit = false, gone = false;
  const pbs2_frame_t* shown = nullptr;  // the picture drawn last (stands in while the next one is decoded)
};

enum State : uint8_t { MENU, RUN, OVER };
enum Act : uint8_t { TAP, LEFT, RIGHT, UP, DOWN };

struct Seg {
  bool link = false, fade = false;
  int map = 0, to = 0, road = -1, n = 160, at = 0;
};

struct Game {
  const World* W = nullptr;
  bool seamless = true, ramp = true;
  bool god = false;    // dev: hits do not count
  bool noPay = false;  // dev and bench runs: no coins to the pet
  bool (*healthy)() = nullptr;        // the pet's four needs average 70 or more
  void (*paid)(int coins) = nullptr;  // the end of a run: the coins go to the pet game
  int startMap = 0;

  State state = MENU;
  double dist = 0, v = 8, t = 0, x = 0, x0 = 0, xt = 1, ct = 0, runT = 0, safe = 0;
  int lane = 0, hearts = 2, coins = 0;
  const pbs2_clip_t* clip = nullptr;
  std::vector<Thing> things;
  double next = 26, nextHeart = 150, overT = 0, best = 0;
  struct {
    double dist = 0;
    int base = 0, bonus = 0, total = 0;
  } last;
  uint32_t pic = 0;
  const pbs2_frame_t* ghost = nullptr;  // the pose before a clip change, fading over two pictures
  int ghostLeft = 0;
  int map = 0;
  Seg seg;
  double segD0 = 0;
  bool paidOut = true;  // this run's coins went to the pet

  void begin(const World* w, int start);
  void reset();
  void act(Act a);
  void step(double dt);
  // The road picture for this distance (moves on to a link, then to the next map): media key.
  int track();
  // The road pictures from distance d on, without moving the road: keys of the next n distinct pictures.
  int ahead(double d, int* keys, int n) const;
  const pbs2_frame_t* dogFrame() const;
  const pbs2_frame_t* dogFrameAt(double ahead) const;  // her picture `ahead` seconds from now (no clip change)
  const pbs2_frame_t* thingFrame(const Thing& t, double d) const;  // d = metres in front of her
  void payNow();  // leaving in the middle of a run: what was earned so far goes to the pet
  void endRun();  // dev: every life lost now
  struct Pay {
    int base, bonus, total;
  };
  Pay pay() const;

 private:
  Seg loopSeg(int m) const;
  Seg linkSeg(const LinkInfo& l) const;
  int segKey(const Seg& g, int i) const;  // the media key of picture i of a link segment
  bool linkAhead(double& a, double& b, int& to) const;
  void setClip(const pbs2_clip_t* c);
  void spawnRow();
  void hit();
  void put(int type, int lane, double dz);
};

double rnd();  // 0 <= x < 1 (Math.random)

}  // namespace run
