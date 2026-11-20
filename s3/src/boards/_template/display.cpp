// TEMPLATE: the panel driver (hal::Display, src/hal/display.h), for a 320x480 SPI panel with a D/C line (ST7796,
// ILI9488 class). It compiles and shows the shape every driver has; fill in the TODOs from your panel's datasheet or
// the vendor's demo. Working drivers to copy from:
//   src/boards/es3c35p/display.cpp  QSPI, tear sync, all four rotations, the fastest path (DMA while LVGL renders)
//   src/boards/es3c28p/display.cpp  SPI + D/C, a 240x320 panel showing the UI at 2/3 (src/scale.h)
//
// The rules every driver keeps (the OS counts on them):
//   - LVGL draws the UI (BOARD.display.ui: 320x480) in RGB565, in bands, into two buffers in internal RAM (DMA
//     capable); flush() sends a band and calls lv_display_flush_ready() once the buffer may be drawn into again.
//     Internal RAM is tight: two bands of 320 x 80 rows are 100 KB; 40 rows (50 KB) is a fair start.
//   - Areas LVGL redraws can be widened in an LV_EVENT_INVALIDATE_AREA callback (whole rows: some panels flicker on
//     narrow windows; rows in 3s for a 2/3 scale).
//   - direct(): the run game sends its pictures through it (lcd::direct) in rows, past LVGL. Return false and the game
//     draws through LVGL instead (slower but correct); a first port can leave it so.
//   - mirror()/frame(): the remote viewer's copy of the UI (PSRAM, 300 KB): keep every band you send in it while on.
//   - begin() leaves the backlight off: the OS turns it on (lcd::setBacklight(70)) once the first screen is drawn.
#ifndef ESPOS_QEMU  // QEMU builds use src/boards/qemu/display.cpp
#include <Arduino.h>

#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "myboard.h"

namespace myboard {
namespace {

constexpr hal::DisplayPins P = BOARD.display.pins;
constexpr int W = BOARD.display.ui.w, H = BOARD.display.ui.h;
constexpr int BAND = 40;  // rows LVGL renders at a time
constexpr size_t BAND_BYTES = W * BAND * 2;

esp_lcd_panel_io_handle_t io;
lv_display_t* disp;
uint8_t *band1, *band2;
uint16_t* mirrorBuf = nullptr;
uint32_t frames = 0, flushUs = 0;

void cmd(uint8_t c, const uint8_t* d = nullptr, int n = 0) { esp_lcd_panel_io_tx_param(io, c, d, n); }

// TODO: your panel's init sequence (power, gamma, memory access order for portrait and its colour order).
void init() {
  cmd(0x01);  // software reset
  delay(120);
  cmd(0x11);  // sleep out
  delay(120);
  const uint8_t madctl[] = {0x48};  // TODO: memory access control: portrait, RGB or BGR
  cmd(0x36, madctl, 1);
  const uint8_t fmt[] = {0x55};  // 16 bits a pixel
  cmd(0x3A, fmt, 1);
  cmd(0x29);  // display on
}

void window(int x0, int y0, int x1, int y1) {
  const uint8_t ca[] = {(uint8_t)(x0 >> 8), (uint8_t)x0, (uint8_t)(x1 >> 8), (uint8_t)x1};
  const uint8_t ra[] = {(uint8_t)(y0 >> 8), (uint8_t)y0, (uint8_t)(y1 >> 8), (uint8_t)y1};
  cmd(0x2A, ca, 4), cmd(0x2B, ra, 4);
}

// The band has gone out by DMA: LVGL may draw into its buffer again.
bool onSent(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void*) {
  lv_display_flush_ready(disp);
  return false;
}

void flush(lv_display_t* d, const lv_area_t* a, uint8_t* px) {
  const uint32_t t0 = micros();
  const int w = lv_area_get_width(a), h = lv_area_get_height(a);
  if (mirrorBuf)
    for (int y = 0; y < h; y++) memcpy(mirrorBuf + (a->y1 + y) * W + a->x1, px + y * w * 2, w * 2);
  lv_draw_sw_rgb565_swap(px, w * h);  // most SPI panels take the high byte first
  window(a->x1, a->y1, a->x2, a->y2);
  esp_lcd_panel_io_tx_color(io, 0x2C, px, w * h * 2);  // queued; onSent() frees the buffer
  if (lv_display_flush_is_last(d)) frames++;
  flushUs += micros() - t0;
}

lv_display_t* begin() {
  ledcAttach(P.bl, 20000, 10);  // the backlight by PWM, off until the OS turns it on
  ledcWrite(P.bl, 0);
  spi_bus_config_t bus = {};
  bus.sclk_io_num = P.sclk;
  bus.mosi_io_num = P.d0;
  bus.miso_io_num = P.d1;
  bus.quadwp_io_num = bus.quadhd_io_num = -1;
  bus.max_transfer_sz = BAND_BYTES;
  ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));
  esp_lcd_panel_io_spi_config_t ioc = {};
  ioc.cs_gpio_num = P.cs;
  ioc.dc_gpio_num = P.dc;
  ioc.spi_mode = 0;
  ioc.pclk_hz = 40 * 1000 * 1000;  // TODO: what your panel and wiring take
  ioc.trans_queue_depth = 4;
  ioc.on_color_trans_done = onSent;
  ioc.lcd_cmd_bits = 8;
  ioc.lcd_param_bits = 8;
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &ioc, &io));
  init();
  band1 = (uint8_t*)heap_caps_malloc(BAND_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  band2 = (uint8_t*)heap_caps_malloc(BAND_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  disp = lv_display_create(W, H);
  lv_display_set_buffers(disp, band1, band2, BAND_BYTES, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(disp, flush);
  return disp;
}

void setRotation(lv_display_rotation_t r) {
  // TODO: the turns your panel can do (PanelDesc::rotations); this one stays portrait.
  (void)r;
  lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_0);
}

void setBacklight(int percent) {
  percent = constrain(percent, 0, 100);
  const uint32_t duty = (uint32_t)percent * percent * 1023 / 10000;  // perceived brightness ~ quadratic in duty
  ledcWrite(P.bl, percent > 0 && duty < 4 ? 4 : duty);
}

void stats(uint32_t& nFrames, uint32_t& avgFlushUs) {
  nFrames = frames;
  avgFlushUs = frames ? flushUs / frames : 0;
  frames = flushUs = 0;
}

void logInvalid(bool) {}

// TODO (optional): the run game's fast path. false: it draws through LVGL.
bool direct(int, int, void (*)(uint16_t*, int, int, void*), void*, bool) { return false; }

void mirror(bool on) {
  if (on == (mirrorBuf != nullptr)) return;
  if (on) {
    mirrorBuf = (uint16_t*)heap_caps_malloc(W * H * 2, MALLOC_CAP_SPIRAM);
    if (mirrorBuf && disp) lv_obj_invalidate(lv_screen_active()), lv_obj_invalidate(lv_layer_top());
  } else {
    heap_caps_free(mirrorBuf);
    mirrorBuf = nullptr;
  }
}

const uint16_t* frame(int& w, int& h) {
  w = W, h = H;
  return mirrorBuf;
}

}  // namespace

// What lcd:: calls (the order of hal::Display's fields).
const hal::Display display = {begin, setRotation, setBacklight, stats, logInvalid, direct, mirror, frame};

}  // namespace myboard
#endif
