#pragma once
// PawBoop on EspOS: Biscuit the dog as an app (the game designer's game, its rules and lines unchanged), a home
// widget, her book (streak, moments album, switches) and the mockup of the coming dog assistant.
// Data: three packs in their own flash partitions (docs/pawboop.md); the game state in NVS namespace "pet".
//
// Hooks for the OS:
//   apps.cpp list:  {"pet", "PawBoop", &ic_pet_52, &ic_pet_40, &ic_pet_28, app_pet::open, nullptr}
//   wid.cpp:        kind "pet"; in wid::create: else if (k == "pet") pet::widget(c.o, w, h, it.size);
//   OS loop:        pet::tick() every 500 ms (cheap; safe to call more often)
//   Control Center tile / gesture:  pet::summon("cc") / pet::summon("gesture")
//   Reminders:      checked off on the device -> Biscuit celebrates by herself (tick() watches s3.todo)
#include <lvgl.h>

namespace pet {

// The app: nullptr or "" opens the game; "book" her book; "summon" pops her up over the current screen.
void open(const char* arg);
// Every 500 ms from the OS loop: her needs fall, she finds coins, notifications (rate limited, never in Do
// Not Disturb or quiet hours), the morning greeting, the celebration of a finished reminder, the widgets.
void tick();

// Home widget: size 0 small 144x138, 1 medium 300x138, 2 large 300x286. Biscuit in her current mood (a still
// picture: the home screen stays smooth) and her four needs. Tap opens the game (through the widget's card).
lv_obj_t* widget(lv_obj_t* parent, int w, int h, int size);

// The assistant mockup (no AI): Biscuit pops up over any screen, greets with a clip and one of her lines, a
// bubble, and three suggestion chips with prepared answers. Tap outside to close. Reasons: "cc", "gesture",
// "morning", "remind", "hungry", "sleepy", "bored" (anything else greets like "cc").
void summon(const char* reason);
void dismiss();
bool summoned();
void celebrate(const char* what);  // = summon(what), e.g. "remind"

bool ready();         // PawBoop and a dog are installed (false: the app shows what is missing)
const char* name();   // the chosen dog's name ("Biscuit"; "PawBoop" when there is none)
bool quietNow();      // Do Not Disturb, or the quiet hours (the dog does not speak up by itself)

}  // namespace pet

namespace app_pet {
void open(const char* arg);
}
