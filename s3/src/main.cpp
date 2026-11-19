// EspOS for the ES3C35P: step 1, the base (display, touch, fonts, UI shell).
#include <Arduino.h>
#include <lvgl.h>

#include "esp_heap_caps.h"

#include "board.h"
#include "devtools.h"
#include "diag.h"
#include "fonts.h"
#include "lcd.h"
#include "desk.h"
#include "net.h"
#include "ota.h"
#include "power.h"
#include "remote.h"
#include "sound.h"
#include "touch.h"
#include "trace.h"
#include "os.h"
#include "led.h"
#include "wake/wake.h"
#ifndef NO_PET
#include "pet/talk.h"
#endif

// The loop task (UI, LVGL, sync answers) needs about 6.8 KB at boot on the board (measured 2026-11-17 with
// uxTaskGetStackHighWaterMark); the default 8 KB left too little room. Its stack is internal RAM (it writes flash).
// "loop stack: N B never used" every 10 s (and diag loop_stack_free) shows the margin.
SET_LOOP_TASK_STACK_SIZE(12 * 1024);

static void lvLog(lv_log_level_t, const char* msg) {
  Serial.print(msg);
}
static uint32_t tickMs() { return millis(); }

void setup() {
  // Ordinary malloc/new/String of 64 bytes or more goes to PSRAM (the build's default kept everything under
  // 4 KB internal). Internal RAM (~40 KB free while running) is what WiFi, DMA, TLS's AES engine, the SD card
  // and task stacks must have, and they ask for it by name. Measured on the board 2026-11-17: News left 13 KB
  // of headlines in internal RAM, and a sweep of the apps took it from 42 KB to 7 KB (SD reads and TLS failed).
  heap_caps_malloc_extmem_enable(64);
  Serial.setRxBufferSize(2048);  // provisioning lines are long
  Serial.begin(115200);
  // Logs must never stall the UI. With the cable in a computer but no program reading the port,
  // each print otherwise blocks for 20 x 100 ms once the buffer is full (the touch went dead).
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);
#endif
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 1500) delay(10);
  Serial.printf("\n%s boot, PSRAM %u KB free, heap %u KB\n", FW_VERSION, ESP.getFreePsram() / 1024,
                ESP.getFreeHeap() / 1024);
  trace::begin();
  static const char* RESETS[] = {"unknown", "power on", "ext pin", "software", "PANIC", "int watchdog",
                                 "task watchdog", "watchdog", "deep sleep", "brownout", "sdio", "usb", "jtag",
                                 "efuse", "power glitch", "cpu lockup"};
  const int rr = esp_reset_reason();
  trace::add("boot %s, reset: %s", FW_VERSION, rr < 16 ? RESETS[rr] : "other");
  board::detect();  // which board (board.h), before any driver takes a pin
  Serial.printf("board: %s\n", board::label());
  diag::set("reset", rr);
  lv_init();
  lv_tick_set_cb(tickMs);
  lv_log_register_print_cb(lvLog);
  auto heap = [](const char* stage) {
    trace::add("heap after %s: %u KB internal (largest %u KB)", stage, heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024,
               heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024);
  };
  heap("start");
  lv_display_t* disp = lcd::begin();
  heap("display");
  if (!touch::begin(disp)) Serial.println("touch: controller not ready");
  fonts::begin();
  power::poll();
  desk::begin();  // settings and the cached dashboard, before the UI is built from them
  ota::begin();
  if (ota::installing()) {
    // Install mode (ota.cpp): only the install screen and WiFi, so the download has the internal RAM.
    ota::beginInstallScreen();
    lcd::setBacklight(70);
    net::begin();
    heap("install mode");
    return;
  }
  os::begin(disp);
  heap("ui");
  lcd::setBacklight(70);
  net::begin();
  heap("wifi");
  remote::begin();
  sound::begin();
  sound::setVolume(desk::settingInt("s3.volume", 45));
  led::begin();   // the RGB LED: the dog assistant's states, arrivals
  wake::begin();  // the mic's listener: the "Gâu" wake word (when turned on) and the dog assistant's recordings
#ifndef NO_PET
  pet::talk::begin();
#endif
}

static uint32_t handlerUs = 0;

void loop() {
  static uint32_t lastStat = 0;
  const uint32_t h0 = micros();
  const uint32_t wait = lv_timer_handler();
  const uint32_t took = micros() - h0;
  handlerUs += took;
  if (took > 120000) {
    trace::add("slow loop %lu ms", took / 1000);  // touch is not read meanwhile
    diag::inc("slow");
  }
  diag::maxOf("loop_max_ms", took / 1000);
  power::poll();
  if (ota::installing()) {
    net::poll();
    ota::poll();
    delay(wait < 5 ? wait : 5);
    return;
  }
  os::tick();
  const uint32_t n0 = millis();
  net::poll();
  if (millis() - n0 > 80) trace::add("slow wifi poll %lu ms", millis() - n0);
  desk::poll();
  wake::tick();
  led::tick();
  ota::poll();
  devtools::poll();
  remote::poll();
  if (millis() - lastStat > 10000) {
    lastStat = millis();
    uint32_t frames, flushUs;
    lcd::stats(frames, flushUs);
    Serial.printf("heap %u KB (min %u), PSRAM %u KB, %u frames in 10 s, send %u us/frame (core 0), lvgl %u us/frame (core 1)\n",
                  ESP.getFreeHeap() / 1024, ESP.getMinFreeHeap() / 1024, ESP.getFreePsram() / 1024, frames, flushUs,
                  frames ? handlerUs / frames : 0);
    diag::set("fps10", frames);
    diag::maxOf("fps10_max", frames);
    diag::set("send_us", flushUs);
    diag::set("lvgl_us", frames ? handlerUs / frames : 0);
    diag::set("heap_kb", ESP.getFreeHeap() / 1024);
    diag::set("heap_min_kb", ESP.getMinFreeHeap() / 1024);
    diag::set("internal_kb", heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
    diag::set("largest_kb", heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024);
    diag::set("psram_kb", ESP.getFreePsram() / 1024);
    diag::set("up_min", millis() / 60000);
    // The loop task's deepest use so far (its stack is internal RAM; a stack canary hit resets the board).
    const uint32_t loopFree = uxTaskGetStackHighWaterMark(nullptr);
    diag::set("loop_stack_free", loopFree);
    Serial.printf("loop stack: %u B never used\n", (unsigned)loopFree);
    handlerUs = 0;
  }
  delay(wait < 5 ? wait : 5);
}
