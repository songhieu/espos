#pragma once
// HAL: audio (CAP_AUDIO_OUT, audio.out; CAP_AUDIO_IN, audio.in: the codec's ADC, no OS API yet). The speaker: an I2S codec set up over the board's I2C bus by src/sound.cpp (the OS's sound:: API: the tap
// click, chimes, the alarm, streams), from this part of the descriptor. One kind today, the ES8311 (both boards); another
// codec is a value here and its register setup in sound.cpp (codecInit, applyVolume).
#include <cstdint>

namespace hal {

enum class Codec : uint8_t { NONE, ES8311 };

struct AudioDesc {
  Codec codec;
  uint8_t addr;                      // the codec's I2C address
  int8_t mclk, bclk, ws, dout, din;  // I2S
  int8_t amp;                        // the speaker amplifier's enable pin; -1: none
  uint8_t ampOn;                     // its level for "on"
};

}  // namespace hal
