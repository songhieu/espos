#pragma GCC optimize("Os")  // cold code: built small (the app slot is nearly full)
// The level-2 UI nodes of JavaScript apps (app-slots.md §15.4): ui.grid, ui.image, ui.split, ui.nav, ui.input, ui.qr,
// ui.line, ui.sheet. Registered with jsapp::addNode; the prelude in jsapp.cpp makes the nodes.
//
// Pictures (ui.image, grid tiles): a baseline JPEG from the app's files ("cache/...", "data/...") or its package
// (any other name), cut to the exact size (jpeg::cover), the dark band for text baked into the bottom rows and the
// round corners painted in the background colour, then kept in a PSRAM cache (1 MB, least recently used first) so
// scrolling only copies pixels. Decoding is lazy: a picture not in the cache shows its fallback (colour + initials)
// and is decoded by a timer, one at a time, only while it is on screen and the finger is still. A file that is not a
// baseline JPEG (WebP, PNG, progressive) keeps its fallback.
#include <Arduino.h>
#include <lvgl.h>

#include <algorithm>
#include <map>
#include <vector>

#include "art.h"
#include "fonts.h"
#include "jpeg.h"
#include "js/jsapp.h"
#include "js/jsfs.h"
#include "js/jsui.h"
#include "keyboard.h"
#include "os.h"
#include "pkg/pkg.h"
#include "sound.h"
#include "trace.h"
#include "uikit.h"

