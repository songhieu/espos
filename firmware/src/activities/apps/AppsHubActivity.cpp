#include "AppsHubActivity.h"

#include <I18n.h>

#include <cstdio>
#include <vector>

#include "DashboardSettingsActivity.h"
#include "EspOSOtaActivity.h"
#include "EspOSStatsActivity.h"
#include "EspOSSyncActivity.h"
#include "PairActivity.h"
#include "PkgListActivity.h"
#include "activities/ActivityManager.h"
#include "components/icons/blocks.h"
#include "components/icons/book.h"
#include "components/icons/folder.h"
#include "components/icons/library.h"
#include "components/icons/listIcons.h"
#include "components/icons/transfer.h"
#include "components/icons/wifi.h"
#include "espos/EspOSOta.h"
#include "espos/EspOSStore.h"
#include "espos/Pkgs.h"
#include "espos/ReadingStats.h"

namespace {
enum Row {
  ROW_NEWS = 0,
  ROW_STATS,
  ROW_DASHBOARD,
  ROW_PKGS,
  ROW_SYNC,
  ROW_UPDATE,
  ROW_LINK,
  ROW_FILES,
  ROW_TRANSFER,
  ROW_COUNT
};
}

const char* AppsHubActivity::title() const { return tr(STR_ID_APPS); }

std::string AppsHubActivity::noteText() const { return tr(STR_EO_APPS_TIP); }

void AppsHubActivity::refresh() {
  char buf[96];
  items_.assign(ROW_COUNT, Item{});
  std::string newsDay;
  newsPath_ = ESPOS.latestNewsPath(&newsDay);
  if (!newsPath_.empty()) {
    snprintf(buf, sizeof(buf), tr(STR_EO_NEWS_ISSUE_FMT), newsDay.c_str());
    items_[ROW_NEWS] = {tr(STR_EO_NEWS), buf, "", icon_file_text_32_bits};
  } else {
    items_[ROW_NEWS] = {tr(STR_EO_NEWS), tr(STR_EO_NEWS_NONE_SHORT), "", icon_file_text_32_bits};
  }
  {
    const auto st = espos::stats::summary();
    if (st.dated) {
      snprintf(buf, sizeof(buf), tr(STR_EO_TODAY_READ_FMT), static_cast<int>(st.todaySec / 60), st.goalMin);
      items_[ROW_STATS] = {tr(STR_EO_STATS_TITLE), buf, "", icon_book_open_32_bits};
    } else {
      items_[ROW_STATS] = {tr(STR_EO_STATS_TITLE), tr(STR_EO_STATS_SUB_NOCLOCK), "", icon_book_open_32_bits};
    }
  }
  if (espos::ota::offerAvailable()) {
    snprintf(buf, sizeof(buf), tr(STR_EO_OTA_SUB_NEW_FMT), espos::ota::shortVersion(espos::FW_VERSION).c_str(),
             espos::ota::shortVersion(ESPOS.fwOffer.version).c_str());
  } else {
    snprintf(buf, sizeof(buf), tr(STR_EO_OTA_SUB_LATEST_FMT), espos::ota::shortVersion(espos::FW_VERSION).c_str());
  }
  items_[ROW_UPDATE] = {tr(STR_EO_OTA_TITLE), buf, "", icon_download_32_bits};
  if (ESPOS.get("dash.enabled", "1") == "0") {
    items_[ROW_DASHBOARD] = {tr(STR_ID_DASHBOARD), tr(STR_ID_STATE_OFF), "", BlocksIcon};
  } else {
    snprintf(buf, sizeof(buf), tr(STR_ID_DASH_SUB_ON_FMT), ESPOS.getInt("dash.interval_min", 30));
    items_[ROW_DASHBOARD] = {tr(STR_ID_DASHBOARD), buf, "", BlocksIcon};
  }
  {
    int faces = 0, widgets = 0, themes = 0;
    for (const auto& p : espos::pkgs::list()) (p.type == "face" ? faces : p.type == "widget" ? widgets : themes)++;
    if (faces + widgets + themes > 0) {
      snprintf(buf, sizeof(buf), tr(STR_EO_PKGS_SUB_FMT), faces, widgets, themes);
      items_[ROW_PKGS] = {tr(STR_EO_PKGS), buf, "", BlocksIcon};
    } else {
      items_[ROW_PKGS] = {tr(STR_EO_PKGS), tr(STR_EO_PKGS_NONE), "", BlocksIcon};
    }
  }
  if (ESPOS.lastSyncMs > 0) {
    char clock[8];
    ESPOS.formatClock(ESPOS.lastSyncMs, clock, sizeof(clock));
    snprintf(buf, sizeof(buf), tr(STR_ID_SYNC_LAST_FMT), clock);
    items_[ROW_SYNC] = {tr(STR_ID_SYNC), buf, "", WifiIcon};
  } else {
    items_[ROW_SYNC] = {tr(STR_ID_SYNC), tr(STR_ID_SYNC_NEVER), "", WifiIcon};
  }
  if (ESPOS.isLinked()) {
    snprintf(buf, sizeof(buf), tr(STR_ID_LINKED_TO_FMT),
             ESPOS.user.empty() ? ESPOS.deviceName.c_str() : ESPOS.user.c_str());
    items_[ROW_LINK] = {tr(STR_ID_LINK), buf, "", LibraryIcon};
  } else {
    items_[ROW_LINK] = {tr(STR_ID_LINK), tr(STR_ID_NOT_LINKED), "", LibraryIcon};
  }
  items_[ROW_FILES] = {tr(STR_BROWSE_FILES), tr(STR_EO_FILES_SUB), "", FolderIcon};
  items_[ROW_TRANSFER] = {tr(STR_FILE_TRANSFER), tr(STR_EO_TRANSFER_SUB), "", TransferIcon};
}

void AppsHubActivity::activate(const int index) {
  const auto onBack = [this](const ActivityResult&) {
    refresh();
    requestUpdate();
  };
  switch (index) {
    case ROW_NEWS:
      if (!newsPath_.empty()) activityManager.goToReader(newsPath_);
      break;
    case ROW_STATS:
      startActivityForResult(std::make_unique<EspOSStatsActivity>(renderer, mappedInput), onBack);
      break;
    case ROW_UPDATE:
      startActivityForResult(std::make_unique<EspOSOtaActivity>(renderer, mappedInput, false), onBack);
      break;
    case ROW_DASHBOARD:
      startActivityForResult(std::make_unique<DashboardSettingsActivity>(renderer, mappedInput), onBack);
      break;
    case ROW_PKGS:
      startActivityForResult(std::make_unique<PkgListActivity>(renderer, mappedInput), onBack);
      break;
    case ROW_SYNC:
      if (ESPOS.isLinked()) {
        startActivityForResult(std::make_unique<EspOSSyncActivity>(renderer, mappedInput), onBack);
      } else {
        startActivityForResult(std::make_unique<PairActivity>(renderer, mappedInput), onBack);
      }
      break;
    case ROW_LINK:
      startActivityForResult(std::make_unique<PairActivity>(renderer, mappedInput), onBack);
      break;
    case ROW_FILES:
      activityManager.goToFileBrowser();
      break;
    case ROW_TRANSFER:
      activityManager.goToFileTransfer();
      break;
    default:
      break;
  }
}
