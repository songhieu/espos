#pragma GCC optimize("Os")  // cold code: built small
// voice.dictate / voice.stop (see jsvoice.h): the OS records, shows its card, sends the WAV and hands back the text.
#include "jsvoice.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <lvgl.h>

#include "desk.h"
#include "i18n.h"
#include "jsapp.h"
#include "lic.h"
#include "lock.h"
#include "mic.h"
#include "assistant.h"
#include "pkg/pkg.h"
#include "trace.h"
#include "uikit.h"
#include "wake/wake.h"

#ifdef SIM
#include <curl/curl.h>

#include <string>
#include <thread>
#else
#include <NetworkClientSecure.h>
#endif

namespace jsvoice {
namespace {

using namespace kit;

JSContext* ctx = nullptr;
bool allowed = false;
enum class St : uint8_t { Idle, Rec, Send };
St st = St::Idle;
JSValue res = JS_UNDEFINED, rej = JS_UNDEFINED;
uint32_t startedAt = 0;
int maxMs = 30000;
String lang, expect, about;
bool untilQuiet = false;  // stop by itself once the person has spoken and gone quiet (a short answer)

// The request, shared with the network worker (or the simulator's thread): freed by the UI once `done`.
struct Job {
  uint8_t* wav = nullptr;
  uint32_t len = 0;
  String path;
  volatile bool done = false;
  int status = 0;
  String body;
};
Job* job = nullptr;

lv_obj_t* card = nullptr;
lv_obj_t *head = nullptr, *sub = nullptr, *ring = nullptr;
lv_timer_t* ticker = nullptr;

// ---------------------------------------------------------------- the card (on the top layer, never takes a touch: the
// finger stays on the app's own hold-to-talk button)
void noTouch(lv_obj_t* o) {
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
  for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) noTouch(lv_obj_get_child(o, i));
}

void showCard() {
  if (card) return;
  const int w = std::min(300, (int)lv_display_get_horizontal_resolution(nullptr) - 24);
  card = box(lv_layer_top(), 0x181d25, 18);
  lv_obj_set_size(card, w, 92);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 34);
  lv_obj_set_style_shadow_width(card, 24, 0);
  lv_obj_set_style_shadow_opa(card, LV_OPA_40, 0);
  ring = box(card, 0xff3b30, 30);
  lv_obj_set_size(ring, 60, 60);
  lv_obj_align(ring, LV_ALIGN_LEFT_MID, 14, 0);
  lv_obj_set_style_bg_opa(ring, LV_OPA_30, 0);
  lv_obj_t* dot = box(ring, 0xff3b30, 22);
  lv_obj_set_size(dot, 44, 44);
  lv_obj_center(dot);
  assistant::micIcon(dot, 0xffffff);  // the OS's glyph (app-slots.md §16: not the dog's)
  head = label(card, "", 17, true, 0xffffff);
  lv_obj_align(head, LV_ALIGN_TOP_LEFT, 88, 20);
  sub = label(card, "", 13, false, 0x9aa6b6);
  lv_obj_set_width(sub, w - 100);
  lv_label_set_long_mode(sub, LV_LABEL_LONG_WRAP);
  lv_obj_align(sub, LV_ALIGN_TOP_LEFT, 88, 46);
  noTouch(card);
}

void hideCard() {
  if (card) lv_obj_delete(card);
  card = head = sub = ring = nullptr;
}

// ---------------------------------------------------------------- the promise
void settle(bool ok, const char* text, const char* code, const char* yesno = nullptr) {
  // Taken out first: the app's handler may start the next dictation (the yes/no after a message), which sets res/rej.
  JSValue res0 = res, rej0 = rej;
  res = rej = JS_UNDEFINED;
  if (ctx && (ok ? JS_IsFunction(ctx, res0) : JS_IsFunction(ctx, rej0))) {
    JSValue v;
    if (ok) {
      v = JS_NewObject(ctx);
      JS_SetPropertyStr(ctx, v, "text", JS_NewString(ctx, text ? text : ""));
      if (yesno && *yesno) JS_SetPropertyStr(ctx, v, "yesno", JS_NewString(ctx, yesno));  // confirm|cancel|unclear|other
    } else {
      v = JS_NewError(ctx);
      JS_SetPropertyStr(ctx, v, "message", JS_NewString(ctx, code));
      JS_SetPropertyStr(ctx, v, "code", JS_NewString(ctx, code));
    }
    JSValue fn = JS_DupValue(ctx, ok ? res0 : rej0);
    jsapp::invoke(fn, 1, &v);
    if (ctx) JS_FreeValue(ctx, fn), JS_FreeValue(ctx, v);
  }
  if (ctx) JS_FreeValue(ctx, res0), JS_FreeValue(ctx, rej0);
}

