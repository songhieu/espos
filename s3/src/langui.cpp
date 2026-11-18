#pragma GCC optimize("Os")  // a cold screen: built small (the firmware must fit 3.75 MB)
// Settings › Language (docs/i18n.md): every language, by its own name with its name in the language shown under it;
// a tick on the one shown. One that is on the device switches at once; another downloads first (its font package,
// then the language) on a progress screen, then switches. sys.lang is stored only once the switch is done, so a
// failed download changes nothing; it goes up to the web with the next sync like every setting made here.
#include "langui.h"

#include <lvgl.h>

#include "apps.h"
#include "art.h"
#include "desk.h"
#include "i18n.h"
#include "net.h"
#include "os.h"
#include "pkg/store.h"
#include "uikit.h"

namespace langui {
namespace {

using namespace kit;
namespace st = pkg::store;

// The download size of a language's missing packages (0 when unknown).
uint64_t missingBytes(const char* tag) {
  const i18n::Lang* l = i18n::find(tag);
  if (!l || !strcmp(tag, "en")) return 0;
  uint64_t b = 0;
  const String id = i18n::packId(tag);
  for (const char* x : {l->font, id.c_str()}) {
    if (!x || pkg::find(x)) continue;
    if (const st::Entry* e = st::entry(x)) b += st::partsBytes(*e, st::defaultParts(*e));
  }
  return b;
}

// After a switch: every screen again in the new language, back on this page.
void rebuild(void*) {
  os::closeApps();
  os::rebuild();
  apps::open("settings", "lang");
}

void switchTo(const char* tag) {
  if (!i18n::use(tag)) {
    os::toast(N_("This language could not be loaded"));
    return;
  }
  desk::setSetting("sys.lang", tag);
  desk::syncNow();  // a device edit goes up at once (docs/settings-parity.md)
  lv_async_call(rebuild, nullptr);
}

// ---------------------------------------------------------------- download screen
struct Dl {
  const i18n::Lang* lang;
  lv_obj_t *pct, *bar, *msg, *retry;
  bool started, failed;
};
Dl dl;

void start() {
  String why;
  dl.failed = false;
  if (net::state() != net::State::Connected) {
    why = T_("Connect to WiFi to download languages.");
  } else if (i18n::download(dl.lang->tag, why)) {
    dl.started = true;
    setText(dl.msg, "");
    lv_obj_add_flag(dl.retry, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  dl.failed = true;
  setText(dl.msg, why.c_str());
  lv_obj_remove_flag(dl.retry, LV_OBJ_FLAG_HIDDEN);
}

void poll(lv_timer_t*) {
  if (i18n::installed(dl.lang->tag)) {
    dl.started = false;
    switchTo(dl.lang->tag);
    return;
  }
  const int p = i18n::progress(dl.lang->tag);
  if (p >= 0) {
    lv_bar_set_value(dl.bar, p, LV_ANIM_OFF);
    setText(dl.pct, (String(p) + "%").c_str());
    return;
  }
  if (dl.started && !dl.failed) {  // the jobs ended without the packages: the store says why
    dl.failed = true;
    const st::Status s = st::status();
    setText(dl.msg, s.err.length() ? s.err.c_str() : T_("The download failed."));
    lv_obj_remove_flag(dl.retry, LV_OBJ_FLAG_HIDDEN);
  }
}

void openDownload(const i18n::Lang* l) {
  dl = Dl();
  dl.lang = l;
  lv_obj_t* body;
  lv_obj_t* s = os::screen(String(l->name).c_str(), &body, os::NOSCROLL);
  lv_obj_set_style_pad_top(body, 40, 0);
  lv_obj_set_style_pad_row(body, 10, 0);
  const uint64_t b = missingBytes(l->tag);
  label(body, b ? i18n::fmt(N_("Downloading {0}"), pkg::sizeText(b)).c_str() : N_("Downloading"), 15, true, pal().fg);
  dl.pct = label(body, "0%", 34, true, pal().fg);
  dl.bar = lv_bar_create(body);
  lv_obj_set_size(dl.bar, lv_pct(100), 8);
  lv_bar_set_range(dl.bar, 0, 100);
  lv_obj_set_style_bg_color(dl.bar, C(pal().line), LV_PART_MAIN);
  lv_obj_set_style_bg_color(dl.bar, C(col::IOS_BLUE), LV_PART_INDICATOR);
  dl.msg = label(body, "", 14, false, pal().dim);
  dl.retry = button(body, N_("Try again"), col::IOS_BLUE, 0xffffff, [](lv_event_t*) { start(); });
  hint(body, N_("The language goes to the device and switches by itself when it is in. You can remove it later in "
                "Settings, Storage."));
  every(s, 300, poll);
  os::open(s);
  start();
}

// ---------------------------------------------------------------- the list
lv_obj_t* subs[16];  // each row's second line (sizes once the store's catalog is in)
uint32_t subsCat = 0;

String subText(const i18n::Lang* l) {
  String sub = T_(l->english);
  if (sub == l->name) sub = "";
  const uint64_t b = i18n::installed(l->tag) ? 0 : missingBytes(l->tag);
  if (b) sub += (sub.length() ? " · " : "") + pkg::sizeText(b);
  return sub;
}

void onRow(lv_event_t* e) {
  const i18n::Lang* l = (const i18n::Lang*)lv_event_get_user_data(e);
  if (!strcmp(l->tag, i18n::lang())) return;
  if (i18n::installed(l->tag)) switchTo(l->tag);
  else openDownload(l);
}

}  // namespace

void open() {
  if (!st::loaded()) st::refresh();  // the download sizes
  lv_obj_t* body;
  lv_obj_t* s = os::screen(N_("Language"), &body);
  const Pal& p = pal();
  for (int i = 0; i < i18n::LANG_COUNT; i++) {
    const i18n::Lang* l = &i18n::LANGS[i];
    const bool on = !strcmp(l->tag, i18n::lang());
    lv_obj_t* r = box(body, p.cell, 12);
    lv_obj_set_size(r, lv_pct(100), 54);
    lv_obj_set_style_pad_hor(r, 14, 0);
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(r, C(p.press), LV_STATE_PRESSED);
    lv_obj_add_event_cb(r, onRow, LV_EVENT_CLICKED, (void*)l);
    lv_obj_t* n = label(r, String(l->name).c_str(), 15, false, p.fg);  // a copy: its own name, never translated
    // Under it: its name in the language shown (unless the same), and what it would download.
    const String sub = subText(l);
    subs[i] = nullptr;
    if (sub.length() || !i18n::installed(l->tag)) {  // (a size may come once the catalog is in)
      lv_obj_align(n, LV_ALIGN_TOP_LEFT, 0, 7);
      lv_obj_t* d = keep(&subs[i], label(r, "", 13, false, p.faint));
      fitText(d, sub.c_str(), os::W() - 24 - 28 - 40, 18, 13, 12);
      lv_obj_align(d, LV_ALIGN_BOTTOM_LEFT, 0, -7);
    } else {
      lv_obj_align(n, LV_ALIGN_LEFT_MID, 0, 0);
    }
    if (on) {
      lv_obj_t* ok = glyph(r, &gl_check_22, col::IOS_BLUE);
      lv_obj_align(ok, LV_ALIGN_RIGHT_MID, 0, 0);
    }
  }
  hint(body, N_("English is built in. Other languages download from the store when you choose them; Chinese, Japanese "
                "and Korean bring a font of 1 to 2 MB."));
  // The download sizes once the catalog is in.
  subsCat = st::catalogVersion();
  every(s, 500, [](lv_timer_t*) {
    if (subsCat == st::catalogVersion()) return;
    subsCat = st::catalogVersion();
    for (int i = 0; i < i18n::LANG_COUNT && i < 16; i++)
      if (subs[i]) fitText(subs[i], subText(&i18n::LANGS[i]).c_str(), os::W() - 24 - 28 - 40, 18, 13, 12);
  });
  os::open(s);
}

}  // namespace langui
