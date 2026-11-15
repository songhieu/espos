#pragma once

#include "activities/Activity.h"

// Shown right after USB provisioning: who the device is linked to and what
// the web installer sent, then Sync now / Later.
class InkDeskWelcomeActivity final : public Activity {
 public:
  explicit InkDeskWelcomeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("InkDeskWelcome", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  int selected_ = 0;  // 0 = sync now, 1 = later
};
