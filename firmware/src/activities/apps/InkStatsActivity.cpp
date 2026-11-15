#include "InkStatsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "fontIds.h"
#include "inkdesk/InkDeskStore.h"
#include "inkdesk/InkFonts.h"
#include "inkdesk/InkUi.h"

namespace iu = inkdesk::ui;

namespace {
constexpr int PAD = 22;
constexpr int GOALS[] = {10, 15, 20, 30, 45, 60};
// INK_44 is digits only: its digits start this far below the text origin and are this tall.
constexpr int DIGIT_TOP = 98 - 66;
constexpr int DIGIT_H = 66;

void rule(const GfxRenderer& r, const int y) { r.fillRect(PAD, y, r.getScreenWidth() - PAD * 2, 2, true); }

void lineLR(const GfxRenderer& r, const int y, const char* left, const char* right) {
  const int w = r.getScreenWidth() - PAD * 2;
  const int rw = r.getTextWidth(iu::fontBody(), right, EpdFontFamily::BOLD);
  iu::fitText(r, iu::fontBody(), PAD, y, left, w - rw - 16);
  r.drawText(iu::fontBody(), PAD + w - rw, y, right, true, EpdFontFamily::BOLD);
}
}  // namespace

void InkStatsActivity::onEnter() {
  Activity::onEnter();
  s_ = inkdesk::stats::summary();
  requestUpdate();
}

