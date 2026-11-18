#pragma GCC optimize("Os")  // a cold screen: built small (the firmware must fit 3.75 MB)
// Weather (Thời tiết): now, by the hour, the next days, air quality, UV and humidity, on a sky that
// follows the weather and the time of day.
#include <Arduino.h>
#include <ArduinoJson.h>

#include "apps.h"
#include "appopts.h"
#include "art.h"
#include "data.h"
#include "desk.h"
#include "fonts.h"
#include "keyboard.h"
#include "os.h"
#include "search.h"
#include "uikit.h"

namespace app_weather {
namespace {

using namespace kit;

// ---------------------------------------------------------------- the city (dash.city, dash.lat, dash.lon)
// The web's list (web/app/src/places.ts CITY_GROUPS, same names and coordinates so both sides show the same
// choice) and the world clock's cities. No server route searches places, so this list is what the device offers.
struct Place {
  const char *name, *lat, *lon;
  uint8_t region;
};
const char* const REGIONS[] = {N_("Northern Vietnam"), N_("Central Vietnam & Highlands"), N_("Southern Vietnam"),
                               N_("World")};
const Place PLACES[] = {
    {"Hà Nội", "21.0285", "105.8542", 0}, {"Hải Phòng", "20.8449", "106.6881", 0}, {"Hạ Long", "20.9599", "107.0425", 0},
    {"Bắc Ninh", "21.1861", "106.0763", 0}, {"Bắc Giang", "21.2731", "106.1946", 0}, {"Thái Nguyên", "21.5942", "105.8482", 0},
    {"Việt Trì", "21.3227", "105.402", 0}, {"Vĩnh Yên", "21.3089", "105.6049", 0}, {"Hải Dương", "20.9373", "106.3146", 0},
    {"Hưng Yên", "20.6464", "106.0511", 0}, {"Thái Bình", "20.4463", "106.3366", 0}, {"Nam Định", "20.42", "106.1683", 0},
    {"Ninh Bình", "20.2506", "105.9745", 0}, {"Phủ Lý", "20.5411", "105.9139", 0}, {"Hòa Bình", "20.8171", "105.3376", 0},
    {"Mộc Châu", "20.8456", "104.6416", 0}, {"Sơn La", "21.3256", "103.9188", 0}, {"Điện Biên Phủ", "21.386", "103.023", 0},
    {"Lai Châu", "22.3964", "103.4582", 0}, {"Lào Cai", "22.4856", "103.9707", 0}, {"Sa Pa", "22.3364", "103.8438", 0},
    {"Yên Bái", "21.7229", "104.9113", 0}, {"Hà Giang", "22.8233", "104.9836", 0}, {"Tuyên Quang", "21.8236", "105.214", 0},
    {"Cao Bằng", "22.6657", "106.2522", 0}, {"Lạng Sơn", "21.8537", "106.7615", 0}, {"Bắc Kạn", "22.147", "105.8348", 0},
    {"Thanh Hóa", "19.8067", "105.7852", 1}, {"Vinh", "18.6796", "105.6813", 1}, {"Hà Tĩnh", "18.3428", "105.9057", 1},
    {"Đồng Hới", "17.4689", "106.6223", 1}, {"Đông Hà", "16.8163", "107.1003", 1}, {"Huế", "16.4637", "107.5909", 1},
    {"Đà Nẵng", "16.0544", "108.2022", 1}, {"Hội An", "15.8801", "108.338", 1}, {"Tam Kỳ", "15.5736", "108.474", 1},
    {"Quảng Ngãi", "15.1205", "108.7923", 1}, {"Quy Nhơn", "13.782", "109.2196", 1}, {"Tuy Hòa", "13.0882", "109.0929", 1},
    {"Nha Trang", "12.2388", "109.1967", 1}, {"Phan Rang", "11.564", "108.9886", 1}, {"Phan Thiết", "10.9289", "108.1021", 1},
    {"Kon Tum", "14.3497", "108.0005", 1}, {"Pleiku", "13.9833", "108", 1}, {"Buôn Ma Thuột", "12.6667", "108.05", 1},
    {"Gia Nghĩa", "11.981", "107.69", 1}, {"Đà Lạt", "11.9404", "108.4583", 1},
    {"TP Hồ Chí Minh", "10.7769", "106.7009", 2}, {"Thủ Dầu Một", "10.9804", "106.6519", 2}, {"Biên Hòa", "10.9574", "106.8426", 2},
    {"Vũng Tàu", "10.346", "107.0843", 2}, {"Đồng Xoài", "11.5349", "106.8832", 2}, {"Tây Ninh", "11.31", "106.0983", 2},
    {"Tân An", "10.5359", "106.4137", 2}, {"Mỹ Tho", "10.36", "106.36", 2}, {"Bến Tre", "10.2434", "106.3756", 2},
    {"Vĩnh Long", "10.2537", "105.9722", 2}, {"Trà Vinh", "9.9347", "106.3453", 2}, {"Cao Lãnh", "10.46", "105.6329", 2},
    {"Long Xuyên", "10.3864", "105.4352", 2}, {"Châu Đốc", "10.7", "105.1167", 2}, {"Rạch Giá", "10.0125", "105.0809", 2},
    {"Phú Quốc", "10.2899", "103.984", 2}, {"Cần Thơ", "10.0452", "105.7469", 2}, {"Vị Thanh", "9.7845", "105.4701", 2},
    {"Sóc Trăng", "9.6025", "105.9739", 2}, {"Bạc Liêu", "9.2941", "105.7278", 2}, {"Cà Mau", "9.1769", "105.1524", 2},
    {"Côn Đảo", "8.6833", "106.6", 2},
    {"Tokyo", "35.6762", "139.6503", 3}, {"Seoul", "37.5665", "126.978", 3}, {"Bắc Kinh", "39.9042", "116.4074", 3},
    {"Singapore", "1.3521", "103.8198", 3}, {"Bangkok", "13.7563", "100.5018", 3}, {"Sydney", "-33.8688", "151.2093", 3},
    {"Dubai", "25.2048", "55.2708", 3}, {"Moskva", "55.7558", "37.6173", 3}, {"Paris", "48.8566", "2.3522", 3},
    {"Berlin", "52.52", "13.405", 3}, {"London", "51.5074", "-0.1278", 3}, {"New York", "40.7128", "-74.006", 3},
    {"Los Angeles", "34.0522", "-118.2437", 3},
};

struct CityState {
  lv_obj_t *scr, *list, *field;
} CS;

void pickCity(lv_event_t* e) {
  const Place& p = PLACES[(int)(intptr_t)lv_event_get_user_data(e)];
  desk::setSetting("dash.city", p.name);
  desk::setSetting("dash.lat", p.lat);
  desk::setSetting("dash.lon", p.lon);
  desk::syncNow();
  kb::close();
  os::toast(i18n::fmt(N_("Chose {0}"), p.name).c_str());
  os::back();
}

void cityRow(lv_obj_t* parent, int i, bool on, const char* right) {
  lv_obj_t* r = listRow(parent, PLACES[i].name, right, pickCity, (void*)(intptr_t)i, false);
  if (!on) return;
  lv_obj_set_style_border_width(r, 2, 0);
  lv_obj_set_style_border_color(r, C(col::IOS_BLUE), 0);
  lv_obj_t* ck = glyph(r, &gl_check_16, col::IOS_BLUE);
  lv_obj_align(ck, LV_ALIGN_RIGHT_MID, 0, 0);
}

void fillCities() {
  if (!CS.list) return;
  lv_obj_clean(CS.list);
  const String q = search::fold(lv_textarea_get_text(CS.field));
  const String now = cityNow();
  const int n = sizeof PLACES / sizeof PLACES[0];
  int shown = 0, region = -1;
  if (!q.length())
    for (int i = 0; i < n; i++)
      if (now == PLACES[i].name) caption(CS.list, N_("Selected")), cityRow(CS.list, i, true, "");
  for (int i = 0; i < n; i++) {
    const Place& p = PLACES[i];
    if (q.length() && search::fold(p.name).indexOf(q) < 0) continue;
    if (!q.length() && p.region != region) caption(CS.list, REGIONS[region = p.region]);
    cityRow(CS.list, i, now == p.name, q.length() ? REGIONS[p.region] : "");
    shown++;
  }
  if (!shown) hint(CS.list, N_("This place is not in the device's list. The EspOS web can find any place."));
}

}  // namespace

String cityNow() {
  const String c = desk::setting("dash.city", "");
  return c.length() ? c : String(data::weather()["city"] | "");
}

void openCity() {
  CS = {};
  lv_obj_t* body;
  CS.scr = os::screen(N_("City"), &body);
  CS.field = lv_textarea_create(body);
  lv_obj_set_width(CS.field, lv_pct(100));
  lv_textarea_set_one_line(CS.field, true);
  lv_textarea_set_max_length(CS.field, 30);
  lv_textarea_set_placeholder_text(CS.field, T_("Search cities"));
  lv_obj_set_style_text_font(CS.field, fonts::get(16), 0);
  lv_obj_set_style_text_color(CS.field, C(pal().fg), 0);
  lv_obj_set_style_text_color(CS.field, C(pal().faint), LV_PART_TEXTAREA_PLACEHOLDER);
  lv_obj_set_style_bg_color(CS.field, C(pal().cell), 0);
  lv_obj_set_style_border_width(CS.field, 0, 0);
  lv_obj_set_style_border_color(CS.field, C(col::IOS_BLUE), LV_STATE_FOCUSED);
  lv_obj_set_style_border_width(CS.field, 2, LV_STATE_FOCUSED);
  lv_obj_set_style_radius(CS.field, 12, 0);
  lv_obj_set_style_pad_all(CS.field, 10, 0);
  lv_obj_add_event_cb(CS.field, [](lv_event_t*) {
    kb::open(CS.field, kb::Mode::Plain, [](lv_obj_t*) { kb::close(); });
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(CS.field, [](lv_event_t*) { fillCities(); }, LV_EVENT_VALUE_CHANGED, nullptr);
  CS.list = stack(body, false, 6);
  lv_obj_set_width(CS.list, lv_pct(100));
  lv_obj_add_event_cb(CS.scr, [](lv_event_t* e) {
    if (lv_event_get_current_target(e) == CS.scr) CS = {};
  }, LV_EVENT_DELETE, nullptr);
  fillCities();
  os::open(CS.scr);
}

namespace {

// Cards are a solid lighter shade of the sky: see-through ones made scrolling crawl (13 a second).
uint32_t cardColor = 0x3a4a9a;

uint32_t lighten(uint32_t a, uint32_t b, int pct) {
  const int r = ((a >> 16) & 255) * (100 - pct) / 100 + ((b >> 16) & 255) * pct / 100;
  const int g = ((a >> 8) & 255) * (100 - pct) / 100 + ((b >> 8) & 255) * pct / 100;
  const int bl = (a & 255) * (100 - pct) / 100 + (b & 255) * pct / 100;
  return (r << 16) | (g << 8) | bl;
}

lv_obj_t* glassCard(lv_obj_t* parent) {
  lv_obj_t* c = box(parent, cardColor, 18);
  lv_obj_set_size(c, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(c, 12, 0);
  return c;
}

const char* aqiText(int a) {  // marked: callers translate
  return a <= 50    ? N_("Good")
         : a <= 100 ? N_("Moderate")
         : a <= 150 ? N_("Poor")
         : a <= 200 ? N_("Unhealthy")
         : a <= 300 ? N_("Very unhealthy")
                    : N_("Hazardous");
}

int paneW = 296;  // the width the cards fill: the screen's (portrait) or the right pane's (landscape)

// A tile: icon and name (marked; wraps onto a second line when the language's word is long), value, note.
lv_obj_t* stat(lv_obj_t* parent, const lv_image_dsc_t* g, const char* name, const String& value, const String& note) {
  lv_obj_t* c = box(parent, cardColor, 16);
  const int w = (paneW - 8) / 2;
  lv_obj_set_size(c, w, LV_SIZE_CONTENT);
  lv_obj_set_style_min_height(c, 86, 0);
  lv_obj_set_style_pad_all(c, 10, 0);
  lv_obj_t* i = glyph(c, g, 0xdfe6ff);
  lv_obj_set_pos(i, 0, 0);
  lv_obj_t* n = label(c, "", 12, true, 0xdfe6ff);
  const int dy = (wrapText(n, upperVi(i18n::trl(name)).c_str(), w - 20 - 22, 12, true, 0, 2) - 1) * lineH(12, true);
  lv_obj_set_pos(n, 22, 2);
  lv_obj_t* v = label(c, value.c_str(), 24, true, 0xffffff);
  lv_obj_set_pos(v, 0, 22 + dy);
  lv_obj_t* t = label(c, "", 12, false, 0xdfe6ff);
  fitText(t, note.c_str(), w - 20, 18, 12, 12);
  lv_obj_set_pos(t, 0, 52 + dy);
  return c;
}

}  // namespace

void open(const char*) {
  lv_obj_t* body;
  lv_obj_t* s = os::screen("", &body, os::BARE);
  JsonObjectConst w = data::weather();
  const bool atNight = data::night();
  uint32_t top, bottom;
  data::sky(w["icon"] | "cloud", atNight, top, bottom);
  grad(s, top, bottom);
  cardColor = lighten(lighten(top, bottom, 50), 0xffffff, 16);
  lv_obj_set_style_pad_hor(body, 12, 0);
  lv_obj_set_style_pad_bottom(body, 24, 0);
  lv_obj_set_style_pad_row(body, 10, 0);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_add_flag(body, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  lv_obj_set_scroll_dir(body, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_OFF);
  // Back.
  lv_obj_t* bk = clear(s);
  lv_obj_set_size(bk, 44, 40);
  lv_obj_set_pos(bk, 0, os::SB_H);
  lv_obj_add_flag(bk, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t* chev = glyph(bk, &gl_back_22, 0xffffff);
  lv_obj_center(chev);
  lv_obj_add_event_cb(bk, [](lv_event_t*) { os::back(); }, LV_EVENT_CLICKED, nullptr);
  // Đổi: the city (dash.city).
  lv_obj_t* ch = box(s, 0xffffff, 14);
  lv_obj_set_style_bg_opa(ch, 56, 0);
  lv_obj_set_size(ch, LV_SIZE_CONTENT, 28);
  lv_obj_set_style_pad_hor(ch, 12, 0);
  lv_obj_align(ch, LV_ALIGN_TOP_RIGHT, -12, os::SB_H + 6);  // as wide as the word
  lv_obj_add_flag(ch, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(ch, 6);
  lv_obj_center(label(ch, N_("Change"), 13, true, 0xffffff));
  lv_obj_add_event_cb(ch, [](lv_event_t*) { openCity(); }, LV_EVENT_CLICKED, nullptr);
  if (!data::hasWeather()) {
    lv_obj_t* t = label(body, N_("Weather"), 22, true, 0xffffff);
    lv_obj_set_style_pad_top(t, 30, 0);
    lv_obj_t* h = label(body, "", 15, false, 0xdfe6ff);
    fitText(h, desk::link() == desk::Link::Linked ? N_("No data yet, waiting for the next sync.") : N_("Link EspOS to get the weather."),
            os::W() - 40, 80, 15, 13, false, 3);
    if (desk::link() == desk::Link::Linked) button(body, N_("Choose a city"), 0xffffff, 0x1360d6, [](lv_event_t*) { openCity(); });
    os::open(s);
    return;
  }
  // Landscape: the city and now on the left (they never scroll under the buttons), hours, days and the tiles
  // scrolling on the right.
  const bool land = os::landscape();
  constexpr int LW = 196;
  lv_obj_t *left = body, *right = body;
  paneW = os::W() - 24;
  if (land) {
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_column(body, 8, 0);
    lv_obj_set_style_pad_bottom(body, 0, 0);
    paneW = os::W() - 24 - LW - 8;
    for (int k = 0; k < 2; k++) {
      lv_obj_t* p = clear(body);
      lv_obj_set_size(p, k ? paneW : LW, os::H() - os::SB_H);
      lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
      lv_obj_set_style_pad_row(p, k ? 10 : 4, 0);
      lv_obj_set_style_pad_top(p, k ? 6 : 40, 0);  // the left one under the back button
      if (k) {
        lv_obj_set_style_pad_bottom(p, 24, 0);
        lv_obj_add_flag(p, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
        lv_obj_set_scroll_dir(p, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(p, LV_SCROLLBAR_MODE_OFF);
      }
      (k ? right : left) = p;
    }
  }
  // Now (the name opens the city list too).
  lv_obj_t* city = label(left, "", 20, true, 0xffffff);
  fitText(city, w["city"] | "", land ? LW - 4 : os::W() - 130, land ? 52 : 28, 20, 15, true);
  lv_obj_set_style_pad_top(city, 6, 0);
  lv_obj_add_flag(city, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(city, 8);
  lv_obj_add_event_cb(city, [](lv_event_t*) { openCity(); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* temp = label(left, (String((int)(w["temp"] | 0)) + "°").c_str(), 84, false, 0xffffff);
  lv_obj_set_style_margin_top(temp, -6, 0);
  // i18n: the server's description, the day's high and low ("Cloudy · H 31° L 26°")
  String line = i18n::fmt(N_("{0} · H {1}° L {2}°"), w["desc"] | "", String((int)(w["max"] | 0)), String((int)(w["min"] | 0)));
  lv_obj_t* d = label(left, "", 15, false, 0xeef2ff);
  fitText(d, line.c_str(), land ? LW - 4 : os::W() - 40, land ? 40 : 22, 15, 12);
  if (!w["feels"].isNull()) label(left, i18n::fmt(N_("Feels like {0}°"), String((int)(w["feels"] | 0))).c_str(), 13, false, 0xdfe6ff);
  if (land) {  // Change under the city's lines instead of at the top right
    lv_obj_set_parent(ch, left);
    lv_obj_set_style_margin_top(ch, 8, 0);
  }
  // Hours.
  JsonArrayConst hours = w["hours"];
  if (hours.size()) {
    lv_obj_t* c = glassCard(right);
    const int n = min<int>(hours.size(), 6);
    const int colW = (paneW - 24) / n;
    lv_obj_set_height(c, 96);
    for (int i = 0; i < n; i++) {
      JsonObjectConst h = hours[i];
      lv_obj_t* col_ = clear(c);
      lv_obj_set_size(col_, colW, 72);
      lv_obj_set_pos(col_, i * colW, 0);
      lv_obj_t* a = label(col_, i == 0 ? N_("Now") : (h["h"] | ""), 12, true, 0xffffff);
      lv_obj_align(a, LV_ALIGN_TOP_MID, 0, 0);
      lv_obj_t* ic = img(col_, data::wxIcon(h["icon"] | "cloud", 28, data::hourIsNight(h["h"] | "12")));
      lv_obj_align(ic, LV_ALIGN_TOP_MID, 0, 18);
      lv_obj_t* b = label(col_, (String((int)(h["t"] | 0)) + "°").c_str(), 14, true, 0xffffff);
      lv_obj_align(b, LV_ALIGN_TOP_MID, 0, 50);
      if (!h["rain"].isNull() && (int)(h["rain"] | 0) >= 30) {
        lv_obj_t* r = label(col_, (String((int)(h["rain"] | 0)) + "%").c_str(), 12, true, 0x93c5fd);
        lv_obj_align(r, LV_ALIGN_TOP_MID, 0, 66);
        lv_obj_set_height(col_, 84);
      }
    }
  }
  // Days.
  JsonArrayConst days = w["days"];
  if (days.size()) {
    lv_obj_t* c = glassCard(right);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c, 4, 0);
    int lo = 99, hi = -99;
    for (JsonObjectConst x : days) lo = min(lo, (int)(x["min"] | 0)), hi = max(hi, (int)(x["max"] | 0));
    const int rw = paneW - 24;
    for (JsonObjectConst x : days) {
      lv_obj_t* r = clear(c);
      lv_obj_set_size(r, rw, 34);
      lv_obj_t* dn = label(r, data::dayName(x).c_str(), 15, true, 0xffffff);
      lv_obj_align(dn, LV_ALIGN_LEFT_MID, 0, 0);
      lv_obj_t* ic = img(r, data::wxIcon(x["icon"] | "cloud", 28));
      lv_obj_align(ic, LV_ALIGN_LEFT_MID, 72, 0);
      const int mn = x["min"] | 0, mx = x["max"] | 0;
      lv_obj_t* a = label(r, (String(mn) + "°").c_str(), 15, false, 0xdfe6ff);
      lv_obj_align(a, LV_ALIGN_LEFT_MID, 108, 0);
      const int bx = 146, bw = rw - bx - 40;
      lv_obj_t* tr = box(r, lighten(cardColor, 0xffffff, 22), 3);
      lv_obj_set_size(tr, bw, 5);
      lv_obj_align(tr, LV_ALIGN_LEFT_MID, bx, 0);
      const int span = max(1, hi - lo);
      lv_obj_t* f = box(r, 0xffffff, 3);
      grad(f, 0x7dd3fc, 0xfbbf24, LV_GRAD_DIR_HOR);
      lv_obj_set_size(f, max(8, (mx - mn) * bw / span), 5);
      lv_obj_align(f, LV_ALIGN_LEFT_MID, bx + (mn - lo) * bw / span, 0);
      lv_obj_t* b = label(r, (String(mx) + "°").c_str(), 15, true, 0xffffff);
      lv_obj_align(b, LV_ALIGN_RIGHT_MID, 0, 0);
    }
  }
  // Air, UV, humidity, wind.
  lv_obj_t* grid = clear(right);
  lv_obj_set_size(grid, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_gap(grid, 8, 0);
  if (!w["aqi"].isNull()) stat(grid, &gl_leaf_16, N_("Air quality"), String((int)(w["aqi"] | 0)), T_(aqiText(w["aqi"] | 0)));
  if (!w["uv"].isNull()) {
    const int uv = w["uv"] | 0;
    stat(grid, &gl_uv_16, N_("UV index"), String(uv),
         T_(uv <= 2 ? N_("Low") : uv <= 5 ? N_("Moderate") : uv <= 7 ? N_("High") : N_("Very high")));
  }
  if (!w["hum"].isNull()) stat(grid, &gl_drop_16, N_("Humidity"), String((int)(w["hum"] | 0)) + "%", "");
  if (!w["wind"].isNull()) stat(grid, &gl_wind_16, N_("Wind"), String((int)(w["wind"] | 0)), "km/h");
  if (!w["sunrise"].isNull())  // i18n: the tile shows the sunrise time, the note when the sun sets
    stat(grid, &gl_sunrise_16, N_("Sun"), String(w["sunrise"] | ""), i18n::fmt(N_("sets {0}"), w["sunset"] | ""));
  if (!w["rain"].isNull()) stat(grid, &gl_drop_16, N_("Chance of rain"), String((int)(w["rain"] | 0)) + "%", T_("today"));
  if (!lv_obj_get_child_count(grid)) lv_obj_delete(grid);
  os::open(s);
}

}  // namespace app_weather
