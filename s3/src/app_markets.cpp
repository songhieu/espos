#pragma GCC optimize("Os")  // a cold screen: built small (the firmware must fit 3.75 MB)
// Markets: gold, stock indices, exchange rates and coins from the server's 15-minute cache.
#include <Arduino.h>

#include <ctime>

#include "apps.h"
#include "appopts.h"
#include "data.h"
#include "desk.h"
#include "os.h"
#include "uikit.h"
#include "widopts.h"

namespace app_markets {
namespace {

using namespace kit;

lv_obj_t *list, *scr_;
int tab = 0;

const char* const TABS[] = {N_("All"), N_("Gold"), N_("Stocks"), N_("Rates"), N_("Crypto")};  // i18n: Rates: exchange rates (a short tab)
const char* const KINDS[] = {"", "gold", "stocks", "fx", "crypto"};
const char* const HEADS[] = {"", N_("Gold · million dong/tael"), N_("Stocks · points"), N_("Vietcombank rates · selling"),
                             N_("Crypto · USD")};

int rowW = 296;  // the whole line, or half of it in landscape (twoCols)

void row(lv_obj_t* parent, const data::Quote& q, const char* kind) {
  lv_obj_t* r = box(parent, pal().cell, 14);
  lv_obj_set_size(r, rowW, 58);
  lv_obj_set_style_pad_hor(r, 14, 0);
  String name = q.name;
  if (!strcmp(kind, "gold") && q.code == "N24K") name = T_("Ring 24K");  // i18n: 24-carat ring gold
  lv_obj_t* a = label(r, "", 16, true, pal().fg);
  fitText(a, name.c_str(), (rowW + 24) / 2 - 20, 22, 16, 13, true);
  lv_obj_align(a, LV_ALIGN_TOP_LEFT, 0, 9);
  lv_obj_t* u = label(r, q.unit.c_str(), 12, false, pal().faint);
  lv_obj_align(u, LV_ALIGN_BOTTOM_LEFT, 0, -9);
  lv_obj_t* v = label(r, q.value.c_str(), 16, true, pal().fg);
  lv_obj_align(v, LV_ALIGN_TOP_RIGHT, 0, 9);
  if (q.change.length()) {
    lv_obj_t* c = box(r, data::trendColor(q.pct, false), 6);
    lv_obj_set_size(c, LV_SIZE_CONTENT, 20);
    lv_obj_set_style_pad_hor(c, 6, 0);
    lv_obj_t* cl = label(c, q.change.c_str(), 12, true, 0xffffff);
    lv_obj_center(cl);
    lv_obj_align(c, LV_ALIGN_BOTTOM_RIGHT, 0, -7);
  }
}

// The codes the app follows (s3.markets, "gold:SJC,stocks:FPT"), of one kind: "SJC,FPT"; "" = every one the
// server sends.
String mineOf(const char* kind) {
  String out;
  const String pre = String(kind) + ":";
  for (const String& t : data::codesOf(desk::setting("s3.markets", ""), ""))
    if (t.startsWith(pre)) out += (out.length() ? "," : "") + t.substring(pre.length());
  return out;
}

void fill() {
  if (!list) return;
  lv_obj_clean(list);
  rowW = twoCols(list);  // landscape: two a line
  const bool mine = desk::setting("s3.markets", "").length() > 0;
  int shown = 0, wanted = 0;
  for (int k = 1; k <= 4; k++) {
    if (tab && tab != k) continue;
    const String codes = mineOf(KINDS[k]);
    if (mine && !codes.length()) continue;
    const auto qs = data::quotes(KINDS[k], codes);
    wanted += data::codesOf(codes, "").size();
    if (qs.empty()) continue;
    caption(list, HEADS[k]);
    for (const auto& q : qs) row(list, q, KINDS[k]), shown++;
  }
  if (mine && wanted > shown) hint(list, N_("New codes get prices after the next sync."));
  if (!shown && !mine)
    hint(list, desk::dash().isNull() ? N_("Link to EspOS to get prices.") : N_("No prices yet; waiting for the next sync."));
}

// Followed codes: the same picker as a markets widget's options.
lv_obj_t* codesBody = nullptr;
void fillCodes() {
  if (!codesBody) return;
  lv_obj_clean(codesBody);
  const widopts::Look look = {pal().fg, pal().faint, pal().chip, pal().chipFg, col::IOS_BLUE, 0xffffff};
  widopts::codes(codesBody, desk::setting("s3.markets", ""), look, os::W() - 24, [](const String& v) {
    desk::setSetting("s3.markets", v);
    desk::syncNow();
    lv_async_call([](void*) {
      fillCodes();
      fill();
    }, nullptr);
  }, 20);
  hint(codesBody, N_("Markets shows only these codes; Mixed is every code the server sends. Prices update every 15 minutes."));
}

}  // namespace

void openCodes() {
  lv_obj_t* s = os::screen(N_("Followed codes"), &codesBody);
  lv_obj_add_event_cb(s, [](lv_event_t* e) {
    if (lv_event_get_current_target(e) == lv_event_get_target(e)) codesBody = nullptr;
  }, LV_EVENT_DELETE, nullptr);
  fillCodes();
  os::open(s);
}

void open(const char* arg) {
  lv_obj_t* body;
  scr_ = os::screen(N_("Markets"), &body, os::ROOT);
  os::navAction(scr_, N_("Edit"), [](lv_event_t*) { openCodes(); });
  tab = 0;
  if (arg)
    for (int i = 1; i < 5; i++)
      if (!strcmp(arg, KINDS[i])) tab = i;
  chips(body, TABS, 5, tab, [](int i) {
    tab = i;
    fill();
  });
  list = stack(body, false, 8);
  lv_obj_add_event_cb(scr_, [](lv_event_t* e) {
    if (lv_event_get_current_target(e) == scr_) list = scr_ = nullptr;  // only this screen's own pointers
  }, LV_EVENT_DELETE, nullptr);
  fill();
  String h = T_("Prices update every 15 minutes through the EspOS server (gold from PNJ, VPS, Vietcombank, CoinGecko).");
  if (desk::lastSyncMs()) {
    const time_t t = desk::lastSyncMs() / 1000;
    tm l;
    localtime_r(&t, &l);
    h += " " + i18n::fmt(N_("Last sync {0}."), hhmm(l.tm_hour, l.tm_min));
  }
  hint(body, h.c_str());
  os::open(scr_);
}

}  // namespace app_markets
