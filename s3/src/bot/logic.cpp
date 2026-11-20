#include "logic.h"

#include <ArduinoJson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

namespace bot {
namespace logic {

const Settings& defaults() {
  static const Settings d = {
      {"bot.style", "eyes"},
      {"bot.screens", R"([{"id":"clock_bold","s":20},{"id":"date_page","s":8},{"id":"weather","s":8},{"id":"eyes","s":20},{"id":"aquarium","s":15}])"},
      {"bot.bright", "60"},
      {"bot.night", "22:00-07:00"},
      {"bot.dog", ""},
      {"bot.wake", "2"},
      {"bot.follow", "1"},
      {"bot.voice", "1"},
      {"bot.volume", "60"},
      {"bot.alarms", ""},
      {"bot.ota", "stable"},
      {"sys.lang", "vi"},
      {"sys.tz_min", "420"},
      {"dash.city", ""},
  };
  return d;
}

std::string get(const Settings& s, const std::string& k) {
  auto it = s.find(k);
  if (it != s.end()) return it->second;
  auto d = defaults().find(k);
  return d != defaults().end() ? d->second : std::string();
}

uint8_t lookOf(const std::string& style) { return style == "pixel" ? 1 : style == "line" ? 2 : 0; }

std::vector<ui::Screen> screensOf(const std::string& json) {
  std::vector<ui::Screen> out;
  JsonDocument d;
  if (deserializeJson(d, json) || !d.is<JsonArray>()) return out;
  for (JsonObjectConst o : d.as<JsonArrayConst>()) {
    const std::string id = o["id"] | "";
    const int s = o["s"] | 0;
    if (s < 3 || s > 600) continue;
    const int16_t k = ui::screenIndex(id);
    if (k == -2) continue;  // a screen of newer firmware
    out.push_back({k, (uint16_t)s});
  }
  return out;
}

int intOf(const std::string& v, int def, int lo, int hi) {
  if (v.empty()) return def;
  char* e = nullptr;
  const long n = strtol(v.c_str(), &e, 10);
  if (!e || *e) return def;
  return n < lo ? lo : n > hi ? hi : (int)n;
}

static int hhmm(const std::string& s) {
  if (s.size() != 5 || s[2] != ':') return -1;
  for (int i : {0, 1, 3, 4})
    if (s[i] < '0' || s[i] > '9') return -1;
  const int h = (s[0] - '0') * 10 + s[1] - '0', m = (s[3] - '0') * 10 + s[4] - '0';
  return h < 24 && m < 60 ? h * 60 + m : -1;
}

Night nightOf(const std::string& v) {
  Night n;
  if (v.size() != 11 || v[5] != '-') return n;
  const int a = hhmm(v.substr(0, 5)), b = hhmm(v.substr(6));
  if (a < 0 || b < 0 || a == b) return n;
  n.on = true, n.from = a, n.to = b;
  return n;
}

bool inNight(const Night& n, int t) {
  if (!n.on) return false;
  return n.from < n.to ? t >= n.from && t < n.to : t >= n.from || t < n.to;
}

std::vector<Alarm> alarmsOf(const std::string& json) {
  std::vector<Alarm> out;
  if (json.empty()) return out;
  JsonDocument d;
  if (deserializeJson(d, json) || !d.is<JsonArray>()) return out;
  for (JsonObjectConst o : d.as<JsonArrayConst>()) {
    Alarm a;
    a.h = o["h"] | -1, a.m = o["m"] | -1;
    if (a.h < 0 || a.h > 23 || a.m < 0 || a.m > 59) continue;
    for (const char* p = o["days"] | ""; *p; p++)
      if (*p >= '0' && *p <= '6') a.days |= 1 << (*p - '0');
    a.on = (o["on"] | 1) != 0;
    a.label = o["label"] | "";
    const char* date = o["date"] | "";
    if (strlen(date) == 10 && sscanf(date, "%4d-%2d-%2d", &a.y, &a.mo, &a.d) != 3) a.y = a.mo = a.d = 0;
    out.push_back(a);
  }
  return out;
}

std::string alarmsJson(const std::vector<Alarm>& as) {
  JsonDocument d;
  JsonArray arr = d.to<JsonArray>();
  for (const Alarm& a : as) {
    JsonObject o = arr.add<JsonObject>();
    o["h"] = a.h, o["m"] = a.m;
    std::string days;
    for (int i = 0; i < 7; i++)
      if (a.days & (1 << i)) days += (char)('0' + i);
    o["days"] = days;
    o["on"] = a.on ? 1 : 0;
    o["label"] = a.label;
    if (a.y) {
      char t[12];
      snprintf(t, sizeof t, "%04d-%02d-%02d", a.y, a.mo, a.d);
      o["date"] = t;
    }
  }
  std::string s;
  serializeJson(d, s);
  return s;
}

int due(const std::vector<Alarm>& as, const ui::Now& n) {
  if (!n.valid) return -1;
  for (size_t i = 0; i < as.size(); i++) {
    const Alarm& a = as[i];
    if (!a.on || a.h != n.hour || a.m != n.min) continue;
    if (a.y) {
      if (a.y == n.year && a.mo == n.mon && a.d == n.day) return (int)i;
      continue;
    }
    if (!a.days || (a.days & (1 << n.wday))) return (int)i;
  }
  return -1;
}

ui::Now localTime(int64_t utcSec, int tzMin) {
  ui::Now n;
  if (utcSec < 1700000000) return n;
  const time_t t = (time_t)(utcSec + (int64_t)tzMin * 60);
  struct tm tm;
  gmtime_r(&t, &tm);
  n.valid = true;
  n.year = tm.tm_year + 1900, n.mon = tm.tm_mon + 1, n.day = tm.tm_mday, n.wday = tm.tm_wday;
  n.hour = tm.tm_hour, n.min = tm.tm_min, n.sec = tm.tm_sec;
  return n;
}

}  // namespace logic
}  // namespace bot
