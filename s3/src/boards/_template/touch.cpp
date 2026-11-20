// TEMPLATE: the touch controller driver (hal::Input, src/hal/input.h), for an I2C capacitive controller read 100 times
// a second (FocalTech FT6x36 register layout; GT911 and CST816 differ only in readPoint()). Working drivers:
//   src/boards/es3c28p/touch.cpp  FT6336G, polled, panel coordinates mapped back to the UI (2/3)
//   src/boards/es3c35p/touch.cpp  the vendor's driver, read when the interrupt line fires
//
// The rules: begin() runs once at boot (touch::begin()); touch::i2cBus() gives the board's I2C bus on BOARD.i2c, made
// on first use. It resets the controller, checks it answers (false if not: the OS then has no touch) and starts a task on core 0.
// The task hands every report to touch::report(x, y, down, ms) and never blocks on anything but the bus. x, y are the
// UI's portrait coordinates, 0..319 and 0..479, whatever the screen's rotation (LVGL turns them); a panel that shows
// the UI scaled maps its point back through BOARD.display.ui. touch.cpp does the rest: the queue, the system
// gestures, the 20 px slop, the tap click.
#ifndef ESPOS_QEMU  // QEMU has no touch controller: the dev console feeds touch::
#include <Arduino.h>

#include "driver/i2c_master.h"
#include "myboard.h"
#include "touch.h"

namespace myboard {
namespace {

constexpr hal::UiMap UI = BOARD.display.ui;
i2c_master_dev_handle_t dev;

// One finger, in the panel's pixels. TODO: your controller's registers.
bool readPoint(int& px, int& py) {
  uint8_t reg = 0x02, b[5];  // FT6x36: fingers, then XH XL YH YL
  if (i2c_master_transmit_receive(dev, &reg, 1, b, 5, 20) != ESP_OK) return false;
  const int n = b[0] & 0x0F;
  if (n < 1 || n > 2) return false;
  px = ((b[1] & 0x0F) << 8) | b[2], py = ((b[3] & 0x0F) << 8) | b[4];
  return true;
}

void task(void*) {
  bool down = false;
  int16_t x = 0, y = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(10));
    int px, py;
    const bool d = readPoint(px, py);
    const uint32_t now = millis();
    if (d) {  // the panel's point back to the UI's
      x = constrain((px - UI.x) * UI.den / UI.num, 0, UI.w - 1);
      y = constrain((py - UI.y) * UI.den / UI.num, 0, UI.h - 1);
      touch::report(x, y, true, now);
    } else if (down) {
      touch::report(x, y, false, now);  // lifted: where it was last
    }
    down = d;
  }
}

bool begin() {
  i2c_master_bus_handle_t bus = touch::i2cBus();  // made here, on BOARD.i2c
  if (BOARD.input.rst >= 0) {  // reset, active low; TODO: the time your controller needs after it
    pinMode(BOARD.input.rst, OUTPUT);
    digitalWrite(BOARD.input.rst, LOW);
    delay(10);
    digitalWrite(BOARD.input.rst, HIGH);
    delay(300);
  }
  i2c_device_config_t dc = {};
  dc.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dc.device_address = BOARD.input.addr;
  dc.scl_speed_hz = 400000;
  if (i2c_master_bus_add_device(bus, &dc, &dev) != ESP_OK) return false;
  if (i2c_master_probe(bus, BOARD.input.addr, 50) != ESP_OK) return false;
  xTaskCreatePinnedToCore(task, "touch", 3072, nullptr, 5, nullptr, 0);
  return true;
}

}  // namespace

const hal::Input input = {begin};

}  // namespace myboard
#endif
