#pragma once
// Paid content and the trial on the device (2026-11-19; mockup mockups/s3-trial-lock.html).
// The server's "lic" (lic.h keeps it: plan, trial_end_ms, ent, buy_url) and the store catalog's marks ("paid": true,
// "free_ids": [...]) decide what may be used now. No "lic" (a self-hosted or older server) = nothing locks, exactly as
// before; a "lic" with nothing marked paid in the catalog locks nothing either.
// Nothing is ever removed: a locked package stays installed and its saves stay as they are, so everything comes back
// exactly as it was when the entitlement returns. The marks are kept in NVS ("lock"), so locks hold offline.
// Ids: a package ("vn.pawboop.dog.momo"), or a part of one, "<package>+<thing>": "vn.pawboop.tank+full" (all of it),
// "vn.pawboop.tank+guppy" (a thing the package's free_ids may list). The trial's end is the server's decision: when
// trial_end_ms passes on this clock the device syncs early, it never locks on its own (a purchase stays a purchase).
// UI loop only, except may()/locked(), which the run's decoder task also asks.
#include <Arduino.h>
#include <ArduinoJson.h>
#include <lvgl.h>

namespace lock {

enum class Plan : uint8_t { None, Free, Trial, Owned, Sub };
enum class Look : uint8_t { Os, Pet, Tank };  // the OS's pages, PawBoop's warm card (pet, run), the aquarium's glass

#if ESPOS_NO_LOCK  // an image without the trial and the locks (its size measured; a self-hosted build): nothing locks
inline Plan plan() { return Plan::None; }
inline bool trial() { return false; }
inline int daysLeft() { return 0; }
inline bool paid(const char*) { return false; }
inline bool may(const char*) { return true; }
inline bool locked(const char*) { return false; }
inline bool freeAnyway(const char*) { return true; }
inline bool bought(const char*) { return false; }
inline uint32_t version() { return 1; }
inline void catalog(JsonArrayConst) {}
inline void tick() {}
inline String chipText() { return String(); }
inline lv_obj_t* chip(lv_obj_t*, bool) { return nullptr; }
inline bool chipPicture(bool, uint16_t*&, uint8_t*&, int&, int&) { return false; }
inline lv_obj_t* badge(lv_obj_t*, int) { return nullptr; }
inline lv_obj_t* glyph(lv_obj_t*, int, uint32_t) { return nullptr; }
inline void sheet(const char*, Look) {}
inline bool sheetShown() { return false; }
inline void endCard() {}
inline bool dev(const char*) { return false; }
inline void account(const char*) {}
inline bool accountBlocked() { return false; }
#else
Plan plan();
bool trial();     // the trial runs: its chip shows (never on "owned" / "sub" devices)
int daysLeft();   // the trial's days left by the calendar: 0 = it ends today
bool paid(const char* id);    // the catalog marks it paid (a package, or the package of "<package>+<thing>")
bool may(const char* id);     // it may be used now
inline bool locked(const char* id) { return !may(id); }
bool freeAnyway(const char* id);  // free even without the trial or a purchase (else the store says "Try" / "Buy")
bool bought(const char* id);  // paid and bought ("Owned" in the store): owned / sub plans, or in ent outside a trial
uint32_t version();           // bumps on every change of the above (screens redraw)

void catalog(JsonArrayConst packages);  // the store's catalog came: its paid marks (store.cpp)
void tick();                            // UI loop: the notices, the end card, the early sync

// ---------------------------------------------------------------- what it looks like
String chipText();                            // "Trial · 3 days left" ("" when no trial)
lv_obj_t* chip(lv_obj_t* parent, bool onDark);  // that as a small pill; nullptr when no trial
// The chip drawn once for games that draw past LVGL (the aquarium, the run): colours + alpha in one PSRAM block
// (rgb, then w*h alpha bytes); the caller frees rgb with heap_caps_free. false when no trial.
bool chipPicture(bool onDark, uint16_t*& rgb, uint8_t*& alpha, int& w, int& h);
lv_obj_t* badge(lv_obj_t* parent, int size);  // the small dark lock on an icon's corner
lv_obj_t* glyph(lv_obj_t* parent, int size, uint32_t color);  // a padlock drawn in a size x size box
// "<name> is locked": the chosen dog explains kindly, the QR to buy_url, "Later". Over whatever is in front (on the
// top layer: the games drawn past LVGL let LVGL draw while it is up); closes with its screen.
void sheet(const char* name, Look look);
bool sheetShown();
void endCard();  // the trial's end: what stays free, what is locked now, the QR, "Later" (once, by tick())

bool dev(const char* cmd);  // "lic {json}" | "lic none" | "card" | "account STATE|ok" (board); "sheet NAME" ... (sim)

// A blocked account (admin-users' contract, 2026-11-19): every device route answers 403 {"error": "account_disabled",
// "state": "blocked" | "deleting" | "deleted"}. desk.cpp (and the assistant, talk.cpp) report it here: a calm full
// screen over Home says so ("Your account is locked" / "...is being deleted", "Contact support@espos.dev"); the token
// is kept, syncing goes on at its pace, and the first 200 of a sync or a ping ends it (account(nullptr)). Kept in RAM
// only: after a restart the next sync tells again.
void account(const char* state);
bool accountBlocked();
#endif

}  // namespace lock
