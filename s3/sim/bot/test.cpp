// EspOS Bot's host tests: the settings, the alarms and night hours (src/bot/logic), the word wrap (never a letter
// lost), the QR sizes and the date lines. sim/bot/run.sh builds and runs them before the pictures.
#include <stdio.h>
#include <string.h>

#include <string>

#include "../../src/bot/gfx.h"
#include "../../src/bot/logic.h"
#include "../../src/bot/ui.h"

using namespace bot;
static int fails = 0, checks = 0;
#define CHECK(c)                                                  \
  do {                                                            \
    checks++;                                                     \
    if (!(c)) fails++, printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); \
  } while (0)

static ui::Now at(int y, int mo, int d, int wd, int h, int m) {
  ui::Now n;
  n.valid = true, n.year = y, n.mon = mo, n.day = d, n.wday = wd, n.hour = h, n.min = m;
  return n;
}

int main() {
  // styles and screens
  CHECK(logic::lookOf("eyes") == 0 && logic::lookOf("pixel") == 1 && logic::lookOf("line") == 2);
  CHECK(logic::lookOf("neo_eyes") == 0 && logic::lookOf("") == 0);
  auto sc = logic::screensOf(logic::get({}, "bot.screens"));
  CHECK(sc.size() == 5 && !strcmp(ui::screenId(sc[0].id), "clock_bold") && sc[0].secs == 20 &&
        sc[2].id == ui::SCR_WEATHER_AUTO && !strcmp(ui::screenId(sc[4].id), "aquarium") && sc[4].secs == 15);
  sc = logic::screensOf(R"([{"id":"fireworks","s":10},{"id":"weather","s":6},{"id":"eyes","s":2},{"id":"gold","s":9}])");
  CHECK(sc.size() == 2 && sc[0].id == ui::SCR_WEATHER_AUTO && sc[0].secs == 6 && !strcmp(ui::screenId(sc[1].id), "gold"));
  sc = logic::screensOf(R"([{"id":"clock","s":10},{"id":"date","s":6},{"id":"dog","s":5}])");  // the first firmware's ids
  CHECK(sc.size() == 3 && !strcmp(ui::screenId(sc[0].id), "clock_bold") && !strcmp(ui::screenId(sc[1].id), "date_page") &&
        !strcmp(ui::screenId(sc[2].id), "eyes"));
  for (const char* id : {"clock_bold", "clock_thin", "clock_7seg", "clock_dots", "clock_analog", "clock_flip", "date_page",
                         "month", "tet", "weather_sun", "weather_rain", "night", "kennel_night", "butterfly", "newspaper",
                         "coffee", "window_rain", "aquarium", "eyes", "gold", "proverb"})
    CHECK(ui::screenIndex(id) >= 0);  // admin-users' 21 ids
  CHECK(logic::screensOf("not json").empty());
  CHECK(logic::intOf("60", 1, 5, 100) == 60 && logic::intOf("150", 1, 5, 100) == 100 && logic::intOf("x", 7, 0, 9) == 7);
  // night hours
  auto n = logic::nightOf("22:00-07:00");
  CHECK(n.on && logic::inNight(n, 23 * 60) && logic::inNight(n, 6 * 60 + 59) && !logic::inNight(n, 7 * 60) &&
        !logic::inNight(n, 12 * 60) && logic::inNight(n, 22 * 60));
  n = logic::nightOf("13:00-14:30");
  CHECK(n.on && logic::inNight(n, 13 * 60 + 5) && !logic::inNight(n, 14 * 60 + 30));
  CHECK(!logic::nightOf("").on && !logic::nightOf("25:00-06:00").on && !logic::nightOf("07:00-07:00").on);
  // alarms
  auto al = logic::alarmsOf(
      R"([{"h":6,"m":30,"days":"12345","on":1,"label":"Dậy"},{"h":9,"m":0,"days":"","on":1,"label":"Gọi mẹ","date":"2026-11-25"},)"
      R"({"h":21,"m":47,"days":"","on":1,"label":""},{"h":7,"m":0,"days":"06","on":0,"label":"off"},{"h":30,"m":0}])");
  CHECK(al.size() == 4);
  CHECK(logic::due(al, at(2026, 11, 20, 5, 6, 30)) == 0);   // Friday
  CHECK(logic::due(al, at(2026, 11, 22, 0, 6, 30)) == -1);  // Sunday: not in 12345
  CHECK(logic::due(al, at(2026, 11, 25, 3, 9, 0)) == 1);    // the reminder's day
  CHECK(logic::due(al, at(2026, 11, 26, 4, 9, 0)) == -1);   // not another day
  CHECK(logic::due(al, at(2026, 11, 20, 5, 21, 47)) == 2);  // once
  CHECK(logic::due(al, at(2026, 11, 22, 0, 7, 0)) == -1);   // off
  CHECK(logic::due(al, ui::Now()) == -1);
  al[2].on = false;
  const std::string back = logic::alarmsJson(al);
  auto al2 = logic::alarmsOf(back);
  CHECK(al2.size() == 4 && !al2[2].on && al2[1].y == 2026 && al2[1].d == 25 && al2[0].days == 0x3E && al2[0].label == "Dậy");
  // local time
  auto lt = logic::localTime(1795154400LL, 420);  // 2026-11-20 06:00 UTC = 13:00 in Hanoi
  CHECK(lt.valid && lt.year == 2026 && lt.mon == 11 && lt.day == 20 && lt.hour == 13 && lt.min == 0 && lt.wday == 5);
  CHECK(!logic::localTime(0, 420).valid);
  // word wrap: no letter lost, no line too wide
  const char* texts[] = {"Hà Nội hôm nay nắng nhẹ, 28 độ. Chiều có mưa rào, nhớ mang áo mưa nha!",
                         "Supercalifragilisticexpialidocious is a long word", "a\nb  c", "", "Đ"};
  for (const char* t : texts)
    for (int w : {40, 72, 124}) {
      auto ls = gfx::wrap(gfx::F11, t, w);
      std::string all, src;
      for (auto& l : ls) {
        CHECK(gfx::textW(gfx::F11, l) <= w || l.size() <= 4);
        all += l;
      }
      for (const char* p = t; *p; p++)
        if (*p != ' ' && *p != '\n') src += *p;
      std::string flat;
      for (char c : all)
        if (c != ' ') flat += c;
      CHECK(flat == src);
    }
  // QR codes: a claim url of 53 characters fits the panel at 2 px a module
  CHECK(gfx::qrSize(std::string("https://espos.dev/c/") + std::string(33, 'k'), 2) <= 64);
  CHECK(gfx::qrSize("WIFI:T:nopass;S:EspOS-7E38;;", 2) <= 64);
  CHECK(gfx::qrSize("WIFI:T:WPA;S:EspOS-7E38;P:48151623;;", 2) <= 64);  // the setup WiFi with its password
  CHECK(gfx::qrSize("https://espos.dev/app#claim=B0TC2345", 2) <= 64);  // admin-users' claim_url (37 characters)
  CHECK(gfx::qrSize(std::string(200, 'x'), 1) == 0);  // too long for version 6: none (the words alone)
  CHECK(gfx::upper("thứ sáu, đêm trăng ơi ưng ý ạ") == "THỨ SÁU, ĐÊM TRĂNG ƠI ƯNG Ý Ạ");
  // the coffee greeting follows the local hour; the calendar page gives way to coffee only 06:00-09:00 local
  CHECK(ui::greeting(8, true) == "Chào buổi sáng" && ui::greeting(11, true) == "Chào buổi trưa" &&
        ui::greeting(16, true) == "Chào buổi chiều" && ui::greeting(19, true) == "Chào buổi tối" &&
        ui::greeting(23, true) == "Chúc ngủ ngon" && ui::greeting(4, true) == "Chúc ngủ ngon" &&
        ui::greeting(5, true) == "Chào buổi sáng" && ui::greeting(10, true) == "Chào buổi trưa");
  {
    const auto local = [](int64_t utc, int h, int m) { return logic::localTime(utc + (int64_t)(h * 3600 + m * 60), 420); };
    const int64_t day = 1795107600LL;  // 2026-11-20 00:00 in Hanoi (+7), as UTC seconds
    ui::setScreens({{ui::screenIndex("date_page"), 60}});
    const struct {
      int h, m;
      const char* want;
    } T[] = {{8, 0, "coffee"}, {16, 50, "date_page"}, {23, 0, "date_page"}, {6, 0, "coffee"}, {9, 0, "date_page"}};
    uint32_t ms = 2000000;
    for (const auto& t : T) {
      const ui::Now n = local(day, t.h, t.m);
      CHECK(n.hour == t.h && n.min == t.m);
      ui::setNow(n);
      ui::night();
      ui::tick(ms += 100);
      ui::standby();
      ui::tick(ms += 100);
      CHECK(!strcmp(ui::standbyId(), t.want));
    }
  }
  // dates fit the panel in both languages, every weekday and month
  for (int lg = 0; lg < 2; lg++) {
    ui::setLang(lg ? ui::Lang::VI : ui::Lang::EN);
    ui::tick(1);
    for (int wd = 0; wd < 7; wd++)
      for (int mo = 1; mo <= 12; mo++) CHECK(gfx::textW(gfx::F11, ui::dateLine(at(2026, mo, 28, wd, 1, 1), 124)) <= 124);
  }
  printf("%d checks, %d failed\n", checks, fails);
  return fails ? 1 : 0;
}
