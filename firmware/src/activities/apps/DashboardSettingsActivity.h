#pragma once

#include <string>

#include "activities/Activity.h"
#include "InkMenuActivity.h"

// Apps > Dashboard: the device side of the dash.* settings. Changes are kept
// as InkDesk dirty keys and sent on the next sync ("• chờ gửi").
class DashboardSettingsActivity final : public InkMenuActivity {
 public:
  explicit DashboardSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : InkMenuActivity("DashboardSettings", renderer, mappedInput) {}
  static constexpr int ROW_COUNT = 7;

 private:
  const char* title() const override;
  void refresh() override;
  void activate(int index) override;
  std::string noteText() const override;
  const char* okLabel() const override;
};

// Full-screen preview of the cached dashboard (what the sleep screen shows).
class DashboardPreviewActivity final : public Activity {
 public:
  explicit DashboardPreviewActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("DashboardPreview", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
