#pragma once
// EspOS Bot: the settings as the server sends them (device_sync, kind "bot": flat string keys bot.*, sys.lang,
// sys.tz_min, dash.city; web/scripts/bot-keys.go.txt is the server's side) read into plain values, the alarms and the
// night hours. No Arduino: the host tests (sim/bot/test.cpp) run the same code.
#include <stdint.h>

#include <map>
#include <string>
#include <vector>

#include "ui.h"

namespace bot {
namespace logic {

using Settings = std::map<std::string, std::string>;

// Every key the bot reads, with the firmware's default (the server's botDefaults; change both).
const Settings& defaults();
std::string get(const Settings& s, const std::string& k);  // the value, or the default

uint8_t lookOf(const std::string& style);  // "eyes" 0, "pixel" 1, "line" 2; anything else 0
std::vector<ui::Screen> screensOf(const std::string& json);  // [{"id","s"}]: known ids only; empty = the default
int intOf(const std::string& v, int def, int lo, int hi);

struct Night {
  bool on = false;
  int from = 0, to = 0;  // minutes of the day; from > to crosses midnight
};
Night nightOf(const std::string& v);  // "22:00-07:00" or ""
bool inNight(const Night& n, int minuteOfDay);

struct Alarm {
  int h = 0, m = 0;
  uint8_t days = 0;  // bit d = weekday d (0 Sunday); 0 = once (the next time it comes, or on `date`)
  bool on = true;
  std::string label;
  int y = 0, mo = 0, d = 0;  // a reminder's date (0: none)
};
std::vector<Alarm> alarmsOf(const std::string& json);
std::string alarmsJson(const std::vector<Alarm>& a);  // back to the server's shape (a one-off rang: on 0)
// The alarm that rings at this minute (-1: none). `now` must be valid.
int due(const std::vector<Alarm>& a, const ui::Now& now);

// The local time from a UTC epoch (seconds) and the zone's offset in minutes.
ui::Now localTime(int64_t utcSec, int tzMin);

}  // namespace logic
}  // namespace bot
