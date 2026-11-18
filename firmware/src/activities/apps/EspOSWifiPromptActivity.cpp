#include "EspOSWifiPromptActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <WiFi.h>

#include <cstdio>

#include "SilentRestart.h"
#include "WifiCredentialStore.h"
#include "activities/network/WifiSelectionActivity.h"
#include "espos/EspOSNet.h"
#include "espos/EspOSSync.h"
#include "espos/EspOSUi.h"
#include "fontIds.h"

namespace iu = espos::ui;

namespace {
enum Row { ROW_CHOOSE = 0, ROW_RETRY, ROW_LATER };
}

const char* EspOSWifiPromptActivity::title() const { return tr(STR_EO_WIFI_PROMPT_TITLE); }

std::string EspOSWifiPromptActivity::noteText() const { return tr(STR_EO_WIFI_NEED_NOTE); }

void EspOSWifiPromptActivity::refresh() {
  WIFI_STORE.loadFromFile();
  savedSsid_ = WIFI_STORE.getLastConnectedSsid();
  if (savedSsid_.empty() && WIFI_STORE.getCredentialCount() > 0) savedSsid_ = WIFI_STORE.getSsidAt(0).value_or("");
  items_.clear();
  items_.push_back({tr(STR_EO_WIFI_CHOOSE), "", "", nullptr});
  if (!savedSsid_.empty()) {
    char buf[96];
    snprintf(buf, sizeof(buf), tr(STR_EO_WIFI_RETRY_FMT), savedSsid_.c_str());
    items_.push_back({buf, "", "", nullptr});
  }
  items_.push_back({tr(STR_EO_LATER), "", "", nullptr});
}

int EspOSWifiPromptActivity::drawTop(int y) {
  const int x = iu::SIDE;
  const int w = renderer.getScreenWidth() - iu::SIDE * 2;
  char head[96];
  if (savedSsid_.empty()) {
    snprintf(head, sizeof(head), "%s", tr(STR_EO_WIFI_NONE));
  } else {
    snprintf(head, sizeof(head), tr(STR_EO_WIFI_FAIL_FMT), savedSsid_.c_str());
  }
  y += iu::wrap(renderer, NOTOSANS_16_FONT_ID, x, y, w, head, 2, true, static_cast<int>(EpdFontFamily::BOLD)) + 6;
  const char* body = trying_              ? tr(STR_EO_WIFI_TRYING)
                     : savedSsid_.empty() ? tr(STR_EO_WIFI_NONE_BODY)
                                          : tr(STR_EO_WIFI_FAIL_BODY);
  y += iu::wrap(renderer, iu::fontBody(), x, y, w, body, 3) + 14;
  return y;
}

void EspOSWifiPromptActivity::syncAndReturn() {
  // Joined: get the time and data now, then come back to a clean Home (TLS fragments the heap).
  espos::sync::run();
  espos::net::off();
  silentRestart();
}

void EspOSWifiPromptActivity::activate(const int index) {
  const int row = (savedSsid_.empty() && index >= ROW_RETRY) ? index + 1 : index;
  switch (row) {
    case ROW_CHOOSE:
      startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput, false),
                             [this](const ActivityResult&) {
                               if (WiFi.status() == WL_CONNECTED) {
                                 syncAndReturn();
                                 return;
                               }
                               refresh();
                               requestUpdate();
                             });
      break;
    case ROW_RETRY:
      trying_ = true;
      requestUpdateAndWait();
      if (espos::net::joinSaved(12000)) {
        syncAndReturn();
        return;
      }
      espos::net::off();
      trying_ = false;
      refresh();
      requestUpdate();
      break;
    default:
      finish();
      break;
  }
}
