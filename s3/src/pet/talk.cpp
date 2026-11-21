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
#include "../lic.h"
#include "../lock.h"
#include "../mic.h"
#include "../net.h"
#include "../sound.h"
#include "../trace.h"
#include "../uikit.h"
#include "../wake/wake.h"
#include "assist.h"
#include "dogs.h"
#include "../cues.h"
#include "dog.h"
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
void talkLock(const char* url);  // the way to unlock talking: the QR in place of the button and the chips
void talkTrial(int left);        // the "{n} left" chip of a trial (< 0: none)

namespace talk {
namespace {

constexpr int RATE = 16000;

enum class St : uint8_t { Idle, Listen, Send, Wait, Speak, After };
St st = St::Idle;
uint32_t stAt = 0;
bool byWake = false, heard = false, typed = false;
uint32_t lastLoud = 0, quietUntil = 0;  // quietUntil: our own "I'm here" sound, not the person speaking
uint32_t ackFrom = 0;                    // when the dog began answering its name (0: no ack in this turn)
uint32_t warmAskedAt = 0;  // when the warm-up was asked for (warmFn)
uint32_t heardAt = 0;                    // when the question began (0.24 s of speech before it)
uint16_t endWait = 2000;                 // the pause that ended the last question (the turn's log; 8000 = the cap)
float spPeak = -90, turnFloor = -60;     // this question's loudest moment (decaying), the room's floor when it began
int silentMs = 0;                        // how long the level has sat right at the floor
int loudMs = 0;                          // how long it has been loud lately (speech, not a knock)
// The question window (owner, 2026-11-20): the question must begin within QUESTION_MS, counted from the end of the
// dog's "I'm here" (the ack: ears up only once it has spoken), or from the decision when there is no ack. Speech
// begins at the floor + SPEAK_DB for 0.24 s (soft speech at the owner's distance peaks 12-14 dB over a -60 floor).
constexpr uint32_t QUESTION_MS = 6000;
constexpr float SPEAK_DB = 7;
uint32_t winFrom = 0, stepAt = 0;        // the window's start; the last listening step (loudMs counts real time)
float winPeak = -90;                     // the window's loudest moment (the "no question" log)
int winLoudMs = 0, winOursMs = 0;        // how long it was loud in the window, all told; our own sound in it
// A wake turn: the 250 ms after the call decide whether the person paused (then the "I'm here" sound) or speaks on;
// `again`: listening once more after the dog's "say it again" line; trimMs: how much of the recording's start to
// leave out (the call itself: the server needs only what follows).
bool decided = false, pausedAfterCall = false, again = false, retryPending = false;
// Follow-up: after the dog has answered, it keeps listening for a while (no call needed): the talk flows.
bool followUp = false;
// After a spoken answer: the next question must START within FOLLOW_MS (the ears up, no call needed) and be close to
// the device: FOLLOW_DB above the room's floor and louder than FOLLOW_MIN (owner, 2026-11-19: talk across the room was
// taken as questions with 6 s and floor + 9 dB). Setting s3.assist "follow" (default on).
// Owner, 2026-11-20 (the color S3 against the OLED bot: "nó trả lời xong nói được tiếp"): 2.75 s was too short to
// begin the next sentence (a follow-up begun at 2.6 s was cut), so 5 s, and close speech a little softer (12 dB over
// the floor, over -45 dBFS) than the 15 / -42 of 11-19, still well above talk across the room.
constexpr uint32_t FOLLOW_MS = 5000;  // 250 ms deciding + 4.75 s
// 2026-11-21: still too strict on the board: the owner's follow-up peaked at -46 dBFS (22 dB over the floor) and was
// dropped by the -45 floor, while the OLED bot took it. Normal speech now: 9 dB over the floor, over -56 dBFS.
constexpr float FOLLOW_DB = 9, FOLLOW_MIN = -56;
// The dog's answer ended with a question to the person (the server's "asks", 2026-11-20): the answer is expected, so
// the follow-up listens as after a call (QUESTION_MS, any speech at SPEAK_DB), not only for close speech.
bool lastAsks = false, asksFollow = false;
bool dryRun = false;  // WAKEFIRE DRY (board checks): the whole listening, but nothing is ever sent
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
  uint32_t tStart = 0, tSent = 0, tHead = 0, tFirst = 0, tEnd = 0, pcmBytes = 0;  // tSent: the request written
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
    ex.tEnd = millis();
    ex.done = true;
    return false;
  }
  String h = String(ex.get ? "GET " : "POST ") + ex.path + " HTTP/1.1\r\nHost: " + host + "\r\nAuthorization: Device " +
             token + "\r\nUser-Agent: EspOS-S3\r\nAccept-Encoding: identity\r\n";
  if (!ex.get) h += String("Content-Type: ") + (ex.json ? "application/json" : "audio/wav") + "\r\nContent-Length: " + ex.len + "\r\n";
  h += "\r\n";
  if (!writeAll(c, (const uint8_t*)h.c_str(), h.length()) || (!ex.get && !writeAll(c, ex.body, ex.len))) {
    ex.status = -2;
    ex.tEnd = millis();
    ex.done = true;
    return false;
  }
  ex.tSent = millis();
  String line;
  if (!readLine(c, line, 25000, 200) || !line.startsWith("HTTP/1.")) {
    ex.status = -3;
    ex.tEnd = millis();
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
      ex.tEnd = millis();
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
  // Not kept after a streamed voice answer: on a test board the next request on that connection always died
  // (no status line, 12 s and 43 s later; the server closes it after streaming), and its resend cost the upload twice.
  return whole && !close && !ex.voice;
}

// While the person speaks (1-8 s), a fresh connection is made for the question: the kept one may be one the server
// closed while idle (seen on a test board: the question then failed and went again, the upload twice), and a
// handshake done now costs nothing later.
bool warmFn(void*, NetworkClientSecure* c, const char* host, const char*) {
  if (!c) return false;
  // Started late (desk had just made a fresh connection, ~2 s, or another request had just used it): keep it.
  // Started at once: desk handed over the connection its ping keeps warm, and the server drops idle ones well before
  // that ping comes again (9q on the board, 2026-11-20: the question failed on it 3 times out of 4, each resend
  // ~1.5 s + a handshake later): a fresh one, made while the person speaks.
  if (millis() - warmAskedAt > 600) return c->connected();
  c->stop();
  return c->connect(host, 443) == 1;  // kept when it is up
}
#else
bool warmFn(void*, NetworkClientSecure*, const char*, const char*) { return false; }
// The simulator: the same turn through wake/tools/simnet.py (the live API over HTTPS; the device token only in the
// environment, SIM_DEVICE_TOKEN from wake/tools/simpair.py), its bytes read back in odd-sized pieces so the carried
// byte is tested the way Cloudflare cuts the stream.
bool exchange(void*, NetworkClientSecure*, const char*, const char*) {
  if (const char* fake = getenv("SIM_FAKE_ASSIST")) {  // tests: "silent" (nothing made out) or "voice" (1 s of tone)
    delay(800);
    ex.tHead = millis();
    if (!strcmp(fake, "trial_ended") || !strcmp(fake, "no_ai")) {  // tests: the AI is locked (402)
      ex.text = String("{\"locked\":\"") + fake + "\",\"buy_url\":\"https://espos.dev/app#/buy\"}";
      ex.status = 402, ex.head = true;
      ex.tEnd = millis();
      ex.done = true;
      return true;
    }
    if (!strcmp(fake, "voice")) {
      // SIM_FAKE_REPLY: the dog's words (a long one: the bubble's scroll); a typed turn hears what it sent.
      JsonDocument q, r;
      if (ex.json) deserializeJson(q, (const char*)ex.body, ex.len);
      r["heard"] = ex.json ? (const char*)(q["text"] | "") : "mấy giờ rồi";
      r["reply"] = getenv("SIM_FAKE_REPLY") ? getenv("SIM_FAKE_REPLY") : "Mười giờ rồi bây.";
      r["mood"] = "happy";
      if (getenv("SIM_FAKE_ASKS")) r["asks"] = true;  // tests: the dog's answer ends with a question
      ex.assist = "";
      serializeJson(r, ex.assist);
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
    ex.tEnd = millis();
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

bool netRetried = false;  // this turn was sent again once already
// A wake call that found no room to record (onWake): tried again for a few seconds; noCall: the recording began
// after the call, so there is no call to cut from its start.
uint32_t retryAt = 0, retryUntil = 0;
lv_timer_t* retryTimer = nullptr;
bool noCall = false;
void retryWake(lv_timer_t*);
void wakeBegan(bool waiting, bool callInside);

// A kept connection the server closed while it was idle fails at once, before any answer (-2: the request could not
// be written, -3: no status line): seen on a test board, the first question after a pause. The turn is sent
// again once on a new connection (the failed one is closed), so its sound is kept for that; a slow failure is not
// retried (the server may have the question already).
bool staleFail() {
  if (netRetried || !ex.tEnd) return false;
  if (ex.status == -2) return true;  // could not even write the request
  // No status line: a dead connection says so at once after the request is written (a long recording takes a while
  // to upload); a server still thinking keeps it open far longer.
  return ex.status == -3 && ex.tSent && ex.tEnd - ex.tSent < 3000;
}

void onDone(void*) {
  exBusy = false;
  if (staleFail()) return;  // handleJson sends it again
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
  ex.tStart = millis(), ex.tSent = ex.tHead = ex.tFirst = ex.tEnd = 0, ex.pcmBytes = 0;
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
void showLock();
void listenAgain();
void listenFollowUp();

void startTimer() {
  if (!timer) timer = lv_timer_create(step, 40, nullptr);
}

// A question waiting to go (desk's one raw slot busy): a recording (pendingBase: its buffer, to free) or a chip's JSON.
uint8_t *pendingWav = nullptr, *pendingBase = nullptr;
uint32_t pendingLen = 0;
bool pendingJson = false;

void finish() {
  if (wake::capturing() || st == St::Listen) wake::captureFree();  // a turn never ends with a recording held
  if (pendingBase) heap_caps_free(pendingBase);  // a question that never went (the pop-up closed while it waited)
  pendingWav = pendingBase = nullptr;
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

volatile bool warmBusy = false;  // the warm-up job is running (the question waits for it)

void warmUp() {
  if (!warmBusy && desk::rawSend(warmFn, [](void*) { warmBusy = false; }, nullptr)) warmBusy = true, warmAskedAt = millis();
}

bool startListening(uint32_t fromSample, int maxMs, bool wakeTurn) {
  warmUp();
  voice::hush();
  // the recording first: a try that finds no room leaves the listener as it was (held and let go every 100 ms
  // during retryWake's 4 s, it traced "listening (resumed)" each time; the detector does not fire while recording)
  if (!wake::captureStart(fromSample, maxMs)) return false;
  wake::hold(true);
  byWake = wakeTurn, heard = false, typed = false, lastLoud = millis(), fallback = 0, dryRun = false, ackFrom = 0;
  winLoudMs = winOursMs = 0;  // never the last turn's count (it sent a call and the dog's "Dạ?" as a question, 2026-11-21)
  wake::vadQuestion(false);
  noCall = false;  // (wakeBegan says otherwise for a call that waited for room)
  set(St::Listen);
  led::mode(led::Mode::Listen);
  startTimer();
  talkButton(wakeTurn ? 3 : 1);
  talkShow("alert", T_(wakeTurn ? N_("I'm listening") : N_("Listening")),
           T_(wakeTurn ? N_("Say what you need") : N_("Let go to send")));
  loudMs = 0, quietUntil = 0, stepAt = millis();
  return true;
}


void trySubmit() {
  if (exBusy || warmBusy) return;  // (the warm-up holds desk's one raw slot for a moment)
  netRetried = false;
  uint8_t *w = pendingWav, *base = pendingBase;
  pendingWav = pendingBase = nullptr;
  const bool json = pendingJson;
  pendingJson = false;
  if (!submit(turnPath(), w, pendingLen, json, false, base)) {
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
  wake::vadQuestion(false);
  const bool capped = !wake::capturing();  // the capture filled up: the longest turn
  if (dryRun) {  // the turn is measured and logged, nothing goes to the server (no AI call)
    trace::add("talk: dry run: the question would go now (%d ms recorded, speech began %ld ms into the window, ended %u ms "
               "after the last speech; peak %.0f, floor %.0f dBFS)", wake::captureMs(), (long)(int32_t)(heardAt - winFrom),
               (unsigned)endWait, spPeak, turnFloor);
    dryRun = false;
    wake::captureFree();
    talkShow("happy", String(), String());
    return finish();
  }
  uint32_t bytes = 0;
  uint8_t* base = wake::wav(bytes);
  if (!base) return finish();
  uint8_t* w = byWake ? trimWav(base, bytes, trimMs) : base;
  if (byWake && capped) trace::add("talk: wake word turn, %u ms of sound (the longest turn)", (unsigned)((bytes - 44) / 32));
  else if (byWake && endWait == 8000) trace::add("talk: wake word turn, %u ms of sound (8 s of question: the cap)", (unsigned)((bytes - 44) / 32));
  else if (byWake) trace::add("talk: wake word turn, %u ms of sound (began %ld ms into the window, ended %u ms after the last "
                              "speech; peak %.0f, floor %.0f dBFS)", (unsigned)((bytes - 44) / 32),
                              (long)(int32_t)(heardAt - winFrom), (unsigned)endWait, spPeak, turnFloor);
  else trace::add("talk: push to talk turn, %u ms of sound", (unsigned)((bytes - 44) / 32));
  talkButton(2);
  talkLevel(-1);
  talkShow("look_up", T_("Thinking"), "");
  led::mode(led::Mode::Think);
  if (byWake && assist::prefs().ack) sound::blipEnd();  // heard, sent: the dog is thinking (the recording is done)
  heap_caps_free(pendingBase);
  pendingWav = w, pendingBase = base, pendingLen = bytes, pendingJson = false;
  set(St::Send);
  trySubmit();
}

void applyWords(JsonDocument& d) {
  if (fallback != 1) {  // (fallback 1: the words of a turn already counted, fetched again)
    lic::usedTurn();
    talkTrial(lic::ai() == lic::Ai::Trial ? lic::aiLeft() : -1);
  }
  if (d["cid"].is<const char*>()) cid = d["cid"].as<const char*>();
  if ((d["done"].is<JsonArray>() && d["done"].size()) || d["saved"] == true) desk::syncNow();  // its change comes now
  const String reply = d["reply"] | "", said = d["heard"] | "";
  const char* mood = d["mood"] | "stand";
  lastReply = reply;
  lastAsks = d["asks"] == true;
  trace::add("talk: heard \"%.80s\" -> \"%.120s\" (%s)", said.c_str(), reply.c_str(), mood);
  talkShow(mood, reply, said.length() ? "“" + said + "”" : String());
}

void handleJson() {
  JsonDocument d;
  deserializeJson(d, ex.text);
  const int s = ex.status;
  trace::add("talk: answer %d (%s) in %lu ms", s, d["silent"] == true ? "nothing made out" : d["error"] | "",
             (unsigned long)((ex.tEnd ? ex.tEnd : millis()) - ex.tStart));
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
  if (s == 402) {  // the AI is not allowed (the trial is over, or none on this account): the dog says so, the QR
    lic::locked(d["locked"] | "no_ai", d["buy_url"] | "");
    return showLock();
  }
  if (s == 403 && !strcmp(d["error"] | "", "account_disabled")) {  // the account is blocked (or being deleted): not
    // an AI error, nothing to retry; the device keeps its token and says it calmly (the OS's own screen says more)
    trace::add("talk: the account is %s", d["state"] | "blocked");
    lock::account(d["state"] | "blocked");  // the OS's calm screen for it (tank-game's lock.h); the same state again: nothing
    // i18n: the dog's pop-up when the person's EspOS account is blocked or being deleted (calm, not an error)
    talkShow("stand", T_("Your account is locked"), T_("See espos.dev for details"));
    led::mode(led::Mode::Done);
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
  if (staleFail() && ex.body) {
    netRetried = true;
    trace::add("talk: no answer on the kept connection (%d after %lu ms): sending again on a new one", s,
               (unsigned long)(ex.tEnd - ex.tStart));
    uint8_t *b = ex.body, *base = ex.base;
    const uint32_t n = ex.len;
    const String path = ex.path;
    const bool json = ex.json, get = ex.get;
    ex.body = ex.base = nullptr;
    if (submit(path, b, n, json, get, base)) return set(St::Wait);
    heap_caps_free(base ? base : b);
  }
  // No status line after a long wait: the server took the question but its AI stalled (2026-11-20 on the board: 28 s,
  // then the server cancelled it). The network is fine, so say so as "wait, then ask again", not "can't reach".
  if (s == -3 && ex.tSent && ex.tEnd - ex.tSent >= 20000) return failShow("no_tired", "no_tired", N_("Wait a moment, then ask again"));
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
        // is speech (loud 0.24 s; our sound not counted). Its end (owner, 2026-11-19, on the board's mic in a room
        // with a fan and a TV: "ngừng đọc cỡ 2s là gửi"): speech = within 10 dB of this question's own peak (the
        // room's noise never is); 2 s after the last speech it goes, 1.2 s when the room is plainly quiet (under the
        // floor + 3 dB, the floor taken when the question began); 8 s after it began at most.
        // "Gâu gâu" and silence is never sent: 6 s after its ack the dog says it is here and the person can call again.
        const uint32_t now = millis(), dt = now - stepAt;
        stepAt = now;
        // The dog's answer to its name may run past its measured length (a line of several clips, a slow start of
        // the speaker): while its voice plays, nothing is the question, the window opens 250 ms after it, and the
        // recording is cut to there (2026-11-21, the 2.8": "vừa gọi chưa kịp nói gì nó ... nghĩ luôn": the end of
        // "Gì đó bây?" was taken for the question and sent with the call).
        if (ackFrom && voice::busy()) {
          quietUntil = max(quietUntil, now + 250);
          wake::vadMute(300);
          winFrom = max(winFrom, now + 250);
          trimMs = max(trimMs, (int)(1100 + (now - ackFrom) + 350));
        }
        const bool ours = now < quietUntil;  // our own sound (the ack, or the end of the answer)
        // What the mic heard since the last step, every 10 ms of it (wake::vadTake): a step can come late (a game
        // drawing at 6 pictures a second, a TLS handshake) and nothing said meanwhile is lost.
        const wake::Vad v = wake::vadTake();
        const bool strict = followUp && !asksFollow && !heard;  // close speech only (no question was asked)
        const bool loud = !ours && (strict ? lv > fl + FOLLOW_DB && lv > FOLLOW_MIN
                                           : lv > fl + (decided ? SPEAK_DB : 9) && lv > -60);
        if (strict) wake::vadSet(FOLLOW_DB, FOLLOW_MIN);
        else wake::vadSet(SPEAK_DB, -60);
        // While waiting for the question after a call, the wake word stays on: a new "Gâu gâu" starts afresh.
        wake::hold(heard || again || followUp);
        bool decidedNow = false;
        if (!decided && t >= 250) {
          decided = decidedNow = true;
          pausedAfterCall = !loud;
          trimMs = again || followUp || noCall ? 0 : pausedAfterCall ? 1100 : 950;  // keep 0.1 s (0.25 s speaking on) before the call ended
          if (pausedAfterCall && !again && !followUp) {
            // The dog answers its name at once (owner: heard from across the room): a word in its own voice, a
            // beep, or nothing (s3.assist "ack"). Its sound is not the question: not counted as speech, and cut
            // from the recording (trimmed up to its end), and the wake word does not hear it (the speaker gate).
            const uint8_t how = assist::prefs().ack;
            uint32_t ackMs = 0;
            if (how == 2) {
              assist::Speech sp;
              if (assist::line("ack", sp) && assist::play(sp))
                for (const voice::Item& it : sp.items) ackMs += (it.clip ? it.clip->samples / (RATE / 1000) : 0) + it.gapMs;
            }
            if (!ackMs && how) sound::blip(), ackMs = cues::length(cues::LISTEN, 1000);  // (its length in ms)
            quietUntil = millis() + ackMs + 250;
            wake::vadMute(ackMs + 250);  // the mic task too: a late step still never counts the ack
            trimMs += ackMs + 350;  // the call and the ack, to its very end
          }
          winFrom = max(millis(), quietUntil), winPeak = -90, winLoudMs = winOursMs = 0;
          ackFrom = pausedAfterCall && !again && !followUp ? millis() : 0;
          trace::add("talk: %s (level %.0f, floor %.0f dBFS)", followUp ? "follow-up: listening" : again ? "listening again"
                     : pausedAfterCall ? "pause after the call: ack sound" : "speaking on", lv, fl);
        }
        // The speech start: the mic task's score since the last restart (loud 10 ms +10, quiet -5: a knock fades,
        // speech builds up), restarted until the call's tail and our own sound are over. Never in the step that
        // decides: its score still holds the call's own tail when that step comes late (a slow loop: 9q on the board
        // took "gâu gâu" for the question 4 times in 15 and sent the turn 2 s later, "began -560 ms into the window").
        const bool oursNow = millis() < quietUntil;
        if (decided && !decidedNow && !ours && !oursNow && !heard) {
          loudMs = (int)wake::vadSpeechMs();
          if (v.peakDb > winPeak) winPeak = v.peakDb;
          winLoudMs += v.loudMs, winOursMs += v.oursMs;
        } else if (!heard) {
          wake::vadRestart();
          loudMs = 0;
        }
        // Speech: 240 ms of it in order, or (a call's question, not a follow-up) 300 ms of loud blocks in the window
        // however broken up: a soft voice far from the mic crosses the floor + 7 only on its louder syllables (the
        // 2.8" on 2026-11-21: "gọi lên mà AI ko ăn", "no question ... loud 340 ms" twice).
        if (!heard && (loudMs >= (followUp ? 280 : 240) || (decided && !decidedNow && !followUp && winLoudMs >= 300))) {
          heard = true, lastLoud = heardAt = millis();
          spPeak = max(lv, v.peakDb), turnFloor = fl, silentMs = 0;
          wake::vadQuestion(true, spPeak);
          talkShow(nullptr, T_("Listening"), T_("Say what you need"));
        } else if (heard && !ours) {
          spPeak = v.peakDb > spPeak ? v.peakDb : spPeak - 0.002f * dt;  // (for the turn's log)
        }
        if (heard) {
          // The question ends 2 s after the last loud 10 ms block, as on the EspOS Bot (owner, 2026-11-21: "từ lúc tao
          // ngừng nói 4-5s thì nó mới gửi"): counted block by block in the mic task against its rolling 2 s floor. It
          // was the loudest block of each UI step over the floor taken when the question began: a fan or the TV
          // peaked over that in most steps, so a quiet room's question often ran to the 8 s cap.
          const uint32_t pause = min<uint32_t>(millis() - heardAt, wake::vadQuietMs());
          silentMs = (int)wake::vadSilentMs();
          if (pause >= 2000 || (pause >= 1200 && silentMs >= 1200)) {
            endWait = pause >= 2000 ? 2000 : 1200;
            return send();
          }
          if (millis() - heardAt >= 8000) {
            endWait = 8000;
            return send();
          }
        }
        // (signed: winFrom is the ack's end, later than `now` while the ack plays)
        if (!heard && (followUp && !asksFollow ? t > FOLLOW_MS
                                               : decided && (int32_t)(now - winFrom) > (int32_t)QUESTION_MS)) {
          if (!followUp && winLoudMs >= 150) {  // something was said, softly or in pieces: the AI decides, not us
            trace::add("talk: a soft question (loud %d ms, loudest %.0f, floor %.0f dBFS): sent", winLoudMs, winPeak, fl);
            endWait = 0, heardAt = winFrom;
            return send();
          }
          trace::add("talk: no question after the %s (loudest %.0f, floor %.0f dBFS, loud %d ms, our sound %d ms)",
                     followUp ? "answer" : again ? "retry line" : "wake word", winPeak, fl, winLoudMs, winOursMs);
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
      if (warmBusy && t > 6000) warmBusy = false;  // a warm-up that never came back does not hold the question
      if (t > 45000) return failShow("look_up", nullptr, N_("Can't reach the server"));  // the slot never came free
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
  if (lock::accountBlocked()) {  // the account is blocked: nothing is recorded or sent; the pop-up says it calmly
    if (talkOpen()) talkShow("stand", T_("Your account is locked"), T_("See espos.dev for details"));
    return;
  }
  if (lic::aiLocked()) {  // the wake word still works: it brings the dog, who says how to unlock talking
    trace::add("talk: called by the wake word, talking is locked");
    return showLock();
  }
  if (!talkOpen()) return;
  if (waiting) wake::captureFree();
  // From 1.2 s before (the call itself too); 1.2 + 0.25 + the ack + the 6 s window + 8 s of question at most.
  if (!startListening(at - (uint32_t)(RATE * 6 / 5), 17000, true)) {
    // No room for the recording yet. On the board (2026-11-20) the call came over the aquarium saver, which lets its
    // pictures go about 2 s after the saver closes: the pop-up opened but never listened ("gọi không lên"). Tried
    // again for 4 s instead (retryWake), the pop-up saying it is listening meanwhile.
    trace::add("talk: no room to record yet (largest PSRAM block %u KB), trying again",
               (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) / 1024));
    retryAt = at, retryUntil = millis() + 4000;
    if (!retryTimer) retryTimer = lv_timer_create(retryWake, 100, nullptr);
    talkShow("alert", T_("I'm listening"), T_("Say what you need"));
    return;
  }
  wakeBegan(waiting, true);
}

void wakeBegan(bool waiting, bool callInside) {
  led::mode(led::Mode::Wake);
  decided = false, again = false, followUp = false, trimMs = 0, quietUntil = 0, ackFrom = 0, winLoudMs = winOursMs = 0;
  noCall = !callInside;
  trace::add("talk: called by the wake word%s", waiting ? " (again)" : !callInside ? " (after waiting for room to record)" : "");
}

void retryWake(lv_timer_t*) {
  const bool idle = st == St::Idle || st == St::After;
  if (!idle || millis() > retryUntil) {
    if (idle) trace::add("talk: still no room to record after 4 s (largest PSRAM block %u KB)",
                         (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) / 1024));
    lv_timer_delete(retryTimer);
    retryTimer = nullptr;
    retryUntil = 0;
    if (idle) talkShow("plead", T_("I'm here. Call me again, or hold the button and speak"), "");
    return;
  }
  // The mic ring keeps the last 2 s: the call itself (1.2 s before it fired) is in it for 0.8 s more; later, the
  // recording begins now and there is no call in it to cut (noCall).
  const uint32_t now = wake::now(), from = retryAt - (uint32_t)(RATE * 6 / 5);
  const bool callInside = now - from < (uint32_t)(RATE * 19 / 10);
  if (!startListening(callInside ? from : now, 17000, true)) return;
  lv_timer_delete(retryTimer);
  retryTimer = nullptr;
  retryUntil = 0;
  wakeBegan(false, callInside);
}

// After the dog's answer: FOLLOW_MS of listening without the call, the ears up (its own voice not counted).
void listenFollowUp() {
  if (!assist::prefs().follow) return set(St::After);  // the setting: every question starts with the call
  asksFollow = lastAsks;
  if (!startListening(wake::now(), asksFollow ? 15000 : 12000, true)) return set(St::After);
  talkShow(nullptr, lastReply.length() ? lastReply : String(T_("I'm listening")), T_("Listening"));  // its answer stays
  decided = false, again = false, followUp = true, trimMs = 0, quietUntil = millis() + 400, ackFrom = 0, winLoudMs = winOursMs = 0;
  wake::vadMute(400);
  if (asksFollow) trace::add("talk: follow-up: the dog asked, listening %lu ms for the answer", (unsigned long)QUESTION_MS);
  else trace::add("talk: follow-up: listening %lu ms for close speech, no call needed", (unsigned long)FOLLOW_MS);
}

// After the dog's "say it again" line: listen once more for 6 s (no call needed).
void listenAgain() {
  if (!startListening(wake::now(), 14500, true)) return;
  talkShow(nullptr, T_("Listening"), T_("Say what you need"));
  decided = false, again = true, followUp = false, trimMs = 0, quietUntil = 0;
}

// Talking is locked (lic.h): the trial is over, or the account has no AI. Each dog says it its own way, in its own
// voice (assist.pba "ai_trial_end" / "ai_locked", dog packages 1.0.1; an older package: the words only), and the QR
// in the pop-up opens the page that unlocks it. The bubble's words are these (the voice is Vietnamese).
const char* lockLineOf(const String& k, bool trial) {
  // i18n: the dog's own words when its free trial of talking is over: scan the QR code under it to keep chatting.
  // Each dog its manner (as "Didn't catch that"); short, the QR is right under it.
  if (trial) {
    if (k == "bao") return T_("Your free talks are used up, kid. Scan the code below to keep going. I'll wait.");
    if (k == "momo") return T_("The free talks are over, man. Scan this code and we keep chatting. No rush.");
    if (k == "boop") return T_("BOSS, THE FREE TALKS ARE OVER! SCAN THIS CODE, BOSS! I'VE GOT TONS TO SAY!");
    if (k == "leaf") return T_("Sweetie, our free talks are all used up. Scan this code so we can keep talking!");
    if (k == "volt") return T_("The free preview is over, audience! Scan this code to see the rest of my show!");
    if (k == "byte") return T_("Beep. Your trial talks have ended. Please scan the code to continue.");
    if (k == "shiba") return T_("Free talks are over. Scan the code. I'll wait.");
    if (k == "corgi") return T_("Darling, our free talks are over. Scan this code so the princess can keep chatting!");
    if (k == "husky") return T_("Alas, our free talks have ended! Scan this code and let me speak again!");
    if (k == "golden") return T_("Trial class is over, team! Scan this code and the coach keeps teaching! Go!");
    return T_("Your free trial of talking is over. Scan the code to keep chatting.");
  }
  // i18n: the dog's own words when talking is not on for this device's account: scan the QR code to turn it on
  if (k == "bao") return T_("Talking isn't open on this device, kid. Scan the code below. I'll wait.");
  if (k == "momo") return T_("Talking isn't open yet, man. Scan this code and we'll chat. No rush.");
  if (k == "boop") return T_("BOSS, TALKING ISN'T ON! SCAN THIS CODE, BOSS! HURRY!");
  if (k == "leaf") return T_("Sweetie, talking isn't open yet. Scan this code so we can chat!");
  if (k == "volt") return T_("The curtain isn't up yet, audience! Scan this code to put me on air!");
  if (k == "byte") return T_("Beep. Talking is not activated. Please scan the code.");
  if (k == "shiba") return T_("Talking isn't on. Scan the code. I'll wait.");
  if (k == "corgi") return T_("Darling, talking isn't open yet. Scan this code so the princess can chat with you!");
  if (k == "husky") return T_("Alas, the door to talking is still locked! Scan this code and let me speak!");
  if (k == "golden") return T_("Talking class hasn't started yet, team! Scan this code and the coach comes in! Go!");
  return T_("Talking isn't on for this device yet. Scan the code to turn it on.");
}

uint32_t lockShownAt = 0, lockSyncAt = 0;

void showLock() {
  if (wake::capturing()) wake::captureFree();
  // Unlocked on the phone a moment ago? The next sync brings the new licence (at most every 30 s from here).
  if (!lockSyncAt || millis() - lockSyncAt > 30000) lockSyncAt = millis(), desk::syncNow();
  if (!talkOpen()) return finish();
  const bool trial = !strcmp(lic::lockedWhy(), "trial_ended");
  const dogs::Info* d = dogs::chosen();
  bool voiced = false;
  if (millis() - lockShownAt > 3000 || !voice::busy()) {  // called again at once: the line is not started twice
    assist::Speech s;
    voiced = assist::line(trial ? "ai_trial_end" : "ai_locked", s) && assist::play(s);
  }
  lockShownAt = millis();
  talkShow("plead", lockLineOf(d ? d->key : "", trial), "");
  String url = lic::buyUrl();
  if (!url.length()) url = ESPOS_WEB;  // the server sent no address: the web, where the account is
  talkLock(url.c_str());
  talkButton(0);
  talkLevel(-1);
  led::mode(led::Mode::Done);
  trace::add("talk: locked (%s), %s, the QR opens %s", trial ? "the trial is over" : "no AI",
             voiced ? "the dog says it" : "no recorded line (words only)", url.c_str());
  set(St::After);
  startTimer();
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
  if (lock::accountBlocked()) return talkShow("stand", T_("Your account is locked"), T_("See espos.dev for details"));
  if (lic::aiLocked()) return showLock();  // nothing is recorded: the dog says how to unlock talking
  if (!startListening(wake::now() - RATE / 10, 12100, false)) {
    // the microphone is there: no room to record right now (PSRAM in pieces), not a broken microphone
    if (!mic::available()) return talkShow("look_up", T_("The microphone isn't working"), "");
    trace::add("talk: no room to record (largest PSRAM block %u KB)",
               (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) / 1024));
    talkShow("plead", T_("I'm here. Call me again, or hold the button and speak"), "");  // translated in every pack
  }
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
bool warming() { return warmBusy; }
bool listening() { return st == St::Listen; }
bool canTalk() { return sound::micChannel() && desk::link() == desk::Link::Linked; }
bool canChat() { return desk::link() == desk::Link::Linked; }

void closed() {  // the pop-up went away: the turn stops
  if (st == St::Idle) return;
  trace::add("talk: pop-up closed: the turn stops (state %d)", (int)st);
  cancelTurn();
  // Always freed (0.5.6 freed it only while still capturing: cancelTurn had just stopped it, so a pop-up closed while
  // listening left the recording held, and the wake word, which waits for no recording, never fired again).
  wake::captureFree();
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

// A typed turn (the pop-up's chips, the console): the same request as a spoken one with {"text"} in place of the
// recording (and "intent": what the chip asks for, the server's hint), the same answer: words, mood, voice.
static bool askText(const String& text, const char* intent) {
  if (st != St::Idle && st != St::After) return false;
  JsonDocument d;
  d["text"] = text;
  if (intent) d["intent"] = intent;
  const String k = dogKey();
  if (k.length()) d["dog"] = k;
  String body;
  serializeJson(d, body);
  uint8_t* b = (uint8_t*)heap_caps_malloc(body.length() + 1, MALLOC_CAP_SPIRAM);
  if (!b) return false;
  memcpy(b, body.c_str(), body.length());
  if (st == St::After) finish();
  byWake = false, typed = true, fallback = 0;
  pendingWav = pendingBase = b, pendingLen = body.length(), pendingJson = true;
  wake::hold(true);
  startTimer();
  talkShow("look_up", T_("Thinking"), text);
  led::mode(led::Mode::Think);
  trace::add("talk: asked by a chip (%s)", intent ? intent : "text");
  set(St::Send);
  trySubmit();  // at once, or at the next step when the slot is busy (a warm-up, a turn being stopped)
  return true;
}

// The pop-up's "Today's news" chip: the dog sums up today's news (the server looks it up: "intent": "news").
void askNews() {
  if (desk::link() != desk::Link::Linked) {
    talkShow("look_up", T_(net::state() == net::State::Connected ? N_("Link this device to your account first")
                                                                 : N_("No internet")), "");
    return;
  }
  if (lock::accountBlocked()) return talkShow("stand", T_("Your account is locked"), T_("See espos.dev for details"));
  if (lic::aiLocked()) return showLock();
  if (st != St::Idle && st != St::After) cancelTurn(), finish();  // a turn on: it stops, the news goes
  // i18n: what the "Today's news" chip asks the dog: sent to the AI, and shown in the bubble under its answer
  askText(T_("What's in today's news?"), "news");
}

bool console(const char* cmd) {
  if (!strncmp(cmd, "ASK ", 4)) {  // ASK <text>; ASK NEWS: the pop-up's "Today's news" chip
    if (!talkOpen()) return Serial.println("ASK busy"), true;
    const bool news = !strcmp(cmd + 4, "NEWS");
    const bool sent = news ? (askNews(), st == St::Send || st == St::Wait) : askText(cmd + 4, nullptr);
    Serial.println(sent ? "ASK sent" : "ASK busy");
    return true;
  }
  if (!strncmp(cmd, "SAYAGAIN ", 9)) {  // SAYAGAIN <dog key>: that dog's "say it again" line in the pop-up (text checks)
    if (talkOpen()) talkShow("alert", sayAgainOf(cmd + 9), "");
    return true;
  }
  if (!strcmp(cmd, "WAKEFIRE") || !strcmp(cmd, "WAKEFIRE DRY")) {  // as if "Gâu gâu" was heard now (tests, demos)
    // DRY: the window, the ack and the speech counting run as usual, but nothing is sent (no AI call).
    onWake(wake::now());
    dryRun = cmd[8] == ' ' && st == St::Listen;
    return true;
  }
  if (!strncmp(cmd, "DOGBENCH", 8)) {  // DOGBENCH [clip]: the dog's picture decoders on 20 pictures of a clip
    Serial.printf("DOGBENCH %s\n", dog::bench(cmd[8] == ' ' ? cmd + 9 : "happy", 20).c_str());
    return true;
  }
#if defined(ESPOS_QA) || defined(SIM)
  if (!strcmp(cmd, "TALKPRESS") || !strcmp(cmd, "TALKRELEASE")) {  // the talk button pressed / let go (tests)
    if (cmd[4] == 'P') press();
    else release();
    return Serial.println("OK"), true;
  }
#endif
  if (!strcmp(cmd, "SUMMON")) {  // the pop-up over what is shown, as a call brings it (no turn, no app closed)
    talkOpen();
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
