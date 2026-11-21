#pragma once
// The dog as the device's assistant (hand-off 3, owner: "mỗi máy sẽ có 1 trợ lý ảo là chó ... báo thức, chào
// theo giờ, thời tiết, thói quen, bộ số"; no device, calendar or reminder lines). The chosen dog's assist.pba:
// 21 moments x 2 lines (greetings by time of day, the alarm ringing a 1st/2nd/3rd time, snooze, off, weather,
// habits) and a 73-clip NUMBER KIT. A recorded line never holds a number: the dog says its line, then the kit
// says the hour or the temperature in the same voice (dogs.json assistant.number_kit, sim/assist.html):
//   time:        t_now, n_<hour 1..12>, u_gio, then u_ruoi (:30) | u_dung (:00) | n_<minute> u_phut, then the part
//                of the day: p_sang (4-10 h), p_trua (11-12), p_chieu (13-17), p_toi (18-21), p_dem (22-3)
//   alarm time:  t_alarm, then the same without t_now        temperature: t_out, n_<0..59>, u_do
//   gaps:        260 ms after the dog's own line, 20 ms between the kit's clips
// It speaks through the pop-up (summon.cpp) and on the alarm's screen (alarm.cpp's hook).
// Settings: s3.assist (synced): {"on":1,"greet":1,"weather":1,"alarm":1,"water":120,"move":90,"eyes":60,"meal":1,
// "wake":0,"wake_word":0,"wake_sens":2,"calls":31,"led":2,"led_bright":2,"follow":1,"ack":2}
// (minutes between habit reminders; 0 = off). Never in Do Not Disturb or quiet hours, except the alarm and a
// late-night touch.
#include <Arduino.h>
#include <lvgl.h>

#include <vector>

#include "voice.h"

namespace pet {
namespace assist {

struct Prefs {
  bool on = true, greet = true, weather = true, alarm = true, meal = true;
  int water = 120, move = 90, eyes = 60;  // minutes, 0 = off
  // The wake word (src/wake/wake.h): wake 0 off, 1 only on the charger, 2 always; wake_word 0 "gâu gâu", 1 "gâu"
  // alone too; wake_sens 1 low, 2 normal, 3 high.
  uint8_t wake = 0, wakeWord = 0, wakeSens = 2;
  // calls: which calls wake it, bits: 1 "gâu gâu", 2 "ki ki", 4 "lu lu", 8 "cún ơi", 16 "milu"; all five by default
  // (2026-11-21, the owner tried "cún ơi" with only the first two on: "cún ơi ko nghe đc"). Never 0.
  uint8_t calls = 31;
  // The RGB LED (src/led.h): led 0 off, 1 only with the dog, 2 the dog and arrivals; led_bright 1-3.
  uint8_t led = 2, ledBright = 2;
  // follow 1: after a spoken answer the dog listens on (no call needed) for speech that starts within ~2.5 s and is
  // close to the device (owner, 2026-11-19: "chặt hơn, vẫn bật"); 0: every question starts with "Gâu gâu" or the button.
  bool follow = true;
  // ack: what the dog does when it hears "gâu gâu" and the person pauses: 0 nothing, 1 a short beep, 2 a word in its
  // own voice ("Dạ?", "Nghe nè!": assist.pba "ack", dog packages 1.0.2; the beep with an older package).
  uint8_t ack = 2;
};
Prefs prefs();
void setPrefs(const Prefs& p);  // stores s3.assist (goes up with the next sync)

bool ready();    // the chosen dog's assistant file is in
void release();  // another dog: its file goes

// What the dog will say: its line for a moment, then the kit; `said` = the line's words, `kit` = the kit's words
// (lower case, as the page shows them). false when the dog has no such line.
struct Speech {
  std::vector<voice::Item> items;
  String said, kit;
  const char* moment = "";
};
bool line(const char* moment, Speech& s);
void addTime(Speech& s, int h, int m, bool alarm);  // "bây giờ là sáu giờ rưỡi sáng" / "báo thức lúc ..."
void addTemp(Speech& s, int degrees);               // "ngoài trời đang ba mươi bốn độ"
bool play(const Speech& s);
// The weather now as a moment (w_air, w_rain, w_hot, w_cold, w_nice) and its temperature; false: no weather data.
bool weatherNow(const char*& moment, int& temp);
// The clip the dog plays while it says a moment.
const char* clipFor(const char* moment);

void tick();  // from pet::tick (500 ms): greetings, the weather, habits, the alarm's repeats
// The alarm (alarm.cpp hook): ev 0 = ringing (ring 1, 2, 3...), 1 = snoozed (h:m: when it rings again), 2 = off.
void alarmEvent(int ev, int ring, int h, int m, lv_obj_t* layer);
bool talking();  // the dog is speaking (the alarm's beeps wait)

}  // namespace assist
}  // namespace pet
