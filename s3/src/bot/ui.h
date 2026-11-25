#pragma once
// EspOS Bot: what the 128x64 OLED shows, drawn into gfx::fb. Pure C++ (no Arduino), so the host build (sim/bot)
// draws the very same pictures. The app says what to show (standby(), listen(), talk(), claim(), ...) and calls tick()
// often; tick() draws when something changed and says so, and the app then sends the picture to the panel.
#include <stdint.h>

#include <string>
#include <vector>

namespace bot {
namespace ui {

enum class Lang : uint8_t { EN, VI };
enum class Mood : uint8_t { TALK, HAPPY, SAD };
// A standby screen of the rotation: one of art2's (art::STANDBY, by index), or SCR_WEATHER_AUTO ("weather": the
// picture of the real sky, resolve() in ui.cpp: sun, sun behind a cloud, clouds, rain, or the night).
constexpr int16_t SCR_WEATHER_AUTO = -1;
struct Screen {
  int16_t id;
  uint16_t secs;
};
// bot.screens' ids: art2's ("clock_bold", "aquarium", ...), "weather", and the first firmware's clock/date/dog
// (clock_bold, date_page, eyes). -2: unknown (a screen of a newer firmware: skipped).
int16_t screenIndex(const std::string& id);
const char* screenId(int16_t index);

struct Now {  // local time; valid once the clock was set (NTP, the server, the USB line)
  bool valid = false;
  int year = 2026, mon = 1, day = 1, wday = 0, hour = 0, min = 0, sec = 0;  // mon 1-12, wday 0 = Sunday
  std::string lunar;  // the server's lunar date ("1/10"), for the date screen in Vietnamese
};
// The dashboard's extras for the info screens (only what the rotation shows comes from the server).
struct Info {
  std::vector<std::string> headlines;    // newspaper
  std::string goldBuy, goldSell, usd;    // gold: million VND a lượng ("148.5"), USD selling ("26.350")
  int hum = -1;                          // weather_rain's humidity %, -1 = none
};
struct Weather {  // the sync answer's "weather" (device_sync merge.go)
  bool valid = false;
  int t = 0, hi = 0, lo = 0;  // °C
  std::string icon;           // sun moon cloudsun cloud fog rain snow storm
  std::string desc;           // the server's words (Vietnamese)
  std::string city;
  int rise = -1, set = -1;    // the city's sunrise and sunset today, minutes after midnight (-1 = not sent)
};

// The words the app shows through talk()/note() (English and Vietnamese, built in).
enum Str : uint8_t {
  S_LISTENING, S_NOT_HEARD, S_JOIN_WIFI, S_TO_SET_UP, S_ADD_DEVICE, S_CONNECTING, S_NO_WIFI, S_UPDATING,
  S_TRIAL_OVER, S_HOLD_RESET, S_WIFI_FORGOTTEN, S_ALARM, S_TIMER, S_HELLO, S_SAVED, S_RESTARTING, S_NO_SERVER,
  S_SET_UP_USB, S_ACCOUNT_LOCKED, S_PASSWORD, S_WRONG_PASSWORD, S_NO_NETWORK, S_JOIN_FAILED, S_TRY_ON_PHONE, S_WIFI_OK,
  S_PAIRED, S_GETTING_CODE, S_PAIR_SOON, S_VOLUME, S_LOST_WIFI, S_RETRYING, S_NEW_WIFI_ON_PHONE, S_FIX_ON_PHONE,
  S_COUNT_
};
const char* str(Str s);

// Settings
void setLook(uint8_t look);  // 0 B (big eyes), 1 C (pixel puppy), 2 D (line art)
uint8_t look();
void setLang(Lang l);
Lang lang();
void setNow(const Now& n);
void setWeather(const Weather& w);
void setScreens(const std::vector<Screen>& s);  // the standby rotation (empty: the default one)
void setInfo(const Info& i);
void setOffline(bool on);  // a small crossed-out WiFi mark in the standby's top right corner

// What to show
enum class State : uint8_t { STANDBY, NIGHT, LISTEN, THINK, TALK, HAPPY, SAD, SETUP_AP, CLAIM, ALARM, NOTE };
State state();
void standby();  // the rotation
void night();    // a small clock that moves (the app also dims the panel)
void listen();
void think();
// The dog's reply in the look's box: the mouth moves while the words scroll (never cut, never "..."), then a still
// face for the mood. talkDone() once the last line has been on the screen long enough to read.
void talk(const std::string& words, Mood mood = Mood::TALK);
bool talkDone();
void happy();
void notHeard();  // the sad face, then "I didn't catch that"
// Setup: join the bot's own WiFi (a QR code with its password, and the password as text), then the claim code with the QR code of claim_url. bothLangs:
// no language chosen yet, the words take turns in English and Vietnamese.
void setupAp(const std::string& ssid, const std::string& pass, bool bothLangs);
void claim(const std::string& code, const std::string& url, bool bothLangs);
void alarm(const std::string& label, int h, int m, bool timer);
// A plain card: a title and a line, with a bar when pct >= 0.
void note(const std::string& title, const std::string& line = "", int pct = -1);

// Draws if something changed since the last call; true: gfx::fb is a new picture.
bool tick(uint32_t ms);

// For the host build and the tests
std::string greeting(int hour, bool vi);  // the coffee screen's words for the local hour
const char* standbyId();                   // the standby screen shown (or last shown)
std::string dateLine(const Now& n, int maxW);
std::string weatherWords(const Weather& w);

}  // namespace ui
}  // namespace bot
