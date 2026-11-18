#pragma once
// Battery (ADC through a 1:2 divider) and USB power. Smoothed, updated by poll().
namespace power {
void poll();      // call often; samples at most every 200 ms
int mv();         // smoothed battery voltage, mV
int percent();    // 0..100 in steps of 5, updated every 30 s so it never flickers
bool usb();       // USB cable powering the board
bool full();      // on USB with the charger reporting full (or no battery fitted)
}  // namespace power
