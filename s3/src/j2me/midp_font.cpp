// MIDP's three font sizes from the firmware's Ubuntu fonts (12, 14, 16 px; regular and bold), copied out as A8
// glyphs for code points 0..255 once, on the UI thread: the game's thread then draws text without touching LVGL
// (its font code keeps a cache that is not thread safe).
#include <lvgl.h>

#include <algorithm>

#include "midp_draw.h"

extern "C" {
extern const lv_font_t ub_r12, ub_r14, ub_r16, ub_b12, ub_b14, ub_b16;
}

namespace j2me {
namespace draw {

bool prepareFonts() {
  if (font(0)) return true;
  const lv_font_t* src[6] = {&ub_r12, &ub_r14, &ub_r16, &ub_b12, &ub_b14, &ub_b16};
  std::vector<uint8_t> tmp;
  for (int id = 0; id < 6; id++) {
    const lv_font_t* lf = src[id];
    Font& f = *fontSlot(id);
    f.lineH = lf->line_height;
    const int top = lf->line_height - lf->base_line;  // the baseline, from the line's top
    f.baseline = top;
    f.glyphs.assign(256, Glyph{});
    f.bits.clear();
    for (uint32_t cp = 32; cp < 256; cp++) {
      lv_font_glyph_dsc_t g;
      lv_memzero(&g, sizeof g);
      if (!lv_font_get_glyph_dsc(lf, &g, cp, 0) || g.resolved_font == nullptr) continue;
      Glyph out{};
      out.adv = (uint8_t)std::min<int>(255, g.adv_w);
      out.w = (uint8_t)g.box_w;
      out.h = (uint8_t)g.box_h;
      out.ofsX = (int16_t)g.ofs_x;
      out.ofsY = (int16_t)(top - g.box_h - g.ofs_y);
      out.off = (uint32_t)f.bits.size();
      if (g.box_w && g.box_h) {
        const uint32_t stride = lv_draw_buf_width_to_stride(g.box_w, LV_COLOR_FORMAT_A8);
        tmp.assign((size_t)stride * g.box_h, 0);
        lv_draw_buf_t db;
        lv_draw_buf_init(&db, g.box_w, g.box_h, LV_COLOR_FORMAT_A8, stride, tmp.data(), (uint32_t)tmp.size());
        // LVGL 9.2 expands the glyph into db (A8, `stride` a row) and returns db itself, not the pixels
        const uint8_t* bm = lv_font_get_glyph_bitmap(&g, &db) ? tmp.data() : nullptr;
        if (bm) {
          for (int r = 0; r < g.box_h; r++)
            f.bits.insert(f.bits.end(), bm + r * stride, bm + r * stride + g.box_w);
        } else {
          out.w = out.h = 0;
        }
      }
      if (!out.adv) out.adv = 1;
      f.glyphs[cp] = out;
    }
    f.bits.shrink_to_fit();
  }
  fontsDone();
  return true;
}

}  // namespace draw
}  // namespace j2me
