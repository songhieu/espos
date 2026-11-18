#include "PairActivity.h"

#include <ArduinoJson.h>
#include <I18n.h>
#include <Logging.h>
#include <Utf8.h>
#include <WiFi.h>
#include <esp_random.h>

#include <cstdio>

#include "EspOSSyncActivity.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "espos/EspOSClient.h"
#include "espos/EspOSStore.h"
#include "fontIds.h"
#include "util/QrUtils.h"

namespace {
constexpr unsigned long POLL_INTERVAL_MS = 5000;
constexpr unsigned long CODE_LIFETIME_MS = 10UL * 60UL * 1000UL;

int drawBigText(const GfxRenderer& r, const char* text, int x, const int baseline, const int scale256) {
  const auto* cursor = reinterpret_cast<const unsigned char*>(text);
  while (const uint32_t cp = utf8NextCodepoint(&cursor)) {
    x += r.drawScaledCodepoint(NOTOSANS_18_FONT_ID, cp, EpdFontFamily::BOLD, x, baseline, scale256);
  }
  return x;
}

int bigTextWidth(const GfxRenderer& r, const char* text, const int scale256) {
  int width = 0;
  const auto* cursor = reinterpret_cast<const unsigned char*>(text);
  while (const uint32_t cp = utf8NextCodepoint(&cursor)) {
    int32_t advanceFP = 0;
    int top = 0;
    if (r.getCodepointMetrics(NOTOSANS_18_FONT_ID, cp, EpdFontFamily::BOLD, advanceFP, top)) {
      width += (static_cast<int32_t>(advanceFP) * scale256 / 256 + 8) >> 4;
    }
  }
  return width;
}
}  // namespace

void PairActivity::onEnter() {
  Activity::onEnter();
  if (ESPOS.isLinked()) {
    state_ = State::Info;
    requestUpdate();
    return;
  }
  connectThenStart();
}

void PairActivity::onExit() {
  Activity::onExit();
  if (networkUsed_) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(30);
    silentRestart();
  }
}

void PairActivity::connectThenStart() {
  networkUsed_ = true;
  if (WiFi.status() == WL_CONNECTED) {
    state_ = State::Starting;
    startPending_ = true;
    requestUpdate();
    return;
  }
  state_ = State::Wifi;
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled) {
                             state_ = State::Failed;
                             return;
                           }
                           state_ = State::Starting;
                           startPending_ = true;
                         });
}

void PairActivity::startPairing() {
  pendingToken_ = espos::client::randomTokenHex();
  const std::string tokenHash = espos::client::sha256Hex(pendingToken_);
  espos::client::Result result = espos::client::Result::Conflict;
  for (int attempt = 0; attempt < 3 && result == espos::client::Result::Conflict; attempt++) {
    snprintf(code_, sizeof(code_), "%06u", static_cast<unsigned>(100000 + esp_random() % 900000));
    JsonDocument req;
    req["code"] = code_;
    req["token_hash"] = tokenHash;
    req["name"] = "Xteink X4";
    std::string body;
    serializeJson(req, body);
    std::string response;
    result = espos::client::postJson("/v1/pair/start", body, response, false, 1024);
  }
  {
    RenderLock lock(*this);
    if (result == espos::client::Result::Ok) {
      state_ = State::Waiting;
      startedAt_ = millis();
      lastPoll_ = millis();
    } else {
      LOG_ERR("ESPOS", "Pair start failed (%d)", espos::client::lastStatus());
      state_ = State::Failed;
    }
  }
  requestUpdate();
}

void PairActivity::poll() {
  std::string response;
  const auto result = espos::client::getJsonWithToken("/v1/device/whoami", response, pendingToken_);
  if (result == espos::client::Result::NotLinked || result == espos::client::Result::Network) return;
  if (result != espos::client::Result::Ok) {
    LOG_ERR("ESPOS", "whoami failed (%d)", espos::client::lastStatus());
    if (result == espos::client::Result::Unauthorized) {
      RenderLock lock(*this);
      state_ = State::Failed;
    }
    requestUpdate();
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, response)) return;
  JsonObjectConst device = doc["device"];
  ESPOS.clearLink();
  if (ESPOS.api.empty()) ESPOS.api = espos::DEFAULT_API;
  ESPOS.token = pendingToken_;
  ESPOS.deviceId = device["id"] | 0;
  ESPOS.deviceName = device["name"] | "";
  ESPOS.user = device["user"] | "";
  ESPOS.syncedMapped.clear();
  ESPOS.saveToFile();
  {
    RenderLock lock(*this);
    state_ = State::Linked;
  }
  requestUpdate();
}

