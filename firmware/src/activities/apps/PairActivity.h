#pragma once

#include <string>

#include "activities/Activity.h"

// Fallback account link without USB: the device makes a token and a 6-digit
// code, registers them (POST /v1/pair/start), shows code + QR, and polls
// GET /v1/device/whoami until the user confirms on the web.
class PairActivity final : public Activity {
 public:
  explicit PairActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Pair", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state_ == State::Waiting || state_ == State::Starting; }

 private:
  enum class State { Info, Wifi, Starting, Waiting, Linked, Failed };

  void connectThenStart();
  void startPairing();
  void poll();

  State state_ = State::Info;
  bool startPending_ = false;
  bool networkUsed_ = false;
  std::string pendingToken_;
  char code_[8] = {};
  unsigned long startedAt_ = 0;
  unsigned long lastPoll_ = 0;
};
