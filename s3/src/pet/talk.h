#pragma once
// Talking to the dog (the AI assistant, s3/docs/assistant-ai.md): hold the mic button in the dog's pop-up, speak,
// let go; or say "Gâu gâu" (the wake word, src/wake/wake.h) and speak. The recording goes up in ONE request
// (POST /v1/device/assist?stream=1 on the kept TLS connection); the dog's words and mood come back in the headers and
// its voice as the body, played while it arrives (16 kHz PCM, the odd byte of a piece carried to the next).
#include <lvgl.h>

namespace pet {
namespace talk {

void begin();  // after sound::begin and wake::begin: hooks the wake word

// The mic button (summon.cpp): pressed, let go (or the finger slid off).
void press();
void release();
bool busy();       // a turn is on: listening, sending, waiting or speaking
bool listening();  // recording the person now
bool canTalk();    // a mic, linked to an account
void closed();     // the pop-up closed: a turn stops

// A white mic drawn with LVGL (the art has no mic glyph): about 16 x 24 px, centred in `parent`.
lv_obj_t* micIcon(lv_obj_t* parent, uint32_t color);

// Dev console: ASK <text> (a typed turn through the same path: the voice is played), TALK (the last turn's numbers).
bool console(const char* cmd);

}  // namespace talk
}  // namespace pet