// ---------------------------------------------------------------- sending (the network worker on the board)
#ifndef SIM
bool writeAll(NetworkClientSecure* c, const uint8_t* p, size_t n) {
  while (n) {
    const size_t k = c->write(p, n > 4096 ? 4096 : n);
    if (!k) return false;
    p += k, n -= k;
  }
  return true;
}

bool readLine(NetworkClientSecure* c, String& out, uint32_t waitMs) {
  out = "";
  const uint32_t t0 = millis();
  while (millis() - t0 < waitMs) {
    if (!c->available()) {
      if (!c->connected()) return false;
      delay(5);
      continue;
    }
    const int ch = c->read();
    if (ch == '\n') return true;
    if (ch != '\r' && out.length() < 1024) out += (char)ch;
  }
  return false;
}

bool exchange(void* p, NetworkClientSecure* c, const char* host, const char* token) {
  Job* j = (Job*)p;
  if (!c) return j->status = -1, false;
  String h = "POST " + j->path + " HTTP/1.1\r\nHost: " + host + "\r\nAuthorization: Device " + token +
             "\r\nUser-Agent: EspOS-S3\r\nAccept-Encoding: identity\r\nContent-Type: audio/wav\r\nContent-Length: " +
             String(j->len) + "\r\n\r\n";
  if (!writeAll(c, (const uint8_t*)h.c_str(), h.length()) || !writeAll(c, j->wav, j->len)) return j->status = -2, false;
  String line;
  if (!readLine(c, line, 30000) || !line.startsWith("HTTP/1.")) return j->status = -3, false;
  const int status = line.substring(9, 12).toInt();
  long clen = -1;
  bool chunked = false, close = false;
  for (;;) {
    if (!readLine(c, line, 10000)) return j->status = -3, false;
    if (!line.length()) break;
    const int colon = line.indexOf(':');
    if (colon < 0) continue;
    String k = line.substring(0, colon), v = line.substring(colon + 1);
    k.toLowerCase();
    v.trim();
    if (k == "content-length") clen = v.toInt();
    else if (k == "transfer-encoding") chunked = v.indexOf("chunked") >= 0;
    else if (k == "connection") close = v.equalsIgnoreCase("close");
  }
  String body;
  bool whole = true;
  auto take = [&](long n) {  // n < 0: to the close
    const uint32_t t0 = millis();
    while ((n < 0 || n > 0) && millis() - t0 < 10000) {
      if (!c->available()) {
        if (!c->connected()) return n < 0;
        delay(5);
        continue;
      }
      const int ch = c->read();
      if (body.length() < 8192) body += (char)ch;
      if (n > 0) n--;
    }
    return n == 0;
  };
  if (chunked) {
    for (;;) {
      if (!readLine(c, line, 10000)) {
        whole = false;
        break;
      }
      const long n = strtol(line.c_str(), nullptr, 16);
      if (n <= 0) {
        readLine(c, line, 2000);
        break;
      }
      if (!take(n) || !readLine(c, line, 2000)) {
        whole = false;
        break;
      }
    }
  } else {
    whole = clen >= 0 ? take(clen) : (take(-1), false);
  }
  j->body = body;
  j->status = status;
  return whole && !close;
}
#else
size_t onBody(char* p, size_t s, size_t n, void* u) {
  String* b = (String*)u;
  if (b->length() < 8192) b->concat(p, s * n);
  return s * n;
}
#endif

