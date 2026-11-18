#include "EspOSSettings.h"

#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "CrossPointSettings.h"
#include "FontSync.h"

namespace espos::settings {
namespace {

constexpr const char* MAPPED_KEYS[] = {"sys.lang",   "sys.sleep_min", "read.size",   "read.spacing", "read.font",
                                       "read.align", "read.margin",   "read.indent", "read.para",    "read.hyphen",
                                       "read.aa",    "read.refresh",  "read.status", "read.images",  "sys.pwr_short",
                                       "sys.side",   "sys.front",     "sys.orient",  "sys.clock_fmt"};

// Name/value tables for the enum settings the web edits.
struct Opt {
  const char* name;
  uint8_t value;
};
constexpr Opt ALIGN_OPTS[] = {{"justify", CrossPointSettings::JUSTIFIED}, {"left", CrossPointSettings::LEFT_ALIGN}};
constexpr Opt REFRESH_OPTS[] = {{"1", CrossPointSettings::REFRESH_1},
                                {"5", CrossPointSettings::REFRESH_5},
                                {"10", CrossPointSettings::REFRESH_10},
                                {"15", CrossPointSettings::REFRESH_15},
                                {"30", CrossPointSettings::REFRESH_30}};
constexpr Opt IMAGE_OPTS[] = {{"show", CrossPointSettings::IMAGES_DISPLAY},
                              {"bw", CrossPointSettings::IMAGES_DISPLAY_BW},
                              {"hide", CrossPointSettings::IMAGES_SUPPRESS}};
constexpr Opt PWR_OPTS[] = {{"ignore", CrossPointSettings::IGNORE},
                            {"sleep", CrossPointSettings::SLEEP},
                            {"page", CrossPointSettings::PAGE_TURN},
                            {"refresh", CrossPointSettings::FORCE_REFRESH}};
constexpr Opt SIDE_OPTS[] = {{"prev_next", CrossPointSettings::PREV_NEXT},
                             {"next_prev", CrossPointSettings::NEXT_PREV}};
constexpr Opt FRONT_OPTS[] = {{"bclr", CrossPointSettings::BACK_CONFIRM_LEFT_RIGHT},
                              {"lrbc", CrossPointSettings::LEFT_RIGHT_BACK_CONFIRM},
                              {"lbcr", CrossPointSettings::LEFT_BACK_CONFIRM_RIGHT},
                              {"bcrl", CrossPointSettings::BACK_CONFIRM_RIGHT_LEFT}};
constexpr Opt ORIENT_OPTS[] = {{"portrait", CrossPointSettings::PORTRAIT},
                               {"landscape", CrossPointSettings::LANDSCAPE_CW},
                               {"inverted", CrossPointSettings::INVERTED}};
constexpr Opt SPACING_OPTS[] = {
    {"1.0", CrossPointSettings::LINE_SPACING_100}, {"1.1", CrossPointSettings::LINE_SPACING_110},
    {"1.2", CrossPointSettings::LINE_SPACING_120}, {"1.3", CrossPointSettings::LINE_SPACING_130},
    {"1.4", CrossPointSettings::LINE_SPACING_145}, {"1.6", CrossPointSettings::LINE_SPACING_160},
    {"1.75", CrossPointSettings::LINE_SPACING_175}};

template <size_t N>
const char* optName(const Opt (&opts)[N], const uint8_t value) {
  for (const auto& o : opts) {
    if (o.value == value) return o.name;
  }
  return "other";
}

// Sets `field` from a named option; true when it changed. Unknown names change nothing.
template <size_t N>
bool setOpt(const Opt (&opts)[N], const std::string& name, uint8_t& field) {
  for (const auto& o : opts) {
    if (name == o.name) {
      if (field == o.value) return false;
      field = o.value;
      return true;
    }
  }
  return false;
}

bool setFlag(const std::string& v, uint8_t& field) {
  if (v != "0" && v != "1") return false;
  const uint8_t want = v == "1" ? 1 : 0;
  if (field == want) return false;
  field = want;
  return true;
}

const char* marginName(const uint8_t m) { return m <= 8 ? "narrow" : m <= 20 ? "medium" : "wide"; }

// Status bar items the web shows as chips: page, percent, battery, clock, chapter.
std::string statusValue() {
  std::string out;
  const auto add = [&out](const char* t) {
    if (!out.empty()) out += ",";
    out += t;
  };
  if (SETTINGS.statusBarChapterPageCount) add("page");
  if (SETTINGS.statusBarBookProgressPercentage) add("percent");
  if (SETTINGS.statusBarBattery) add("battery");
  if (SETTINGS.statusBarClock != CrossPointSettings::STATUS_BAR_CLOCK_HIDE) add("clock");
  if (SETTINGS.statusBarTitle == CrossPointSettings::CHAPTER_TITLE) add("chapter");
  return out;
}

bool hasToken(const std::string& list, const char* token) {
  size_t start = 0;
  while (start <= list.size()) {
    const size_t end = list.find(',', start);
    const std::string t = list.substr(start, end == std::string::npos ? std::string::npos : end - start);
    if (t == token) return true;
    if (end == std::string::npos) break;
    start = end + 1;
  }
  return false;
}

bool isMapped(const char* key) {
  for (const char* k : MAPPED_KEYS) {
    if (strcmp(k, key) == 0) return true;
  }
  return false;
}

const char* sizeName(const uint8_t points) {
  if (points <= 12) return "small";
  if (points <= 14) return "medium";
  if (points <= 16) return "large";
  return "xlarge";
}

uint8_t sizePoints(const std::string& name) {
  if (name == "small") return 12;
  if (name == "large") return 16;
  if (name == "xlarge") return 18;
  return 14;
}

// Pushes one mapped key into SETTINGS; true when the device value changed.
bool applyMapped(const std::string& key, const std::string& value) {
  if (key == "sys.lang") {
    // Any language the reader carries (lib/I18n), by its web tag ("de", "pt-BR"...); an unknown tag changes nothing.
    Language lang;
    if (!I18n::languageFromTag(value.c_str(), lang)) return false;
    if (SETTINGS.language == static_cast<uint8_t>(lang)) return false;
    SETTINGS.language = static_cast<uint8_t>(lang);
    I18N.setLanguage(lang);
    return true;
  }
  if (key == "sys.sleep_min") {
    const int minutes = atoi(value.c_str());
    if (minutes < 1 || minutes > 60 || SETTINGS.sleepTimeoutMinutes == minutes) return false;
    SETTINGS.sleepTimeoutMinutes = static_cast<uint8_t>(minutes);
    return true;
  }
  if (key == "read.size") {
    // Only snap when the bucket differs, so a custom size inside the bucket stays.
    if (value == sizeName(SETTINGS.fontPointSize)) return false;
    SETTINGS.fontPointSize = sizePoints(value);
    return true;
  }
  if (key == "read.spacing") return setOpt(SPACING_OPTS, value, SETTINGS.lineSpacing);
  if (key == "read.font") return fonts::select(value);
  if (key == "read.align") return setOpt(ALIGN_OPTS, value, SETTINGS.paragraphAlignment);
  if (key == "read.margin") {
    const uint8_t m = value == "narrow" ? 5 : value == "medium" ? 15 : value == "wide" ? 25 : 0;
    if (m == 0 || value == marginName(SETTINGS.screenMargin)) return false;
    SETTINGS.screenMargin = m;
    return true;
  }
  if (key == "read.indent") {
    if (value != "0" && value != "1" && value != "2") return false;
    const uint8_t n = static_cast<uint8_t>(value[0] - '0');
    if (SETTINGS.paragraphIndentSpaces == n) return false;
    SETTINGS.paragraphIndentSpaces = n;
    return true;
  }
  if (key == "read.para") return setFlag(value, SETTINGS.extraParagraphSpacing);
  if (key == "read.hyphen") return setFlag(value, SETTINGS.hyphenationEnabled);
  if (key == "read.aa") return setFlag(value, SETTINGS.textAntiAliasing);
  if (key == "read.refresh") return setOpt(REFRESH_OPTS, value, SETTINGS.refreshFrequency);
  if (key == "read.images") return setOpt(IMAGE_OPTS, value, SETTINGS.imageRendering);
  if (key == "read.status") {
    if (value == statusValue()) return false;
    SETTINGS.statusBarChapterPageCount = hasToken(value, "page") ? 1 : 0;
    SETTINGS.statusBarBookProgressPercentage = hasToken(value, "percent") ? 1 : 0;
    SETTINGS.statusBarBattery = hasToken(value, "battery") ? 1 : 0;
    SETTINGS.statusBarClock = hasToken(value, "clock") ? CrossPointSettings::STATUS_BAR_CLOCK_RIGHT
                                                       : CrossPointSettings::STATUS_BAR_CLOCK_HIDE;
    SETTINGS.statusBarTitle =
        hasToken(value, "chapter") ? CrossPointSettings::CHAPTER_TITLE : CrossPointSettings::HIDE_TITLE;
    return true;
  }
  if (key == "sys.pwr_short") return setOpt(PWR_OPTS, value, SETTINGS.shortPwrBtn);
  if (key == "sys.side") return setOpt(SIDE_OPTS, value, SETTINGS.sideButtonLayout);
  if (key == "sys.front") return setOpt(FRONT_OPTS, value, SETTINGS.frontButtonLayout);
  if (key == "sys.orient") return setOpt(ORIENT_OPTS, value, SETTINGS.orientation);
  if (key == "sys.clock_fmt") {
    const uint8_t f = value == "12" ? 1 : value == "24" ? 0 : 255;
    if (f == 255 || SETTINGS.clockFormat == f) return false;
    SETTINGS.clockFormat = f;
    return true;
  }
  return false;
}

std::string snapshotValue(const std::string& key) {
  for (const auto& kv : ESPOS.syncedMapped) {
    if (kv.key == key) return kv.value;
  }
  return {};
}

// Snapshot of the mapped values as just synced. Keys in `skip` stay out of it, so the next
// sync reports the device's own value for them.
void takeSnapshot(const std::vector<std::string>& skip) {
  ESPOS.syncedMapped.clear();
  ESPOS.syncedMapped.reserve(sizeof(MAPPED_KEYS) / sizeof(MAPPED_KEYS[0]));
  for (const char* k : MAPPED_KEYS) {
    if (std::find(skip.begin(), skip.end(), k) != skip.end()) continue;
    ESPOS.syncedMapped.push_back({k, mappedValue(k)});
  }
}

}  // namespace

std::string mappedValue(const char* key) {
  if (strcmp(key, "sys.lang") == 0) {
    char tag[16];
    I18n::languageTag(I18N.getLanguage(), tag, sizeof(tag));
    return tag;
  }
  if (strcmp(key, "sys.sleep_min") == 0) return std::to_string(SETTINGS.sleepTimeoutMinutes);
  if (strcmp(key, "read.size") == 0) return sizeName(SETTINGS.fontPointSize);
  if (strcmp(key, "read.spacing") == 0) return optName(SPACING_OPTS, SETTINGS.lineSpacing);
  if (strcmp(key, "read.font") == 0) return SETTINGS.sdFontFamilyName[0] ? SETTINGS.sdFontFamilyName : "notosans";
  if (strcmp(key, "read.align") == 0) return optName(ALIGN_OPTS, SETTINGS.paragraphAlignment);
  if (strcmp(key, "read.margin") == 0) return marginName(SETTINGS.screenMargin);
  if (strcmp(key, "read.indent") == 0) {
    return SETTINGS.paragraphIndentSpaces <= 2 ? std::to_string(SETTINGS.paragraphIndentSpaces) : "other";
  }
  if (strcmp(key, "read.para") == 0) return SETTINGS.extraParagraphSpacing ? "1" : "0";
  if (strcmp(key, "read.hyphen") == 0) return SETTINGS.hyphenationEnabled ? "1" : "0";
  if (strcmp(key, "read.aa") == 0) return SETTINGS.textAntiAliasing ? "1" : "0";
  if (strcmp(key, "read.refresh") == 0) return optName(REFRESH_OPTS, SETTINGS.refreshFrequency);
  if (strcmp(key, "read.images") == 0) return optName(IMAGE_OPTS, SETTINGS.imageRendering);
  if (strcmp(key, "read.status") == 0) return statusValue();
  if (strcmp(key, "sys.pwr_short") == 0) return optName(PWR_OPTS, SETTINGS.shortPwrBtn);
  if (strcmp(key, "sys.side") == 0) return optName(SIDE_OPTS, SETTINGS.sideButtonLayout);
  if (strcmp(key, "sys.front") == 0) return optName(FRONT_OPTS, SETTINGS.frontButtonLayout);
  if (strcmp(key, "sys.orient") == 0) {
    return SETTINGS.orientation == CrossPointSettings::LANDSCAPE_CCW ? "landscape"
                                                                     : optName(ORIENT_OPTS, SETTINGS.orientation);
  }
  if (strcmp(key, "sys.clock_fmt") == 0) return SETTINGS.clockFormat == 1 ? "12" : "24";
  return {};
}

std::string valueToString(JsonVariantConst value) {
  if (value.is<const char*>()) return value.as<const char*>();
  if (value.is<bool>()) return value.as<bool>() ? "1" : "0";
  if (value.is<long long>()) return std::to_string(value.as<long long>());
  if (value.is<double>()) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%g", value.as<double>());
    return buf;
  }
  return {};
}

