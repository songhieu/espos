#pragma GCC optimize("Os")  // a cold screen: built small (the firmware must fit 3.75 MB)
// Calendar: the month with lunar days and holidays, a tapped day's events, and the week ahead.
#include <Arduino.h>

#include <vector>

#include "apps.h"
#include "appopts.h"
#include "art.h"
#include "data.h"
#include "desk.h"
#include "fonts.h"
#include "keyboard.h"
#include "os.h"
#include "uikit.h"

namespace app_calendar {
namespace {

using namespace kit;

// i18n: the shortest weekday names, Sunday first, heading the columns of month grids (one or two letters)
const char* const WD_MIN_EN[7] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};
const i18n::List WD_MIN = {"@weekdays_min", WD_MIN_EN, 7};

struct State {
  lv_obj_t *scr, *grid, *dayBox, *title;
  int m, y;           // month shown
  data::Day picked;
} S;

int daysIn(int m, int y) {
  static const int D[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  return m == 2 && ((y % 4 == 0 && y % 100) || y % 400 == 0) ? 29 : D[m - 1];
}

void fillDay();

void fillGrid() {
  lv_obj_clean(S.grid);
  fitText(S.title, i18n::date(N_("{MM} {y}"), 1, S.m, S.y).c_str(), os::W() - 150, 30, 22, 16, true, 0);  // the arrows' room
  const data::Day today = data::today();
  data::Day first = data::addDays({1, S.m, S.y, 0}, 0);
  const int lead = (first.wday + 6) % 7;
  const int n = daysIn(S.m, S.y);
  const bool land = os::landscape();  // the month on the left, 6 rows of 33 always fit
  lv_obj_update_layout(S.grid);
  const int cw = lv_obj_get_width(S.grid) / 7;
  for (int i = 0; i < 7; i++) {  // Monday first
    lv_obj_t* l = label(S.grid, WD_MIN[(i + 1) % 7], 13, true, i == 6 ? col::RED : pal().faint);
    lv_obj_set_width(l, cw);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(l, i * cw, 0);
  }
  const int ch = land ? 33 : 40;
  for (int d = 1; d <= n; d++) {
    const int idx = lead + d - 1;
    const int x = (idx % 7) * cw, y = 22 + (idx / 7) * ch;
    const bool isToday = d == today.d && S.m == today.m && S.y == today.y;
    const bool isPicked = d == S.picked.d && S.m == S.picked.m && S.y == S.picked.y;
    lv_obj_t* c = clear(S.grid);
    lv_obj_set_size(c, cw, ch);
    lv_obj_set_pos(c, x, y);
    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
    if (isToday || isPicked) {
      lv_obj_t* dot = box(c, isToday ? col::RED : pal().chip, land ? 13 : 15);
      lv_obj_set_size(dot, land ? 26 : 30, land ? 26 : 30);
      lv_obj_align(dot, LV_ALIGN_TOP_MID, 0, -3);
    }
    const data::Day dd = {d, S.m, S.y, 0};
    const data::Lunar ln = data::lunar(dd);
    const bool hol = data::holiday(dd, ln).length() > 0;
    char b[4];
    snprintf(b, sizeof b, "%d", d);
    lv_obj_t* l = label(c, b, land ? 15 : 16, isToday, isToday ? 0xffffff : (idx % 7 == 6 || hol) ? col::RED : pal().fg);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, land ? -2 : 0);
    char lb[8];
    if (ln.d == 1) snprintf(lb, sizeof lb, "%d/%d", ln.d, ln.m);
    else snprintf(lb, sizeof lb, "%d", ln.d);
    lv_obj_t* s = label(c, lb, 12, ln.d == 1 || ln.d == 15, ln.d == 1 || ln.d == 15 ? col::RED : pal().faint);
    lv_obj_align(s, LV_ALIGN_TOP_MID, 0, land ? 16 : 22);
    if (!data::eventsOn(dd).empty()) {
      lv_obj_t* e = box(c, col::IOS_BLUE, 2);
      lv_obj_set_size(e, 4, 4);
      lv_obj_align(e, LV_ALIGN_TOP_RIGHT, -6, 2);
    }
    lv_obj_add_event_cb(c, [](lv_event_t* e) {
      S.picked = data::addDays({(int)(intptr_t)lv_event_get_user_data(e), S.m, S.y, 0}, 0);
      lv_async_call([](void*) {
        fillGrid();
        fillDay();
      }, nullptr);
    }, LV_EVENT_CLICKED, (void*)(intptr_t)d);
  }
  lv_obj_set_height(S.grid, 22 + ((lead + n + 6) / 7) * ch);
}

void eventRow(lv_obj_t* parent, const data::Event& e, uint32_t bar, int w) {
  lv_obj_t* r = cell(parent, 10);
  lv_obj_t* b = box(r, bar, 2);
  lv_obj_set_size(b, 4, 34);
  lv_obj_t* t = label(r, e.time.length() ? e.time.c_str() : N_("All day"), 13, true, pal().faint);
  lv_obj_set_pos(t, 12, 0);
  lv_obj_t* n = label(r, "", 15, false, pal().fg);
  fitText(n, e.title.c_str(), w - 20 - 12, 40, 15, 12);
  lv_obj_set_pos(n, 12, 17);
}

void fillDay() {
  lv_obj_clean(S.dayBox);
  lv_obj_update_layout(S.dayBox);
  const int w = lv_obj_get_content_width(S.dayBox);  // the whole line, or the right pane in landscape
  const data::Day d = S.picked;
  const data::Lunar l = data::lunar(d);
  const String head = upperVi(i18n::date(N_("{W}, {M} {d}"), d.d, d.m, d.y, d.wday) + " · " +
                              i18n::fmt(N_("{0} (lunar)"), String(l.d) + "/" + l.m));
  lv_obj_t* h = label(S.dayBox, "", 13, true, col::RED);
  fitText(h, head.c_str(), w - 8, 34, 13, 12, true);
  lv_obj_set_style_pad_left(h, 4, 0);
  // i18n: the day's, month's and year's stems and branches (vi "Ngày {0}, tháng {1}, năm {2}")
  String sub = i18n::fmt(N_("{0} day, {1} month, {2} year"), data::canChiDay(d), data::canChiMonth(l), data::canChiYear(l.y));
  const String hol = data::holiday(d, l);
  if (hol.length()) sub = hol + " · " + sub;
  hint(S.dayBox, sub.c_str());
  const auto evs = data::eventsOn(d);
  if (evs.empty()) {
    lv_obj_t* r = cell(S.dayBox, 12);
    label(r, N_("No events"), 15, false, pal().faint);
  }
  int i = 0;
  for (const auto& e : evs) eventRow(S.dayBox, e, i++ % 2 ? col::GREEN : col::IOS_BLUE, w);
  // The week ahead.
  const data::Day t = data::today();
  bool any = false;
  for (int k = 1; k <= 6; k++) {
    const data::Day n = data::addDays(t, k);
    const auto ev = data::eventsOn(n);
    if (ev.empty()) continue;
    if (!any) caption(S.dayBox, N_("This week")), any = true;
    lv_obj_t* hh = label(S.dayBox, upperVi(data::dayLabel(n)).c_str(), 12, true, pal().faint);
    lv_obj_set_style_pad_left(hh, 4, 0);
    for (const auto& e : ev) eventRow(S.dayBox, e, col::ORANGE, w);
  }
  const int n = linkCount();
  // i18n: no linked calendar yet (vi "chưa có")
  listRow(S.dayBox, N_("Linked calendars"), n ? String(n).c_str() : N_("none"), [](lv_event_t*) { openLinks(); });
  if (!n) hint(S.dayBox, N_("Link a Google, iCloud or Outlook calendar to see the next 7 days here."));
}

void shift(int months) {
  S.m += months;
  while (S.m > 12) S.m -= 12, S.y++;
  while (S.m < 1) S.m += 12, S.y--;
  lv_async_call([](void*) { fillGrid(); }, nullptr);
}

// ---------------------------------------------------------------- linked calendars (dash.ics_url)
// One link a line, as the web's Calendar box keeps them; "#" in front keeps a calendar but leaves it out (the
// server only fetches tokens that start with https:// or webcal://). At most 5 shown, as the server reads.
constexpr int MAX_SHOWN = 5, MAX_LINKS = 10;

std::vector<String> links() {
  std::vector<String> out;
  const String v = desk::setting("dash.ics_url", "");
  int i = 0;
  while (i < (int)v.length()) {
    while (i < (int)v.length() && isspace((unsigned char)v[i])) i++;
    int j = i;
    while (j < (int)v.length() && !isspace((unsigned char)v[j])) j++;
    if (j > i) out.push_back(v.substring(i, j));
    i = j;
  }
  return out;
}

void saveLinks(const std::vector<String>& l) {
  String v;
  for (const String& s : l) v += (v.length() ? "\n" : "") + s;
  desk::setSetting("dash.ics_url", v);
  desk::syncNow();
}

bool hidden(const String& l) { return l.startsWith("#"); }
int shownCount(const std::vector<String>& l) {
  int n = 0;
  for (const String& s : l) n += !hidden(s);
  return n;
}

// "Google" + "ban@gmail.com" from the link (the account in Google's path, else the host).
void whose(const String& link, String& name, String& who) {
  String u = hidden(link) ? link.substring(1) : link;
  const int s = u.indexOf("://");
  if (s >= 0) u = u.substring(s + 3);
  const int slash = u.indexOf('/');
  const String host = slash >= 0 ? u.substring(0, slash) : u;
  who = host;
  if (host.indexOf("google.") >= 0) {
    name = "Google";
    const int a = u.indexOf("/ical/");
    if (a >= 0) {
      String acc = u.substring(a + 6);
      if (acc.indexOf('/') > 0) acc = acc.substring(0, acc.indexOf('/'));
      acc.replace("%40", "@");
      if (acc.length()) who = acc;
    }
  } else if (host.indexOf("icloud.") >= 0) {
    name = "iCloud";
  } else if (host.indexOf("outlook.") >= 0 || host.indexOf("office365.") >= 0 || host.indexOf("live.com") >= 0) {
    name = "Outlook";
  } else {
    name = T_("Calendar");
  }
}

const uint32_t DOTS[] = {col::IOS_BLUE, col::GREEN, col::ORANGE, col::RED, 0xaf52de};

lv_obj_t *linksScr = nullptr, *linksBody = nullptr;
void fillLinks(lv_obj_t* body);
void openLink(int i);
void openAdd();

void refreshLinks() {
  if (!linksBody) return;
  lv_obj_clean(linksBody);
  fillLinks(linksBody);
}

void toggleShown(int i) {
  auto l = links();
  if (i >= (int)l.size()) return;
  if (hidden(l[i])) {
    if (shownCount(l) >= MAX_SHOWN) return os::toast(N_("At most 5 calendars can be shown: turn one off first"));
    l[i] = l[i].substring(1);
  } else {
    l[i] = "#" + l[i];
  }
  saveLinks(l);
}

void fillLinks(lv_obj_t* body) {
  const auto l = links();
  for (int i = 0; i < (int)l.size(); i++) {
    String name, who;
    whose(l[i], name, who);
    lv_obj_t* r = box(body, pal().cell, 12);
    lv_obj_set_size(r, lv_pct(100), 56);
    lv_obj_set_style_pad_hor(r, 14, 0);
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(r, C(pal().press), LV_STATE_PRESSED);
    lv_obj_t* d = box(r, DOTS[i % 5], 5);
    lv_obj_set_size(d, 10, 10);
    lv_obj_align(d, LV_ALIGN_LEFT_MID, 0, 0);
    // Source and account only: the link itself is a secret and a desk screen is seen by anyone passing by.
    lv_obj_t* a = label(r, "", 15, false, hidden(l[i]) ? pal().faint : pal().fg);
    fitText(a, (name + " · " + who).c_str(), os::W() - 24 - 28 - 18 - 64, 21, 15, 12);
    lv_obj_set_pos(a, 18, 5);
    lv_obj_set_pos(label(r, N_("Private link"), 12, false, pal().faint), 18, 29);
    lv_obj_add_event_cb(r, [](lv_event_t* e) { openLink((int)(intptr_t)lv_event_get_user_data(e)); }, LV_EVENT_CLICKED,
                        (void*)(intptr_t)i);
    lv_obj_t* t = toggle(r, !hidden(l[i]), [](lv_event_t* e) {
      toggleShown((int)(intptr_t)lv_event_get_user_data(e));
      lv_async_call([](void*) { refreshLinks(); }, nullptr);
    }, (void*)(intptr_t)i);
    lv_obj_align(t, LV_ALIGN_RIGHT_MID, 0, 0);
  }
  if (l.empty()) hint(body, N_("No calendars linked yet. Add a Google, iCloud or Outlook calendar link to see the next 7 days here."));
  if ((int)l.size() < MAX_LINKS) {
    lv_obj_t* add = listRow(body, "", "", [](lv_event_t*) { openAdd(); }, nullptr, false);
    lv_obj_t* t = label(add, N_("+ Add calendar"), 15, true, col::IOS_BLUE);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 0, 0);
  }
  if (!l.empty())
    hint(body, N_("Switched off: the calendar is kept but not fetched. Tap a calendar to see or remove it. At most 5 are shown."));
}

