#pragma once
// EspOS Bot's turn with the dog (the S3's talk path, words only: no speaker yet). "Gâu gâu" or BOOT -> it listens
// (6 s for the question to start, it ends 2 s after the person stops, 9 s at most) -> the question goes to
// /v1/device/assist?voice=0 (sent again once if the connection failed) -> the reply on the screen. A reply that asks
// something, or bot.follow on, listens again without the wake word; nothing heard then ends it quietly. 20 s after
// the last reply the standby screens come back.
#include <stdint.h>

namespace bot {
namespace talk {

void loop();
void start(bool byWake);  // BOOT or the wake word
bool active();            // the dog's screen is on
bool busy();              // listening or waiting for the answer (a wake word then is not a new turn)
void press();             // BOOT during a turn: listening -> send now; a reply -> close it

void setQuiet(bool on);   // night hours: no voice (the words only, ?voice=0) and no cues; alarms still ring

// Tests (the console): the turn as if this was the question's answer, no network.
void fake(int status, const char* json);

}  // namespace talk
}  // namespace bot
