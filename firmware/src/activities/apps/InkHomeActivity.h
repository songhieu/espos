#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "inkdesk/Widgets.h"

// InkDesk home screen (replaces CrossPoint's Home): the clock and battery along the top, then
// a grid of widgets arranged on the InkDesk web (home.w; built-in layout until then). The side
// and front buttons move between the widgets that open something; OK opens it; Back opens
// the apps.
class InkHomeActivity final : public Activity {
 public:
  explicit InkHomeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("InkHome", renderer, mappedInput) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isHomeActivity() const override { return true; }

 private:
  void loadData();
  void activate(int index);
  void moveFocus(int dx, int dy);
  void drawStatus(int h);
  void drawBanner(const inkdesk::widgets::Box& b);
  void ensureCoverThumb();
  bool shouldAutoSync() const;
  void runAutoSync();
  void promptWifi();
  bool shouldInstallFont() const;
  void openFontDownload();

  bool linked_ = false;
  inkdesk::widgets::Layout layout_;
  inkdesk::widgets::Data data_;
  std::vector<inkdesk::widgets::Box> boxes_;
  int focus_ = -1;
  int sub_ = 0;  // the book focused inside a library widget
  int lastMinute_ = -1;
  bool autoSync_ = false;
  bool syncing_ = false;
  bool fontInstall_ = false;
  bool otaInstall_ = false;
  std::string banner_;      // "0.2.0" once, right after an update
  std::string statusMsg_;   // shown in the status line while something runs
  unsigned long enterMs_ = 0;
};
