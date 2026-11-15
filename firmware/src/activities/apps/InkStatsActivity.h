#pragma once

#include "activities/Activity.h"
#include "inkdesk/ReadingStats.h"

// Ứng dụng > Thống kê đọc: today against the daily goal, the streak, the last 7 days and
// this week's / month's totals, from the reading log on the card (no network). OK cycles
// the daily goal (read.goal_min, synced like other settings).
class InkStatsActivity final : public Activity {
 public:
  explicit InkStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("InkStats", renderer, mappedInput) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  inkdesk::stats::Summary s_;
};