// One calendar: source and account, the link only after "Show link" (gone again when the page closes), Show on
// device, Remove this calendar (a second tap confirms).
int linkAt = -1;
bool armed = false;
lv_obj_t* linkText = nullptr;  // the cell's second line: "Private link", or the whole link once asked for
void openLink(int i) {
  const auto l = links();
  if (i < 0 || i >= (int)l.size()) return;
  linkAt = i, armed = false;
  String name, who;
  whose(l[i], name, who);
  lv_obj_t* body;
  lv_obj_t* s = os::screen(name.c_str(), &body);
  lv_obj_t* c = cell(body, 12);
  lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(c, 4, 0);
  lv_obj_t* a = label(c, "", 15, false, pal().fg);
  fitText(a, (name + " · " + who).c_str(), os::W() - 24 - 24, 42, 15, 12);
  linkText = label(c, N_("Private link"), 13, false, pal().faint);
  lv_obj_add_event_cb(s, [](lv_event_t* e) {
    if (lv_event_get_current_target(e) == lv_event_get_target(e)) linkText = nullptr;
  }, LV_EVENT_DELETE, nullptr);
  listRow(body, N_("Show link"), "", [](lv_event_t* e) {
    lv_obj_t* row = (lv_obj_t*)lv_event_get_current_target(e);
    const auto l = links();
    if (!linkText || linkAt < 0 || linkAt >= (int)l.size()) return;
    const bool show = !strcmp(lv_label_get_text(lv_obj_get_child(row, 0)), T_("Show link"));
    if (show) wrapText(linkText, hidden(l[linkAt]) ? l[linkAt].substring(1).c_str() : l[linkAt].c_str(), os::W() - 24 - 24, 13, false, 2);
    else setText(linkText, N_("Private link"));
    setText(lv_obj_get_child(row, 0), show ? N_("Hide link") : N_("Show link"));
  }, nullptr, false);
  lv_obj_t* r = listRow(body, N_("Show on device"), "", nullptr);
  lv_obj_t* t = toggle(r, !hidden(l[i]), [](lv_event_t*) {
    toggleShown(linkAt);
    refreshLinks();
  });
  lv_obj_align(t, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_t* del = listRow(body, N_("Remove this calendar"), "", [](lv_event_t* e) {
    lv_obj_t* row = (lv_obj_t*)lv_event_get_current_target(e);
    if (!armed) {
      armed = true;
      setText(lv_obj_get_child(row, 0), N_("Tap again to remove"));
      return;
    }
    auto l = links();
    if (linkAt >= 0 && linkAt < (int)l.size()) l.erase(l.begin() + linkAt);
    saveLinks(l);
    refreshLinks();
    os::toast(N_("Calendar removed"));
    os::back();
  }, nullptr, false);
  lv_obj_set_style_text_color(lv_obj_get_child(del, 0), C(col::RED), 0);
  hint(body, N_("Anyone with this link can see your calendar, so it only shows when you tap Show link."));
  os::open(s);
}

lv_obj_t *addTa, *addMsg;
void addError(const char* text) {
  if (!addMsg) return;
  wrapText(addMsg, text, os::W() - 24 - 8, 13, false, 2);
  lv_obj_set_style_text_color(addMsg, C(col::RED), 0);
}
void addDone(lv_obj_t* ta) {
  String v = lv_textarea_get_text(ta);
  v.trim();
  String low = v;
  low.toLowerCase();
  if (low.startsWith("webcal://")) v = "https://" + v.substring(9);
  else if (low.startsWith("https://")) v = "https://" + v.substring(8);
  const bool ok = low.startsWith("https://") || low.startsWith("webcal://");
  if (!ok || v.length() < 14 || v.indexOf(' ') >= 0 || v.indexOf('\n') >= 0)
    return addError(N_("The link must start with https:// or webcal:// and have no spaces."));
  auto l = links();
  for (const String& s : l)
    if (s == v || s == "#" + v) return addError(N_("This calendar is already here."));
  if (shownCount(l) >= MAX_SHOWN) v = "#" + v;  // kept, hidden until another one is turned off
  l.push_back(v);
  saveLinks(l);
  kb::close();
  refreshLinks();
  os::toast(v.startsWith("#") ? N_("Added, hidden: 5 calendars are shown already") : N_("Calendar added"));
  os::back();
}

void openAdd() {
  lv_obj_t* body;
  lv_obj_t* s = os::screen(N_("Add calendar"), &body, os::NOSCROLL);
  addTa = lv_textarea_create(body);
  lv_obj_set_width(addTa, lv_pct(100));
  lv_obj_set_height(addTa, 76);
  lv_textarea_set_max_length(addTa, 600);
  lv_textarea_set_placeholder_text(addTa, T_("https:// or webcal://"));
  lv_obj_set_style_text_font(addTa, fonts::get(14), 0);
  lv_obj_set_style_text_color(addTa, C(pal().fg), 0);
  lv_obj_set_style_text_color(addTa, C(pal().faint), LV_PART_TEXTAREA_PLACEHOLDER);
  lv_obj_set_style_bg_color(addTa, C(pal().cell), 0);
  lv_obj_set_style_border_color(addTa, C(col::IOS_BLUE), 0);
  lv_obj_set_style_border_width(addTa, 2, 0);
  lv_obj_set_style_radius(addTa, 12, 0);
  lv_obj_set_style_pad_all(addTa, 10, 0);
  lv_obj_add_state(addTa, LV_STATE_FOCUSED);
  // i18n: where each service shows a calendar's private link: use the names their own menus have in your language
  addMsg = hint(body,
                N_("Google: Settings, Integrate calendar, \"Secret address in iCal format\". iCloud: Public Calendar. "
                   "Outlook: Publish a calendar, ICS link."));
  lv_obj_add_event_cb(s, [](lv_event_t* e) {
    if (lv_event_get_current_target(e) == lv_event_get_target(e)) addTa = addMsg = nullptr;
  }, LV_EVENT_DELETE, nullptr);
  os::open(s);
  kb::open(addTa, kb::Mode::Plain, addDone, T_("Add"));
}

}  // namespace

