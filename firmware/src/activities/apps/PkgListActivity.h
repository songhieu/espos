#pragma once

#include <string>
#include <utility>
#include <vector>

#include "InkMenuActivity.h"
#include "activities/Activity.h"
#include "inkdesk/Pkgs.h"

// Ứng dụng › Gói đã cài: the faces and widgets on the card (s3desk/docs/profiles.md §5).
// Opening it first unpacks .inkp files copied into /InkDesk/pkgs (with a "please wait" screen).
class PkgListActivity final : public InkMenuActivity {
 public:
  explicit PkgListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : InkMenuActivity("PkgList", renderer, mappedInput) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&& lock) override;

 private:
  const char* title() const override;
  void refresh() override;
  void activate(int index) override;
  std::string noteText() const override;
  const char* emptyText() const override;

  std::vector<inkdesk::pkgs::Info> pkgs_;
  bool unpacking_ = false;  // copied .inkp files wait: the next loop unpacks them
};

// One package: preview it, use a face as the sleep screen, remove it (OK twice).
class PkgActivity final : public InkMenuActivity {
 public:
  PkgActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string id)
      : InkMenuActivity("Pkg", renderer, mappedInput), id_(std::move(id)) {}

 private:
  enum class Action : uint8_t { Preview, Use, Remove };
  const char* title() const override { return info_.name.c_str(); }
  void refresh() override;
  void activate(int index) override;
  std::string noteText() const override;

  std::string id_;
  inkdesk::pkgs::Info info_;
  std::vector<std::pair<Action, inkdesk::pkgs::FaceRef>> actions_;  // per row; Use carries its face
  bool confirmRemove_ = false;
};

// A package drawn as the device will show it: a face on the whole screen (Up/Down switch between its
// shapes), a widget in two Home boxes.
class PkgPreviewActivity final : public Activity {
 public:
  PkgPreviewActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string id)
      : Activity("PkgPreview", renderer, mappedInput), id_(std::move(id)) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::string id_;
  std::vector<std::string> screens_;  // a face's layout sizes
  size_t shown_ = 0;
};
