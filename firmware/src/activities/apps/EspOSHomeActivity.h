#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "espos/Widgets.h"

// EspOS home screen (replaces CrossPoint's Home): the clock and battery along the top, then
// a grid of widgets arranged on the EspOS web (home.w; built-in layout until then). The side
// and front buttons move between the widgets that open something; OK opens it; Back opens
// the apps.
class EspOSHomeActivity final : public Activity {
 public:
  explicit EspOSHomeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("EspOSHome", renderer, mappedInput) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isHomeActivity() const override { return true; }

 private:
  void loadData();
  void activate(int index);
  void moveFocus(int dx, int dy);
  void drawStatus(int h);
  void drawBanner(const espos::widgets::Box& b);
  void ensureCoverThumb();
  bool shouldAutoSync() const;
  void runAutoSync();
  void promptWifi();
  bool shouldInstallFont() const;
  void openFontDownload();

  bool linked_ = false;
  espos::widgets::Layout layout_;
  espos::widgets::Data data_;
  std::vector<espos::widgets::Box> boxes_;
  int focus_ = -1;
  int sub_ = 0;  // the book focused inside a library widget
  int lastMinute_ = -1;
  bool autoSync_ = false;
  bool syncing_ = false;
  bool fontInstall_ = false;
  bool otaInstall_ = false;
  std::string banner_;     // "0.2.0" once, right after an update
  std::string statusMsg_;  // shown in the status line while something runs
  unsigned long enterMs_ = 0;
};
