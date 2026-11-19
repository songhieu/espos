// The dog's pop-up over any screen (LVGL's top layer): the dog playing a clip, what it says in a bubble (its own
// words in the page's font, then the number kit's words in orange as sim/assist.html shows them), and three chips:
// "What time is it?" (the number kit alone), "Weather?" (its weather line and the temperature) and "Play with
// <dog>" (the game). The assistant (assist.cpp) speaks through it; a tap outside closes it, so do 20 s without a
// touch once the dog has finished.
#include <Arduino.h>
#include <ArduinoJson.h>

#include <ctime>

#include "apps.h"
#include "assist.h"
#include "data.h"
#include "dogs.h"
#include "game.h"
#include "os.h"
#include "packs.h"
#include "text.h"
#include "pet.h"
#include "talk.h"
#include "trace.h"
#include "uikit.h"
#include "view.h"
#include "voice.h"

namespace pet {
void ensureTicker();

namespace {

using namespace kit;

constexpr int CW = 304, VIEW_H = 196;
constexpr uint32_t INK = 0x2b2f3a, SOFT = 0x8a90a0, PANEL = 0xf3f4f7, WARM = 0xffcf5c, KIT = 0xffb347;

lv_obj_t *layer, *card, *bubble, *said, *answer, *micBtn, *micHint;
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
  } else {
    dismiss();
    apps::open("pet");
  }
}

lv_obj_t* chip(lv_obj_t* parent, const char* text, int k, int w) {
  lv_obj_t* b = box(parent, k == 2 ? WARM : PANEL, 18);
  lv_obj_set_size(b, w, 44);
  lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_opa(b, LV_OPA_70, LV_STATE_PRESSED);
  lv_obj_t* l = label(b, "", 14, true, k == 2 ? 0x5a3a00 : INK);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  fitText(l, text, w - 12, 40, 14, 11, true, 0);  // two bold lines fit (19 px each)
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
    view = nullptr, card = bubble = said = answer = nullptr;
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
  keep(&card, box(layer, 0xffffff, 26));
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
  lv_obj_t* name = label(card, dogs::name(), 16, true, INK);
  lv_obj_add_flag(name, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_set_pos(name, 48, 15);

  // What it says.
  keep(&bubble, box(col, INK, 16));
  lv_obj_set_size(bubble, land ? colW : CW - 24, LV_SIZE_CONTENT);
  lv_obj_set_style_min_height(bubble, land ? 110 : 64, 0);
  lv_obj_set_style_pad_hor(bubble, 12, 0);
  lv_obj_set_style_pad_ver(bubble, 10, 0);
  lv_obj_set_flex_flow(bubble, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(bubble, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(bubble, 4, 0);
  keep(&said, label(bubble, "", 16, true, 0xffffff));
  lv_obj_set_style_text_align(said, LV_TEXT_ALIGN_CENTER, 0);
  keep(&answer, label(bubble, "", 14, false, 0xd6dae3));
  lv_obj_set_style_text_align(answer, LV_TEXT_ALIGN_CENTER, 0);

  // The dog itself: full size, the stage's rows 62..257 (its head to its feet in nearly every clip).
  lv_obj_t* stage = clear(land ? card : col);
  lv_obj_set_size(stage, stageW, VIEW_H);
  if (land) lv_obj_set_pos(stage, 0, 46);
  view = View::create(stage, stageW, VIEW_H, 0, 0xffffff, (320 - stageW) / 2, 62);
  lv_obj_set_pos(view->obj(), 0, 0);
  if (!land) {
    lv_obj_t* tail = img(stage, pk::image(pk::piece("tail_down")));  // the game's bubble point, towards the dog
    lv_obj_set_pos(tail, CW / 2 - 6, -1);
  }
  // Talk to the dog (the AI assistant, talk.cpp): hold the round button, speak, let go.
  if (talk::canTalk()) {
    keep(&micBtn, box(stage, 0xff7a45, 28));
    lv_obj_set_size(micBtn, 56, 56);
    lv_obj_align(micBtn, LV_ALIGN_BOTTOM_RIGHT, -8, -22);
    lv_obj_add_flag(micBtn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_outline_color(micBtn, C(0xff7a45), 0);
    lv_obj_set_style_outline_opa(micBtn, LV_OPA_40, 0);
    lv_obj_set_style_outline_width(micBtn, 0, 0);
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
    lv_obj_set_style_bg_color(micHint, C(0xffffff), 0);
    lv_obj_set_style_bg_opa(micHint, LV_OPA_80, 0);
    lv_obj_set_style_radius(micHint, 6, 0);
    lv_obj_set_style_pad_hor(micHint, 3, 0);
    hintText(N_("Hold to talk"));  // i18n: under the round mic button, very short
  }

  // Three chips: the time, the weather, the game.
  // i18n: short buttons asking the dog (the dog answers in its own voice)
  const String play = i18n::fmt(N_("Play with {0}"), dogs::name());
  const char* chips[3] = {N_("What time is it?"), N_("Weather?"), play.c_str()};
  lv_obj_t* row = clear(col);
  lv_obj_set_style_margin_top(row, 8, 0);
  const int cw = ((land ? colW : CW - 24) - 16) / 3;  // a row of three, both ways (two lines each when long)
  lv_obj_set_size(row, land ? colW : CW - 24, 44);
  for (int k = 0; k < 3; k++) {
    lv_obj_t* c = chip(row, chips[k], k, cw);
    lv_obj_set_pos(c, k * (cw + 8), 0);
  }

  // Slide up.
  lv_obj_set_style_translate_y(card, 60, 0);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, card);
  lv_anim_set_values(&a, 60, 0);
  lv_anim_set_duration(&a, 220);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_set_exec_cb(&a, [](void* o, int32_t v) { lv_obj_set_style_translate_y((lv_obj_t*)o, v, 0); });
  lv_anim_start(&a);
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

void summonTick() {  // from tick(): close after 20 s without a touch
  if (layer && millis() - touchedAt > 20000 && !voice::busy() && !talk::busy()) dismiss();
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
}

void talkLevel(int pct) {
  if (micBtn) lv_obj_set_style_outline_width(micBtn, pct < 0 ? 0 : 2 + pct / 8, 0);
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

}  // namespace pet
