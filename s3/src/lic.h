#pragma once
// The account's licence as the server tells it (2026-11-19: espos.dev's trial, 7 days or 50 AI turns, then the paid
// features lock). device_sync carries "lic": {"ai": "on" | "trial" | "off" | "byok", "ai_left": n, "buy_url": "…",
// …}; POST /v1/device/assist answers 402 {"locked": "trial_ended" | "no_ai", "buy_url": "…"} when the AI is not
// allowed. No "lic" at all (a self-hosted or older server) = None: everything works as before, nothing locks.
// Kept in NVS ("lic"), so a lock holds through a restart without the network. The talk (pet/talk.cpp) and the
// store and pickers (tank-game's lock UI) read it; lock::sheet shows the way to unlock.
#include <Arduino.h>
#include <ArduinoJson.h>

namespace lic {

enum class Ai : uint8_t { None, On, Trial, Off, Byok };

void begin();                         // the last state from NVS (every call below does it the first time)
Ai ai();
inline bool aiLocked() { return ai() == Ai::Off; }
int aiLeft();                         // turns left while Trial; -1 = not told
const String& buyUrl();               // where the person unlocks it (shown as a QR); "" = not told
const char* lockedWhy();              // "trial_ended" | "no_ai" | "" (the last 402's reason)
const String& raw();                  // the last "lic" object as JSON ("" = none): other fields for other screens
uint32_t version();                   // changes with every change (screens that show it redraw)

void fromSync(JsonVariantConst lic);  // device_sync's "lic" (null: None)
void locked(const char* why, const char* buyUrl);  // a 402 between two syncs: Off until a sync says otherwise
void usedTurn();                      // an answered turn: ai_left - 1 until the next sync tells the real count

// Tests and demos: "LIC <json>" | "LIC none" | "LIC?" (reached through tank-game's "LOCK lic ..." console command).
bool console(const char* cmd);

}  // namespace lic