bool send(Job* j) {
#ifdef SIM
  // The simulator: straight to SIM_API_BASE (a local mock: apps/sessions/test/mock_server.py), on a thread.
  const char* base = getenv("SIM_API_BASE");
  if (!base || !*base) return false;
  const String url = String(base) + j->path;
  std::thread([j, url] {
    CURL* c = curl_easy_init();
    curl_slist* hl = curl_slist_append(nullptr, "Content-Type: audio/wav");
    const char* tok = getenv("SIM_DEVICE_TOKEN");  // the simulator's device credential, as jsnet's deviceAuth
    hl = curl_slist_append(hl, (std::string("Authorization: Device ") +
                                (tok && *tok ? tok : "5e5e5e5e5e5e5e5e5e5e5e5e5e5e5e5e5e5e5e5e5e5e5e5e5e5e5e5e5e5e5e5e")).c_str());
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, (const char*)j->wav);
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)j->len);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hl);
    curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, 30000L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, onBody);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &j->body);
    long code = 0;
    if (curl_easy_perform(c) == CURLE_OK) curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_slist_free_all(hl);
    curl_easy_cleanup(c);
    j->status = code ? (int)code : -1;
    __sync_synchronize();
    j->done = true;
  }).detach();
  return true;
#else
  return desk::rawSend(exchange, [](void* p) { ((Job*)p)->done = true; }, j);
#endif
}

