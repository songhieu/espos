#include "EspOSSyncActivity.h"

#include <FontCacheManager.h>
#include <HalStorage.h>
#include <I18n.h>
#include <LibraryBuilder.h>
#include <Logging.h>
#include <WiFi.h>

#include <cstdio>

#include "SdCardFontSystem.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "espos/EspOSNet.h"
#include "espos/EspOSStore.h"
#include "espos/EspOSSync.h"
#include "espos/EspOSUi.h"
#include "espos/FontSync.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"

extern SdCardFontSystem sdFontSystem;

namespace {
enum ItemIndex : size_t { ITEM_SETTINGS = 0, ITEM_PROGRESS = 1, FIXED_ITEMS = 2 };

void drawCheck(const GfxRenderer& r, const int x, const int y) {
  r.drawLine(x + 2, y + 11, x + 8, y + 17, 3, true);
  r.drawLine(x + 8, y + 17, x + 20, y + 4, 3, true);
}

void drawCross(const GfxRenderer& r, const int x, const int y) {
  r.drawLine(x + 4, y + 4, x + 18, y + 18, 3, true);
  r.drawLine(x + 18, y + 4, x + 4, y + 18, 3, true);
}

void drawDots(const GfxRenderer& r, const int x, const int y) {
  for (int i = 0; i < 3; i++) r.fillRect(x + 2 + i * 7, y + 10, 4, 4, true);
}

void drawWaiting(const GfxRenderer& r, const int x, const int y) {
  r.drawRoundedRect(x + 2, y + 2, 18, 18, 2, 9, true);
  r.fillRect(x + 9, y + 2, 4, 3, false);
  r.fillRect(x + 9, y + 17, 4, 3, false);
}
}  // namespace

void EspOSSyncActivity::onEnter() {
  Activity::onEnter();
  items_.clear();
  items_.reserve(FIXED_ITEMS + 4);
  if (mode_ == Mode::Sync) {
    items_.push_back({tr(STR_ID_SYNC_SETTINGS), "", ItemStatus::Waiting});
    items_.push_back({tr(STR_ID_SYNC_PROGRESS), "", ItemStatus::Waiting});
    if (!ESPOS.isLinked()) {
      fail(tr(STR_ID_NOT_LINKED));
      return;
    }
  } else {
    // Counts even when WiFi fails, so Home does not reopen this screen in a loop.
    espos::fonts::markAttempted();
  }
  networkUsed_ = true;
  headline_ = tr(STR_ID_SYNC_CONNECTING);
  if (WiFi.status() == WL_CONNECTED) {
    state_ = State::Working;
    startPending_ = true;
    requestUpdate();
    return;
  }
  state_ = State::Wifi;
  requestUpdateAndWait();
  if (espos::net::joinSaved()) {
    state_ = State::Working;
    startPending_ = true;
    requestUpdate();
    return;
  }
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled) {
                             fail(mode_ == Mode::Font ? tr(STR_EO_FONT_FAILED) : tr(STR_ID_SYNC_FAILED));
                             return;
                           }
                           state_ = State::Working;
                           startPending_ = true;
                         });
}

void EspOSSyncActivity::onExit() {
  Activity::onExit();
  if (networkUsed_) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(30);
    // TLS leaves the heap fragmented; reboot into Home like the other network screens.
    silentRestart();
  }
}

void EspOSSyncActivity::fail(const char* message) {
  {
    RenderLock lock(*this);
    state_ = State::Failed;
    headline_ = message;
  }
  requestUpdate();
}

void EspOSSyncActivity::setItem(const size_t index, const ItemStatus status, const std::string& detail) {
  RenderLock lock(*this);
  if (index >= items_.size()) return;
  items_[index].status = status;
  items_[index].detail = detail;
}

void EspOSSyncActivity::loop() {
  if (startPending_) {
    startPending_ = false;
    if (mode_ == Mode::Font) {
      runFont();
    } else {
      runSync();
    }
    return;
  }
  if (state_ == State::Done || state_ == State::Failed) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      finish();
    }
  }
}

