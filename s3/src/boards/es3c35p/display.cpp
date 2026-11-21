// Display path copied from the vendor's LVGL demo (LCDWIKI ES3C35P, Example_08_LVGL_Demos):
// esp_lcd QSPI panel IO + the vendor ST77922 panel driver, a full-screen LVGL buffer in PSRAM,
// two 1/10-screen DMA buffers that the flush copies into (rotating there), and tear-effect
// sync on GPIO42 before each frame. The board's hal::Display (was src/lcd.cpp; lcd.h says what each call is for).
#ifndef ESPOS_QEMU  // QEMU: src/boards/qemu/display.cpp

#include <Arduino.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "es3c35p.h"
#include "lcd.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "vendor/esp_lcd_st77922.h"
#include "src/display/lv_display_private.h"

extern "C" const st77922_lcd_init_cmd_t st77922_vendor_init_cmds[];
extern "C" const unsigned st77922_vendor_init_cmds_size;

namespace es3c35p {
namespace {

constexpr int LCD_W = BOARD.display.ui.w, LCD_H = BOARD.display.ui.h;  // the UI (the panel's own size here)
constexpr hal::DisplayPins P = BOARD.display.pins;
constexpr int TE_PIN = P.te;
constexpr int TVDL_MS = 13;                    // vendor BSP_SYNC_TASK_CONFIG

esp_lcd_panel_io_handle_t io;
esp_lcd_panel_handle_t panel;
lv_display_t* disp;
SemaphoreHandle_t teCatch, teVsync, bandSent;

bool IRAM_ATTR onColorDone(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void*) {
  BaseType_t woke = pdFALSE;
  xSemaphoreGiveFromISR(bandSent, &woke);
  return woke == pdTRUE;
}

void IRAM_ATTR onTear(void*) {
  BaseType_t woke = pdFALSE;
  xSemaphoreGiveFromISR(teVsync, &woke);
  if (woke) portYIELD_FROM_ISR();
}

// Vendor bsp_display_sync_task: drop a stale vsync when nobody waited for it in time.
void syncTask(void*) {
  for (;;) {
    if (xSemaphoreTake(teCatch, pdMS_TO_TICKS(TVDL_MS)) != pdPASS) xSemaphoreTake(teVsync, 0);
  }
}

// Vendor bsp_display_sync_cb, with a timeout so a missing TE signal can never freeze the UI.
void waitTear() {
  xSemaphoreGive(teCatch);
  xSemaphoreTake(teVsync, pdMS_TO_TICKS(40));
}

constexpr int BAND = 80;  // rows per band (portrait); LVGL renders each band in internal RAM
constexpr size_t BAND_BYTES = LCD_W * BAND * 2;  // 62.5 KB
uint8_t *band1, *band2;
// Landscape: the panel only takes windows spanning its full width (narrow ones break up), but
// a rotated LVGL band is a narrow vertical strip of the panel. So in landscape every band is
// copied into a whole logical frame in PSRAM, and once the refresh is complete the changed
// columns are rotated out of it in blocks that each cover the full panel width.
uint16_t* shadow;               // logical frame, landscape only (300 KB PSRAM)
int32_t dirtyX1 = INT32_MAX, dirtyX2 = -1;
bool newFrame = true;
uint32_t frames = 0, flushUs = 0;
bool logInv = false;
uint16_t* mirrorBuf = nullptr;  // the logical portrait frame for remote viewing (landscape: `shadow` is one)

void keep(const lv_area_t* a, const uint8_t* px) {  // a portrait band, before it is swapped for the panel
  if (!mirrorBuf) return;
  const int w = lv_area_get_width(a);
  for (int y = a->y1; y <= a->y2; y++) memcpy(mirrorBuf + y * LCD_W + a->x1, px + (y - a->y1) * w * 2, w * 2);
}

// The panel only accepts windows that span its full width (narrow windows flicker), so every
// invalidated area is widened to whole panel rows, aligned to 2 rows. In landscape the shadow
// frame takes care of the width; areas are only aligned to 2 logical columns (= panel rows).
void roundArea(lv_event_t* e) {
  auto* a = (lv_area_t*)lv_event_get_param(e);
  lv_display_t* d = (lv_display_t*)lv_event_get_target(e);
  if (d->rendering_in_progress) return;  // LVGL probing the band height, not an invalidation
  if (logInv) Serial.printf("inv %ld,%ld %ldx%ld\n", a->x1, a->y1, lv_area_get_width(a), lv_area_get_height(a));
  const lv_display_rotation_t rot = lv_display_get_rotation(d);
  const int32_t w = lv_display_get_horizontal_resolution(d), h = lv_display_get_vertical_resolution(d);
  if (rot == LV_DISPLAY_ROTATION_90 || rot == LV_DISPLAY_ROTATION_270) {
    a->x1 &= ~1;
    a->x2 = min<int32_t>(a->x2 | 1, w - 1);
  } else {
    a->x1 = 0;
    a->x2 = w - 1;
    a->y1 &= ~1;
    a->y2 = min<int32_t>(a->y2 | 1, h - 1);
  }
}

void sendLandscape(lv_display_t* d, lv_display_rotation_t rot);

void flush(lv_display_t* d, const lv_area_t* area, uint8_t* px) {
  const uint32_t t0 = micros();
  const lv_display_rotation_t rot = lv_display_get_rotation(d);
  const int w = lv_area_get_width(area), h = lv_area_get_height(area);
  if (rot == LV_DISPLAY_ROTATION_0 || rot == LV_DISPLAY_ROTATION_180) keep(area, px);
  lv_draw_sw_rgb565_swap(px, w * h);  // panel takes RGB565 high byte first
  if (rot == LV_DISPLAY_ROTATION_90 || rot == LV_DISPLAY_ROTATION_270) {
    const int fw = lv_display_get_horizontal_resolution(d);
    for (int y = 0; y < h; y++)
      memcpy(shadow + (area->y1 + y) * fw + area->x1, px + y * w * 2, w * 2);
    dirtyX1 = min(dirtyX1, area->x1);
    dirtyX2 = max(dirtyX2, area->x2);
    if (lv_display_flush_is_last(d)) sendLandscape(d, rot);
    flushUs += micros() - t0;
    lv_display_flush_ready(d);  // nothing in flight: both band buffers are free again
    return;
  }
  if (rot == LV_DISPLAY_ROTATION_180) {
    // Upside down is the band reversed pixel by pixel: done in place, no extra buffer.
    uint16_t *a = (uint16_t*)px, *b = a + w * h - 1;
    while (a < b) {
      const uint16_t t = *a;
      *a++ = *b;
      *b-- = t;
    }
  }
  lv_area_t a = *area;
  lv_display_rotate_area(d, &a);
  if (newFrame) waitTear();
  newFrame = lv_display_flush_is_last(d);
  esp_lcd_panel_draw_bitmap(panel, a.x1, a.y1, a.x2 + 1, a.y2 + 1, px);
  flushUs += micros() - t0;
  if (newFrame) frames++;
}

// Called at the end of a landscape refresh, when LVGL has handed over its last band: both band
// buffers are free, so they serve as ping-pong DMA buffers for the rotated blocks.
void sendLandscape(lv_display_t* d, lv_display_rotation_t rot) {
  const int fw = lv_display_get_horizontal_resolution(d), fh = lv_display_get_vertical_resolution(d);
  const int step = (BAND_BYTES / (fh * 2)) & ~1;  // logical columns per block (100)
  uint8_t* bufs[2] = {band1, band2};
  int inFlight = 0, n = 0;
  waitTear();
  for (int x = dirtyX1 & ~1; x <= dirtyX2; x += step, n++) {
    const int cols = min(step, fw - x);
    if (inFlight == 2) xSemaphoreTake(bandSent, portMAX_DELAY), inFlight--;
    uint8_t* out = bufs[n & 1];
    lv_draw_sw_rotate(shadow + x, out, cols, fh, fw * 2, fh * 2, rot, LV_COLOR_FORMAT_RGB565);
    lv_area_t a = {x, 0, x + cols - 1, fh - 1};
    lv_display_rotate_area(d, &a);
    esp_lcd_panel_draw_bitmap(panel, a.x1, a.y1, a.x2 + 1, a.y2 + 1, out);
    inFlight++;
  }
  while (inFlight--) xSemaphoreTake(bandSent, portMAX_DELAY);
  dirtyX1 = INT32_MAX;
  dirtyX2 = -1;
  frames++;
}

// LVGL renders the next band into the other buffer while DMA sends this one; it waits here
// before reusing a buffer.
void flushWait(lv_display_t*) { xSemaphoreTake(bandSent, portMAX_DELAY); }

void logInvalid(bool on) { logInv = on; }

bool direct(int y0, int y1, void (*fill)(uint16_t*, int, int, void*), void* ctx, bool tear) {
  if (!disp || disp->rendering_in_progress) return false;
  const lv_display_rotation_t rot = lv_display_get_rotation(disp);
  if (rot != LV_DISPLAY_ROTATION_0 && rot != LV_DISPLAY_ROTATION_180) return false;
  if (disp->flushing) {  // LVGL's last band is still on its way: wait for it as LVGL would
    flushWait(disp);
    disp->flushing = 0;
  }
  const uint32_t t0 = micros();
  if (tear) waitTear();
  uint8_t* bufs[2] = {band1, band2};
  int inFlight = 0, n = 0;
  for (int y = y0; y < y1; y += BAND, n++) {
    const int rows = min(BAND, y1 - y);
    if (inFlight == 2) xSemaphoreTake(bandSent, portMAX_DELAY), inFlight--;  // the buffer's last DMA is done
    uint16_t* out = (uint16_t*)bufs[n & 1];
    fill(out, y, rows, ctx);
    if (mirrorBuf) memcpy(mirrorBuf + y * LCD_W, out, LCD_W * rows * 2);
    lv_draw_sw_rgb565_swap(out, LCD_W * rows);
    if (rot == LV_DISPLAY_ROTATION_180) {  // as flush(): the band reversed pixel by pixel
      uint16_t *a = out, *b = out + LCD_W * rows - 1;
      while (a < b) {
        const uint16_t t = *a;
        *a++ = *b;
        *b-- = t;
      }
    }
    lv_area_t a = {0, y, LCD_W - 1, y + rows - 1};
    lv_display_rotate_area(disp, &a);
    esp_lcd_panel_draw_bitmap(panel, a.x1, a.y1, a.x2 + 1, a.y2 + 1, out);
    inFlight++;
  }
  while (inFlight-- > 0) xSemaphoreTake(bandSent, portMAX_DELAY);
  newFrame = true;
  flushUs += micros() - t0;
  frames++;
  return true;
}

// lcd::native: the whole screen in pieces past LVGL. Upright: 80-row bands, as direct(). On its side: blocks of
// logical columns that are whole panel rows, filled straight in the panel's order (the strides do what
// lv_draw_sw_rotate does in sendLandscape: 90 = pixel (x+i, j) at (cols-1-i) * 320 + j, 270 = i * 320 + 319 - j), so
// there is no shadow frame and no turning pass. The remote viewer's copy is kept upright only.
bool native(void (*fill)(uint16_t*, int, int, int, int, int, int, void*), void* ctx, bool tear) {
  if (!disp || disp->rendering_in_progress) return false;
  const lv_display_rotation_t rot = lv_display_get_rotation(disp);
  if (disp->flushing) {
    flushWait(disp);
    disp->flushing = 0;
  }
  const uint32_t t0 = micros();
  if (tear) waitTear();
  uint8_t* bufs[2] = {band1, band2};
  int inFlight = 0, n = 0;
  const bool side = rot == LV_DISPLAY_ROTATION_90 || rot == LV_DISPLAY_ROTATION_270;
  const int fw = side ? LCD_H : LCD_W, fh = side ? LCD_W : LCD_H;
  const int step = side ? (int)(BAND_BYTES / (fh * 2)) & ~1 : BAND;  // columns (side) or rows a piece
  for (int at = 0; at < (side ? fw : fh); at += step, n++) {
    const int k = min(step, (side ? fw : fh) - at);
    if (inFlight == 2) xSemaphoreTake(bandSent, portMAX_DELAY), inFlight--;
    uint16_t* out = (uint16_t*)bufs[n & 1];
    lv_area_t a;
    if (side) {
      if (rot == LV_DISPLAY_ROTATION_90) fill(out + (k - 1) * fh, -fh, 1, at, 0, at + k, fh, ctx);
      else fill(out + fh - 1, fh, -1, at, 0, at + k, fh, ctx);
      a = {at, 0, at + k - 1, fh - 1};
    } else {
      if (rot == LV_DISPLAY_ROTATION_180) fill(out + LCD_W * k - 1, -1, -LCD_W, 0, at, LCD_W, at + k, ctx);
      else {
        fill(out, 1, LCD_W, 0, at, LCD_W, at + k, ctx);
        if (mirrorBuf) memcpy(mirrorBuf + at * LCD_W, out, LCD_W * k * 2);
      }
      a = {0, at, LCD_W - 1, at + k - 1};
    }
    lv_draw_sw_rgb565_swap(out, LCD_W * k);  // the same pixel count either way (k panel rows of 320)
    lv_display_rotate_area(disp, &a);
    esp_lcd_panel_draw_bitmap(panel, a.x1, a.y1, a.x2 + 1, a.y2 + 1, out);
    inFlight++;
  }
  while (inFlight-- > 0) xSemaphoreTake(bandSent, portMAX_DELAY);
  newFrame = true;
  flushUs += micros() - t0;
  frames++;
  return true;
}

void mirror(bool on) {
  if (on == (mirrorBuf != nullptr)) return;
  if (on) {
    mirrorBuf = (uint16_t*)heap_caps_malloc(LCD_W * LCD_H * 2, MALLOC_CAP_SPIRAM);
    if (mirrorBuf && disp) lv_obj_invalidate(lv_screen_active()), lv_obj_invalidate(lv_layer_top());
  } else {
    heap_caps_free(mirrorBuf);
    mirrorBuf = nullptr;
  }
}

const uint16_t* frame(int& w, int& h) {
  const lv_display_rotation_t rot = disp ? lv_display_get_rotation(disp) : LV_DISPLAY_ROTATION_0;
  const bool side = rot == LV_DISPLAY_ROTATION_90 || rot == LV_DISPLAY_ROTATION_270;
  w = side ? LCD_H : LCD_W, h = side ? LCD_W : LCD_H;
  return !mirrorBuf ? nullptr : side ? shadow : mirrorBuf;
}

void stats(uint32_t& nFrames, uint32_t& avgFlushUs) {
  nFrames = frames;
  avgFlushUs = frames ? flushUs / frames : 0;
  frames = flushUs = 0;
}

lv_display_t* begin() {
  ledcAttach(P.bl, 20000, 10);
  ledcWrite(P.bl, 0);
  teCatch = xSemaphoreCreateCounting(1, 0);
  teVsync = xSemaphoreCreateCounting(1, 0);

  spi_bus_config_t bus = {};
  bus.sclk_io_num = P.sclk;
  bus.data0_io_num = P.d0;
  bus.data1_io_num = P.d1;
  bus.data2_io_num = P.d2;
  bus.data3_io_num = P.d3;
  bus.max_transfer_sz = LCD_W * LCD_H * 2;  // vendor: max_transfer_sz = hres * vres * 2
  ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));

