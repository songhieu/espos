#include "InkHomeActivity.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "AppsHubActivity.h"
#include "CrossPointSettings.h"
#include "InkDeskSyncActivity.h"
#include "InkOtaActivity.h"
#include "InkSetupActivity.h"
#include "InkStatsActivity.h"
#include "InkWifiPromptActivity.h"
#include "SilentRestart.h"
#include "WifiCredentialStore.h"
#include "activities/ActivityManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "inkdesk/FontSync.h"
#include "inkdesk/InkDeskStore.h"
#include "inkdesk/DashboardView.h"
#include "inkdesk/InkDeskSync.h"
#include "inkdesk/InkFonts.h"
#include "inkdesk/InkNet.h"
#include "inkdesk/InkOta.h"
#include "inkdesk/InkUi.h"

namespace iu = inkdesk::ui;
namespace wg = inkdesk::widgets;

namespace {
constexpr int COVER_H = 156;
constexpr int SIDE = 12;
constexpr int STATUS_H = 40;
}  // namespace

void InkHomeActivity::onEnter() {
  Activity::onEnter();
  loadData();
  ensureCoverThumb();
  // A firmware update the web asked for goes first; sync and fonts follow after its restart.
  otaInstall_ = InkOtaActivity::installWanted();
  autoSync_ = !otaInstall_ && shouldAutoSync();
  fontInstall_ = !otaInstall_ && shouldInstallFont();
  banner_ = INKDESK.justUpdated;
  if (!banner_.empty()) {
    INKDESK.justUpdated.clear();
    INKDESK.saveToFile();
  }
  enterMs_ = millis();
  lastMinute_ = data_.timeValid ? data_.now.tm_min : -1;
  // Linked, never synced, and no WiFi saved at all: nothing to try, ask now.
  if (!autoSync_ && linked_ && INKDESK.lastSyncMs <= 0 && WIFI_STORE.getCredentialCount() == 0) promptWifi();
  requestUpdate();
}

namespace {
// The WiFi prompt shows at most once per boot.
bool s_wifiPrompted = false;
}  // namespace

bool InkHomeActivity::shouldInstallFont() const {
  const std::string missing = inkdesk::fonts::missing();
  WIFI_STORE.loadFromFile();
  const bool go = linked_ && !inkdesk::fonts::attempted() && !missing.empty() &&
                  WIFI_STORE.getCredentialCount() > 0 && powerManager.getBatteryPercentage() >= 15;
  LOG_DBG("IKHM", "Font check: want=%s missing=%s -> %s", INKDESK.get("read.font", "-").c_str(), missing.c_str(),
          go ? "install" : "skip");
  return go;
}

// The font screen shows the download and lets Back stop it; it needs a fresh heap for the
// catalogue's TLS session, so reboot into it unless Home already has one.
void InkHomeActivity::openFontDownload() {
  if (ESP.getMaxAllocHeap() < inkdesk::fonts::MIN_BLOCK) {
    silentRestartToFontDownload();
    return;
  }
  startActivityForResult(
      std::make_unique<InkDeskSyncActivity>(renderer, mappedInput, InkDeskSyncActivity::Mode::Font),
      [this](const ActivityResult&) { requestUpdate(); });
}

void InkHomeActivity::promptWifi() {
  if (s_wifiPrompted || !linked_ || INKDESK.lastSyncMs > 0) return;
  s_wifiPrompted = true;
  startActivityForResult(std::make_unique<InkWifiPromptActivity>(renderer, mappedInput), [this](const ActivityResult&) {
    loadData();
    requestUpdate();
  });
}

bool InkHomeActivity::shouldAutoSync() const {
  if (!linked_ || inkdesk::net::autoSyncTried()) return false;
  WIFI_STORE.loadFromFile();
  if (WIFI_STORE.getCredentialCount() == 0) return false;
  if (powerManager.getBatteryPercentage() < 8 || inkdesk::dashboard::batterySaving(gpio.isUsbConnected())) return false;
  const int64_t now = InkDeskStore::nowMs();
  const bool stale = INKDESK.lastSyncMs <= 0 || !InkDeskStore::clockValid() || now - INKDESK.lastSyncMs > 30LL * 60 * 1000;
  return stale;
}

void InkHomeActivity::runAutoSync() {
  inkdesk::net::markAutoSyncTried();
  syncing_ = true;
  requestUpdateAndWait();
  const bool joined = inkdesk::net::joinSaved(8000);
  if (joined) {
    const auto outcome = inkdesk::sync::run();
    if (outcome.result == inkdesk::client::Result::Ok) inkdesk::sync::downloadFiles(outcome);
  }
  inkdesk::net::off();
  // Linked but never synced and the saved WiFi does not answer: ask instead of staying silent.
  if (!joined && INKDESK.lastSyncMs <= 0) {
    syncing_ = false;
    promptWifi();
    return;
  }
  // TLS leaves the heap fragmented: come back to a clean Home that shows the fresh data.
  if (joined) silentRestart();
  syncing_ = false;
  requestUpdate();
}

