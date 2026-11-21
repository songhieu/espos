#pragma GCC optimize("Os")  // cold code (events, saves, ticks): size first (s3-perf, 2026-11-20)
// The home widget: the chosen dog in its current mood (one still picture, redrawn only when the mood changes)
// and its four needs as the game's gauges. Small 144x138, medium 300x138, large 300x286.
#include <Arduino.h>

#include <vector>

#include "apps.h"
#include "dogs.h"
#include "game.h"
#include "packs.h"
#include "pet.h"
#if !ESPOS_ONBOARD
#include "pet_icon.h"
#endif
#include "text.h"
#include "uikit.h"
#include "view.h"
#include "../wid/kit.h"

namespace pet {

void widgetsTick();
void ensureTicker();

namespace {

using namespace kit;

constexpr uint32_t CREAM = 0xfff3df, INK = 0x2b2f3a, SOFT = 0x8a6d4a, TRACK = 0xf1dfc2, LOW_RED = 0xe8483e;
const uint32_t GAUGE[4] = {0xf0922b, 0x3b8ee6, 0x7b62dd, 0x22ab9b};  // food, fun, rest, clean (the game's)
const char* const ICON[4] = {"g_food", "g_fun", "g_energy", "g_clean"};

struct Live {
  lv_obj_t* root;
  View* view;
  lv_obj_t* bar[4];
  lv_obj_t *lv, *say, *name;
  int size, barW;
  const char* mood;
};
std::vector<Live> lives;

// The picture for a mood: a clip and one of its pictures that reads well small.
void moodPicture(const char* mood, const char*& clip, int& frame) {
  if (!strcmp(mood, "hungry")) clip = "sulk", frame = 12;
  else if (!strcmp(mood, "sleepy")) clip = "lie_down", frame = 25;
  else if (!strcmp(mood, "bored")) clip = "plead", frame = 6;
  else clip = "happy", frame = 0;
}

const char* moodLine(const char* mood) {  // the dog's own line for the mood (the voice pack's text)
  return !strcmp(mood, "hungry") ? "sulk" : !strcmp(mood, "sleepy") ? "lie_down" : !strcmp(mood, "bored") ? "plead" : "happy";
}

// The mood's line that fits the bubble in two rows of the page's font (the shortest one when none does).
String fitLine(const char* on, int w) {
  const lv_font_t* f = pk::font(0);
  String best;
  int bestH = 1 << 30;
  for (int i = 0; i < pk::lineCount(); i++) {
    const pba_clip_t* l = pk::lineAt(i);
    if (strncmp(l->on, on, 16)) continue;
    lv_point_t sz;
    lv_text_get_size(&sz, pk::text(l), f, 0, 1, w, LV_TEXT_FLAG_NONE);
    if (sz.y < bestH) bestH = sz.y, best = pk::text(l);
    if (sz.y <= 2 * f->line_height + 1) return pk::text(l);
  }
  return best;
}

void fill(Live& l) {
  const game::State& s = game::s();
  for (int k = 0; k < 4; k++) {
    const bool low = s.need[k] < game::G.low;
    const int w = max(4, (int)(l.barW * s.need[k] / 100));
    lv_obj_set_width(l.bar[k], w);
    lv_obj_set_style_bg_color(l.bar[k], C(low ? LOW_RED : GAUGE[k]), 0);
  }
  char b[48];
  snprintf(b, sizeof b, l.size == 0 ? T_("Level %d") : T_("Level %d · %d gold"), s.lv, s.coins);
  setText(l.lv, b);
  const char* mood = game::moodRole();
  if (mood != l.mood) {
    l.mood = mood;
    const char* clip;
    int frame;
    moodPicture(mood, clip, frame);
    l.view->still(clip, frame);
    if (l.say) {
      const int w = l.size == 2 ? 252 : 136;
      lv_obj_set_style_text_line_space(l.say, 1, 0);
      int n = 0;  // the dog's words: the page's font (every letter), lines broken to the box
      const String t = text::wrap(pk::font(0), fitLine(moodLine(mood), w), w, n);
      lv_obj_set_style_text_font(l.say, pk::font(0), 0);
      lv_label_set_long_mode(l.say, LV_LABEL_LONG_WRAP);
      lv_obj_set_width(l.say, w);
      lv_label_set_text(l.say, t.c_str());
      lv_obj_center(l.say);
    }
  }
}

// One need: the game's icon and a gauge.
lv_obj_t* gauge(lv_obj_t* p, int k, int x, int y, int barW) {
  const lv_image_dsc_t* ic = pk::image(pk::piece(ICON[k]));
  if (ic) {
    lv_obj_t* i = img(p, ic);
    lv_obj_set_pos(i, x + (16 - ic->header.w) / 2, y - ic->header.h / 2);
  }
  lv_obj_t* t = box(p, wid::SKIN.own ? TRACK : wid::SKIN.dim, 3);
  if (!wid::SKIN.own) lv_obj_set_style_bg_opa(t, 90, 0);
  lv_obj_set_size(t, barW, 6);
  lv_obj_set_pos(t, x + 20, y - 3);
  lv_obj_t* f = box(t, GAUGE[k], 3);
  lv_obj_set_size(f, barW, 6);
  return f;
}

lv_obj_t* missingCard(lv_obj_t* root, int w, int h) {
#if ESPOS_ONBOARD
  lv_obj_t* i = apps::packageIcon(root, "vn.pawboop.pet", 52);
#else
  lv_obj_t* i = img(root, &ic_pet_52);
#endif
  lv_obj_align(i, LV_ALIGN_TOP_LEFT, 6, 6);
  lv_obj_t* t = label(root, "", 14, true, INK);
  fitText(t, N_("Open PawBoop and choose a dog"), w - 24, h - 80, 14, 12, true);
  lv_obj_set_pos(t, 12, 70);
  return root;
}

}  // namespace

lv_obj_t* widget(lv_obj_t* parent, int w, int h, int size) {
  ensureTicker();
  // The card look (wid::create sets it): her cream card, or a skin's colour and text; on glass or no card she
  // keeps a cream picture of her own.
  const wid::Skin& sk = wid::SKIN;
  const uint32_t paper = sk.own || sk.glass || sk.none ? CREAM : sk.bg;
  const uint32_t ink = sk.own ? INK : sk.fg, soft = sk.own ? SOFT : sk.dim;
  lv_obj_set_style_pad_all(parent, 0, 0);
  if (sk.own) lv_obj_set_style_bg_color(parent, C(CREAM), 0);
  lv_obj_set_style_clip_corner(parent, true, 0);
  lv_obj_t* root = clear(parent);
  lv_obj_set_size(root, w, h);
  lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);  // standalone use; inside a widget card the card takes taps
  lv_obj_add_event_cb(root, [](lv_event_t*) { apps::open("pet"); }, LV_EVENT_SHORT_CLICKED, nullptr);
  if (!pk::ready()) return missingCard(root, w, h);

