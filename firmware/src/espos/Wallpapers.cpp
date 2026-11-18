#include "Wallpapers.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <Logging.h>
#include <esp_random.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "EspOSStore.h"
#include "espos/EspOSUi.h"
#include "fontIds.h"

namespace espos::wallpapers {
namespace {

int32_t idFromName(const char* name) {
  // "<digits>.bmp"
  const size_t n = strlen(name);
  if (n < 5 || strcmp(name + n - 4, ".bmp") != 0) return 0;
  int32_t id = 0;
  for (size_t i = 0; i + 4 < n; i++) {
    if (name[i] < '0' || name[i] > '9') return 0;
    id = id * 10 + (name[i] - '0');
  }
  return id;
}

// Time and battery in a black band along the bottom edge.
void drawStrip(const GfxRenderer& r) {
  constexpr int H = 30;
  const int w = r.getScreenWidth();
  const int y = r.getScreenHeight() - H;
  r.fillRect(0, y, w, H, true);
  const int font = UI_10_FONT_ID;
  const int ty = y + (H - r.getLineHeight(font)) / 2 + 1;
  struct tm tm{};
  char left[64] = "";
  if (espos::ui::localTime(tm)) {
    snprintf(left, sizeof(left), "%02d:%02d · %s %02d/%02d", tm.tm_hour, tm.tm_min, espos::ui::weekdayCaps(tm.tm_wday),
             tm.tm_mday, tm.tm_mon + 1);
  }
  if (*left) r.drawText(font, 14, ty, left, false, EpdFontFamily::BOLD);
  char pct[8];
  snprintf(pct, sizeof(pct), "%d%%", static_cast<int>(powerManager.getBatteryPercentage()));
  r.drawText(font, w - 14 - r.getTextWidth(font, pct, EpdFontFamily::BOLD), ty, pct, false, EpdFontFamily::BOLD);
}

}  // namespace

std::vector<int32_t> ids() {
  std::vector<int32_t> out;
  HalFile dir = Storage.open(DIR);
  if (!dir || !dir.isDirectory()) return out;
  char name[64];
  for (HalFile e = dir.openNextFile(); e; e = dir.openNextFile()) {
    const size_t len = e.getName(name, sizeof(name));
    const bool isDir = e.isDirectory();
    e.close();
    if (len == 0 || len >= sizeof(name) || isDir) continue;
    if (const int32_t id = idFromName(name)) out.push_back(id);
  }
  dir.close();
  std::sort(out.begin(), out.end());
  return out;
}

bool any() { return !ids().empty(); }

std::string pathFor(const int32_t id) { return std::string(DIR) + "/" + std::to_string(id) + ".bmp"; }

void removeExcept(const std::vector<int32_t>& keep) {
  for (const int32_t id : ids()) {
    if (std::find(keep.begin(), keep.end(), id) == keep.end()) Storage.remove(pathFor(id).c_str());
  }
}

void advance() {
  const auto list = ids();
  if (list.empty()) return;
  int32_t next = list.front();
  if (ESPOS.get("sleep.order", "seq") == "random" && list.size() > 1) {
    // Any picture but the one on show.
    do {
      next = list[esp_random() % list.size()];
    } while (next == ESPOS.wallId);
  } else {
    const auto it = std::upper_bound(list.begin(), list.end(), ESPOS.wallId);
    next = it == list.end() ? list.front() : *it;
  }
  ESPOS.wallId = next;
  ESPOS.wallChangedMs = EspOSStore::nowMs();
  ESPOS.saveToFile();
}

int rotateMinutes() {
  const std::string v = ESPOS.get("sleep.rotate", "sleep");
  if (v == "sleep") return 0;
  const int m = atoi(v.c_str());
  return m >= 5 ? m : 0;
}

int64_t secondsToRotate() {
  const int m = rotateMinutes();
  if (m == 0) return 0;
  const int64_t now = EspOSStore::nowMs();
  if (now <= 0 || ESPOS.wallChangedMs <= 0) return static_cast<int64_t>(m) * 60;
  const int64_t due = ESPOS.wallChangedMs + static_cast<int64_t>(m) * 60000;
  return due > now ? (due - now + 999) / 1000 : 0;
}

bool draw(GfxRenderer& r, const bool strip) {
  const auto list = ids();
  if (list.empty()) return false;
  if (std::find(list.begin(), list.end(), ESPOS.wallId) == list.end()) ESPOS.wallId = list.front();
  HalFile file;
  if (!Storage.openFileForRead("WALL", pathFor(ESPOS.wallId), file)) return false;
  Bitmap bmp(file);
  if (bmp.parseHeaders() != BmpReaderError::Ok) {
    LOG_ERR("WALL", "Picture %d unreadable", static_cast<int>(ESPOS.wallId));
    return false;
  }
  const bool landscape = bmp.getWidth() > bmp.getHeight();
  r.setOrientation(landscape ? GfxRenderer::Orientation::LandscapeCounterClockwise
                             : GfxRenderer::Orientation::Portrait);
  r.clearScreen();
  if (!r.drawBitmap(bmp, 0, 0, r.getScreenWidth(), r.getScreenHeight())) return false;
  if (strip) drawStrip(r);
  return true;
}

}  // namespace espos::wallpapers