void InkHomeActivity::loadData() {
  linked_ = INKDESK.isLinked();
  layout_ = wg::load(wg::Screen::Home, linked_);
  data_ = wg::Data{};
  wg::loadData(data_, COVER_H);
  data_.showSeq = wg::showSeq(true);
  // Focus the book (or the first widget that opens something).
  focus_ = -1;
  sub_ = 0;
  for (size_t i = 0; i < layout_.items.size(); i++) {
    const auto a = wg::action(layout_.items[i]);
    if (a == wg::Action::None) continue;
    if (focus_ < 0) focus_ = static_cast<int>(i);
    if (a == wg::Action::Reader || a == wg::Action::Setup) {
      focus_ = static_cast<int>(i);
      break;
    }
  }
}

void InkHomeActivity::ensureCoverThumb() {
  // Thumbnails at the heights this layout draws (current book, library shelf); the first time they are
  // made from the book, which takes a moment.
  const auto boxes = wg::boxesFor(layout_);
  if (!wg::ensureCovers(layout_, boxes, data_, true)) return;
  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  wg::ensureCovers(layout_, boxes, data_);
}

void InkHomeActivity::moveFocus(const int dx, const int dy) {
  if (focus_ < 0 || boxes_.size() != layout_.items.size()) return;
  auto focusable = [&](const size_t i) { return wg::action(layout_.items[i]) != wg::Action::None; };
  auto books = [&](const int i) { return wg::bookCount(renderer, layout_.items[i], boxes_[i], data_); };
  // Inside a library widget the buttons step through its books first (a list also with up and down).
  const int inBooks = books(focus_);
  const int step = dx != 0 ? dx : layout_.items[focus_].style == "list" ? dy : 0;
  if (inBooks > 1 && step != 0 && sub_ + step >= 0 && sub_ + step < inBooks) {
    sub_ += step;
    requestUpdate();
    return;
  }
  int next = focus_;
  if (dx != 0) {
    // Reading order (rows, then columns), wrapping around.
    std::vector<int> order;
    for (size_t i = 0; i < layout_.items.size(); i++) {
      if (focusable(i)) order.push_back(static_cast<int>(i));
    }
    std::sort(order.begin(), order.end(), [&](const int a, const int b) {
      const auto& A = layout_.items[a];
      const auto& B = layout_.items[b];
      return A.y != B.y ? A.y < B.y : A.x < B.x;
    });
    const auto pos = std::find(order.begin(), order.end(), focus_) - order.begin();
    const int n = static_cast<int>(order.size());
    if (n == 0) return;
    next = order[(pos + dx + n) % n];
  } else {
    // The nearest widget above or below, preferring the same column.
    const auto& cur = boxes_[focus_];
    const int ccx = cur.x + cur.w / 2;
    long best = LONG_MAX;
    for (size_t i = 0; i < boxes_.size(); i++) {
      if (static_cast<int>(i) == focus_ || !focusable(i)) continue;
      const auto& b = boxes_[i];
      const bool below = b.y >= cur.y + cur.h - 1;
      const bool above = b.y + b.h <= cur.y + 1;
      if ((dy > 0 && !below) || (dy < 0 && !above)) continue;
      const long vd = dy > 0 ? b.y - (cur.y + cur.h) : cur.y - (b.y + b.h);
      const long hd = std::labs(static_cast<long>(b.x + b.w / 2 - ccx));
      const long score = vd + hd * 2;
      if (score < best) {
        best = score;
        next = static_cast<int>(i);
      }
    }
  }
  if (next != focus_) {
    focus_ = next;
    // Coming back into a library widget from after it lands on its last book.
    const int n = books(next);
    sub_ = dx < 0 && n > 0 ? n - 1 : 0;
    requestUpdate();
  }
}

