// The display in QEMU (env s3desk-qemu, docs/simulator.md): Espressif's virtual RGB panel in place of the ST77922.
// The firmware is the board's otherwise. The panel's registers sit at 0x21000000 (qemu include/hw/display/esp_rgb.h):
// the window size, an area (from, to), where its pixels are (an offset in the panel's own memory at 0x20000000), and
// "update" (cleared once QEMU copied the area to the window, at its next screen refresh). One area at a time.
// Where the pixels are is a full address: in the panel's memory (0x20000000..) or in internal RAM.
// The whole logical frame is kept in the panel's memory: LVGL's bands and the run game's rows are copied in at
// their rows and the whole frame goes out as one area (QEMU blanks its window when it is made or resized, so the
// unchanged rows must come again; once a second too), shown the right way up whatever the board's mounting.
// The 2.8" board (BOARD_ES3C28P): the window is its 240x320 panel; the 320x480 UI is kept in PSRAM and goes to the
// window at 2/3 (scale::down32, 213 px across from x 13), as src/boards/es3c28p/display.cpp does on the board.
// Turned (90, 180, 270): the window stays the panel as it sits in the hand, portrait, like the boards' glass: LVGL's
// frame (480x320 in landscape) is turned into the portrait frame (lv_draw_sw_rotate, as the 3.5" board turns its
// blocks), so touches from the viewer stay the panel's. "lcd: rotation N" on the serial port tells the viewer
// (tools/qemu/view.cpp) to show it the right way up.
// The QEMU board's hal::Display (was src/lcd_qemu.cpp; lcd.h says what each call is for).
#ifdef ESPOS_QEMU
#include <Arduino.h>

#include "esp_heap_caps.h"
#include "qemu.h"
#include "scale.h"
#include "src/display/lv_display_private.h"

