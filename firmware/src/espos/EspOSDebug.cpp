#include "EspOSDebug.h"

#include <GfxRenderer.h>
#include <LibraryBuilder.h>
#include <Logging.h>

#include <cstring>
#include <memory>

#include "activities/ActivityManager.h"
#include "activities/RenderLock.h"
#include "activities/apps/AppsHubActivity.h"
#include "activities/apps/DashboardSettingsActivity.h"
#include "activities/apps/EspOSOtaActivity.h"
#include "activities/apps/EspOSSetupActivity.h"
#include "activities/apps/EspOSStatsActivity.h"
#include "activities/apps/EspOSSyncActivity.h"
#include "activities/apps/EspOSWelcomeActivity.h"
#include "activities/apps/EspOSWifiPromptActivity.h"
#include "activities/apps/PairActivity.h"
#include "espos/DashboardView.h"
#include "espos/EspOSStore.h"

namespace espos {

void debugOpen(const char* s, GfxRenderer& r, MappedInputManager& in) {
  LOG_INF("ESPOS", "debug open %s", s);
  if (!strcmp(s, "home")) {
    activityManager.goHome();
  } else if (!strcmp(s, "setup")) {
    activityManager.replaceActivity(std::make_unique<EspOSSetupActivity>(r, in));
  } else if (!strcmp(s, "apps")) {
    activityManager.replaceActivity(std::make_unique<AppsHubActivity>(r, in));
  } else if (!strcmp(s, "sync")) {
    activityManager.replaceActivity(std::make_unique<EspOSSyncActivity>(r, in));
  } else if (!strcmp(s, "pair")) {
    activityManager.replaceActivity(std::make_unique<PairActivity>(r, in));
  } else if (!strcmp(s, "welcome")) {
    activityManager.replaceActivity(std::make_unique<EspOSWelcomeActivity>(r, in));
  } else if (!strcmp(s, "dashset")) {
    activityManager.replaceActivity(std::make_unique<DashboardSettingsActivity>(r, in));
  } else if (!strcmp(s, "stats")) {
    activityManager.replaceActivity(std::make_unique<EspOSStatsActivity>(r, in));
  } else if (!strcmp(s, "ota")) {
    activityManager.replaceActivity(std::make_unique<EspOSOtaActivity>(r, in, false));
  } else if (!strcmp(s, "libdirty")) {
    library::markLibraryIndexDirty();
  } else if (!strncmp(s, "read:", 5)) {
    activityManager.goToReader(s + 5);
  } else if (!strcmp(s, "wifiprompt")) {
    activityManager.replaceActivity(std::make_unique<EspOSWifiPromptActivity>(r, in));
  } else if (!strcmp(s, "sleep")) {
    RenderLock lock;
    dashboard::WakePlan plan = dashboard::planWake(false);
    plan.clock = EspOSStore::clockValid();
    if (!(dashboard::sleepScreenEnabled() && dashboard::draw(r, plan))) dashboard::drawPlaceholder(r);
    r.displayBuffer(HalDisplay::HALF_REFRESH);
    r.setOrientation(GfxRenderer::Orientation::Portrait);
  }
}

}  // namespace espos