void InkHomeActivity::loop() {
  using B = MappedInputManager::Button;
  if (otaInstall_ && millis() - enterMs_ > 800) {
    otaInstall_ = false;
    startActivityForResult(std::make_unique<InkOtaActivity>(renderer, mappedInput, true), [this](const ActivityResult&) {
      loadData();
      requestUpdate();
    });
    return;
  }
  if (fontInstall_ && millis() - enterMs_ > 800) {
    fontInstall_ = false;
    openFontDownload();
    return;
  }
  if (autoSync_) {
    // Any button in the first moments means the user is busy: skip the quick sync.
    if (mappedInput.wasPressed(B::Confirm) || mappedInput.wasPressed(B::Back) || mappedInput.wasPressed(B::Left) ||
        mappedInput.wasPressed(B::Right) || mappedInput.wasPressed(B::Up) || mappedInput.wasPressed(B::Down)) {
      autoSync_ = false;
    } else if (millis() - enterMs_ > 1500) {
      autoSync_ = false;
      runAutoSync();
      return;
    }
  }
  if (mappedInput.wasPressed(B::Right) || mappedInput.wasPressed(B::NavNext)) return moveFocus(1, 0);
  if (mappedInput.wasPressed(B::Left) || mappedInput.wasPressed(B::NavPrevious)) return moveFocus(-1, 0);
  if (mappedInput.wasPressed(B::Down)) return moveFocus(0, 1);
  if (mappedInput.wasPressed(B::Up)) return moveFocus(0, -1);
  if (mappedInput.wasReleased(B::Confirm)) return activate(focus_);
  // Back always reaches the apps, whatever the layout holds.
  if (mappedInput.wasReleased(B::Back)) {
    activityManager.replaceActivity(std::make_unique<AppsHubActivity>(renderer, mappedInput));
    return;
  }
  // Clocks on Home move with the minute.
  struct tm now {};
  if (iu::localTime(now) && now.tm_min != lastMinute_) {
    RenderLock lock(*this);
    lastMinute_ = now.tm_min;
    data_.now = now;
    data_.timeValid = true;
    requestUpdate();
  }
}

void InkHomeActivity::activate(const int index) {
  if (index < 0 || index >= static_cast<int>(layout_.items.size())) return;
  const auto onBack = [this](const ActivityResult&) {
    loadData();
    requestUpdate();
  };
  auto setup = [&]() { startActivityForResult(std::make_unique<InkSetupActivity>(renderer, mappedInput), onBack); };
  switch (wg::action(layout_.items[index])) {
    case wg::Action::Reader:
      if (data_.hasBook) {
        activityManager.goToReader(data_.bookPath);
      } else {
        activityManager.goToLibrary();
      }
      break;
    case wg::Action::Library: {
      // A library widget opens the book chosen in it; the shortcut opens the Library.
      const std::string path = wg::bookPath(layout_.items[index], data_, sub_);
      if (layout_.items[index].kind == wg::Kind::Books && !path.empty()) {
        activityManager.goToReader(path);
      } else {
        activityManager.goToLibrary();
      }
      break;
    }
    case wg::Action::News:
      if (!data_.newsPath.empty()) {
        activityManager.goToReader(data_.newsPath);
      } else {
        GUI.drawPopup(renderer, tr(STR_IK_NEWS_NONE));
        delay(2500);
        requestUpdate();
      }
      break;
    case wg::Action::Stats:
      startActivityForResult(std::make_unique<InkStatsActivity>(renderer, mappedInput), onBack);
      break;
    case wg::Action::Sync:
      if (linked_) {
        startActivityForResult(std::make_unique<InkDeskSyncActivity>(renderer, mappedInput), onBack);
      } else {
        setup();
      }
      break;
    case wg::Action::Transfer:
      activityManager.goToFileTransfer();
      break;
    case wg::Action::Apps:
      activityManager.replaceActivity(std::make_unique<AppsHubActivity>(renderer, mappedInput));
      break;
    case wg::Action::Settings:
      activityManager.goToSettings();
      break;
    case wg::Action::Update:
      startActivityForResult(std::make_unique<InkOtaActivity>(renderer, mappedInput, false), onBack);
      break;
    case wg::Action::Setup:
      setup();
      break;
    default:
      break;
  }
}

