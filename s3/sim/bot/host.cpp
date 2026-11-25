// EspOS Bot's screens on the computer: the firmware's own drawing code (src/bot/gfx, ui, art) writes every state as
// 1-bit pictures, which tools/bot/shots.py turns into PNGs and GIFs (tools/bot/out/ui.html).
//   sim/bot/run.sh            builds and runs this, then makes the page
// Each shot: OUT/<name>.frames = a header line "<n> <ms between frames>" then n pictures of 1024 bytes (SSD1306 order).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <functional>
#include <string>
#include <vector>

#include "../../src/bot/art.h"
#include "../../src/bot/gfx.h"
#include "../../src/bot/ui.h"

using namespace bot;

static std::string OUT;
static uint32_t clockMs = 1000000;  // the UI's millis(); moves forward only

static void save(const std::string& name, uint32_t durMs, uint32_t stepMs) {
  std::vector<uint8_t> all;
  int n = 0;
  for (uint32_t t = 0; t <= durMs; t += stepMs, n++) {
    ui::tick(clockMs + t);
    all.insert(all.end(), gfx::fb, gfx::fb + sizeof gfx::fb);
  }
  clockMs += durMs + 10000;
  FILE* f = fopen((OUT + "/" + name + ".frames").c_str(), "wb");
  if (!f) {
    perror(name.c_str());
    exit(1);
  }
  fprintf(f, "%d %u\n", n, stepMs);
  fwrite(all.data(), 1, all.size(), f);
  fclose(f);
}

static ui::Now NOW(int h, int m, int s = 0) {
  ui::Now n;
  n.valid = true, n.year = 2026, n.mon = 11, n.day = 20, n.wday = 5, n.hour = h, n.min = m, n.sec = s;
  n.lunar = "1/10";
  return n;
}

static void begin() { ui::tick(clockMs), clockMs += 100; }  // the setter's start time is the last tick

