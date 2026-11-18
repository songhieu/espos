#include "PkgListActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <cstdio>

#include "components/UITheme.h"
#include "espos/DeclGfx.h"
#include "espos/EspOSStore.h"
#include "espos/EspOSUi.h"
#include "espos/Widgets.h"
#include "fontIds.h"

namespace {

const char* typeLabel(const espos::pkgs::Info& p) {
  if (p.type == "face") return tr(STR_EO_PKG_FACE);
  if (p.type == "widget") return tr(STR_EO_PKG_WIDGET);
  return tr(STR_EO_PKG_THEME);
}

// The face dash.layout shows now (empty id when it is a built-in style).
espos::pkgs::FaceRef faceInUse() {
  espos::pkgs::FaceRef ref;
  espos::pkgs::FaceRef::parse(ESPOS.get("dash.layout", "desk"), ref);
  return ref;
}

std::string shapeLabel(const espos::pkgs::FaceRef& ref) {
  return std::string(ref.portrait() ? tr(STR_EO_PKG_PORTRAIT) : tr(STR_EO_PKG_LANDSCAPE)) + " " +
         std::to_string(ref.w) + "x" + std::to_string(ref.h);
}

}  // namespace

// ---- Gói đã cài ----

void PkgListActivity::onEnter() {
  unpacking_ = espos::pkgs::sideloadPending() > 0;
  EspOSMenuActivity::onEnter();
}

void PkgListActivity::loop() {
  if (unpacking_) {
    requestUpdateAndWait();  // the "unpacking" screen is on the panel before the card work starts
    espos::pkgs::sideload();
    {
      RenderLock lock(*this);
      unpacking_ = false;
      refresh();
    }
    requestUpdate();
    return;
  }
  EspOSMenuActivity::loop();
}

void PkgListActivity::render(RenderLock&& lock) {
  if (!unpacking_) {
    EspOSMenuActivity::render(std::move(lock));
    return;
  }
  renderer.clearScreen();
  espos::ui::header(renderer, title());
  espos::ui::wrap(renderer, espos::ui::fontBody(), espos::ui::SIDE, espos::ui::HEADER_H + 30,
                  renderer.getScreenWidth() - 2 * espos::ui::SIDE, tr(STR_EO_PKGS_UNPACKING), 3);
  renderer.displayBuffer();
}

const char* PkgListActivity::title() const { return tr(STR_EO_PKGS); }

std::string PkgListActivity::noteText() const { return tr(STR_EO_PKGS_NOTE); }

const char* PkgListActivity::emptyText() const { return tr(STR_EO_PKGS_EMPTY); }

void PkgListActivity::refresh() {
  pkgs_ = espos::pkgs::list();
  const std::string inUse = faceInUse().id;
  items_.clear();
  for (const auto& p : pkgs_) {
    std::string sub = p.card ? tr(STR_EO_PKG_CARD) : typeLabel(p);
    if (!p.version.empty()) sub += " · " + p.version;
    items_.push_back({p.name, sub, p.id == inUse ? tr(STR_EO_PKG_IN_USE) : "", nullptr});
  }
}

void PkgListActivity::activate(const int index) {
  if (index < 0 || index >= static_cast<int>(pkgs_.size())) return;
  startActivityForResult(std::make_unique<PkgActivity>(renderer, mappedInput, pkgs_[index].id),
                         [this](const ActivityResult&) {
                           refresh();
                           if (selected_ >= static_cast<int>(items_.size())) selected_ = 0;
                           requestUpdate();
                         });
}

// ---- One package ----

void PkgActivity::refresh() {
  if (!espos::pkgs::get(id_, info_)) info_.name = id_;
  items_.clear();
  actions_.clear();
  espos::pkgs::FaceRef self;
  self.id = id_;
  if (info_.type == "face" || info_.type == "widget") {
    items_.push_back({tr(STR_EO_PKG_PREVIEW), "", "", nullptr});
    actions_.push_back({Action::Preview, self});
  }
  if (info_.type == "face") {
    const espos::pkgs::FaceRef cur = faceInUse();
    // One row per shape when the face has several, else one row with no size.
    std::vector<espos::pkgs::FaceRef> rows;
    for (const auto& s : info_.screens) {
      espos::pkgs::FaceRef r = self;
      if (info_.screens.size() > 1 && espos::pkgs::FaceRef::parseSize(s, r.w, r.h)) rows.push_back(r);
    }
    if (rows.empty()) rows.push_back(self);
    for (size_t i = 0; i < rows.size(); i++) {
      const auto& r = rows[i];
      // In use: this face, and this shape (a value with no size means its first shape).
      const bool used = cur.id == id_ && (rows.size() == 1 || (r.w == cur.w && r.h == cur.h) || (cur.w == 0 && i == 0));
      items_.push_back(
          {tr(STR_EO_PKG_USE_FACE), r.w > 0 ? shapeLabel(r) : "", used ? tr(STR_EO_PKG_IN_USE) : "", nullptr});
      actions_.push_back({Action::Use, r});
    }
  }
  items_.push_back({tr(STR_EO_PKG_REMOVE), confirmRemove_ ? tr(STR_EO_PKG_REMOVE_CONFIRM) : "", "", nullptr});
  actions_.push_back({Action::Remove, self});
}

