#pragma once

#include <string>

#include "activities/Activity.h"

// Ứng dụng > Cập nhật máy. Info mode shows the running firmware and the server's offer;
// "Cập nhật ngay" marks the offer for install and restarts to a clean heap, where Home
// opens this screen in install mode: join WiFi, stream the image with a progress bar,
// restart into it. A failure is shown and reported; the old firmware keeps running.
class EspOSOtaActivity final : public Activity {
 public:
  EspOSOtaActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const bool install)
      : Activity("EspOSOta", renderer, mappedInput), install_(install) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

  // Home asks: the web (or the user here) wants the offer installed, and it can be now.
  static bool installWanted();

 private:
  enum class Mode { Info, Installing, Restarting, Failed };
  void runInstall();
  void renderInfo();
  void renderProgress();

  bool install_;
  Mode mode_ = Mode::Info;
  int selected_ = 0;
  int percent_ = 0;
  size_t done_ = 0;
  size_t total_ = 0;
  std::string error_;
  bool started_ = false;
};
