#pragma once
// Notifications for every app: built-in apps, native games, JavaScript apps (docs/ecosystem.md).
// A notification shows as a banner at the top (tap: opens the app with `arg`, swipe up: away), stays in the
// Notification Center (pulled down from the left half of the status bar) and counts on the app's icon badge.
// The user's per-app switches are setting s3.notif (synced with the web); Do Not Disturb and quiet hours
// keep banners and sounds away but notifications still reach the Center.
// History (50 newest) and scheduled ones live in LittleFS /sys/notif.json, written a few seconds after a change.
// The dashboard's server notes (dash "notes") arrive as notifications of the pseudo app "desk" ("EspOS").
// Mockup: mockups/s3-notif.html.
#include <Arduino.h>
#include <lvgl.h>

#include <time.h>

#include <vector>

namespace notif {

struct Opts {
  const char* arg = nullptr;  // passed to the app's open(arg) when the notification is tapped
  const char* tag = nullptr;  // a later one with the same app+tag replaces it (and a scheduled one)
  bool sound = true;
  bool badge = true;          // counts on the app's icon until the app is opened or it is cleared
};

// false when the app's notifications are off (or the app has no "notify" permission, for packages).
// Text is kept up to 120 bytes of title and 400 of body.
bool post(const char* app, const char* title, const char* body, const Opts& o = Opts());
// Fires at `at` (unix seconds) even when the app is closed; kept across restarts. Same tag replaces.
// A time already past posts at once; one found more than 15 minutes late (device was off) goes to the
// Center without banner or sound. At most 32 pending.
bool schedule(const char* app, time_t at, const char* title, const char* body, const Opts& o = Opts());
// tag nullptr: every pending one of the app; with a tag: the pending one and the delivered one of that tag.
void cancel(const char* app, const char* tag);
void clear(const char* app);                    // removes the app's notifications and its badge
void seen(const char* app);                     // the app was opened: its notifications stop counting (apps::open)

int badge(const char* app);  // what the Home icon shows (0: none): own count, else max(unread, built-in count)
void setBadge(const char* app, int n);  // an app's own count (e.g. unread items); -1 back to automatic
bool enabled(const char* app);

void openCenter();
void closeCenter();
bool centerShown();
// Follows a finger pulling the Center down from the status bar: dy = pixels pulled so far; on release it
// opens (pulled past a fifth of the screen) or slides back.
void pullCenter(int dy, bool released);

void openSettings();  // Settings > Thông báo: a switch per app, "Xem trước"
int count();          // notifications in the Center (the Control Center's tile)

// Read-only view of the Center's history for widgets (no scheduled ones), newest first, at most n.
// Changes are announced by version().
struct Shown {
  String app, title, body, arg;  // arg: pass to apps::open(app, arg) on tap, like the Center does
  time_t at = 0;                 // unix seconds (0: posted before the clock was set)
  bool unread = false;           // not yet seen (the app was not opened since)
};
std::vector<Shown> latest(int n);
String name(const char* app);  // display name as the Center shows it ("EspOS" for "desk", package names)
// The app's icon as in the Center (size 28/40/52; "desk" gets the blue bell square).
lv_obj_t* icon(lv_obj_t* parent, const char* app, int size);
String when(time_t at);  // the Center's time text: "bây giờ", "12 phút trước", "09:41", "Hôm qua", "14/11"; "" if unknown

void tick();       // from os::tick: fires scheduled ones, ends banners
uint32_t version();  // changes when a badge or the Center's list changes

}  // namespace notif
