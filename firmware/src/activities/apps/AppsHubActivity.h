#pragma once

#include <string>

#include "InkMenuActivity.h"

// Home > Ứng dụng: every InkDesk app plus the file tools, one row each with a status line.
class AppsHubActivity final : public InkMenuActivity {
 public:
  explicit AppsHubActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : InkMenuActivity("AppsHub", renderer, mappedInput) {}

 private:
  const char* title() const override;
  void refresh() override;
  void activate(int index) override;
  std::string noteText() const override;

  std::string newsPath_;  // newest morning-news issue on the card
};