  Live l = {};
  l.root = root, l.size = size, l.mood = nullptr;
  if (size == 0) {  // small: the dog on the left half, name, level and the four gauges on the right
    l.view = View::create(root, 80, h, 1, paper, 40, 22);
    lv_obj_set_pos(l.view->obj(), 0, 0);
    lv_obj_t* n = l.name = label(root, dogs::name(), 14, true, ink);
    lv_obj_set_pos(n, 80, 12);
    l.lv = label(root, "", 12, false, soft);
    lv_obj_set_pos(l.lv, 80, 32);
    l.barW = 34;
    for (int k = 0; k < 4; k++) l.bar[k] = gauge(root, k, 78, 64 + k * 17, l.barW);
  } else if (size == 1) {  // medium: the dog, its name, a line of its own for its mood, the gauges
    l.view = View::create(root, 132, h, 1, paper, 14, 22);
    lv_obj_set_pos(l.view->obj(), 0, 0);
    lv_obj_t* n = l.name = label(root, dogs::name(), 16, true, ink);
    lv_obj_set_pos(n, 136, 10);
    l.lv = label(root, "", 12, false, soft);
    lv_obj_set_pos(l.lv, 136, 31);
    lv_obj_t* b = box(root, INK, 12);
    lv_obj_set_size(b, 152, 44);
    lv_obj_set_pos(b, 134, 50);
    l.say = label(b, "", 13, true, 0xffffff);
    lv_obj_center(l.say);
    lv_obj_set_style_text_align(l.say, LV_TEXT_ALIGN_CENTER, 0);
    l.barW = 50;
    for (int k = 0; k < 4; k++) l.bar[k] = gauge(root, k, 134 + (k % 2) * 78, 108 + (k / 2) * 18, l.barW);
  } else {  // large: the dog at full size, then the same
    l.view = View::create(root, w, 188, 0, paper, (320 - w) / 2, 74);
    lv_obj_set_pos(l.view->obj(), 0, 0);
    lv_obj_t* n = l.name = label(root, dogs::name(), 18, true, ink);
    lv_obj_set_pos(n, 14, 186);
    l.lv = label(root, "", 13, false, soft);
    lv_obj_align(l.lv, LV_ALIGN_TOP_RIGHT, -14, 190);
    lv_obj_t* b = box(root, INK, 12);
    lv_obj_set_size(b, w - 24, 40);
    lv_obj_set_pos(b, 12, 210);
    l.say = label(b, "", 13, true, 0xffffff);
    lv_obj_center(l.say);
    lv_obj_set_style_text_align(l.say, LV_TEXT_ALIGN_CENTER, 0);
    l.barW = 40;
    for (int k = 0; k < 4; k++) l.bar[k] = gauge(root, k, 12 + k * 70, 266, l.barW);
  }
  fill(l);
  lives.push_back(l);
  lv_obj_add_event_cb(root, [](lv_event_t* e) {
    lv_obj_t* o = (lv_obj_t*)lv_event_get_current_target(e);
    for (auto it = lives.begin(); it != lives.end(); ++it)
      if (it->root == o) {
        lives.erase(it);
        break;
      }
  }, LV_EVENT_DELETE, nullptr);
  return root;
}

void widgetsTick() {  // every 30 s from tick(): the gauges, and a new picture when the dog's mood changed
  for (Live& l : lives) fill(l);
}

void widgetsReload() {  // another dog: its name, its picture and its line
  for (Live& l : lives) {
    if (l.name) lv_label_set_text(l.name, dogs::name());
    l.mood = nullptr;
    if (pk::ready()) fill(l);
  }
}

}  // namespace pet
