#include "InkDeskWelcomeActivity.h"

#include <I18n.h>

#include <cstdio>

#include "InkDeskSyncActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "inkdesk/InkDeskSerial.h"
#include "inkdesk/InkDeskStore.h"
#include "inkdesk/InkUi.h"

void InkDeskWelcomeActivity::onEnter() {
  Activity::onEnter();
  selected_ = 0;
  requestUpdate();
}

void InkDeskWelcomeActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::NavNext) ||
      mappedInput.wasPressed(MappedInputManager::Button::NavPrevious)) {
    selected_ = 1 - selected_;
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goHome();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (selected_ == 0) {
      activityManager.replaceActivity(std::make_unique<InkDeskSyncActivity>(renderer, mappedInput));
    } else {
      activityManager.goHome();
    }
  }
}

void InkDeskWelcomeActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int w = renderer.getScreenWidth();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, w, metrics.headerHeight}, tr(STR_ID_WELCOME));
  const int left = metrics.contentSidePadding + 8;
  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + 12;

  renderer.drawText(NOTOSANS_14_FONT_ID, left, y, tr(STR_ID_LINKED_WITH));
  y += renderer.getLineHeight(NOTOSANS_14_FONT_ID);
  inkdesk::ui::fitText(renderer, NOTOSANS_18_FONT_ID, left, y, INKDESK.user.c_str(), w - left * 2, true,
                       EpdFontFamily::BOLD);
  y += renderer.getLineHeight(NOTOSANS_18_FONT_ID) + 18;

  renderer.drawText(UI_10_FONT_ID, left, y, tr(STR_ID_RECEIVED), true, EpdFontFamily::BOLD);
  y += renderer.getLineHeight(UI_10_FONT_ID) + 8;

  const auto& summary = inkdesk::serial::lastProvision();
  char lines[3][96];
  int count = 0;
  if (!summary.wifiSsid.empty()) snprintf(lines[count++], sizeof(lines[0]), tr(STR_ID_WIFI_FMT), summary.wifiSsid.c_str());
  if (!INKDESK.deviceName.empty()) {
    snprintf(lines[count++], sizeof(lines[0]), tr(STR_ID_DEVICE_NAME_FMT), INKDESK.deviceName.c_str());
  }
  snprintf(lines[count++], sizeof(lines[0]), tr(STR_ID_SETTINGS_APPLIED_FMT),
           static_cast<int>(INKDESK.settings.size()));
  const int rowH = renderer.getLineHeight(NOTOSANS_14_FONT_ID) + 8;
  for (int i = 0; i < count; i++) {
    renderer.drawLine(left + 2, y + 11, left + 8, y + 17, 3, true);
    renderer.drawLine(left + 8, y + 17, left + 20, y + 4, 3, true);
    renderer.drawText(NOTOSANS_14_FONT_ID, left + 34, y, lines[i]);
    y += rowH;
  }
  y += 24;

  const char* options[2] = {tr(STR_ID_SYNC_NOW), tr(STR_ID_LATER)};
  const int optH = renderer.getLineHeight(NOTOSANS_16_FONT_ID) + 24;
  for (int i = 0; i < 2; i++) {
    const bool sel = i == selected_;
    if (sel) renderer.fillRoundedRect(left - 8, y, w - (left - 8) * 2, optH, 10, Color::Black);
    renderer.drawText(NOTOSANS_16_FONT_ID, left + 8, y + 12, options[i], !sel,
                      sel ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    y += optH + 6;
  }

  const auto labels = mappedInput.mapLabels("", tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