void InkStatsActivity::loop() {
  using B = MappedInputManager::Button;
  if (mappedInput.wasReleased(B::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasReleased(B::Confirm)) {
    // Next goal in the list (wraps), saved locally and sent with the next sync.
    int next = GOALS[0];
    for (const int g : GOALS) {
      if (g > s_.goalMin) {
        next = g;
        break;
      }
    }
    INKDESK.setLocal("read.goal_min", std::to_string(next));
    INKDESK.saveToFile();
    s_ = inkdesk::stats::summary();
    requestUpdate();
  }
}

void InkStatsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  iu::header(renderer, tr(STR_IK_STATS_TITLE));
  const int w = renderer.getScreenWidth() - PAD * 2;
  int y = iu::HEADER_H + 18;
  char buf[96];
  char goalHint[32];
  snprintf(goalHint, sizeof(goalHint), tr(STR_IK_STATS_GOAL_HINT_FMT), s_.goalMin);

  if (!s_.dated) {
    iu::wrap(renderer, iu::fontBody(), PAD, y, w, tr(STR_IK_STATS_NO_CLOCK), 4);
    iu::hints(renderer, mappedInput, tr(STR_BACK), goalHint, "", "");
    renderer.displayBuffer();
    return;
  }

  // Today: big minutes against the goal.
  iu::kicker(renderer, PAD, y, tr(STR_IK_STATS_TODAY_CAPS));
  y += renderer.getLineHeight(iu::fontKicker()) + 4;
  const int todayMin = static_cast<int>(s_.todaySec / 60);
  snprintf(buf, sizeof(buf), "%d", todayMin);
  renderer.drawText(INK_44_FONT_ID, PAD - 2, y - DIGIT_TOP, buf, true, EpdFontFamily::BOLD);
  const int numW = renderer.getTextWidth(INK_44_FONT_ID, buf, EpdFontFamily::BOLD);
  snprintf(buf, sizeof(buf), tr(STR_IK_STATS_OF_GOAL_FMT), s_.goalMin);
  const int unitFont = NOTOSANS_16_FONT_ID;
  renderer.drawText(unitFont, PAD + numW + 10, y + DIGIT_H - renderer.getFontAscenderSize(unitFont), buf, true,
                    EpdFontFamily::BOLD);
  y += DIGIT_H + 14;
  const int pct = std::min(100, s_.goalMin > 0 ? todayMin * 100 / s_.goalMin : 0);
  iu::progressBar(renderer, PAD, y, w, 16, pct);
  y += 16 + 8;
  snprintf(buf, sizeof(buf), tr(STR_IK_STATS_PAGES_FMT), static_cast<int>(s_.todayPages));
  renderer.drawText(iu::fontSmall(), PAD, y, buf, true, EpdFontFamily::BOLD);
  if (s_.streak > 0) {
    snprintf(buf, sizeof(buf), tr(STR_IK_STATS_STREAK_FMT), s_.streak);
    const int sw = renderer.getTextWidth(iu::fontSmall(), buf, EpdFontFamily::BOLD);
    renderer.drawText(iu::fontSmall(), PAD + w - sw, y, buf, true, EpdFontFamily::BOLD);
  }
  y += renderer.getLineHeight(iu::fontSmall()) + 12;
  rule(renderer, y);
  y += 14;

  // Last 7 days: filled bars reached the goal, outlined ones did not; dashed line = goal.
  iu::kicker(renderer, PAD, y, tr(STR_IK_STATS_WEEK_CAPS));
  y += renderer.getLineHeight(iu::fontKicker()) + 8;
  const int barsH = 110;
  const uint32_t goalSec = static_cast<uint32_t>(s_.goalMin) * 60;
  uint32_t top = goalSec;
  for (const uint32_t v : s_.last7) top = std::max(top, v);
  top = top + top / 8;
  const int gap = 10;
  const int bw = (w - gap * 6) / 7;
  const int base = y + barsH;
  const int goalY = base - static_cast<int>(static_cast<uint64_t>(goalSec) * barsH / top);
  for (int x = PAD; x < PAD + w; x += 12) renderer.drawLine(x, goalY, std::min(x + 6, PAD + w), goalY, true);
  // Short weekday names, Sunday first: "CN,T2,…".
  char names[64];
  snprintf(names, sizeof(names), "%s", tr(STR_IK_WDAYS_SHORT));
  const char* wd[7] = {};
  {
    char* save = nullptr;
    int i = 0;
    for (char* t = strtok_r(names, ",", &save); t && i < 7; t = strtok_r(nullptr, ",", &save)) wd[i++] = t;
  }
  for (int i = 0; i < 7; i++) {
    const int bx = PAD + i * (bw + gap);
    const uint32_t v = s_.last7[i];
    const int h = v > 0 ? std::max(3, static_cast<int>(static_cast<uint64_t>(v) * barsH / top)) : 0;
    if (h > 0) {
      if (v >= goalSec) {
        renderer.fillRect(bx, base - h, bw, h, true);
      } else {
        renderer.drawRect(bx, base - h, bw, h, 2, true);
      }
    }
    renderer.fillRect(bx, base, bw, 2, true);
    const int wday = (s_.todayWday - 6 + i + 14) % 7;
    const char* label = wd[wday] ? wd[wday] : "";
    const int lw = renderer.getTextWidth(iu::fontKicker(), label, EpdFontFamily::BOLD);
    renderer.drawText(iu::fontKicker(), bx + (bw - lw) / 2, base + 6, label, true, EpdFontFamily::BOLD);
  }
  y = base + 6 + renderer.getLineHeight(iu::fontKicker()) + 10;
  rule(renderer, y);
  y += 12;

  // Totals.
  auto duration = [&](const uint32_t sec, char* out, const size_t n) {
    const int m = static_cast<int>(sec / 60);
    if (m >= 60) {
      snprintf(out, n, tr(STR_IK_DUR_HM_FMT), m / 60, m % 60);
    } else {
      snprintf(out, n, tr(STR_IK_DUR_M_FMT), m);
    }
  };
  const int lh = renderer.getLineHeight(iu::fontBody()) + 8;
  char right[48];
  duration(s_.weekSec, right, sizeof(right));
  lineLR(renderer, y, tr(STR_IK_STATS_THIS_WEEK), right);
  y += lh;
  struct tm tm {};
  iu::localTime(tm);
  snprintf(buf, sizeof(buf), tr(STR_IK_STATS_MONTH_FMT), tm.tm_mon + 1);
  duration(s_.monthSec, right, sizeof(right));
  lineLR(renderer, y, buf, right);
  y += lh;
  if (!s_.topTitle.empty()) {
    duration(s_.topSec, right, sizeof(right));
    lineLR(renderer, y, s_.topTitle.c_str(), right);
  }

  iu::hints(renderer, mappedInput, tr(STR_BACK), goalHint, "", "");
  renderer.displayBuffer();
}
