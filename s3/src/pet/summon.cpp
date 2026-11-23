#pragma GCC optimize("Os")  // the pop-up's UI: nothing hot; the firmware must fit (0.5.7 flash diet)
// The dog's pop-up over any screen (LVGL's top layer): the dog playing a clip, what it says in a bubble (its own
// words in the page's font, then the number kit's words in orange as sim/assist.html shows them), and three chips:
// "What time is it?" (the number kit alone), "Weather?" (its weather line and the temperature) and "Play with
// <dog>" (the game). The assistant (assist.cpp) speaks through it; a tap outside closes it, so do 20 s without a
// touch once the dog has finished.
#include <Arduino.h>
#include <ArduinoJson.h>

#include <ctime>

#include "apps.h"
#include "assistant.h"
#include "assist.h"
#include "data.h"
#include "dogs.h"
#include "game.h"
#include "lic.h"
#include "os.h"
#include "packs.h"
#include "text.h"
#include "pet.h"
#include "talk.h"
#include "trace.h"
#include "uikit.h"
#include "view.h"
#include "voice.h"
#include "theme.h"

namespace pet {
void ensureTicker();

namespace {

using namespace kit;

constexpr int CW = 304, VIEW_H = 196, CHIP_H = 38;
constexpr uint32_t INK = 0x2b2f3a, SOFT = 0x8a90a0, PANEL = 0xf3f4f7, WARM = 0xffcf5c, KIT = 0xffb347;
// The card's colours follow the OS theme (owner, 2026-11-20: the white card looked "trắng bóc" on the color S3):
// Light keeps the white card; Color, Dark and Photo get a warm cream one (below).
uint32_t cCard = 0xffffff, cName = INK, cPanel = PANEL, cChip = INK, cBubble = INK;
void pickColors() {
  // A dark card was tried first: the dog's pictures are drawn for a light ground (their soft edges are light), so on
  // navy they showed a pale halo. So the colour themes get a warm cream card (the dog's edges blend into it) with
  // peach chips; Light keeps white.
  const bool warm = strcmp(theme::kindId(), "light") != 0;
  cCard = warm ? 0xfff4e6 : 0xffffff;
  cName = INK;
  cPanel = warm ? 0xffe3c7 : PANEL;
  cChip = warm ? 0x5a3517 : INK;
  cBubble = warm ? 0x3b2a20 : INK;
}

lv_obj_t *layer, *card, *bubble, *words_, *said, *answer, *micBtn, *micHint, *micHalo;  // words_: scrolls in the bubble
int haloStep = -1;  // the halo's last level (0..4): it is redrawn only when that changes
lv_obj_t *col_, *chipsRow, *lockRow, *trialChip;  // the column, the four chips, the unlock QR, "{n} left"
uint32_t holdUntil = 0;                            // the pop-up stays at least until (the unlock QR: a minute)
int textW_ = CW - 48;
View* view;
uint32_t touchedAt = 0;

int openTodos(int* done = nullptr) {
  JsonDocument doc;
  int open_ = 0, d = 0;
  if (data::loadJson("s3.todo", doc))
    for (JsonObjectConst t : doc.as<JsonArrayConst>()) (t["done"] | false) ? d++ : open_++;
  if (done) *done = d;
  return open_;
}

// A long answer (today's news) never runs off the card and is never cut: the bubble takes the room the card has
// left (portrait: the card may reach 8 px under the screen's top; landscape: the column, 8 px kept under it) and
// past that its words scroll, from the first line, in a window of whole lines (no half line at its edge), a thin
// bar on the right showing there is more. Short words stay centred as before.
void fitBubble() {
  if (!bubble || !words_ || !col_ || !card) return;
  lv_obj_set_style_max_height(words_, LV_COORD_MAX, 0);
  lv_obj_update_layout(card);
  int others = lv_obj_get_style_pad_top(col_, 0);
  for (uint32_t i = 0; i < lv_obj_get_child_count(col_); i++) {
    lv_obj_t* c = lv_obj_get_child(col_, i);
    if (c == bubble || lv_obj_has_flag(c, LV_OBJ_FLAG_HIDDEN) || lv_obj_has_flag(c, LV_OBJ_FLAG_IGNORE_LAYOUT)) continue;
    others += lv_obj_get_height(c) + lv_obj_get_style_margin_top(c, 0) + lv_obj_get_style_margin_bottom(c, 0);
  }
  const int room = os::landscape() ? lv_obj_get_height(card) - 8 : os::H() - 16 - lv_obj_get_style_pad_bottom(card, 0);
  const int pads = lv_obj_get_style_pad_top(bubble, 0) + lv_obj_get_style_pad_bottom(bubble, 0);
  int maxH = max(40, room - others - pads);
  const bool over = lv_obj_get_height(words_) > maxH;
  if (over) {
    const int lh = lv_font_get_line_height(lv_obj_get_style_text_font(said, 0)) + lv_obj_get_style_text_line_space(said, 0);
    if (lh > 0) maxH = max(2, maxH / lh) * lh;
  }
  lv_obj_set_style_max_height(words_, maxH, 0);
  lv_obj_set_flex_align(words_, over ? LV_FLEX_ALIGN_START : LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  if (over) lv_obj_add_flag(words_, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  else lv_obj_remove_flag(words_, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  lv_obj_set_scrollbar_mode(words_, over ? LV_SCROLLBAR_MODE_ON : LV_SCROLLBAR_MODE_OFF);
  lv_obj_scroll_to_y(words_, 0, LV_ANIM_OFF);
}

// The dog's words in the page's font, broken into lines to the bubble's width (bold 14, bold 12 when longer).
void setLabel(lv_obj_t* l, const String& s, int maxLines) {
  if (!s.length()) return lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(l, LV_OBJ_FLAG_HIDDEN);
  text::words(l, s, textW_, maxLines);
}

// The bubble: the dog's words (Lexend: every letter of its lines) and, under them, `extra` (the kit's words in
// orange, or a line of the device's own in the OS's font).
void showText(const String& words, const String& extra, bool kitWords) {
  if (!card) return;
  setLabel(said, words, os::landscape() ? 6 : 4);
  if (!extra.length()) {
    lv_obj_add_flag(answer, LV_OBJ_FLAG_HIDDEN);
  } else if (kitWords) {
    lv_obj_set_style_text_color(answer, C(KIT), 0);
    setLabel(answer, extra, 2);
  } else {
    lv_obj_set_style_text_color(answer, C(0xd6dae3), 0);
    lv_obj_remove_flag(answer, LV_OBJ_FLAG_HIDDEN);
    fitText(answer, extra.c_str(), textW_, 40, 14, 12, false, 2);
  }
  fitBubble();
}

// One of the dog's game lines (never the one it just said) with a line of the device's own under it.
void show(const pba_clip_t* l, const String& extra) {
  if (!card) return;
  l = voice::notAgain(l);
  for (const char* alt : {"happy", "look_up", "stand", "nudge", "alert"})
    if (!l) l = voice::lineFor(alt);
  const bool spoke = l && voice::speak(l, true);
  showText(spoke ? String(pk::text(l)) : String(), extra, false);
}

void afterGreeting(View* v, void*) { v->play("happy"); }  // then it waits, wagging

void speech(const assist::Speech& s, const char* clip) {
  view->play(clip);
  view->onEnd(afterGreeting, nullptr);
  showText(s.said, s.kit, true);  // the kit's words in orange, as the page (alone for "What time is it?")
  assist::play(s);
}

// What time is it: the number kit alone ("bây giờ là sáu giờ rưỡi sáng").
void tellTime() {
  const time_t now = time(nullptr);
  tm t;
  localtime_r(&now, &t);
  assist::Speech s;
  s.moment = "time";
  if (now < 1700000000 || !assist::ready()) return show(pk::line("look_up"), T_("The clock is not set yet."));
  assist::addTime(s, t.tm_hour, t.tm_min, false);
  if (s.items.empty()) return;
  s.items.front().gapMs = 20;
  s.kit += ".";
  s.kit.setCharAt(0, toupper(s.kit[0]));
  speech(s, assist::clipFor("time"));
}

void tellWeather() {
  const char* m;
  int temp;
  assist::Speech s;
  if (!assist::weatherNow(m, temp) || !assist::line(m, s)) return show(pk::line("look_up"), T_("No weather yet."));
  if (strcmp(m, "w_air")) assist::addTemp(s, temp);
  if (s.kit.length()) s.kit += ".";
  speech(s, assist::clipFor(m));
}

void greet(const char* reason) {
  const String r = reason ? reason : "";
  const char* mood = game::moodRole();
  String extra;
  const char* clip = "nudge";
  const char* line = "hello";
  if (r == "remind") {
    int done = 0;
    const int left = openTodos(&done);
    clip = "high_five", line = "high_five";
    extra = left ? i18n::plural(left, NP_("One done! Just {n} task left.|One done! Just {n} tasks left."))
                 : String(T_("All tasks done, great job!"));
  } else if (r == "hungry" || r == "sleepy" || r == "bored") {
    clip = !strcmp(mood, "idle") ? "nudge" : r == "hungry" ? "beg" : r == "sleepy" ? "lie_down" : "plead";
    line = clip;
    extra = i18n::fmt(N_("Open PawBoop to look after {0}."), dogs::name());
  } else if (assist::ready()) {
    // Called up (Control Center, a gesture, the book): the greeting of this part of the day.
    const time_t now = time(nullptr);
    tm t;
    localtime_r(&now, &t);
    const int h = now > 1700000000 ? t.tm_hour : 9;
    const char* m = h >= 4 && h <= 10 ? "hi_morning" : h <= 12 && h >= 11 ? "hi_noon" : h >= 13 && h <= 17 ? "hi_afternoon"
                    : h >= 18 && h <= 21 ? "hi_evening" : h >= 22 ? "good_night" : "late_night";
    assist::Speech s;
    if (assist::line(m, s)) {
      speech(s, assist::clipFor(m));
      trace::add("pet: summoned (%s)", reason ? reason : "");
      return;
    }
  }
  view->play(clip);
  view->onEnd(afterGreeting, nullptr);
  show(voice::lineFor(line[0] == 'h' && !strcmp(line, "hello") ? "@hello" : line), extra);
  trace::add("pet: summoned (%s)", reason ? reason : "");
}

void onChip(lv_event_t* e) {
  touchedAt = millis();
  const int k = (int)(intptr_t)lv_event_get_user_data(e);
  if (k == 0) {
    tellTime();
  } else if (k == 1) {
    tellWeather();
  } else if (k == 3) {
    talk::askNews();
  } else {
    dismiss();
    apps::open("pet");
  }
}

lv_obj_t* chip(lv_obj_t* parent, const char* text, int k, int w) {
  lv_obj_t* b = box(parent, k == 2 ? WARM : cPanel, 18);
  lv_obj_set_size(b, w, CHIP_H);
  lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_opa(b, LV_OPA_70, LV_STATE_PRESSED);
  lv_obj_t* l = label(b, "", 14, true, k == 2 ? 0x5a3a00 : cChip);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  fitText(l, text, w - 12, CHIP_H - 4, 14, 11, true, 0);  // one bold line, or two smaller ones
  lv_obj_center(l);
  lv_obj_add_event_cb(b, onChip, LV_EVENT_CLICKED, (void*)(intptr_t)k);
  return b;
}

// ---- swipe down to close
lv_point_t dragFrom;
int dragY = 0;
bool dragging = false;
// The last few touch samples: the speed at the release is measured over the last ~60 ms, so a short flick counts
// in full and a finger that stopped before lifting counts as still.
constexpr int NS = 8;
int16_t smpY[NS];
uint32_t smpT[NS];
uint32_t smpN = 0;

void sample(int y, uint32_t t) {
  smpY[smpN % NS] = (int16_t)y, smpT[smpN % NS] = t;
  smpN++;
}

float releaseSpeed() {  // px per ms, down > 0
  if (smpN < 2) return 0;
  const int last = (int)((smpN - 1) % NS);
  int old = last;
  for (uint32_t k = 1; k < NS && k < smpN; k++) {
    const int i = (int)((smpN - 1 - k) % NS);
    if (k > 1 && smpT[last] - smpT[i] > 60) break;  // at least the sample before the last
    old = i;
  }
  const uint32_t dt = smpT[last] - smpT[old];
  return dt ? (float)(smpY[last] - smpY[old]) / (float)dt : 0;
}

void slideCard(int from, int to, uint32_t ms, bool close) {
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, card);
  lv_anim_set_values(&a, from, to);
  lv_anim_set_duration(&a, ms);
  lv_anim_set_path_cb(&a, close ? lv_anim_path_ease_in : lv_anim_path_overshoot);
  lv_anim_set_exec_cb(&a, [](void* o, int32_t v) { lv_obj_set_style_translate_y((lv_obj_t*)o, v, 0); });
  if (close) lv_anim_set_completed_cb(&a, [](lv_anim_t*) { dismiss(); });
  lv_anim_start(&a);
}

void onDrag(lv_event_t* e) {
  if (!card) return;
  const lv_event_code_t code = lv_event_get_code(e);
  lv_point_t p;
  lv_indev_t* in = lv_indev_active();
  if (in) lv_indev_get_point(in, &p);
  const uint32_t now = millis();
  if (code == LV_EVENT_PRESSED) {
    dragFrom = p, dragY = 0, dragging = false, smpN = 0;
    sample(p.y, now);
    return;
  }
  if (code == LV_EVENT_PRESSING) {
    sample(p.y, now);
    const int dy = p.y - dragFrom.y, dx = p.x - dragFrom.x;
    if (!dragging && dy > 10 && dy > 2 * abs(dx)) dragging = true;  // down, and more down than sideways
    if (!dragging) return;
    touchedAt = now;
    dragY = dy > 0 ? dy : 0;
    lv_obj_set_style_translate_y(card, dragY, 0);
    return;
  }
  if (!dragging) return;  // a tap: the card's own children got it
  dragging = false;
  sample(p.y, now);
  const float v = releaseSpeed();
  const int h = lv_obj_get_height(card);
  if (dragY > h * 3 / 10 || (v > 0.5f && dragY > 24)) {
    trace::add("pet: swiped down (%d px, %.2f px/ms)", dragY, v);
    slideCard(dragY, h + 40, 160, true);
  } else {
    trace::add("pet: swipe let go (%d px, %.2f px/ms): stays", dragY, v);
    slideCard(dragY, 0, 260, false);
  }
}

// ---- the backdrop (board 0.5.6: ~200 ms UI loops while the pop-up was up). The pop-up lives on the top layer, and
// LVGL draws the active screen under the top layer whatever covers it, so each of the dog's pictures also redrew
// Home's wallpaper and widgets under a 40 % dim. Over Home the screen is hidden while the pop-up is up and the layer
// becomes a solid dark cover: then a dog picture costs only its own pixels. (2026-11-21, owner: "agent vẫn lag quá":
// it was a snapshot of Home, dimmed, taken 0.9 s after opening: 172 ms of a frozen screen on the 2.8", up to 0.7 MB of
// PSRAM, and that picture under every redraw.)
constexpr uint32_t COVER = 0x1f1a17;  // the cover behind the pop-up: the warm card's own dark
lv_obj_t* hiddenScr = nullptr;

void unhideScreen() {
  if (hiddenScr && lv_obj_is_valid(hiddenScr)) lv_obj_remove_flag(hiddenScr, LV_OBJ_FLAG_HIDDEN);
  hiddenScr = nullptr;
}

void dropBackdrop() { unhideScreen(); }

// Back to the plain 40 % dim layer over a visible screen (no cover).
void plainDim() {
  unhideScreen();
  if (layer) lv_obj_set_style_bg_color(layer, C(0x000000), 0), lv_obj_set_style_bg_opa(layer, LV_OPA_40, 0);
}

void takeBackdrop() {
  if (!layer) return;
  lv_obj_t* scr = lv_screen_active();
  // Only over Home: a game draws past LVGL (the run, the aquarium) and the apps change under it; there the dim stays.
  if (scr != os::homeScreen()) return plainDim();
  if (hiddenScr == scr) return;
  unhideScreen();
  lv_obj_set_style_bg_color(layer, C(COVER), 0);
  lv_obj_set_style_bg_opa(layer, LV_OPA_COVER, 0);
  lv_obj_add_flag(scr, LV_OBJ_FLAG_HIDDEN);
  hiddenScr = scr;
}

// The trial's last talks (lic.h: "trial", ai_left 10 or fewer): a small chip at the card's top right.
void trialShow(int left) {
  if (!card) return;
  if (left < 0 || left > 10) {
    if (trialChip) lv_obj_delete(trialChip);
    return;
  }
  if (!trialChip) {
    keep(&trialChip, box(card, 0xfff1d6, 11));
    lv_obj_add_flag(trialChip, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(trialChip, LV_SIZE_CONTENT, 22);
    lv_obj_set_style_pad_hor(trialChip, 9, 0);
    lv_obj_t* l = label(trialChip, "", 12, true, 0x8a5a00);
    lv_obj_center(l);
  }
  // i18n: a small chip on the dog's pop-up near the end of the free trial: the talks left (vi "Còn {n} lượt")
  lv_label_set_text(lv_obj_get_child(trialChip, 0), i18n::plural(left, NP_("{n} talk left|{n} talks left")).c_str());
  lv_obj_update_layout(trialChip);
  lv_obj_set_pos(trialChip, lv_obj_get_width(card) - lv_obj_get_width(trialChip) - 12, 14);
}

// The words under the mic button: centred under it while they fit, longer ones end 4 px from the card's edge (the
// button sits 8 px in), never cut. 12 px is the smallest text font.
void hintText(const char* t) {
  fitText(micHint, t, 160, 20, 12, 12, true, 0);
  const char* s = lv_label_get_text(micHint);
  const int w = lv_text_get_width(s, strlen(s), lv_obj_get_style_text_font(micHint, 0), lv_obj_get_style_text_letter_space(micHint, 0));
  const int over = (w + 6) / 2 - (lv_obj_get_width(micBtn) / 2 + 4);  // + the backing's padding
  lv_obj_align_to(micHint, micBtn, LV_ALIGN_OUT_BOTTOM_MID, over > 0 ? -over : 0, 2);
}

void build() {
  keep(&layer, box(lv_layer_top(), 0x000000, 0));
  lv_obj_set_size(layer, os::W(), os::H());
  lv_obj_set_style_bg_opa(layer, LV_OPA_40, 0);
  lv_obj_add_flag(layer, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(layer, [](lv_event_t*) { dismiss(); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(layer, [](lv_event_t*) {
    view = nullptr, card = bubble = words_ = said = answer = nullptr;
    holdUntil = 0;
    dropBackdrop();  // the screen shows again
    voice::hush();
    talk::closed();
  }, LV_EVENT_DELETE, nullptr);

  // Portrait: name, bubble, the dog, the three chips (a column, as tall as its content). Landscape: the dog on the
  // left, the bubble and the chips in a column on the right.
  const bool land = os::landscape();
  // Landscape: the dog in a 200 px window (its middle), the column beside it as wide as the rest.
  const int stageW = land ? 200 : CW;
  const int cardW = land ? os::W() - 16 : CW, colX = stageW + 8, colW = cardW - colX - 12;
  textW_ = land ? colW - 24 : CW - 48;
  pickColors();
  keep(&card, box(layer, cCard, 26));
  lv_obj_set_width(card, cardW);
  lv_obj_set_height(card, land ? os::H() - 12 : LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(card, 0, 0);
  lv_obj_set_style_pad_bottom(card, land ? 0 : 12, 0);
  lv_obj_align(card, LV_ALIGN_BOTTOM_MID, 0, -8);
  lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);  // a tap on the card does not close it
  lv_obj_add_event_cb(card, [](lv_event_t*) { touchedAt = millis(); }, LV_EVENT_PRESSED, nullptr);
  // Swipe down to close: the card follows the finger; past 30 % of its height or on a quick flick it goes, otherwise
  // it springs back. Closing stops a turn (talk::closed via the layer's delete), the LED fades, the wake word resumes.
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(card, onDrag, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(card, onDrag, LV_EVENT_PRESSING, nullptr);
  lv_obj_add_event_cb(card, onDrag, LV_EVENT_RELEASED, nullptr);
  lv_obj_add_event_cb(card, onDrag, LV_EVENT_PRESS_LOST, nullptr);
  lv_obj_t* col = clear(card);  // portrait: the column
  keep(&col_, col);
  lv_obj_set_size(col, land ? colW : cardW, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_top(col, land ? 46 : 46, 0);
  lv_obj_set_style_pad_row(col, 0, 0);  // the dog right under the bubble, so the bubble's point touches it
  if (land) lv_obj_set_pos(col, colX, 0);

  // Who: the paw badge and the dog's name.
  lv_obj_t* paw = img(card, pk::crop("screen", 8, 6, 28, 28));  // the game's own paw badge
  lv_obj_add_flag(paw, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_set_pos(paw, 13, 11);
  lv_obj_t* name = label(card, dogs::name(), 16, true, cName);
  lv_obj_add_flag(name, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_set_pos(name, 48, 15);

  // What it says.
  keep(&bubble, box(col, cBubble, 16));
  lv_obj_set_size(bubble, land ? colW : CW - 24, LV_SIZE_CONTENT);
  lv_obj_set_style_min_height(bubble, land ? 110 : 64, 0);
  lv_obj_set_style_pad_left(bubble, 12, 0);  // the right: the words' own 12 px (their scroll bar lives there)
  lv_obj_set_style_pad_ver(bubble, 10, 0);
  lv_obj_set_flex_flow(bubble, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(bubble, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  keep(&words_, clear(bubble));
  lv_obj_set_size(words_, textW_ + 12, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_right(words_, 12, 0);
  lv_obj_set_flex_flow(words_, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(words_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(words_, 4, 0);
  // A long answer scrolls (fitBubble): a thin light bar on the right, in the words' right padding.
  lv_obj_set_style_bg_color(words_, C(0xffffff), LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_opa(words_, LV_OPA_50, LV_PART_SCROLLBAR);
  lv_obj_set_style_width(words_, 3, LV_PART_SCROLLBAR);
  lv_obj_set_style_radius(words_, 2, LV_PART_SCROLLBAR);
  lv_obj_set_style_pad_right(words_, 2, LV_PART_SCROLLBAR);
  lv_obj_set_scroll_dir(words_, LV_DIR_VER);  // up and down only (no sideways bar)
  lv_obj_add_event_cb(words_, [](lv_event_t*) { touchedAt = millis(); }, LV_EVENT_PRESSING, nullptr);  // reading
  keep(&said, label(words_, "", 16, true, 0xffffff));
  lv_obj_set_style_text_align(said, LV_TEXT_ALIGN_CENTER, 0);
  keep(&answer, label(words_, "", 14, false, 0xd6dae3));
  lv_obj_set_style_text_align(answer, LV_TEXT_ALIGN_CENTER, 0);

  // The dog itself: full size, the stage's rows 62..257 (its head to its feet in nearly every clip).
  lv_obj_t* stage = clear(land ? card : col);
  lv_obj_set_size(stage, stageW, VIEW_H);
  if (land) lv_obj_set_pos(stage, 0, 46);
  view = View::create(stage, stageW, VIEW_H, 0, cCard, (320 - stageW) / 2, 62);
  lv_obj_set_pos(view->obj(), 0, 0);
  if (!land) {
    lv_obj_t* tail = img(stage, pk::image(pk::piece("tail_down")));  // the game's bubble point, towards the dog
    lv_obj_set_pos(tail, CW / 2 - 6, -1);
  }
  // Talk to the dog (the AI assistant, turn.cpp): hold the round button, speak, let go. Only while the dog is the
  // device's assistant (app-slots.md §5: another app in the slot answers through its own pop-up).
  if (talk::canTalk() && assistant::dog()) {
    // The voice's level: a fixed 84 px halo behind the button whose opacity follows it in 5 steps. (It was the
    // button's outline width, changed every 40 ms: each change re-measured and redrew the card under the 40 % dim
    // layer, ~200 ms UI loops on the board while listening, 0.5.6.)
    keep(&micHalo, box(stage, 0xff7a45, 42));
    lv_obj_set_size(micHalo, 84, 84);
    lv_obj_align(micHalo, LV_ALIGN_BOTTOM_RIGHT, -8 + 14, -22 + 14);
    lv_obj_set_style_bg_opa(micHalo, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(micHalo, LV_OBJ_FLAG_CLICKABLE);
    haloStep = -1;
    keep(&micBtn, box(stage, 0xff7a45, 28));
    lv_obj_set_size(micBtn, 56, 56);
    lv_obj_align(micBtn, LV_ALIGN_BOTTOM_RIGHT, -8, -22);
    lv_obj_add_flag(micBtn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(talk::micIcon(micBtn, 0xffffff));
    lv_obj_add_event_cb(micBtn, [](lv_event_t*) {
      touchedAt = millis();
      talk::press();
    }, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(micBtn, [](lv_event_t*) { talk::release(); }, LV_EVENT_RELEASED, nullptr);
    lv_obj_add_event_cb(micBtn, [](lv_event_t*) { talk::release(); }, LV_EVENT_PRESS_LOST, nullptr);
    keep(&micHint, label(stage, "", 12, true, SOFT));
    lv_obj_set_style_text_align(micHint, LV_TEXT_ALIGN_CENTER, 0);
    // A long hint reaches over the dog's picture: a white backing keeps it readable there.
    lv_obj_set_style_bg_color(micHint, C(cCard), 0);
    lv_obj_set_style_bg_opa(micHint, LV_OPA_80, 0);
    lv_obj_set_style_radius(micHint, 6, 0);
    lv_obj_set_style_pad_hor(micHint, 3, 0);
    hintText(N_("Hold to talk"));  // i18n: under the round mic button, very short
  }

  // Four chips, two by two: the time, the weather / today's news (AI), the game.
  // i18n: short buttons asking the dog (the dog answers in its own voice)
  const String play = i18n::fmt(N_("Play with {0}"), dogs::name());
  // i18n: a short button on the dog's pop-up: the dog sums up today's news (vi "Tin hôm nay")
  const char* chips[4] = {N_("What time is it?"), N_("Weather?"), play.c_str(), N_("Today's news")};
  const int at[4] = {0, 1, 3, 2};  // the grid's places: news under the time, the game under the weather
  lv_obj_t* row = clear(col);
  keep(&chipsRow, row);
  lv_obj_set_style_margin_top(row, 8, 0);
  const int rw = land ? colW : CW - 24, cw = (rw - 8) / 2;
  lv_obj_set_size(row, rw, CHIP_H * 2 + 6);
  for (int k = 0; k < 4; k++) {
    if (k == 3 && (!talk::canChat() || !assistant::dog())) continue;  // no AI here (not linked, or not the assistant)
    lv_obj_t* c = chip(row, chips[k], k, cw);
    lv_obj_set_pos(c, (at[k] % 2) * (cw + 8), (at[k] / 2) * (CHIP_H + 6));
  }

  trialShow(lic::ai() == lic::Ai::Trial ? lic::aiLeft() : -1);

  // The cover at once, and no slide-up (owner, 2026-11-21: "nhẹ giao diện lại"): the card is simply there, drawn once.
  takeBackdrop();
}

bool open_() {
  ensureTicker();
  if (!pk::ready()) {
    os::toast(N_("Choose a dog in PawBoop first"));
    return false;
  }
  touchedAt = millis();
  if (!layer) build();
  lv_obj_move_foreground(layer);
  return true;
}

}  // namespace

void summon(const char* reason) {
  if (open_()) greet(reason);
}

void showSpeech(const assist::Speech& s, const char* clip, bool wake) {
  if (wake && os::saverOn()) os::wake();
  if (!open_()) return;
  speech(s, clip);
  trace::add("pet: assistant says %s", s.moment);
}

void dismiss() {
  if (!layer) return;
  voice::hush();
  lv_obj_t* l = layer;
  layer = nullptr;  // keep() clears the others on delete
  lv_obj_delete_async(l);
  trace::add("pet: dismissed");
}

bool summoned() { return layer != nullptr; }

void summonTick() {  // from tick(): close after 20 s without a touch (the unlock QR: a minute)
  // Another screen came up under the pop-up (an app opened from a chip, Home after an app): a new backdrop.
  if (layer && hiddenScr && lv_screen_active() != hiddenScr) takeBackdrop();  // (not Home: the dim)
  if (layer && millis() - touchedAt > 20000 && (int32_t)(millis() - holdUntil) > 0 && !voice::busy() && !talk::busy())
    dismiss();
}

// ---------------------------------------------------------------- the AI turn (talk.cpp)
bool talkOpen() {
  if (os::saverOn()) os::wake();
  if (!open_()) return false;
  voice::hush();
  return true;
}

// The dog's clip (nullptr: keep it), and the bubble: `big` in the dog's words (its reply, or what it is doing), `small`
// under it (what it heard, or a hint). Both empty: the bubble keeps what it shows.
void talkShow(const char* clip, const String& big, const String& small) {
  if (!card) return;
  touchedAt = millis();
  if (clip && view) {
    view->onEnd(nullptr, nullptr);
    view->play(clip);
  }
  if (!big.length() && !small.length()) return;
  setLabel(said, big, os::landscape() ? 7 : 6);
  if (!small.length()) {
    lv_obj_add_flag(answer, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_set_style_text_color(answer, C(0xd6dae3), 0);
    lv_obj_remove_flag(answer, LV_OBJ_FLAG_HIDDEN);
    fitText(answer, small.c_str(), textW_, 40, 13, 11, false, 2);
  }
  fitBubble();
}

void talkTrial(int left) { trialShow(left); }

// Talking is locked (talk.cpp showLock): the QR that unlocks it takes the place of the mic button and the chips; the
// dog's bubble says it in its own words. The pop-up stays a minute (time to take a phone out).
void talkLock(const char* url) {
  if (!card || !col_) return;
  holdUntil = millis() + 60000;
  touchedAt = millis();
  trialShow(-1);
  if (chipsRow) lv_obj_add_flag(chipsRow, LV_OBJ_FLAG_HIDDEN);
  if (micBtn) lv_obj_add_flag(micBtn, LV_OBJ_FLAG_HIDDEN);
  if (micHalo) lv_obj_add_flag(micHalo, LV_OBJ_FLAG_HIDDEN);
  if (micHint) lv_obj_add_flag(micHint, LV_OBJ_FLAG_HIDDEN);
  if (lockRow) lv_obj_delete(lockRow);
  const bool land = os::landscape();
  const int w = lv_obj_get_width(col_) - (land ? 0 : 24), q = 112;  // lock::sheet's code size (tank-game)
  keep(&lockRow, clear(col_));
  lv_obj_set_size(lockRow, w, LV_SIZE_CONTENT);
  lv_obj_set_style_margin_top(lockRow, land ? 10 : 6, 0);
  lv_obj_set_flex_flow(lockRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(lockRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(lockRow, 12, 0);
  lv_obj_t* code = kit::qr(lockRow, url, q);
  lv_obj_set_style_border_color(code, C(0xe4e6ec), 0);  // white on white: a hairline shows the square
  lv_obj_set_style_border_width(code, 1, 0);
  lv_obj_t* txt = clear(lockRow);
  lv_obj_set_size(txt, w - q - 12, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(txt, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(txt, 4, 0);
  lv_obj_t* t = label(txt, "", 15, true, INK);
  // i18n: beside the QR code on the dog's pop-up when talking is locked (the trial is over): a short title
  wrapText(t, T_("Scan to keep talking"), w - q - 12, 15, true);
  lv_obj_t* b = label(txt, "", 13, false, SOFT);
  // i18n: under that title: point the phone's camera at the code; the page opens there
  wrapText(b, T_("Point your phone's camera at the code to open the page."), w - q - 12, 13);
  fitBubble();  // the QR row is taller than the chips: less room for the words
}

void talkLevel(int pct) {
  if (!micHalo) return;
  const int step = pct < 0 ? 0 : 1 + min(3, pct / 25);  // none, then 4 steps of loudness
  if (step == haloStep) return;
  haloStep = step;
  static const lv_opa_t OPA[5] = {LV_OPA_TRANSP, LV_OPA_20, LV_OPA_30, LV_OPA_40, LV_OPA_50};
  lv_obj_set_style_bg_opa(micHalo, OPA[step], 0);
}

void talkButton(int state) {
  if (!micBtn) return;
  lv_obj_set_style_bg_color(micBtn, C(state == 1 || state == 3 ? 0xe5484d : state == 2 ? 0xb8bcc6 : 0xff7a45), 0);
  if (micHint) {
    // i18n: under the round mic button: held / called by "Gâu" (a tap ends it) / ready
    const char* h = state == 1 ? N_("Let go to send") : state == 3 ? N_("Tap when done") : N_("Hold to talk");
    hintText(h);
  }
}

void talkTouched() { touchedAt = millis(); }

// ---------------------------------------------------------------- the dog as the assistant's face (assistant.h Face)
// The OS client (turn.cpp) shows a turn through this when PawBoop is in the assistant slot: this pop-up, its clips,
// and the dog's own recorded words (the answer to its name, "say it again", the lock lines) in its own voice.
namespace {
uint32_t dogSay(const char* what) {
  uint32_t ms = 0;
  if (!strcmp(what, "ack") || !strcmp(what, "ai_trial_end") || !strcmp(what, "ai_locked")) {  // assist.pba moments
    assist::Speech sp;
    if (!assist::line(what, sp) || !assist::play(sp)) return 0;
    for (const voice::Item& it : sp.items) ms += (it.clip ? it.clip->samples / 16 : 0) + it.gapMs;
    return ms ? ms : 1;
  }
  const pba_clip_t* l = pk::line(what);  // a clip the server named, else a line of that mood
  if (!l) l = voice::lineFor(what);
  if (!l) return 0;
  voice::speak(l, true);
  return l->samples / 16 ? l->samples / 16 : 1;
}
bool dogSpeaking() { return voice::busy(); }
void dogHush() { voice::hush(); }
uint32_t dogQuiet() { return voice::quietMs(); }
String dogVoice() {
  const dogs::Info* d = dogs::chosen();
  return d ? d->key : "";
}
}  // namespace

const assistant::Face& face() {
  static const assistant::Face F = {talkOpen, talkShow, talkLevel, talkButton, talkTouched, talkLock, talkTrial,
                                    dogSay, dogSpeaking, dogHush, dogQuiet, dogVoice};
  return F;
}

}  // namespace pet