void EspOSSyncActivity::runSync() {
  {
    RenderLock lock(*this);
    headline_ = tr(STR_ID_SYNC_RUNNING);
    for (size_t i = 0; i < FIXED_ITEMS; i++) items_[i].status = ItemStatus::Working;
  }
  requestUpdateAndWait();

  espos::sync::Outcome outcome = espos::sync::run();
  if (outcome.result != espos::client::Result::Ok) {
    for (size_t i = 0; i < FIXED_ITEMS; i++) setItem(i, ItemStatus::Failed, "");
    switch (outcome.result) {
      case espos::client::Result::Unauthorized:
      case espos::client::Result::NotLinked:
        fail(tr(STR_ID_SYNC_UNAUTHORIZED));
        break;
      case espos::client::Result::LowMemory:
        fail(tr(STR_ID_SYNC_LOW_MEMORY));
        break;
      default:
        fail(tr(STR_ID_SYNC_FAILED));
        break;
    }
    return;
  }

  char buf[96];
  snprintf(buf, sizeof(buf), tr(STR_ID_SETTINGS_APPLIED_FMT), outcome.settingsApplied);
  setItem(ITEM_SETTINGS, ItemStatus::Done, buf);
  setItem(ITEM_PROGRESS, ItemStatus::Done, "");

  bool libraryChanged = false;
  for (const auto& path : outcome.deletions) {
    if (Storage.exists(path.c_str())) libraryChanged = Storage.remove(path.c_str()) || libraryChanged;
  }

  const size_t firstBook = items_.size();
  {
    RenderLock lock(*this);
    for (const auto& d : outcome.downloads) {
      items_.push_back({d.title.empty() ? d.file : d.title, tr(STR_ID_WAITING), ItemStatus::Waiting});
    }
  }
  requestUpdateAndWait();

  if (!outcome.downloads.empty()) Storage.mkdir(espos::BOOKS_DIR);
  bool anyFailed = false;
  const std::vector<HttpDownloader::Header> headers = {{"Authorization", ESPOS.authHeader()}};
  cancellable_ = true;
  for (size_t i = 0; i < outcome.downloads.size() && !cancel_; i++) {
    const auto& d = outcome.downloads[i];
    const size_t row = firstBook + i;
    lastPercent_ = -1;
    setItem(row, ItemStatus::Working, "");
    requestUpdate(true);
    const std::string dest = std::string(espos::BOOKS_DIR) + "/" + d.file;
    // The book URL answers 302 to signed storage; HttpDownloader drops the
    // Authorization header on the cross-origin hop.
    const auto result = HttpDownloader::downloadToFile(
        d.url, dest,
        [this, row](const size_t done, const size_t total) {
          mappedInput.update(true);
          if (mappedInput.wasReleased(MappedInputManager::Button::Back)) cancel_ = true;
          const int percent = total > 0 ? static_cast<int>(static_cast<uint64_t>(done) * 100 / total) : 0;
          if (percent >= lastPercent_ + 20 || percent >= 100) {
            lastPercent_ = percent;
            char detail[48];
            snprintf(detail, sizeof(detail), tr(STR_ID_DOWNLOADING_FMT), percent);
            setItem(row, ItemStatus::Working, detail);
            requestUpdate(true);
          }
        },
        &cancel_, "", "", headers);
    if (result == HttpDownloader::OK) {
      ESPOS.setBook(d.id, d.file);
      ESPOS.saveToFile();
      libraryChanged = true;
      setItem(row, ItemStatus::Done, tr(STR_ID_DONE));
    } else {
      LOG_ERR("ESPOS", "Book %d download failed: %d", static_cast<int>(d.id), static_cast<int>(result));
      setItem(row, ItemStatus::Failed, tr(STR_ID_FAILED));
      anyFailed = true;
    }
  }

  if (!cancel_ && espos::sync::downloadFiles(outcome, &cancel_) > 0) anyFailed = true;
  cancellable_ = false;

  // The Library list reads a prebuilt index: rebuild it on the next visit.
  if (libraryChanged) library::markLibraryIndexDirty();
  // Reading font picked on the web: here, or after a reboot for a fresh heap.
  if (!cancel_ && !outcome.fontWanted.empty() && !downloadFont(outcome.fontWanted)) anyFailed = true;

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  {
    RenderLock lock(*this);
    state_ = State::Done;
    headline_ = (anyFailed || cancel_) ? tr(STR_ID_SYNC_FAILED) : tr(STR_ID_SYNC_DONE);
  }
  requestUpdate();
}

