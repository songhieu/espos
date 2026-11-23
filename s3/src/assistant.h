#pragma once
// The assistant slot (docs/app-slots.md §5, §7, §16): which installed app answers the wake word, push to talk and the
// Control Center's assistant tile. Every assistant is an app like any other, ours included: removable, reinstallable.
//
// Setting `slot.assistant` (per device, synced with the web like every setting): a package id, or "none". Unset: the
// PawBoop dog (vn.pawboop.pet) when it is installed, else "none". The device's one assistant engine today is the dog's
// (its pop-up and turn: src/pet/summon.cpp, talk.cpp); a slot naming an app the device cannot run counts as "none"
// here until the OS assistant client takes the turn over (§5.3).
//
// "none": the wake word does nothing (the listener stops listening: src/wake/wake.cpp), and calling the assistant
// (the Control Center tile) shows "No assistant installed" with the way to the Store's Assistants. Removing the slot's
// app sets it to "none" and posts one quiet notification; installing an assistant while the slot is "none" makes it
// the assistant (a reinstalled dog comes back as it was bought: entitlements live on the account, lock.h).
// UI loop only.
#include <Arduino.h>
#include <lvgl.h>

#include <vector>

#include "pkg/pkg.h"

namespace assistant {

constexpr const char* DOG = "vn.pawboop.pet";
constexpr const char* KEY = "slot.assistant";

// What shows a turn (turn.h) and speaks its small lines: the slot app's face. PawBoop's is the dog's pop-up
// (pet::face(), src/pet/summon.cpp: its clips, recorded lines and voice); any other assistant app gets the OS pop-up
// (here: its icon and name, the words, the mic button).
struct Face {
  bool (*open)();                                                          // over the screen (wakes it); false: cannot
  void (*show)(const char* mood, const String& big, const String& small);  // a mood or clip (nullptr: as is), the words
  void (*level)(int pct);                                                  // the mic button's ring, < 0: none
  void (*button)(int state);  // 0 ready, 1 recording (held), 2 busy, 3 recording after the wake word (a tap ends it)
  void (*touched)();          // keeps it open
  void (*lock)(const char* url);  // talking is locked: the code that unlocks it in place of the button
  void (*trial)(int left);        // "{n} talks left" of a trial (< 0: none)
  // Its own recorded words for a moment or a clip ("ack", "alert", "no_tired", "ai_locked", a clip the server named),
  // playing now: their length in ms; 0: none (the words alone, or a beep).
  uint32_t (*say)(const char* what);
  bool (*speaking)();
  void (*hush)();
  uint32_t (*quietMs)();  // since its own voice last played
  String (*voice)();      // the voice the server speaks with (the request's "dog"); "" = the server's own choice
};
const Face& face();  // the slot app's (the OS pop-up when there is none: talking then says how to get one)

String id();       // the slot's app (installed, one the device can run), or "none"
bool any();        // an assistant is installed and chosen
bool dog();        // it is the PawBoop dog
bool runs(const char* pkgId);  // an installed assistant app the device can call (PawBoop, or "provides": ["assistant"])
std::vector<String> installed();  // every assistant app on the device (Settings › Default apps)
String name();     // what the Control Center's tile says: the dog's own name; "" when none
bool listens();    // the wake word may listen for it (the listener only listens for the slot's app)
bool open();       // its pop-up (the dog's or the OS's, or the "No assistant installed" card) is over the screen
bool busy();       // a turn is on: listening, sending, waiting or speaking
void summon(const char* reason);  // its pop-up, or the "No assistant installed" card
void noneCard();   // "No assistant installed": get one in the Store
void closeCard();
// The OS's microphone glyph (no art for it: LVGL boxes), about 18 x 26 px, centred in `parent`.
lv_obj_t* micIcon(lv_obj_t* parent, uint32_t color);

// The store's jobs (store.cpp afterJob, UI loop): a package went or came. A removed package's own device data goes as
// any app's does (the dog's game and moments, s3.pet_dog, its notifications; the run game's best); the account keeps
// what the dogs remember.
void removed(const char* pkgId, const String& name, const String& addonOf);
void installed(const pkg::Package& p);

}  // namespace assistant
