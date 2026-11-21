#pragma GCC optimize("Os")  // a cold screen: built small (the firmware must fit 3.75 MB)
// Settings: WiFi, EspOS account, look (theme, wallpaper), display, sound, saver, notifications, time and data
// (time zone, refresh, night hours), shortcuts to the apps' own settings (city, calendars, news, markets), the
// device's pictures, updates, device name, keyboard test, about. Everything the web sets is settable here too
// (docs/settings-parity.md).
#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>

#include "apps.h"
#include "appopts.h"
#include "art.h"
#include "board.h"
#include "brand.h"
#include "data.h"
#include "desk.h"
#include "desksaver.h"
#include "fonts.h"
#include "keyboard.h"
#include "langui.h"
#include "lcd.h"
#include "net.h"
#include "notif.h"
#include "saveredit.h"
#include "os.h"
#include "pkg/pkg.h"
#include "remote.h"
#include "ota.h"
#include "sound.h"
#include "theme.h"
#include "touch.h"
#include "trace.h"
#include "uikit.h"

namespace app_settings {
namespace {

using namespace kit;

Preferences prefs;

String wifiSummary() {
  switch (net::state()) {
    case net::State::Connected: return net::ssid();
    case net::State::Connecting: return T_("connecting");
    default: return T_("not connected");
  }
}

lv_obj_t* field(lv_obj_t* body, const char* text, bool password) {
  lv_obj_t* ta = lv_textarea_create(body);
  lv_obj_set_width(ta, lv_pct(100));
  lv_textarea_set_one_line(ta, true);
  lv_textarea_set_text(ta, text);
  lv_textarea_set_password_mode(ta, password);
  lv_obj_set_style_text_font(ta, fonts::get(18), 0);
  lv_obj_set_style_text_color(ta, C(pal().fg), 0);
  lv_obj_set_style_text_color(ta, C(pal().faint), LV_PART_TEXTAREA_PLACEHOLDER);
  lv_obj_set_style_bg_color(ta, C(pal().cell), 0);
  lv_obj_set_style_border_color(ta, C(col::ACC), 0);
  lv_obj_set_style_border_width(ta, 1, 0);
  lv_obj_set_style_radius(ta, 12, 0);
  lv_obj_set_style_pad_all(ta, 10, 0);
  lv_obj_set_style_bg_color(ta, C(col::ACC), LV_PART_CURSOR | LV_STATE_FOCUSED);
  lv_obj_add_state(ta, LV_STATE_FOCUSED);
  return ta;
}

// A row whose value is kept up to date by `refresh` while the screen is open.
lv_obj_t* valueOf(lv_obj_t* row) { return lv_obj_get_child(row, 1); }

// Marks one of several rows (user data tag) with a check.
void checkRows(lv_obj_t* body, void* tag, lv_obj_t* chosen) {
  for (uint32_t i = 0; i < lv_obj_get_child_count(body); i++) {
    lv_obj_t* r = lv_obj_get_child(body, i);
    if (lv_obj_get_user_data(r) != tag) continue;
    lv_obj_t* v = valueOf(r);
    lv_label_set_text(v, r == chosen ? "✓" : "");
    lv_obj_set_style_text_font(v, &lv_font_montserrat_20, 0);
    lv_label_set_text(v, r == chosen ? LV_SYMBOL_OK : "");
    lv_obj_set_style_text_color(v, C(col::IOS_BLUE), 0);
  }
}

lv_obj_t* choice(lv_obj_t* body, const char* name, bool on, void* tag, lv_event_cb_t cb, void* data) {
  lv_obj_t* r = listRow(body, name, "", cb, data, false);
  lv_obj_set_user_data(r, tag);
  lv_obj_t* v = valueOf(r);
  lv_obj_set_style_text_font(v, &lv_font_montserrat_20, 0);
  lv_label_set_text(v, on ? LV_SYMBOL_OK : "");
  lv_obj_set_style_text_color(v, C(col::IOS_BLUE), 0);
  return r;
}

// ------------------------------------------------------------------ WiFi
lv_obj_t* signalBars(lv_obj_t* parent, int n) {
  lv_obj_t* b = clear(parent);
  lv_obj_set_size(b, 4 * 4 + 3 * 2, 14);
  for (int i = 0; i < 4; i++) {
    lv_obj_t* bar = box(b, i < n ? pal().fg : pal().line, 1);
    lv_obj_set_size(bar, 4, 5 + i * 3);
    lv_obj_set_pos(bar, i * 6, 14 - (5 + i * 3));
  }
  return b;
}

String pendingSsid;
void openWifiPassword(const String& ssid, bool other);
void openWifiDetail(const String& ssid);

struct RowData {
  String ssid;
  bool secure;
  lv_obj_t *bars, *state;
};

void setBars(lv_obj_t* bars, int n) {
  for (int i = 0; i < 4; i++) lv_obj_set_style_bg_color(lv_obj_get_child(bars, i), C(i < n ? pal().fg : pal().line), 0);
}

void updateRow(RowData* d, const net::Ap* ap) {
  const bool here = net::ssid() == d->ssid;
  const bool up = here && net::state() == net::State::Connected;
  // i18n: a WiFi row's state, at most ~10 letters (64 px)
  const char* st = up ? N_("connected") : here ? N_("connecting") : net::isSaved(d->ssid) ? N_("saved") : d->secure ? "" : TC_("wifi", "open");
  setText(d->state, st);
  lv_obj_set_style_text_font(d->state, fonts::get(12, here), 0);
  lv_obj_set_style_text_color(d->state, C(here ? col::GREEN : pal().faint), 0);
  if (ap) setBars(d->bars, net::bars(ap->rssi));
}

constexpr int STATE_W = 64;

lv_obj_t* wifiRow(lv_obj_t* parent, const net::Ap& ap) {
  lv_obj_t* r = box(parent, pal().cell, 12);
  lv_obj_set_size(r, lv_pct(100), 48);
  lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(r, C(pal().press), LV_STATE_PRESSED);
  lv_obj_set_style_pad_hor(r, 14, 0);
  RowData* d = new RowData{ap.ssid, ap.secure, signalBars(r, 0), nullptr};
  lv_obj_align(d->bars, LV_ALIGN_RIGHT_MID, 0, 0);
  d->state = label(r, "", 12);
  lv_obj_align(d->state, LV_ALIGN_RIGHT_MID, -28, 0);
  updateRow(d, &ap);
  lv_obj_t* name = label(r, "", 15, false, pal().fg);
  fitText(name, ap.ssid.c_str(), os::W() - 24 - 28 - 28 - STATE_W, 40, 15, 12, false, 0);
  lv_obj_align(name, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_user_data(r, d);
  own(r, d);
  lv_obj_add_event_cb(r, [](lv_event_t* e) {
    const RowData d = *(RowData*)lv_obj_get_user_data((lv_obj_t*)lv_event_get_current_target(e));
    if (net::ssid() == d.ssid && net::state() == net::State::Connected) openWifiDetail(d.ssid);
    else if (net::isSaved(d.ssid)) net::connectSaved(d.ssid);
    else if (!d.secure) net::connect(d.ssid, "");
    else openWifiPassword(d.ssid, false);
  }, LV_EVENT_SHORT_CLICKED, nullptr);
  lv_obj_add_event_cb(r, [](lv_event_t* e) {
    const RowData* d = (RowData*)lv_obj_get_user_data((lv_obj_t*)lv_event_get_current_target(e));
    if (net::isSaved(d->ssid)) openWifiDetail(d->ssid);
  }, LV_EVENT_LONG_PRESSED, nullptr);
  return r;
}

struct WifiList {
  lv_obj_t* list;
  uint32_t seen, lastScan;
  bool rows;
};

// The first scan builds the list (network in use first, then by signal); later scans refresh
// rows in place so a row never moves under a finger.
void fillWifiList(WifiList* w) {
  w->seen = net::changes();
  const auto& aps = net::results();
  if (w->rows) {
    for (uint32_t i = 0; i < lv_obj_get_child_count(w->list); i++) {
      RowData* d = (RowData*)lv_obj_get_user_data(lv_obj_get_child(w->list, i));
      if (!d) continue;
      const net::Ap* ap = nullptr;
      for (const net::Ap& a : aps)
        if (a.ssid == d->ssid) ap = &a;
      updateRow(d, ap);
    }
    return;
  }
  lv_obj_clean(w->list);
  for (int pass = 0; pass < 2; pass++)
    for (const net::Ap& ap : aps)
      if ((net::ssid() == ap.ssid) == (pass == 0)) wifiRow(w->list, ap);
  w->rows = !aps.empty();
  if (aps.empty()) {
    lv_obj_t* r = box(w->list, pal().cell, 12);
    lv_obj_set_size(r, lv_pct(100), 48);
    lv_obj_set_style_pad_hor(r, 14, 0);
    lv_obj_t* l = label(r, net::scanning() ? N_("Looking for networks") : N_("No networks found"), 15, false, pal().faint);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
    if (net::scanning()) {
      lv_obj_t* sp = spinner(r, 20, pal().line, col::IOS_BLUE);
      lv_obj_align(sp, LV_ALIGN_RIGHT_MID, 0, 0);
    }
  }
  listRow(w->list, N_("Other network"), N_("type its name"), [](lv_event_t*) { openWifiPassword("", true); });
  hint(w->list, net::savedCount()
                    ? N_("Tap a network to type its password. The device joins the saved network with the strongest "
                         "signal by itself; touch and hold a saved network to forget it.")
                    : N_("Tap a network to type its password on the screen keyboard."));
}

void openWifi() {
  lv_obj_t* body;
  lv_obj_t* s = os::screen(N_("WiFi"), &body);
  static WifiList w;
  lv_obj_t* list = stack(body, false, 8);
  w = {list, 0, millis(), false};
  net::scan();
  fillWifiList(&w);
  every(s, 250, [](lv_timer_t* t) {
    WifiList* w = (WifiList*)lv_timer_get_user_data(t);
    if (millis() - w->lastScan > 15000 && net::state() != net::State::Connecting) {
      w->lastScan = millis();
      net::scan();
    }
    if (w->seen != net::changes() && !os::fingerDown()) fillWifiList(w);
  }, &w);
  os::open(s);
}

struct PassScreen {
  lv_obj_t *scr, *ta, *status, *spin, *show, *hint;
  uint32_t seen, okAt;
  bool naming, other;
};
PassScreen pw;

void passStatus(const char* text, uint32_t color, bool busy) {
  fitText(pw.status, text, os::W() - 24 - 28, 40, 13, 12, false, 2);
  lv_obj_set_style_text_color(pw.status, C(color), 0);
  busy ? lv_obj_remove_flag(pw.spin, LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(pw.spin, LV_OBJ_FLAG_HIDDEN);
}

void passSubmit(lv_obj_t* ta) {
  const String text = lv_textarea_get_text(ta);
  if (pw.naming) {
    if (!text.length()) return passStatus(N_("Type the WiFi network's name first."), col::RED, false);
    pendingSsid = text;
    pw.naming = false;
    lv_textarea_set_text(ta, "");
    lv_textarea_set_placeholder_text(ta, T_("Password"));
    wrapText(pw.hint, N_("WiFi password: the keyboard adds no accents and no capital letters. Leave it empty for an open "
                         "network."), os::W() - 24 - 8, 13, false, 3);
    lv_textarea_set_password_mode(ta, true);
    lv_obj_remove_flag(pw.show, LV_OBJ_FLAG_HIDDEN);
    passStatus(i18n::fmt(N_("Network {0}"), text).c_str(), pal().faint, false);
    kb::setMode(kb::Mode::Password, T_("Connect"));
    return;
  }
  if (!pw.other && !text.length()) return passStatus(N_("Type the password first."), col::RED, false);
  if (text.length() > 0 && text.length() < 8) return passStatus(N_("A WiFi password has at least 8 characters."), col::RED, false);
  net::connect(pendingSsid, text);
  pw.seen = net::changes();
  passStatus(i18n::fmt(N_("Connecting to {0}"), pendingSsid).c_str(), pal().faint, true);
}

void openWifiPassword(const String& ssid, bool other) {
  pendingSsid = ssid;
  lv_obj_t* body;
  pw = {};
  pw.naming = pw.other = other;
  pw.scr = os::screen(other ? N_("Other network") : ssid.c_str(), &body, os::NOSCROLL);
  lv_obj_t* wrap = clear(body);
  lv_obj_set_size(wrap, lv_pct(100), LV_SIZE_CONTENT);
  pw.ta = field(wrap, "", !other);
  // Room for "Show" / "Hide" (longer in some languages).
  lv_obj_set_style_pad_right(pw.ta, max(56, max(textW(N_("Show"), 13, true), textW(N_("Hide"), 13, true)) + 26), 0);
  lv_textarea_set_placeholder_text(pw.ta, other ? T_("Network name") : T_("Password"));
  pw.show = label(wrap, N_("Show"), 13, true, pal().link);
  lv_obj_set_style_pad_all(pw.show, 10, 0);
  lv_obj_set_ext_click_area(pw.show, 8);
  lv_obj_add_flag(pw.show, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_align(pw.show, LV_ALIGN_RIGHT_MID, -2, 0);
  if (other) lv_obj_add_flag(pw.show, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(pw.show, [](lv_event_t*) {
    const bool hidden = !lv_textarea_get_password_mode(pw.ta);
    lv_textarea_set_password_mode(pw.ta, hidden);
    lv_label_set_text(pw.show, hidden ? T_("Show") : T_("Hide"));
  }, LV_EVENT_CLICKED, nullptr);
  pw.hint = hint(body, other ? N_("Type the network name exactly, with its capital and small letters.")
                             : N_("WiFi password: the keyboard adds no accents and no capital letters."));
  lv_obj_t* row = clear(body);
  lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
  pw.status = label(row, "", 13, false, pal().faint);
  pw.spin = spinner(row, 18, pal().line, col::IOS_BLUE);
  lv_obj_align(pw.spin, LV_ALIGN_TOP_RIGHT, 0, 0);
  lv_obj_add_flag(pw.spin, LV_OBJ_FLAG_HIDDEN);
  every(pw.scr, 200, [](lv_timer_t*) {
    if (pw.okAt) {
      if (millis() - pw.okAt > 700) {
        pw.okAt = 0;
        os::back();
      }
      return;
    }
    if (pw.seen == net::changes() || pw.naming) return;
    pw.seen = net::changes();
    if (net::ssid() == pendingSsid && net::state() == net::State::Connected) {
      passStatus(i18n::fmt(N_("Connected to {0}"), pendingSsid).c_str(), col::GREEN, false);
      kb::close();
      pw.okAt = millis();
    } else if (net::state() == net::State::Idle && net::error()[0]) {
      passStatus(net::error(), col::RED, false);
    }
  });
  os::open(pw.scr);
  kb::open(pw.ta, other ? kb::Mode::Plain : kb::Mode::Password, passSubmit, other ? T_("Next") : T_("Connect"));
}

void openWifiDetail(const String& ssid) {
  pendingSsid = ssid;
  lv_obj_t* body;
  lv_obj_t* s = os::screen(ssid.c_str(), &body);
  const bool here = net::ssid() == ssid && net::state() == net::State::Connected;
  listRow(body, N_("Status"), here ? N_("connected") : N_("saved"), nullptr);
  if (here) {
    char buf[48];
    const int r = net::rssi();
    snprintf(buf, sizeof buf, "%s (%d dBm)", r >= -60 ? T_("strong") : r >= -72 ? T_("fair") : T_("weak"), r);
    listRow(body, N_("Signal"), buf, nullptr);
    listRow(body, N_("IP address"), net::ip().c_str(), nullptr);
    listRow(body, N_("Disconnect"), "", [](lv_event_t*) {
      net::disconnect();
      os::back();
    });
  }
  lv_obj_t* f = listRow(body, N_("Forget this network"), "", [](lv_event_t*) {
    net::forget(pendingSsid);
    os::back();
  }, nullptr, false);
  lv_obj_set_style_text_color(lv_obj_get_child(f, 0), C(col::RED), 0);
  hint(body, N_("Forgetting a network deletes its saved password; the device no longer joins it by itself."));
  os::open(s);
}

// ------------------------------------------------------------------ EspOS account
struct LinkScreen {
  lv_obj_t *scr, *body, *qr, *code, *status;
  String shownCode;
  bool wasLinked;
};
LinkScreen ls;

String spacedCode(const String& c) { return c.length() == 6 ? c.substring(0, 3) + " " + c.substring(3) : c; }
String hhmmOf(int64_t ms) {
  const time_t t = ms / 1000;
  tm x;
  localtime_r(&t, &x);
  return hhmm(x.tm_hour, x.tm_min);
}

void fillAccount(lv_obj_t* body);

void fillPairing(lv_obj_t* body) {
  lv_obj_clean(body);
  ls.qr = ls.code = nullptr;
  if (net::state() != net::State::Connected) {
    hint(body, N_("Connect to WiFi first to get a link code."));
    listRow(body, "WiFi", wifiSummary().c_str(), [](lv_event_t*) { openWifi(); });
    ls.status = nullptr;
    return;
  }
  const bool land = os::landscape();
  lv_obj_t* wrap = clear(body);
  lv_obj_set_size(wrap, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(wrap, land ? LV_FLEX_FLOW_ROW : LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(wrap, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_gap(wrap, 12, 0);
  lv_obj_t* qrBox = box(wrap, 0xffffff, 14);
  lv_obj_set_size(qrBox, 166, 166);
  ls.qr = lv_qrcode_create(qrBox);
  lv_qrcode_set_size(ls.qr, 146);
  lv_qrcode_set_dark_color(ls.qr, lv_color_black());
  lv_qrcode_set_light_color(ls.qr, lv_color_white());
  lv_obj_center(ls.qr);
  lv_obj_t* right = clear(wrap);
  lv_obj_set_size(right, land ? os::W() - 24 - 166 - 12 : lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(right, LV_FLEX_ALIGN_START, land ? LV_FLEX_ALIGN_START : LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_t* how = label(right, "", 13, false, pal().faint);
  fitText(how, N_("Scan the code, or enter it at espos.dev/app"), land ? os::W() - 210 : os::W() - 24, 40, 13, 12);
  ls.code = label(right, "", 34, true, pal().fg);
  lv_obj_set_style_text_letter_space(ls.code, 4, 0);
  ls.status = label(right, "", 13, true, col::ORANGE);
  hint(body, N_("Or plug the USB cable into a computer, open the EspOS website, Devices, Add device. The code "
                "changes every 10 minutes."));
  ls.shownCode = "";
}

void refreshLink(lv_timer_t*) {
  if (!ls.scr) return;
  const bool linked = desk::link() == desk::Link::Linked;
  if (linked != ls.wasLinked) {
    ls.wasLinked = linked;
    if (linked) {
      sound::chime();
      fillAccount(ls.body);
      return;
    }
    desk::startPairing();
    fillPairing(ls.body);
  }
  if (linked) {
    String last = desk::syncing() ? String(T_("syncing"))
                  : desk::lastError()[0] ? String(desk::lastError())
                  : desk::lastSyncMs() ? i18n::fmt(N_("at {0}"), hhmmOf(desk::lastSyncMs())) : String(T_("not yet"));
    if (ls.code) setText(ls.code, last.c_str());
    return;
  }
  if (!ls.status) {
    if (net::state() == net::State::Connected && !ls.qr) fillPairing(ls.body);
    return;
  }
  const String code = desk::pairCode();
  if (code != ls.shownCode) {
    ls.shownCode = code;
    setText(ls.code, code.length() ? spacedCode(code).c_str() : "");
    if (code.length()) {
      const String url = desk::pairUrl();
      lv_qrcode_update(ls.qr, url.c_str(), url.length());
    }
  }
  setText(ls.status, desk::pairStatus());
}

void fillAccount(lv_obj_t* body) {
  lv_obj_clean(body);
  ls.qr = ls.code = ls.status = nullptr;
  listRow(body, N_("Device"), desk::deviceName().length() ? desk::deviceName().c_str() : "EspOS S3", nullptr);
  listRow(body, N_("Account"), desk::user().c_str(), nullptr);
  String last = desk::lastSyncMs() ? i18n::fmt(N_("at {0}"), hhmmOf(desk::lastSyncMs())) : String(T_("not yet"));
  lv_obj_t* syncRow = listRow(body, N_("Sync now"), last.c_str(), [](lv_event_t*) { desk::syncNow(); }, nullptr, false);
  keep(&ls.code, valueOf(syncRow));
  lv_obj_t* u = listRow(body, N_("Unlink"), "", [](lv_event_t*) { desk::unlink(); }, nullptr, false);
  lv_obj_set_style_text_color(lv_obj_get_child(u, 0), C(col::RED), 0);
  hint(body, N_("Home Screen, widgets, notes and alarms can be edited on the device and at espos.dev/app; the device "
                "syncs by itself."));
}

void openLink() {
  lv_obj_t* body;
  ls = {};
  ls.scr = os::screen(N_("EspOS account"), &body);
  ls.body = body;
  ls.wasLinked = desk::link() == desk::Link::Linked;
  if (ls.wasLinked) fillAccount(body);
  else {
    desk::startPairing();
    fillPairing(body);
  }
  lv_obj_add_event_cb(ls.scr, [](lv_event_t* e) {
    if (lv_event_get_current_target(e) != ls.scr) return;
    ls.scr = nullptr;
    desk::stopPairing();
  }, LV_EVENT_DELETE, nullptr);
  every(ls.scr, 300, refreshLink);
  os::open(ls.scr);
}

// ------------------------------------------------------------------ look
const char* rotName(int r) {
  static const char* const names[] = {N_("Portrait"), N_("Landscape"), N_("Portrait, upside down"),
                                      N_("Landscape, upside down")};
  return T_(names[r & 3]);
}

void openLook() {
  lv_obj_t* body;
  lv_obj_t* s = os::screen(N_("Appearance"), &body);
  caption(body, N_("Style"));
  static const char* KINDS[][2] = {{"color", N_("Color")}, {"light", N_("Light")}, {"dark", N_("Dark")}, {"photo", N_("Your photo")}};
  const String cur = theme::kindId();
  lv_obj_t* grid = clear(body);
  lv_obj_set_size(grid, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_gap(grid, 8, 0);
  const int tw = (os::W() - 24 - 8) / 2;
  for (auto& k : KINDS) {
    const bool on = cur == k[0];
    lv_obj_t* t = box(grid, 0x000000, 14);
    lv_obj_set_size(t, tw, 70);
    lv_obj_set_style_border_width(t, on ? 3 : 0, 0);
    lv_obj_set_style_border_color(t, C(col::IOS_BLUE), 0);
    lv_obj_set_style_pad_all(t, 10, 0);
    lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);
    if (!strcmp(k[0], "color")) grad(t, 0x2b2580, 0x6d1d5c);
    else if (!strcmp(k[0], "light")) grad(t, 0xeef2f9, 0xd9e1f0);
    else if (!strcmp(k[0], "dark")) grad(t, 0x05070c, 0x1a1f2b);
    else grad(t, 0x5856d6, 0xaf52de);
    lv_obj_t* l = label(t, k[1], 15, true, !strcmp(k[0], "light") ? 0x16181d : 0xffffff);
    lv_obj_align(l, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_add_event_cb(t, [](lv_event_t* e) {
      desk::setSetting("s3.theme", (const char*)lv_event_get_user_data(e));
      os::toast(N_("Appearance changed"));
      lv_async_call([](void*) { os::goHome(); }, nullptr);
    }, LV_EVENT_CLICKED, (void*)k[0]);
  }
  if (cur == "color") {
    caption(body, N_("Wallpaper"));
    lv_obj_t* walls = clear(body);
    lv_obj_set_size(walls, lv_pct(100), 128);
    lv_obj_set_flex_flow(walls, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(walls, 8, 0);
    lv_obj_add_flag(walls, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_scroll_dir(walls, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(walls, LV_SCROLLBAR_MODE_OFF);
    const String wcur = theme::wall().id;
    for (int i = 0; i < theme::WALL_COUNT; i++) {
      const theme::Wall& w = theme::WALLS[i];
      lv_obj_t* t = box(walls, 0x000000, 12);
      lv_obj_set_size(t, 70, 124);
      lv_obj_set_style_clip_corner(t, true, 0);
      lv_obj_set_style_border_width(t, wcur == w.id ? 3 : 0, 0);
      lv_obj_set_style_border_color(t, C(col::IOS_BLUE), 0);
      lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);
      lv_image_dsc_t* pv = theme::makePreview(w, 70, 104);
      if (pv) {
        img(t, pv);
        lv_obj_add_event_cb(t, [](lv_event_t* e) { theme::freePreview((lv_image_dsc_t*)lv_event_get_user_data(e)); },
                            LV_EVENT_DELETE, pv);
      }
      lv_obj_t* l = label(t, "", 12, true, pal().fg);
      fitText(l, w.name, 68, 18, 12, 12, true, 0);  // a long name: letters closer (never cut)
      lv_obj_set_style_bg_color(l, C(pal().cell), 0);
      lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
      lv_obj_set_size(l, 70, 20);
      lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_align(l, LV_ALIGN_BOTTOM_MID, 0, 0);
      lv_obj_add_event_cb(t, [](lv_event_t* e) {
        desk::setSetting("s3.wall", (const char*)lv_event_get_user_data(e));
        os::toast(N_("Wallpaper changed"));
        lv_async_call([](void*) { os::goHome(); }, nullptr);
      }, LV_EVENT_CLICKED, (void*)w.id);
    }
  }
  if (cur == "photo") hint(body, N_("The wallpaper is the first of the device's photos (add photos on the web)."));
  lv_obj_t* r = listRow(body, N_("Dark at night"), "", nullptr);
  lv_obj_t* sw = toggle(r, desk::settingInt("s3.theme_auto", 1), [](lv_event_t* e) {
    const bool on = lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED);
    desk::setSetting("s3.theme_auto", on ? "1" : "0");
  });
  lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
  hint(body, N_("Apps are light by day and turn dark from 7 PM to 6 AM."));
  os::open(s);
}

// ------------------------------------------------------------------ display, saver, sound
void brightnessSlider(lv_obj_t* body) {
  lv_obj_t* c = cell(body, 14);
  lv_obj_t* sl = lv_slider_create(c);
  lv_obj_set_width(sl, lv_pct(100));
  lv_slider_set_range(sl, 5, 100);
  lv_slider_set_value(sl, os::brightness(), LV_ANIM_OFF);
  lv_obj_set_style_bg_color(sl, C(col::IOS_BLUE), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(sl, C(pal().line), LV_PART_MAIN);
  lv_obj_set_style_bg_color(sl, C(0xffffff), LV_PART_KNOB);
  lv_obj_set_style_shadow_width(sl, 6, LV_PART_KNOB);
  lv_obj_set_style_shadow_opa(sl, LV_OPA_30, LV_PART_KNOB);
  lv_obj_add_event_cb(sl, [](lv_event_t* e) {
    os::setBrightness(lv_slider_get_value((lv_obj_t*)lv_event_get_target(e)), false);
  }, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(sl, [](lv_event_t* e) {
    os::setBrightness(lv_slider_get_value((lv_obj_t*)lv_event_get_target(e)), true);
  }, LV_EVENT_RELEASED, nullptr);
}

// One way up as a picture: the device drawn turned that way, a grey mark where its USB-C port then is (on the
// physical bottom edge: LVGL's 90 puts it on the left, 270 on the right), the name under it (h: 100, or 116 when a
// name takes two lines, so it stays clear of the picture).
void rotTile(lv_obj_t* parent, int r, int w, int h) {
  const bool on = r == os::rotation(), side = r & 1;
  lv_obj_t* t = box(parent, pal().cell, 14);
  lv_obj_set_size(t, w, h);
  lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(t, C(pal().press), LV_STATE_PRESSED);
  if (on) {
    lv_obj_set_style_border_width(t, 3, 0);
    lv_obj_set_style_border_color(t, C(col::IOS_BLUE), 0);
  }
  const int dw = side ? 50 : 34, dh = side ? 34 : 50;
  lv_obj_t* dev = box(t, 0x16181d, 6);
  lv_obj_set_size(dev, dw, dh);
  lv_obj_set_style_pad_all(dev, 3, 0);
  lv_obj_add_flag(dev, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_align(dev, LV_ALIGN_TOP_MID, 0, 12 + (side ? 8 : 0));
  lv_obj_t* glass = box(dev, 0x4a35b8, 3);
  lv_obj_set_size(glass, lv_pct(100), lv_pct(100));
  grad(glass, 0x4a35b8, 0xa3266f);
  lv_obj_t* usb = box(dev, 0xc3c6ce, 2);  // on the edge's frame (dev's content box is inset by its 3 px pad)
  const int uw = side ? 4 : 14, uh = side ? 14 : 4;
  lv_obj_set_size(usb, uw, uh);
  lv_obj_set_pos(usb, side ? (r == 1 ? -3 : dw - 6 - uw + 3) : (dw - 6 - uw) / 2,
                 side ? (dh - 6 - uh) / 2 : (r == 2 ? -3 : dh - 6 - uh + 3));
  lv_obj_t* l = label(t, "", 12, on, pal().fg);
  fitText(l, rotName(r), w - 12, textW(rotName(r), 12, on) > w - 12 ? 38 : 30, 13, 12, on, 0);  // one line if it can
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(l, LV_ALIGN_BOTTOM_MID, 0, -6);
  lv_obj_add_event_cb(t, [](lv_event_t* e) {
    const int r = (int)(intptr_t)lv_event_get_user_data(e);
    if (r == os::rotation()) return;
    apps::setRoute("settings", "display");  // this page opens again, laid out for the new shape
    lv_async_call([](void* r) { os::applyRotation((int)(intptr_t)r); }, (void*)(intptr_t)r);
  }, LV_EVENT_CLICKED, (void*)(intptr_t)r);
}

void openDisplay() {
  lv_obj_t* body;
  lv_obj_t* s = os::screen(N_("Display"), &body);
  caption(body, N_("Brightness"));
  brightnessSlider(body);
  caption(body, N_("Orientation"));
  // Four pictures: one row in landscape, two by two in portrait.
  lv_obj_t* row = clear(body);
  lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_gap(row, 8, 0);
  const int per = os::landscape() ? 4 : 2, w = (os::W() - 24 - 8 * (per - 1)) / per;
  int h = 100;
  for (int r : {0, 1, 2, 3})
    if (textW(rotName(r), 12, r == os::rotation()) > w - 12) h = 116;  // a name on two lines (bold: the one chosen)
  for (int r : {0, 1, 2, 3}) rotTile(row, r, w, h);
  hint(body, N_("There is no tilt sensor: the screen turns when you pick a way up here, with Rotate in the Control "
                "Center (touch and hold it to flip the screen over), or on the EspOS web."));
  os::open(s);
}

int saverSecNow() { return desk::settingInt("s3.saver", 120); }

// The saver as it is (its first page of this orientation, drawn small) and the way into its editor.
lv_obj_t* saverPic = nullptr;
void fillSaverPic() {
  if (!saverPic) return;
  lv_obj_clean(saverPic);
  const desksaver::Doc& d = desksaver::doc();
  const std::vector<desksaver::Page>& v = os::landscape() ? d.l : d.p;
  const int div = os::landscape() ? 5 : 4;
  lv_obj_t* f = box(saverPic, 0x000000, 8);
  lv_obj_set_size(f, os::W() / div, os::H() / div);
  lv_obj_set_style_clip_corner(f, true, 0);
  if (!v.empty()) desksaver::thumb(f, v[0], div);
  lv_obj_t* t = label(saverPic, "", 13, false, pal().dim);
  const char* night = d.night == "off" ? N_("screen off") : d.night == "day" ? N_("same as by day")
                       : (os::landscape() ? d.hasNl : d.hasNp) ? N_("its own night page") : N_("dim red clock");
  const String pages = os::landscape() ? i18n::plural(v.size(), NP_("{n} landscape page|{n} landscape pages"))
                                       : i18n::plural(v.size(), NP_("{n} portrait page|{n} portrait pages"));
  wrapText(t, (pages + "\n" + i18n::fmt(N_("At night: {0}"), T_(night))).c_str(), os::W() - 24 - 28 - os::W() / div - 14, 13);
  lv_obj_align(t, LV_ALIGN_LEFT_MID, os::W() / div + 14, 0);
}

void openSaver() {
  lv_obj_t* body;
  lv_obj_t* s = os::screen(N_("Screen saver"), &body);
  lv_obj_t* c = cell(body, 14);
  keep(&saverPic, clear(c));
  lv_obj_set_size(saverPic, lv_pct(100), os::H() / (os::landscape() ? 5 : 4));
  fillSaverPic();
  listRow(body, N_("Edit the screen saver"), "", [](lv_event_t*) {
    os::showSaver();
    saveredit::open([] {
      os::wake();  // back here, the picture redrawn
      fillSaverPic();
    });
  });
  caption(body, N_("Start it after no touch for"));
  static const int opts[] = {60, 120, 300, 600, 0};
  static const char* const names[] = {N_("1 min"), N_("2 min"), N_("5 min"), N_("10 min"), N_("Off")};
  for (int i = 0; i < 5; i++)
    choice(body, names[i], opts[i] == saverSecNow(), (void*)1, [](lv_event_t* e) {
      desk::setSetting("s3.saver", String((int)(intptr_t)lv_event_get_user_data(e)));
      checkRows(lv_obj_get_parent((lv_obj_t*)lv_event_get_current_target(e)), (void*)1,
                (lv_obj_t*)lv_event_get_current_target(e));
    }, (void*)(intptr_t)opts[i]);
  hint(body, N_("Put the time, date, text, photos and widgets anywhere; portrait and landscape have their own pages. "
                "On the saver, touch and hold for 1 second to edit it too; on the web: the device page, Screen saver tab."));
  os::open(s);
}

void openSound() {
  lv_obj_t* body;
  lv_obj_t* s = os::screen(N_("Sound"), &body, os::TWO);
  caption(body, N_("Volume"));
  lv_obj_t* c = cell(body, 14);
  lv_obj_t* sl = lv_slider_create(c);
  lv_obj_set_width(sl, lv_pct(100));
  lv_slider_set_range(sl, 0, 100);
  lv_slider_set_value(sl, os::volume(), LV_ANIM_OFF);
  lv_obj_set_style_bg_color(sl, C(col::IOS_BLUE), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(sl, C(pal().line), LV_PART_MAIN);
  lv_obj_set_style_bg_color(sl, C(0xffffff), LV_PART_KNOB);
  lv_obj_add_event_cb(sl, [](lv_event_t* e) {
    os::setVolume(lv_slider_get_value((lv_obj_t*)lv_event_get_target(e)), false);
  }, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(sl, [](lv_event_t* e) {
    os::setVolume(lv_slider_get_value((lv_obj_t*)lv_event_get_target(e)), true);
    sound::click();
  }, LV_EVENT_RELEASED, nullptr);
  lv_obj_t* r = listRow(body, N_("Touch sounds"), "", nullptr);
  lv_obj_t* sw = toggle(r, desk::settingInt("s3.click", 1), [](lv_event_t* e) {
    desk::setSetting("s3.click", lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED) ? "1" : "0");
  });
  lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_t* d = listRow(body, N_("Do Not Disturb"), "", nullptr);
  lv_obj_t* sw2 = toggle(d, desk::settingInt("s3.dnd", 0), [](lv_event_t* e) {
    desk::setSetting("s3.dnd", lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED) ? "1" : "0");
  });
  lv_obj_align(sw2, LV_ALIGN_RIGHT_MID, 0, 0);
  listRow(body, N_("Test the speaker"), "", [](lv_event_t*) { sound::test(); });
  listRow(body, N_("Test the alarm sound"), "", [](lv_event_t*) { sound::alarmBeep(); });
  hint(body, N_("Do Not Disturb silences touch sounds and notifications; alarms still ring."));
  os::open(s);
}

// ------------------------------------------------------------------ update, name, about
// Settings › Update (mockups/s3-update.html, states 1, 2 and 4; the install itself is ota.cpp's install mode).
namespace upd {
lv_obj_t* box = nullptr;  // the part that changes with the offer
String shown;             // the offer (or error) it was built for

String shortVer(const String& v) {
  const int dash = v.lastIndexOf('-');
  return dash >= 0 ? v.substring(dash + 1) : v;
}
String mb(uint32_t b) {
  String s = String(b / 1048576.0f, 1);
  s.replace(".", i18n::decSep());
  return s;
}

// The first letter in upper case (UTF-8 aware): ota's kept errors start in lower case.
String capFirst(const String& s) {
  const uint8_t c = s.length() ? s[0] : 0;
  const int n = c < 0x80 ? 1 : c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2;
  return upperVi(s.substring(0, n)) + s.substring(n);
}

void fill() {
  if (!box) return;
  lv_obj_clean(box);
  const desk::FwOffer& o = desk::fwOffer();
  const String& err = ota::lastError();
  shown = o.version + "|" + err;
  if (err.length()) {
    lv_obj_t* c = cell(box, 14);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c, 4, 0);
    label(c, i18n::fmt(N_("Couldn't install {0}"), shortVer(ota::lastErrorVersion())).c_str(), 15, true, col::RED);
    lv_obj_t* why = label(c, "", 13, false, pal().dim);
    // ota keeps the English (the language may have changed since): translated here.
    fitText(why, i18n::fmt(N_("{0}. The device still runs {1}."), capFirst(T_(err.c_str())), shortVer(FW_VERSION)).c_str(),
            os::W() - 52, 120, 13, 12, false, 3);
  }
  if (o.version.length()) {
    lv_obj_t* c = cell(box, 14);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c, 4, 0);
    label(c, (upperVi(T_("New version")) + (o.date.length() ? " · " + o.date : String(""))).c_str(), 12, true, pal().faint);
    label(c, shortVer(o.version).c_str(), 20, true, pal().fg);
    if (o.notes.length()) {
      lv_obj_t* n = label(c, "", 13, false, pal().dim);
      String notes = "• " + o.notes;
      notes.replace("\n", "\n• ");
      fitText(n, notes.c_str(), os::W() - 52, 200, 13, 12, false, 3);
    }
    lv_obj_t* b = button(box, (String(err.length() ? T_("Try again") : T_("Download and install")) + (o.size ? " · " + mb(o.size) + " MB" : String(""))).c_str(),
                         col::IOS_BLUE, 0xffffff, [](lv_event_t*) {
                           ota::clearError();
                           ota::start();
                         });
    lv_obj_set_width(b, lv_pct(100));
    hint(box, N_("The device restarts and downloads at once, showing only the update screen until it is done. If the "
                 "new version fails, the device goes back to the one it has."));
  } else if (desk::link() == desk::Link::Linked) {
    lv_obj_t* c = cell(box, 14);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c, 4, 0);
    fitText(label(c, "", 15, true, col::GREEN), N_("The device has the latest version"), os::W() - 52, 44, 15, 13, true, 0);
    String t = T_("The device checks at every sync.");
    if (desk::lastSyncMs()) t = i18n::fmt(N_("Last checked at {0}."), hhmmOf(desk::lastSyncMs())) + " " + t;
    lv_obj_t* h = label(c, "", 12, false, pal().faint);
    fitText(h, t.c_str(), os::W() - 52, 60, 12, 12, false, 2);
    listRow(box, N_("Check now"), "", [](lv_event_t*) {
      desk::syncNow();
      os::toast(N_("Checking"));
    });
  } else {
    hint(box, N_("Link to EspOS to get updates over WiFi."));
  }
}
}  // namespace upd

void openUpdate() {
  lv_obj_t* body;
  lv_obj_t* s = os::screen(N_("Update"), &body);
  // The version card.
  lv_obj_t* v = cell(body, 12);
  lv_obj_t* ico = brand::mark(v, 32, pal().fg);  // the EspOS mark (brand.h), in the 36 px slot
  lv_obj_set_pos(ico, 2, 2);
  lv_obj_t* name = label(v, ("EspOS " + upd::shortVer(FW_VERSION)).c_str(), 15, true, pal().fg);
  lv_obj_align(name, LV_ALIGN_TOP_LEFT, 46, 0);
  lv_obj_t* bl = label(v, board::label(), 12, false, pal().dim);
  lv_obj_align(bl, LV_ALIGN_TOP_LEFT, 46, 20);
  lv_obj_set_height(v, 60);
  upd::box = stack(body, false, 10);
  lv_obj_set_width(upd::box, lv_pct(100));
  keep(&upd::box, upd::box);
  upd::fill();
  if (desk::link() == desk::Link::Linked) {
    lv_obj_t* r = listRow(body, N_("Update by itself while charging"), "", nullptr);
    lv_obj_t* sw = toggle(r, desk::settingInt("dev.fw_auto", 0), [](lv_event_t* e) {
      desk::setSetting("dev.fw_auto", lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED) ? "1" : "0");
      desk::syncNow();
    });
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
  }
  // A check that brought an offer (or an error) redraws the middle part.
  every(s, 700, [](lv_timer_t*) {
    if (desk::fwOffer().version + "|" + ota::lastError() != upd::shown) upd::fill();
  });
  os::open(s);
}

// The device's name: the account's (the web's, from each sync) once linked, else the local one.
String nameNow() {
  if (desk::link() == desk::Link::Linked && desk::deviceName().length()) return desk::deviceName();
  return prefs.getString("name", "");
}

void openName() {
  lv_obj_t* body;
  lv_obj_t* s = os::screen(N_("Device name"), &body, os::NOSCROLL);
  lv_obj_t* ta = field(body, nameNow().c_str(), false);
  lv_textarea_set_max_length(ta, 40);
  // i18n: Vietnamese adds how Telex typing works (the keyboard types Telex only in Vietnamese)
  hint(body, desk::link() == desk::Link::Linked ? N_("This name shows on the EspOS website too.")
                                                : N_("Touch and hold a vowel to pick an accented letter."));
  os::open(s);
  kb::open(ta, kb::Mode::Text, [](lv_obj_t* t) {
    String n = lv_textarea_get_text(t);
    n.trim();
    if (!n.length()) return os::toast(N_("Type a name first"));
    prefs.putString("name", n);
    if (desk::link() == desk::Link::Linked) desk::setSetting("dev.name", n), desk::syncNow();
    kb::close();
    os::toast(N_("Device name saved"));
  });
}

void openAbout() {
  lv_obj_t* body;
  lv_obj_t* s = os::screen(N_("About this device"), &body, os::TWO);
  char buf[64];
  listRow(body, N_("Firmware"), FW_VERSION, nullptr);
  snprintf(buf, sizeof buf, "%s rev%d", ESP.getChipModel(), ESP.getChipRevision());
  listRow(body, N_("Chip"), buf, nullptr);
  snprintf(buf, sizeof buf, T_("%u KB free"), (unsigned)(ESP.getFreePsram() / 1024));
  listRow(body, "PSRAM 8 MB", buf, nullptr);
  snprintf(buf, sizeof buf, T_("%u KB free"), (unsigned)(ESP.getFreeHeap() / 1024));
  listRow(body, N_("Internal RAM"), buf, nullptr);
  listRow(body, "MAC", WiFi.macAddress().c_str(), nullptr);
  snprintf(buf, sizeof buf, T_("%lu min"), (unsigned long)(millis() / 60000));
  listRow(body, N_("Uptime"), buf, nullptr);
  os::open(s);
}

// ------------------------------------------------------------------ remote control (remote.h)
struct {
  lv_obj_t *scr = nullptr, *body = nullptr;
  String shownUrl;
} rs;

void fillRemote() {
  lv_obj_clean(rs.body);
  lv_obj_t* r = listRow(rs.body, N_("Allow"), "", nullptr);
  lv_obj_t* sw = toggle(r, remote::enabled(), [](lv_event_t* e) {
    remote::setEnabled(lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED));
    lv_async_call([](void*) { if (rs.scr) fillRemote(); }, nullptr);
  });
  lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
  rs.shownUrl = remote::url();
  if (remote::enabled()) {
    const String p = remote::pin();
    listRow(rs.body, N_("PIN"), (p.substring(0, 3) + " " + p.substring(3)).c_str(), nullptr);
    String where = rs.shownUrl;
    where.replace("http://", "");
    listRow(rs.body, N_("Address"), where.length() ? where.c_str() : N_("waiting for the network"), nullptr);
  }
  hint(rs.body, N_("Open it from the EspOS website (the device page, Remote control) from anywhere, as long as the device "
                   "is online. On the same WiFi it is faster: type the address above into a browser, then the PIN. Turn "
                   "it off when you are not using it; turning it on again gives a new PIN."));
}

void openRemote() {
  lv_obj_t* body;
  rs = {};
  rs.scr = os::screen(N_("Remote control"), &body);
  rs.body = body;
  fillRemote();
  lv_obj_add_event_cb(rs.scr, [](lv_event_t* e) {
    if (lv_event_get_current_target(e) == rs.scr) rs.scr = nullptr;
  }, LV_EVENT_DELETE, nullptr);
  every(rs.scr, 1000, [](lv_timer_t*) {  // the address comes when the network does
    if (rs.scr && remote::url() != rs.shownUrl) fillRemote();
  });
  os::open(rs.scr);
}

void openHomeReset() {
  lv_obj_t* body;
  lv_obj_t* s = os::screen(N_("Home Screen"), &body);
  hint(body, N_("Touch and hold an empty spot on the Home Screen to edit it: drag to move, tap − to remove, + Widget to "
                "add. You can edit it on the web too."));
  // Landscape has its own pages (s3.home_l), first laid out from the portrait ones.
  listRow(body, N_("Lay out landscape from portrait again"), "", [](lv_event_t*) {
    desk::setSetting("s3.home_l", "");
    os::toast(N_("Landscape pages laid out again"));
    lv_async_call([](void*) { os::rebuild(); }, nullptr);
  }, nullptr, false);
  hint(body, N_("Landscape has its own pages. They start as a copy of the portrait ones; after that each is edited on "
                "its own."));
  lv_obj_t* r = listRow(body, N_("Reset to the original layout"), "", [](lv_event_t*) {
    desk::setSetting("s3.home", "");
    desk::setSetting("s3.home_l", "");
    os::toast(N_("Home Screen reset"));
    lv_async_call([](void*) { os::rebuild(); }, nullptr);
  }, nullptr, false);
  lv_obj_set_style_text_color(lv_obj_get_child(r, 0), C(col::RED), 0);
  os::open(s);
}

// ------------------------------------------------------------------ time and data (as the web's S3 settings)
// sys.tz_min: the web's time zones (places.ts TIME_ZONES), applied at once.
const struct {
  int16_t min;
  const char *name, *places;
} ZONES[] = {{420, "GMT+7", N_("Vietnam, Thailand, Laos, Cambodia, Jakarta")},  // i18n: places in a time zone
             {480, "GMT+8", N_("Singapore, Malaysia, Philippines, China, Taiwan, Hong Kong, Perth")},
             {540, "GMT+9", N_("Japan, Korea")},
             {600, "GMT+10", N_("Brisbane, Guam")},
             {720, "GMT+12", N_("New Zealand (winter)")},
             {330, "GMT+5:30", N_("India")},
             {240, "GMT+4", N_("Dubai")},
             {180, "GMT+3", N_("Moscow, Saudi Arabia, Turkey")},
             {120, "GMT+2", N_("Eastern Europe (winter), Egypt, South Africa")},
             {60, "GMT+1", N_("France, Germany, Italy (winter)")},
             {0, "GMT+0", N_("UK, Portugal (winter)")},
             {-180, "GMT−3", N_("Brazil, Argentina")},
             {-240, "GMT−4", N_("New York, Toronto (summer)")},
             {-300, "GMT−5", N_("New York (winter), Chicago (summer)")},
             {-360, "GMT−6", N_("Chicago (winter), Denver (summer)")},
             {-420, "GMT−7", N_("Los Angeles (summer), Denver (winter), Arizona")},
             {-480, "GMT−8", N_("Los Angeles, Vancouver (winter)")},
             {-600, "GMT−10", N_("Hawaii")}};

int tzNow() { return desk::settingInt("sys.tz_min", 420); }
const char* tzName(int m) {
  for (const auto& z : ZONES)
    if (z.min == m) return z.name;
  return "GMT+7";
}

void applyTz(int m) {
  // POSIX TZ has the sign the other way round (UTC+7 is "<+07>-7"), as desk.cpp sets it after a sync.
  char tz[32];
  const int a = abs(m);
  snprintf(tz, sizeof tz, "<%c%02d%02d>%c%d:%02d", m >= 0 ? '+' : '-', a / 60, a % 60, m >= 0 ? '-' : '+', a / 60, a % 60);
  setenv("TZ", tz, 1);
  tzset();
}

lv_obj_t* tdBody = nullptr;
void fillTimeData();

void openTz() {
  lv_obj_t* body;
  lv_obj_t* s = os::screen(N_("Time zone"), &body);
  const int now = tzNow();
  for (const auto& z : ZONES) {
    lv_obj_t* r = box(body, pal().cell, 12);
    lv_obj_set_size(r, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(r, 52, 0);
    lv_obj_set_style_pad_hor(r, 14, 0);
    lv_obj_set_style_pad_ver(r, 7, 0);
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(r, C(pal().press), LV_STATE_PRESSED);
    if (z.min == now) {
      lv_obj_set_style_border_width(r, 2, 0);
      lv_obj_set_style_border_color(r, C(col::IOS_BLUE), 0);
      lv_obj_align(glyph(r, &gl_check_16, col::IOS_BLUE), LV_ALIGN_RIGHT_MID, 0, 0);
    }
    label(r, z.name, 15, true, pal().fg);
    lv_obj_t* p = label(r, "", 12, false, pal().faint);
    wrapText(p, z.places, os::W() - 24 - 28 - 24, 12, false, 1);
    lv_obj_set_pos(p, 0, 20);
    lv_obj_add_event_cb(r, [](lv_event_t* e) {
      const int m = (int)(intptr_t)lv_event_get_user_data(e);
      desk::setSetting("sys.tz_min", String(m));
      desk::syncNow();
      applyTz(m);
      os::toast(i18n::fmt(N_("Time zone {0}"), tzName(m)).c_str());
      os::back();
      lv_async_call([](void*) { fillTimeData(); }, nullptr);
    }, LV_EVENT_CLICKED, (void*)(intptr_t)z.min);
  }
  hint(body, N_("The device's clock, the lunar calendar and the hourly weather follow this time zone."));
  os::open(s);
}

// Night from / until (dash.quiet_from, dash.quiet_to): two rollers like the alarm's.
const char* quietKey = "dash.quiet_from";
lv_obj_t *qh, *qm;
lv_obj_t* roller(lv_obj_t* parent, int n, int sel) {
  String opts;
  for (int i = 0; i < n; i++) opts += (i ? "\n" : "") + String(i < 10 ? "0" : "") + i;
  lv_obj_t* r = lv_roller_create(parent);
  lv_roller_set_options(r, opts.c_str(), LV_ROLLER_MODE_INFINITE);
  lv_roller_set_visible_row_count(r, 3);
  lv_roller_set_selected(r, sel, LV_ANIM_OFF);
  lv_obj_set_width(r, 96);
  lv_obj_set_style_text_font(r, fonts::get(34, true), 0);
  lv_obj_set_style_text_font(r, fonts::get(34, true), LV_PART_SELECTED);
  lv_obj_set_style_bg_color(r, C(pal().bg), 0);
  lv_obj_set_style_border_width(r, 0, 0);
  lv_obj_set_style_text_color(r, C(pal().faint), 0);
  lv_obj_set_style_text_color(r, C(pal().fg), LV_PART_SELECTED);
  lv_obj_set_style_bg_color(r, C(pal().cell), LV_PART_SELECTED);
  lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_PART_SELECTED);
  lv_obj_set_style_radius(r, 12, LV_PART_SELECTED);
  lv_obj_set_style_text_align(r, LV_TEXT_ALIGN_CENTER, 0);
  return r;
}

void openQuiet(bool from) {
  quietKey = from ? "dash.quiet_from" : "dash.quiet_to";
  const String v = desk::setting(quietKey, from ? "23:00" : "06:00");
  lv_obj_t* body;
  lv_obj_t* s = os::screen(from ? N_("Night from") : N_("Night until"), &body, os::NOSCROLL);
  os::navAction(s, N_("Save"), [](lv_event_t*) {
    desk::setSetting(quietKey, hhmm(lv_roller_get_selected(qh), lv_roller_get_selected(qm)));
    desk::syncNow();
    os::back();
    lv_async_call([](void*) { fillTimeData(); }, nullptr);
  });
  lv_obj_t* row = clear(body);
  lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_top(row, 24, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 8, 0);
  qh = roller(row, 24, constrain(v.toInt(), 0, 23));
  label(row, ":", 34, true, pal().fg);
  qm = roller(row, 60, constrain(v.substring(v.indexOf(':') + 1).toInt(), 0, 59));
  os::open(s);
}

void fillTimeData() {
  if (!tdBody) return;
  lv_obj_clean(tdBody);
  const int tz = tzNow();
  const String zone = String(tzName(tz)) + (tz == 420 ? String(" · ") + T_("Vietnam") : String(""));
  listRow(tdBody, N_("Time zone"), zone.c_str(), [](lv_event_t*) { openTz(); });
  caption(tdBody, N_("Fetch new data every"));
  static const char* const N[] = {"5", "15", "30", N_("60 min")};
  const int m = desk::settingInt("dash.interval_min", 30);
  chips(tdBody, N, 4, m <= 5 ? 0 : m <= 15 ? 1 : m <= 30 ? 2 : 3, [](int i) {
    static const char* const V[] = {"5", "15", "30", "60"};
    desk::setSetting("dash.interval_min", V[i]);
    desk::syncNow();
  });
  hint(tdBody, N_("Weather, calendar, prices, news."));
  caption(tdBody, N_("Night"));
  listRow(tdBody, N_("From"), desk::setting("dash.quiet_from", "23:00").c_str(), [](lv_event_t*) { openQuiet(true); });
  listRow(tdBody, N_("Until"), desk::setting("dash.quiet_to", "06:00").c_str(), [](lv_event_t*) { openQuiet(false); });
  hint(tdBody, N_("In these hours the screen saver shows its night page and notifications stay silent."));
}

void openTimeData() {
  lv_obj_t* s = os::screen(N_("Time and data"), &tdBody);
  lv_obj_add_event_cb(s, [](lv_event_t* e) {
    if (lv_event_get_current_target(e) == lv_event_get_target(e)) tdBody = nullptr;
  }, LV_EVENT_DELETE, nullptr);
  fillTimeData();
  os::open(s);
}

// ------------------------------------------------------------------ the device's pictures (s3.photos)
// Picked on the web (the browser renders them at the screen sizes); here they can be taken off.
lv_obj_t* phBody = nullptr;
int phArmed = -1;
void fillPhotos();

lv_obj_t* thumb(lv_obj_t* parent, int id, int w, int h) {
  lv_obj_t* c = box(parent, pal().cell2, 10);
  lv_obj_set_size(c, w, h);
  lv_obj_set_style_clip_corner(c, true, 0);
  if (!desksaver::photoImg(c, id, w, h)) lv_obj_center(label(c, N_("Loading"), 12, false, pal().faint));
  return c;
}

void fillPhotos() {
  if (!phBody) return;
  lv_obj_clean(phBody);
  const auto ids = data::codesOf(desk::setting("s3.photos", ""), "");
  if (ids.empty()) hint(phBody, N_("No photos on the device yet."));
  lv_obj_t* grid = clear(phBody);
  lv_obj_set_size(grid, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_gap(grid, 6, 0);
  const int per = os::landscape() ? 4 : 3;
  const int w = (os::W() - 24 - 6 * (per - 1)) / per, h = os::landscape() ? w * 2 / 3 : w * 3 / 2;
  for (int i = 0; i < (int)ids.size(); i++) {
    lv_obj_t* c = thumb(grid, ids[i].toInt(), w, h);
    lv_obj_t* x = box(c, phArmed == i ? col::RED : 0x000000, 12);
    lv_obj_set_style_bg_opa(x, phArmed == i ? LV_OPA_COVER : 150, 0);
    lv_obj_set_size(x, phArmed == i ? LV_SIZE_CONTENT : 24, 24);
    lv_obj_set_style_pad_hor(x, phArmed == i ? 8 : 0, 0);
    lv_obj_align(x, LV_ALIGN_TOP_RIGHT, -4, 4);
    lv_obj_add_flag(x, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(x, 8);
    if (phArmed == i) lv_obj_center(label(x, N_("Remove photo"), 12, true, 0xffffff));
    else lv_obj_center(glyph(x, &gl_trash_16, 0xffffff));
    lv_obj_add_event_cb(x, [](lv_event_t* e) {
      const int i = (int)(intptr_t)lv_event_get_user_data(e);
      if (phArmed != i) {
        phArmed = i;
      } else {
        auto ids = data::codesOf(desk::setting("s3.photos", ""), "");
        String out;
        for (int k = 0; k < (int)ids.size(); k++)
          if (k != i) out += (out.length() ? "," : "") + ids[k];
        desk::setSetting("s3.photos", out);
        desk::syncNow();
        phArmed = -1;
        os::toast(N_("Photo removed from the device"));
      }
      lv_async_call([](void*) { fillPhotos(); }, nullptr);
    }, LV_EVENT_CLICKED, (void*)(intptr_t)i);
  }
  hint(phBody, N_("Add photos on the EspOS website, Photos (the browser crops them for the screen). The first one is "
                  "the wallpaper when the appearance is Your photo."));
}

void openPhotos() {
  phArmed = -1;
  lv_obj_t* s = os::screen(N_("Photos on the device"), &phBody);
  lv_obj_add_event_cb(s, [](lv_event_t* e) {
    if (lv_event_get_current_target(e) == lv_event_get_target(e)) phBody = nullptr;
  }, LV_EVENT_DELETE, nullptr);
  fillPhotos();
  os::open(s);
}

// A settings row with an app-style icon on the left.
lv_obj_t* iconRow(lv_obj_t* parent, uint32_t color, const lv_image_dsc_t* g, const char* name, const char* value,
                  lv_event_cb_t cb) {
  // Half a line (a TWO list in landscape): the value is laid out below, once (not first squeezed by listRow).
  const bool halfList = lv_obj_has_flag(parent, LV_OBJ_FLAG_USER_1);
  lv_obj_t* r = listRow(parent, name, halfList ? "" : value, cb);
  if (halfList) lv_label_set_text(lv_obj_get_child(r, 1), i18n::trl(value));
  lv_obj_t* ic = box(r, color, 7);
  lv_obj_set_size(ic, 28, 28);
  lv_obj_align(ic, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_t* gi = glyph(ic, g, 0xffffff);
  lv_image_set_src(gi, g);
  lv_obj_center(gi);
  lv_obj_align(lv_obj_get_child(r, 0), LV_ALIGN_LEFT_MID, 40, 0);
  // Long values stay clear of the name; a long name (a longer language) shrinks or wraps beside its value. The row
  // is half the line in a TWO list (landscape): then both may take two lines.
  lv_obj_t* v = lv_obj_get_child(r, 1);
  lv_obj_update_layout(r);
  const int rowW = lv_obj_get_width(r), half = rowW < os::W() / 2;
  const String vt = lv_label_get_text(v);
  if (!half) {
    const int room = rowW - 28 - 40 - textW(name, 15) - 30;
    if (textW(vt.c_str(), 14) > room) fitText(v, vt.c_str(), max(60, room), 36, 14, 12);  // two lines fit the row
    lv_obj_update_layout(r);
    const int nroom = rowW - 28 - 40 - lv_obj_get_width(v) - 40;
    if (textW(name, 15) > nroom) fitText(lv_obj_get_child(r, 0), name, nroom, 40, 15, 12);
    return r;
  }
  // Half a line (landscape): name and value side by side when both fit; else the value goes under the name (as a
  // subtitle; words are never split), the row as tall as they need.
  const char* nt = i18n::trl(name);
  const int inner = rowW - 28 - 40 - 14 - 8;
  if (textW(nt, 15) + textW(vt.c_str(), 14) <= inner) return r;
  lv_obj_t* nl = lv_obj_get_child(r, 0);
  fitText(nl, nt, inner + 8, 38, 15, 12);
  lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_LEFT, 0);
  fitText(v, vt.c_str(), inner + 8, 34, 13, 11);
  lv_obj_update_layout(r);
  const int nh = lv_obj_get_height(nl), vh = lv_obj_get_height(v);
  lv_obj_set_height(r, max(46, 8 + nh + 1 + vh + 8));
  lv_obj_align(nl, LV_ALIGN_TOP_LEFT, 40, 8);
  lv_obj_align(v, LV_ALIGN_TOP_LEFT, 40, 8 + nh + 1);
  return r;
}

// "2.1 MB free" for the Settings row (the internal app area; the card when one is in).
String storageSummary() {
  const pkg::VolInfo f = pkg::volume(pkg::FLASH), c = pkg::volume(pkg::SD);
  const String dev = pkg::sizeText((uint64_t)f.freeKB * 1024);
  if (c.present) return i18n::fmt(N_("{0} free, card {1}"), dev, pkg::sizeText((uint64_t)c.freeKB * 1024));
  return i18n::fmt(N_("{0} free"), dev);
}

void openMain() {
  lv_obj_t* body;
  lv_obj_t* s = os::screen(N_("Settings"), &body, os::ROOT | os::TWO);
  lv_obj_set_style_pad_row(body, 6, 0);
  lv_obj_t* wifiRow_ = iconRow(body, 0x0a84ff, &gl_wifi3_16, N_("WiFi"), wifiSummary().c_str(), [](lv_event_t*) { openWifi(); });
  every(s, 1000, [](lv_timer_t* t) {
    lv_obj_t* r = (lv_obj_t*)lv_timer_get_user_data(t);
    setText(lv_obj_get_child(r, 1), wifiSummary().c_str());
  }, wifiRow_);
  const bool linked = desk::link() == desk::Link::Linked;
  iconRow(body, 0x34c759, &gl_sync_16, N_("EspOS account"),
          linked ? (desk::deviceName().length() ? desk::deviceName().c_str() : N_("linked")) : N_("not linked"),
          [](lv_event_t*) { openLink(); });
  caption(body, "");
  // The language shown, by its own name (Settings › Language: langui.cpp).
  const i18n::Lang* lg = i18n::find(i18n::lang());
  iconRow(body, 0x0a84ff, &gl_globe_16, N_("Language"), String(lg ? lg->name : "English").c_str(),  // its own name
          [](lv_event_t*) { langui::open(); });
  iconRow(body, 0x5e5ce6, &gl_photo_16, N_("Appearance"), !strcmp(theme::kindId(), "color") ? N_("Color") : !strcmp(theme::kindId(), "light") ? N_("Light") : !strcmp(theme::kindId(), "dark") ? N_("Dark") : N_("Photo"), [](lv_event_t*) { openLook(); });
  iconRow(body, 0x64748b, &gl_grid_16, N_("Home Screen"), "", [](lv_event_t*) { openHomeReset(); });
  iconRow(body, 0x0a84ff, &gl_sun_16, N_("Display"), (String(rotName(os::rotation())) + ", " + os::brightness() + "%").c_str(),
          [](lv_event_t*) { openDisplay(); });
  iconRow(body, 0x5e5ce6, &gl_moon_16, N_("Screen saver"),
          saverSecNow() ? i18n::fmt(N_("{0} min"), String(saverSecNow() / 60)).c_str() : N_("off"),
          [](lv_event_t*) { openSaver(); });
  iconRow(body, 0xff3b30, &gl_speaker_16, N_("Sound"), (String(os::volume()) + "%").c_str(), [](lv_event_t*) { openSound(); });
  iconRow(body, 0xff3b30, &gl_bell_16, N_("Notifications"), "", [](lv_event_t*) { notif::openSettings(); });
  caption(body, "");
  iconRow(body, 0x0a84ff, &gl_timer_16, N_("Time and data"), tzName(tzNow()), [](lv_event_t*) { openTimeData(); });
  iconRow(body, 0x3b82f6, &gl_sunrise_16, N_("Weather"), app_weather::cityNow().c_str(), [](lv_event_t*) { app_weather::openCity(); });
  const int cals = app_calendar::linkCount();
  iconRow(body, 0xff3b30, &gl_flag_16, N_("Linked calendars"), cals ? String(cals).c_str() : N_("none"), [](lv_event_t*) { app_calendar::openLinks(); });
  iconRow(body, 0xc2410c, &gl_globe_16, N_("News"), i18n::plural(app_news::sectionCount(), NP_("{n} section|{n} sections")).c_str(),
          [](lv_event_t*) { app_news::openSections(); });
  iconRow(body, 0x16a34a, &gl_list_16, N_("Markets"), desk::setting("s3.markets", "").length() ? N_("custom") : N_("overview"),
          [](lv_event_t*) { app_markets::openCodes(); });
  const int pics = data::codesOf(desk::setting("s3.photos", ""), "").size();
  iconRow(body, 0x34c759, &gl_photo_16, N_("Photos on the device"), pics ? i18n::plural(pics, NP_("{n} photo|{n} photos")).c_str() : N_("none"), [](lv_event_t*) { openPhotos(); });
  caption(body, "");
  iconRow(body, 0x8e8e93, &gl_list_16, N_("Storage"), storageSummary().c_str(), [](lv_event_t*) { pkg::openStorage(); });
  iconRow(body, 0x8e8e93, &gl_gear_16, N_("Update"), desk::fwOffer().version.length() ? N_("new version") : "",
          [](lv_event_t*) { openUpdate(); });
  const String nm = nameNow();
  iconRow(body, 0x8e8e93, &gl_edit_16, N_("Device name"), nm.length() ? nm.c_str() : N_("not set"), [](lv_event_t*) { openName(); });
  iconRow(body, 0x8e8e93, &gl_font_16, N_("Keyboard test"), N_("Vietnamese"), [](lv_event_t*) {
    lv_obj_t* body;
    lv_obj_t* s = os::screen(N_("Keyboard test"), &body, os::NOSCROLL);
    lv_obj_t* ta = field(body, "", false);
    lv_textarea_set_one_line(ta, false);
    lv_obj_set_height(ta, os::H() - os::SB_H - os::NAV_H - kb::height() - 16);
    os::open(s);
    kb::open(ta, kb::Mode::Text, [](lv_obj_t*) { kb::close(); });
  });
  iconRow(body, 0x5856d6, &gl_grid_16, N_("Remote control"), remote::enabled() ? N_("on") : N_("off"),
          [](lv_event_t*) { openRemote(); });
  const String ver = String("S3 · ") + (strrchr(FW_VERSION, '-') + 1);
  iconRow(body, 0x8e8e93, &gl_power_16, N_("About this device"), ver.c_str(), [](lv_event_t*) { openAbout(); });
  os::open(s);
}

}  // namespace

void open(const char* arg) {
  static bool started = false;
  if (!started) prefs.begin("desk"), started = true;
  const String a = arg ? arg : "";
  if (a == "wifi") openWifi();
  else if (a == "account") openLink();
  else if (a == "look") openLook();
  else {
    // "lang": back on Settings › Language after a language switch rebuilt the screens (langui.cpp).
    // A page found by Search opens over the main list, so back goes to Settings as on a phone.
    openMain();
    if (a == "display") openDisplay();
    else if (a == "saver") openSaver();
    else if (a == "sound") openSound();
    else if (a == "update") openUpdate();
    else if (a == "name") openName();
    else if (a == "about") openAbout();
    else if (a == "remote") openRemote();
    else if (a == "home") openHomeReset();
    else if (a == "notif") notif::openSettings();
    else if (a == "storage") pkg::openStorage();
    else if (a == "time") openTimeData();
    else if (a == "photos") openPhotos();
    else if (a == "city") app_weather::openCity();
    else if (a == "calendars") app_calendar::openLinks();
    else if (a == "news") app_news::openSections();
    else if (a == "markets") app_markets::openCodes();
    else if (a == "lang") langui::open();
  }
}

}  // namespace app_settings
