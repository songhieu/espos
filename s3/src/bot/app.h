#pragma once
// EspOS Bot's main loop: what the screen shows (setup, claim, standby, night, the dog, alarms, updates), the BOOT
// button, and the settings put to work.
namespace bot {
namespace app {
void begin();
void loop();
void button(bool longHold);    // a press (also the console's BOOT short|long)
void uiTest(const char* what); // the console's UI command: a screen held for a picture; "auto" ends it
}  // namespace app
}  // namespace bot