void PairActivity::loop() {
  if (startPending_) {
    startPending_ = false;
    requestUpdateAndWait();
    startPairing();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    switch (state_) {
      case State::Info:
      case State::Failed:
      case State::Waiting:
        connectThenStart();
        return;
      case State::Linked:
        startActivityForResult(std::make_unique<EspOSSyncActivity>(renderer, mappedInput),
                               [](const ActivityResult&) { finish(); });
        return;
      default:
        break;
    }
  }
  if (state_ == State::Waiting) {
    if (millis() - startedAt_ > CODE_LIFETIME_MS) {
      {
        RenderLock lock(*this);
        state_ = State::Failed;
      }
      requestUpdate();
      return;
    }
    if (millis() - lastPoll_ >= POLL_INTERVAL_MS) {
      lastPoll_ = millis();
      poll();
    }
  }
}

void PairActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, w, metrics.headerHeight}, tr(STR_ID_LINK));
  const int left = metrics.contentSidePadding + 8;
  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + 10;
  const int lineH = renderer.getLineHeight(NOTOSANS_14_FONT_ID);

  const char* confirmLabel = "";
  switch (state_) {
    case State::Info: {
      char buf[128];
      snprintf(buf, sizeof(buf), tr(STR_ID_LINKED_TO_FMT),
               ESPOS.user.empty() ? ESPOS.deviceName.c_str() : ESPOS.user.c_str());
      renderer.drawText(NOTOSANS_14_FONT_ID, left, y, buf, true, EpdFontFamily::BOLD);
      confirmLabel = tr(STR_ID_PAIR_NEW_CODE);
      break;
    }
    case State::Wifi:
    case State::Starting:
      renderer.drawText(NOTOSANS_14_FONT_ID, left, y, tr(STR_ID_SYNC_CONNECTING));
      break;
    case State::Waiting: {
      for (const auto& line :
           renderer.wrappedText(NOTOSANS_14_FONT_ID, tr(STR_ID_PAIR_INSTRUCTIONS), w - left * 2, 4)) {
        renderer.drawText(NOTOSANS_14_FONT_ID, left, y, line.c_str());
        y += lineH;
      }
      y += 16;
      char spaced[8];
      snprintf(spaced, sizeof(spaced), "%.3s %.3s", code_, code_ + 3);
      const int scale = 680;
      const int codeW = bigTextWidth(renderer, spaced, scale);
      const int ascent = renderer.getFontAscenderSize(NOTOSANS_18_FONT_ID) * scale / 256;
      drawBigText(renderer, spaced, (w - codeW) / 2, y + ascent, scale);
      y += ascent + 30;
      const int qrSize = 190;
      const std::string url = ESPOS.pairUrl(code_);
      QrUtils::drawQrCode(renderer, Rect{(w - qrSize) / 2, y, qrSize, qrSize}, url);
      y += qrSize + 14;
      const char* waiting = tr(STR_ID_PAIR_WAITING);
      renderer.drawText(NOTOSANS_12_FONT_ID, (w - renderer.getTextWidth(NOTOSANS_12_FONT_ID, waiting)) / 2, y, waiting);
      confirmLabel = tr(STR_ID_PAIR_NEW_CODE);
      break;
    }
    case State::Linked:
      renderer.drawText(NOTOSANS_18_FONT_ID, left, y, tr(STR_ID_PAIR_DONE), true, EpdFontFamily::BOLD);
      y += renderer.getLineHeight(NOTOSANS_18_FONT_ID) + 8;
      renderer.drawText(NOTOSANS_14_FONT_ID, left, y, ESPOS.user.c_str());
      confirmLabel = tr(STR_ID_SYNC_NOW);
      break;
    case State::Failed:
      renderer.drawText(NOTOSANS_14_FONT_ID, left, y, tr(STR_ID_PAIR_FAILED), true, EpdFontFamily::BOLD);
      confirmLabel = tr(STR_ID_PAIR_NEW_CODE);
      break;
  }
  (void)h;
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