namespace jsui {
namespace {

using namespace kit;
using namespace jsapp::node;

JSValue sub(JSValueConst o, const char* k) { return JS_GetPropertyStr(ctx(), o, k); }

// ---------------------------------------------------------------- pictures
constexpr uint32_t CACHE_MAX = 1024 * 1024;

struct Pic {
  String key;
  uint16_t* px = nullptr;
  lv_image_dsc_t dsc{};
  uint32_t bytes = 0, used = 0;
  int refs = 0;
};
std::vector<Pic*> pics;
uint32_t picBytes = 0, tick = 0;

struct Want {  // a picture an lv_image waits for
  lv_obj_t* img;
  String src, key;
  int w, h, radius;
  int band;  // rows at the bottom darkened for text (0: none)
  uint32_t bg;
};
std::vector<Want> wants;
lv_timer_t* decoder = nullptr;

String picKey(const String& src, int w, int h, int r, int band, uint32_t bg) {
  char k[48];
  snprintf(k, sizeof k, "|%dx%d|%d|b%d|%06lx", w, h, r, band, (unsigned long)bg);
  return src + k;
}

void unref(lv_event_t* e) {  // an lv_image showing a cached picture went away
  Pic* p = (Pic*)lv_event_get_user_data(e);
  for (Pic* q : pics)
    if (q == p) q->refs--;
}

void show(lv_obj_t* img, Pic* p) {
  p->refs++, p->used = ++tick;
  lv_image_set_src(img, &p->dsc);
  lv_obj_remove_flag(img, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(img, unref, LV_EVENT_DELETE, p);
}

Pic* find(const String& key) {
  for (Pic* p : pics)
    if (p->key == key) return p;
  return nullptr;
}

void dropOld(uint32_t need) {  // the cache over its size: pictures nobody shows, oldest first
  while (picBytes + need > CACHE_MAX) {
    Pic* old = nullptr;
    for (Pic* p : pics)
      if (!p->refs && (!old || p->used < old->used)) old = p;
    if (!old) return;
    pics.erase(std::find(pics.begin(), pics.end(), old));
    picBytes -= old->bytes;
    heap_caps_free(old->px);
    delete old;
  }
}

uint8_t* readSrc(const String& src, size_t& len) {
  if (src.startsWith("cache/") || src.startsWith("data/")) {
    const String p = jsfs::path(src.c_str());
    FILE* f = p.length() ? fopen(p.c_str(), "rb") : nullptr;
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* b = n > 0 && n <= 512 * 1024 ? (uint8_t*)heap_caps_malloc(n, MALLOC_CAP_SPIRAM) : nullptr;
    if (b && fread(b, 1, n, f) != (size_t)n) heap_caps_free(b), b = nullptr;
    fclose(f);
    len = b ? n : 0;
    return b;
  }
  pkg::File f;
  if (!pkg::open(appId().c_str(), src.c_str(), f)) return nullptr;
  len = f.size();
  return len <= 512 * 1024 ? pkg::loadAll(f) : nullptr;
}

Pic* decode(const Want& w) {
  size_t len = 0;
  uint8_t* data = readSrc(w.src, len);
  uint16_t* px = data ? jpeg::cover(data, len, w.w, w.h) : nullptr;
  heap_caps_free(data);
  if (!px) return nullptr;
  // The band under text: the bottom `band` rows darkened. It turns dark over its first 16 rows (where the text does
  // not start yet), then goes on darkening a little to the bottom: white text reads on any photo, a white cup included.
  if (w.band > 0)
    for (int y = std::max(0, w.h - w.band); y < w.h; y++) {
      const int d = y - (w.h - w.band), ramp = std::min(16, w.band / 2);
      const int k = d < ramp ? 256 - d * 160 / ramp : 96 - (d - ramp) * 26 / std::max(1, w.band - ramp);  // 256 .. 70
      for (int x = 0; x < w.w; x++) {
        uint16_t c = px[y * w.w + x];
        const int r = ((c >> 11) & 31) * k >> 8, g = ((c >> 5) & 63) * k >> 8, b = (c & 31) * k >> 8;
        px[y * w.w + x] = r << 11 | g << 5 | b;
      }
    }
  // Round corners in the background colour (no clipping while scrolling).
  if (w.radius > 0) {
    const uint16_t bg = (w.bg >> 8 & 0xf800) | (w.bg >> 5 & 0x07e0) | (w.bg >> 3 & 0x001f);
    const int r = std::min(w.radius, std::min(w.w, w.h) / 2);
    for (int y = 0; y < r; y++)
      for (int x = 0; x < r; x++) {
        const int dx = r - x, dy = r - y;
        if (dx * dx + dy * dy <= r * r) continue;
        px[y * w.w + x] = px[y * w.w + w.w - 1 - x] = bg;
        px[(w.h - 1 - y) * w.w + x] = px[(w.h - 1 - y) * w.w + w.w - 1 - x] = bg;
      }
  }
  const uint32_t bytes = w.w * w.h * 2;
  dropOld(bytes);
  Pic* p = new Pic;
  p->key = w.key, p->px = px, p->bytes = bytes;
  p->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  p->dsc.header.cf = LV_COLOR_FORMAT_RGB565;
  p->dsc.header.w = w.w, p->dsc.header.h = w.h, p->dsc.header.stride = w.w * 2;
  p->dsc.data = (const uint8_t*)px, p->dsc.data_size = bytes;
  pics.push_back(p);
  picBytes += bytes;
  return p;
}

// One picture a tick: the first wanted one on screen, never while a finger moves.
uint32_t decodes = 0, decodeMs = 0, decodeMax = 0;

void decodeTick(lv_timer_t*) {
  if (wants.empty() || lv_display_get_inactive_time(nullptr) < 150) return;
  for (size_t i = 0; i < wants.size(); i++) {
    if (!lv_obj_is_visible(lv_obj_get_parent(wants[i].img))) continue;  // (the image itself is hidden until then)
    Want w = wants[i];
    wants.erase(wants.begin() + i);
    Pic* p = find(w.key);
    if (!p) {
      const uint32_t t0 = millis();
      p = decode(w);
      const uint32_t ms = millis() - t0;  // the app's numbers, traced when it closes
      decodes++, decodeMs += ms, decodeMax = std::max(decodeMax, ms);
    }
    if (p) show(w.img, p);
    else lv_obj_add_flag(w.img, LV_OBJ_FLAG_HIDDEN);  // not drawable: the fallback stays
    return;
  }
}

// A picture into `parent` at w x h: at once from the cache, else later (the fallback under it shows meanwhile).
void picture(lv_obj_t* parent, const String& src, int w, int h, int radius, int band, uint32_t bg) {
  if (!src.length() || w <= 0 || h <= 0 || w * h > 320 * 480) return;
  lv_obj_t* img = lv_image_create(parent);
  lv_obj_set_size(img, w, h);
  lv_obj_add_flag(img, LV_OBJ_FLAG_EVENT_BUBBLE);
  const String key = picKey(src, w, h, radius, band, bg);
  if (Pic* p = find(key)) return show(img, p);
  lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);
  wants.push_back({img, src, key, w, h, radius, band, bg});
  // Deleted before its turn (a new render): out of the list at once. An address in the list could otherwise belong to
  // a new object of another kind by the next tick (lv_obj_is_valid cannot tell).
  lv_obj_add_event_cb(img, [](lv_event_t* e) {
    lv_obj_t* me = (lv_obj_t*)lv_event_get_current_target(e);
    for (size_t i = 0; i < wants.size(); i++)
      if (wants[i].img == me) wants.erase(wants.begin() + i--);
  }, LV_EVENT_DELETE, nullptr);
  if (!decoder) decoder = lv_timer_create(decodeTick, 15, nullptr);
}

// ---------------------------------------------------------------- icons: OS glyphs by name, or an app's .a8 file
struct Glyph {
  const char* name;
  uint16_t art;
};
const Glyph GLYPHS[] = {{"grid", ART_gl_grid_22},     {"list", ART_gl_list_22},   {"search", ART_gl_search_22},
                        {"home", ART_gl_home_22},     {"gear", ART_gl_gear_22},   {"plus", ART_gl_plus_22},
                        {"check", ART_gl_check_22},   {"bell", ART_gl_bell_22},   {"timer", ART_gl_timer_22},
                        {"photo", ART_gl_photo_22},   {"edit", ART_gl_edit_22},   {"trash", ART_gl_trash_22},
                        {"sync", ART_gl_sync_22},     {"flag", ART_gl_flag_22},   {"globe", ART_gl_globe_22},
                        {"pin", ART_gl_pin_22},       {"power", ART_gl_power_22}, {"back", ART_gl_back_22},
                        {"chev", ART_gl_chev_22}};
std::map<String, lv_image_dsc_t*> a8s;  // an app's own icons ("icons/bag.a8": u16 w, u16 h, then w*h alpha bytes)

const lv_image_dsc_t* iconOf(const String& name) {
  for (const Glyph& g : GLYPHS)
    if (name == g.name) return art_dsc(g.art);
  if (!name.endsWith(".a8")) return nullptr;
  auto it = a8s.find(name);
  if (it != a8s.end()) return it->second;
  pkg::File f;
  uint8_t* b = pkg::open(appId().c_str(), name.c_str(), f) && f.size() > 4 && f.size() < 8192 ? pkg::loadAll(f) : nullptr;
  if (!b) return nullptr;
  const int w = b[0] | b[1] << 8, h = b[2] | b[3] << 8;
  if ((uint32_t)(4 + w * h) > f.size()) return heap_caps_free(b), nullptr;
  lv_image_dsc_t* d = (lv_image_dsc_t*)heap_caps_calloc(1, sizeof *d, MALLOC_CAP_SPIRAM);
  d->header.magic = LV_IMAGE_HEADER_MAGIC, d->header.cf = LV_COLOR_FORMAT_A8;
  d->header.w = w, d->header.h = h, d->header.stride = w;
  d->data = b + 4, d->data_size = w * h;
  return a8s[name] = d;
}

// ---------------------------------------------------------------- the nodes
void onTap(lv_event_t* e) {
  const intptr_t v = (intptr_t)lv_event_get_user_data(e);
  sound::click();
  fire((int)(v >> 12), (int)(v & 0xfff) == 0xfff ? -1 : (int)(v & 0xfff));
}
void* tapData(int h, int i) { return (void*)(intptr_t)(h << 12 | (i & 0xfff)); }

lv_obj_t* fallback(lv_obj_t* parent, JSValueConst o, int w, int h, int radius) {
  const Pal& p = pal();
  JSValue fb = sub(o, "fallback");
  lv_obj_t* b = box(parent, color(fb, "bg", p.cell2), radius);
  lv_obj_set_size(b, w, h);
  lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE);
  const String t = str(fb, "text");
  if (t.length()) {
    lv_obj_t* l = label(b, "", 20, true, color(fb, "fg", p.dim));
    fitText(l, t.c_str(), w - 8, h - 8, std::min(28, h / 2), 12, true);
    lv_obj_center(l);
  }
  JS_FreeValue(ctx(), fb);
  return b;
}

// ui.image(src, {w, h, radius, band, fallback: {text, bg, fg}})
void nImage(lv_obj_t* parent, JSValueConst o, int width) {
  const int w = std::min(num(o, "w", width), width), h = num(o, "h", w), r = num(o, "radius", 8);
  lv_obj_t* b = fallback(parent, o, w, h, r);
  picture(b, str(o, "src"), w, h, r, flag(o, "band") ? h * 3 / 5 : 0, pal().bg);
}

// ui.grid(items, onTap, {cols, h, gap, onLong}): tiles of {title, sub, image, badge, badgeBg, tag, tagBg, bg, bar,
// dim, span}. A tile with an image shows it full size with its text on the dark band; without one, the bg colour.
void nGrid(lv_obj_t* parent, JSValueConst o, int width) {
  const Pal& p = pal();
  const int cols = constrain(num(o, "cols", 3), 1, 8), gap = num(o, "gap", 5), th = constrain(num(o, "h", 72), 32, 240);
  const int tw = (width - gap * (cols - 1)) / cols;
  const int h = handler(o, "on"), hl = handler(o, "onLong");
  lv_obj_t* g = clear(parent);
  lv_obj_set_size(g, width, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(g, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_gap(g, gap, 0);
  JSValue items = sub(o, "items");
  int64_t n = 0;
  JS_GetLength(ctx(), items, &n);
  for (int64_t i = 0; i < n && i < 200; i++) {
    JSValue it = JS_GetPropertyUint32(ctx(), items, (uint32_t)i);
    const String img = str(it, "image"), title = str(it, "title");
    // A word too wide for the tile even at 12 px: the tile takes two columns (names are never cut).
    bool span = flag(it, "span");
    for (int a = 0; !span && a < (int)title.length();) {
      int e = title.indexOf(' ', a);
      if (e < 0) e = title.length();
      span = textW(title.substring(a, e).c_str(), 12, true) > tw - 10;
      a = e + 1;
    }
    const int w = span && cols > 1 ? std::min(width, tw * 2 + gap) : tw;
    const uint32_t bg = color(it, "bg", img.length() ? 0x4b515c : p.cell);  // a photo tile: dark until its photo comes
    lv_obj_t* t = box(g, bg, 10);
    lv_obj_set_size(t, w, th);
    lv_obj_remove_flag(t, LV_OBJ_FLAG_SCROLLABLE);
    if (h >= 0) lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE), lv_obj_add_event_cb(t, onTap, LV_EVENT_SHORT_CLICKED, tapData(h, i));
    if (hl >= 0) lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE), lv_obj_add_event_cb(t, onTap, LV_EVENT_LONG_PRESSED, tapData(hl, i));
    lv_obj_t* photo = nullptr;  // the picture goes in once the title's height is known (its band covers the text)
    if (img.length()) photo = clear(t), lv_obj_set_size(photo, w, th);
    else if (str(it, "bar").length()) {
      lv_obj_t* bar = box(t, color(it, "bar", p.line), 0);
      lv_obj_set_size(bar, 4, th);
    }
    const uint32_t fg = color(it, "fg", img.length() ? 0xffffff : p.fg);
    lv_obj_t* tl = label(t, "", 12, true, fg);
    const String sub1 = str(it, "sub");
    const int subH = sub1.length() ? 15 : 0;
    // 13 px unless a word would break at 13 (then 12, where every word fits: the span rule above).
    int px = 13;
    for (int a = 0; px == 13 && a < (int)title.length();) {
      int e = title.indexOf(' ', a);
      if (e < 0) e = title.length();
      if (textW(title.substring(a, e).c_str(), 13, true) > w - 10) px = 12;
      a = e + 1;
    }
    const int lines = wrapText(tl, title.c_str(), w - 10, px, true, 1, std::max(1, (th - 24 - subH) / (px + 2)));
    lv_obj_align(tl, LV_ALIGN_BOTTOM_LEFT, 6, -5 - subH);
    if (photo) {
      lv_obj_move_to_index(photo, 0);  // under the texts
      picture(photo, img, w, th, 10, std::min(th, 5 + subH + lines * (px + 5) + 14), p.bg);
    }
    const String ini = str(it, "initials");
    const int room = th - 5 - subH - lines * (px + 5) - 4;  // above the title
    if (ini.length() && room >= 22) {  // the watermark of a tile without a picture, where it does not meet the title
      lv_obj_t* l = label(t, ini.c_str(), room >= 32 ? 28 : 20, true, color(it, "bar", p.dim));
      lv_obj_set_style_text_opa(l, LV_OPA_50, 0);
      lv_obj_align(l, LV_ALIGN_TOP_RIGHT, -6, 4);
    }
    if (subH) {
      lv_obj_t* s = label(t, "", 12, false, color(it, "subColor", img.length() ? 0xe8e8e8 : color(it, "fg", p.dim)));
      fitText(s, sub1.c_str(), w - 10, lineH(12, false), 12, 12);
      lv_obj_align(s, LV_ALIGN_BOTTOM_LEFT, 6, -4);
    }
    const String badge = str(it, "badge"), tag = str(it, "tag");
    if (badge.length()) {
      lv_obj_t* b = box(t, color(it, "badgeBg", col::IOS_BLUE), 10);
      lv_obj_set_size(b, LV_SIZE_CONTENT, 20);
      lv_obj_set_style_pad_hor(b, 6, 0);
      lv_obj_t* l = label(b, badge.c_str(), 12, true, 0xffffff);
      lv_obj_center(l);
      lv_obj_align(b, LV_ALIGN_TOP_RIGHT, -5, 5);
    }
    if (tag.length()) {
      lv_obj_t* b = box(t, color(it, "tagBg", col::ORANGE), 7);
      lv_obj_set_size(b, LV_SIZE_CONTENT, 19);
      lv_obj_set_style_pad_hor(b, 6, 0);
      lv_obj_t* l = label(b, "", 12, true, 0xffffff);
      // the room the badge leaves: tile margins 5 + 5, the tag's padding 12, the badge (text + 12) and a 4 px gap
      const int room = w - 10 - 12 - (badge.length() ? textW(badge.c_str(), 12, true) + 12 + 4 : 0);
      fitText(l, tag.c_str(), room, lineH(12, true), 12, 12, true);
      lv_obj_center(l);
      lv_obj_align(b, LV_ALIGN_TOP_LEFT, 5, 5);
    }
    if (flag(it, "dim")) lv_obj_set_style_opa(t, LV_OPA_60, 0);
    JS_FreeValue(ctx(), it);
  }
  JS_FreeValue(ctx(), items);
}

// One pane of a split: a scrolling body and an optional fixed foot.
// A keyed split keeps where each pane was scrolled across renders (a tap on a product must not send the grid back to
// the top). The map's values never move, so the panes' scroll events write straight into them; kept for the session.
std::map<String, int32_t> paneScroll;
void onPaneScroll(lv_event_t* e) {
  *(int32_t*)lv_event_get_user_data(e) = lv_obj_get_scroll_y((lv_obj_t*)lv_event_get_target(e));
}

void restoreScroll(lv_obj_t* sc, int32_t* keep) {
  if (*keep > 0) {
    lv_obj_update_layout(sc);
    lv_obj_scroll_to_y(sc, *keep, LV_ANIM_OFF);  // LVGL keeps it inside the content when the list got shorter
  }
  lv_obj_add_event_cb(sc, onPaneScroll, LV_EVENT_SCROLL, keep);
}

void pane(lv_obj_t* row, JSValueConst body, JSValueConst foot, int w, uint32_t bg, int pad, int32_t* keep) {
  lv_obj_t* p = clear(row);
  lv_obj_set_size(p, w, lv_pct(100));
  lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(p, pad, 0);
  lv_obj_set_style_pad_row(p, pad, 0);
  if (bg != 0xfffffffe) lv_obj_set_style_bg_color(p, C(bg), 0), lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
  lv_obj_t* sc = clear(p);
  lv_obj_set_width(sc, lv_pct(100));
  lv_obj_set_flex_grow(sc, 1);
  lv_obj_set_flex_flow(sc, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(sc, pad, 0);
  lv_obj_add_flag(sc, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  lv_obj_set_scroll_dir(sc, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(sc, LV_SCROLLBAR_MODE_OFF);
  build(sc, body, w - 2 * pad);
  if (JS_IsObject(foot)) build(p, foot, w - 2 * pad);
  if (keep) restoreScroll(sc, keep);
}

// ui.split(left, right, {w, footL, footR, bgL, bgR, pad, key}): two panes side by side, each scrolling on its own; with
// a key, each pane stays where it was scrolled when the screen is drawn again.
void nSplit(lv_obj_t* parent, JSValueConst o, int width) {
  if (lv_obj_get_style_flex_flow(parent, 0) != LV_FLEX_FLOW_COLUMN) {  // in a row: what its siblings left
    lv_obj_update_layout(parent);
    int left = lv_obj_get_content_width(parent);
    for (uint32_t i = 0; i < lv_obj_get_child_count(parent); i++)
      left -= lv_obj_get_width(lv_obj_get_child(parent, i)) + lv_obj_get_style_pad_column(parent, 0);
    if (left > 40 && left < width) width = left;
  }
  lv_obj_t* row = clear(parent);
  lv_obj_set_size(row, width, lv_pct(100));
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  const int pad = num(o, "pad", 6), lw = constrain(num(o, "w", width / 2), 40, width - 40);
  JSValue l = sub(o, "a"), r = sub(o, "b"), fl = sub(o, "footL"), fr = sub(o, "footR");
  const String key = str(o, "key");
  pane(row, l, fl, lw, str(o, "bgL").length() ? color(o, "bgL", 0) : 0xfffffffe, pad, key.length() ? &paneScroll[key + "<"] : nullptr);
  pane(row, r, fr, width - lw, str(o, "bgR").length() ? color(o, "bgR", 0) : 0xfffffffe, pad,
       key.length() ? &paneScroll[key + ">"] : nullptr);
  lv_obj_t* right = lv_obj_get_child(row, 1);  // the right pane takes what is left (the split may sit in a row)
  lv_obj_set_flex_grow(right, 1);
  lv_obj_set_width(right, 1);
  for (JSValue v : {l, r, fl, fr}) JS_FreeValue(ctx(), v);
}

// ui.nav(tabs, sel, onPick, {badges, foot}): a rail down the left side in landscape, a bar at the bottom in portrait.
void nNav(lv_obj_t* parent, JSValueConst o, int width) {
  const Pal& p = pal();
  const bool rail = os::landscape();
  const int h = handler(o, "on"), sel = num(o, "sel", 0);
  lv_obj_t* n = box(parent, p.cell, 0);
  // The bar is 56 px: the tabs use the top 50, and the OS's home indicator sits in the strip under the labels.
  lv_obj_set_size(n, rail ? 60 : width, rail ? lv_pct(100) : 56);
  lv_obj_set_flex_flow(n, rail ? LV_FLEX_FLOW_COLUMN : LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(n, rail ? LV_FLEX_ALIGN_START : LV_FLEX_ALIGN_SPACE_AROUND, rail ? LV_FLEX_ALIGN_CENTER : LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_ver(n, rail ? 4 : 0, 0);
  lv_obj_set_style_border_color(n, C(p.line), 0);
  lv_obj_set_style_border_width(n, 1, 0);
  lv_obj_set_style_border_side(n, rail ? LV_BORDER_SIDE_RIGHT : LV_BORDER_SIDE_TOP, 0);
  JSValue tabs = sub(o, "tabs"), badges = sub(o, "badges"), foot = sub(o, "foot");
  int64_t count = 0;
  JS_GetLength(ctx(), tabs, &count);
  const int tw = rail ? 60 : (width - (JS_IsObject(foot) ? 52 : 0)) / std::max<int64_t>(1, count);
  for (int64_t i = 0; i < count && i < 6; i++) {
    JSValue t = JS_GetPropertyUint32(ctx(), tabs, (uint32_t)i), bv = JS_GetPropertyUint32(ctx(), badges, (uint32_t)i);
    const bool on = i == sel;
    const uint32_t fg = on ? (p.dark ? 0x5aa9ff : col::IOS_BLUE) : p.dim;
    lv_obj_t* tab = clear(n);
    lv_obj_set_size(tab, tw, rail ? 48 : 46);
    lv_obj_add_flag(tab, LV_OBJ_FLAG_CLICKABLE);
    if (h >= 0) lv_obj_add_event_cb(tab, onTap, LV_EVENT_CLICKED, tapData(h, i));
    lv_obj_t* pill = box(tab, on ? (p.dark ? 0x16294a : 0xe3efff) : p.cell, 12);
    lv_obj_set_size(pill, 40, 24);
    lv_obj_align(pill, LV_ALIGN_TOP_MID, 0, rail ? 3 : 2);
    if (const lv_image_dsc_t* d = iconOf(str(t, "icon"))) lv_obj_center(glyph(pill, d, fg));
    lv_obj_t* l = label(tab, "", 12, false, fg);  // the colour marks the tab in front (bold would not fit "Customers")
    fitText(l, str(t, "label").c_str(), tw - 2, lineH(12, false), 12, 12, false);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, rail ? 29 : 27);
    const String b = JS_IsUndefined(bv) || JS_IsNull(bv) ? "" : [&] {
      const char* s = JS_ToCString(ctx(), bv);
      String r = s ? s : "";
      JS_FreeCString(ctx(), s);
      return r;
    }();
    if (b.length() && b != "0") {
      lv_obj_t* bb = box(tab, col::ORANGE, 9);
      lv_obj_set_size(bb, LV_SIZE_CONTENT, 17);
      lv_obj_set_style_pad_hor(bb, 4, 0);
      lv_obj_center(label(bb, b.c_str(), 12, true, 0xffffff));
      lv_obj_align(bb, LV_ALIGN_TOP_MID, 16, 0);
    }
    JS_FreeValue(ctx(), t);
    JS_FreeValue(ctx(), bv);
  }
  if (JS_IsObject(foot)) {
    if (rail) {
      lv_obj_t* sp = clear(n);  // the foot at the bottom of the rail
      lv_obj_set_flex_grow(sp, 1);
      lv_obj_set_width(sp, 4);
    }
    build(n, foot, rail ? 56 : 52);
  }
  for (JSValue v : {tabs, badges, foot}) JS_FreeValue(ctx(), v);
}

// ui.input(value, {key, hint, mode, lines, max, w, ok, focus, onChange, onSubmit, keys, keysH, bg, border}): a field
// with the OS keyboard. With a key it keeps its text, cursor and keyboard across renders. mode "chat": sentences (capital
// first letter, Vietnamese typing in Vietnamese); ok: the keyboard's OK key ("Done" by default); focus: the keyboard
// opens when it first shows; keys: a node (a ui.row of buttons) shown on top of the keyboard while this field has it,
// keysH px tall (40; a terminal's Esc, Tab, arrows); onFocus: the keyboard opened on it; bg / border: the field's
// colours; quiet: onChange does not redraw the screen (a chat field); force: the app's value wins
// even while the keyboard is open (a chat field emptied after Send).
struct Field {
  int change = -1, submit = -1, focus = -1;
  bool chat = false, keys = false, quiet = false;
  String ok;
};
void onDone(lv_obj_t* ta) {
  Field* f = (Field*)lv_obj_get_user_data(ta);
  if (f && f->submit >= 0) fireText(f->submit, lv_textarea_get_text(ta));
}
void openKb(lv_obj_t* t) {
  Field* f = (Field*)lv_obj_get_user_data(t);
  const kb::Mode m = f && lv_textarea_get_password_mode(t) ? kb::Mode::Password : f && f->chat ? kb::Mode::Text : kb::Mode::Plain;
  if (f && f->ok.length()) kb::open(t, m, onDone, f->ok.c_str());
  else kb::open(t, m, onDone);
  if (f && f->focus >= 0) fire(f->focus, -1);  // onFocus: the keyboard is up (then a redraw)
  else if (f && f->keys) jsapp::node::redraw();  // the next render builds the field's keys into the keyboard
}
void nInput(lv_obj_t* parent, JSValueConst o, int width) {
  const Pal& p = pal();
  lv_obj_t* ta = kept(o);
  const String mode = str(o, "mode", "text");
  const int lines = constrain(num(o, "lines", 1), 1, 4);
  if (ta) {
    lv_obj_set_parent(ta, parent);
  } else {
    ta = lv_textarea_create(parent);
    lv_obj_set_style_bg_color(ta, C(p.cell), 0);
    lv_obj_set_style_border_width(ta, 0, 0);
    lv_obj_set_style_radius(ta, 10, 0);
    lv_obj_set_style_pad_hor(ta, 10, 0);
    lv_obj_set_style_pad_ver(ta, 7, 0);
    lv_obj_set_style_text_font(ta, fonts::get(15, false), 0);
    lv_obj_set_style_text_color(ta, C(p.fg), 0);
    lv_obj_set_style_text_color(ta, C(p.faint), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_textarea_set_one_line(ta, lines == 1);
    if (mode == "password") lv_textarea_set_password_mode(ta, true);
    if (mode == "number" || mode == "decimal") lv_textarea_set_accepted_chars(ta, mode == "number" ? "0123456789" : "0123456789.,-");
    lv_obj_set_user_data(ta, new Field);
    lv_obj_add_event_cb(ta, [](lv_event_t* e) { delete (Field*)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e)); },
                        LV_EVENT_DELETE, nullptr);
    lv_obj_add_event_cb(ta, [](lv_event_t* e) { openKb((lv_obj_t*)lv_event_get_target(e)); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(ta, [](lv_event_t* e) {
      lv_obj_t* t = (lv_obj_t*)lv_event_get_target(e);
      Field* f = (Field*)lv_obj_get_user_data(t);
      if (f && f->change >= 0) fireText(f->change, lv_textarea_get_text(t), !f->quiet);  // quiet: typing redraws nothing
    }, LV_EVENT_VALUE_CHANGED, nullptr);
    keep(o, ta);
    lv_textarea_set_text(ta, str(o, "v").c_str());
    if (flag(o, "focus")) lv_async_call([](void* t) {  // after the screen is built: the keyboard lifts what it must
      if (lv_obj_is_valid((lv_obj_t*)t)) openKb((lv_obj_t*)t);
    }, ta);
  }
  Field* f = (Field*)lv_obj_get_user_data(ta);
  f->change = handler(o, "onChange"), f->submit = handler(o, "onSubmit"), f->focus = handler(o, "onFocus");
  f->chat = mode == "chat", f->ok = str(o, "ok"), f->quiet = flag(o, "quiet");
  lv_obj_set_style_bg_color(ta, C(color(o, "bg", p.cell)), 0);
  lv_obj_set_style_border_width(ta, str(o, "border").length() ? 1 : 0, 0);
  if (str(o, "border").length()) lv_obj_set_style_border_color(ta, C(color(o, "border", p.faint)), 0);
  JSValue keys = JS_GetPropertyStr(ctx(), o, "keys");
  f->keys = JS_IsObject(keys);
  if (f->keys)
    if (lv_obj_t* row = kb::accessory(ta, constrain(num(o, "keysH", 40), 24, 96))) {
      lv_obj_clean(row);
      build(row, keys, os::W() - 8);
    }
  JS_FreeValue(ctx(), keys);
  lv_textarea_set_placeholder_text(ta, str(o, "hint").c_str());
  lv_textarea_set_max_length(ta, constrain(num(o, "max", 200), 1, 2000));
  const int w = num(o, "w", 0);
  if (lv_textarea_get_one_line(ta) != (lines == 1)) lv_textarea_set_one_line(ta, lines == 1);  // lines may change
  lv_obj_set_size(ta, w > 0 ? std::min(w, width) : width, lines == 1 ? 38 : 16 + lines * 20);
  // Text from the app only while nobody is typing in it (its own text wins over a stale render).
  if ((!kb::isOpen() || flag(o, "force")) && strcmp(lv_textarea_get_text(ta), str(o, "v").c_str()))
    lv_textarea_set_text(ta, str(o, "v").c_str());
}

// ui.rows(items, onTap, {thumb, onStep, gap, card}): compact rows of {title, sub, right, rsub, tone, image, initials,
// bg, badge, sel, step}: a thumbnail (picture or initials on bg) with a count badge, a title that wraps, a sub line,
// a value on the right with a line under it (tone ok|warn|bad), or a -/n/+ stepper (onStep(index * 2 + (up ? 1 : 0))).
void nRows(lv_obj_t* parent, JSValueConst o, int width) {
  const Pal& p = pal();
  const int th = constrain(num(o, "thumb", 0), 0, 64), h = handler(o, "on"), hs = handler(o, "onStep");
  const bool card = flag(o, "card");
  JSValue items = sub(o, "items");
  int64_t n = 0;
  JS_GetLength(ctx(), items, &n);
  lv_obj_t* list = clear(parent);
  lv_obj_set_size(list, width, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(list, card ? num(o, "gap", 5) : 0, 0);
  for (int64_t i = 0; i < n && i < 120; i++) {
    JSValue it = JS_GetPropertyUint32(ctx(), items, (uint32_t)i);
    const bool sel = flag(it, "sel");
    lv_obj_t* r = card || sel ? box(list, sel && !card ? p.cell2 : p.cell, card ? 10 : 6) : clear(list);
    lv_obj_set_size(r, width, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_ver(r, card ? 6 : 5, 0);
    lv_obj_set_style_pad_hor(r, card ? 8 : sel ? 4 : 0, 0);
    if (sel && card) lv_obj_set_style_border_width(r, 2, 0), lv_obj_set_style_border_color(r, C(col::IOS_BLUE), 0);
    if (!card) {
      lv_obj_set_style_border_side(r, LV_BORDER_SIDE_BOTTOM, 0);
      lv_obj_set_style_border_width(r, 1, 0);
      lv_obj_set_style_border_color(r, C(p.line), 0);
    }
    lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    if (h >= 0) lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE), lv_obj_add_event_cb(r, onTap, LV_EVENT_CLICKED, tapData(h, i));
    const int pad = card ? 16 : sel ? 8 : 0;
    int x = 0;
    if (th) {
      lv_obj_t* t = box(r, color(it, "bg", p.cell2), 7);
      lv_obj_set_size(t, th, th);
      const String ini = str(it, "initials");
      if (ini.length()) lv_obj_center(label(t, ini.c_str(), th >= 36 ? 15 : 12, true, color(it, "fg", p.dim)));
      picture(t, str(it, "image"), th, th, 7, 0, sel && !card ? p.cell2 : p.cell);
      const String b = str(it, "badge");
      if (b.length()) {
        lv_obj_t* bb = box(t, p.chipOn, 8);
        lv_obj_set_size(bb, LV_SIZE_CONTENT, 15);
        lv_obj_set_style_pad_hor(bb, 3, 0);
        lv_obj_center(label(bb, b.c_str(), 12, true, p.chipOnFg));
        lv_obj_align(bb, LV_ALIGN_BOTTOM_RIGHT, 2, 2);
      }
      x = th + 7;
    }
    int rw = 0;  // the right side
    const int step = num(it, "step", -1);
    if (step >= 0 && hs >= 0) {
      lv_obj_t* st = box(r, p.cell2, 9);
      lv_obj_set_size(st, 92, 30);
      lv_obj_align(st, LV_ALIGN_RIGHT_MID, 0, 0);
      for (int k = 0; k < 2; k++) {
        lv_obj_t* b = clear(st);
        lv_obj_set_size(b, 30, 30);
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(b, k ? LV_ALIGN_RIGHT_MID : LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_center(label(b, k ? "+" : "\xe2\x88\x92", 17, true, p.fg));
        lv_obj_add_event_cb(b, onTap, LV_EVENT_CLICKED, tapData(hs, i * 2 + k));
      }
      lv_obj_center(label(st, String(step).c_str(), 14, true, p.fg));
      rw = 92 + 6;
    } else {
      const String rt = str(it, "right"), rs = str(it, "rsub");
      if (rt.length() || rs.length()) {
        lv_obj_t* c = clear(r);
        lv_obj_set_size(c, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
        if (rt.length()) label(c, rt.c_str(), 13, true, p.fg);
        if (rs.length()) {
          const String tone = str(it, "tone");
          label(c, rs.c_str(), 12, false, tone == "ok" ? (p.dark ? 0x34c759 : 0x1e9e52) : tone == "warn" ? (p.dark ? 0xff9f43 : 0xc2580c)
                                         : tone == "bad" ? (p.dark ? 0xff6b6b : 0xe0383b) : p.dim);
        }
        lv_obj_align(c, LV_ALIGN_TOP_RIGHT, 0, 0);
        lv_obj_update_layout(c);
        rw = lv_obj_get_width(c) + 6;
      }
    }
    lv_obj_t* tc = clear(r);
    const int tw = std::max(40, width - pad - x - rw);
    lv_obj_set_size(tc, tw, LV_SIZE_CONTENT);
    lv_obj_set_pos(tc, x, 0);
    lv_obj_set_flex_flow(tc, LV_FLEX_FLOW_COLUMN);
    lv_obj_t* t = label(tc, "", 13, true, p.fg);
    wrapText(t, str(it, "title").c_str(), tw, 13, true, 1);
    for (const char* k : {"sub", "sub2"}) {
      const String s2 = str(it, k);
      if (!s2.length()) continue;
      lv_obj_t* l = label(tc, "", 12, false, !strcmp(k, "sub") ? color(it, "subColor", p.dim) : p.dim);
      wrapText(l, s2.c_str(), tw, 12, false, 1);
    }
    if (th) lv_obj_set_style_min_height(tc, th, 0);
    JS_FreeValue(ctx(), it);
  }
  JS_FreeValue(ctx(), items);
}

// ui.qr(text, {size})
void nQr(lv_obj_t* parent, JSValueConst o, int width) {
  const int s = constrain(num(o, "size", 140), 60, std::min(width, 300));
  qr(parent, str(o, "s").c_str(), s);
}

// ui.line(left, right, {bold, size, color, dim, rcolor}): a label and a value on one row.
void nLine(lv_obj_t* parent, JSValueConst o, int width) {
  const Pal& p = pal();
  const int px = constrain(num(o, "size", 13), 12, 24);
  const bool bold = flag(o, "bold");
  const uint32_t fg = color(o, "color", flag(o, "dim") ? p.dim : p.fg);
  lv_obj_t* row = clear(parent);
  lv_obj_set_size(row, width, LV_SIZE_CONTENT);
  lv_obj_t* r = label(row, str(o, "r").c_str(), px, bold, color(o, "rcolor", fg));
  lv_obj_align(r, LV_ALIGN_TOP_RIGHT, 0, 0);
  lv_obj_update_layout(r);
  lv_obj_t* l = label(row, "", px, bold, fg);
  wrapText(l, str(o, "l").c_str(), std::max(40, width - (int)lv_obj_get_width(r) - 8), px, bold, 1);
}

// ui.sheet(children, {onClose, w}): a card over the screen on a dim backdrop; a tap outside calls onClose.
void nSheet(lv_obj_t*, JSValueConst o, int) {
  lv_obj_t* layer = jsapp::node::layer();
  if (!layer) return;
  const Pal& p = pal();
  lv_obj_t* shade = box(layer, 0x000000, 0);
  lv_obj_set_size(shade, lv_pct(100), lv_pct(100));
  lv_obj_set_style_bg_opa(shade, p.dark ? LV_OPA_60 : LV_OPA_40, 0);
  const int hc = handler(o, "onClose");
  lv_obj_add_flag(shade, LV_OBJ_FLAG_CLICKABLE);
  if (hc >= 0) lv_obj_add_event_cb(shade, onTap, LV_EVENT_CLICKED, tapData(hc, 0xfff));
  const int w = std::min(num(o, "w", os::W() - 24), os::W() - 16);
  lv_obj_t* card = box(layer, p.bg, 16);
  lv_obj_set_width(card, w);
  lv_obj_set_height(card, LV_SIZE_CONTENT);
  lv_obj_set_style_max_height(card, os::H() - os::SB_H - 16, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(card, 12, 0);
  lv_obj_set_style_pad_row(card, 7, 0);
  lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);  // taps on the card stay on it
  lv_obj_set_scroll_dir(card, LV_DIR_VER);
  lv_obj_center(card);
  children(card, o, w - 24);
  // A sheet takes the keyboard from a field under it (the sheet's buttons would sit under the keys).
  if (lv_obj_t* f = kb::field()) {
    bool inside = false;
    for (lv_obj_t* q = f; q && !inside; q = lv_obj_get_parent(q)) inside = q == card;
    if (!inside) kb::close();
  }
}

struct Registrar {
  Registrar() {
    jsapp::addNode("image", nImage);
    jsapp::addNode("grid", nGrid);
    jsapp::addNode("split", nSplit);
    jsapp::addNode("nav", nNav);
    jsapp::addNode("input", nInput);
    jsapp::addNode("qr", nQr);
    jsapp::addNode("rows", nRows);
    jsapp::addNode("line", nLine);
    jsapp::addNode("sheet", nSheet);
  }
} registrar;

}  // namespace

const lv_image_dsc_t* icon(const char* name) { return name && *name ? iconOf(String(name)) : nullptr; }

// The app closed: its pictures and icons go (PSRAM back for the next app).
void keepScroll(lv_obj_t* sc, const String& key) { restoreScroll(sc, &paneScroll["col:" + key]); }

void stop() {
  if (decodes)
    trace::add("jsui: %lu pictures decoded, %lu ms each on average, %lu ms at most; cache %lu KB",
               (unsigned long)decodes, (unsigned long)(decodeMs / decodes), (unsigned long)decodeMax,
               (unsigned long)(picBytes >> 10));
  decodes = decodeMs = decodeMax = 0;
  wants.clear();
  for (Pic* p : pics) heap_caps_free(p->px), delete p;
  pics.clear();
  picBytes = 0;
  for (auto& kv : a8s) heap_caps_free((void*)(kv.second->data - 4)), heap_caps_free(kv.second);
  a8s.clear();
  if (decoder) lv_timer_delete(decoder), decoder = nullptr;
}

}  // namespace jsui
