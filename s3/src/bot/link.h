#pragma once
// EspOS Bot and espos.dev: device_sync (kind "bot", board "bot128": its bot.* keys, a small weather, the lunar date,
// the licence, firmware offers by bot.ota), the ping that says when the web changed something, the claim of a unit
// for sale (401 not_linked until its owner enters the code), the dog's turns (POST /v1/device/assist?voice=0: the
// JSON answer only) and updates. HTTPS runs on its own task, so the screen never stops; results are taken here, on
// the main loop.
#include <stdint.h>

#include <string>

#include "ui.h"

namespace bot {
namespace link {

// None: never provisioned (or the token was refused) and offline; Pairing: it pairs by itself over WiFi (a code to
// enter on espos.dev, as the S3 and the X4 do); Claim: a unit for sale waits for its claim code.
enum class State : uint8_t { None, Linked, Claim, Pairing };

void begin();
void loop();
State state();
bool blocked();      // the account is disabled (403 account_disabled)
void syncNow();
bool synced();       // a sync answered 200 since boot
uint32_t lastSyncAgoS();
std::string hwid();  // SHA-256 of "espos-hw:" + the MAC (12 hex): one trial per board
std::string mac4();

// ESPOS:PROVISION's JSON (the USB installer, Kho máy): token, api, now_ms, wifi[], settings, settings_rev, claim, ...
bool provision(const char* json, std::string& err);

// Self-pairing (no token, no claim code, on WiFi): the code on the OLED and its link (a QR), "" while it is asked
// for; justPaired() once, when the web confirmed it (the bot then greets).
std::string pairCode();
std::string pairUrl();
bool justPaired();

// The small weather and the lunar date from the last sync (kept through a restart? no: the next sync brings them).
bool weather(ui::Weather& w);
std::string lunar();
const ui::Info& dashInfo();  // the news, the gold price, the humidity (for the info screens)
uint32_t dashVersion();       // changes with every sync that brought them

// The dog: a turn's question, sent before anything else waiting. `wav` stays the caller's until takeAnswer() returns
// true (or cancelAsk()). false: a question is already on its way, or the bot is not provisioned.
// voice: the reply's voice streams back into the speaker (spk.h) after the words (bot.voice 1, the default).
bool ask(const uint8_t* wav, uint32_t len, const std::string& query, bool voice);
void cancelAsk();  // the answer is no longer wanted (it is dropped when it comes)
struct Answer {
  int status = 0;  // HTTP status; < 0 the connection failed (-1 connect, -2 send, -3 no answer)
  std::string body;  // the answer's JSON (with the voice: from its X-Assist header)
  uint32_t ms = 0;
  bool voice = false;  // its voice is playing
};
bool takeAnswer(Answer& a);
void warm();  // a fresh TLS connection now, for the question being spoken

// Updates: the offer from the last sync (bot.ota's channel); install() downloads, checks and restarts into it.
void install();
bool updating();
int updatePct();
std::string offerVersion();  // "" = none
bool offerNow();             // asked on the web ("Cập nhật"): at the next quiet moment, day or night

}  // namespace link
}  // namespace bot