int main(int argc, char** argv) {
  OUT = argc > 1 ? argv[1] : "out";
  const char* LOOK = "BCD";
  const char* LANG[] = {"en", "vi"};
  const std::string LONG_VI =
      "Hà Nội hôm nay nắng nhẹ, 28 độ. Chiều có mưa rào, nhớ mang áo mưa nha! Tối mát, ngủ ngon lắm đó.";
  const std::string LONG_EN =
      "Hanoi is sunny today, 28 degrees. Showers in the afternoon, so take a raincoat! A cool evening, good for sleep.";
  for (int lk = 0; lk < 3; lk++) {
    for (int lg = 0; lg < 2; lg++) {
      const std::string sfx = std::string("_") + LOOK[lk] + "_" + LANG[lg];
      ui::setLook(lk);
      ui::setLang(lg ? ui::Lang::VI : ui::Lang::EN);
      ui::setNow(NOW(21, 47));
      ui::Weather w;
      w.valid = true, w.t = 28, w.hi = 31, w.lo = 22, w.icon = "cloudsun", w.desc = "Nắng nhẹ";
      w.city = lg ? "Hà Nội" : "Hanoi";
      ui::setWeather(w);
      // the standby screens (art2's 21) come once per language, after the looks (they are the same in each look)
      ui::setWeather(w);
      // the dog
      begin(), ui::listen(), save("listen" + sfx, 3000, 80);
      begin(), ui::think(), save("think" + sfx, 3000, 80);
      begin(), ui::happy(), save("happy" + sfx, 3000, 80);
      begin(), ui::talk(lg ? "Mấy giờ rồi hả? 9 giờ 47 tối rồi đó." : "It's 9:47 in the evening.", ui::Mood::HAPPY);
      save("talk_short" + sfx, 5000, 100);
      begin(), ui::talk(lg ? LONG_VI : LONG_EN, ui::Mood::TALK);
      save("talk_long" + sfx, 16000, 100);
      begin(), ui::notHeard(), save("not_heard" + sfx, 8000, 100);
      begin(), ui::talk(ui::str(ui::S_TRIAL_OVER), ui::Mood::SAD), save("trial_over" + sfx, 9000, 100);
      begin(), ui::alarm("", 6, 30, false), save("alarm" + sfx, 1000, 250);
      begin(), ui::alarm(lg ? "Uống thuốc" : "Take the pills", 13, 0, false), save("reminder" + sfx, 0, 100);
      if (lk == 0) {
        ui::Now n = NOW(23, 12);
        ui::setNow(n);
        begin(), ui::night(), save("night" + sfx, 0, 100);
        ui::setNow(NOW(21, 47));
        begin(), ui::setupAp("EspOS-7E38", "48151623", false), save("setup_ap" + sfx, 0, 100);
        begin(), ui::claim("K7QM2XPA", "https://espos.dev/c/K7QM2XPA", false), save("claim" + sfx, 0, 100);
        begin(), ui::claim("K7QM2XPA", "", false), save("claim_noqr" + sfx, 0, 100);
        begin(), ui::note(ui::str(ui::S_UPDATING), "espos-bot 0.1.1", 42), save("updating" + sfx, 0, 100);
        begin(), ui::note(ui::str(ui::S_CONNECTING), "Nhà Lan 5G", -1), save("connecting" + sfx, 0, 100);
        begin(), ui::note(ui::str(ui::S_HOLD_RESET), "3", -1), save("hold_reset" + sfx, 0, 100);
        begin(), ui::note(ui::str(ui::S_NO_SERVER), "", -1), save("no_server" + sfx, 0, 100);
      }
    }
  }
  // art2's standby screens, each for 4 s, in both languages, with sample values
  for (int lg = 0; lg < 2; lg++) {
    ui::setLook(0);
    ui::setLang(lg ? ui::Lang::VI : ui::Lang::EN);
    ui::setNow(NOW(21, 30, 42));
    ui::Weather w;
    w.valid = true, w.t = 28, w.hi = 31, w.lo = 22, w.icon = "sun", w.desc = "Nắng nhẹ", w.city = lg ? "Hà Nội" : "Hanoi";
    ui::setWeather(w);
    ui::Info inf;
    inf.headlines = {"Hà Nội: chiều nay có mưa rào rải rác, nhiệt độ giảm còn 22 độ vào ban đêm"};
    inf.goldBuy = "148.5", inf.goldSell = "150.5", inf.usd = "26.350", inf.hum = 90;
    ui::setInfo(inf);
    for (int i = 0; i < art::STANDBY_COUNT; i++) {
      ui::setScreens({{(int16_t)i, 60}});
      ui::night();
      begin();
      ui::standby();
      save(std::string("standby_") + art::STANDBY[i].id + (lg ? "_vi" : "_en"), 4000, 100);
    }
    // "weather" shows the real sky: the picture for each icon at 14:00, the night after the sunset (17:35)
    ui::setScreens({{ui::SCR_WEATHER_AUTO, 60}});
    w.rise = 5 * 60 + 45, w.set = 17 * 60 + 35;
    const char* SKY[][3] = {{"sun", "Trời quang", "sun"},       {"cloudsun", "Mây rải rác", "cloudsun"},
                            {"cloud", "Nhiều mây", "cloud"},     {"fog", "Sương mù", "fog"},
                            {"rain", "Mưa vừa", "rain"},         {"storm", "Dông", "storm"}};
    for (auto& k : SKY) {
      w.icon = k[0], w.desc = k[1];
      ui::setWeather(w);
      ui::setNow(NOW(14, 0, 0));
      ui::night(), begin(), ui::standby();
      save(std::string("auto_weather_") + k[2] + (lg ? "_vi" : "_en"), 3000, 100);
    }
    w.icon = "sun", w.desc = "Trời quang";
    ui::setWeather(w);
    ui::setNow(NOW(17, 40, 0));
    ui::night(), begin(), ui::standby();
    save(std::string("auto_weather_night") + (lg ? "_vi" : "_en"), 0, 100);
    w.rise = w.set = -1;
    ui::setNow(NOW(21, 30, 42));
    // the calendar page gives way to the coffee 06:00-09:00
    ui::setNow(NOW(7, 5, 10));
    ui::setScreens({{(int16_t)ui::screenIndex("date_page"), 60}});
    ui::night(), begin(), ui::standby();
    save(std::string("morning_coffee") + (lg ? "_vi" : "_en"), 2000, 100);
  }
  // the setup screens before a language is chosen: the words take turns
  ui::setLook(0);
  begin(), ui::setupAp("EspOS-7E38", "48151623", true), save("setup_ap_both", 8000, 500);
  begin(), ui::claim("K7QM2XPA", "https://espos.dev/c/K7QM2XPA", true), save("claim_both", 8000, 500);
  // the rotation as it plays, with the burn-in shift (3 minutes a step)
  ui::setLang(ui::Lang::VI);
  ui::setScreens({});
  ui::night(), begin(), ui::standby();
  save("rotation_B_vi", 32000, 200);
  printf("ok\n");
  return 0;
}
