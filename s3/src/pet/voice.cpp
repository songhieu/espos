#include "voice.h"

#include <Arduino.h>
#include <opus.h>

#include <vector>

#include "dog.h"
#include "packs.h"
#include "sound.h"
#include "trace.h"

#ifndef SIM
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#endif

namespace pet {
namespace voice {
namespace {

// ONE gain for every line (they are levelled to -16 LUFS with peaks at full scale). 256 = as recorded.
// Lower it here, for all lines at once, if the small speaker distorts.
constexpr int VOICE_GAIN = 256;
constexpr int MAXI = 12;
constexpr uint32_t RATE = 16000;  // every PawBoop voice file (pba.h)

Stats st;
OpusDecoder* dec;
const pba_clip_t* cur = nullptr;
const pba_clip_t* prev = nullptr;  // the last line said (kept through hush)
uint32_t startMs = 0, endMs = 0;
bool everSpoke = false, mute = false;
// The line picked last for each moment of each voice file: a small table in PSRAM (no internal RAM).
struct Last {
  uint32_t key;  // hash of the file and the moment; 0 = free
  char name[16];
};
constexpr int LASTS = 160;
Last* lasts = nullptr;
Last* lastFor(const void* file, const char* on) {
  uint32_t h = 2166136261u ^ (uint32_t)(uintptr_t)file;
  for (int i = 0; i < 16 && on[i]; i++) h = (h ^ (uint8_t)on[i]) * 16777619u;
  if (!h) h = 1;
  if (!lasts && !(lasts = (Last*)heap_caps_calloc(LASTS, sizeof(Last), MALLOC_CAP_SPIRAM))) return nullptr;
  for (int i = 0; i < LASTS; i++)
    if (lasts[i].key == h || !lasts[i].key) {
      lasts[i].key = h;
      return &lasts[i];
    }
  return &lasts[h % LASTS];  // full (never with 44 + 21 moments): share a slot
}

inline int16_t gain(int v) {
  if (VOICE_GAIN == 256) return (int16_t)v;
  v = v * VOICE_GAIN / 256;
  return (int16_t)constrain(v, -32768, 32767);
}

bool makeDecoder() {
  if (dec) return true;
  int err = 0;
  dec = opus_decoder_create(RATE, 1, &err);  // ~18 KB, lands in PSRAM
  if (err != OPUS_OK) dec = nullptr, trace::add("pet: opus decoder %d", err);
  return dec;
}

// One clip into `out` (called with its samples, after pre-skip), packet by packet. false: stop (asked or failed).
template <class Out>
bool decodeClip(const Item& it, Out out, volatile uint32_t* gen = nullptr, uint32_t want = 0) {
  const pba_clip_t* l = it.clip;
  uint8_t* buf = it.pack->mapped() ? nullptr : (uint8_t*)heap_caps_malloc(l->bytes ? l->bytes : 1, MALLOC_CAP_SPIRAM);
  const uint8_t* p = it.pack->packets(l, buf);
  if (!p) {
    st.readFails++;
    heap_caps_free(buf);
    return true;  // a card read failed: this clip is silent, the next ones still play
  }
  const uint8_t* end = p + l->bytes;
  int16_t pcm[960];
  opus_decoder_ctl(dec, OPUS_RESET_STATE);  // every clip starts clean
  uint32_t skip = it.pack->head()->pre_skip, left = l->samples;
  bool go = true;
  for (int k = 0; k < l->packets && left && p + 2 <= end; k++) {
    if (gen && *gen != want) {
      go = false;
      break;
    }
    const int len = p[0] | p[1] << 8;
    if (p + 2 + len > end) break;
    const uint32_t t0 = nowUs();
    const int got = opus_decode(dec, p + 2, len, pcm, 960, 0);
    const uint32_t us = nowUs() - t0;
    st.packets++, st.decUs += us, st.decMaxUs = max(st.decMaxUs, us);
    p += 2 + len;
    if (got <= 0) break;
    const uint32_t from = min((uint32_t)got, skip);
    skip -= from;
    const uint32_t m = min((uint32_t)got - from, left);
    if (!out(pcm + from, m)) {
      go = false;
      break;
    }
    left -= m;
  }
  heap_caps_free(buf);
  return go;
}

#ifndef SIM
// ------------------------------------------------------------------ board: a decoder task, three rings
constexpr uint32_t CAP = 16000;  // 1 s of samples a ring; the decoder stays ahead of the speaker
struct Ring {
  int16_t* pcm = nullptr;
  volatile uint32_t w = 0, r = 0;
  volatile bool done = true;
  Item it[MAXI];
  int n = 0;
  uint32_t t0 = 0;  // micros() at speak
};
Ring rings[3];
int nextRing = 0;
volatile uint32_t wantGen = 0;
volatile int wantRing = -1;
TaskHandle_t task;
portMUX_TYPE vLock = portMUX_INITIALIZER_UNLOCKED;

int pull(short* out, int n, void* ctx) {
  Ring* g = (Ring*)ctx;
  const uint32_t avail = g->w - g->r;
  if (!avail) {
    if (g->done) return 0;
    memset(out, 0, n * 2);  // the decoder is late: silence rather than a stop
    st.underruns++;
    return n;
  }
  if (!g->r) st.firstSoundUs = micros() - g->t0;
  const int m = min((uint32_t)n, avail);
  for (int i = 0; i < m; i++) out[i] = gain(g->pcm[(g->r + i) % CAP]);
  g->r += m;
  return m;
}

void voiceTask(void*) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    for (;;) {
      portENTER_CRITICAL(&vLock);
      const uint32_t gen = wantGen;
      const int ri = wantRing;
      portEXIT_CRITICAL(&vLock);
      if (ri < 0) break;
      Ring& g = rings[ri];
      // Into the ring, waiting while it is full; false when another line was asked for.
      auto put = [&](const int16_t* s, uint32_t m) {
        while (g.w - g.r + m > CAP && wantGen == gen) vTaskDelay(5);
        if (wantGen != gen) return false;
        for (uint32_t i = 0; i < m; i++) g.pcm[(g.w + i) % CAP] = s ? s[i] : 0;
        __sync_synchronize();
        g.w += m;
        return true;
      };
      for (int k = 0; k < g.n && wantGen == gen; k++) {
        if (!decodeClip(g.it[k], put, &wantGen, gen)) break;
        for (uint32_t z = (uint32_t)g.it[k].gapMs * (RATE / 1000); z && wantGen == gen;) {  // the gap: silence
          const uint32_t m = min<uint32_t>(z, 480);
          if (!put(nullptr, m)) break;
          z -= m;
        }
      }
      g.done = true;
      portENTER_CRITICAL(&vLock);
      if (wantGen == gen) wantRing = -1;
      portEXIT_CRITICAL(&vLock);
    }
  }
}

bool start() {
  if (task) return true;
  if (!makeDecoder()) return false;
  for (Ring& g : rings)
    if (!g.pcm && !(g.pcm = (int16_t*)heap_caps_malloc(CAP * 2, MALLOC_CAP_SPIRAM))) return false;
  // Core 0 next to the sound task; the stack is in PSRAM (opus keeps its scratch on the heap, see lib/opus).
  if (xTaskCreatePinnedToCoreWithCaps(voiceTask, "petvoice", 8192, nullptr, 3, &task, 0, MALLOC_CAP_SPIRAM) != pdPASS) {
    task = nullptr;
    trace::add("pet: no voice task");
    return false;
  }
  return true;
}

void play(const Item* items, int n) {
  if (!start()) return;
  Ring& g = rings[nextRing];
  portENTER_CRITICAL(&vLock);
  g.w = 0;
  g.r = 0;
  g.done = false;
  g.n = n;
  for (int k = 0; k < n; k++) g.it[k] = items[k];
  g.t0 = micros();
  wantRing = nextRing;
  wantGen = wantGen + 1;
  portEXIT_CRITICAL(&vLock);
  nextRing = (nextRing + 1) % 3;
  xTaskNotifyGive(task);
  // Start the speaker once the decoder has the first packet out (a few ms; a card read first), so a line never
  // begins with a gap of silence.
  for (int k = 0; k < 30 && g.w - g.r == 0 && !g.done; k++) vTaskDelay(1);
  sound::stream(pull, &g);
}
void stopDecoder() {  // whatever the task decodes is not wanted any more (it may point at a dog going away)
  portENTER_CRITICAL(&vLock);
  wantGen = wantGen + 1;
  wantRing = -1;
  portEXIT_CRITICAL(&vLock);
}
#else
void stopDecoder() {}
// ------------------------------------------------------------------ simulator: decode it all now
std::vector<int16_t> simPcm;
size_t simAt = 0;

int pull(short* out, int n, void*) {
  const int m = (int)min<size_t>(n, simPcm.size() - simAt);
  for (int i = 0; i < m; i++) out[i] = gain(simPcm[simAt + i]);
  simAt += m;
  return m;
}

void play(const Item* items, int n) {
  if (!makeDecoder()) return;
  simPcm.clear();
  simAt = 0;
  for (int k = 0; k < n; k++) {
    decodeClip(items[k], [](const int16_t* s, uint32_t m) {
      simPcm.insert(simPcm.end(), s, s + m);
      return true;
    });
    simPcm.insert(simPcm.end(), (size_t)items[k].gapMs * (RATE / 1000), 0);
  }
  sound::stream(pull, nullptr);
}
#endif

void begin(const Item* items, int n) {
  uint32_t ms = 0;
  for (int k = 0; k < n; k++) ms += (uint32_t)(items[k].clip->samples * 1000ull / RATE) + items[k].gapMs;
  cur = prev = items[0].clip;
  everSpoke = true;
  startMs = millis();
  endMs = startMs + ms;
  st.lines++;
  trace::add("pet: says %.16s%s%s", items[0].clip->name, n > 1 ? " + kit" : "", mute ? " (muted)" : "");
  if (mute) sound::stopStream();
  else play(items, n);
}

}  // namespace

