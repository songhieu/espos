#pragma once

#include <ArduinoJson.h>

#include <cstdint>
#include <string>
#include <vector>

// Reading statistics: active reading time per local day and book. The reader appends one
// line per session to a small log (no JSON work while a book is open); compact() folds the
// log into a per-day file kept for 60 days, which the screens read and the sync sends.
namespace espos::stats {

// Times one reading session. A page counts from when it is shown until the reader turns
// forward (2 s to 10 min): a book left open and asleep adds nothing.
class Tracker {
 public:
  void noteTurn(const bool forward, const bool ok) { pendingForward_ = forward && ok; }
  void onRender(uint32_t monotonicMs);
  // Appends the session to the log (when it is worth it) and starts over.
  void flush(const std::string& bookPath, const std::string& title);

 private:
  uint32_t anchorMs_ = 0;
  bool hasAnchor_ = false;
  bool pendingForward_ = false;
  uint32_t seconds_ = 0;
  uint32_t pages_ = 0;
};

struct Summary {
  bool dated = false;   // the clock is set, so "today" is known
  bool recent = false;  // something was read in the last 7 days
  uint32_t todaySec = 0;
  uint32_t todayPages = 0;
  int goalMin = 30;
  int streak = 0;        // days in a row reaching the goal; today counts once reached
  uint32_t weekSec = 0;  // since Monday
  uint32_t monthSec = 0;
  uint32_t monthPages = 0;
  uint32_t last7[7] = {};  // seconds, oldest first; the last one is today
  int todayWday = 0;       // 0 = Sunday
  std::string topTitle;    // most read this month
  uint32_t topSec = 0;
};

int goalMinutes();
// Folds the session log into the per-day file. Needs the clock (sessions read without it
// wait in the log and land on the day the clock comes back).
void compact();
Summary summary();

// Sync: per-day, per-book totals changed since the last successful sync (absolute values, so
// sending them twice does not count them twice). `days` gets the days sent.
void collect(JsonArray out, std::vector<std::string>& days);
void markSent(const std::vector<std::string>& days);

}  // namespace espos::stats
