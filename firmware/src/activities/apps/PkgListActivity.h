#pragma once

#include <string>
#include <utility>
#include <vector>

#include "EspOSMenuActivity.h"
#include "activities/Activity.h"
#include "espos/Pkgs.h"

// Ứng dụng › Gói đã cài: the faces and widgets on the card (s3desk/docs/profiles.md §5).
// Opening it first unpacks .espk files copied into /EspOS/pkgs (with a "please wait" screen).
class PkgListActivity final : public EspOSMenuActivity {
 public:
  explicit PkgListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : EspOSMenuActivity("PkgList", renderer, mappedInput) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&& lock) override;

 private:
  const char* title() const override;
  void refresh() override;
  void activate(int index) override;
  std::string noteText() const override;
  const char* emptyText() const override;

  std::vector<espos::pkgs::Info> pkgs_;
  bool unpacking_ = false;  // copied .espk files wait: the next loop unpacks them
};

// One package: preview it, use a face as the sleep screen, remove it (OK twice).
class PkgActivity final : public EspOSMenuActivity {
 public:
  PkgActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string id)
      : EspOSMenuActivity("Pkg", renderer, mappedInput), id_(std::move(id)) {}

 private:
  enum class Action : uint8_t { Preview, Use, Remove };
  const char* title() const override { return info_.name.c_str(); }
  void refresh() override;
  void activate(int index) override;
  std::string noteText() const override;

  std::string id_;
  espos::pkgs::Info info_;
  std::vector<std::pair<Action, espos::pkgs::FaceRef>> actions_;  // per row; Use carries its face
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