  esp_lcd_panel_io_spi_config_t ioc = {};  // vendor ST77922_PANEL_IO_QSPI_CONFIG
  ioc.cs_gpio_num = P.cs;
  ioc.dc_gpio_num = -1;
  ioc.spi_mode = 0;
  ioc.pclk_hz = 80 * 1000 * 1000;
  ioc.trans_queue_depth = 10;
  ioc.on_color_trans_done = onColorDone;
  ioc.lcd_cmd_bits = 32;
  ioc.lcd_param_bits = 8;
  ioc.flags.quad_mode = true;
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &ioc, &io));

  st77922_vendor_config_t vendor = {};
  vendor.init_cmds = st77922_vendor_init_cmds;
  vendor.init_cmds_size = st77922_vendor_init_cmds_size;
  vendor.flags.use_qspi_interface = 1;
  esp_lcd_panel_dev_config_t pc = {};
  pc.reset_gpio_num = -1;
  pc.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
  pc.bits_per_pixel = 16;
  pc.vendor_config = &vendor;
  ESP_ERROR_CHECK(esp_lcd_new_panel_st77922(io, &pc, &panel));
  esp_lcd_panel_reset(panel);
  esp_lcd_panel_init(panel);
  esp_lcd_panel_disp_on_off(panel, true);

  // Tear-effect sync (vendor: GPIO42, any edge, pull-up).
  gpio_config_t te = {};
  te.intr_type = GPIO_INTR_ANYEDGE;
  te.mode = GPIO_MODE_INPUT;
  te.pin_bit_mask = 1ULL << TE_PIN;
  te.pull_up_en = GPIO_PULLUP_ENABLE;
  gpio_config(&te);
  gpio_install_isr_service(0);
  gpio_isr_handler_add((gpio_num_t)TE_PIN, onTear, nullptr);
  xTaskCreate(syncTask, "tear", 2048, nullptr, 4, nullptr);

  band1 = (uint8_t*)heap_caps_malloc(BAND_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  band2 = (uint8_t*)heap_caps_malloc(BAND_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  bandSent = xSemaphoreCreateCounting(8, 0);  // one give per finished DMA block
  disp = lv_display_create(LCD_W, LCD_H);
  lv_display_set_buffers(disp, band1, band2, BAND_BYTES, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_add_event_cb(disp, roundArea, LV_EVENT_INVALIDATE_AREA, nullptr);
  lv_display_set_flush_cb(disp, flush);
  lv_display_set_flush_wait_cb(disp, flushWait);
  return disp;
}

void setRotation(lv_display_rotation_t r) {
  // Upside down (180, and 270 = 90 upside down) is the panel's own mirrored scan (MADCTL MX+MY: no cost); LVGL, the
  // games and the aquarium then draw as at 0 / 90 (lcd.h flipped()). The software 180 turn made the aquarium saver
  // take 160 ms a loop on a test board (2026-11-20).
  const bool flip = r == LV_DISPLAY_ROTATION_180 || r == LV_DISPLAY_ROTATION_270;
  if (flip) r = r == LV_DISPLAY_ROTATION_180 ? LV_DISPLAY_ROTATION_0 : LV_DISPLAY_ROTATION_90;
  esp_lcd_panel_mirror(panel, flip, flip);
  lcd::setFlipped(flip);
  const bool side = r == LV_DISPLAY_ROTATION_90 || r == LV_DISPLAY_ROTATION_270;
  if (side && !shadow) {
    shadow = (uint16_t*)heap_caps_malloc(LCD_W * LCD_H * 2, MALLOC_CAP_SPIRAM);
    if (!shadow) {  // no room: stay as we are rather than crash
      Serial.println("lcd: no PSRAM for landscape");
      return;
    }
  } else if (!side && shadow) {
    heap_caps_free(shadow);
    shadow = nullptr;
  }
  dirtyX1 = INT32_MAX;
  dirtyX2 = -1;
  lv_display_set_rotation(disp, r);  // LVGL redraws everything, which fills the shadow frame
}

void setBacklight(int percent) {
  percent = constrain(percent, 0, 100);
  // Perceived brightness is roughly quadratic in duty.
  const uint32_t duty = (uint32_t)percent * percent * 1023 / 10000;
  ledcWrite(P.bl, percent > 0 && duty < 4 ? 4 : duty);
}

}  // namespace

const hal::Display display = {begin, setRotation, setBacklight, stats, logInvalid, direct, mirror, frame, native};

}  // namespace es3c35p
#endif  // ESPOS_QEMU
