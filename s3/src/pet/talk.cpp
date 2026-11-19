#pragma GCC optimize("Os")  // the firmware must fit 3.75 MB; nothing here is hot
#include "talk.h"

#include <Arduino.h>
#include <ArduinoJson.h>

#include <cmath>
#ifndef SIM
#include <NetworkClientSecure.h>
#else
#include <unistd.h>
#endif

#include "../desk.h"
#include "../i18n.h"
#include "../led.h"
#include "../net.h"
#include "../sound.h"
#include "../trace.h"
#include "../uikit.h"
#include "../wake/wake.h"
#include "dogs.h"
#include "packs.h"
#include "pet.h"
#include "voice.h"

namespace pet {
// The pop-up's side (summon.cpp).
bool talkOpen();                                                   // open it (wakes the screen); false: no dog
void talkShow(const char* clip, const String& big, const String& small);  // the dog's clip (nullptr: as is), the bubble
void talkLevel(int pct);                                           // the mic button's ring, < 0: none
void talkButton(int state);  // 0 ready, 1 recording (held), 2 busy, 3 recording after "Gâu" (a tap ends it)
void talkTouched();                                                // keeps the pop-up open

namespace talk {
namespace {

constexpr int RATE = 16000;

enum class St : uint8_t { Idle, Listen, Send, Wait, Speak, After };
St st = St::Idle;
uint32_t stAt = 0;
bool byWake = false, heard = false, typed = false;
uint32_t lastLoud = 0, quietUntil = 0;  // quietUntil: our own "I'm here" sound, not the person speaking
uint32_t heardAt = 0;                    // when the question began (0.28 s of speech before it)
uint16_t endWait = 800;                  // the quiet that ended the last question (the turn's log)
int loudMs = 0;                          // how long it has been loud lately (speech, not a knock)
// A wake turn: the 250 ms after the call decide whether the person paused (then the "I'm here" sound) or speaks on;
// `again`: listening once more after the dog's "say it again" line; trimMs: how much of the recording's start to
// leave out (the call itself: the server needs only what follows).
bool decided = false, pausedAfterCall = false, again = false, retryPending = false;
// Follow-up: after the dog has answered, it keeps listening for a while (no call needed): the talk flows.
bool followUp = false;
constexpr uint32_t FOLLOW_MS = 6000;
String lastReply;  // the answer stays in the bubble while it listens for the next question
int trimMs = 0;
lv_timer_t* timer = nullptr;
String cid;
int fallback = 0;  // 1: asked /say?n=last&format=json, 2: then its voice

// ---------------------------------------------------------------- the request (UI <-> network worker)
struct Ex {
  String path;
  uint8_t* body = nullptr;
  uint8_t* base = nullptr;  // the buffer to free (body may point inside it)
  uint32_t len = 0;
  bool json = false, get = false;
  volatile int status = 0;
  volatile bool head = false, done = false, cancel = false, voice = false, cut = false;
  String assist, text;
  int rate = RATE;
  uint32_t tStart = 0, tHead = 0, tFirst = 0, tEnd = 0, pcmBytes = 0;
};
Ex ex;
volatile bool exBusy = false;

// The voice: a ring of 4 s in PSRAM, written by the worker, read by the sound task.
constexpr uint32_t RING = RATE * 4;
int16_t* ring = nullptr;
volatile uint32_t wpos = 0, rpos = 0, lastPull = 0, underruns = 0;
bool playing = false;
uint8_t carry = 0;
bool hasCarry = false;

void set(St s) {
  st = s;
  stAt = millis();
}

// ---- worker side
// The voice's bytes: pieces are cut anywhere (through Cloudflare most have an odd length), so a sample split between
// two pieces is put together from the byte kept back (assistant-ai.md §3).
bool pushPcm(const uint8_t* d, int n) {
#ifdef SIM
  static FILE* dump = nullptr;  // SIM_TALK_DUMP=path: every turn's decoded voice appended (16 kHz s16le), to check it
  if (!ex.tFirst && getenv("SIM_TALK_DUMP")) dump = dump ? dump : fopen(getenv("SIM_TALK_DUMP"), "ab");
#endif
  if (!ex.tFirst) ex.tFirst = millis();
  ex.pcmBytes += n;
  int i = 0;
  auto put = [](int16_t s) {
    while (wpos - rpos >= RING) {  // the ring is full: the speaker is behind, wait for it
      if (ex.cancel) return false;
      delay(10);
    }
    ring[wpos % RING] = s;
    wpos = wpos + 1;
#ifdef SIM
    if (dump) fwrite(&s, 2, 1, dump), fflush(dump);
#endif
    return true;
  };
  if (hasCarry && n > 0) {
    if (!put((int16_t)(carry | d[0] << 8))) return false;
    hasCarry = false;
    i = 1;
  }
  for (; i + 1 < n; i += 2)
    if (!put((int16_t)(d[i] | d[i + 1] << 8))) return false;
  if (i < n) carry = d[i], hasCarry = true;
  return true;
}

bool pushText(const uint8_t* d, int n) {
  if (ex.text.length() + n > 16384) return false;
  char tmp[257];
  for (int o = 0; o < n; o += 256) {  // JSON has no NUL bytes
    const int m = n - o < 256 ? n - o : 256;
    memcpy(tmp, d + o, m);
    tmp[m] = 0;
    ex.text += tmp;
  }
  return true;
}


// Standard base64 (the X-Assist header) -> bytes; returns the length, -1 on a bad character.
int unb64(const String& in, uint8_t* out) {
  int n = 0, bits = 0;
  uint32_t acc = 0;
  for (unsigned i = 0; i < in.length(); i++) {
    const char c = in[i];
    int v = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52
            : c == '+' ? 62 : c == '/' ? 63 : -2;
    if (c == '=' || c == ' ') continue;
    if (v < 0) return -1;
    acc = acc << 6 | v, bits += 6;
    if (bits >= 8) bits -= 8, out[n++] = (uint8_t)(acc >> bits);
  }
  return n;
}

void setAssist(const String& xa) {
  uint8_t* d = (uint8_t*)heap_caps_malloc(xa.length() + 1, MALLOC_CAP_SPIRAM);
  const int n = d ? unb64(xa, d) : -1;
  if (n > 0) {
    d[n] = 0;
    ex.assist = (const char*)d;
  }
  heap_caps_free(d);
}

#ifndef SIM
bool writeAll(NetworkClientSecure* c, const uint8_t* p, uint32_t n) {
  const uint32_t t0 = millis();
  while (n) {
    if (ex.cancel || millis() - t0 > 30000) return false;
    const size_t w = c->write(p, n > 4096 ? 4096 : n);
    if (!w) {
      if (!c->connected()) return false;
      delay(5);
      continue;
    }
    p += w, n -= w;
  }
  return true;
}

// Up to `want` bytes; 0 when the server closed, -1 on a timeout (gapMs without a byte) or a cancel.
int readSome(NetworkClientSecure* c, uint8_t* buf, int want, uint32_t gapMs) {
  const uint32_t t0 = millis();
  for (;;) {
    if (ex.cancel) return -1;
    const int a = c->available();
    if (a > 0) {
      const int n = c->read(buf, a < want ? a : want);
      if (n > 0) return n;
    } else if (!c->connected()) {
      return 0;
    }
    if (millis() - t0 > gapMs) return -1;
    delay(4);
  }
}

bool readLine(NetworkClientSecure* c, String& line, uint32_t gapMs, unsigned max) {
  line = "";
  uint8_t b;
  for (;;) {
    if (readSome(c, &b, 1, gapMs) <= 0) return false;
    if (b == '\n') {
      if (line.length() && line[line.length() - 1] == '\r') line.remove(line.length() - 1);
      return true;
    }
    if (line.length() < max) line += (char)b;
  }
}

// The body: chunked, of a known length, or until the server closes. false: cut short (or cancelled).
bool readBody(NetworkClientSecure* c, bool chunked, long clen, bool voice) {
  uint8_t buf[1024];
  uint32_t gap = voice ? 15000 : 25000;  // the first bytes of the voice may wait for a busy TTS
  auto take = [&](long n) {
    while (n) {
      const int want = n < 0 || n > (long)sizeof buf ? (int)sizeof buf : (int)n;
      const int got = readSome(c, buf, want, gap);
      if (got == 0 && n < 0) return true;  // closed: that was the end
      if (got <= 0) return false;
      if (voice) gap = 8000;
      if (!(voice ? pushPcm(buf, got) : pushText(buf, got))) return false;
      if (n > 0) n -= got;
    }
    return true;
  };
  if (chunked) {
    String line;
    for (;;) {
      if (!readLine(c, line, gap, 64)) return false;
      const long size = strtol(line.c_str(), nullptr, 16);
      if (size <= 0) {
        while (readLine(c, line, 2000, 512) && line.length()) {
        }
        return true;
      }
      if (!take(size) || !readLine(c, line, gap, 8)) return false;
    }
  }
  if (clen >= 0) return take(clen);
  take(-1);
  return false;  // read to the close: the connection is spent
}

bool exchange(void*, NetworkClientSecure* c, const char* host, const char* token) {
  if (!c) {
    ex.status = -1;
    ex.done = true;
    return false;
  }
  String h = String(ex.get ? "GET " : "POST ") + ex.path + " HTTP/1.1\r\nHost: " + host + "\r\nAuthorization: Device " +
             token + "\r\nUser-Agent: EspOS-S3\r\nAccept-Encoding: identity\r\n";
  if (!ex.get) h += String("Content-Type: ") + (ex.json ? "application/json" : "audio/wav") + "\r\nContent-Length: " + ex.len + "\r\n";
  h += "\r\n";
  if (!writeAll(c, (const uint8_t*)h.c_str(), h.length()) || (!ex.get && !writeAll(c, ex.body, ex.len))) {
    ex.status = -2;
    ex.done = true;
    return false;
  }
  String line;
  if (!readLine(c, line, 25000, 200) || !line.startsWith("HTTP/1.")) {
    ex.status = -3;
    ex.done = true;
    return false;
  }
  const int status = line.substring(9, 12).toInt();
  long clen = -1;
  bool chunked = false, close = false;
  String ctype, xa;
  for (;;) {
    if (!readLine(c, line, 10000, 6000)) {
      ex.status = -3;
      ex.done = true;
      return false;
    }
    if (!line.length()) break;
    const int colon = line.indexOf(':');
    if (colon < 0) continue;
    String k = line.substring(0, colon), v = line.substring(colon + 1);
    k.toLowerCase();
    v.trim();
    if (k == "content-type") ctype = v;
    else if (k == "content-length") clen = v.toInt();
    else if (k == "transfer-encoding") chunked = v.indexOf("chunked") >= 0;
    else if (k == "x-assist") xa = v;
    else if (k == "x-sample-rate") ex.rate = v.toInt();
    else if (k == "connection") close = v.equalsIgnoreCase("close");
  }
  if (xa.length()) setAssist(xa);
  ex.voice = status == 200 && ctype.startsWith("audio/pcm");
  ex.status = status;
  ex.tHead = millis();
  __sync_synchronize();
  ex.head = true;  // the words can show now
  const bool whole = readBody(c, chunked, clen, ex.voice);
  ex.cut = !whole;
  ex.tEnd = millis();
  ex.done = true;
  return whole && !close;
}

#else
// The simulator: the same turn through wake/tools/simnet.py (the live API over HTTPS; the device token only in the
// environment, SIM_DEVICE_TOKEN from wake/tools/simpair.py), its bytes read back in odd-sized pieces so the carried
// byte is tested the way Cloudflare cuts the stream.
bool exchange(void*, NetworkClientSecure*, const char*, const char*) {
  if (const char* fake = getenv("SIM_FAKE_ASSIST")) {  // tests: "silent" (nothing made out) or "voice" (1 s of tone)
    delay(800);
    ex.tHead = millis();
    if (!strcmp(fake, "voice")) {
      ex.assist = "{\"heard\":\"mấy giờ rồi\",\"reply\":\"Mười giờ rồi bây.\",\"mood\":\"happy\"}";
      ex.status = 200, ex.voice = true, ex.head = true;
      uint8_t b[640];
      const int secs = getenv("SIM_FAKE_SECS") ? constrain(atoi(getenv("SIM_FAKE_SECS")), 1, 3) : 1;  // the ring holds 4 s
      for (int k = 0; k < 50 * secs; k++) {
        for (int i = 0; i < 320; i++) {
          const int16_t v = (int16_t)(sinf(2 * 3.14159f * 440 * (k * 320 + i) / 16000) * 6000);
          b[2 * i] = v & 0xff, b[2 * i + 1] = (v >> 8) & 0xff;
        }
        pushPcm(b, sizeof b);
      }
    } else {
      ex.text = "{\"silent\":true,\"reply\":\"\"}";
      ex.status = 200, ex.head = true;
    }
    ex.tEnd = millis();
    ex.done = true;
    return true;
  }
  if (!getenv("SIM_DEVICE_TOKEN")) {
    ex.status = -1;
    ex.done = true;
    return false;
  }
  String body = "-";
  if (!ex.get) {
    body = String(getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp") + "/espos-sim-turn-" + String((int)getpid()) + ".bin";
    FILE* f = fopen(body.c_str(), "wb");
    if (f) fwrite(ex.body, 1, ex.len, f), fclose(f);
  }
  const char* helper = getenv("SIM_NET_HELPER") ? getenv("SIM_NET_HELPER") : "wake/tools/simnet.py";
  const String cmd = String("python3 ") + helper + " " + (ex.get ? "GET" : "POST") + " '" + ex.path + "' " + body + " " +
                     (ex.json ? "application/json" : "audio/wav");
  FILE* p = popen(cmd.c_str(), "r");
  char line[8192];
  int status = -3;
  String ctype, xa;
  if (p && fgets(line, sizeof line, p) && !strncmp(line, "STATUS ", 7)) status = atoi(line + 7);
  while (p && fgets(line, sizeof line, p) && line[0] != '\n') {
    String l = line, k;
    l.trim();
    const int colon = l.indexOf(':');
    if (colon < 0) continue;
    k = l.substring(0, colon);
    k.toLowerCase();
    String v = l.substring(colon + 1);
    v.trim();
    if (k == "content-type") ctype = v;
    else if (k == "x-assist") xa = v;
    else if (k == "x-sample-rate") ex.rate = v.toInt();
  }
  if (xa.length()) setAssist(xa);
  ex.voice = status == 200 && ctype.startsWith("audio/pcm");
  ex.status = status;
  ex.tHead = millis();
  ex.head = true;
  static const int CUT[] = {1, 7, 333, 1023, 2, 511, 4095, 3};  // odd pieces, as through Cloudflare
  uint8_t buf[4096];
  bool whole = p != nullptr;
  for (int k = 0; p && !ex.cancel; k++) {
    const size_t got = fread(buf, 1, CUT[k % 8], p);
    if (!got) break;
    if (!(ex.voice ? pushPcm(buf, (int)got) : pushText(buf, (int)got))) {
      whole = false;
      break;
    }
  }
  if (p) pclose(p);
  if (!ex.get) unlink(body.c_str());
  ex.cut = !whole;
  ex.tEnd = millis();
  ex.done = true;
  return whole;
}
#endif

void onDone(void*) {
  exBusy = false;
  heap_caps_free(ex.base ? ex.base : ex.body);
  ex.body = ex.base = nullptr;
}

// ---- the sound task: the voice as it comes; silence while the network is behind (assistant-ai.md §3)
int pull(short* out, int n, void*) {
  lastPull = millis();
  const uint32_t have = wpos - rpos;
  int m = have < (uint32_t)n ? (int)have : n;
  float e = 0;
  for (int i = 0; i < m; i++) out[i] = ring[(rpos + i) % RING], e += (float)out[i] * out[i];
  led::voice(m ? fminf(1.0f, sqrtf(e / m) / 6000.0f) : 0);  // the LED follows the voice's loudness
  rpos = rpos + m;
  if (m < n && !ex.done) {
    memset(out + m, 0, (n - m) * 2);
    underruns = underruns + 1;
    return n;
  }
  return m;
}

// ---------------------------------------------------------------- UI
String dogKey() {
  const dogs::Info* d = dogs::chosen();
  return d ? d->key : "";
}

bool submit(const String& path, uint8_t* body, uint32_t len, bool json, bool get, uint8_t* base = nullptr) {
  if (exBusy) return false;
  ex.path = path, ex.body = body, ex.base = base, ex.len = len, ex.json = json, ex.get = get;
  ex.status = 0, ex.head = ex.done = ex.cancel = ex.voice = ex.cut = false;
  ex.assist = "", ex.text = "", ex.rate = RATE;
  ex.tStart = millis(), ex.tHead = ex.tFirst = ex.tEnd = 0, ex.pcmBytes = 0;
  if (!ring) ring = (int16_t*)heap_caps_malloc(RING * 2, MALLOC_CAP_SPIRAM);
  if (!ring) return false;
  wpos = rpos = 0, underruns = 0, hasCarry = false, playing = false;
  if (!desk::rawSend(exchange, onDone, nullptr)) return false;
  exBusy = true;
  return true;
}

String turnPath() {
  String p = "/v1/device/assist?stream=1&say_rate=16000";
  const String k = dogKey();
  if (k.length()) p += "&dog=" + k;
  if (cid.length()) p += "&cid=" + cid;
  if (byWake) p += "&wake=1";
  return p;
}

void step(lv_timer_t*);
const char* sayAgain();
void listenAgain();
void listenFollowUp();

void startTimer() {
  if (!timer) timer = lv_timer_create(step, 40, nullptr);
}

void finish() {
  set(St::Idle);
  led::mode(led::Mode::Done);
  followUp = again = false;
  wake::hold(false);
  talkLevel(-1);
  talkButton(0);
  if (timer) lv_timer_delete(timer);
  timer = nullptr;
}

void failShow(const char* clip, const char* line, const char* words) {
  led::mode(led::Mode::Error);
  const pba_clip_t* l = line ? voice::lineFor(line) : nullptr;
  if (l) voice::speak(l, true);
  talkShow(clip, T_(words), "");
  set(St::After);
}

void cancelTurn() {
  if (exBusy) ex.cancel = true;
  if (playing) sound::stopStream();
  playing = false;
  if (wake::capturing()) wake::captureStop();
}

bool startListening(uint32_t fromSample, int maxMs, bool wakeTurn) {
  voice::hush();
  wake::hold(true);
  if (!wake::captureStart(fromSample, maxMs)) {
    wake::hold(false);
    return false;
  }
  byWake = wakeTurn, heard = false, typed = false, lastLoud = millis(), fallback = 0;
  set(St::Listen);
  led::mode(led::Mode::Listen);
  startTimer();
  talkButton(wakeTurn ? 3 : 1);
  talkShow("alert", T_(wakeTurn ? N_("I'm listening") : N_("Listening")),
           T_(wakeTurn ? N_("Say what you need") : N_("Let go to send")));
  loudMs = 0, quietUntil = 0;
  return true;
}

uint8_t *pendingWav = nullptr, *pendingBase = nullptr;  // a recording waiting (pendingBase: its buffer, to free)
uint32_t pendingLen = 0;

void trySubmit() {
  if (exBusy) return;
  uint8_t *w = pendingWav, *base = pendingBase;
  pendingWav = pendingBase = nullptr;
  if (!submit(turnPath(), w, pendingLen, false, false, base)) {
    heap_caps_free(base);
    return failShow("look_up", nullptr, net::state() == net::State::Connected ? N_("Can't reach the server") : N_("No internet"));
  }
  set(St::Wait);
}

// Leave out the first `ms` of a WAV made by wake::wav (the call: the server needs what follows): a new header is
// written in place, `base` keeps the buffer to free.
uint8_t* trimWav(uint8_t* w, uint32_t& bytes, int ms) {
  const uint32_t skip = (uint32_t)ms * (RATE / 1000) * 2;
  if (!ms || bytes < 44 + skip + RATE) return w;  // keep at least 0.5 s of sound
  uint8_t hdr[44];
  memcpy(hdr, w, 44);
  uint8_t* n = w + skip;
  bytes -= skip;
  const uint32_t data = bytes - 44;
  memcpy(n, hdr, 44);
  n[4] = (36 + data) & 0xff, n[5] = (36 + data) >> 8 & 0xff, n[6] = (36 + data) >> 16 & 0xff, n[7] = (36 + data) >> 24;
  n[40] = data & 0xff, n[41] = data >> 8 & 0xff, n[42] = data >> 16 & 0xff, n[43] = data >> 24;
  return n;
}

void send() {
  const bool capped = !wake::capturing();  // the capture filled up: the longest turn
  uint32_t bytes = 0;
  uint8_t* base = wake::wav(bytes);
  if (!base) return finish();
  uint8_t* w = byWake ? trimWav(base, bytes, trimMs) : base;
  if (byWake && capped) trace::add("talk: wake word turn, %u ms of sound (the longest turn)", (unsigned)((bytes - 44) / 32));
  else if (byWake) trace::add("talk: wake word turn, %u ms of sound (ended by %u ms of quiet)", (unsigned)((bytes - 44) / 32), (unsigned)endWait);
  else trace::add("talk: push to talk turn, %u ms of sound", (unsigned)((bytes - 44) / 32));
  talkButton(2);
  talkLevel(-1);
  talkShow("look_up", T_("Thinking"), "");
  led::mode(led::Mode::Think);
  heap_caps_free(pendingBase);
  pendingWav = w, pendingBase = base, pendingLen = bytes;
  set(St::Send);
  trySubmit();
}

void applyWords(JsonDocument& d) {
  if (d["cid"].is<const char*>()) cid = d["cid"].as<const char*>();
  if ((d["done"].is<JsonArray>() && d["done"].size()) || d["saved"] == true) desk::syncNow();  // its change comes now
  const String reply = d["reply"] | "", said = d["heard"] | "";
  const char* mood = d["mood"] | "stand";
  lastReply = reply;
  trace::add("talk: heard \"%.80s\" -> \"%.120s\" (%s)", said.c_str(), reply.c_str(), mood);
  talkShow(mood, reply, said.length() ? "“" + said + "”" : String());
}

void handleJson() {
  JsonDocument d;
  deserializeJson(d, ex.text);
  const int s = ex.status;
  trace::add("talk: answer %d (%s) in %lu ms", s, d["silent"] == true ? "nothing made out" : d["error"] | "", (unsigned long)(ex.tEnd - ex.tStart));
  if (s == 200 && d["silent"] == true) {  // nothing the server could make out after "Gâu gâu": ask again, listen again
    const pba_clip_t* l = voice::lineFor("alert");
    if (l) voice::speak(l, true);
    talkShow("alert", sayAgain(), "");
    led::mode(led::Mode::Error);
    retryPending = !again;  // once: a second empty answer ends it
    return set(St::After);
  }
  if (fallback == 1 && s == 200) {  // the words of the turn whose voice failed: show them, then try the voice once
    applyWords(d);
    fallback = 2;
    String p = "/v1/device/assist/say?n=last&rate=16000";
    if (cid.length()) p = "/v1/device/assist/say?cid=" + cid + "&n=last&rate=16000";
    if (submit(p, nullptr, 0, false, true)) return set(St::Wait);
    return set(St::After);
  }
  if (s == 200) {
    applyWords(d);
    return set(St::After);
  }
  if (s == 401) return failShow("look_up", nullptr, N_("Pair this device again in Settings"));
  if (s == 413 || s == 422) return failShow("alert", "alert", N_("Say it again, please"));
  if (s == 429) return failShow("no_tired", "no_tired", N_("Wait a moment, then ask again"));
  if (s == 502 && d["reply"].is<const char*>()) {  // the model failed: the dog's own recorded clip says the reply
    const pba_clip_t* l = pk::line(d["clip"] | "no_tired");
    if (!l) l = voice::lineFor("no_tired");
    if (l) voice::speak(l, true);
    talkShow("no_tired", d["reply"].as<const char*>(), "");
    return set(St::After);
  }
  if (s == 502 && fallback == 0 && !ex.assist.length()) {  // the voice failed after the turn was saved: its words
    fallback = 1;
    if (submit("/v1/device/assist/say?n=last&format=json", nullptr, 0, false, true)) return set(St::Wait);
  }
  if (s < 0) return failShow("look_up", nullptr, N_("Can't reach the server"));
  failShow("no_tired", "no_tired", N_("Something went wrong, try again"));
}

void step(lv_timer_t*) {
  talkTouched();
  const uint32_t t = millis() - stAt;
  switch (st) {
    case St::Idle:
      return finish();
    case St::Listen: {
      const float lv = wake::levelDb(), fl = wake::floorDb();
      talkLevel((int)constrain((lv - fl) * 4, 0, 100));
      if (byWake) {
        // After "Gâu gâu": the call's own tail (250 ms) is not the question. Then: quiet = the person waits for the
        // dog, who answers at once (a short soft sound); loud = the question follows in the same breath. The question
        // is speech (loud 0.28 s; our sound not counted). It ends after 0.8 s of quiet while it is short, 1.2 s once
        // the person has been speaking ~2 s (a long sentence has thinking pauses); the capture's length caps the turn.
        // "Gâu gâu" and silence is never sent: after 5 s the dog says it is here and the person can call again.
        const bool loud = millis() >= quietUntil && lv > fl + 9 && lv > -60;
        // While waiting for the question after a call, the wake word stays on: a new "Gâu gâu" starts afresh.
        wake::hold(heard || again || followUp);
        if (!decided && t >= 250) {
          decided = true;
          pausedAfterCall = !loud;
          trimMs = again || followUp ? 0 : pausedAfterCall ? 1100 : 950;  // keep 0.1 s (0.25 s speaking on) before the call ended
          if (pausedAfterCall && !again && !followUp) {
            sound::blip();
            quietUntil = millis() + 400;
          }
          trace::add("talk: %s (level %.0f, floor %.0f dBFS)", followUp ? "follow-up: listening" : again ? "listening again"
                     : pausedAfterCall ? "pause after the call: ack sound" : "speaking on", lv, fl);
        }
        loudMs = decided && loud ? loudMs + 40 : max(0, loudMs - 20);
        if (loud && heard) lastLoud = millis();
        if (!heard && loudMs >= 280) {
          heard = true, lastLoud = heardAt = millis();
          talkShow(nullptr, T_("Listening"), T_("Say what you need"));
        }
        endWait = heard && millis() - heardAt > 1700 ? 1200 : 800;
        if (heard && millis() - lastLoud > endWait) return send();
        if (!heard && t > (followUp ? FOLLOW_MS : 5250)) {
          trace::add("talk: no question after the %s", followUp ? "answer" : again ? "retry line" : "wake word");
          wake::captureFree();
          if (followUp) {  // the answer stays in the bubble; back to waiting for "Gâu gâu" (the pop-up stays a while)
            talkShow("happy", String(), String());
            return finish();
          }
          talkShow("plead", T_("I'm here. Call me again, or hold the button and speak"), "");
          return set(St::After);
        }
      }
      if (!wake::capturing()) return send();  // the longest turn
      return;
    }
    case St::Send:
      return trySubmit();
    case St::Wait:
      if (ex.head && ex.voice) {
        JsonDocument d;
        deserializeJson(d, ex.assist);
        trace::add("talk: words after %lu ms", (unsigned long)(ex.tHead - ex.tStart));
        applyWords(d);
        talkButton(0);
        return set(St::Speak);
      }
      if (ex.done && !exBusy) {
        talkButton(0);
        return handleJson();
      }
      if (t > 45000) {
        ex.cancel = true;
        failShow("look_up", nullptr, N_("Can't reach the server"));
      }
      return;
    case St::Speak: {
      const uint32_t have = wpos - rpos;
      if (!playing && (have >= RATE || (ex.done && have))) {
        voice::hush();
        sound::stream(pull, nullptr);
        playing = true;
        led::mode(led::Mode::Speak);
      }
      const bool quiet = !playing || millis() - lastPull > 300;
      if (ex.done && !exBusy && wpos == rpos && quiet) {
        trace::add("talk: voice %u B, first sound %lu ms after sending, %u underruns%s", (unsigned)ex.pcmBytes,
                   (unsigned long)(ex.tFirst ? ex.tFirst - ex.tStart : 0), (unsigned)underruns, ex.cut ? ", cut" : "");
        playing = false;
        if (ex.voice && ex.pcmBytes) return listenFollowUp();  // the talk goes on without "Gâu gâu"
        set(St::After);
      }
      return;
    }
    case St::After:
      if (retryPending && !voice::busy() && voice::quietMs() > 300) {  // its line is over (the mic would hear it)
        retryPending = false;
        return listenAgain();
      }
      if (t > 2000 && !voice::busy() && !retryPending) {
        talkShow("happy", String(), String());  // keep the words, back to wagging
        finish();
      }
      return;
  }
}

void onWake(uint32_t at) {
  const bool waiting = st == St::Listen && byWake && !heard && !again && !followUp;  // called again before asking
  if ((st != St::Idle && st != St::After && !waiting) || !canTalk()) return;
  if (!talkOpen()) return;
  if (waiting) wake::captureFree();
  if (!startListening(at - (uint32_t)(RATE * 6 / 5), 15200, true)) return;  // from 1.2 s before: the call itself too
  led::mode(led::Mode::Wake);
  decided = false, again = false, followUp = false, trimMs = 0, quietUntil = 0;
  trace::add("talk: called by the wake word%s", waiting ? " (again)" : "");
}

// After the dog's answer: FOLLOW_MS of listening without the call, the ears up (its own voice not counted).
void listenFollowUp() {
  if (!startListening(wake::now(), 12000, true)) return set(St::After);
  talkShow(nullptr, lastReply.length() ? lastReply : String(T_("I'm listening")), T_("Listening"));  // its answer stays
  decided = false, again = false, followUp = true, trimMs = 0, quietUntil = millis() + 400;
  trace::add("talk: follow-up: listening %lu s, no call needed", (unsigned long)(FOLLOW_MS / 1000));
}

// After the dog's "say it again" line: listen once more for 5 s (no call needed).
void listenAgain() {
  if (!startListening(wake::now(), 10000, true)) return;
  talkShow(nullptr, T_("Listening"), T_("Say what you need"));
  decided = false, again = true, followUp = false, trimMs = 0, quietUntil = 0;
}

// "I didn't catch that", each dog its own way (the server heard nothing after "Gâu gâu", or could not make it out).
const char* sayAgainOf(const String& k);
const char* sayAgain() {
  const dogs::Info* d = dogs::chosen();
  return sayAgainOf(d ? d->key : "");
}

const char* sayAgainOf(const String& k) {
  // i18n: the dog's own words when it could not make out what was said; each dog has its manner (docs: assistant-ai.md §8)
  if (k == "bao") return T_("Didn't catch that, kid. Say it again.");
  if (k == "momo") return T_("Missed that, man. Say it again, slowly.");
  if (k == "boop") return T_("Huh? Say that again, boss!");
  if (k == "leaf") return T_("Leaf didn't hear that. Say it again, please.");
  if (k == "volt") return T_("The star didn't catch that. Say it again, clearly.");
  if (k == "byte") return T_("Command not received. Please repeat.");
  if (k == "shiba") return T_("Didn't hear. Again.");
  if (k == "corgi") return T_("Biscuit didn't catch that. Say it again for me.");
  if (k == "husky") return T_("Oh dear, Frost heard nothing at all! Say it again!");
  if (k == "golden") return T_("Didn't catch it! Say it again, loud and clear!");
  return T_("I didn't catch that. Say it again?");
}

}  // namespace

void begin() { wake::onWake(onWake); }

void press() {
  if (st == St::Listen) {
    if (byWake && millis() - stAt > 500) send();  // called by "Gâu": a tap says "done"
    return;
  }
  if (!canTalk()) {
    talkShow("look_up", T_(net::state() == net::State::Connected ? N_("Link this device to your account first")
                                                                 : N_("No internet")), "");
    return;
  }
  if (st != St::Idle) cancelTurn();  // talking over the dog: it stops, you speak
  if (!startListening(wake::now() - RATE / 10, 12100, false)) talkShow("look_up", T_("The microphone isn't working"), "");
}

void release() {
  if (st != St::Listen || byWake) return;
  if (wake::captureMs() < 400) {  // 0.1 s before the press + under 0.3 s held
    wake::captureFree();
    talkShow("look_up", T_("Hold the button while you speak"), "");
    return set(St::After);
  }
  send();
}

bool busy() { return st != St::Idle; }
bool listening() { return st == St::Listen; }
bool canTalk() { return sound::micChannel() && desk::link() == desk::Link::Linked; }

void closed() {  // the pop-up went away: the turn stops
  if (st == St::Idle) return;
  trace::add("talk: pop-up closed: the turn stops (state %d)", (int)st);
  cancelTurn();
  if (wake::capturing()) wake::captureFree();
  finish();
}

lv_obj_t* micIcon(lv_obj_t* parent, uint32_t color) {
  lv_obj_t* m = kit::clear(parent);
  lv_obj_set_size(m, 18, 26);
  lv_obj_t* cap = kit::box(m, color, 5);  // the capsule
  lv_obj_set_size(cap, 10, 15);
  lv_obj_align(cap, LV_ALIGN_TOP_MID, 0, 0);
  // the U around it: three bars (an arc widget would cost 3 KB of firmware)
  for (int k = 0; k < 3; k++) {
    lv_obj_t* b = kit::box(m, color, 1);
    lv_obj_set_size(b, k == 2 ? 16 : 2, k == 2 ? 2 : 8);
    lv_obj_set_pos(b, k == 1 ? 16 : 1, k == 2 ? 17 : 9);
  }
  lv_obj_t* stem = kit::box(m, color, 1);
  lv_obj_set_size(stem, 2, 5);
  lv_obj_align(stem, LV_ALIGN_BOTTOM_MID, 0, -2);
  lv_obj_t* base = kit::box(m, color, 1);
  lv_obj_set_size(base, 10, 2);
  lv_obj_align(base, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_flag(m, LV_OBJ_FLAG_EVENT_BUBBLE);
  return m;
}

bool console(const char* cmd) {
  if (!strncmp(cmd, "ASK ", 4)) {
    if (st != St::Idle || !talkOpen()) return Serial.println("ASK busy"), true;
    JsonDocument d;
    d["text"] = cmd + 4;
    const String k = dogKey();
    if (k.length()) d["dog"] = k;
    String body;
    serializeJson(d, body);
    uint8_t* b = (uint8_t*)heap_caps_malloc(body.length() + 1, MALLOC_CAP_SPIRAM);
    if (!b) return true;
    memcpy(b, body.c_str(), body.length());
    byWake = false, typed = true, fallback = 0;
    wake::hold(true);
    startTimer();
    talkShow("look_up", T_("Thinking"), "");
    led::mode(led::Mode::Think);
    if (!submit(turnPath(), b, body.length(), true, false)) {
      heap_caps_free(b);
      Serial.println("ASK offline");
      finish();
      return true;
    }
    set(St::Wait);
    Serial.println("ASK sent");
    return true;
  }
  if (!strncmp(cmd, "SAYAGAIN ", 9)) {  // SAYAGAIN <dog key>: that dog's "say it again" line in the pop-up (text checks)
    if (talkOpen()) talkShow("alert", sayAgainOf(cmd + 9), "");
    return true;
  }
  if (!strcmp(cmd, "WAKEFIRE")) {  // as if "Gâu gâu" was heard now (tests, demos)
    onWake(wake::now());
    return true;
  }
  if (!strncmp(cmd, "LED ", 4)) {  // LED wake|listen|think|speak|error|done|receive: show a state (tests, demos)
    const char* m = cmd + 4;
    if (!strcmp(m, "receive")) led::receive();
    else if (!strcmp(m, "speak")) led::voice(0.6f), led::mode(led::Mode::Speak);
    else
      led::mode(!strcmp(m, "wake") ? led::Mode::Wake : !strcmp(m, "listen") ? led::Mode::Listen : !strcmp(m, "think")
                ? led::Mode::Think : !strcmp(m, "error") ? led::Mode::Error : led::Mode::Done);
    return true;
  }
  if (!strcmp(cmd, "TALK")) {
    Serial.printf("TALK state %d status %d words %lu ms first sound %lu ms end %lu ms voice %u B underruns %u cut %d cid %s\n",
                  (int)st, ex.status, (unsigned long)(ex.tHead ? ex.tHead - ex.tStart : 0),
                  (unsigned long)(ex.tFirst ? ex.tFirst - ex.tStart : 0), (unsigned long)(ex.tEnd ? ex.tEnd - ex.tStart : 0),
                  (unsigned)ex.pcmBytes, (unsigned)underruns, ex.cut, cid.c_str());
    return true;
  }
  return false;
}

}  // namespace talk
}  // namespace pet
