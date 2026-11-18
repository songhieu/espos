#pragma once
// Alarms (setting s3.alarms, synced with the web) and the countdown timer. A due alarm wakes
// the screen, rings through the speaker (even in Do Not Disturb) until "Tắt" or "Báo lại".
#include <Arduino.h>
#include <lvgl.h>

#include <vector>

namespace alarms {

struct Alarm {
  int h = 7, m = 0;
  String days;   // weekday digits, 0 = Sunday ("12345" = Monday to Friday); "" = once
  bool on = true;
  String label;
};

void begin();
void buildLayer();  // after the shell rebuilt the top layer
void tick();        // from the UI loop

std::vector<Alarm> list();
void save(const std::vector<Alarm>& all);
String daysText(const String& days);  // "Mỗi ngày", "T2 đến T6", "Một lần"...
// The next alarm that will ring: "06:30" and "Thứ Hai · Dậy đi làm" ("" when none).
void next(String& when, String& day);

// Timer.
void startTimer(int seconds);
void pauseTimer();
void resumeTimer();
void cancelTimer();
bool timerOn();      // running or paused
bool timerPaused();
int timerLeft();     // seconds
int timerTotal();
bool ringing();

// Someone speaking at the alarm (PawBoop's dog assistant, pet/assist.cpp): told when an alarm rings (ring 1, 2,
// 3...: it rings again after each snooze; not the timer), is snoozed (h:m: when it rings again) or turned off;
// `layer` is the alarm's screen. While it talks (talking()) the beeps wait.
struct Hook {
  void (*event)(int ev, int ring, int h, int m, lv_obj_t* layer);  // ev 0 ringing, 1 snoozed, 2 off
  bool (*talking)();
};
void setHook(const Hook* h);

}  // namespace alarms
