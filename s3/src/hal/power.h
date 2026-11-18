#pragma once
// HAL: power. The battery's voltage through an ADC pin and a divider (src/power.cpp: power::mv(), percent()); USB from
// the chip's USB-Serial/JTAG (power::usb()). A board without CAP_BATTERY reports a steady 85 % (QEMU).
#include <cstdint>

namespace hal {

struct PowerDesc {
  int8_t batAdc;             // the ADC pin; -1: none
  uint8_t divider;           // the battery's voltage = the pin's x this
  uint16_t emptyMv, fullMv;  // 0 % and 100 %
};

}  // namespace hal
