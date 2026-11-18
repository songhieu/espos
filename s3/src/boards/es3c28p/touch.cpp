// ES3C28P's touch (the board's hal::Input; was in src/touch.cpp): the FT6336G on the board's I2C bus, read 100 times a
// second by a task (its interrupt line is not needed for that); every report to touch::report() (src/touch.cpp).
#ifndef ESPOS_QEMU  // QEMU: no touch controller
#include <Arduino.h>

#include "driver/i2c_master.h"
#include "es3c28p.h"
#include "touch.h"

namespace es3c28p {
namespace {

constexpr int LCD_W = hal::UI_W, LCD_H = hal::UI_H;  // the UI
constexpr hal::UiMap UI = BOARD.display.ui;           // where it is on the panel (2/3, from x 13)
i2c_master_dev_handle_t ft;

// FT6336G: register 0x02 = how many fingers (low 4 bits), then the first one's XH (the event in bits 6-7, x bits 8-11),
// XL, YH, YL, in the panel's 240x320. The UI is the panel's 3/2 (213 px of it across, from x 13).
bool readFt(int16_t& x, int16_t& y) {
  uint8_t reg = 0x02, b[5];
  if (i2c_master_transmit_receive(ft, &reg, 1, b, 5, 20) != ESP_OK) return false;
  const int n = b[0] & 0x0F;
  if (n < 1 || n > 2) return false;
  const int px = ((b[1] & 0x0F) << 8) | b[2], py = ((b[3] & 0x0F) << 8) | b[4];
  x = constrain((px - UI.x) * UI.den / UI.num, 0, LCD_W - 1);
  y = constrain((py - UI.y) * UI.den / UI.num, 0, LCD_H - 1);
  return true;
}

void task(void*) {
  bool down = false;
  int16_t x = 0, y = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(10));
    int16_t nx, ny;
    const bool d = readFt(nx, ny);
    const uint32_t now = millis();
    if (d) {
      x = nx, y = ny;
      touch::report(x, y, true, now);
    } else if (down) {
      touch::report(x, y, false, now);
    }
    down = d;
  }
}

// A reset (active low), ~300 ms, then its id (FocalTech's: 0x11 on these).
bool begin() {
  i2c_master_bus_handle_t bus = touch::i2cBus();  // the board's bus first, then the controller's reset
  pinMode(BOARD.input.rst, OUTPUT);
  digitalWrite(BOARD.input.rst, LOW);
  delay(10);
  digitalWrite(BOARD.input.rst, HIGH);
  delay(300);
  i2c_device_config_t dc = {};
  dc.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dc.device_address = BOARD.input.addr;
  dc.scl_speed_hz = 400000;
  if (i2c_master_bus_add_device(bus, &dc, &ft) != ESP_OK) return false;
  uint8_t reg = 0xA8, vendor = 0;
  const esp_err_t err = i2c_master_transmit_receive(ft, &reg, 1, &vendor, 1, 50);
  Serial.printf("touch: FT6336 %s, vendor %02x\n", esp_err_to_name(err), vendor);
  if (err != ESP_OK) return false;
  xTaskCreatePinnedToCore(task, "touch", 3072, nullptr, 5, nullptr, 0);
  return true;
}

}  // namespace

const hal::Input input = {begin};

}  // namespace es3c28p
#endif
