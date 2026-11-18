// The 2.8" board's display (board.h ES3C28P): ILI9341V, 240x320, 4-wire SPI (+ D/C) at 40 MHz.
// The UI is the 3.5" board's 320x480: LVGL renders it in bands of 78 rows (areas widened to whole rows, aligned to 3);
// each band goes out at 2/3 (scale::down32: 213 px across, shapes kept), centred with 13 px of black each side. The
// panel takes RGB565 high byte first. The band is scaled into one of two small DMA buffers, so LVGL renders the next
// one while this one is sent (esp_lcd queues it; a command waits for what is queued before it). No tear signal on
// this board. Landscape (90/270): the panel turns itself (MADCTL row/column exchange) to 320x240 and the UI, 480x320
// there, goes out at 2/3 = 320x213 from panel row 13 (13 black rows above, 14 below): no shadow frame, no software
// turning (unlike the 3.5" board). Each landscape band is scaled in two halves of 240, so the 3 x 213 scratch is
// enough; the UI's last 2 rows (320 = 3 x 106 + 2) make one panel row. Touches need no change: the UI keeps its
// place on the panel (the FT6336 maps to the portrait UI, LVGL turns that). MADCTL directions not yet seen on a board.
// The board's hal::Display (was src/lcd_ili9341.cpp; lcd.h says what each call is for).
#ifndef ESPOS_QEMU  // QEMU: src/boards/qemu/display.cpp
#include <Arduino.h>

#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "es3c28p.h"
#include "esp_lcd_panel_io.h"
#include "scale.h"
#include "src/display/lv_display_private.h"
#include "trace.h"

