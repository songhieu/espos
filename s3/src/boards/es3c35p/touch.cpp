// ES3C35P's touch (the board's hal::Input; was in src/touch.cpp): the vendor's ST77922 touch driver (src/vendor) on the
// board's I2C bus, read by a task when its INT line fires (the controller reports 100 times a second while a finger is
// down); every report to touch::report(), whose queue LVGL and the system gestures take in order (src/touch.cpp).
#ifndef ESPOS_QEMU  // QEMU: no touch controller
#include <Arduino.h>

#include "es3c35p.h"
#include "esp_lcd_panel_io.h"
#include "freertos/semphr.h"
#include "touch.h"
#include "vendor/esp_lcd_st77922.h"

namespace es3c35p {
namespace {

constexpr int LCD_W = BOARD.display.ui.w, LCD_H = BOARD.display.ui.h;  // the UI: the panel's own size here
esp_lcd_touch_handle_t tp;
SemaphoreHandle_t intr;

void IRAM_ATTR onInt(esp_lcd_touch_handle_t) {
  BaseType_t woke = pdFALSE;
  xSemaphoreGiveFromISR(intr, &woke);
  if (woke) portYIELD_FROM_ISR();
}

void task(void*) {
  bool down = false;
  int16_t x = 0, y = 0;
  uint32_t lastReport = 0;
  for (;;) {
    const bool got = xSemaphoreTake(intr, pdMS_TO_TICKS(20)) == pdTRUE;
    const uint32_t now = millis();
    if (got) {
      esp_lcd_touch_read_data(tp);
      uint16_t px[1], py[1];
      uint8_t n = 0;
      const bool d = esp_lcd_touch_get_coordinates(tp, px, py, nullptr, &n, 1) && n > 0;
      if (d) {
        x = min<int>(px[0], LCD_W - 1), y = min<int>(py[0], LCD_H - 1);
        lastReport = now;
        touch::report(x, y, true, now);
      } else if (down) {
        touch::report(x, y, false, now);
      }
      down = d;
    } else if (down && now - lastReport > 80) {
      down = false;  // reports stopped without a release report
      touch::report(x, y, false, now);
    }
  }
}

// Through the vendor's driver, read by task() when the INT line fires.
bool begin() {
  intr = xSemaphoreCreateBinary();
  esp_lcd_panel_io_i2c_config_t ioc = {};  // vendor ESP_LCD_TOUCH_IO_I2C_ST77922_CONFIG
  ioc.dev_addr = ESP_LCD_TOUCH_IO_I2C_ST77922_ADDRESS;
  ioc.control_phase_bytes = 1;
  ioc.lcd_cmd_bits = 16;
  ioc.scl_speed_hz = ESP_LCD_TOUCH_IO_I2C_CLK_SPEED_HZ;
  ioc.flags.disable_control_phase = 1;
  esp_lcd_panel_io_handle_t tio;
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c_v2(touch::i2cBus(), &ioc, &tio));

  esp_lcd_touch_config_t tc = {};  // vendor bsp_touch_new
  tc.x_max = LCD_W;
  tc.y_max = LCD_H;
  tc.rst_gpio_num = (gpio_num_t)BOARD.input.rst;
  tc.int_gpio_num = (gpio_num_t)BOARD.input.irq;
  tc.levels.reset = 0;
  tc.levels.interrupt = 0;
  const esp_err_t err = esp_lcd_touch_new_i2c_st77922(tio, &tc, &tp);
  Serial.printf("touch: init %s\n", esp_err_to_name(err));
  if (err != ESP_OK) return false;
  esp_lcd_touch_register_interrupt_callback(tp, onInt);
  xTaskCreatePinnedToCore(task, "touch", 3072, nullptr, 5, nullptr, 0);
  return true;
}

}  // namespace

const hal::Input input = {begin};

}  // namespace es3c35p
#endif
