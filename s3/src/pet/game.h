#pragma once
// PawBoop's rules (game.json -> rules, the page's G): needs that fall in real time, moods, taste and food
// tiers, XP and levels, coins. Only the numbers and the state live here; the screen is app.cpp.
// Saved in NVS (Preferences "pet") after every player action and every 5 minutes.
#include <stdint.h>

namespace pet {
namespace game {

enum Need { FOOD = 0, FUN, ENERGY, CLEAN };

// game.json -> rules (do not change without the owner)
struct Rules {
  float drift[4] = {5, 4, 3, 2.5f};  // points an hour
  int low = 30, sleepy = 21, playNo = 15, happyAt = 50;
  int price[4] = {0, 20, 60, 150}, foodLevel[4] = {1, 1, 3, 6};
  // feed by verdict: meh, ok, love -> food, fun, xp
  int feed[3][3] = {{15, 0, 3}, {45, 4, 8}, {55, 15, 14}};
  int xpAt[12] = {0, 60, 150, 280, 450, 680, 980, 1350, 1800, 2400, 3100, 4000};
  int coin = 5;
  int playCoins[3][2] = {{50, 4}, {80, 3}, {101, 1}};
  int coinLife = 8;               // seconds a dropped coin waits
  int findEvery[2] = {480, 720};  // a happy dog brings a coin every 8-12 min
  int findMax = 3;
  int levelGift = 50, dayGift = 50;
  int touchXpDay = 20;
};
extern const Rules G;

// what an action does: food, fun, energy, clean, xp
struct Effect {
  int food, fun, energy, clean, xp;
};
extern const Effect PLAY, PET, WASH, NAP, TAP;
extern const char* const TAP_CLIPS[4];

struct State {
  float need[4] = {75, 70, 85, 80};  // start.board
  int coins = 50, xp = 0, lv = 1;
  int touchXp = 0;
  uint32_t touchDay = 0;       // the day touchXp counts for
  uint32_t poweredMin = 0;     // minutes the board has been on, ever
  uint32_t giftDay = 0;        // yyyymmdd of the last day's gift (with a clock)
  uint32_t giftPowered = 0;    // poweredMin at the last gift (without a clock)
  int found = 0;               // coins she found that wait on the floor (find_max at most)
  float findIn = 600;          // seconds to the next found coin
  int streak = 0, best = 0;    // days in a row she was cared for
  uint32_t careDay = 0;        // yyyymmdd of the last care
  bool muted = false;          // her voice off (bubbles stay)
  bool notify = true;          // tell the user when she needs something (respects Do Not Disturb)
  bool sfx = true;             // the game's sounds (sfx.h), the book's "Game sounds"
  bool music = true;           // its music, the book's "Background music"
  // to the screen
  bool levelUp = false;        // a level was reached: the screen floats +50 and she says @levelup when quiet
  bool gift = false;           // the day's gift was paid: +50 and @gift
};
State& s();

void load();
void forget();        // PawBoop was removed: her save in NVS goes, a new dog's state from now (pet::uninstalled)
void save();          // now
void saveSoon();      // within a second (after a player action)
void advance(uint32_t ms, bool alone);  // time passes: needs fall, powered time, she may find a coin

// rules
int levelOf(int xp);
float xpFrac();           // to the next level, 0..1
int taste();              // the food tier she expects now
int verdict(int tier);    // 0 meh, 1 ok, 2 love
const char* moodRole();   // "hungry", "sleepy", "bored" or "idle" (the first that is true)
bool happy();             // all four needs at happy_at or more
void apply(const Effect& e, bool touch);  // touch: XP counts against the day's cap
void feedEffect(int verdict);
void addXp(int n);
void addCoins(int n);
void cared();             // a player action today (the streak)
bool giftDue();           // the day's gift is waiting; pays it (sets gift) and returns true
int playDrop();           // coins a PLAY drops now (by fun before the play)

uint32_t today();         // yyyymmdd, 0 when the clock is not set
float speed();            // 1 = real time (PAWBOOP_SPEED in the simulator speeds it up)

// "Moments": the album of firsts (a level, a meal she loved, a bath...), drawn from a clip's picture.
struct Moment {
  uint8_t kind;   // see book.cpp
  uint8_t arg;    // a level, a streak length...
  uint8_t clip;   // clip index in the pack
  uint8_t frame;  // picture in the clip
  uint32_t day;   // yyyymmdd (0 = no clock)
};
int moments(Moment* out, int max);
bool hasMoment(int kind, int arg);
void addMoment(int kind, int arg, const char* clip, int frame);

}  // namespace game
}  // namespace pet
