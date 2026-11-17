#include "AppsHubActivity.h"

#include <I18n.h>

#include <cstdio>
#include <vector>

#include "DashboardSettingsActivity.h"
#include "InkOtaActivity.h"
#include "InkStatsActivity.h"
#include "InkDeskSyncActivity.h"
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
#include "inkdesk/InkDeskStore.h"
#include "inkdesk/InkOta.h"
#include "inkdesk/Pkgs.h"
#include "inkdesk/ReadingStats.h"

namespace {
enum Row { ROW_NEWS = 0, ROW_STATS, ROW_DASHBOARD, ROW_PKGS, ROW_SYNC, ROW_UPDATE, ROW_LINK, ROW_FILES, ROW_TRANSFER, ROW_COUNT };
}

const char* AppsHubActivity::title() const { return tr(STR_ID_APPS); }

std::string AppsHubActivity::noteText() const { return tr(STR_IK_APPS_TIP); }

void AppsHubActivity::refresh() {
  char buf[96];
  items_.assign(ROW_COUNT, Item{});
  std::string newsDay;
  newsPath_ = INKDESK.latestNewsPath(&newsDay);
  if (!newsPath_.empty()) {
    snprintf(buf, sizeof(buf), tr(STR_IK_NEWS_ISSUE_FMT), newsDay.c_str());
    items_[ROW_NEWS] = {tr(STR_IK_NEWS), buf, "", icon_file_text_32_bits};
  } else {
    items_[ROW_NEWS] = {tr(STR_IK_NEWS), tr(STR_IK_NEWS_NONE_SHORT), "", icon_file_text_32_bits};
  }
  {
    const auto st = inkdesk::stats::summary();
    if (st.dated) {
      snprintf(buf, sizeof(buf), tr(STR_IK_TODAY_READ_FMT), static_cast<int>(st.todaySec / 60), st.goalMin);
      items_[ROW_STATS] = {tr(STR_IK_STATS_TITLE), buf, "", icon_book_open_32_bits};
    } else {
      items_[ROW_STATS] = {tr(STR_IK_STATS_TITLE), tr(STR_IK_STATS_SUB_NOCLOCK), "", icon_book_open_32_bits};
    }
  }
  if (inkdesk::ota::offerAvailable()) {
    snprintf(buf, sizeof(buf), tr(STR_IK_OTA_SUB_NEW_FMT), inkdesk::ota::shortVersion(inkdesk::FW_VERSION).c_str(),
             inkdesk::ota::shortVersion(INKDESK.fwOffer.version).c_str());
  } else {
    snprintf(buf, sizeof(buf), tr(STR_IK_OTA_SUB_LATEST_FMT), inkdesk::ota::shortVersion(inkdesk::FW_VERSION).c_str());
  }
  items_[ROW_UPDATE] = {tr(STR_IK_OTA_TITLE), buf, "", icon_download_32_bits};
  if (INKDESK.get("dash.enabled", "1") == "0") {
    items_[ROW_DASHBOARD] = {tr(STR_ID_DASHBOARD), tr(STR_ID_STATE_OFF), "", BlocksIcon};
  } else {
    snprintf(buf, sizeof(buf), tr(STR_ID_DASH_SUB_ON_FMT), INKDESK.getInt("dash.interval_min", 30));
    items_[ROW_DASHBOARD] = {tr(STR_ID_DASHBOARD), buf, "", BlocksIcon};
  }
  {
    int faces = 0, widgets = 0, themes = 0;
    for (const auto& p : inkdesk::pkgs::list()) (p.type == "face" ? faces : p.type == "widget" ? widgets : themes)++;
    if (faces + widgets + themes > 0) {
      snprintf(buf, sizeof(buf), tr(STR_IK_PKGS_SUB_FMT), faces, widgets, themes);
      items_[ROW_PKGS] = {tr(STR_IK_PKGS), buf, "", BlocksIcon};
    } else {
      items_[ROW_PKGS] = {tr(STR_IK_PKGS), tr(STR_IK_PKGS_NONE), "", BlocksIcon};
    }
  }
  if (INKDESK.lastSyncMs > 0) {
    char clock[8];
    INKDESK.formatClock(INKDESK.lastSyncMs, clock, sizeof(clock));
    snprintf(buf, sizeof(buf), tr(STR_ID_SYNC_LAST_FMT), clock);
    items_[ROW_SYNC] = {tr(STR_ID_SYNC), buf, "", WifiIcon};
  } else {
    items_[ROW_SYNC] = {tr(STR_ID_SYNC), tr(STR_ID_SYNC_NEVER), "", WifiIcon};
  }
  if (INKDESK.isLinked()) {
    snprintf(buf, sizeof(buf), tr(STR_ID_LINKED_TO_FMT),
             INKDESK.user.empty() ? INKDESK.deviceName.c_str() : INKDESK.user.c_str());
    items_[ROW_LINK] = {tr(STR_ID_LINK), buf, "", LibraryIcon};
  } else {
    items_[ROW_LINK] = {tr(STR_ID_LINK), tr(STR_ID_NOT_LINKED), "", LibraryIcon};
  }
  items_[ROW_FILES] = {tr(STR_BROWSE_FILES), tr(STR_IK_FILES_SUB), "", FolderIcon};
  items_[ROW_TRANSFER] = {tr(STR_FILE_TRANSFER), tr(STR_IK_TRANSFER_SUB), "", TransferIcon};
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
      startActivityForResult(std::make_unique<InkStatsActivity>(renderer, mappedInput), onBack);
      break;
    case ROW_UPDATE:
      startActivityForResult(std::make_unique<InkOtaActivity>(renderer, mappedInput, false), onBack);
      break;
    case ROW_DASHBOARD:
      startActivityForResult(std::make_unique<DashboardSettingsActivity>(renderer, mappedInput), onBack);
      break;
    case ROW_PKGS:
      startActivityForResult(std::make_unique<PkgListActivity>(renderer, mappedInput), onBack);
      break;
    case ROW_SYNC:
      if (INKDESK.isLinked()) {
        startActivityForResult(std::make_unique<InkDeskSyncActivity>(renderer, mappedInput), onBack);
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
