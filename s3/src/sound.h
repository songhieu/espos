#pragma once
// Speaker: ES8311 codec on the touch I2C bus, I2S at 16 kHz, amplifier enable on GPIO1.
#include <stdint.h>
namespace sound {
bool begin();
void setVolume(int percent);  // 0..100
void click();                 // a short soft tick for a tap (skipped when muted or in Do Not Disturb)
void chime();                 // two notes, e.g. for a finished sync the user asked for
void blip();                  // two soft quick notes: the dog heard its name (the wake word); not muted by Do Not Disturb
void test();                  // a short tune to check the speaker (ignores Do Not Disturb)
void alarmBeep();             // four loud beeps for an alarm or a timer (ignores Do Not Disturb)

// Streaming PCM, 16 kHz mono (PawBoop's voice lines, decoded on the fly). The sound task pulls 20 ms at a time:
// `pull` fills up to n samples and returns how many, 0 when the stream is over. It runs on the sound task, so
// it must only copy from a buffer. A new stream cuts the one playing with a 10 ms fade. A click is mixed in;
// chime, test and alarm stop the stream. The volume is the system volume.
typedef int (*Pull)(short* out, int n, void* ctx);
void stream(Pull pull, void* ctx);
void stopStream();   // fades out over 10 ms
bool streaming();

// The microphone's I2S channel (i2s_chan_handle_t: the codec's ADC, full duplex with the speaker), nullptr when the
// board has none or the codec did not answer. src/mic.cpp enables and reads it.
void* micChannel();
// Something played in the last `ms` (a stream, a click, a chime, the alarm): the wake word does not listen to the
// device's own speaker.
bool outputSince(uint32_t ms);
}  // namespace sound