namespace qemu {
namespace {

constexpr int LCD_W = hal::UI_W, LCD_H = hal::UI_H;  // the UI
#ifdef BOARD_ES3C28P
constexpr int PANEL28_W = BOARD.display.panel.w, PANEL28_H = BOARD.display.panel.h;
constexpr int SHOW_W = BOARD.display.ui.w(), SHOW_X = BOARD.display.ui.x;
#endif

volatile uint32_t* const REG = (volatile uint32_t*)0x21000000;
uint16_t* const VRAM = (uint16_t*)0x20000000;  // 800x600x4 bytes; our frame is 320x480x2 from its start
enum { R_VERSION = 0, R_WIN = 1, R_FROM = 2, R_TO = 3, R_CONTENT = 4, R_STATUS = 5, R_BPP = 6 };

constexpr int BAND = 80;                         // as the board: two 80-row bands in internal RAM
constexpr size_t BAND_BYTES = LCD_W * BAND * 2;
uint8_t *band1, *band2;
lv_display_t* disp;
int fw = LCD_W, fh = LCD_H;                      // the logical frame
lv_display_rotation_t rot = LV_DISPLAY_ROTATION_0;
uint16_t* LF = nullptr;                          // turned: LVGL's frame as it draws it (PSRAM)
int pend1 = 0, pend2 = 0;
uint32_t frames = 0, flushUs = 0;
#ifdef BOARD_ES3C28P
uint16_t* FB = nullptr;                          // the UI, 320x480 in PSRAM
constexpr int WW = PANEL28_W, WH = PANEL28_H;        // the window: the panel
scale::C3* tmp = nullptr;
#else
uint16_t* const FB = VRAM;                       // the UI (portrait, as the panel sits) is the window
constexpr int WW = LCD_W, WH = LCD_H;
#endif

void window() {
  REG[R_BPP] = 16;
  REG[R_WIN] = (uint32_t)WW << 16 | WH;
}

// The frame to the window's memory (turned into the portrait frame first; the 2.8" board: then at 2/3; the 3.5" one:
// the portrait frame is the window).
void toWindow() {
  if (rot != LV_DISPLAY_ROTATION_0 && LF)
    lv_draw_sw_rotate(LF, FB, fw, fh, fw * 2, LCD_W * 2, rot, LV_COLOR_FORMAT_RGB565);
#ifdef BOARD_ES3C28P
  scale::down32(FB, LCD_W, LCD_H, LCD_W, VRAM + SHOW_X, PANEL28_W, tmp);
#endif
}

// Rows [y1, y2] of the frame to the window. Still waiting for QEMU: the waiting area grows to cover both.
void submit(int y1, int y2) {
  if (REG[R_STATUS] & 1) y1 = min(y1, pend1), y2 = max(y2, pend2);
  REG[R_FROM] = (uint32_t)y1;  // x 0
  REG[R_TO] = (uint32_t)WW << 16 | (y2 + 1);
  REG[R_CONTENT] = (uint32_t)(uintptr_t)(VRAM + y1 * WW);  // its full address (QEMU's view of the memory)
  REG[R_STATUS] = 1;
  pend1 = y1, pend2 = y2;
}

// As the board: every area widened to whole rows (LVGL renders the same amount).
void roundArea(lv_event_t* e) {
  auto* a = (lv_area_t*)lv_event_get_param(e);
  lv_display_t* d = (lv_display_t*)lv_event_get_target(e);
  if (d->rendering_in_progress) return;
  const int32_t w = lv_display_get_horizontal_resolution(d), h = lv_display_get_vertical_resolution(d);
  a->x1 = 0;
  a->x2 = w - 1;
  a->y1 &= ~1;
  a->y2 = min<int32_t>(a->y2 | 1, h - 1);
}

void flush(lv_display_t* d, const lv_area_t* area, uint8_t* px) {
  const uint32_t t0 = micros();
  const int w = lv_area_get_width(area), h = lv_area_get_height(area);
  uint16_t* to = rot != LV_DISPLAY_ROTATION_0 && LF ? LF : FB;
  for (int y = 0; y < h; y++) memcpy(to + (area->y1 + y) * fw + area->x1, px + y * w * 2, w * 2);
  if (lv_display_flush_is_last(d)) {
    toWindow();
    submit(0, WH - 1);
    frames++;
  }
  flushUs += micros() - t0;
  lv_display_flush_ready(d);
}

void logInvalid(bool) {}

bool mirrored = false;
void mirror(bool on) { mirrored = on; }
const uint16_t* frame(int& w, int& h) {
  w = fw, h = fh;
  return mirrored ? (rot != LV_DISPLAY_ROTATION_0 && LF ? LF : FB) : nullptr;  // the whole logical frame is there
}

bool direct(int y0, int y1, void (*fill)(uint16_t*, int, int, void*), void* ctx, bool) {
  if (!disp || disp->rendering_in_progress || rot != LV_DISPLAY_ROTATION_0) return false;
  const uint32_t t0 = micros();
  for (int y = y0; y < y1; y += BAND) fill(FB + y * fw, y, min(BAND, y1 - y), ctx);
  toWindow();
  submit(0, WH - 1);
  flushUs += micros() - t0;
  frames++;
  return true;
}

// lcd::native: the screen at the panel's own size of the UI, as the boards take it. The 3.5" board: into the logical
// frame (turned into the window as LVGL's frames are). The 2.8" board: straight into the window's 213x320 (or turned)
// place, nothing scaled: in 80-row pieces in internal RAM, as the boards fill their DMA buffers.
bool native(void (*fill)(uint16_t*, int, int, int, int, int, int, void*), void* ctx, bool) {
  if (!disp || disp->rendering_in_progress) return false;
  const uint32_t t0 = micros();
  uint16_t* b = (uint16_t*)band1;
#ifdef BOARD_ES3C28P
  static uint16_t* NB = nullptr;  // the picture (320x213 at most), then into the window as LVGL's frames go there
  if (!NB) NB = (uint16_t*)heap_caps_malloc(320 * 213 * 2, MALLOC_CAP_SPIRAM);
  if (!NB) return false;
  const bool side = rot == LV_DISPLAY_ROTATION_90 || rot == LV_DISPLAY_ROTATION_270;
  const int w = side ? PANEL28_H : SHOW_W, h = side ? SHOW_W : PANEL28_H, rowsA = (int)(BAND_BYTES / 2) / w;
  for (int y = 0; y < h; y += rowsA) {
    const int n = min(rowsA, h - y);
    fill(b, 1, w, 0, y, w, y + n, ctx);
    memcpy(NB + y * w, b, w * n * 2);
  }
  if (side) lv_draw_sw_rotate(NB, VRAM + SHOW_X, w, h, w * 2, WW * 2, rot, LV_COLOR_FORMAT_RGB565);
  else if (rot == LV_DISPLAY_ROTATION_180) lv_draw_sw_rotate(NB, VRAM + (PANEL28_W - SHOW_X - SHOW_W), w, h, w * 2, WW * 2, rot, LV_COLOR_FORMAT_RGB565);
  else
    for (int y = 0; y < h; y++) memcpy(VRAM + y * WW + SHOW_X, NB + y * w, w * 2);
#else
  uint16_t* to = rot != LV_DISPLAY_ROTATION_0 && LF ? LF : FB;
  const int rowsA = (int)(BAND_BYTES / 2) / fw;  // 80 rows of 320, 53 of 480
  for (int y = 0; y < fh; y += rowsA) {
    const int n = min(rowsA, fh - y);
    fill(b, 1, fw, 0, y, fw, y + n, ctx);
    memcpy(to + y * fw, b, fw * n * 2);
  }
  if (rot != LV_DISPLAY_ROTATION_0 && LF) lv_draw_sw_rotate(LF, FB, fw, fh, fw * 2, LCD_W * 2, rot, LV_COLOR_FORMAT_RGB565);
#endif
  submit(0, WH - 1);
  flushUs += micros() - t0;
  frames++;
  return true;
}

void stats(uint32_t& nFrames, uint32_t& avgFlushUs) {
  nFrames = frames;
  avgFlushUs = frames ? flushUs / frames : 0;
  frames = flushUs = 0;
}

lv_display_t* begin() {
#ifdef BOARD_ES3C28P
  FB = (uint16_t*)heap_caps_calloc(LCD_W * LCD_H, 2, MALLOC_CAP_SPIRAM);
  tmp = (scale::C3*)heap_caps_malloc(3 * SHOW_W * sizeof(scale::C3), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  memset(VRAM, 0, PANEL28_W * PANEL28_H * 2);  // the black bars each side
#endif
  window();
  Serial.printf("lcd: QEMU virtual panel, version %08lx\n", (unsigned long)REG[R_VERSION]);
  band1 = (uint8_t*)heap_caps_malloc(BAND_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  band2 = (uint8_t*)heap_caps_malloc(BAND_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  disp = lv_display_create(LCD_W, LCD_H);
  lv_display_set_buffers(disp, band1, band2, BAND_BYTES, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_add_event_cb(disp, roundArea, LV_EVENT_INVALIDATE_AREA, nullptr);
  lv_display_set_flush_cb(disp, flush);
  lv_timer_create([](lv_timer_t*) { submit(0, WH - 1); }, 1000, nullptr);
  return disp;
}

void setRotation(lv_display_rotation_t r) {
  const bool side = r == LV_DISPLAY_ROTATION_90 || r == LV_DISPLAY_ROTATION_270;
  if (r != LV_DISPLAY_ROTATION_0 && !LF) LF = (uint16_t*)heap_caps_calloc(LCD_W * LCD_H, 2, MALLOC_CAP_SPIRAM);
  rot = r;
  fw = side ? LCD_H : LCD_W, fh = side ? LCD_W : LCD_H;
  window();
  Serial.printf("lcd: rotation %d\n", (int)r);  // the viewer turns its picture
  lv_display_set_rotation(disp, r);  // LVGL redraws everything
}

void setBacklight(int) {}

}  // namespace

const hal::Display display = {begin, setRotation, setBacklight, stats, logInvalid, direct, mirror, frame, native};

}  // namespace qemu
#endif