// Returns false when the font did not install (failed or stopped).
bool EspOSSyncActivity::downloadFont(const std::string& family) {
  char label[64];
  snprintf(label, sizeof(label), tr(STR_EO_FONT_ROW_FMT), family.c_str());
  size_t row = 0;
  {
    RenderLock lock(*this);
    items_.push_back({label, "", ItemStatus::Working});
    row = items_.size() - 1;
  }
  // The reading font loaded from the SD card holds RAM the TLS download needs.
  {
    RenderLock lock;  // the render task looks fonts up while it draws
    sdFontSystem.unloadForTransfer(renderer);
    if (auto* fcm = renderer.getFontCacheManager()) fcm->releaseSdFontCaches();
  }
  if (mode_ == Mode::Sync && ESP.getMaxAllocHeap() < espos::fonts::MIN_BLOCK) {
    LOG_INF("ESPOS", "Font %s: max block %u, rebooting into the font screen", family.c_str(),
            static_cast<unsigned>(ESP.getMaxAllocHeap()));
    setItem(row, ItemStatus::Working, tr(STR_EO_FONT_RESTART));
    requestUpdateAndWait();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(30);
    silentRestartToFontDownload();  // returns only when sleep supersedes it
    setItem(row, ItemStatus::Failed, tr(STR_ID_FAILED));
    return false;
  }
  espos::fonts::markAttempted();
  lastPercent_ = -1;
  cancellable_ = true;
  requestUpdate(true);
  const bool ok = espos::fonts::install(
      family,
      [this, row](const size_t done, const size_t total) {
        mappedInput.update(true);
        if (mappedInput.wasReleased(MappedInputManager::Button::Back)) cancel_ = true;
        const int percent = total > 0 ? static_cast<int>(static_cast<uint64_t>(done) * 100 / total) : 0;
        if (percent >= lastPercent_ + 10 || percent >= 100) {
          lastPercent_ = percent;
          char detail[48];
          snprintf(detail, sizeof(detail), tr(STR_ID_DOWNLOADING_FMT), percent);
          setItem(row, ItemStatus::Working, detail);
          requestUpdate(true);
        }
      },
      &cancel_);
  cancellable_ = false;
  LOG_INF("ESPOS", "Font %s: %s", family.c_str(), ok ? "installed" : (cancel_ ? "stopped" : "failed"));
  setItem(row, ok ? ItemStatus::Done : ItemStatus::Failed,
          ok ? tr(STR_EO_FONT_IN_USE) : (cancel_ ? tr(STR_EO_STOPPED) : tr(STR_ID_FAILED)));
  return ok;
}

void EspOSSyncActivity::runFont() {
  {
    RenderLock lock(*this);
    headline_ = tr(STR_EO_FONT_LOADING);
  }
  requestUpdateAndWait();
  const std::string family = espos::fonts::missing();
  const bool ok = family.empty() || downloadFont(family);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  {
    RenderLock lock(*this);
    state_ = ok ? State::Done : State::Failed;
    headline_ = ok ? tr(STR_EO_FONT_READY) : (cancel_ ? tr(STR_EO_FONT_STOPPED) : tr(STR_EO_FONT_FAILED));
  }
  requestUpdate();
}

void EspOSSyncActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, w, metrics.headerHeight}, tr(STR_ID_SYNC));

  const int left = metrics.contentSidePadding + 6;
  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + 8;
  renderer.drawText(NOTOSANS_14_FONT_ID, left, y, headline_.c_str(), true, EpdFontFamily::BOLD);
  y += renderer.getLineHeight(NOTOSANS_14_FONT_ID) + 14;

  const int bottomLimit = h - metrics.buttonHintsHeight - 70;
  const int labelH = renderer.getLineHeight(NOTOSANS_14_FONT_ID);
  const int detailH = renderer.getLineHeight(NOTOSANS_12_FONT_ID);
  for (const auto& item : items_) {
    if (y + labelH > bottomLimit) break;
    const int iconX = left;
    switch (item.status) {
      case ItemStatus::Done:
        drawCheck(renderer, iconX, y);
        break;
      case ItemStatus::Failed:
        drawCross(renderer, iconX, y);
        break;
      case ItemStatus::Working:
        drawDots(renderer, iconX, y);
        break;
      case ItemStatus::Waiting:
        drawWaiting(renderer, iconX, y);
        break;
    }
    const int textX = iconX + 34;
    espos::ui::fitText(renderer, NOTOSANS_14_FONT_ID, textX, y, item.label.c_str(), w - textX - left);
    y += labelH;
    if (!item.detail.empty()) {
      renderer.drawText(NOTOSANS_12_FONT_ID, textX, y, item.detail.c_str());
      y += detailH;
    }
    y += 10;
  }

  if (state_ == State::Done && mode_ == Mode::Sync) {
    const auto lines =
        renderer.wrappedText(NOTOSANS_12_FONT_ID, tr(STR_ID_SYNC_NOTE), w - left * 2 - 24, 3, EpdFontFamily::REGULAR);
    const int boxH = static_cast<int>(lines.size()) * detailH + 20;
    const int boxY = h - metrics.buttonHintsHeight - boxH - 12;
    renderer.drawRoundedRect(left, boxY, w - left * 2, boxH, 2, 8, true);
    int ly = boxY + 10;
    for (const auto& line : lines) {
      renderer.drawText(NOTOSANS_12_FONT_ID, left + 12, ly, line.c_str());
      ly += detailH;
    }
  }

  const bool finished = state_ == State::Done || state_ == State::Failed;
  const char* back = finished ? tr(STR_BACK) : (cancellable_ ? tr(STR_CANCEL) : "");
  const auto labels = mappedInput.mapLabels(back, "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
