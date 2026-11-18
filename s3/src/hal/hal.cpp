// HAL helpers: the board's I2C bus, and the I2C probe for the boards' probe() (src/boards/*/bsp.cpp). Not in the
// simulator.
#if !defined(SIM)
#include <Arduino.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "hal/board.h"

namespace hal {

namespace {
i2c_master_bus_handle_t bus;
}  // namespace

// The vendor's bsp_i2c_init, on the board's own pins. The touch driver asks first (touch::i2cBus()), the codec after.
i2c_master_bus_handle_t i2c() {
  if (bus) return bus;
  i2c_master_bus_config_t bc = {};
  bc.i2c_port = I2C_NUM_0;
  bc.sda_io_num = (gpio_num_t)board().i2c.sda;
  bc.scl_io_num = (gpio_num_t)board().i2c.scl;
  bc.clk_source = I2C_CLK_SRC_DEFAULT;
  bc.glitch_ignore_cnt = 7;
  bc.flags.enable_internal_pullup = true;
  ESP_ERROR_CHECK(i2c_new_master_bus(&bc, &bus));
  return bus;
}

uint8_t i2cAnswer(int sda, int scl, std::initializer_list<uint8_t> addrs, uint32_t forMs) {
  i2c_master_bus_config_t bc = {};
  bc.i2c_port = I2C_NUM_0;
  bc.sda_io_num = (gpio_num_t)sda;
  bc.scl_io_num = (gpio_num_t)scl;
  bc.clk_source = I2C_CLK_SRC_DEFAULT;
  bc.glitch_ignore_cnt = 7;
  bc.flags.enable_internal_pullup = true;
  i2c_master_bus_handle_t bus;
  if (i2c_new_master_bus(&bc, &bus) != ESP_OK) return 0;
  uint8_t found = 0;
  const uint32_t t0 = millis();
  do {
    for (uint8_t a : addrs)
      if (i2c_master_probe(bus, a, 20) == ESP_OK) {
        found = a;
        break;
      }
    if (!found && forMs) delay(25);
  } while (!found && millis() - t0 < forMs);
  i2c_del_master_bus(bus);
  gpio_reset_pin((gpio_num_t)sda);
  gpio_reset_pin((gpio_num_t)scl);
  return found;
}

}  // namespace hal
#endif
