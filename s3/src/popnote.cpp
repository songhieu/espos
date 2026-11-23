#pragma GCC optimize("Os")  // cold code: built small
// Server notes for an app: its notifications and pop-ups (see popnote.h).
#include "popnote.h"

#include <Arduino.h>
#include <lvgl.h>

#include <algorithm>
#include <vector>

#include "alarm.h"
#include "appset.h"
#include "sound.h"
#include "apps.h"
#include "desk.h"
#ifndef SIM
#include "esp_random.h"
#endif
#include "i18n.h"
#include "js/jsapp.h"
#include "js/jsnet.h"
#include "lic.h"
#include "notif.h"
#include "os.h"
#include "assistant.h"
#include "pkg/pkg.h"
#include "trace.h"
#include "uikit.h"

namespace popnote {
namespace {

using namespace kit;

struct Act {
  String id, post, body;  // body: JSON text
  bool risky = false;
};

struct Note {
  String app, key, arg, title, head, ptitle, mono, text, say, sig;
  int64_t exp = 0;
  std::vector<Act> acts;
  bool posted = false;  // its notification is in
  bool done = false;    // answered here: no card again
};

std::vector<Note> notes;  // the dashboard's notes for apps, in its order

// ---------------------------------------------------------------- the card
lv_obj_t* card = nullptr;
lv_obj_t* countdown = nullptr;  // "8:42"
lv_obj_t* bar = nullptr;        // its bar
String shownKey;
int64_t shownExp = 0, shownStart = 0;
bool confirming = false, sending = false;
String confirmAct;

bool hasPerm(const String& app, const char* perm) {
  const pkg::Package* p = pkg::find(app.c_str());
  if (!p) return false;
  for (const String& s : p->perms)
    if (s == perm) return true;
  return false;
}

Note* find(const String& key) {
  for (Note& n : notes)
    if (n.key == key) return &n;
  return nullptr;
}

const char* actLabel(const String& id) {
  if (id == "approve") return T_("Approve");  // i18n: a pop-up's button (e.g. Claude asks to run a command)
  if (id == "deny") return T_("Deny");        // i18n: a pop-up's button: refuse what is asked
  if (id == "ok") return T_("OK");
  return nullptr;  // not in the vocabulary: no button
}

void closeCard() {
  if (card) lv_obj_delete(card);
  card = countdown = bar = nullptr;
  shownKey = "";
  confirming = sending = false;
}

void build();

// The app answered (the act's request done): a toast, and the card goes.
void onDone(void* ctx, int status, const char* err, const String& body) {
  String* key = (String*)ctx;
  Note* n = find(*key);
  JsonDocument d;
  deserializeJson(d, body);
  const char* st = d["state"] | "";
  String msg;
  if (status == 428) {  // the server says it is risky: the second tap first, then the same request with ack_risky
    if (n && shownKey == *key) {
      sending = false, confirming = true;
      build();
    }
    delete key;
    return;
  }
  if (status >= 200 && status < 300 && strcmp(st, "refused") && strcmp(st, "failed")) {
    msg = d["text"] | "";
    if (!msg.length()) msg = !strcmp(st, "confirm_mac") ? T_("Waiting for you to confirm it on the Mac") : T_("Done");
    if (n) n->done = true;
    closeCard();
  } else {
    msg = String(T_("Not done: ")) + (d["err"] | (status ? String(status).c_str() : (err ? err : "?")));
    if (n && shownKey == *key) sending = false, build();
  }
  os::toast(msg.c_str());
  delete key;
}

void act(const String& id) {
  Note* n = find(shownKey);
  if (!n || sending) return;
  const Act* a = nullptr;
  for (const Act& x : n->acts)
    if (x.id == id) a = &x;
  if (!a) return;
  if (a->risky && !confirming) {  // a second tap, in plain words, before anything goes
    confirming = true, confirmAct = id;
    build();
    return;
  }
  JsonDocument d;
  deserializeJson(d, a->body);
  char nonce[17];
  uint8_t rnd[8];
#ifdef SIM
  arc4random_buf(rnd, sizeof rnd);
#else
  esp_fill_random(rnd, sizeof rnd);
#endif
  for (int i = 0; i < 16; i++) nonce[i] = "0123456789abcdef"[(rnd[i / 2] >> (i & 1 ? 0 : 4)) & 15];
  nonce[16] = 0;
  d["nonce"] = nonce;
  if (confirming) d["ack_risky"] = true;
  String json;
  serializeJson(d, json);
  String path = a->post;
  while (path.startsWith("/")) path.remove(0, 1);
  String* key = new String(n->key);
  if (!jsnet::osPost(n->app.c_str(), path.c_str(), json, onDone, key)) {
    delete key;
    os::toast(T_("Not done: no connection"));
    return;
  }
  sending = true;
  build();
}

void onBtn(lv_event_t* e) {
  const String* id = (const String*)lv_event_get_user_data(e);
  if (id) act(*id);
}

void onOpen(lv_event_t*) {
  Note* n = find(shownKey);
  if (!n) return;
  const String app = n->app, arg = n->arg;
  closeCard();
  os::wake();
  apps::open(app.c_str(), arg.c_str());
}

void onLater(lv_event_t*) {  // away for now: it stays in the Notification Center
  if (Note* n = find(shownKey)) n->done = true;
  closeCard();
}

std::vector<String> btnIds;  // the buttons' ids (their event data points here)

lv_obj_t* textBtn(lv_obj_t* parent, const char* text, uint32_t bg, uint32_t fg, lv_event_cb_t cb, void* data) {
  lv_obj_t* b = button(parent, text, bg, fg, cb, data);
  lv_obj_set_height(b, 40);
  lv_obj_set_flex_grow(b, 1);
  return b;
}

void build() {
  Note* n = find(shownKey);
  if (!n) return closeCard();
  const Pal& p = pal();
  if (card) lv_obj_clean(card);
  else {
    keep(&card, box(lv_layer_top(), p.dark ? 0x141923 : 0xf4f4f7, 18));  // null again if the OS rebuilds its layers
    lv_obj_set_style_shadow_width(card, 30, 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_40, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_set_style_pad_row(card, 6, 0);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);  // taps on the card stay on it
  }
  const int w = os::landscape() ? std::min(420, os::W() - 30) : os::W() - 20;
  lv_obj_set_width(card, w);
  lv_obj_set_height(card, LV_SIZE_CONTENT);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, os::SB_H + 6);
  lv_obj_move_foreground(card);
  const int iw = w - 24;
  // the app, the place, Open ›, ×
  lv_obj_t* head = clear(card);
  lv_obj_set_size(head, iw, 28);
  lv_obj_t* ic = notif::icon(head, n->app.c_str(), 28);
  if (ic) lv_obj_align(ic, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_t* later = label(head, "\xc3\x97", 20, true, p.dim);  // ×
  lv_obj_align(later, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_add_flag(later, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(later, 12);
  lv_obj_add_event_cb(later, onLater, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* open = label(head, T_("Open"), 14, true, p.link);  // i18n: a pop-up: open the app on this
  lv_obj_align(open, LV_ALIGN_RIGHT_MID, -30, 0);
  lv_obj_add_flag(open, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(open, 10);
  lv_obj_add_event_cb(open, onOpen, LV_EVENT_CLICKED, nullptr);
  lv_obj_update_layout(open);
  lv_obj_t* who = label(head, "", 13, true, p.fg);
  const String whoText = notif::name(n->app.c_str()) + (n->head.length() ? "  ·  " + n->head : String());
  fitText(who, whoText.c_str(), iw - 36 - 30 - (int)lv_obj_get_width(open) - 12, 26, 13, 11, true);
  lv_obj_align(who, LV_ALIGN_LEFT_MID, 36, 0);
  // the request
  int more = -1;
  for (const Note& x : notes)
    if (x.app == n->app && !x.acts.empty() && !x.done) more++;
  lv_obj_t* t = label(card, "", 15, true, confirming ? 0xe0383b : p.fg);
  String title = confirming ? String(T_("Are you sure?")) : (n->ptitle.length() ? n->ptitle : n->title);
  if (more > 0 && !confirming) title += "  (" + i18n::fmt(N_("{0} more waiting"), String(more).c_str()) + ")";
  wrapText(t, title.c_str(), iw, 15, true, 2);
  if (n->mono.length()) {
    lv_obj_t* m = box(card, p.dark ? 0x0b0e14 : 0xe4e6ec, 9);
    lv_obj_set_size(m, iw, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(m, 7, 0);
    lv_obj_t* ml = label(m, "", 13, false, p.fg);
    wrapText(ml, n->mono.c_str(), iw - 14, 13, false, 2);
  }
  if (confirming) {
    lv_obj_t* why = label(card, "", 13, false, p.fg);
    wrapText(why, T_("Check what it does before you approve it."), iw, 13, false, 2);
  } else if (n->text.length()) {
    lv_obj_t* why = label(card, "", 13, false, p.fg);
    wrapText(why, ("\xe2\x80\x9c" + n->text + "\xe2\x80\x9d").c_str(), iw, 13, false, 2);
  }
  if (n->exp) {
    lv_obj_t* row = clear(card);
    lv_obj_set_size(row, iw, 16);
    keep(&bar, box(row, p.line, 2));
    lv_obj_set_size(bar, iw - 48, 4);
    lv_obj_align(bar, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t* fill = box(bar, 0xff9500, 2);
    lv_obj_set_size(fill, 0, 4);
    keep(&countdown, label(row, "", 12, false, p.dim));
    lv_obj_align(countdown, LV_ALIGN_RIGHT_MID, 0, 0);
  }
  // the buttons
  lv_obj_t* btns = clear(card);
  lv_obj_set_size(btns, iw, 40);
  lv_obj_set_flex_flow(btns, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(btns, 8, 0);
  btnIds.clear();
  btnIds.reserve(4);
  if (sending) {
    lv_obj_t* s = label(btns, T_("Sending"), 14, true, p.dim);  // i18n: a pop-up: the answer is on its way
    lv_obj_set_flex_grow(s, 1);
  } else if (confirming) {
    lv_obj_t* c = textBtn(btns, T_("Cancel"), p.chip, p.chipFg, [](lv_event_t*) { confirming = false, build(); }, nullptr);
    (void)c;
    btnIds.push_back(confirmAct);
    textBtn(btns, actLabel(confirmAct) ? actLabel(confirmAct) : "OK", 0xff3b30, 0xffffff, onBtn, &btnIds.back());
  } else {
    for (const Act& a : n->acts) {
      const char* l = actLabel(a.id);
      if (!l || btnIds.size() >= 3) continue;
      btnIds.push_back(a.id);
      const bool yes = a.id == "approve" || a.id == "ok";
      textBtn(btns, l, yes ? 0x34c759 : p.chip, yes ? 0xffffff : 0xe0383b, onBtn, &btnIds.back());
    }
  }
  tick();
}

// Whether a card may show now (app-slots.md §6: priority high).
bool mayShow(const Note& n) {
  if (n.done || n.acts.empty() || !hasPerm(n.app, "overlay") || !notif::enabled(n.app.c_str()) || notif::quiet()) return false;
  if (jsapp::current() == n.app) return false;  // the app itself is open: it shows the request
  if (assistant::busy() || alarms::ringing()) return false;
  if (n.exp && desk::nowMs() && desk::nowMs() > n.exp) return false;
  return true;
}

void showNext() {
  if (card && find(shownKey) && mayShow(*find(shownKey))) return;
  if (card) closeCard();
  for (Note& n : notes)
    if (mayShow(n)) {
      shownKey = n.key;
      shownExp = n.exp, shownStart = desk::nowMs();
      confirming = sending = false;
      build();
      trace::add("popnote: %s %s", n.app.c_str(), n.key.c_str());
      return;
    }
}

// ---------------------------------------------------------------- spoken announcements (desktop-protocol.md §11)
// A new note speaks its pop.say (a note with actions falls back to its pop.title): at most one announcement every
// 2 minutes, notes that came meanwhile merged into one ("3 requests are waiting for you"), none in quiet hours, Do Not
// Disturb or at night (22-7), none over an assistant turn, an alarm or other sound, none while the app is open in front,
// none when the app's own setting "announce" is off. What could not be said then is dropped, never said later.
// The voice: GET /v1/device/tts (16 kHz PCM, the device's credential, desk::fetch), played through sound::stream.
// The voice is the paid layer (desktop-protocol.md §9.6): 402 (no AI licence, the trial is over) or 429 (the day's 60
// are used) leaves the note's words and its chime only, no spoken or shown error, and no more asking until the next UTC
// day or a change of licence. A licence known to be off does not ask at all.
struct Say {
  String app, key, text;
};
std::vector<Say> toSay;
uint32_t lastSaid = 0, queuedAt = 0;
bool voiceRefused = false;
uint32_t refusedDay = 0, refusedLic = 0;

uint32_t utcDay() {
  const time_t now = time(nullptr);
  return now > 1700000000 ? (uint32_t)(now / 86400) : 0;
}

bool voiceOff() {
  if (voiceRefused && (utcDay() != refusedDay || lic::version() != refusedLic)) voiceRefused = false;
  return voiceRefused || lic::aiLocked();
}
uint8_t* pcm = nullptr;
uint32_t pcmLen = 0;
volatile uint32_t pcmPos = 0;

void queueSay(const Note& n) {
  String text = n.say.length() ? n.say : (!n.acts.empty() ? (n.ptitle.length() ? n.ptitle : n.title) : String());
  if (!text.length() || appset::get(n.app.c_str(), "announce") == "0") return;
  if (toSay.empty()) queuedAt = millis();
  toSay.push_back({n.app, n.key, text.substring(0, 160)});
}

bool night() {
  const time_t now = time(nullptr);
  if (now < 1700000000) return false;
  tm t;
  localtime_r(&now, &t);
  return t.tm_hour >= 22 || t.tm_hour < 7;
}

int pullPcm(short* out, int n, void*) {  // on the sound task: copies only
  const uint32_t left = pcmPos < pcmLen ? (pcmLen - pcmPos) / 2 : 0;
  const int k = std::min<int>(n, left);
  if (k > 0) memcpy(out, pcm + pcmPos, k * 2), pcmPos += k * 2;
  return k;
}

void onVoice(void*, int status, JsonDocument*, const uint8_t* data, size_t len) {
  if (status == 402 || status == 429) voiceRefused = true, refusedDay = utcDay(), refusedLic = lic::version();
  if (status != 200 || len < 320 || sound::streaming()) {
    trace::add("popnote: no voice (%d, %u B)", status, (unsigned)len);
    return;
  }
  heap_caps_free(pcm);
  pcm = (uint8_t*)heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
  if (!pcm) return;
  memcpy(pcm, data, len);
  pcmLen = len & ~1u, pcmPos = 0;
  sound::stream(pullPcm, nullptr);
  trace::add("popnote: speaking %u ms", (unsigned)(pcmLen / 32));
}

String urlenc(const String& s) {
  String o;
  for (unsigned i = 0; i < s.length(); i++) {
    const uint8_t c = s[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c;
    else {
      char b[4];
      snprintf(b, sizeof b, "%%%02X", c);
      o += b;
    }
  }
  return o;
}

void sayTick() {
  // what was answered before it could be said is not said
  toSay.erase(std::remove_if(toSay.begin(), toSay.end(), [](const Say& x) { return !find(x.key); }), toSay.end());
  if (toSay.empty() || millis() - queuedAt < 3000) return;  // a moment to gather a burst
  if (lastSaid && millis() - lastSaid < 120000) return;     // at most one every 2 minutes
  const String app = toSay.front().app;
  const bool may = !notif::quiet() && !night() && notif::enabled(app.c_str()) && !assistant::busy() &&
                   !alarms::ringing() && !sound::streaming() && jsapp::current() != app;
  String text = toSay.size() == 1 ? toSay.front().text
                                   : i18n::fmt(N_("{0} requests are waiting for you"), String((int)toSay.size()));
  toSay.clear();
  if (!may || voiceOff()) {
    trace::add("popnote: not said (%s)", may ? "no voice: licence or the day's limit" : "quiet, night, busy or the app is open");
    return;
  }
  lastSaid = millis() | 1;
  trace::add("popnote: saying \"%s\"", text.c_str());
  desk::fetch("/v1/device/tts?rate=16000&lang=" + String(i18n::lang()) + "&text=" + urlenc(text), true, onVoice, nullptr);
}

}  // namespace

void sync(JsonArrayConst list) {
  std::vector<Note> next;
  for (JsonObjectConst o : list) {
    const char* app = o["app"] | "";
    if (!*app || !pkg::find(app)) continue;  // not for an installed app: an "EspOS" note (notif.cpp)
    Note n;
    n.app = app, n.arg = o["arg"] | "", n.title = o["title"] | "";
    n.key = String(o["ref"] | "").length() ? String(o["ref"] | "") : (String(o["kind"] | "") + ":" + n.arg);
    n.exp = o["exp_ms"] | (int64_t)0;
    JsonObjectConst pop = o["pop"];
    n.head = pop["head"] | "", n.ptitle = pop["title"] | "", n.mono = pop["mono"] | "", n.text = pop["text"] | "";
    n.say = pop["say"] | "";
    n.sig = n.title + "|" + n.ptitle + "|" + n.mono + "|" + n.text;  // a new round (claude_status) posts again
    for (JsonObjectConst a : o["acts"].as<JsonArrayConst>()) {
      Act x;
      x.id = a["id"] | "", x.post = a["post"] | "", x.risky = a["risky"] | false;
      serializeJson(a["body"], x.body);
      if (x.id.length() && x.post.length()) n.acts.push_back(x);
    }
    const Note* old = find(n.key);
    if (old) n.posted = old->posted && old->sig == n.sig, n.done = old->done && old->sig == n.sig;
    if (!old) queueSay(n);
    next.push_back(n);
  }
  for (const Note& old : notes) {  // gone from the dashboard (answered anywhere, or expired): its notification too
    bool still = false;
    for (const Note& n : next) still = still || n.key == old.key;
    if (!still) notif::cancel(old.app.c_str(), old.key.c_str());
  }
  notes = next;
  for (Note& n : notes) {
    if (n.posted || (n.exp && desk::nowMs() && desk::nowMs() > n.exp)) continue;
    n.posted = true;
    notif::Opts o;
    o.arg = n.arg.c_str(), o.tag = n.key.c_str();
    o.banner = !mayShow(n);  // the card shows instead of a banner
    notif::post(n.app.c_str(), n.title.c_str(), (n.mono.length() ? n.mono : n.text).c_str(), o);
  }
  showNext();
}

void tick() {
  static uint32_t last = 0;
  if (millis() - last < 500) return;
  last = millis();
  sayTick();
  if (!card) {  // none, or the OS rebuilt its layers (a turn of the screen): shown again if it still may
    countdown = bar = nullptr;
    if (!notes.empty()) shownKey = "", showNext();
    return;
  }
  Note* n = find(shownKey);
  if (!n || !mayShow(*n)) return showNext();
  // The saver lives on the same layer and comes to the front when it starts: the card stays over it (the aquarium
  // lets LVGL draw while something of the OS is on that layer, src/tank/screen.cpp overlays()).
  if (os::saverOn() && lv_obj_get_index(card) != (int32_t)lv_obj_get_child_count(lv_layer_top()) - 1)
    lv_obj_move_foreground(card);
  if (countdown && n->exp && desk::nowMs()) {
    const int64_t left = std::max<int64_t>(0, n->exp - desk::nowMs());
    char b[16];
    snprintf(b, sizeof b, "%d:%02d", (int)(left / 60000), (int)(left / 1000 % 60));
    setText(countdown, b);
    const int64_t span = std::max<int64_t>(1, n->exp - shownStart);
    if (bar && lv_obj_get_child_count(bar))
      lv_obj_set_width(lv_obj_get_child(bar, 0), (int)(lv_obj_get_width(bar) * std::min<int64_t>(left, span) / span));
  }
}

}  // namespace popnote
