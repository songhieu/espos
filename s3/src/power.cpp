#include "power.h"

#include <Arduino.h>

#include "driver/usb_serial_jtag.h"
#include "hal/board.h"

namespace power {
namespace {
float avg = 0;
int pct = -1;
uint32_t lastSample = 0, lastPct = 0;
}  // namespace

void poll() {
  if (!hal::has(hal::CAP_BATTERY)) {  // no battery to read (QEMU)
    avg = 4020, pct = 85;
    return;
  }
  if (avg != 0 && millis() - lastSample < 200) return;
  lastSample = millis();
  const hal::PowerDesc& p = hal::board().power;  // the battery through a divider (1:2 on both boards)
  uint32_t sum = 0;
  for (int i = 0; i < 4; i++) sum += analogReadMilliVolts(p.batAdc);
  const float v = sum / 4.0f * p.divider;
  avg = avg == 0 ? v : avg * 0.95f + v * 0.05f;
  if (pct < 0 || millis() - lastPct > 30000) {
    lastPct = millis();
    pct = constrain(((int)avg - p.emptyMv) * 100 / (p.fullMv - p.emptyMv), 0, 100) / 5 * 5;
  }
}

int mv() { return (int)avg; }
int percent() { return pct < 0 ? 0 : pct; }
bool usb() { return usb_serial_jtag_is_connected(); }
bool full() { return usb() && avg > 4100; }

}  // namespace power