namespace es3c28p {
namespace {

constexpr int LCD_W = hal::UI_W, LCD_H = hal::UI_H;  // the UI
constexpr hal::DisplayPins P = BOARD.display.pins;
constexpr int PANEL28_W = BOARD.display.panel.w, PANEL28_H = BOARD.display.panel.h;
constexpr int SHOW_W = BOARD.display.ui.w(), SHOW_X = BOARD.display.ui.x;  // the UI's 2/3 on it, and where it starts across
static_assert(BOARD.display.ui.num == 2 && BOARD.display.ui.den == 3, "this driver sends the UI at 2/3");

esp_lcd_panel_io_handle_t io;
lv_display_t* disp;
constexpr int BAND = 78;  // UI rows a band (a multiple of 3): 52 panel rows
constexpr size_t BAND_BYTES = LCD_W * BAND * 2;
constexpr int OUT_ROWS = BAND * 2 / 3;
uint8_t *band1, *band2;    // LVGL renders here (internal RAM)
uint16_t* out[2];          // a scaled band, 240 wide (DMA, internal RAM)
int outAt = 0;
scale::C3* tmp;            // 3 rows of 213
uint16_t* mirrorBuf = nullptr;
uint32_t frames = 0, flushUs = 0;
bool upside = false;       // rotation 180
bool side = false;         // landscape: 90 or 270
int sideRow0 = 13;         // landscape: the picture's first panel row (13 at 90, 14 at 270: where the portrait UI is)
int pw = PANEL28_W, ph = PANEL28_H;  // the panel's address space as MADCTL turns it

void cmd(uint8_t c, const uint8_t* d = nullptr, int n = 0) { esp_lcd_panel_io_tx_param(io, c, d, n); }

// ILI9341: the vendor's ILI9341V settings are close to these (power, VCOM, gamma left at the controller's defaults
// where the demos do not change them).
void init() {
  cmd(0x01);  // software reset
  delay(120);
  cmd(0x11);  // sleep out
  delay(120);
  const uint8_t pwrA[] = {0x39, 0x2C, 0x00, 0x34, 0x02}, pwrB[] = {0x00, 0xC1, 0x30}, timA[] = {0x85, 0x00, 0x78},
                timB[] = {0x00, 0x00}, seq[] = {0x64, 0x03, 0x12, 0x81}, pump[] = {0x20};
  cmd(0xCB, pwrA, 5), cmd(0xCF, pwrB, 3), cmd(0xE8, timA, 3), cmd(0xEA, timB, 2), cmd(0xED, seq, 4), cmd(0xF7, pump, 1);
  const uint8_t p1[] = {0x23}, p2[] = {0x10}, vc1[] = {0x3E, 0x28}, vc2[] = {0x86};
  cmd(0xC0, p1, 1), cmd(0xC1, p2, 1), cmd(0xC5, vc1, 2), cmd(0xC7, vc2, 1);
  const uint8_t madctl[] = {0x48};  // MX, BGR: portrait, the panel's colour order
  cmd(0x36, madctl, 1);
  const uint8_t fmt[] = {0x55};     // 16 bits a pixel
  cmd(0x3A, fmt, 1);
  const uint8_t frm[] = {0x00, 0x18}, dfc[] = {0x08, 0x82, 0x27}, g3[] = {0x00}, gc[] = {0x01};
  cmd(0xB1, frm, 2), cmd(0xB6, dfc, 3), cmd(0xF2, g3, 1), cmd(0x26, gc, 1);
  const uint8_t pg[] = {0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00},
                ng[] = {0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F};
  cmd(0xE0, pg, 15), cmd(0xE1, ng, 15);
  cmd(0x29);  // display on
}

void window(int x0, int y0, int x1, int y1) {
  const uint8_t ca[] = {(uint8_t)(x0 >> 8), (uint8_t)x0, (uint8_t)(x1 >> 8), (uint8_t)x1};
  const uint8_t ra[] = {(uint8_t)(y0 >> 8), (uint8_t)y0, (uint8_t)(y1 >> 8), (uint8_t)y1};
  cmd(0x2A, ca, 4), cmd(0x2B, ra, 4);
}

// Black over the whole panel (the bars beside the UI are never drawn again).
void clear() {
  memset(out[0], 0, PANEL28_W * OUT_ROWS * 2);
  const int step = PANEL28_W * OUT_ROWS / pw;  // rows of the buffer at the panel's width as turned
  for (int y = 0; y < ph; y += step) {
    const int n = min(step, ph - y);
    window(0, y, pw - 1, y + n - 1);
    esp_lcd_panel_io_tx_color(io, 0x2C, out[0], pw * n * 2);
  }
  window(0, 0, 0, 0);  // waits for the last one
}

// UI rows [y, y + rows) (native RGB565, the UI's width: 320, or 480 in landscape) to the panel at 2/3. rows is a
// multiple of 3, but for the landscape UI's last 2 rows (one panel row).
void sendRows(const uint16_t* px, int y, int rows) {
  const int lw = side ? LCD_H : LCD_W, ow = side ? PANEL28_H : SHOW_W;
  const int rows3 = rows / 3 * 3, tail = rows - rows3;
  const int py = y * 2 / 3, n = rows3 * 2 / 3 + (tail ? 1 : 0);
  uint16_t* o = out[outAt];
  outAt ^= 1;
  if (side) {
    for (int h = 0; h < 2; h++) {  // two halves of 240 -> 160
      if (rows3) scale::down32(px + h * 240, 240, rows3, lw, o + h * 160, ow, tmp);
      if (tail) scale::tail32(px + rows3 * lw + h * 240, 240, lw, o + (n - 1) * ow + h * 160, tmp);
    }
  } else {
    scale::down32(px, LCD_W, rows3, LCD_W, o, SHOW_W, tmp);
  }
  lv_draw_sw_rgb565_swap(o, ow * n);
  int x0 = side ? 0 : SHOW_X, y0 = side ? sideRow0 + py : py;
  if (upside) {  // upside down: the band reversed pixel by pixel, at the mirrored place
    for (uint16_t *a = o, *b = o + SHOW_W * n - 1; a < b; a++, b--) {
      const uint16_t t = *a;
      *a = *b, *b = t;
    }
    x0 = PANEL28_W - SHOW_X - SHOW_W, y0 = PANEL28_H - py - n;
  }
  window(x0, y0, x0 + ow - 1, y0 + n - 1);  // waits for the band before the last (its buffer is ours again)
  esp_lcd_panel_io_tx_color(io, 0x2C, o, ow * n * 2);
}

void roundArea(lv_event_t* e) {
  auto* a = (lv_area_t*)lv_event_get_param(e);
  lv_display_t* d = (lv_display_t*)lv_event_get_target(e);
  if (d->rendering_in_progress) {  // LVGL asking how many rows a band may hold: whole groups of 3 (landscape: 51)
    a->y2 = (a->y2 / 3) * 3 + 2;
    return;
  }
  a->x1 = 0;
  a->x2 = lv_display_get_horizontal_resolution(d) - 1;
  a->y1 = a->y1 / 3 * 3;
  a->y2 = min<int32_t>((a->y2 / 3) * 3 + 2, lv_display_get_vertical_resolution(d) - 1);
}

void keep(const lv_area_t* a, const uint8_t* px) {  // the remote control's copy of the UI (lcd::mirror), as turned
  if (!mirrorBuf) return;
  const int w = lv_area_get_width(a), fw = side ? LCD_H : LCD_W;
  for (int y = a->y1; y <= a->y2; y++) memcpy(mirrorBuf + y * fw + a->x1, px + (y - a->y1) * w * 2, w * 2);
}

void flush(lv_display_t* d, const lv_area_t* area, uint8_t* px) {
  const uint32_t t0 = micros();
  keep(area, px);
  sendRows((const uint16_t*)px, area->y1, lv_area_get_height(area));
  if (lv_display_flush_is_last(d)) frames++;
  flushUs += micros() - t0;
  lv_display_flush_ready(d);  // the band is scaled into our own buffer: LVGL may draw the next one now
}

void logInvalid(bool) {}

bool direct(int y0, int y1, void (*fill)(uint16_t*, int, int, void*), void* ctx, bool) {
  if (!disp || disp->rendering_in_progress || side) return false;  // portrait rows only (landscape: LVGL draws)
  const uint32_t t0 = micros();
  y0 = y0 / 3 * 3;  // whole groups of 3 rows (the status bar is 24 high: no change there)
  uint16_t* b = (uint16_t*)band1;
  for (int y = y0; y < y1; y += BAND) {
    const int rows = min(BAND, y1 - y) / 3 * 3;
    if (rows <= 0) break;
    fill(b, y, rows, ctx);
    if (mirrorBuf) memcpy(mirrorBuf + y * LCD_W, b, LCD_W * rows * 2);
    sendRows(b, y, rows);
  }
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
  w = side ? LCD_H : LCD_W, h = side ? LCD_W : LCD_H;
  return mirrorBuf;
}

void stats(uint32_t& nFrames, uint32_t& avgFlushUs) {
  nFrames = frames;
  avgFlushUs = frames ? flushUs / frames : 0;
  frames = flushUs = 0;
}

lv_display_t* begin() {
  ledcAttach(P.bl, 20000, 10);
  ledcWrite(P.bl, 0);
  spi_bus_config_t bus = {};
  bus.sclk_io_num = P.sclk;
  bus.mosi_io_num = P.d0;
  bus.miso_io_num = P.d1;
  bus.quadwp_io_num = bus.quadhd_io_num = -1;
  bus.max_transfer_sz = PANEL28_W * OUT_ROWS * 2 + 64;
  ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));
  esp_lcd_panel_io_spi_config_t ioc = {};
  ioc.cs_gpio_num = P.cs;
  ioc.dc_gpio_num = P.dc;
  ioc.spi_mode = 0;
  ioc.pclk_hz = 40 * 1000 * 1000;
  ioc.trans_queue_depth = 4;
  ioc.lcd_cmd_bits = 8;
  ioc.lcd_param_bits = 8;
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &ioc, &io));
  out[0] = (uint16_t*)heap_caps_malloc(PANEL28_W * OUT_ROWS * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  out[1] = (uint16_t*)heap_caps_malloc(PANEL28_W * OUT_ROWS * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  tmp = (scale::C3*)heap_caps_malloc(3 * SHOW_W * sizeof(scale::C3), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  init();
  clear();
  band1 = (uint8_t*)heap_caps_malloc(BAND_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  band2 = (uint8_t*)heap_caps_malloc(BAND_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  disp = lv_display_create(LCD_W, LCD_H);
  lv_display_set_buffers(disp, band1, band2, BAND_BYTES, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_add_event_cb(disp, roundArea, LV_EVENT_INVALIDATE_AREA, nullptr);
  lv_display_set_flush_cb(disp, flush);
  trace::add("lcd: ILI9341 240x320, the UI at 2/3");
  return disp;
}

void setRotation(lv_display_rotation_t r) {
  side = r == LV_DISPLAY_ROTATION_90 || r == LV_DISPLAY_ROTATION_270;
  upside = r == LV_DISPLAY_ROTATION_180;
  // Portrait: MX (as the vendor's demos). Landscape: the row/column exchange, the two ways up as LVGL's 90 and 270
  // turn the UI (90: MY MX MV; 270: MV), each keeping the UI where the portrait one is on the glass.
  const uint8_t m = r == LV_DISPLAY_ROTATION_90 ? 0xE8 : r == LV_DISPLAY_ROTATION_270 ? 0x28 : 0x48;
  cmd(0x36, &m, 1);
  sideRow0 = r == LV_DISPLAY_ROTATION_270 ? 14 : 13;
  pw = side ? PANEL28_H : PANEL28_W, ph = side ? PANEL28_W : PANEL28_H;
  clear();
  trace::add("lcd: rotation %d (%s)", (int)r, side ? "landscape, 320x213 from row 13" : "portrait");
  lv_display_set_rotation(disp, r);  // LVGL redraws everything (touch turns with it)
}

void setBacklight(int percent) {
  percent = constrain(percent, 0, 100);
  const uint32_t duty = (uint32_t)percent * percent * 1023 / 10000;  // perceived brightness ~ quadratic
  ledcWrite(P.bl, percent > 0 && duty < 4 ? 4 : duty);
}

}  // namespace

const hal::Display display = {begin, setRotation, setBacklight, stats, logInvalid, direct, mirror, frame};

}  // namespace es3c28p
#endif
