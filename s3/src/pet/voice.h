#pragma once
// The dog's voice: its Vietnamese lines (voice.pba) and the assistant's lines and number kit (assist.pba), Opus
// 16 kHz mono, decoded on the fly by a task (libopus, lib/opus) and streamed to the speaker through sound::stream.
// Every line is levelled to the same loudness, so all play at ONE gain (VOICE_GAIN in voice.cpp) and the system
// volume. When the dog talks follows the page's speak(): an action cuts what it says, its own talk waits for quiet.
#include <stdint.h>

#include "formats.h"

namespace pet {
namespace pk {
class Pba;
}
namespace voice {

// The owner: a moment's line is picked at random, never the one said last for that moment, nor the line said
// just before (when the moment has another). lineFor(on): a line of a picture clip or an "@event" of the dog's
// lines; of(pack, on): of any voice file (the assistant's). nullptr when the moment has none.
const pba_clip_t* lineFor(const char* on);
const pba_clip_t* of(const pk::Pba& pack, const char* on);
// The two beats (caught_1 then caught_2): the answer that belongs to the first line (the same index k).
const pba_clip_t* answerTo(const pba_clip_t* first, const char* secondMoment);
const pba_clip_t* notAgain(const pba_clip_t* l);  // l, or another line of its moment when l was said last
const pba_clip_t* last();
bool speak(const pba_clip_t* l, bool force);  // one of the dog's lines; force: cut a line that plays
// Several clips one after another, each followed by its gap (silence): the assistant's line then the number
// kit ("Bây giờ là" "sáu" "giờ" "rưỡi" "sáng"). At most 12. Plays like speak(..., true).
struct Item {
  const pk::Pba* pack;
  const pba_clip_t* clip;
  uint16_t gapMs;
};
bool say(const Item* items, int n);
void hush();                                  // stop talking now (10 ms fade)
bool busy();                                  // a line is playing (by its length)
uint32_t quietMs();                           // since the last line ended (large when never)
const pba_clip_t* current();                  // the line playing, or nullptr
uint32_t startedAt();                         // millis() when it started
void setMuted(bool muted);                    // bubbles still show, no sound
bool muted();
void release();  // forget the dog's lines (another dog's packs come)

struct Stats {
  uint32_t lines = 0, packets = 0, decUs = 0, decMaxUs = 0, underruns = 0, firstSoundUs = 0, readFails = 0;
};
Stats& stats();

}  // namespace voice
}  // namespace pet
