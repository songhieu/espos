#include "EspOSOtaActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <I18n.h>
#include <Logging.h>
#include <esp_attr.h>
#include <esp_system.h>

#include <cstdio>

#include "SdCardFontSystem.h"
#include "SilentRestart.h"
#include "WifiCredentialStore.h"
#include "espos/EspOSFonts.h"
#include "espos/EspOSNet.h"
#include "espos/EspOSOta.h"
#include "espos/EspOSStore.h"
#include "espos/EspOSUi.h"
#include "fontIds.h"

extern SdCardFontSystem sdFontSystem;

namespace iu = espos::ui;

namespace {
constexpr int PAD = 22;
constexpr int MIN_BATTERY = 30;
constexpr uint32_t CLEAN_HEAP = 70000;  // largest free block a fresh boot has for TLS

// Restarts taken to get a clean heap, kept across those restarts only (RTC_NOINIT).
RTC_NOINIT_ATTR uint32_t s_otaMagic;
RTC_NOINIT_ATTR uint32_t s_otaRestarts;
constexpr uint32_t OTA_MAGIC = 0x07A0C0DE;

bool powerOk() { return gpio.isUsbConnected() || powerManager.getBatteryPercentage() >= MIN_BATTERY; }

std::string sizeText(const size_t bytes) {
  char buf[24];
  snprintf(buf, sizeof(buf), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
  for (char& c : buf) {
    if (c == '.') c = ',';
  }
  return buf;
}
}  // namespace

bool EspOSOtaActivity::installWanted() {
  if (esp_reset_reason() != ESP_RST_SW || s_otaMagic != OTA_MAGIC) {
    s_otaMagic = OTA_MAGIC;
    s_otaRestarts = 0;
  }
  WIFI_STORE.loadFromFile();
  return ESPOS.isLinked() && ESPOS.fwOffer.install && espos::ota::offerAvailable() && powerOk() &&
         WIFI_STORE.getCredentialCount() > 0;
}

void EspOSOtaActivity::onEnter() {
  Activity::onEnter();
  mode_ = install_ ? Mode::Installing : Mode::Info;
  requestUpdate();
}

void EspOSOtaActivity::loop() {
  using B = MappedInputManager::Button;
  switch (mode_) {
    case Mode::Installing:
      if (!started_) {
        started_ = true;
        runInstall();
      }
      return;
    case Mode::Restarting:
      return;
    case Mode::Failed:
      if (mappedInput.wasReleased(B::Back) || mappedInput.wasReleased(B::Confirm)) {
        // TLS leaves the heap fragmented: back to a clean Home.
        silentRestart();
      }
      return;
    case Mode::Info:
      break;
  }
  if (mappedInput.wasReleased(B::Back)) {
    finish();
    return;
  }
  if (!espos::ota::offerAvailable()) return;
  if (mappedInput.wasPressed(B::Down) || mappedInput.wasPressed(B::Right) || mappedInput.wasPressed(B::NavNext) ||
      mappedInput.wasPressed(B::Up) || mappedInput.wasPressed(B::Left) || mappedInput.wasPressed(B::NavPrevious)) {
    selected_ = 1 - selected_;
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(B::Confirm)) {
    if (selected_ == 1) {
      finish();
      return;
    }
    if (!powerOk()) {
      error_ = tr(STR_EO_OTA_LOW_BATTERY);
      requestUpdate();
      return;
    }
    // Install from a fresh boot: the download needs the heap a clean start has.
    ESPOS.fwOffer.install = true;
    ESPOS.saveToFile();
    silentRestart();
  }
}

void EspOSOtaActivity::runInstall() {
  // A reading font from the SD card is loaded at boot and takes the RAM the TLS download
  // needs (with Lexica selected the install failed at once). The restart after the install
  // loads it again.
  {
    RenderLock lock;  // the render task looks fonts up while it draws
    sdFontSystem.unloadForTransfer(renderer);
    if (auto* fcm = renderer.getFontCacheManager()) fcm->releaseSdFontCaches();
  }
  LOG_INF("OTA", "Install %s: max block %u", ESPOS.fwOffer.version.c_str(),
          static_cast<unsigned>(ESP.getMaxAllocHeap()));
  if (ESP.getMaxAllocHeap() < CLEAN_HEAP && s_otaRestarts < 2) {
    s_otaRestarts++;
    silentRestart();
    return;
  }
  // A crash mid-install must not loop: three tries per offered version.
  if (ESPOS.fwOffer.attempts >= 3) {
    ESPOS.fwOffer.install = false;
    ESPOS.saveToFile();
    error_ = tr(STR_EO_OTA_FAILED);
    mode_ = Mode::Failed;
    requestUpdate();
    return;
  }
  ESPOS.fwOffer.attempts++;
  ESPOS.saveToFile();
  total_ = ESPOS.fwOffer.size;
  requestUpdateAndWait();

  const espos::ota::Result result = espos::ota::install(
      [this](const size_t done, const size_t total) {
        done_ = done;
        if (total > 0) total_ = total;
        const int pct = total_ > 0 ? static_cast<int>(static_cast<uint64_t>(done) * 100 / total_) : 0;
        // E-ink: repaint every 10%.
        if (pct >= percent_ + 10) {
          percent_ = pct - pct % 10;
          requestUpdateAndWait();
        }
      },
      [] { return espos::net::joinSaved(12000); });
  espos::net::off();

  if (result == espos::ota::Result::Ok) {
    percent_ = 100;
    mode_ = Mode::Restarting;
    requestUpdateAndWait();
    delay(300);
    esp_restart();
    return;
  }
  switch (result) {
    case espos::ota::Result::Network:
      error_ = tr(STR_EO_OTA_ERR_NETWORK);
      break;
    case espos::ota::Result::Hash:
    case espos::ota::Result::WrongImage:
      error_ = tr(STR_EO_OTA_ERR_IMAGE);
      break;
    default:
      error_ = tr(STR_EO_OTA_FAILED);
      break;
  }
  mode_ = Mode::Failed;
  requestUpdate();
}

void EspOSOtaActivity::render(RenderLock&&) {
  renderer.clearScreen();
  if (mode_ == Mode::Info) {
    renderInfo();
  } else {
    renderProgress();
  }
  renderer.displayBuffer();
}

void EspOSOtaActivity::renderInfo() {
  iu::header(renderer, tr(STR_EO_OTA_TITLE));
  const int w = renderer.getScreenWidth() - PAD * 2;
  int y = iu::HEADER_H + 16;
  const auto& offer = ESPOS.fwOffer;
  const bool has = espos::ota::offerAvailable();

  iu::kicker(renderer, PAD, y, tr(STR_EO_OTA_RUNNING_CAPS));
  y += renderer.getLineHeight(iu::fontKicker()) + 2;
  renderer.drawText(ESPOS_24_FONT_ID, PAD, y, espos::ota::shortVersion(espos::FW_VERSION).c_str(), true,
                    EpdFontFamily::BOLD);
  y += renderer.getLineHeight(ESPOS_24_FONT_ID) + 10;

  if (!has) {
    iu::wrap(renderer, iu::fontBody(), PAD, y, w, tr(STR_EO_OTA_UP_TO_DATE), 2, true, EpdFontFamily::BOLD);
    y += renderer.getLineHeight(iu::fontBody()) * 2 + 8;
    char when[8];
    ESPOS.formatClock(ESPOS.lastSyncMs, when, sizeof(when));
    char buf[256];
    snprintf(buf, sizeof(buf), tr(STR_EO_OTA_CHECKED_FMT), ESPOS.lastSyncMs > 0 ? when : "—");
    iu::wrap(renderer, iu::fontSmall(), PAD, y, w, buf, 6);
    iu::hints(renderer, mappedInput, tr(STR_BACK), "", "", "");
    return;
  }

  iu::kicker(renderer, PAD, y, tr(STR_EO_OTA_NEW_CAPS));
  y += renderer.getLineHeight(iu::fontKicker()) + 2;
  const std::string ver = espos::ota::shortVersion(offer.version);
  renderer.drawText(ESPOS_24_FONT_ID, PAD, y, ver.c_str(), true, EpdFontFamily::BOLD);
  std::string meta = offer.date.empty() ? std::string() : " · " + offer.date;
  if (offer.size > 0) meta += " · " + sizeText(offer.size);
  const int vw = renderer.getTextWidth(ESPOS_24_FONT_ID, ver.c_str(), EpdFontFamily::BOLD);
  renderer.drawText(iu::fontSmall(), PAD + vw + 8,
                    y + renderer.getFontAscenderSize(ESPOS_24_FONT_ID) - renderer.getFontAscenderSize(iu::fontSmall()),
                    meta.c_str(), true, EpdFontFamily::BOLD);
  y += renderer.getLineHeight(ESPOS_24_FONT_ID) + 8;

  // What's new: one item per line.
  const int lh = renderer.getLineHeight(iu::fontSmall());
  size_t pos = 0;
  int lines = 0;
  while (pos < offer.notes.size() && lines < 6) {
    size_t end = offer.notes.find('\n', pos);
    if (end == std::string::npos) end = offer.notes.size();
    const std::string item = "• " + offer.notes.substr(pos, end - pos);
    pos = end + 1;
    const int used = iu::wrap(renderer, iu::fontSmall(), PAD, y, w, item.c_str(), 2);
    y += used > 0 ? used : lh;
    lines++;
  }
  y += 12;

  const int rowH = iu::rowHeight(renderer, true);
  iu::row(renderer, PAD, y, w, tr(STR_EO_OTA_NOW), tr(STR_EO_OTA_NOW_SUB), "", selected_ == 0);
  y += rowH + 8;
  iu::row(renderer, PAD, y, w, tr(STR_EO_OTA_LATER), "", "", selected_ == 1);
  y += iu::rowHeight(renderer, false) + 12;
  iu::wrap(renderer, iu::fontSmall(), PAD, y, w, error_.empty() ? tr(STR_EO_OTA_NOTE) : error_.c_str(), 3, true,
           error_.empty() ? 0 : EpdFontFamily::BOLD);
  iu::hints(renderer, mappedInput, tr(STR_BACK), tr(STR_EO_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
}

void EspOSOtaActivity::renderProgress() {
  const int w = renderer.getScreenWidth() - PAD * 2 - 10;
  const int x = PAD + 5;
  int y = renderer.getScreenHeight() / 2 - 150;
  iu::kicker(renderer, x, y, "ESPOS");
  y += renderer.getLineHeight(iu::fontKicker()) + 6;
  char buf[160];
  const char* title = mode_ == Mode::Failed       ? tr(STR_EO_OTA_FAIL_TITLE)
                      : mode_ == Mode::Restarting ? tr(STR_EO_OTA_RESTARTING)
                                                  : tr(STR_EO_OTA_INSTALLING_FMT);
  snprintf(buf, sizeof(buf), title, espos::ota::shortVersion(ESPOS.fwOffer.version).c_str());
  y += iu::wrap(renderer, ESPOS_24_FONT_ID, x, y, w, buf, 3, true, EpdFontFamily::BOLD, 0);
  y += 22;
  if (mode_ == Mode::Failed) {
    iu::wrap(renderer, iu::fontBody(), x, y, w, error_.c_str(), 4);
    y += renderer.getLineHeight(iu::fontBody()) * 2 + 16;
    iu::wrap(renderer, iu::fontSmall(), x, y, w, tr(STR_EO_OTA_KEPT_OLD), 3);
    iu::hints(renderer, mappedInput, tr(STR_BACK), tr(STR_EO_OPEN), "", "");
    return;
  }
  iu::progressBar(renderer, x, y, w, 20, percent_);
  y += 20 + 8;
  snprintf(buf, sizeof(buf), "%d%%", percent_);
  renderer.drawText(iu::fontSmall(), x, y, buf, true, EpdFontFamily::BOLD);
  if (total_ > 0) {
    const std::string sz = sizeText(done_) + " / " + sizeText(total_);
    const int sw = renderer.getTextWidth(iu::fontSmall(), sz.c_str(), EpdFontFamily::BOLD);
    renderer.drawText(iu::fontSmall(), x + w - sw, y, sz.c_str(), true, EpdFontFamily::BOLD);
  }
  y += renderer.getLineHeight(iu::fontSmall()) + 30;
  iu::wrap(renderer, iu::fontSmall(), x, y, w, tr(STR_EO_OTA_DONT_OFF), 4);
}
