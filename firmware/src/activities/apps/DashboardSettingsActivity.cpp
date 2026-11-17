#include "DashboardSettingsActivity.h"

#include <I18n.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "InkDeskSyncActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "inkdesk/DashboardView.h"
#include "inkdesk/InkDeskStore.h"
#include "inkdesk/Pkgs.h"


namespace {
enum Row { ROW_ENABLED = 0, ROW_INTERVAL, ROW_CHARGING, ROW_SAVE, ROW_LAYOUT, ROW_PREVIEW, ROW_SYNC };
const char* const ROW_KEYS[DashboardSettingsActivity::ROW_COUNT] = {
    "dash.enabled", "dash.interval_min", "dash.charging_min", "dash.save_pct", "dash.layout", nullptr, nullptr};
// dash.save_pct steps: 0 means never pause.
constexpr int SAVE_LEVELS[] = {0, 10, 20, 30};
constexpr int INTERVALS[] = {5, 15, 30, 60};

// dash.layout values in the order the row cycles through: the built-in styles, then each installed face
// (one value per shape when it has both). "landscape" is the old name of "desk".
std::vector<std::string> layoutValues() {
  std::vector<std::string> v = {"desk", "calendar", "portrait", "wl", "wp"};
  for (const auto& p : inkdesk::pkgs::list()) {
    if (p.type != "face") continue;
    inkdesk::pkgs::FaceRef ref;
    ref.id = p.id;
    if (p.screens.size() <= 1) {
      v.push_back(ref.format());
      continue;
    }
    for (const auto& s : p.screens) {
      if (inkdesk::pkgs::FaceRef::parseSize(s, ref.w, ref.h)) v.push_back(ref.format());
    }
  }
  return v;
}
std::string layoutLabel(const std::string& v) {
  if (v == "calendar") return tr(STR_IK_LAYOUT_CAL);
  if (v == "portrait") return tr(STR_ID_PORTRAIT);
  if (v == "wl") return tr(STR_IK_LAYOUT_WL);
  if (v == "wp") return tr(STR_IK_LAYOUT_WP);
  inkdesk::pkgs::FaceRef ref;
  inkdesk::pkgs::Info info;
  if (inkdesk::pkgs::FaceRef::parse(v, ref)) {
    if (!inkdesk::pkgs::get(ref.id, info)) return tr(STR_IK_PKG_MISSING);
    if (ref.w == 0 || info.screens.size() <= 1) return info.name;
    return info.name + " · " + (ref.portrait() ? tr(STR_IK_PKG_PORTRAIT) : tr(STR_IK_PKG_LANDSCAPE));
  }
  return tr(STR_IK_LAYOUT_DESK);
}
std::string nextLayout(const std::string& v) {
  const auto all = layoutValues();
  const std::string cur = v == "landscape" ? "desk" : v;
  // A face matches its own value, or (set without a size, or from the web) its first one.
  inkdesk::pkgs::FaceRef face, each;
  const bool isFace = inkdesk::pkgs::FaceRef::parse(cur, face);
  int at = -1;
  for (size_t i = 0; i < all.size() && at < 0; i++) {
    if (all[i] == cur) at = static_cast<int>(i);
  }
  for (size_t i = 0; i < all.size() && at < 0 && isFace; i++) {
    if (inkdesk::pkgs::FaceRef::parse(all[i], each) && each.id == face.id) at = static_cast<int>(i);
  }
  return at < 0 ? all.front() : all[(at + 1) % all.size()];
}
}  // namespace

const char* DashboardSettingsActivity::title() const { return tr(STR_ID_DASHBOARD); }
const char* DashboardSettingsActivity::okLabel() const { return tr(STR_IK_CHANGE); }
std::string DashboardSettingsActivity::noteText() const { return tr(STR_ID_DASH_NOTE); }

