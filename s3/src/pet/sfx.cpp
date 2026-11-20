// Size first (the flash is tight); the mixer itself (busPull, 50 times a second on the sound task) stays -O2.
#pragma GCC optimize("Os")
#include "sfx.h"

#include <Arduino.h>
#include <Preferences.h>
#include <opus.h>

#include <atomic>
#include <mutex>
#include <new>

#include "desk.h"
#include "formats.h"
#include "game.h"
#include "packs.h"
#include "os.h"
#include "pet.h"
#include "talk.h"
#include "trace.h"
#include "voice.h"

#ifndef SIM
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#else
#include <thread>
bool simLiveSound();  // sim/live.cpp: the speaker plays in real time (the script simulator drains streams at once)
#endif

namespace pet {
namespace sfx {
namespace {

constexpr int RATE = 16000;
constexpr int NSLOT = 10, NSTREAM = 4, NJOB = 10, NCMD = 24, NPCM = 48, NCD = 24;
constexpr uint32_t CACHE_PET = 512 * 1024, CACHE_RUN = 256 * 1024;  // decoded effects kept, PSRAM (the run needs its own)
constexpr uint32_t RING = 16384;            // 1 s of a stream ahead of the speaker (a power of two)
constexpr int VFADE = 160;                  // 10 ms: a voice cut by another
constexpr int FR = 8;                       // a slot's place: samples x 256 (its pitch is the step)
constexpr int32_t ONE = 1 << 16;            // envelopes, Q16
constexpr int32_t UNITY = 4096;             // gains, Q12
constexpr int32_t DUCK = 1720;              // the tune under the voice: -7.5 dB (the phone game's compressor)
constexpr uint32_t IDLE_BED_MS = 30000;     // no touch this long: no tune, no ambience (the wake word hears better)
// The owner (2026-11-20): the tunes and the ambience (continuous, they mask a call alike) quieter than the phone
// game's, on by default. -18 dB: the speaker sits next to the mic; at the phone game's level a tune reaches
// -36..-41 dBFS there and only ~5 of 35 "Gâu gâu" calls get through, 18 dB lower it is near the -55 dBFS row
// (23-24 of 35; 26-28 with no tune). A gain here, the files unchanged; effects and the dog's lines as they were.
constexpr float BED_DB = -18.0f;
#ifdef SIM
constexpr int MAXN = 8192;  // the live simulator's audio callback asks for more at a time than the sound task
#else
constexpr int MAXN = 2048;  // the sound task asks for 320 (20 ms)
#endif

inline void* psram(size_t n) { return heap_caps_malloc(n ? n : 1, MALLOC_CAP_SPIRAM); }
inline int32_t gainOf(float db) { return (int32_t)(UNITY * powf(10.0f, db / 20.0f)); }
inline int32_t perSample(float seconds) { return ONE / max(1, (int)(seconds * RATE)); }
inline int32_t ramp(int32_t v, bool down, int32_t d) { return down ? (v > d ? v - d : 0) : (v + d < ONE ? v + d : ONE); }
inline int32_t scale(int32_t v, int32_t gain, int32_t env, int32_t bus) {  // a sample x gain (Q12) x envelopes (Q16)
  int64_t t = (int64_t)v * gain;
  t = t * (env >> 4) >> 12;
  return (int32_t)(t * (bus >> 4) >> 12);
}

// The cue table: sfx.cue of vn.pawboop.pet / vn.pawboop.run ("PBC1", u16 count, u16 0, then the records),
// made by tools/pawboop/sounds.py from the phone game's calls.
struct __attribute__((packed)) Cue {
  char name[16];   // the clip, the event or the place
  char kind[16];   // the sound's moment in the banks ("eat_crunch")
  char alt[16];    // played instead while `kind` waits for its cooldown or is missing (the phone game's lists)
  uint16_t t;      // ms: from the clip's start, or the event's delay
  uint16_t cd;     // ms: the same kind not again so soon (the phone game's per-dog cooldowns)
  int8_t db;       // the call's own dB (the kit's volume is in the sound)
  uint8_t p0, p1;  // pitch x 100, a random one between
  uint8_t type;    // 0 a clip's, 1 an event's, 2 a place's tune
  uint8_t loop;    // 0 a one-shot; a loop: fade in, 1/20 s + 1
  uint8_t out;     // a loop's fade out, 1/20 s
  uint8_t once;    // a clip's cue: on its first pass only
  uint8_t pr;      // a chance, % (0: always)
};
static_assert(sizeof(Cue) == 60, "sfx.cue record");

struct Pcm {  // an effect decoded whole (PSRAM), shared by the slots that play it
  const pba_clip_t* clip = nullptr;
  int16_t* s = nullptr;
  uint32_t n = 0, used = 0, gen = 0;
  std::atomic<int> refs{0};
};
struct Slot {
  Pcm* p = nullptr;
  uint32_t pos = 0, step = 1 << FR;
  uint32_t wait = 0;  // samples before it starts
  int32_t gain = UNITY;
  int32_t env = ONE, envStep = 0;
  uint16_t key = 0;  // a loop's key, 0: a one-shot
};
struct Seg {  // a part of a tune in its file
  uint32_t at = 0, bytes = 0, samples = 0;
};
struct Src {
  pkg::File f;
  Seg seg[2];  // its start (played once) and the part that loops (a tune that loops from a later point)
  uint16_t pre = 0;
};
struct Stream {
  Src src;
  OpusDecoder* dec = nullptr;
  uint8_t* chunk = nullptr;  // 4 KB of packets read ahead
  uint32_t chunkAt = 0, chunkN = 0, pos = 0, skip = 0, left = 0;
  uint8_t part = 0;
  int16_t* ring = nullptr;
  volatile uint32_t w = 0, r = 0;
  std::atomic<uint8_t> st{0};  // 0 free, 1 filling (the task), 2 playing, 3 done (the task frees it)
  uint8_t role = 0;            // 0 the tune, 1 the ambience
  uint16_t id = 0;
  int32_t gain = UNITY, env = 0, envStep = 0;
};
enum CmdType : uint8_t { C_START, C_STOP, C_STOPALL, C_VOICE, C_FADE, C_PITCH };
struct Cmd {
  CmdType t;
  uint8_t role;
  uint16_t key;  // C_STOP, C_PITCH: the loop; C_FADE: the stream that stays
  int32_t step;
  Slot slot;
  sound::Pull pull;
  void* ctx;
};
struct Job {
  const pk::Pba* pack = nullptr;
  const pba_clip_t* clip = nullptr;
  Slot slot;
  uint32_t gen = 0;
  uint8_t stream = 0;  // 0 an effect; 1 the tune, 2 the ambience
  uint16_t id = 0;
  Src src;
};
struct Cd {
  uint16_t key;
  uint32_t until;
};

// Everything in PSRAM, made at the first sound (internal RAM is scarce).
struct St {
  // the sound task's
  Slot slots[NSLOT];
  Stream streams[NSTREAM];
  sound::Pull vPull = nullptr, oPull = nullptr;  // the voice; the one fading out
  void *vCtx = nullptr, *oCtx = nullptr;
  int oLeft = 0;
  int32_t fxEnv = ONE, musEnv = ONE, duck = UNITY, lim = ONE;
  int32_t acc[MAXN];  // the mix
  // to the sound task (CLOCK)
  Cmd cmds[NCMD];
  int cmdN = 0;
  // the decoding task's (jMx)
  std::mutex jMx;
  Job jobs[NJOB];
  int jobN = 0;
  Pcm pcms[NPCM];
  uint32_t cacheBytes = 0, useClock = 0;
  OpusDecoder* fxDec = nullptr;
  int16_t pcm[960];  // one packet decoded (the task's)
  // the UI's
  pk::Pba banks[3];  // the dog's, the pet's, the run's sfx.pba
  String bankId[3];
  uint8_t* cueFile = nullptr;
  const Cue* cues = nullptr;
  int ncues = 0;
  String cueId;
  const Cue* clipCues = nullptr;  // the clip showing: its cues (together in the table)
  int clipN = 0, clipPass = 0, lastPic = -1;
  uint16_t clipLoop[4] = {};  // the loops this clip started (they end with it)
  Cd cds[NCD] = {};
  String wantWorld, playingTune, playingAmb;  // the map; what the streams play now
};
St* S = nullptr;
std::atomic<bool> fxPaused{false}, musPaused{false}, running{false}, working{false};
std::atomic<uint32_t> lastPull{0}, startedAt{0}, gen{1}, lastLoud{0}, cacheMax{CACHE_PET};  // lastLoud: the voice or an effect sounded
enum { B_DOG, B_PET, B_RUN };

bool made() {
  if (S) return true;
  void* m = psram(sizeof(St));
  if (m) S = new (m) St();
  return S;
}

#ifndef SIM
portMUX_TYPE cLock = portMUX_INITIALIZER_UNLOCKED;
#define CLOCK() portENTER_CRITICAL(&cLock)
#define CUNLOCK() portEXIT_CRITICAL(&cLock)
TaskHandle_t task = nullptr;
#else
std::mutex cMx;
#define CLOCK() cMx.lock()
#define CUNLOCK() cMx.unlock()
bool task = false;
#endif

Cmd* push() { return S->cmdN < NCMD ? &S->cmds[S->cmdN++] : nullptr; }  // under CLOCK

void post(CmdType t, uint8_t role = 0, uint16_t key = 0, int32_t step = 0) {
  CLOCK();
  if (Cmd* c = push()) c->t = t, c->role = role, c->key = key, c->step = step;
  CUNLOCK();
}

void release(Slot& s) {
  if (s.p) s.p->refs--;
  s.p = nullptr;
}

void apply(Cmd& c) {
  switch (c.t) {
    case C_START:
      for (Slot& s : S->slots)
        if (!s.p) {
          s = c.slot;
          return;
        }
      c.slot.p->refs--;  // no room: dropped
      return;
    case C_STOP:
      for (Slot& s : S->slots)
        if (s.p && s.key == c.key) s.envStep = -c.step;
      return;
    case C_PITCH:
      for (Slot& s : S->slots)
        if (s.p && s.key == c.key) s.step = (uint32_t)c.step;
      return;
    case C_STOPALL:
      for (Slot& s : S->slots) release(s);
      for (Stream& m : S->streams)
        if (m.st == 2) m.st = 3;
      S->oPull = S->vPull = nullptr;
      return;
    case C_FADE:  // the streams of a role fade out, but the one that comes
      for (Stream& m : S->streams)
        if (m.st == 2 && m.role == c.role && m.id != c.key) m.envStep = -c.step;
      return;
    case C_VOICE:
      if (S->vPull && S->vPull != c.pull) S->oPull = S->vPull, S->oCtx = S->vCtx, S->oLeft = VFADE;
      S->vPull = c.pull, S->vCtx = c.ctx;
      return;
  }
}

// The bus: the voice, the effects, the tune and the ambience, 20 ms at a time on the sound task. 0: all done.
__attribute__((optimize("O2"))) int busPull(short* out, int n, void*) {
  lastPull = millis();
  St& g = *S;
  if (n > MAXN) n = MAXN;
  CLOCK();  // a few loops over the slots and the streams (no copy: the sound task's stack is small)
  for (int i = 0; i < g.cmdN; i++) apply(g.cmds[i]);
  g.cmdN = 0;
  CUNLOCK();
  int32_t* acc = g.acc;
  bool live = false;
  int vn = 0;
  if (g.vPull) {
    vn = g.vPull(out, n, g.vCtx);
    if (vn <= 0) g.vPull = nullptr, vn = 0;
    else live = true;
  }
  for (int i = 0; i < n; i++) acc[i] = i < vn ? out[i] * UNITY : 0;
  if (g.oPull) {
    const int m = g.oPull(out, min(n, g.oLeft), g.oCtx);
    for (int i = 0; i < m; i++) acc[i] += out[i] * UNITY / VFADE * (g.oLeft - i);
    g.oLeft -= max(m, 0);
    if (m <= 0 || g.oLeft <= 0) g.oPull = nullptr;
    else live = true;
  }
  // A pause: 30 ms down (the assistant's turn records right after); then the one-shots are dropped and the loops and
  // the streams wait where they are.
  const int32_t pauseStep = ONE / 480;
  const bool fp = fxPaused, mp = musPaused;
  for (Slot& s : g.slots) {
    if (!s.p) continue;
    if (fp && g.fxEnv == 0) {
      if (!s.key) release(s);
      continue;
    }
    live = true;
    const int16_t* pcm = s.p->s;
    const uint32_t len = s.p->n;
    int j = 0;
    if (s.wait) {
      j = (int)min<uint32_t>(s.wait, n);
      s.wait -= j;
    }
    for (; j < n; j++) {
      uint32_t at = s.pos >> FR;
      if (at >= len) {
        if (!s.key) break;
        s.pos -= len << FR, at = s.pos >> FR;
      }
      const int32_t a = pcm[at], b = at + 1 < len ? pcm[at + 1] : s.key ? pcm[0] : 0;
      const int32_t v = a + ((b - a) * (int32_t)(s.pos & ((1 << FR) - 1)) >> FR);
      acc[j] += scale(v, s.gain, s.env, ramp(g.fxEnv, fp, pauseStep * j));
      s.pos += s.step;
      if (s.envStep) {
        s.env += s.envStep;
        if (s.env >= ONE) s.env = ONE, s.envStep = 0;
        if (s.env <= 0) break;
      }
    }
    if ((!s.key && (s.pos >> FR) >= len) || s.env <= 0) release(s);
  }
  for (Stream& m : g.streams) {
    const bool p = m.role ? fp : mp;  // the ambience goes with the effects (the phone game's SOUND switch)
    const int32_t be = m.role ? g.fxEnv : g.musEnv;
    if (m.st != 2 || (p && be == 0)) continue;
    live = true;
    const int32_t gain = m.role ? m.gain : (int32_t)((int64_t)m.gain * g.duck / UNITY);
    for (int j = 0; j < n && m.r != m.w; j++) {  // late: a gap rather than a stop
      const int32_t v = m.ring[m.r % RING];
      m.r = m.r + 1;
      acc[j] += scale(v, gain, m.env, ramp(be, p, pauseStep * j));
      if (m.envStep) {
        m.env += m.envStep;
        if (m.env >= ONE) m.env = ONE, m.envStep = 0;
        if (m.env <= 0) {
          m.env = 0, m.st = 3;
          break;
        }
      }
    }
  }
  g.fxEnv = ramp(g.fxEnv, fp, pauseStep * n);
  g.musEnv = ramp(g.musEnv, mp, pauseStep * n);
  const int32_t duckTo = vn ? DUCK : UNITY;  // attack 20 ms, release 300 ms (the phone game's compressor)
  g.duck = duckTo < g.duck ? duckTo : min(duckTo, g.duck + (UNITY - DUCK) / 15);
  // Never clipped: the voice alone peaks at full scale, the effects on top would go over. The phone game's
  // Master bus has hard limiters; here the 20 ms gets the gain that keeps its peak at full scale, coming back
  // over ~160 ms (a voice alone is never touched).
  int32_t peak = 0;
  for (int i = 0; i < n; i++) peak = max(peak, abs(acc[i]));
  const int32_t lim = 32767 * UNITY;
  const int32_t need = peak > lim ? (int32_t)((int64_t)lim * ONE / peak) : ONE;
  g.lim = need < g.lim ? need : min(need, g.lim + ONE / 8);
  if (vn || g.oPull || (!fp && g.fxEnv)) {  // more than a tune or an ambience (the wake word: sfx::bedOnly)
    bool fx = vn || g.oPull;
    for (const Slot& s : g.slots) fx |= s.p && !s.wait;
    if (fx) lastLoud = millis();
  }
  for (int i = 0; i < n; i++) out[i] = (short)constrain((int32_t)((int64_t)acc[i] * g.lim >> 16) / UNITY, -32768, 32767);
  if (live) return n;
  running = false;
  return vn;
}

// The speaker plays the bus (again, after a chime, an alarm or the assistant's voice took it).
void ensure() {
  const uint32_t now = millis();
  if (running.exchange(true) && (now - lastPull < 150 || now - startedAt < 150)) return;
  startedAt = now;
  sound::stream(busPull, nullptr);
}

// The speaker was asked for the bus half a second ago and never pulled it (no codec: QEMU; or another stream has
// it): what waits for it is dropped, so no decoded sound stays held by a queue nobody reads.
bool deaf() {
  const uint32_t at = startedAt;
  if (!running || (int32_t)(lastPull - at) >= 0 || millis() - at < 500) return false;
  CLOCK();
  for (int i = 0; i < S->cmdN; i++)
    if (S->cmds[i].t == C_START) S->cmds[i].slot.p->refs--;
  S->cmdN = 0;
  CUNLOCK();
  return true;
}

bool simRealtime() {
#ifdef SIM
  return simLiveSound();
#else
  return true;
#endif
}

uint16_t keyOf(const char* kind) {
  uint16_t h = 7;
  for (int i = 0; i < 16 && kind[i]; i++) h = h * 31 + (uint8_t)kind[i];
  return h ? h : 1;
}

void loadCues(const char* pkgId) {
  if (S->cueId == pkgId) return;
  heap_caps_free(S->cueFile);
  S->cueFile = nullptr, S->cues = nullptr, S->ncues = 0, S->cueId = pkgId;
  pkg::File f;
  if (!pkg::open(pkgId, "sfx.cue", f) || f.size() < 8 || !(S->cueFile = pkg::loadAll(f))) return;
  const uint16_t n = S->cueFile[4] | S->cueFile[5] << 8;
  if (memcmp(S->cueFile, "PBC1", 4) || 8 + n * sizeof(Cue) > f.size()) {
    trace::add("sfx: %s: not a cue table", pkgId);
    return;
  }
  S->cues = (const Cue*)(S->cueFile + 8), S->ncues = n;
}

// ------------------------------------------------------------------ decoding (a task on the board)
OpusDecoder* decoder() {
  int err = 0;
  std::lock_guard<std::mutex> lk(voice::opusLock());  // lib/opus's scratch is one for all tasks
  OpusDecoder* d = opus_decoder_create(RATE, 1, &err);  // ~18 KB, PSRAM
  return err == OPUS_OK ? d : nullptr;
}

void freePcm(Pcm& p) {
  heap_caps_free(p.s);
  S->cacheBytes -= p.n * 2;
  p.s = nullptr, p.clip = nullptr, p.n = 0;
}

int decodeTo(OpusDecoder* d, const uint8_t* p, int len, int16_t* pcm) {
  std::lock_guard<std::mutex> lk(voice::opusLock());
  return opus_decode(d, p, len, pcm, 960, 0);
}
void reset(OpusDecoder* d) {
  std::lock_guard<std::mutex> lk(voice::opusLock());
  opus_decoder_ctl(d, OPUS_RESET_STATE);
}

// An effect decoded whole into the cache (the oldest unused ones go when it is full).
Pcm* decodeAll(const pk::Pba& pack, const pba_clip_t* l, uint32_t g) {
  for (Pcm& p : S->pcms)
    if (p.clip == l && p.s && p.gen == g) return p.used = ++S->useClock, &p;
  if (!S->fxDec && !(S->fxDec = decoder())) return nullptr;
  const uint32_t need = l->samples * 2;
  while (S->cacheBytes + need > cacheMax) {
    Pcm* old = nullptr;
    for (Pcm& p : S->pcms)
      if (p.s && p.refs == 0 && (!old || p.used < old->used)) old = &p;
    if (!old) break;
    freePcm(*old);
  }
  Pcm* to = nullptr;
  for (Pcm& p : S->pcms)
    if (!p.s && p.refs == 0) to = &p;
  if (!to) return nullptr;
  int16_t* s = (int16_t*)psram(need);
  uint8_t* buf = s && !pack.mapped() ? (uint8_t*)psram(l->bytes) : nullptr;
  const uint8_t* p = s ? pack.packets(l, buf) : nullptr;
  if (!p) {
    heap_caps_free(s), heap_caps_free(buf);
    return nullptr;
  }
  const uint8_t* end = p + l->bytes;
  int16_t* pcm = S->pcm;  // (not on the task's stack: opus needs most of it)
  reset(S->fxDec);
  uint32_t skip = pack.head()->pre_skip, got = 0;
  for (int k = 0; k < l->packets && got < l->samples && p + 2 <= end; k++) {
    const int len = p[0] | p[1] << 8;
    if (p + 2 + len > end) break;
    const int m = decodeTo(S->fxDec, p + 2, len, pcm);
    p += 2 + len;
    if (m <= 0) break;
    const uint32_t from = min((uint32_t)m, skip);
    skip -= from;
    const uint32_t c = min((uint32_t)m - from, l->samples - got);
    memcpy(s + got, pcm + from, c * 2);
    got += c;
  }
  heap_caps_free(buf);
  if (got < l->samples) memset(s + got, 0, (l->samples - got) * 2);
  to->clip = l, to->s = s, to->n = l->samples, to->used = ++S->useClock, to->gen = g;
  S->cacheBytes += need;
  return to;
}

// A stream: its packets read 4 KB at a time and decoded into its ring; at its end, again from where it loops.
void fill(Stream& m) {
  int16_t* pcm = S->pcm;  // (not on the task's stack: opus needs most of it)
  while (m.w - m.r + 960 <= RING) {
    if (!m.left) {
      if (m.part == 0 && m.src.seg[1].bytes) m.part = 1;  // the start was played: the looping part from now on
      reset(m.dec);
      m.pos = 0, m.chunkN = 0, m.skip = m.src.pre, m.left = m.src.seg[m.part].samples;
      if (!m.left) return;
    }
    const Seg& sg = m.src.seg[m.part];
    const uint32_t in = m.pos - m.chunkAt;
    if (m.pos < m.chunkAt || m.pos + 2 > m.chunkAt + m.chunkN ||
        m.pos + 2 + (m.chunk[in] | m.chunk[in + 1] << 8) > m.chunkAt + m.chunkN) {
      if (m.pos + 2 > sg.bytes) {
        m.left = 0;
        continue;
      }
      m.chunkAt = m.pos, m.chunkN = min<uint32_t>(4096, sg.bytes - m.pos);
      if (!m.src.f.read(sg.at + m.pos, m.chunk, m.chunkN)) return;  // the card went: silence
    }
    const uint8_t* p = m.chunk + (m.pos - m.chunkAt);
    const int len = p[0] | p[1] << 8;
    const int got = m.pos + 2 + len <= m.chunkAt + m.chunkN ? decodeTo(m.dec, p + 2, len, pcm) : -1;
    m.pos += 2 + len;
    if (got <= 0) {
      m.left = 0;
      continue;
    }
    const uint32_t from = min((uint32_t)got, m.skip);
    m.skip -= from;
    const uint32_t c = min((uint32_t)got - from, m.left);
    for (uint32_t i = 0; i < c; i++) m.ring[(m.w + i) % RING] = pcm[from + i];
    std::atomic_thread_fence(std::memory_order_release);
    m.w = m.w + c, m.left -= c;
  }
}

void freeStream(Stream& m) {
  if (m.dec) opus_decoder_destroy(m.dec);  // a plain free (no scratch)
  heap_caps_free(m.chunk), heap_caps_free(m.ring);
  m.dec = nullptr, m.chunk = nullptr, m.ring = nullptr, m.src = Src();
  m.st = 0;
}

void startStream(const Job& j) {
  for (Stream& m : S->streams) {
    if (m.st != 0) continue;
    m.src = j.src;
    m.dec = decoder();
    m.chunk = (uint8_t*)psram(4096);
    m.ring = (int16_t*)psram(RING * 2);
    if (!m.dec || !m.chunk || !m.ring) return freeStream(m);
    m.part = 0, m.chunkAt = m.chunkN = m.pos = 0, m.skip = m.src.pre, m.left = m.src.seg[0].samples, m.w = m.r = 0;
    m.role = j.stream - 1, m.id = j.id, m.gain = j.slot.gain, m.env = 0, m.envStep = j.slot.envStep;
    m.st = 1;
    fill(m);
    m.st = 2;
    return;
  }
}

void ensureLater() {  // the task gave the speaker something (not while all is paused: the assistant may have it)
  if (!fxPaused || !musPaused) ensure();
}

// One round of the task: the sounds asked for, then the streams topped up. true while a stream plays.
bool work() {
  static uint32_t swept = 0;
  if (swept != gen) {  // other packages now (the run over the pet, another dog): their decoded sounds go
    swept = gen;
    for (Pcm& p : S->pcms)
      if (p.s && p.refs == 0 && p.gen != swept) freePcm(p);
  }
  for (;;) {
    Job j;
    {
      std::lock_guard<std::mutex> l(S->jMx);
      if (!S->jobN) break;
      j = S->jobs[0];
      for (int i = 1; i < S->jobN; i++) S->jobs[i - 1] = S->jobs[i];
      S->jobs[--S->jobN] = Job();
      working = true;
    }
    if (j.gen == gen) {
      if (j.stream) {
        startStream(j);
        ensureLater();
      } else if (Pcm* p = decodeAll(*j.pack, j.clip, j.gen)) {
        p->refs++;
        CLOCK();
        Cmd* c = push();
        if (c) c->t = C_START, c->slot = j.slot, c->slot.p = p;
        else p->refs--;
        CUNLOCK();
        ensureLater();
      }
    }
    working = false;
  }
  bool any = false;
  for (Stream& m : S->streams) {
    if (m.st == 3) freeStream(m);
    if (m.st == 2) fill(m), any = true;
  }
  return any;
}

#ifndef SIM
void fxTask(void*) {
  for (;;) {
    const bool streaming = work();
    ulTaskNotifyTake(pdTRUE, streaming ? pdMS_TO_TICKS(20) : portMAX_DELAY);
  }
}
#endif

void kick() {
#ifndef SIM
  // Core 0 beside the sound and voice tasks; the stack in PSRAM (opus keeps its scratch on the heap).
  if (!task && xTaskCreatePinnedToCoreWithCaps(fxTask, "petfx", 10240, nullptr, 3, &task, 0, MALLOC_CAP_SPIRAM) != pdPASS)
    task = nullptr;
  if (task) xTaskNotifyGive(task);
#else
  if (!simRealtime()) return (void)work();
  if (!task) {
    task = true;
    std::thread([] {
      for (;;) work(), std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }).detach();
  }
#endif
}

void queue(const Job& j) {
  {
    std::lock_guard<std::mutex> l(S->jMx);
    if (S->jobN >= NJOB) return;
    S->jobs[S->jobN++] = j;
  }
  kick();
}

// ------------------------------------------------------------------ when sounds may play
bool fxOn = true, musOn = true, shown = false, ownerRun = false, quietCache = false, homeLooked = false;
String lastWorld;  // the map whose ambience goes on at home (the phone game never stops it)
const char* wantTune = nullptr;  // the place's tune (a type 2 cue), else the map's
uint16_t streamId = 0;
uint32_t quietAt = 0;

bool quiet() {
  if (!shown || talk::busy() || pet::summoned()) return true;  // the assistant hears the room: silent, not softer
  if (millis() - quietAt > 500) {  // the clock and the settings: twice a second is enough
    quietAt = millis();
    quietCache = pet::quietNow() || desk::settingInt("s3.volume", 45) <= 0;
  }
  return quietCache;
}

const pba_clip_t* find(const char* kind, const pk::Pba** pack) {
  for (int b : {B_DOG, B_PET, B_RUN}) {
    if (!*kind || !S->banks[b].ok()) continue;
    if (const pba_clip_t* l = voice::of(S->banks[b], kind)) {
      *pack = &S->banks[b];
      return l;
    }
  }
  return nullptr;
}

// The same kind again: not within its cooldown, nor within 25 ms (the phone game's retrigger limit).
bool cooled(const char* kind, uint16_t cdMs) {
  const uint16_t k = keyOf(kind);
  const uint32_t now = millis();
  Cd* slot = nullptr;
  for (Cd& c : S->cds) {
    if (c.key == k) {
      if ((int32_t)(now - c.until) < 0) return false;
      slot = &c;
      break;
    }
    if (!slot && (int32_t)(now - c.until) >= 0) slot = &c;
  }
  if (!slot) slot = &S->cds[now % NCD];
  slot->key = k, slot->until = now + max<uint16_t>(cdMs, 25);
  return true;
}

bool start(const Cue& q) {
  if ((q.pr && (int)random(100) >= q.pr) || deaf()) return false;
  const pk::Pba* pack = nullptr;
  const pba_clip_t* l = find(q.kind, &pack);
  const bool later = q.type == 1 && q.t;  // an event's later sound (the pour's ticks): its own time, no limit
  if (!l || (!later && !cooled(q.kind, q.cd))) {  // the phone game plays its list's next name instead
    if (!*q.alt || !(l = find(q.alt, &pack)) || !cooled(q.alt, 0)) return false;
  }
  if (q.loop && !simRealtime()) return false;  // the script simulator drains the speaker at once: no loops
#ifdef SIM
  trace::add("sfx: %.15s %+d dB (%.15s)", l->name, q.db, q.name);
#endif
  Job j;
  j.pack = pack, j.clip = l, j.gen = gen;
  j.slot.gain = gainOf(q.db);
  const int p = q.p0 + (q.p1 > q.p0 ? (int)random(q.p1 - q.p0 + 1) : 0);
  j.slot.step = (uint32_t)((1 << FR) * p / 100);
  if (q.type == 1) j.slot.wait = (uint32_t)q.t * (RATE / 1000);
  if (q.loop) j.slot.key = keyOf(q.kind), j.slot.env = 0, j.slot.envStep = perSample((q.loop - 1) / 20.0f);
  queue(j);
  return true;
}

const Cue* cueOf(uint8_t type, const char* name) {
  for (int i = 0; i < S->ncues; i++)
    if (S->cues[i].type == type && !strncmp(S->cues[i].name, name, 16)) return &S->cues[i];
  return nullptr;
}

// A tune of a voice file: the clip `kind` (its start, or all of it) and `kind`_l (the part that loops), if any.
bool srcOf(const pk::Pba& p, const char* kind, Src& s) {
  char lp[20];
  snprintf(lp, sizeof lp, "%s_l", kind);
  bool any = false;
  for (int i = 0; i < p.count(); i++) {
    const pba_clip_t* l = p.at(i);
    const int k = !strncmp(l->on, kind, 16) ? 0 : !strncmp(l->on, lp, 16) ? 1 : -1;
    if (k < 0) continue;
    s.seg[k].at = p.head()->data_off + l->off, s.seg[k].bytes = l->bytes, s.seg[k].samples = l->samples;
    any |= k == 0;
  }
  if (any) s.f = p.file(), s.pre = p.head()->pre_skip;
  return any;
}

// A tune (role 0) or an ambience (1) from a voice file, crossfaded; nothing found: what played fades out.
void stream(int role, const pk::Pba* p, const char* kind, const Cue* q, float fade) {
  Job j;
  if (!p || !srcOf(*p, kind, j.src)) return post(C_FADE, role, 0, perSample(fade));
  j.gen = gen, j.stream = role + 1, j.id = ++streamId;
  j.slot.gain = gainOf((q ? q->db : 0) + BED_DB), j.slot.envStep = perSample(fade);
  post(C_FADE, role, j.id, perSample(fade));
  queue(j);
}

// The tune and the ambience wanted now, started when they may play (and only then: nothing decoded for nobody).
void applyMusic() {
  // No touch for 30 s: the person may be across the room and call "Gâu gâu" (a tune masks the call): the tune
  // and the ambience fade until the next touch.
  const bool q = quiet() || !simRealtime() || os::idleMs() > IDLE_BED_MS;
  const char* map = S->wantWorld.c_str();
  const char* tune = q || !musOn ? "" : wantTune ? wantTune : map;
  const char* amb = q || !fxOn ? "" : map;
  char key[48];  // "@home" (a place's tune) or the map's package
  snprintf(key, sizeof key, "%s%s", wantTune && *tune ? "@" : "", tune);
  if (S->playingTune != key) {
    const bool gate = !wantTune && S->playingTune.length() && S->playingTune[0] != '@';  // map to map: 2 s
    S->playingTune = key;
    if (!*tune) {
      post(C_FADE, 0, 0, perSample(1.0f));
    } else if (wantTune) {
      const Cue* c = cueOf(2, tune);
      const pk::Pba* b = !c ? nullptr : S->banks[B_PET].ok() ? &S->banks[B_PET] : &S->banks[B_RUN];
      stream(0, b, c ? c->kind : "", c, 1.0f);
    } else {
      pk::Pba p;
      stream(0, p.load(tune, "music.pba") ? &p : nullptr, "music", cueOf(2, "world"), gate ? 2.0f : 1.0f);
    }
  }
  if (S->playingAmb != amb) {
    S->playingAmb = amb;
    pk::Pba p;
    stream(1, *amb && p.load(amb, "music.pba") ? &p : nullptr, "amb", cueOf(2, "amb"), 1.5f);
  }
}

void stopLoop(uint16_t key, float fade) {
  if (key) post(C_STOP, 0, key, perSample(fade));
}

}  // namespace

// ------------------------------------------------------------------ the games' side
void open(const char* dogPkg, bool run) {
  if (!made()) return;
  auto load = [](int b, const char* id) {
    if (S->bankId[b] == id) return;
    gen++;
    S->banks[b].free();
    S->bankId[b] = id;
    if (*id && S->banks[b].load(id, "sfx.pba")) trace::add("sfx: %s: %d sounds", id, S->banks[b].count());
  };
  if (run != ownerRun) wantTune = nullptr, S->wantWorld = "";
  const uint32_t g0 = gen;
  ownerRun = run;
  cacheMax = run ? CACHE_RUN : CACHE_PET;
  load(B_DOG, dogPkg ? dogPkg : "");
  load(B_PET, "vn.pawboop.pet");
  load(B_RUN, run ? "vn.pawboop.run" : "");
  loadCues(run ? "vn.pawboop.run" : "vn.pawboop.pet");
  fxOn = game::s().sfx, musOn = game::s().music;
  if (gen != g0 && S->cacheBytes) kick();  // the task frees what the packages before decoded
}

void close(bool run) {
  if (!S || run != ownerRun) return;
  gen++;
  {
    std::lock_guard<std::mutex> l(S->jMx);
    S->jobN = 0;
  }
  CLOCK();  // what waits for the sound task is stale now: only the stop (a queue never left full)
  for (int i = 0; i < S->cmdN; i++)
    if (S->cmds[i].t == C_START) S->cmds[i].slot.p->refs--;
  S->cmdN = 0;
  if (Cmd* c = push()) c->t = C_STOPALL;
  CUNLOCK();
  S->playingTune = S->playingAmb = S->wantWorld = "", wantTune = nullptr;
  S->clipCues = nullptr, S->clipN = 0;
  memset(S->clipLoop, 0, sizeof S->clipLoop);
  if (running) ensure();  // the sound task lets go of the slots at its next pull
  for (int k = 0; k < 10 && (working || (running && millis() - lastPull < 100)); k++) delay(10);
  for (Pcm& p : S->pcms)
    if (p.s && p.refs == 0) freePcm(p);  // PSRAM back for the next app
  for (int b = 0; b < 3; b++) S->banks[b].free(), S->bankId[b] = "";
  heap_caps_free(S->cueFile), S->cueFile = nullptr, S->cues = nullptr, S->ncues = 0, S->cueId = "";
  shown = homeLooked = false;
}

void voice(sound::Pull pull, void* ctx) {
  if (!made()) return;
  CLOCK();
  if (Cmd* c = push()) c->t = C_VOICE, c->pull = pull, c->ctx = ctx;
  CUNLOCK();
  if (pull) ensure();
}

void clip(const char* name) {
  if (!S) return;
  const Cue* first = nullptr;
  int n = 0;
  for (int i = 0; i < S->ncues; i++)
    if (S->cues[i].type == 0 && !strncmp(S->cues[i].name, name, 16)) {
      if (!first) first = &S->cues[i];
      n++;
    }
  for (uint16_t& k : S->clipLoop) {  // the loops of the clip before end, unless this one has them too
    bool keep = false;
    for (int i = 0; i < n; i++) keep |= first[i].loop && keyOf(first[i].kind) == k;
    if (!k || keep) continue;
    float out = 0.2f;
    for (int i = 0; i < S->ncues; i++)
      if (S->cues[i].loop && keyOf(S->cues[i].kind) == k) out = S->cues[i].out / 20.0f;
    stopLoop(k, out);
    k = 0;
  }
  S->clipCues = first, S->clipN = n, S->clipPass = 0, S->lastPic = -1;
}

void picture(int i) {
  if (!S || !S->clipCues) return;
  if (i <= S->lastPic) S->clipPass++;  // the clip began again
  const int from = i <= S->lastPic ? 0 : (S->lastPic + 1) * 100, to = (i + 1) * 100;  // ms, 10 pictures a second
  S->lastPic = i;
  if (!fxOn || quiet()) return;
  for (int k = 0; k < S->clipN; k++) {
    const Cue& q = S->clipCues[k];
    if (q.t < from || q.t >= to || (q.once && S->clipPass)) continue;
    if (q.loop) {
      const uint16_t key = keyOf(q.kind);
      uint16_t* free = nullptr;
      bool on = false;
      for (uint16_t& c : S->clipLoop) on |= c == key, free = !c && !free ? &c : free;
      if (on || !free) continue;
      *free = key;
    }
    start(q);
  }
}

bool event(const char* name, bool on) {
  if (!S) return false;
  bool any = false;
  for (int i = 0; i < S->ncues; i++) {
    const Cue& q = S->cues[i];
    if (q.type != 1 || strncmp(q.name, name, 16)) continue;
    if (q.loop && !on) stopLoop(keyOf(q.kind), q.out / 20.0f);
    else if (fxOn && !quiet()) any |= start(q);
  }
  return any;
}

void loopPitch(const char* name, float pitch) {
  if (const Cue* q = S ? cueOf(1, name) : nullptr) post(C_PITCH, 0, keyOf(q->kind), (int32_t)((1 << FR) * pitch));
}

void music(const char* name) {
  wantTune = name;
  if (S) applyMusic();
}

void world(const char* mapPkg) {
  if (!S) return;
  S->wantWorld = mapPkg ? mapPkg : "";
  if (S->wantWorld.length()) lastWorld = S->wantWorld;
  applyMusic();
}

void home() {
  if (!S) return;
  wantTune = "home";
  if (!lastWorld.length() && !homeLooked) {  // as at the phone game's start: the world the run starts in
    homeLooked = true;
    Preferences p;
    p.begin("run", true);
    const String m = p.getString("map", "");
    p.end();
    for (const pkg::Package& k : pkg::list())
      if (k.available && (k.id == "vn.pawboop.map." + m || (!m.length() && k.id.startsWith("vn.pawboop.map.")))) {
        lastWorld = k.id;
        break;
      }
  }
  if (S->wantWorld != lastWorld) S->wantWorld = lastWorld;
  applyMusic();
}

// The bus is what the speaker plays (pulled in the last 100 ms: not the assistant's own stream, not a chime) and
// it carried no voice and no effect for ms.
bool bedOnly(uint32_t ms) {
  const uint32_t now = millis();
  return now - lastPull < 100 && now - lastLoud >= ms;
}

void frame(bool run, bool onScreen) {
  if (!S || run != ownerRun) return;
  shown = onScreen;
  const bool q = quiet();
  fxPaused = q || !fxOn;
  musPaused = q || !musOn;
  applyMusic();
  if (q || (!fxOn && !musOn)) return;
  bool any = S->cmdN > 0;
  for (const Stream& m : S->streams) any |= m.st == 2;
  for (const Slot& s : S->slots) any |= s.p != nullptr;
  if (any) ensure();
}

bool effectsOn() { return game::s().sfx; }
bool musicOn() { return game::s().music; }
void setEffects(bool on) {
  fxOn = game::s().sfx = on;
  if (!on && S)
    for (uint16_t& k : S->clipLoop)
      if (k) stopLoop(k, 0.2f), k = 0;
  if (S) applyMusic();
  game::saveSoon();
}
void setMusic(bool on) {
  musOn = game::s().music = on;
  if (S) applyMusic();
  game::saveSoon();
}

}  // namespace sfx
}  // namespace pet