Stats& stats() { return st; }

const pba_clip_t* of(const pk::Pba& pack, const char* on) {
  if (!on || !pack.ok()) return nullptr;
  const pba_clip_t* all[8];
  int n = 0;
  for (int i = 0; i < pack.count() && n < 8; i++)
    if (!strncmp(pack.at(i)->on, on, 16)) all[n++] = pack.at(i);
  if (!n) return nullptr;
  Last* last = lastFor(pack.head(), on);
  const pba_clip_t* pick[8];
  int m = 0;
  for (int i = 0; i < n; i++)
    if (all[i] != prev && !(last && !strncmp(last->name, all[i]->name, 16))) pick[m++] = all[i];
  if (!m) {  // its only line: not right after itself
    if (all[0] == prev) return nullptr;
    pick[m++] = all[0];
  }
  const pba_clip_t* l = pick[random(m)];
  if (last) memcpy(last->name, l->name, 16);
  return l;
}

const pba_clip_t* lineFor(const char* on) {
  if (!on || !pk::ready()) return nullptr;
  return of(pk::lines(), on);
}

const pba_clip_t* answerTo(const pba_clip_t* first, const char* second) {
  if (!first || !second) return nullptr;
  // caught_1, caught_1_2 ... -> caught_2, caught_2_2 ...: the same index k.
  const char* base = first->on[0] == '@' ? first->on + 1 : first->on;
  const size_t bl = strnlen(base, 15);
  if (strncmp(first->name, base, bl)) return nullptr;
  char nm[24];
  snprintf(nm, sizeof nm, "%s%.*s", second[0] == '@' ? second + 1 : second, (int)(strnlen(first->name, 16) - bl), first->name + bl);
  return pk::line(nm);
}

