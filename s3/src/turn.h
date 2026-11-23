#pragma once
// The assistant's turn, the OS's client for whichever assistant app is in the slot (assistant.h; docs/app-slots.md §5,
// lane 4b): hold the mic button in its pop-up, speak, let go; or say the wake word (src/wake/wake.h) and speak. The
// recording goes up in ONE request (POST /v1/device/assist?stream=1 on the kept TLS connection); the server routes it to
// the slot's assistant (PawBoop AI, or a third-party app's provider); the words and mood come back in the headers and
// the voice as the body, played while it arrives (16 kHz PCM, the odd byte of a piece carried to the next). What shows
// it is the slot app's face (assistant::face()): the dog's pop-up for PawBoop, the OS pop-up for any other app.
#include <lvgl.h>

namespace turn {

void begin();  // after sound::begin and wake::begin: hooks the wake word

// The mic button of the pop-up: pressed, let go (or the finger slid off).
void press();
void release();
bool busy();       // a turn is on: listening, sending, waiting or speaking
bool warming();    // the TLS connection for the question is being made (core 0 is busy: animations at half speed)
bool listening();  // recording the person now
bool canTalk();    // a mic, linked to an account
bool canChat();    // linked to an account (typed turns: the chips; no mic needed)
void closed();     // the pop-up closed: a turn stops
void askNews();    // the dog's "Today's news" chip: a typed turn that sums up today's news

// The OS's mic glyph (assistant::micIcon), about 18 x 26 px, centred in `parent`.
lv_obj_t* micIcon(lv_obj_t* parent, uint32_t color);

// Dev console: ASK <text> (a typed turn through the same path: the voice is played), TALK (the last turn's numbers),
// WAKEFIRE, TALKPRESS / TALKRELEASE, SUMMON, LED <state>, SAYAGAIN <key>, DOGBENCH.
bool console(const char* cmd);

}  // namespace turn
