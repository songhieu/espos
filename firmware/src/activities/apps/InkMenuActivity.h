#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"

// Base for InkDesk list screens drawn with inkdesk::ui (mockup style): header, rounded rows
// with icon/subtitle/right value that invert when selected, optional content above the rows
// (tabs, hero), optional dashed note at the bottom, theme button hints.
class InkMenuActivity : public Activity {
 public:
  using Activity::Activity;
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 protected:
  struct Item {
    std::string title;
    std::string sub;
    std::string right;
    const uint8_t* icon = nullptr;
  };

  virtual const char* title() const = 0;
  virtual void refresh() = 0;            // rebuild items_ (and anything drawn by drawTop)
  virtual void activate(int index) = 0;  // Confirm on a row
  virtual int drawTop(int y) { return y; }
  virtual std::string noteText() const { return {}; }
  virtual const char* okLabel() const;
  virtual const char* emptyText() const { return ""; }
  // Side Up/Down: by default they move the selection; return true to take them over (tabs).
  virtual bool onSideButton(int /*direction*/) { return false; }

  void select(int index);

  std::vector<Item> items_;
  int selected_ = 0;
  int scroll_ = 0;
};