const pba_clip_t* notAgain(const pba_clip_t* l) {
  if (!l || l != prev) return l;
  return lineFor(l->on);
}

const pba_clip_t* last() { return prev; }

bool busy() { return cur && (int32_t)(millis() - endMs) < 0; }

uint32_t quietMs() {
  if (!everSpoke) return 1u << 30;
  return busy() ? 0 : millis() - endMs;
}

const pba_clip_t* current() { return busy() ? cur : nullptr; }
uint32_t startedAt() { return startMs; }

bool speak(const pba_clip_t* l, bool force) {
  if (!l || l == prev || (busy() && !force) || !pk::ready()) return false;
  const Item it = {&pk::lines(), l, 0};
  begin(&it, 1);
  return true;
}

bool say(const Item* items, int n) {
  n = min(n, MAXI);
  for (int k = 0; k < n; k++)
    if (!items[k].pack || !items[k].clip) return false;
  if (n <= 0) return false;
  begin(items, n);
  return true;
}

void hush() {  // as the page: the dog counts as quiet for long, so its own talk may come next
  everSpoke = false;
  cur = nullptr;
  sound::stopStream();
  stopDecoder();
}

void setMuted(bool m) {
  mute = m;
  if (m) sound::stopStream();
}
bool muted() { return mute; }

void release() {
  hush();
  cur = prev = nullptr;
  if (lasts) memset(lasts, 0, LASTS * sizeof(Last));
}

}  // namespace voice
}  // namespace pet
