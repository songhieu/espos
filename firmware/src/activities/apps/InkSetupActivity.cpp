#include "InkSetupActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstdio>
#include <string>

#include "PairActivity.h"
#include "WifiCredentialStore.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "components/icons/library.h"
#include "components/icons/transfer.h"
#include "components/icons/wifi.h"
#include "inkdesk/InkDeskStore.h"
#include "inkdesk/InkUi.h"
#include "util/QrUtils.h"

namespace iu = inkdesk::ui;

namespace {
constexpr int ROWS = 3;
}  // namespace

void InkSetupActivity::onEnter() {
  Activity::onEnter();
  selected_ = WIFI_STORE.getCredentialSummaries().empty() ? 0 : 1;
  webDetail_ = false;
  requestUpdate();
}

void InkSetupActivity::loop() {
  using B = MappedInputManager::Button;
  if (mappedInput.wasReleased(B::Back)) {
    if (webDetail_) {
      webDetail_ = false;
      requestUpdate();
    } else {
      finish();
    }
    return;
  }
  if (webDetail_) return;
  if (mappedInput.wasPressed(B::Down) || mappedInput.wasPressed(B::Right) || mappedInput.wasPressed(B::NavNext)) {
    selected_ = (selected_ + 1) % ROWS;
    requestUpdate();
  } else if (mappedInput.wasPressed(B::Up) || mappedInput.wasPressed(B::Left) ||
             mappedInput.wasPressed(B::NavPrevious)) {
    selected_ = (selected_ + ROWS - 1) % ROWS;
    requestUpdate();
  } else if (mappedInput.wasReleased(B::Confirm)) {
    activate();
  }
}

void InkSetupActivity::activate() {
  const auto back = [this](const ActivityResult&) {
    if (INKDESK.isLinked()) {
      finish();
      return;
    }
    requestUpdate();
  };
  switch (selected_) {
    case 0:
      startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput, false), back);
      break;
    case 1:
      startActivityForResult(std::make_unique<PairActivity>(renderer, mappedInput), back);
      break;
    default:
      webDetail_ = true;
      requestUpdate();
      break;
  }
}

void InkSetupActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const int w = renderer.getScreenWidth();
  iu::header(renderer, tr(STR_IK_SETUP_TITLE));
  int y = iu::HEADER_H + 18;

  if (webDetail_) {
    renderer.drawText(iu::fontBody(), iu::SIDE, y, tr(STR_IK_SETUP_WEB), true, EpdFontFamily::BOLD);
    y += renderer.getLineHeight(iu::fontBody()) + 8;
    y += iu::wrap(renderer, iu::fontSmall(), iu::SIDE, y, w - iu::SIDE * 2, tr(STR_IK_SETUP_WEB_DETAIL), 6);
    // A build without a server address has no page to point at.
    if (!INKDESK.webUrl().empty()) {
      const int qr = 220;
      const int qy = y + 24;
      QrUtils::drawQrCode(renderer, Rect{(w - qr) / 2, qy, qr, qr}, INKDESK.webUrl());
      const char* cap = tr(STR_IK_SCAN_QR);
      const int cw = renderer.getTextWidth(iu::fontSmall(), cap, EpdFontFamily::BOLD);
      renderer.drawText(iu::fontSmall(), (w - cw) / 2, qy + qr + 12, cap, true, EpdFontFamily::BOLD);
      const std::string host = INKDESK.webHost();
      const int uw = renderer.getTextWidth(iu::fontBody(), host.c_str(), EpdFontFamily::BOLD);
      renderer.drawText(iu::fontBody(), (w - uw) / 2, qy + qr + 12 + renderer.getLineHeight(iu::fontSmall()) + 4,
                        host.c_str(), true, EpdFontFamily::BOLD);
    }
    iu::hints(renderer, mappedInput, tr(STR_BACK), "", "", "");
    renderer.displayBuffer();
    return;
  }

  y += iu::wrap(renderer, iu::fontBody(), iu::SIDE, y, w - iu::SIDE * 2, tr(STR_IK_SETUP_INTRO), 3) + 12;

  const auto saved = WIFI_STORE.getCredentialSummaries();
  char wifiSub[96];
  if (saved.empty()) {
    snprintf(wifiSub, sizeof(wifiSub), "%s", tr(STR_IK_SETUP_WIFI_NONE));
  } else {
    snprintf(wifiSub, sizeof(wifiSub), tr(STR_IK_SETUP_WIFI_SUB_FMT), saved.front().ssid.c_str());
  }
  struct Row {
    const char* title;
    const char* sub;
    const uint8_t* icon;
  };
  const Row rows[ROWS] = {
      {tr(STR_IK_SETUP_WIFI), wifiSub, WifiIcon},
      {tr(STR_IK_SETUP_PAIR), tr(STR_IK_SETUP_PAIR_SUB), LibraryIcon},
      {tr(STR_IK_SETUP_WEB), tr(STR_IK_SETUP_WEB_SUB), TransferIcon},
  };
  for (int i = 0; i < ROWS; i++) {
    char num[4];
    snprintf(num, sizeof(num), "%d", i + 1);
    y += iu::row(renderer, 12, y, w - 24, rows[i].title, rows[i].sub, num, i == selected_, rows[i].icon) + 8;
  }
  y += 10;
  iu::note(renderer, iu::SIDE + 4, y, w - (iu::SIDE + 4) * 2, tr(STR_IK_SETUP_NOTE), 3);

  iu::hints(renderer, mappedInput, tr(STR_BACK), tr(STR_IK_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  renderer.displayBuffer();
}
