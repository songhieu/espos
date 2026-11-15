#include "DashboardSettingsActivity.h"

#include <I18n.h>

#include <cstdio>
#include <cstdlib>

#include "InkDeskSyncActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "inkdesk/DashboardView.h"
#include "inkdesk/InkDeskStore.h"


namespace {
enum Row { ROW_ENABLED = 0, ROW_INTERVAL, ROW_CHARGING, ROW_SAVE, ROW_LAYOUT, ROW_PREVIEW, ROW_SYNC };
const char* const ROW_KEYS[DashboardSettingsActivity::ROW_COUNT] = {
    "dash.enabled", "dash.interval_min", "dash.charging_min", "dash.save_pct", "dash.layout", nullptr, nullptr};
// dash.save_pct steps: 0 means never pause.
constexpr int SAVE_LEVELS[] = {0, 10, 20, 30};
constexpr int INTERVALS[] = {5, 15, 30, 60};

// dash.layout values in the order the row cycles through; "landscape" is the old name of "desk".
const char* layoutLabel(const std::string& v) {
  if (v == "calendar") return tr(STR_IK_LAYOUT_CAL);
  if (v == "portrait") return tr(STR_ID_PORTRAIT);
  if (v == "wl") return tr(STR_IK_LAYOUT_WL);
  if (v == "wp") return tr(STR_IK_LAYOUT_WP);
  return tr(STR_IK_LAYOUT_DESK);
}
const char* nextLayout(const std::string& v) {
  if (v == "calendar") return "portrait";
  if (v == "portrait") return "wl";
  if (v == "wl") return "wp";
  if (v == "wp") return "desk";
  return "calendar";
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