// ---------------------------------------------------------------- the flow
void finishRec() {
  uint32_t bytes = 0;
  uint8_t* wav = wake::wav(bytes);  // stops the recording
  wake::hold(false);
  if (!wav || bytes < 44 + 16000 / 4 * 2) {  // under a quarter of a second: nothing said
    heap_caps_free(wav);
    st = St::Idle;
    hideCard();
    settle(true, "", nullptr);
    return;
  }
  job = new Job;
  job->wav = wav, job->len = bytes;
  job->path = "/v1/device/dictate?lang=" + (lang.length() ? lang : String(i18n::lang()));
  if (expect == "yesno") {  // a yes/no about what was read back: the server says confirm|cancel|unclear|other
    String a;
    for (unsigned i = 0; i < about.length() && a.length() < 900; i++) {
      const uint8_t ch = about[i];
      if (isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') a += (char)ch;
      else {
        char b[4];
        snprintf(b, sizeof b, "%%%02X", ch);
        a += b;
      }
    }
    job->path += "&expect=yesno&about=" + a;
  }
  if (!send(job)) {
    heap_caps_free(job->wav);
    delete job;
    job = nullptr;
    st = St::Idle;
    hideCard();
    settle(false, nullptr, "offline");
    return;
  }
  st = St::Send;
  setText(head, T_("Writing it down"));  // i18n: dictation: the speech is being turned into text
  setText(sub, T_("The text goes into the field. Nothing is sent."));
  lv_obj_set_style_bg_opa(ring, LV_OPA_10, 0);
}

void onTick(lv_timer_t*) {
  if (st == St::Rec) {
    const uint32_t ms = millis() - startedAt;
    const float db = wake::levelDb();
    const int grow = constrain((int)((db + 60) * 0.5f), 0, 22);  // -60 dB → 0, -16 dB → 22
    if (ring) lv_obj_set_size(ring, 52 + grow, 52 + grow), lv_obj_align(ring, LV_ALIGN_LEFT_MID, 18 - grow / 2, 0);
    char b[48];
    snprintf(b, sizeof b, "%s  %lu:%02lu", T_("Listening"), (unsigned long)(ms / 60000), (unsigned long)(ms / 1000 % 60));
    setText(head, b);
    const bool quiet = untilQuiet && ms > 600 && wake::vadSpeechMs() > 250 && wake::vadQuietMs() > 700;
    if (!wake::capturing() || (int)ms >= maxMs || quiet) finishRec();
    return;
  }
  if (st == St::Send && job && job->done) {
    __sync_synchronize();
    Job* j = job;
    job = nullptr;
    st = St::Idle;
    hideCard();
    JsonDocument d;
    deserializeJson(d, j->body);
    if (j->status == 200) {
      settle(true, d["text"] | "", nullptr, d["yesno"] | "");
    } else if (j->status == 402) {
      lic::locked(d["locked"] | "no_ai", d["buy_url"] | "");
      lock::sheet(T_("Dictation"), lock::Look::Os);  // i18n: what is locked: speech to text in apps
      settle(false, nullptr, "locked");
    } else {
      trace::add("voice: dictate -> %d", j->status);
      settle(false, nullptr, j->status <= 0 ? "offline" : "failed");
    }
    heap_caps_free(j->wav);
    delete j;
  }
  if (st == St::Idle && !job && ticker) {
    lv_timer_delete(ticker);
    ticker = nullptr;
  }
}

// __voice(lang, maxMs, resolve, reject, expect, about, untilQuiet)
JSValue nVoice(JSContext* c, JSValueConst, int argc, JSValueConst* argv) {
  JSValue resolve = argc > 2 ? argv[2] : JS_UNDEFINED, reject = argc > 3 ? argv[3] : JS_UNDEFINED;
  auto fail = [&](const char* code) {
    JSValue e = JS_NewError(c);
    JS_SetPropertyStr(c, e, "message", JS_NewString(c, code));
    JS_SetPropertyStr(c, e, "code", JS_NewString(c, code));
    JSValue r = JS_Call(c, reject, JS_UNDEFINED, 1, &e);
    JS_FreeValue(c, r);
    JS_FreeValue(c, e);
    return JS_UNDEFINED;
  };
  if (!allowed) return fail("denied");
  if (!mic::available()) return fail("no_mic");
  if (st != St::Idle || job || assistant::busy()) return fail("busy");
  const char* l = argc > 0 ? JS_ToCString(c, argv[0]) : nullptr;
  lang = l ? l : "";
  JS_FreeCString(c, l);
  int32_t ms = 30000;
  if (argc > 1) JS_ToInt32(c, &ms, argv[1]);
  auto str = [&](int i) {
    const char* v = argc > i ? JS_ToCString(c, argv[i]) : nullptr;
    String o = v ? v : "";
    JS_FreeCString(c, v);
    return o;
  };
  expect = str(4), about = str(5).substring(0, 300);
  untilQuiet = argc > 6 && JS_ToBool(c, argv[6]) > 0;
  maxMs = constrain(ms > 0 ? ms : 30000, 2000, 30000);
  wake::hold(true);
  if (!wake::captureStart(wake::now(), maxMs)) {
    wake::hold(false);
    return fail("no_room");
  }
  res = JS_DupValue(c, resolve), rej = JS_DupValue(c, reject);
  if (untilQuiet) wake::vadRestart();
  st = St::Rec;
  startedAt = millis();
  showCard();
  setText(sub, T_("Let go to stop."));  // i18n: dictation, hold to talk: lifting the finger ends the recording
  onTick(nullptr);
  if (!ticker) ticker = lv_timer_create(onTick, 100, nullptr);
  return JS_UNDEFINED;
}

// __voiceStop(cancel): ends the recording; cancel drops it (nothing is sent, the promise rejects with "cancelled")
JSValue nStop(JSContext* c, JSValueConst, int argc, JSValueConst* argv) {
  if (st != St::Rec) return JS_UNDEFINED;
  if (argc > 0 && JS_ToBool(c, argv[0]) > 0) {
    wake::captureFree();
    wake::hold(false);
    st = St::Idle;
    hideCard();
    settle(false, nullptr, "cancelled");
  } else {
    finishRec();
  }
  return JS_UNDEFINED;
}

}  // namespace

void install(JSContext* c, const pkg::Package& p) {
  ctx = c;
  allowed = false;
  for (const String& perm : p.perms)
    if (perm == "dictate") allowed = true;
  JSValue g = JS_GetGlobalObject(c);
  JS_SetPropertyStr(c, g, "__voice", JS_NewCFunction(c, nVoice, "__voice", 7));
  JS_SetPropertyStr(c, g, "__voiceStop", JS_NewCFunction(c, nStop, "__voiceStop", 1));
  JS_FreeValue(c, g);
}

void stop() {
  if (st == St::Rec) {
    wake::captureFree();
    wake::hold(false);
    st = St::Idle;
  }
  if (ctx) JS_FreeValue(ctx, res), JS_FreeValue(ctx, rej);  // nobody hears the answer: the context goes
  res = rej = JS_UNDEFINED;
  hideCard();  // a request on its way finishes on its own; onTick drops its answer and frees it
  ctx = nullptr;
  allowed = false;
}

}  // namespace jsvoice
