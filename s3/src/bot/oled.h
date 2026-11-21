#pragma once
// The SSD1306 128x64 over I2C (SDA 41, SCL 42, 800 kHz: 0 errors on the first board). Sends only the pages
// (8-row bands) that changed since the last picture.
#include <stdint.h>

namespace bot {
namespace oled {

bool begin();                     // finds the panel (0x3C or 0x3D) and sets it up; false: none answered
bool ok();
void push(const uint8_t* fb);     // gfx::fb's order: page by page, 128 bytes each
void pushAll(const uint8_t* fb);  // every page (after a reset of the panel)
void brightness(int pct);         // 5..100 (the panel's contrast)
void power(bool on);
void flip(bool on);               // turned 180° (bot.flip): the panel's own remap, the picture sent again
uint32_t errors();                // I2C transfers that failed

}  // namespace oled
}  // namespace bot