void DashboardSettingsActivity::refresh() {
  char buf[64];
  items_.assign(ROW_COUNT, Item{});
  const bool enabled = INKDESK.get("dash.enabled", "1") != "0";
  items_[ROW_ENABLED] = {tr(STR_ID_DASH_SLEEP), "", enabled ? tr(STR_ID_DASHBOARD) : tr(STR_ID_STATE_OFF), nullptr};
  snprintf(buf, sizeof(buf), tr(STR_ID_MINUTES_FMT), INKDESK.getInt("dash.interval_min", 30));
  items_[ROW_INTERVAL] = {tr(STR_ID_DASH_INTERVAL), "", buf, nullptr};
  items_[ROW_CHARGING] = {tr(STR_ID_DASH_CHARGING), "",
                          INKDESK.get("dash.charging_min", "1") == "1" ? tr(STR_ID_EVERY_MINUTE) : tr(STR_ID_STATE_OFF),
                          nullptr};
  const int save = inkdesk::dashboard::saveBelowPercent();
  char saveBuf[16];
  snprintf(saveBuf, sizeof(saveBuf), "%d%%", save);
  items_[ROW_SAVE] = {tr(STR_IK_SAVE_BELOW), "", save > 0 ? saveBuf : tr(STR_ID_STATE_OFF), nullptr};
  items_[ROW_LAYOUT] = {tr(STR_ID_DASH_LAYOUT), "", layoutLabel(INKDESK.get("dash.layout", "desk")), nullptr};
  items_[ROW_PREVIEW] = {tr(STR_ID_DASH_PREVIEW), "", "", nullptr};
  items_[ROW_SYNC] = {tr(STR_ID_SYNC_NOW), "", "", nullptr};
  for (int i = 0; i < ROW_COUNT; i++) {
    if (ROW_KEYS[i] && INKDESK.isDirty(ROW_KEYS[i])) items_[i].right = std::string(tr(STR_ID_PENDING)) + "  " + items_[i].right;
  }
}

void DashboardSettingsActivity::activate(const int index) {
  const auto back = [this](const ActivityResult&) {
    refresh();
    requestUpdate();
  };
  switch (index) {
    case ROW_ENABLED:
      INKDESK.setLocal("dash.enabled", INKDESK.get("dash.enabled", "1") == "0" ? "1" : "0");
      break;
    case ROW_INTERVAL: {
      const int current = INKDESK.getInt("dash.interval_min", 30);
      int next = INTERVALS[0];
      for (size_t i = 0; i < sizeof(INTERVALS) / sizeof(INTERVALS[0]); i++) {
        if (INTERVALS[i] == current) {
          next = INTERVALS[(i + 1) % (sizeof(INTERVALS) / sizeof(INTERVALS[0]))];
          break;
        }
      }
      INKDESK.setLocal("dash.interval_min", std::to_string(next));
      break;
    }
    case ROW_CHARGING:
      INKDESK.setLocal("dash.charging_min", INKDESK.get("dash.charging_min", "1") == "1" ? "0" : "1");
      break;
    case ROW_SAVE: {
      const int current = inkdesk::dashboard::saveBelowPercent();
      int next = SAVE_LEVELS[0];
      for (size_t i = 0; i < sizeof(SAVE_LEVELS) / sizeof(SAVE_LEVELS[0]); i++) {
        if (SAVE_LEVELS[i] == current) {
          next = SAVE_LEVELS[(i + 1) % (sizeof(SAVE_LEVELS) / sizeof(SAVE_LEVELS[0]))];
          break;
        }
      }
      INKDESK.setLocal("dash.save_pct", std::to_string(next));
      break;
    }
    case ROW_LAYOUT:
      INKDESK.setLocal("dash.layout", nextLayout(INKDESK.get("dash.layout", "desk")));
      break;
    case ROW_PREVIEW:
      startActivityForResult(std::make_unique<DashboardPreviewActivity>(renderer, mappedInput), back);
      return;
    case ROW_SYNC:
      startActivityForResult(std::make_unique<InkDeskSyncActivity>(renderer, mappedInput), back);
      return;
    default:
      return;
  }
  INKDESK.saveToFile();
  refresh();
  requestUpdate();
}

void DashboardPreviewActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void DashboardPreviewActivity::onExit() {
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  Activity::onExit();
}

void DashboardPreviewActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    finish();
  }
}

void DashboardPreviewActivity::render(RenderLock&&) {
  inkdesk::dashboard::WakePlan plan = inkdesk::dashboard::planWake(false);
  plan.clock = InkDeskStore::clockValid();
  if (!inkdesk::dashboard::draw(renderer, plan)) {
    renderer.setOrientation(GfxRenderer::Orientation::Portrait);
    renderer.clearScreen();
    const auto& metrics = UITheme::getInstance().getMetrics();
    const int w = renderer.getScreenWidth();
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, w, metrics.headerHeight}, tr(STR_ID_DASH_PREVIEW));
    int y = metrics.topPadding + metrics.headerHeight + 30;
    for (const auto& line : renderer.wrappedText(NOTOSANS_14_FONT_ID, tr(STR_ID_DASH_NO_DATA), w - 60, 4)) {
      renderer.drawText(NOTOSANS_14_FONT_ID, 30, y, line.c_str());
      y += renderer.getLineHeight(NOTOSANS_14_FONT_ID);
    }
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}