std::vector<KeyValue> readObject(JsonObjectConst object) {
  std::vector<KeyValue> out;
  out.reserve(object.size());
  for (JsonPairConst kv : object) out.push_back({kv.key().c_str(), valueToString(kv.value())});
  return out;
}

int applyServerSet(std::vector<KeyValue>&& merged, const std::vector<std::string>& webKeys) {
  int changed = 0;
  // A key this device has synced before follows the server. A key that is new to an already
  // linked device keeps the device's value, which goes up at the next sync instead of being
  // replaced by the server's default. A device that never synced takes the server's set whole.
  const bool firstSync = ESPOS.syncedMapped.empty();
  std::vector<std::string> deviceWins;
  for (const auto& kv : merged) {
    if (!isMapped(kv.key.c_str())) continue;
    bool known = firstSync || std::find(webKeys.begin(), webKeys.end(), kv.key) != webKeys.end();
    for (const auto& s : ESPOS.syncedMapped) known = known || s.key == kv.key;
    // Once the server holds the device's own value the key is in sync; without this it would
    // stay out of the snapshot and win every later sync.
    known = known || mappedValue(kv.key.c_str()) == kv.value;
    if (!known) {
      deviceWins.push_back(kv.key);
      continue;
    }
    if (applyMapped(kv.key, kv.value)) changed++;
  }
  ESPOS.replaceSettings(std::move(merged));
  if (changed > 0) SETTINGS.saveToFile();
  takeSnapshot(deviceWins);
  LOG_INF("ESPOS", "Applied server settings (%d device changes)", changed);
  return changed;
}

void collectDeviceChanges(JsonArray out) {
  // A never-taken snapshot means the device has not synced since linking:
  // the server set is authoritative, so report nothing.
  if (!ESPOS.syncedMapped.empty()) {
    for (const char* k : MAPPED_KEYS) {
      const std::string now = mappedValue(k);
      if (now == "other" || now == snapshotValue(k)) continue;
      JsonObject o = out.add<JsonObject>();
      o["k"] = k;
      o["v"] = now;
      o["age_s"] = 0;
    }
  }
  for (const auto& key : ESPOS.dirtyKeys) {
    JsonObject o = out.add<JsonObject>();
    o["k"] = key;
    o["v"] = ESPOS.get(key.c_str(), "");
    o["age_s"] = 0;
  }
}

int pendingChangeCount() {
  int count = static_cast<int>(ESPOS.dirtyKeys.size());
  if (ESPOS.syncedMapped.empty()) return count;
  for (const char* k : MAPPED_KEYS) {
    const std::string now = mappedValue(k);
    if (now != "other" && now != snapshotValue(k)) count++;
  }
  return count;
}

}  // namespace espos::settings
