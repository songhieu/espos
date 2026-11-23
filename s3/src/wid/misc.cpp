#pragma GCC optimize("Os")  // widgets/JS glue built once per change: size over speed (0.5.7 flash diet)
// The small families (docs/widgets.md §3): reminders (open / all), notes, photo (cover / fit / frame), my widgets,
// notifications (notif::latest) and the device's status. web/app/src/os/wid/misc.ts draws the same (its
// notifications are the dashboard's notes, which the device shows as EspOS's).
#include <ArduinoJson.h>

#include <cmath>

#include "../board.h"
#include "../data.h"
#include "../desk.h"
#include "../net.h"
#include "../notif.h"
#include "../photo.h"
#include "../power.h"
#include "kit.h"

namespace wid {

using namespace kit;

// Free sizes (docs/widgets.md §2b): these lay out by their box, so they take the taller spans as they are; notes and
// the photo also a one-row strip.
constexpr uint16_t TALL = SPAN(3, 2) | SPAN(2, 3) | SPAN(3, 3) | SPAN(4, 3) | SPAN(2, 4) | SPAN(3, 4);
constexpr uint16_t ROW = SPAN(2, 1) | SPAN(3, 1) | SPAN(4, 1);
const std::vector<Style> REMIND_STYLES = {{"open", N_("Open tasks"), TALL}, {"all", N_("All tasks"), TALL}};
const std::vector<Style> NOTES_STYLES = {{"note", N_("Note"), TALL | ROW}};
const std::vector<Style> PHOTO_STYLES = {{"cover", N_("Fill"), TALL | ROW}, {"fit", N_("Fit"), TALL | ROW}, {"frame", N_("Frame"), TALL}};
const std::vector<Style> NOTIFY_STYLES = {
    {"latest", N_("Latest"), SPAN(3, 2) | SPAN(3, 3) | SPAN(4, 3) | SPAN(3, 4)}};  // its rows want three wide
const std::vector<Style> SYSTEM_STYLES = {{"status", N_("Battery, WiFi, sync")}};

namespace {

// A 14 x 14 ring (a done one filled green).
lv_obj_t* ring(lv_obj_t* p, int x, int y, uint32_t color, bool filled) {
  lv_obj_t* r = clear(p);
  lv_obj_set_size(r, 14, 14);
  lv_obj_set_style_radius(r, 7, 0);
  lv_obj_set_style_border_width(r, 2, 0);
  lv_obj_set_style_border_color(r, C(color), 0);
  if (filled) lv_obj_set_style_bg_color(r, C(color), 0), lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
  lv_obj_set_pos(r, x, y);
  return r;
}

// A plain rectangle that never takes a tap (the card does).
lv_obj_t* rect(lv_obj_t* p, int x, int y, int w, int h, uint32_t color, int r = 0, lv_opa_t opa = LV_OPA_COVER) {
  lv_obj_t* b = box(p, color, r);
  lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(b, w, h);
  lv_obj_set_pos(b, x, y);
  if (opa != LV_OPA_COVER) lv_obj_set_style_bg_opa(b, opa, 0);
  return b;
}

}  // namespace

// ---------------------------------------------------------------- reminders
// open: the tasks still to do; all: those, then the done ones (a green ring, grey text). A long task wraps to a
// second line and pushes the next rows down (never "…", never overlapping).
void wRemind(lv_obj_t*, const Item& it, Card c) {
  JsonDocument doc;
  data::loadJson("s3.todo", doc);
  JsonArrayConst list = doc.as<JsonArrayConst>();
  const bool all = styleOf(it) == "all";
  int open_ = 0, total = 0;
  for (JsonObjectConst t : list) open_ += !(t["done"] | false), total++;
  cap(c.o, N_("Reminders"), SKIN.own ? 0xff9500 : GREY);
  right(c.o, all ? String(total - open_) + "/" + String(total) : String(open_), -2, 20, true, INK);
  const uint32_t ringC = SKIN.own ? 0xc7cad1 : GREY, doneC = SKIN.own ? 0x9ca3af : GREY;
  int y = 24, shown = 0;
  bool full = false;
  for (int pass = 0; pass < (all ? 2 : 1) && !full; pass++) {
    for (JsonObjectConst t : list) {
      const bool done = t["done"] | false;
      if (done != (pass == 1)) continue;
      if (y + 20 > c.h) {
        full = true;
        break;
      }
      lv_obj_t* r = ring(c.o, 0, y + 2, done ? 0x34c759 : ringC, done);
      lv_obj_t* l = fit(c.o, t["t"] | "", 22, y, c.w - 22, 34, 14, 12, false, done ? doneC : INK);
      const int used = heightOf(l);
      if (y + used > c.h) {  // no room for this one: the count above still says how many there are
        lv_obj_delete(l);
        lv_obj_delete(r);
        full = true;
        break;
      }
      y += max(used, 18) + (it.size == S ? 4 : 6);
      shown++;
    }
  }
  if (!shown)
    fit(c.o, open_ ? "" : all && !total ? N_("No tasks yet. Tap to add one.") : N_("No tasks left. Tap to add one."), 0, 26,
        c.w, c.h - 26, 14, 12, false, GREY);
}

// ---------------------------------------------------------------- notes
void wNotes(lv_obj_t*, const Item& it, Card c) {
  JsonDocument doc;
  data::loadJson("s3.notes", doc);
  JsonObjectConst n;
  for (JsonObjectConst x : doc.as<JsonArrayConst>())
    if (!it.opt.length() || it.opt == (x["id"] | "")) {
      n = x;
      break;
    }
  // Its own look is a yellow note; a skin brings its colours.
  if (SKIN.own) lv_obj_set_style_bg_color(c.o, C(0xfff4b8), 0);
  const uint32_t ink = SKIN.own ? 0x3b2f00 : INK;
  const String title = n["title"] | "", body = n["body"] | "";
  int cw, ch;
  spanOf(it, cw, ch);
  if (ch == 1) {
    // One row: the heading over the note's first line; too long for that, the note alone on two lines.
    const String head = n.isNull() ? String(T_("No notes yet. Tap to write one.")) : title.length() ? title : body;
    if (measure(c.o, head, c.w, c.h - 21, 15, 13, true) >= 0) {
      cap(c.o, N_("Notes"), SKIN.own ? 0xa16207 : GREY);
      fit(c.o, head, 0, 21, c.w, c.h - 21, 15, 13, true, n.isNull() ? GREY : ink);
    } else {
      fit(c.o, head, 0, -1, c.w, c.h + 2, 14, 12, true, n.isNull() ? GREY : ink);
    }
    return;
  }
  cap(c.o, N_("Notes"), SKIN.own ? 0xa16207 : GREY);
  if (n.isNull()) return empty(c, N_("No notes yet. Tap to write one."), SKIN.own ? 0x6b5a16 : GREY, 22);
  fit(c.o, title.length() ? title : body, 0, 22, c.w, 22, 15, 13, true, ink);
  if (title.length()) fit(c.o, body, 0, 46, c.w, c.h - 46, 14, 12, false, SKIN.own ? 0x5b4a10 : GREY, 2);
}

// ---------------------------------------------------------------- photo
namespace {

// The picture scaled around its centre to cover (or fit inside) a w x h box, centred in it.
void place(lv_obj_t* box_, const lv_image_dsc_t* pic, int w, int h, bool cover) {
  const int iw = pic->header.w, ih = pic->header.h;
  const int sx = w * 256 / iw, sy = h * 256 / ih;
  int s = cover ? max((w * 256 + iw - 1) / iw, (h * 256 + ih - 1) / ih) : min(sx, sy);
  lv_obj_t* i = img(box_, pic);
  lv_image_set_scale(i, s);
  lv_obj_center(i);
}

}  // namespace

// cover: the picture fills the card; fit: all of it, on a dark backdrop (or the skin); frame: a print on a mat,
// with the option's caption under it ("N|caption"). The picture is the device's own copy for the card's
// orientation (photo::screen), scaled by LVGL.
void wPhoto(lv_obj_t*, const Item& it, Card c) {
  const int W = c.w + 24, H = c.h + 20;  // the whole card
  const String st = styleOf(it);
  const int bar = it.opt.indexOf('|');
  const String caption = bar >= 0 ? it.opt.substring(bar + 1) : String("");
  const int m = it.size == S ? 8 : 10, bottom = st == "frame" && caption.length() ? (it.size == S ? 26 : 30) : m;
  const int bw = st == "frame" ? W - 2 * m : W, bh = st == "frame" ? H - m - bottom : H;
  const bool land = bw >= bh;
  // Pinned: the saver's slideshow may decode other pictures while this card still shows this one.
  const lv_image_dsc_t* pic = photo::screen(it.opt.toInt(), land ? 480 : 320, land ? 320 : 480, true);
  if (!pic) {
    if (SKIN.own) grad(c.o, 0x5856d6, 0xaf52de);
    cap(c.o, N_("Photos"), SKIN.own ? 0xffffff : GREY);
    return empty(c, N_("Add photos on the EspOS web: Devices, Photos"), SKIN.own ? 0xf3e8ff : GREY, 22);
  }
  lv_obj_set_style_pad_all(c.o, 0, 0);
  if (st == "frame") {
    // The mat: the theme's paper (or the skin), the picture inside it with square corners.
    if (SKIN.own) lv_obj_set_style_bg_color(c.o, C(PAPER), 0);
    lv_obj_t* hold = clear(c.o);
    lv_obj_remove_flag(hold, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(hold, bw, bh);
    lv_obj_set_pos(hold, m, m);
    place(hold, pic, bw, bh, true);
    if (caption.length()) {
      lv_obj_t* l = label(c.o, "", 13, false, INK);
      fitText(l, caption.c_str(), W - 2 * m, bottom - 6, it.size == S ? 13 : 15, 12, false, 2);
      lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_align(l, LV_ALIGN_TOP_MID, 0, H - bottom + (bottom - heightOf(l)) / 2);
    }
    return;
  }
  lv_obj_set_style_clip_corner(c.o, true, 0);
  if (st == "fit" && SKIN.own) lv_obj_set_style_bg_color(c.o, C(0x111318), 0);
  place(c.o, pic, W, H, st != "fit");
}

// ---------------------------------------------------------------- my widgets
// Their own colours (the builder's), or the skin's.
void wMine(lv_obj_t*, const Item& it, Card c) {
  const Mine* m = findMine(it.opt);
  if (!m) {
    cap(c.o, N_("My widget"), GREY);
    return empty(c, N_("This widget was deleted"), GREY, 22);
  }
  const uint32_t fg = SKIN.own ? m->fg : SKIN.fg;
  if (SKIN.own) lv_obj_set_style_bg_color(c.o, C(m->bg), 0);
  if (m->type == "count") {
    if (SKIN.own) grad(c.o, m->bg, m->bg == 0xd7261e ? 0xf97316 : m->bg);
    cap(c.o, m->title.c_str(), SKIN.own ? 0xffe9a8 : GREY);
    int days = 0;
    if (m->date.length() == 10) {
      const data::Day t = data::today();
      tm a = {}, b = {};
      a.tm_mday = t.d, a.tm_mon = t.m - 1, a.tm_year = t.y - 1900, a.tm_hour = 12;
      b.tm_mday = m->date.substring(8, 10).toInt(), b.tm_mon = m->date.substring(5, 7).toInt() - 1;
      b.tm_year = m->date.substring(0, 4).toInt() - 1900, b.tm_hour = 12;
      days = (int)lround(difftime(mktime(&b), mktime(&a)) / 86400.0);
    } else {
      days = data::daysToTet(data::today());
    }
    at(c.o, String(abs(days)), -2, 22, 44, true, fg);
    // i18n: under a big number of days (a countdown widget)
    at(c.o, days > 0 ? i18n::plural(days, NP_("day to go|days to go"))
            : days == 0 ? String(T_("is today")) : i18n::plural(-days, NP_("day ago|days ago")),
       0, 72, 14, false, fg);
    if (it.size != S && m->date.length() == 10)
      right(c.o, m->date.substring(8, 10) + "/" + m->date.substring(5, 7) + "/" + m->date.substring(0, 4), 26, 14, true, fg);
    return;
  }
  if (m->type == "quote") {
    // One a day, in turn.
    std::vector<String> qs;
    int s = 0;
    const String& t = m->text;
    while (s <= (int)t.length()) {
      int e = t.indexOf('\n', s);
      if (e < 0) e = t.length();
      String q = t.substring(s, e);
      q.trim();
      if (q.length()) qs.push_back(q);
      s = e + 1;
    }
    at(c.o, "“", 0, -8, 34, true, fg);
    const data::Day d = data::today();
    const String q = qs.empty() ? String(T_("Write a few sayings, one per line")) : qs[(d.y * 372 + d.m * 31 + d.d) % qs.size()];
    fit(c.o, q, 0, 24, c.w, c.h - 24, it.size == S ? 15 : 18, 12, true, fg, 3);
    return;
  }
  if (m->type == "list") {
    cap(c.o, m->title.length() ? m->title.c_str() : N_("List"), SKIN.own ? 0xff9500 : GREY);
    int y = 22;
    for (const auto& e : m->items) {
      if (y + 20 > c.h) break;
      ring(c.o, 0, y + 2, e.second ? 0x34c759 : SKIN.own ? 0xc7cad1 : GREY, e.second);
      fit(c.o, e.first, 22, y, c.w - 22, 18, 14, 12, false, e.second ? (SKIN.own ? 0x9ca3af : GREY) : fg);
      y += 22;
    }
    return;
  }
  // Sticky note.
  fit(c.o, m->text, 0, 0, c.w, c.h - 16, it.size == S ? m->px : m->px + 3, 12, true, fg, 3);
  const uint32_t foot =
      SKIN.own ? lv_color_to_u32(lv_color_mix(lv_color_hex(m->fg), lv_color_hex(m->bg), 180)) & 0xffffff : GREY;
  lv_obj_t* f = label(c.o, N_("Sticky note"), 12, false, foot);
  lv_obj_align(f, LV_ALIGN_BOTTOM_RIGHT, 0, 2);
}

// ---------------------------------------------------------------- notifications
namespace {

// The Center's newest notifications into the box (rebuilt when the Center's list changes, and every minute for
// the times). The list's version is kept in the box's user data.
void fillNotify(lv_obj_t* b, const tm& now, bool) {
  const uint32_t ver = notif::version();
  if ((uint32_t)(uintptr_t)lv_obj_get_user_data(b) == ver && now.tm_sec) return;
  lv_obj_set_user_data(b, (void*)(uintptr_t)ver);
  lv_obj_clean(b);
  const int w = lv_obj_get_style_width(b, 0), h = lv_obj_get_style_height(b, 0);
  const auto list = notif::latest(8);
  cap(b, N_("Notifications"), SKIN.own ? 0xff3b30 : GREY);
  if (list.empty()) {
    fit(b, N_("No new notifications"), 0, 26, w, h - 26, 14, 12, false, GREY, 3);
    return;
  }
  right(b, String(notif::count()), -2, 20, true, INK);
  int y = 24;
  for (const notif::Shown& n : list) {
    if (y + 18 + 17 > h) break;
    const String text = n.body.length() ? n.title + " · " + n.body : n.title;
    lv_obj_t* l = fit(b, text, 38, y + 18, w - 38, 35, 14, 12, false, INK, 1);
    const int used = heightOf(l);
    if (y + 18 + used > h) {  // no room for this one whole: the count above still says how many
      lv_obj_delete(l);
      break;
    }
    lv_obj_t* ic = notif::icon(b, n.app.c_str(), 28);
    lv_obj_remove_flag(ic, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(ic, 0, y + 2);
    at(b, notif::name(n.app.c_str()), 38, y, 12, true, GREY);
    const String when = notif::when(n.at);
    if (when.length()) right(b, when, y, 12, false, GREY);
    y += 18 + max(used, 17) + 8;
  }
}

}  // namespace

// latest: the Notification Center's newest ones, each with its app's icon, name and time, and its text (two lines
// at most, shrinking to fit); a row that has no room left is not drawn (never cut, never overlapping).
void wNotify(lv_obj_t*, const Item&, Card c) {
  lv_obj_t* b = clear(c.o);
  lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(b, c.w, c.h);
  lv_obj_set_user_data(b, (void*)(uintptr_t)(notif::version() + 1));  // not drawn yet
  const time_t t = time(nullptr);
  tm l;
  localtime_r(&t, &l);
  l.tm_sec = 0;  // draw now
  fillNotify(b, l, false);
  live(b, fillNotify, true);  // checks the Center's version each second (cheap), redraws only on a change
}

// ---------------------------------------------------------------- the device
namespace {

// Battery, WiFi, last sync and firmware into the box (rebuilt every minute). Size in the box's user data.
void fillSystem(lv_obj_t* b, const tm&, bool) {
  lv_obj_clean(b);
  const bool small = lv_obj_get_user_data(b) == nullptr;
  const int w = lv_obj_get_style_width(b, 0), h = lv_obj_get_style_height(b, 0);
  // The skin's own colours: light text on the dark card, or the skin's.
  const uint32_t fg = onOwn(WHITE), dim = onOwnDim(0x94a3b8), line = SKIN.own ? 0x2a3242 : SKIN.dim;
  const int pct = power::percent();
  const bool usb = power::usb();
  // The battery: its percentage big, a bar under it.
  const int bw = 112, by = small ? 50 : 56, bh = small ? 20 : 22;
  cap(b, N_("Device"), dim);
  at(b, String(pct) + "%", -1, 16, small ? 28 : 34, true, fg);
  lv_obj_t* shell = rect(b, 0, by, bw, bh, line, 6);
  lv_obj_set_style_bg_opa(shell, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(shell, 2, 0);
  lv_obj_set_style_border_color(shell, C(dim), 0);
  rect(b, bw + 2, by + bh / 2 - 4, 4, 8, dim, 2);
  rect(b, 4, by + 4, max(4, (bw - 8) * pct / 100), bh - 8, usb || pct > 20 ? 0x34c759 : 0xff3b30, 3);
  // WiFi: four bars and the network's name.
  const bool on = net::state() == net::State::Connected;
  const int bars = on ? net::bars(net::rssi()) : 0;
  const String ssid = on ? net::ssid() : String(T_("No WiFi"));
  const int64_t last = desk::lastSyncMs();
  // i18n: when the last sync was, e.g. "Synced 5 min ago"
  const String sync = last ? i18n::fmt(N_("Synced {0}"), ago(last, desk::nowMs())) : String(T_("Not synced yet"));
  auto wifi = [&](int x, int y) {
    for (int i = 0; i < 4; i++) rect(b, x + i * 5, y + 13 - (4 + i * 3), 3, 4 + i * 3, i < bars ? fg : line, 1);
  };
  if (small) {
    wifi(0, 79);
    fit(b, ssid, 26, 76, w - 26, 20, 13, 12, true, fg, 1);
    // one line: "Synced 5 min ago", else (a longer language) only when
    if (!fitIn(b, sync, 0, 99, w, 19, 12, 12, false, dim, 1)) fit(b, last ? ago(last, desk::nowMs()) : sync, 0, 99, w, 19, 12, 12, false, dim, 1);
    return;
  }
  at(b, usb ? (power::full() ? N_("Fully charged") : N_("Charging")) : N_("On battery"), 0, 86, 12, false, dim);
  // Medium: WiFi, sync and firmware on the right.
  rect(b, 128, 0, 1, h, line);
  const int x = 142, cw = w - x;
  String ver = FW_VERSION;
  if (ver.lastIndexOf('-') >= 0) ver = ver.substring(ver.lastIndexOf('-') + 1);
  at(b, "WiFi", x, 0, 12, false, dim);
  wifi(w - 18, 2);
  fit(b, ssid, x, 16, cw, 22, 15, 12, true, fg, 1);
  at(b, N_("Sync"), x, 40, 12, false, dim);
  fit(b, last ? ago(last, desk::nowMs()) : String(T_("Not synced yet")), x, 56, cw, 22, 15, 12, true, fg, 1);
  at(b, N_("Version"), x, 80, 12, false, dim);
  fit(b, ver, x, 96, cw, 22, 15, 12, true, fg, 1);
}

}  // namespace

void wSystem(lv_obj_t*, const Item& it, Card c) {
  lv_obj_t* b = clear(c.o);
  lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(b, c.w, c.h);
  lv_obj_set_user_data(b, it.size == S ? nullptr : (void*)1);
  const time_t now = time(nullptr);
  tm l;
  localtime_r(&now, &l);
  fillSystem(b, l, false);
  live(b, fillSystem);
}

}  // namespace wid
