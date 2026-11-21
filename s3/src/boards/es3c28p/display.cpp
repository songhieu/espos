// The 2.8" board's display (board.h ES3C28P): ILI9341V, 240x320, 4-wire SPI (+ D/C) at 40 MHz.
// The UI is 360x480 (the descriptor's, 2026-11-20; it was the 3.5" board's 320x480 at 213 px with black bars each
// side): LVGL renders it in bands of 69 rows (areas widened to whole rows, aligned to 3); each band goes out at 2/3
// (scale::down32: 240 px across, the whole panel, shapes kept). The panel takes RGB565 high byte first. The band is
// scaled into one of two small DMA buffers, so LVGL renders the next one while this one is sent (esp_lcd queues it; a
// command waits for what is queued before it). No tear signal on this board. Landscape (90/270): the panel turns
// itself (MADCTL row/column exchange) to 320x240 and the UI, 480x360 there, fills it at 2/3: no shadow frame, no
// software turning (unlike the 3.5" board). Each landscape band (51 rows) is scaled in two halves of 240, so the
// 3 x 240 scratch is enough. Touches need no change (the FT6336 maps to the portrait UI, LVGL turns that).
// The board's hal::Display (was src/lcd_ili9341.cpp; lcd.h says what each call is for).
#ifndef ESPOS_QEMU  // QEMU: src/boards/qemu/display.cpp
#include <Arduino.h>

#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "es3c28p.h"
#include "esp_lcd_panel_io.h"
#include "scale.h"
#include "lcd.h"
#include "src/display/lv_display_private.h"
#include "trace.h"

