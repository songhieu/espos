#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"

// Runs one EspOS sync: WiFi, POST /v1/device/sync, then book, picture and
// reading-font downloads, showing one row per step. Network work blocks loop(),
// like the other download screens; Back cancels a running download.
// Mode::Font only downloads the reading font picked on the web (after a reboot
// for a fresh heap, or from Home).
class EspOSSyncActivity final : public Activity {
 public:
  enum class Mode { Sync, Font };

  explicit EspOSSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, Mode mode = Mode::Sync)
      : Activity("EspOSSync", renderer, mappedInput), mode_(mode) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state_ == State::Working; }

 private:
  enum class State { Wifi, Working, Done, Failed };
  enum class ItemStatus { Waiting, Working, Done, Failed };
  struct Item {
    std::string label;
    std::string detail;
    ItemStatus status = ItemStatus::Waiting;
  };

  void runSync();
  void runFont();
  bool downloadFont(const std::string& family);
  void setItem(size_t index, ItemStatus status, const std::string& detail);
  void fail(const char* message);

  Mode mode_;
  State state_ = State::Wifi;
  bool cancellable_ = false;  // a download is running: Back stops it
  bool startPending_ = false;
  bool networkUsed_ = false;
  bool cancel_ = false;
  std::string headline_;
  std::vector<Item> items_;
  int lastPercent_ = -1;
};
