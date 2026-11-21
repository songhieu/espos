#include "oled.h"

#include <Arduino.h>
#include <Wire.h>
#include <string.h>

#include "config.h"

namespace bot {
namespace oled {
namespace {

uint8_t addr = 0;
uint8_t shadow[1024];
bool shadowOk = false;
uint32_t errs = 0;
bool flipped = false;

void cmd(std::initializer_list<uint8_t> c) {
  Wire.beginTransmission(addr);
  Wire.write(0x00);
  for (uint8_t b : c) Wire.write(b);
  if (Wire.endTransmission()) errs++;
}

void page(const uint8_t* fb, int p) {
  cmd({0x21, 0, 127, 0x22, (uint8_t)p, (uint8_t)p});
  for (int i = 0; i < 128; i += 64) {  // the Wire buffer holds 128 bytes: two pieces a page
    Wire.beginTransmission(addr);
    Wire.write(0x40);
    Wire.write(fb + p * 128 + i, 64);
    if (Wire.endTransmission()) errs++;
  }
}

}  // namespace

bool begin() {
  Wire.begin(pins::SDA, pins::SCL, 400000);
  for (uint8_t a : {0x3C, 0x3D}) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      addr = a;
      break;
    }
  }
  if (!addr) return false;
  Wire.setClock(800000);
  cmd({0xAE, 0xD5, 0x80, 0xA8, 63, 0xD3, 0x00, 0x40, 0x8D, 0x14, 0x20, 0x00});
  cmd({(uint8_t)(flipped ? 0xA0 : 0xA1), (uint8_t)(flipped ? 0xC0 : 0xC8)});
  cmd({0xDA, 0x12, 0x81, 0x9F, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0x2E, 0xAF});
  shadowOk = false;
  return true;
}

bool ok() { return addr != 0; }

void push(const uint8_t* fb) {
  if (!addr) return;
  for (int p = 0; p < 8; p++)
    if (!shadowOk || memcmp(shadow + p * 128, fb + p * 128, 128)) page(fb, p);
  memcpy(shadow, fb, sizeof shadow);
  shadowOk = true;
}

void pushAll(const uint8_t* fb) {
  shadowOk = false;
  push(fb);
}

void brightness(int pct) {
  if (!addr) return;
  pct = pct < 5 ? 5 : pct > 100 ? 100 : pct;
  // The panel's contrast is far from linear: the low end matters at night. 5 % -> 1, 100 % -> 255.
  const int c = 1 + (pct - 5) * (pct - 5) * 254 / (95 * 95);
  // Precharge lower too when dim: the dimmest the SSD1306 goes.
  cmd({0x81, (uint8_t)c, 0xD9, (uint8_t)(pct < 20 ? 0x22 : 0xF1), 0xDB, (uint8_t)(pct < 20 ? 0x00 : 0x40)});
}

// Segment remap (A0/A1) applies to what is written next, the COM scan (C0/C8) at once: so the whole picture again.
void flip(bool on) {
  if (on == flipped) return;
  flipped = on;
  if (!addr) return;
  cmd({(uint8_t)(flipped ? 0xA0 : 0xA1), (uint8_t)(flipped ? 0xC0 : 0xC8)});
  if (shadowOk) {
    static uint8_t again[sizeof shadow];
    memcpy(again, shadow, sizeof again);
    pushAll(again);
  }
}

void power(bool on) {
  if (addr) cmd({(uint8_t)(on ? 0xAF : 0xAE)});
}

uint32_t errors() { return errs; }

}  // namespace oled
}  // namespace bot