int linkCount() { return links().size(); }

void openLinks() {
  linksScr = os::screen(N_("Linked calendars"), &linksBody);
  lv_obj_add_event_cb(linksScr, [](lv_event_t* e) {
    if (lv_event_get_current_target(e) == linksScr) linksScr = linksBody = nullptr;
  }, LV_EVENT_DELETE, nullptr);
  fillLinks(linksBody);
  os::open(linksScr);
}

void open(const char*) {
  S = {};
  const data::Day t = data::today();
  S.m = t.m, S.y = t.y, S.picked = t;
  lv_obj_t* body;
  S.scr = os::screen(N_("Calendar"), &body, os::ROOT);
  S.title = os::navTitle(S.scr);
  lv_obj_set_style_text_font(S.title, fonts::get(22, true), 0);
  lv_obj_t* nav = (lv_obj_t*)lv_obj_get_user_data(S.scr);
  for (int dir : {-1, 1}) {
    lv_obj_t* b = clear(nav);
    lv_obj_set_size(b, 40, 40);
    lv_obj_align(b, LV_ALIGN_RIGHT_MID, dir < 0 ? -48 : -6, 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t* g = glyph(b, dir < 0 ? &gl_back_22 : &gl_chev_22, col::RED);
    lv_obj_center(g);
    lv_obj_add_event_cb(b, [](lv_event_t* e) { shift((int)(intptr_t)lv_event_get_user_data(e)); }, LV_EVENT_CLICKED,
                        (void*)(intptr_t)dir);
  }
  // Landscape: the month on the left, the picked day and the week on the right (its own scroll).
  const bool land = os::landscape();
  if (land) {
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(body, 10, 0);
  }
  S.grid = clear(body);
  lv_obj_set_width(S.grid, land ? 266 : lv_pct(100));
  S.dayBox = stack(body, false, 8);
  if (land) {
    lv_obj_set_size(S.dayBox, os::W() - 24 - 266 - 10, os::H() - os::SB_H - os::NAV_H - 8);
    lv_obj_add_flag(S.dayBox, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_scroll_dir(S.dayBox, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(S.dayBox, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_bottom(S.dayBox, 20, 0);
  }
  lv_obj_add_event_cb(S.scr, [](lv_event_t* e) {
    if (lv_event_get_current_target(e) == S.scr) S.scr = S.grid = S.dayBox = S.title = nullptr;
  }, LV_EVENT_DELETE, nullptr);
  fillGrid();
  fillDay();
  os::open(S.scr);
}

}  // namespace app_calendar
