#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"

// Runs one InkDesk sync: WiFi, POST /v1/device/sync, then book downloads,
// showing one row per step. Network work blocks loop(), like the other
// download screens; Back cancels a running download.
class InkDeskSyncActivity final : public Activity {
 public:
  explicit InkDeskSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("InkDeskSync", renderer, mappedInput) {}

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
  void setItem(size_t index, ItemStatus status, const std::string& detail);
  void fail(const char* message);

  State state_ = State::Wifi;
  bool startPending_ = false;
  bool networkUsed_ = false;
  bool cancel_ = false;
  std::string headline_;
  std::vector<Item> items_;
  int lastPercent_ = -1;
};
