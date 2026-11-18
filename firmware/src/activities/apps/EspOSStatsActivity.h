#pragma once

#include "activities/Activity.h"
#include "espos/ReadingStats.h"

// Ứng dụng > Thống kê đọc: today against the daily goal, the streak, the last 7 days and
// this week's / month's totals, from the reading log on the card (no network). OK cycles
// the daily goal (read.goal_min, synced like other settings).
class EspOSStatsActivity final : public Activity {
 public:
  explicit EspOSStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("EspOSStats", renderer, mappedInput) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  espos::stats::Summary s_;
};