void InkHomeActivity::drawStatus(const int h) {
  // Clock and date on the left, battery on the right.
  const int x = SIDE + 4;
  const int battLeft = iu::battery(renderer, renderer.getScreenWidth() - SIDE - 4, h / 2 + 2);
  int right = battLeft - 14;
  if (inkdesk::ota::offerAvailable() && statusMsg_.empty() && !syncing_) {
    const char* pill = tr(STR_IK_OTA_PILL);
    const int pw = renderer.getTextWidth(iu::fontKicker(), pill, EpdFontFamily::BOLD) + 14;
    const int ph = renderer.getLineHeight(iu::fontKicker()) + 2;
    renderer.fillRoundedRect(right - pw, (h - ph) / 2 + 2, pw, ph, ph / 2, Color::Black);
    renderer.drawText(iu::fontKicker(), right - pw + 7, (h - ph) / 2 + 3, pill, false, EpdFontFamily::BOLD);
    right -= pw + 12;
  }
  if (syncing_ || !statusMsg_.empty()) {
    const char* st = statusMsg_.empty() ? tr(STR_ID_SYNC_RUNNING) : statusMsg_.c_str();
    const int stw = renderer.getTextWidth(iu::fontSmall(), st, EpdFontFamily::BOLD);
    renderer.drawText(iu::fontSmall(), right - stw, (h - renderer.getLineHeight(iu::fontSmall())) / 2 + 2, st, true,
                      EpdFontFamily::BOLD);
    right -= stw + 14;
  }
  if (data_.timeValid) {
    char clock[8];
    const int hour = SETTINGS.clockFormat == 1 ? (data_.now.tm_hour % 12 == 0 ? 12 : data_.now.tm_hour % 12) : data_.now.tm_hour;
    snprintf(clock, sizeof(clock), "%02d:%02d", hour, data_.now.tm_min);
    // Big clock, then the date sitting on the same baseline.
    const int clockFont = NOTOSANS_14_FONT_ID;
    const int clockY = (h - renderer.getLineHeight(clockFont)) / 2 + 2;
    renderer.drawText(clockFont, x, clockY, clock, true, EpdFontFamily::BOLD);
    const int baseline = clockY + renderer.getFontAscenderSize(clockFont);
    char date[48];
    snprintf(date, sizeof(date), "%s · %02d/%02d", iu::weekdayCaps(data_.now.tm_wday), data_.now.tm_mday,
             data_.now.tm_mon + 1);
    const int dx = x + renderer.getTextWidth(clockFont, clock, EpdFontFamily::BOLD) + 12;
    // Short weekday ("T7 · 14/11") when the full one does not fit beside the status pills.
    if (renderer.getTextWidth(iu::fontKicker(), date, EpdFontFamily::BOLD) > right - dx) {
      char names[64];
      snprintf(names, sizeof(names), "%s", tr(STR_IK_WDAYS_SHORT));
      const char* wd = "";
      char* save = nullptr;
      int i = 0;
      for (char* t = strtok_r(names, ",", &save); t; t = strtok_r(nullptr, ",", &save), i++) {
        if (i == data_.now.tm_wday) wd = t;
      }
      snprintf(date, sizeof(date), "%s · %02d/%02d", wd, data_.now.tm_mday, data_.now.tm_mon + 1);
    }
    iu::fitText(renderer, iu::fontKicker(), dx, baseline - renderer.getFontAscenderSize(iu::fontKicker()), date,
                right - dx, true, EpdFontFamily::BOLD);
  } else {
    char line[96];
    const char* name = linked_ && !INKDESK.deviceName.empty() ? INKDESK.deviceName.c_str() : "InkDesk";
    snprintf(line, sizeof(line), "%s · %s", name, tr(STR_IK_NO_CLOCK));
    iu::fitText(renderer, iu::fontSmall(), x, (h - renderer.getLineHeight(iu::fontSmall())) / 2 + 2, line, right - x, true,
                EpdFontFamily::BOLD);
  }
}

void InkHomeActivity::drawBanner(const wg::Box& b) {
  // Right after an update: say so once, in place of the top widget.
  renderer.fillRoundedRect(b.x, b.y, b.w, b.h, 12, Color::Black);
  char title[64];
  snprintf(title, sizeof(title), tr(STR_IK_OTA_DONE_FMT), banner_.c_str());
  const int x = b.x + 22;
  int ty = b.y + (b.h - renderer.getLineHeight(INK_24_FONT_ID) - renderer.getLineHeight(iu::fontSmall())) / 2;
  renderer.drawText(INK_24_FONT_ID, x, ty, title, false, EpdFontFamily::BOLD);
  ty += renderer.getLineHeight(INK_24_FONT_ID);
  iu::wrap(renderer, iu::fontSmall(), x, ty, b.w - 44, tr(STR_IK_OTA_DONE_SUB), 2, false);
}

void InkHomeActivity::render(RenderLock&&) {
  renderer.clearScreen();
  drawStatus(STATUS_H);
  boxes_ = wg::boxesFor(layout_);
  bool bannerDrawn = banner_.empty();
  for (size_t i = 0; i < layout_.items.size(); i++) {
    if (!bannerDrawn && layout_.items[i].y == 0) {
      drawBanner(boxes_[i]);
      bannerDrawn = true;
      continue;
    }
    data_.bookFocus = static_cast<int>(i) == focus_ ? sub_ : -1;
    wg::draw(renderer, layout_.items[i], boxes_[i], data_, static_cast<int>(i) == focus_, layout_.skin);
  }
  data_.bookFocus = -1;
  iu::hints(renderer, mappedInput, tr(STR_IK_HOME_BACK), focus_ >= 0 ? tr(STR_IK_OPEN) : "", tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  renderer.displayBuffer();
  // Home came up: this firmware works, keep it.
  inkdesk::ota::confirmRunning();
}