std::string PkgActivity::noteText() const {
  char buf[48];
  snprintf(buf, sizeof(buf), " · %s · %u KB", info_.version.c_str(), static_cast<unsigned>(info_.kb));
  return std::string(info_.card ? tr(STR_EO_PKG_CARD) : typeLabel(info_)) + buf;
}

void PkgActivity::activate(const int index) {
  if (index < 0 || index >= static_cast<int>(actions_.size())) return;
  const auto& a = actions_[index];
  if (a.first != Action::Remove) confirmRemove_ = false;
  switch (a.first) {
    case Action::Preview:
      startActivityForResult(std::make_unique<PkgPreviewActivity>(renderer, mappedInput, id_),
                             [this](const ActivityResult&) { requestUpdate(); });
      return;
    case Action::Use:
      espos::pkgs::useFace(a.second);
      break;
    case Action::Remove: {
      if (!confirmRemove_) {
        confirmRemove_ = true;
        break;
      }
      bool wasFace = false;
      if (!espos::pkgs::remove(id_, &wasFace)) {
        // Still on the card: it stays wanted and in use; the page stays.
        confirmRemove_ = false;
        GUI.drawPopup(renderer, tr(STR_EO_PKG_REMOVE_FAILED));
        delay(2500);
        break;
      }
      espos::pkgs::forget(id_);
      if (wasFace) espos::pkgs::leaveFace();
      ESPOS.saveToFile();
      finish();
      return;
    }
  }
  refresh();
  requestUpdate();
}

// ---- Preview ----

void PkgPreviewActivity::onEnter() {
  Activity::onEnter();
  espos::pkgs::Info info;
  if (espos::pkgs::get(id_, info) && info.type == "face") screens_ = info.screens;
  requestUpdate();
}

void PkgPreviewActivity::onExit() {
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  Activity::onExit();
}

void PkgPreviewActivity::loop() {
  using B = MappedInputManager::Button;
  if (mappedInput.wasReleased(B::Back) || mappedInput.wasReleased(B::Confirm)) {
    finish();
    return;
  }
  // A face with several shapes: the side and page buttons switch between them.
  if (screens_.size() > 1) {
    int step = 0;
    if (mappedInput.wasPressed(B::Down) || mappedInput.wasPressed(B::Right) || mappedInput.wasPressed(B::NavNext))
      step = 1;
    if (mappedInput.wasPressed(B::Up) || mappedInput.wasPressed(B::Left) || mappedInput.wasPressed(B::NavPrevious))
      step = -1;
    if (step != 0) {
      shown_ = (shown_ + screens_.size() + step) % screens_.size();
      requestUpdate();
    }
  }
}

void PkgPreviewActivity::render(RenderLock&&) {
  espos::pkgs::Info info;
  const bool known = espos::pkgs::get(id_, info);
  espos::pkgs::FaceRef face;
  face.id = id_;
  if (shown_ < screens_.size()) espos::pkgs::FaceRef::parseSize(screens_[shown_], face.w, face.h);
  if (known && info.type == "face" && espos::decl::drawFace(renderer, face, EspOSStore::clockValid())) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    return;
  }
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  renderer.clearScreen();
  espos::ui::header(renderer, known ? info.name.c_str() : tr(STR_EO_PKG_PREVIEW));
  if (known && info.type == "widget") {
    // The two box shapes Home uses most: 4x2 and 2x2.
    espos::widgets::Data d;
    espos::widgets::loadData(d, 0);
    const espos::widgets::Box wide{12, 110, 456, 210}, small{12, 350, 222, 210};
    for (const auto& b : {wide, small}) {
      renderer.drawRoundedRect(b.x, b.y, b.w, b.h, 2, 12, true);
      espos::decl::drawWidget(renderer, id_, b, d);
    }
  } else {
    espos::ui::wrap(renderer, espos::ui::fontBody(), espos::ui::SIDE, 120,
                    renderer.getScreenWidth() - 2 * espos::ui::SIDE, tr(STR_EO_PKG_BROKEN), 3);
  }
  espos::ui::hints(renderer, mappedInput, tr(STR_BACK), "", "", "");
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}