namespace es3c28p {
namespace {

constexpr hal::UiMap UI = BOARD.display.ui;
constexpr int LCD_W = UI.w, LCD_H = UI.h;  // the UI, 360x480
constexpr hal::DisplayPins P = BOARD.display.pins;
constexpr int PANEL28_W = BOARD.display.panel.w, PANEL28_H = BOARD.display.panel.h;
static_assert(UI.num == 2 && UI.den == 3 && UI.pw() == PANEL28_W && UI.ph() == PANEL28_H && UI.x == 0 && UI.y == 0,
              "this driver sends the UI at 2/3 over the whole panel");

esp_lcd_panel_io_handle_t io;
lv_display_t* disp;
constexpr int BAND = 69;  // UI rows a band (a multiple of 3): 46 panel rows; 360 x 69 x 2 = 49,680 bytes
constexpr size_t BAND_BYTES = LCD_W * BAND * 2;
constexpr int OUT_ROWS = BAND * 2 / 3;
uint8_t *band1, *band2;    // LVGL renders here (internal RAM)
uint16_t* out[2];          // a scaled band, 240 wide (DMA, internal RAM)
int outAt = 0;
scale::C3* tmp;            // 3 rows of 240
uint16_t* mirrorBuf = nullptr;
uint32_t frames = 0, flushUs = 0;
bool upside = false;       // rotation 180
bool side = false;         // landscape: 90 or 270
int pw = PANEL28_W, ph = PANEL28_H;  // the panel's address space as MADCTL turns it

void cmd(uint8_t c, const uint8_t* d = nullptr, int n = 0) { esp_lcd_panel_io_tx_param(io, c, d, n); }

// The vendor's ILI9341V init for this IPS panel (lcdwiki ES3C28P, ILI9341V_Init.txt): its power, VCOM, frame rate,
// interface and gamma values, and colour inversion on (0x21). An IPS panel needs the inversion: without it black
// showed as white (the side bars) and the whole picture was pale (seen on a test board, 2026-11-20, when the
// generic TN ILI9341 values were used). Kept ours: MADCTL 0x48 + B6 08 82 27 (MX with SS=0 is the vendor's MADCTL
// 0x08 with SS=1: the same mirror), so the touch map and the landscape MADCTLs below stay as they were.
void init() {
  cmd(0x01);  // software reset
  delay(120);
  const uint8_t pwrB[] = {0x00, 0xC1, 0x30}, seq[] = {0x64, 0x03, 0x12, 0x81}, timA[] = {0x85, 0x00, 0x78},
                pwrA[] = {0x39, 0x2C, 0x00, 0x34, 0x02}, pump[] = {0x20}, timB[] = {0x00, 0x00};
  cmd(0xCF, pwrB, 3), cmd(0xED, seq, 4), cmd(0xE8, timA, 3), cmd(0xCB, pwrA, 5), cmd(0xF7, pump, 1), cmd(0xEA, timB, 2);
  const uint8_t p1[] = {0x13}, p2[] = {0x13}, vc1[] = {0x22, 0x35}, vc2[] = {0xBD};
  cmd(0xC0, p1, 1), cmd(0xC1, p2, 1), cmd(0xC5, vc1, 2), cmd(0xC7, vc2, 1);
  cmd(0x21);  // display inversion on: this is an IPS panel
  const uint8_t madctl[] = {0x48};  // MX, BGR: portrait, the panel's colour order
  cmd(0x36, madctl, 1);
  const uint8_t dfc[] = {0x08, 0x82, 0x27};
  cmd(0xB6, dfc, 3);
  const uint8_t fmt[] = {0x55};     // 16 bits a pixel
  cmd(0x3A, fmt, 1);
  const uint8_t ifc[] = {0x01, 0x30}, frm[] = {0x00, 0x1B}, g3[] = {0x00}, gc[] = {0x01};
  cmd(0xF6, ifc, 2), cmd(0xB1, frm, 2), cmd(0xF2, g3, 1), cmd(0x26, gc, 1);
  const uint8_t pg[] = {0x0F, 0x35, 0x31, 0x0B, 0x0E, 0x06, 0x49, 0xA7, 0x33, 0x07, 0x0F, 0x03, 0x0C, 0x0A, 0x00},
                ng[] = {0x00, 0x0A, 0x0F, 0x04, 0x11, 0x08, 0x36, 0x58, 0x4D, 0x07, 0x10, 0x0C, 0x32, 0x34, 0x0F};
  cmd(0xE0, pg, 15), cmd(0xE1, ng, 15);
  cmd(0x11);  // sleep out
  delay(120);
  cmd(0x29);  // display on
}

void window(int x0, int y0, int x1, int y1) {
  const uint8_t ca[] = {(uint8_t)(x0 >> 8), (uint8_t)x0, (uint8_t)(x1 >> 8), (uint8_t)x1};
  const uint8_t ra[] = {(uint8_t)(y0 >> 8), (uint8_t)y0, (uint8_t)(y1 >> 8), (uint8_t)y1};
  cmd(0x2A, ca, 4), cmd(0x2B, ra, 4);
}

// Black over the whole panel (before the first picture).
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

// UI rows [y, y + rows) (native RGB565, the UI's width: 360, or 480 in landscape; rows a multiple of 3) to the panel
// at 2/3, the panel's whole width.
void sendRows(const uint16_t* px, int y, int rows) {
  const int lw = side ? LCD_H : LCD_W, ow = side ? PANEL28_H : PANEL28_W;
  const int py = y * 2 / 3, n = rows / 3 * 2;
  uint16_t* o = out[outAt];
  outAt ^= 1;
  if (side)
    for (int h = 0; h < 2; h++) scale::down32(px + h * 240, 240, rows, lw, o + h * 160, ow, tmp);  // halves 240 -> 160
  else scale::down32(px, LCD_W, rows, LCD_W, o, ow, tmp);
  lv_draw_sw_rgb565_swap(o, ow * n);
  int y0 = py;
  if (upside) {  // upside down: the band reversed pixel by pixel, at the mirrored place
    for (uint16_t *a = o, *b = o + ow * n - 1; a < b; a++, b--) {
      const uint16_t t = *a;
      *a = *b, *b = t;
    }
    y0 = PANEL28_H - py - n;
  }
  window(0, y0, ow - 1, y0 + n - 1);  // waits for the band before the last (its buffer is ours again)
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

// lcd::native: the screen drawn straight at the panel's size (240x320 upright, 320x240 on its side: the 2/3 is the
// picture's own, nothing is scaled here), in pieces of the scaled-band buffers, placed as sendRows() places a band.
bool native(void (*fill)(uint16_t*, int, int, int, int, int, int, void*), void* ctx, bool) {
  if (!disp || disp->rendering_in_progress) return false;
  const uint32_t t0 = micros();
  const int w = side ? PANEL28_H : PANEL28_W, h = side ? PANEL28_W : PANEL28_H;  // 320x240 or 240x320
  const int rowsA = PANEL28_W * OUT_ROWS / w;                                     // what one buffer holds
  for (int py = 0; py < h; py += rowsA) {
    const int n = min(rowsA, h - py);
    uint16_t* o = out[outAt];
    outAt ^= 1;
    int y0 = py;
    if (upside) {
      fill(o + w * n - 1, -1, -w, 0, py, w, py + n, ctx);
      y0 = PANEL28_H - py - n;
    } else fill(o, 1, w, 0, py, w, py + n, ctx);
    lv_draw_sw_rgb565_swap(o, w * n);
    window(0, y0, w - 1, y0 + n - 1);  // waits for the piece before the last (its buffer is ours again)
    esp_lcd_panel_io_tx_color(io, 0x2C, o, w * n * 2);
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
  tmp = (scale::C3*)heap_caps_malloc(3 * PANEL28_W * sizeof(scale::C3), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  init();
  clear();
  band1 = (uint8_t*)heap_caps_malloc(BAND_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  band2 = (uint8_t*)heap_caps_malloc(BAND_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  disp = lv_display_create(LCD_W, LCD_H);
  lv_display_set_buffers(disp, band1, band2, BAND_BYTES, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_add_event_cb(disp, roundArea, LV_EVENT_INVALIDATE_AREA, nullptr);
  lv_display_set_flush_cb(disp, flush);
  trace::add("lcd: ILI9341 240x320, the 360x480 UI at 2/3");
  return disp;
}

void setRotation(lv_display_rotation_t r) {
  // Upside down portrait is the panel's own mirrored scan (MADCTL MY instead of MX: 0x88), not a software turn:
  // LVGL and the aquarium draw as upright (lcd.h flipped()). The two landscape ways were already the panel's own.
  const bool flip = r == LV_DISPLAY_ROTATION_180;
  if (flip) r = LV_DISPLAY_ROTATION_0;
  lcd::setFlipped(flip);
  side = r == LV_DISPLAY_ROTATION_90 || r == LV_DISPLAY_ROTATION_270;
  upside = false;
  // Portrait: MX (as the vendor's demos). Landscape: the row/column exchange, the two ways up as LVGL's 90 and 270
  // turn the UI (90: MY MX MV; 270: MV). LVGL redraws the whole panel, nothing to clear.
  const uint8_t m = r == LV_DISPLAY_ROTATION_90 ? 0xE8 : r == LV_DISPLAY_ROTATION_270 ? 0x28 : flip ? 0x88 : 0x48;
  cmd(0x36, &m, 1);
  pw = side ? PANEL28_H : PANEL28_W, ph = side ? PANEL28_W : PANEL28_H;
  trace::add("lcd: rotation %d (%s)", (int)r, side ? "landscape, 480x360 at 2/3" : "portrait");
  lv_display_set_rotation(disp, r);  // LVGL redraws everything (touch turns with it)
}

void setBacklight(int percent) {
  percent = constrain(percent, 0, 100);
  // Linear here, not the 3.5" board's quadratic: this panel's 4 backlight LEDs are dimmer, and at the default 70 %
  // the quadratic curve (49 % duty) looked dull on a test board (2026-11-20). 70 % is now 70 % duty.
  const uint32_t duty = (uint32_t)percent * 1023 / 100;
  ledcWrite(P.bl, percent > 0 && duty < 4 ? 4 : duty);
}

}  // namespace

const hal::Display display = {begin, setRotation, setBacklight, stats, logInvalid, direct, mirror, frame, native};

}  // namespace es3c28p
#endif
