#pragma GCC optimize("Os")  // read once a sync, nothing hot; the firmware must fit (0.5.7 flash diet)
#include "lic.h"

#include <Preferences.h>

#include "trace.h"

namespace lic {
namespace {

Ai ai_ = Ai::None;
int left_ = -1;
String url_, raw_;
char why_[16] = "";
uint32_t ver_ = 1;
bool loaded = false;

Ai parseAi(const char* s) {
  if (!s) return Ai::None;
  if (!strcmp(s, "on")) return Ai::On;
  if (!strcmp(s, "trial")) return Ai::Trial;
  if (!strcmp(s, "off")) return Ai::Off;
  if (!strcmp(s, "byok")) return Ai::Byok;
  return Ai::On;  // a word this firmware does not know: not locked
}

const char* name(Ai a) {
  switch (a) {
    case Ai::On: return "on";
    case Ai::Trial: return "trial";
    case Ai::Off: return "off";
    case Ai::Byok: return "byok";
    default: return "none";
  }
}

void save() {
  Preferences p;
  if (!p.begin("lic", false)) return;
  p.putInt("ai", (int)ai_);
  p.putInt("left", left_);
  p.putString("url", url_);
  p.putString("why", why_);
  p.putString("raw", raw_);
  p.end();
}

void changed(const char* from) {
  ver_++;
  save();
  trace::add("lic: ai %s, %d left%s%s (%s)", name(ai_), left_, *why_ ? ", " : "", why_, from);
}

}  // namespace

void begin() {
  if (loaded) return;
  loaded = true;
  Preferences p;
  if (!p.begin("lic", true)) return;  // never written: None
  ai_ = (Ai)p.getInt("ai", 0);
  if ((uint8_t)ai_ > (uint8_t)Ai::Byok) ai_ = Ai::None;
  left_ = p.getInt("left", -1);
  url_ = p.getString("url", "");
  raw_ = p.getString("raw", "");
  strlcpy(why_, p.getString("why", "").c_str(), sizeof why_);
  p.end();
}

Ai ai() { begin(); return ai_; }
int aiLeft() { begin(); return left_; }
const String& buyUrl() { begin(); return url_; }
const char* lockedWhy() { begin(); return why_; }
const String& raw() { begin(); return raw_; }
uint32_t version() { return ver_; }

void fromSync(JsonVariantConst lic) {
  begin();
  Ai a = Ai::None;
  int left = -1;
  String url, raw;
  if (lic.is<JsonObjectConst>()) {
    a = parseAi(lic["ai"] | "on");
    left = lic["ai_left"].is<int>() ? lic["ai_left"].as<int>() : -1;
    url = lic["buy_url"] | "";
    serializeJson(lic, raw);
  }
  if (a == ai_ && left == left_ && raw == raw_) return;
  const Ai was = ai_;
  ai_ = a, left_ = left, raw_ = raw;
  if (url.length() || a == Ai::None) url_ = url;  // else a 402's address stays until a sync sends one
  if (a != Ai::Off) why_[0] = 0;
  else if (!*why_) strlcpy(why_, was == Ai::Trial || left == 0 ? "trial_ended" : "no_ai", sizeof why_);
  changed("sync");
}

void locked(const char* why, const char* url) {
  begin();
  ai_ = Ai::Off;
  strlcpy(why_, why && *why ? why : "no_ai", sizeof why_);
  if (url && *url) url_ = url;
  if (left_ > 0) left_ = 0;
  changed("402");
}

void usedTurn() {
  begin();
  if (ai_ != Ai::Trial || left_ <= 0) return;
  left_--;
  ver_++;  // not saved: the next sync tells the real count
}

bool console(const char* cmd) {
  if (!strcmp(cmd, "LIC?")) {
    Serial.printf("LIC ai %s left %d why '%s' url '%s' raw %s\n", name(ai_), left_, why_, url_.c_str(), raw_.c_str());
    return true;
  }
  if (strncmp(cmd, "LIC ", 4)) return false;
  const char* a = cmd + 4;
  if (!strcmp(a, "none")) {
    fromSync(JsonVariantConst());
    return true;
  }
  JsonDocument d;
  if (deserializeJson(d, a)) return Serial.println("LIC: not JSON"), true;
  fromSync(d.as<JsonVariantConst>());
  return true;
}

}  // namespace lic
