#include "InkDeskDebug.h"

#include <GfxRenderer.h>
#include <LibraryBuilder.h>
#include <Logging.h>

#include <cstring>
#include <memory>

#include "activities/RenderLock.h"
#include "activities/ActivityManager.h"
#include "activities/apps/AppsHubActivity.h"
#include "activities/apps/DashboardSettingsActivity.h"
#include "activities/apps/InkDeskSyncActivity.h"
#include "activities/apps/InkDeskWelcomeActivity.h"
#include "activities/apps/InkSetupActivity.h"
#include "activities/apps/PairActivity.h"
#include "activities/apps/InkWifiPromptActivity.h"
#include "activities/apps/InkOtaActivity.h"
#include "activities/apps/InkStatsActivity.h"
#include "inkdesk/DashboardView.h"
#include "inkdesk/InkDeskStore.h"

namespace inkdesk {

void debugOpen(const char* s, GfxRenderer& r, MappedInputManager& in) {
  LOG_INF("INKD", "debug open %s", s);
  if (!strcmp(s, "home")) {
    activityManager.goHome();
  } else if (!strcmp(s, "setup")) {
    activityManager.replaceActivity(std::make_unique<InkSetupActivity>(r, in));
  } else if (!strcmp(s, "apps")) {
    activityManager.replaceActivity(std::make_unique<AppsHubActivity>(r, in));
  } else if (!strcmp(s, "sync")) {
    activityManager.replaceActivity(std::make_unique<InkDeskSyncActivity>(r, in));
  } else if (!strcmp(s, "pair")) {
    activityManager.replaceActivity(std::make_unique<PairActivity>(r, in));
  } else if (!strcmp(s, "welcome")) {
    activityManager.replaceActivity(std::make_unique<InkDeskWelcomeActivity>(r, in));
  } else if (!strcmp(s, "dashset")) {
    activityManager.replaceActivity(std::make_unique<DashboardSettingsActivity>(r, in));
  } else if (!strcmp(s, "stats")) {
    activityManager.replaceActivity(std::make_unique<InkStatsActivity>(r, in));
  } else if (!strcmp(s, "ota")) {
    activityManager.replaceActivity(std::make_unique<InkOtaActivity>(r, in, false));
  } else if (!strcmp(s, "libdirty")) {
    library::markLibraryIndexDirty();
  } else if (!strncmp(s, "read:", 5)) {
    activityManager.goToReader(s + 5);
  } else if (!strcmp(s, "wifiprompt")) {
    activityManager.replaceActivity(std::make_unique<InkWifiPromptActivity>(r, in));
  } else if (!strcmp(s, "sleep")) {
    RenderLock lock;
    dashboard::WakePlan plan = dashboard::planWake(false);
    plan.clock = InkDeskStore::clockValid();
    if (!(dashboard::sleepScreenEnabled() && dashboard::draw(r, plan))) dashboard::drawPlaceholder(r);
    r.displayBuffer(HalDisplay::HALF_REFRESH);
    r.setOrientation(GfxRenderer::Orientation::Portrait);
  }
}

}  // namespace inkdesk
