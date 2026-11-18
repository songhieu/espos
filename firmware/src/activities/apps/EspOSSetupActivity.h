#pragma once

#include "activities/Activity.h"

// Shown when the reader is not linked to EspOS: connect WiFi, link with a code,
// or follow the computer (USB) setup with a QR code to the EspOS site.
class EspOSSetupActivity final : public Activity {
 public:
  explicit EspOSSetupActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("EspOSSetup", renderer, mappedInput) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  void activate();
  int selected_ = 0;
  bool webDetail_ = false;
};
