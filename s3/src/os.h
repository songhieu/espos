#pragma once
// EspOS shell: the status bar, screens of apps (open, back, home), the gestures as on a phone
// (status bar pulled down on its left part: Notification Center, on its right part: Control Center;
// up from the bottom edge: home; right from the left edge: back; down on a Home page: Search — they
// watch the touch samples, so taps near the edges still reach the app), toasts, the screen saver,
// rotation and brightness.
#include <Arduino.h>
#include <lvgl.h>

namespace os {

void begin(lv_display_t* disp);
void tick();  // from loop()

int W();
int H();
bool landscape();
// Content laid out for the 3.5" board's screen, 320 x 480 (480 x 320 turned: the games, the saver's pages), keeps that
// size in the middle of a bigger screen (the 2.8" board's 360 x 480): colX() / colY() is where its (0, 0) is (0 on the
// 3.5" board); column(body) moves a BARE screen's body there (the screen's background shows around it).
int colX();
int colY();
void column(lv_obj_t* body);

// ------------------------------------------------------------------ app screens
enum Flags {
  ROOT = 1,      // an app's first screen: big title; back goes home
  DARK = 2,      // always dark (clock, night apps)
  NOSCROLL = 4,  // body does not scroll
  BARE = 8,      // no nav bar, no padding: the app draws everything
  TWO = 16,      // a list: in landscape its rows go two a line (kit::listRow), captions and hints keep the line
};
constexpr int SB_H = 24;   // status bar
constexpr int NAV_H = 44;  // nav bar under it

// A screen with a nav bar ("‹ title", or a big title for ROOT) and a body under it: a
// vertical flex column with 12 px side padding that scrolls. Colours from kit::pal().
lv_obj_t* screen(const char* title, lv_obj_t** body, int flags = 0);
// A small text action at the right of the nav bar (e.g. "Lưu", "Sửa", "+").
lv_obj_t* navAction(lv_obj_t* scr, const char* text, lv_event_cb_t cb, void* data = nullptr);
// The nav bar title label, to change it later.
lv_obj_t* navTitle(lv_obj_t* scr);
void open(lv_obj_t* scr);     // slide it in over the current screen
void back();                  // close the top screen
// An app screen that handles the back gesture itself first (a JS app's app.onBack, lane 1f): fn returns true when it
// went back inside the app; false lets back() close the screen. Removed with the screen.
void setBackHook(lv_obj_t* scr, bool (*fn)());
void goHome();                // close every app screen
void closeApps();             // at once, without animation (an app opens another app)
bool atHome();
lv_obj_t* homeScreen();
// The status bar draws dark text (light screens) or white text.
void barDark(bool darkText);
void barDarkFor(lv_obj_t* scr, bool darkText);  // a BARE screen's status bar text (white by default)
void fullScreenFor(lv_obj_t* scr, bool on);      // a BARE screen without the status bar while it is in front

// ------------------------------------------------------------------ system
void toast(const char* text);
void post(const char* app, const char* title, const char* body);  // a notification
void openControl();
void showSaver();
void wake();                 // leave the saver
bool saverOn();
// Rebuild every screen (theme or layout change); `keepApps` keeps an open app on top.
void rebuild();
// s3.rot 0 portrait, 1 landscape, 2 portrait upside down, 3 landscape upside down: every screen is built again and the
// app in front opens again (apps::route).
void applyRotation(int r);
void turn();  // the Control Center's Rotate: the other of the user's own portrait and landscape ways
int rotation();
int brightness();
void setBrightness(int pct, bool save);
int volume();
void setVolume(int pct, bool save);
bool fingerDown();
uint32_t idleMs();           // since the last touch

// Shown by the edit mode of Home (hides the status bar while it is on).
void setStatusHidden(bool hidden);

}  // namespace os
