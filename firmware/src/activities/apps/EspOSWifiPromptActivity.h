#pragma once

#include <string>

#include "EspOSMenuActivity.h"

// Shown on Home when a linked reader has never synced and cannot reach WiFi (first boot
// after setup with a wrong password, or no network saved): choose another network, retry the
// saved one, or skip. A successful join syncs at once (time, data) and returns to Home.
class EspOSWifiPromptActivity final : public EspOSMenuActivity {
 public:
  explicit EspOSWifiPromptActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : EspOSMenuActivity("EspOSWifiPrompt", renderer, mappedInput) {}

 protected:
  const char* title() const override;
  void refresh() override;
  void activate(int index) override;
  int drawTop(int y) override;
  std::string noteText() const override;

 private:
  void syncAndReturn();
  std::string savedSsid_;
  bool trying_ = false;
};
