// The live simulator's window, input and speaker on a computer (SDL2).
//   The window: the 320x480 panel (RGB565 as the panel gets it) at the board's real size on this screen
//     (sim/realsize.h: 49 x 73 mm, following the display the window is on), or enlarged (Z, or --zoom).
//   The finger: the mouse's left button. Keys: arrows = a swipe in the middle of the screen (the run game),
//     H = the home swipe from the bottom edge, B or Backspace = the back swipe from the left edge,
//     S = a screenshot (sim/shots), M = memory, Z = real size / enlarged, Q = quit.
//   The speaker: the board's sound.cpp at 16 kHz mono, the same click, chime, test tune and alarm, the same
//     volume curve (the codec's DAC register), Do Not Disturb and the click setting; streams (Biscuit's voice).
#include "live.h"

#include <SDL.h>
#include <zlib.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <vector>

#include "desk.h"
#include "realsize.h"
#include "sound.h"
#include "cues.h"
#include "mic.h"
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>

namespace live {
bool on = false;

namespace {
SDL_Window* win = nullptr;
SDL_Renderer* ren = nullptr;
SDL_Texture* tex = nullptr;
int texW = 0, texH = 0;
float zoom = 1;      // window points per panel pixel
bool realSize = true;  // zoom follows the board's real size on the display the window is on (else bigZoom)
float bigZoom = 2;
const int MARGIN = 14;  // points of dark frame around the panel (the board's black glass edge)
bool fingerDown = false;
std::string frameShotPath;  // the next present() saves the window here

void fitWindow(int w, int h) {
  SDL_SetWindowSize(win, (int)(w * zoom + 0.5f) + 2 * MARGIN, (int)(h * zoom + 0.5f) + 2 * MARGIN);
}

// The zoom for the window's display now (real size), or the enlarged one; the window follows.
void rezoom() {
  float z = bigZoom;
  if (realSize) {
    z = realZoom(SDL_GetWindowDisplayIndex(win));
    if (z <= 0) z = bigZoom;  // the display's size is unknown
  }
  if (fabsf(z - zoom) < 0.001f) return;
  zoom = z;
  fitWindow(texW ? texW : 320, texH ? texH : 480);
}

float pickZoom(int w, int h) {
  SDL_Rect r;
  if (SDL_GetDisplayUsableBounds(0, &r) != 0) return 1;
  // Points per pixel; on a Retina screen 1.5 points = 3 device pixels, still whole pixels.
  for (float z : {2.0f, 1.5f, 1.0f})
    if (h * z + 2 * MARGIN + 40 <= r.h && w * z + 2 * MARGIN <= r.w) return z;
  return 1;
}
}  // namespace

bool frameShot(const std::string& path);

bool begin(int w, int h, float z) {
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) != 0) {
    fprintf(stderr, "[sim] SDL: %s\n", SDL_GetError());
    return false;
  }
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");  // nearest: the panel's pixels stay square
  SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
  bigZoom = pickZoom(w, h);
  realSize = z <= 0;
  zoom = z > 0 ? z : bigZoom;
  win = SDL_CreateWindow("EspOS", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, (int)(w * zoom) + 2 * MARGIN,
                         (int)(h * zoom) + 2 * MARGIN, SDL_WINDOW_ALLOW_HIGHDPI);
  if (!win) return fprintf(stderr, "[sim] SDL window: %s\n", SDL_GetError()), false;
  ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
  if (!ren) return fprintf(stderr, "[sim] SDL renderer: %s\n", SDL_GetError()), false;
  if (realSize) {
    rezoom();
    SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    fprintf(stderr, "[sim] real size: %.2f points a pixel (%.0f x %.0f mm), Z = enlarged\n", zoom, 320 * PANEL_MM_PER_PX,
            480 * PANEL_MM_PER_PX);
  }
  on = true;
  SDL_ShowWindow(win);
  SDL_RaiseWindow(win);  // in front of the terminal that started it
  sound::begin();
  return true;
}

void end() {
  if (tex) SDL_DestroyTexture(tex);
  if (ren) SDL_DestroyRenderer(ren);
  if (win) SDL_DestroyWindow(win);
  SDL_Quit();
  on = false;
}

uint32_t ms() { return SDL_GetTicks(); }
void sleepMs(int n) { SDL_Delay(n); }
void setTitle(const std::string& t) { SDL_SetWindowTitle(win, t.c_str()); }
void shootWindow(const std::string& path) { frameShotPath = path; }

std::atomic<uint32_t> ledRgb{0};  // the RGB LED's colour as the board would light it (src/led.cpp)
std::atomic<bool> ledNew{false};
bool ledChanged() { return ledNew.exchange(false); }

// The LED: a dot in the frame above the panel's top-right corner, with a soft halo (it is small on the board too).
void drawLed(float s) {
  const uint32_t c = ledRgb;
  const int r = c >> 16 & 255, g = c >> 8 & 255, b = c & 255;
  const float cx = (MARGIN + texW * zoom - 12) * s, cy = (MARGIN / 2.0f) * s, rad = 4.0f * s;
  SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
  // the bare LED when dark: a faint grey bead
  const float lit = std::max(r, std::max(g, b)) / 255.0f;
  const float k = lit > 0 ? std::min(1.0f, 0.35f + lit * 2.5f) : 0;  // WS2812s look bright even when dimmed
  for (int dy = -(int)(rad * 3); dy <= (int)(rad * 3); dy++)
    for (int dx = -(int)(rad * 3); dx <= (int)(rad * 3); dx++) {
      const float d = sqrtf((float)(dx * dx + dy * dy));
      int a;
      uint8_t cr, cg, cb;
      if (d <= rad) {
        a = 255;
        cr = (uint8_t)std::min(255.0f, 40 + r * k * 255.0f / std::max(1, std::max(r, std::max(g, b))));
        cg = (uint8_t)std::min(255.0f, 40 + g * k * 255.0f / std::max(1, std::max(r, std::max(g, b))));
        cb = (uint8_t)std::min(255.0f, 44 + b * k * 255.0f / std::max(1, std::max(r, std::max(g, b))));
      } else if (k > 0 && d <= rad * 3) {
        a = (int)(110 * k * (1 - (d - rad) / (rad * 2)) * (1 - (d - rad) / (rad * 2)));
        const int m = std::max(1, std::max(r, std::max(g, b)));
        cr = r * 255 / m, cg = g * 255 / m, cb = b * 255 / m;
      } else {
        continue;
      }
      SDL_SetRenderDrawColor(ren, cr, cg, cb, (uint8_t)a);
      SDL_RenderDrawPoint(ren, (int)cx + dx, (int)cy + dy);
    }
  SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
}

void present(const uint16_t* fb, int w, int h) {
  if (!tex || texW != w || texH != h) {
    if (tex) SDL_DestroyTexture(tex);
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, w, h);
    texW = w, texH = h;
    fitWindow(w, h);
  }
  SDL_UpdateTexture(tex, nullptr, fb, w * 2);
  int ww, wh, ow, oh;
  SDL_GetWindowSize(win, &ww, &wh);
  SDL_GetRendererOutputSize(ren, &ow, &oh);
  const float s = ww ? (float)ow / ww : 1;  // device pixels per point
  // Whole device pixels a panel pixel: square pixels (nearest); real size is in between: smoothed, as glass shows it.
  const float dev = zoom * s;
  SDL_SetTextureScaleMode(tex, fabsf(dev - roundf(dev)) < 0.01f ? SDL_ScaleModeNearest : SDL_ScaleModeLinear);
  SDL_SetRenderDrawColor(ren, 0x15, 0x17, 0x1c, 255);
  SDL_RenderClear(ren);
  SDL_Rect frame = {(int)(MARGIN * s / 2), (int)(MARGIN * s / 2), (int)(ow - MARGIN * s), (int)(oh - MARGIN * s)};
  SDL_SetRenderDrawColor(ren, 0x05, 0x05, 0x07, 255);  // the glass around the panel
  SDL_RenderFillRect(ren, &frame);
  SDL_Rect dst = {(int)(MARGIN * s), (int)(MARGIN * s), (int)(w * zoom * s + 0.5f), (int)(h * zoom * s + 0.5f)};
  SDL_RenderCopy(ren, tex, nullptr, &dst);
  drawLed(s);
  if (!frameShotPath.empty()) {
    fprintf(stderr, frameShot(frameShotPath) ? "[sim] window %s\n" : "[sim] no window picture %s\n", frameShotPath.c_str());
    frameShotPath.clear();
  }
  SDL_RenderPresent(ren);
}

bool events(Finger& f, std::vector<std::string>& cmds) {
  SDL_Event e;
  bool open = true;
  auto toPanel = [&](int mx, int my) {
    f.x = std::max(0, std::min(texW - 1, (int)((mx - MARGIN) / zoom)));
    f.y = std::max(0, std::min(texH - 1, (int)((my - MARGIN) / zoom)));
  };
  auto inside = [&](int mx, int my) {
    return mx >= MARGIN && my >= MARGIN && mx < MARGIN + texW * zoom && my < MARGIN + texH * zoom;
  };
  while (SDL_PollEvent(&e)) {
    switch (e.type) {
      case SDL_QUIT:
        open = false;
        break;
      case SDL_WINDOWEVENT:  // moved to another display: its real size there
        if (e.window.event == SDL_WINDOWEVENT_DISPLAY_CHANGED || e.window.event == SDL_WINDOWEVENT_MOVED) rezoom();
        break;
      case SDL_MOUSEBUTTONDOWN:
        if (e.button.button == SDL_BUTTON_LEFT && inside(e.button.x, e.button.y)) {
          fingerDown = true, f.changed = true, f.down = true;
          toPanel(e.button.x, e.button.y);
        }
        break;
      case SDL_MOUSEMOTION:
        if (fingerDown) f.changed = true, f.down = true, toPanel(e.motion.x, e.motion.y);
        break;
      case SDL_MOUSEBUTTONUP:
        if (e.button.button == SDL_BUTTON_LEFT && fingerDown) {
          fingerDown = false, f.changed = true, f.down = false;
          toPanel(e.button.x, e.button.y);
        }
        break;
      case SDL_KEYDOWN:
        if (e.key.repeat) break;
        switch (e.key.keysym.sym) {
          case SDLK_LEFT: cmds.push_back("swipe left"); break;
          case SDLK_RIGHT: cmds.push_back("swipe right"); break;
          case SDLK_UP: cmds.push_back("swipe up"); break;
          case SDLK_DOWN: cmds.push_back("swipe down"); break;
          case SDLK_h: cmds.push_back("homeswipe"); break;
          case SDLK_b:
          case SDLK_BACKSPACE: cmds.push_back("backswipe"); break;
          case SDLK_s: cmds.push_back("shot"); break;
          case SDLK_m: cmds.push_back("mem"); break;
          case SDLK_z: realSize = !realSize, rezoom(); break;
          case SDLK_q: open = false; break;
          default: break;
        }
        break;
      default:
        break;
    }
  }
  return open;
}

// ------------------------------------------------------------------ PNG (screenshots)
bool frameShot(const std::string& path) {  // the whole window as drawn (frame and screen), for checking it
  int ow, oh;
  if (!ren || SDL_GetRendererOutputSize(ren, &ow, &oh) != 0) return false;
  std::vector<uint8_t> rgb((size_t)ow * oh * 3);
  if (SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_RGB24, rgb.data(), ow * 3) != 0) return false;
  std::vector<uint16_t> px((size_t)ow * oh);
  for (size_t i = 0; i < px.size(); i++)
    px[i] = (uint16_t)((rgb[3 * i] >> 3) << 11 | (rgb[3 * i + 1] >> 2) << 5 | rgb[3 * i + 2] >> 3);
  return savePng(path, px.data(), ow, oh);
}

bool savePng(const std::string& path, const uint16_t* fb, int w, int h) {
  std::vector<uint8_t> raw;
  raw.reserve((size_t)(w * 3 + 1) * h);
  for (int y = 0; y < h; y++) {
    raw.push_back(0);
    for (int x = 0; x < w; x++) {
      const uint16_t v = fb[y * w + x];
      raw.push_back((v >> 11) * 255 / 31), raw.push_back((v >> 5 & 63) * 255 / 63), raw.push_back((v & 31) * 255 / 31);
    }
  }
  uLongf zn = compressBound(raw.size());
  std::vector<uint8_t> z(zn);
  if (compress2(z.data(), &zn, raw.data(), raw.size(), 6) != Z_OK) return false;
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) return false;
  auto be = [](uint8_t* p, uint32_t v) { p[0] = v >> 24, p[1] = v >> 16, p[2] = v >> 8, p[3] = v; };
  auto chunk = [&](const char* type, const uint8_t* d, uint32_t n) {
    uint8_t b[8];
    be(b, n), memcpy(b + 4, type, 4);
    fwrite(b, 1, 8, f);
    if (n) fwrite(d, 1, n, f);
    uLong c = crc32(0, (const Bytef*)type, 4);
    if (n) c = crc32(c, d, n);
    be(b, (uint32_t)c);
    fwrite(b, 1, 4, f);
  };
  static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
  fwrite(sig, 1, 8, f);
  uint8_t ihdr[13];
  be(ihdr, w), be(ihdr + 4, h);
  ihdr[8] = 8, ihdr[9] = 2, ihdr[10] = ihdr[11] = ihdr[12] = 0;  // 8-bit RGB
  chunk("IHDR", ihdr, 13);
  chunk("IDAT", z.data(), (uint32_t)zn);
  chunk("IEND", nullptr, 0);
  fclose(f);
  return true;
}
}  // namespace live

// ------------------------------------------------------------------ the speaker (src/sound.cpp on a computer)
namespace sound {
namespace {
constexpr int RATE = 16000;
SDL_AudioDeviceID dev = 0;
// SIM_MUTE=1: no speaker at all; the same callback is paced by the clock, 20 ms at a time as the board's sound task
// (SDL's dummy driver runs slower than real time), so SIM_WAV records the right timing without a sound.
bool muted = false;
std::mutex muteMx;
void lockDev() { muted ? muteMx.lock() : SDL_LockAudioDevice(dev); }
void unlockDev() { muted ? muteMx.unlock() : SDL_UnlockAudioDevice(dev); }
int vol = 45;
std::vector<int16_t> tones, voice, clickWave;  // what plays next (filled under the device lock)
size_t toneAt = 0, voiceAt = 0;
Pull sPull = nullptr;  // a stream pulled as it plays, 20 ms at a time like the board's sound task (the AI's voice)
void* sCtx = nullptr;
std::atomic<int64_t> lastOutMs{-100000};  // when the speaker last played something (the wake word's gate)
int64_t nowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
int clickAt = -1;
float gainNow = 1;

// As applyVolume(): 0 % mutes; 1..100 % = DAC register 0x83..0xD7, 0.5 dB a step, 0xBF = 0 dB. The loudest
// (+12 dB) is the computer's full scale.
float gainFor(int v) {
  if (v <= 0) return 0;
  const int reg = 0x83 + v * (0xD7 - 0x83) / 100;
  return powf(10.0f, ((reg - 0xBF) * 0.5f - 12.0f) / 20.0f);
}

// SIM_ECHO: the speaker's samples as they are handed to SDL, each chunk stamped with the wall clock when it came,
// so the mic thread can take what was playing 25 ms before its own block (the board's speaker-to-mic path).
constexpr int ECHO_RING = 1 << 17;  // 8 s
int16_t echoRing[ECHO_RING];
std::atomic<int64_t> echoIdx{0}, echoStart{0}, echoAtUs{0};  // samples written; the last chunk's first; when it came
int64_t nowUs() {
  return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
void echoPut(const int16_t* out, int n) {
  const int64_t i0 = echoIdx;
  for (int i = 0; i < n; i++) echoRing[(i0 + i) & (ECHO_RING - 1)] = out[i];
  echoStart = i0, echoAtUs = nowUs();
  echoIdx = i0 + n;
}

void callback(void*, Uint8* bytes, int len) {
  int16_t* out = (int16_t*)bytes;
  const int n = len / 2;
  static short pulled[8192];  // (the dummy driver asks for more than 320 at a time)
  int have = 0;
  if (sPull && n <= 8192) {
    have = sPull(pulled, n, sCtx);
    if (have <= 0) sPull = nullptr, have = 0;  // it ended
  }
  bool loud = false;
  for (int i = 0; i < n; i++) {
    int v = i < have ? pulled[i] : 0;
    if (toneAt < tones.size()) v += tones[toneAt++];
    else if (!tones.empty()) tones.clear(), toneAt = 0, gainNow = gainFor(vol);  // after an alarm at 70 %
    if (voiceAt < voice.size()) v += voice[voiceAt++];
    if (clickAt >= 0) {
      v += clickWave[clickAt++];
      if (clickAt >= (int)clickWave.size()) clickAt = -1;
    }
    out[i] = (int16_t)std::max(-32768, std::min(32767, (int)(v * gainNow)));
    if (i < 8192) pulled[i] = (short)std::max(-32768, std::min(32767, v));  // before the volume, for SIM_WAV
    loud = loud || out[i] > 64 || out[i] < -64;
  }
  // SIM_WAV=path: what the speaker plays, as it plays (16 kHz mono s16le, before the volume), to check timing.
  static FILE* wav = getenv("SIM_WAV") ? fopen(getenv("SIM_WAV"), "wb") : nullptr;
  if (wav) fwrite(pulled, 2, std::min(n, 8192), wav);
  if (loud || sPull) lastOutMs = nowMs();
  echoPut(out, n);
}

bool allowed() { return dev && vol > 0 && !desk::settingInt("s3.dnd", 0); }

void queueTone(std::vector<int16_t>&& t, float gain) {
  lockDev();
  tones = std::move(t), toneAt = 0;
  voice.clear(), voiceAt = 0;  // chime, test and alarm stop the stream
  gainNow = gain;
  unlockDev();
}

void note(std::vector<int16_t>& o, int n, float (*wave)(int, int, float), float hz) {
  for (int k = 0; k < n; k++) o.push_back((int16_t)wave(k, n, hz));
}
}  // namespace

bool begin() {
  SDL_AudioSpec want = {}, got = {};
  want.freq = RATE, want.format = AUDIO_S16SYS, want.channels = 1, want.samples = 320, want.callback = callback;
  muted = getenv("SIM_MUTE") != nullptr;
  dev = muted ? 1 : SDL_OpenAudioDevice(nullptr, 0, &want, &got, 0);
  if (!dev) {
    fprintf(stderr, "[sim] no speaker: %s\n", SDL_GetError());
    return false;
  }
  uint32_t seed = 12345;  // the 9 ms tick of play(CLICK)
  for (int i = 0; i < RATE * 9 / 1000; i++) {
    const float env = expf(-i / 22.0f);
    seed = seed * 1103515245 + 12345;
    const float noise = ((int)(seed >> 16 & 0x7fff) - 16384) / 16384.0f;
    clickWave.push_back((int16_t)((sinf(2 * M_PI * 2800 * i / RATE) * 0.7f + noise * 0.3f) * env * 14000));
  }
  gainNow = gainFor(vol);
  if (muted) {
    std::thread([] {
      static int16_t b[320];
      auto next = std::chrono::steady_clock::now();
      for (;;) {
        next += std::chrono::milliseconds(20);
        std::this_thread::sleep_until(next);
        std::lock_guard<std::mutex> l(muteMx);
        callback(nullptr, (Uint8*)b, sizeof b);
      }
    }).detach();
    return true;
  }
  SDL_PauseAudioDevice(dev, 0);
  return true;
}

void setVolume(int percent) {
  vol = std::max(0, std::min(100, percent));
  if (!dev) return;
  lockDev();
  gainNow = gainFor(vol);
  unlockDev();
}

void click() {
  if (!allowed() || !desk::settingInt("s3.click", 1)) return;
  lockDev();
  clickAt = 0;
  unlockDev();
}

void chime() {
  if (!allowed()) return;
  std::vector<int16_t> t;
  for (float hz : {1046.5f, 1568.0f})
    note(t, RATE * 120 / 1000, [](int k, int, float f) {
      return sinf(2 * M_PI * f * k / RATE) * std::min(1.0f, k / 80.0f) * expf(-k / 900.0f) * 16000;
    }, hz);
  queueTone(std::move(t), gainFor(vol));
}

void blip() {
  if (!dev || vol <= 0) return;
  std::vector<int16_t> t(cues::length(cues::LISTEN, RATE));  // the board's cue, sample for sample (src/cues.h)
  cues::render(cues::LISTEN, RATE, t.data());
  lockDev();
  tones = std::move(t), toneAt = 0;  // over the stream, not instead of it
  gainNow = gainFor(vol);
  unlockDev();
}

void blipEnd() {
  if (!dev || vol <= 0) return;
  std::vector<int16_t> t(cues::length(cues::SENT, RATE));  // the board's cue, sample for sample (src/cues.h)
  cues::render(cues::SENT, RATE, t.data());
  lockDev();
  tones = std::move(t), toneAt = 0;
  gainNow = gainFor(vol);
  unlockDev();
}

void test() {
  if (!dev) return;
  std::vector<int16_t> t;
  const float notes[][2] = {{523.3f, 180}, {659.3f, 180}, {784.0f, 180}, {1046.5f, 360}, {784.0f, 180}, {1046.5f, 420}};
  for (auto& nt : notes)
    note(t, RATE * (int)nt[1] / 1000, [](int k, int n, float f) {
      const float env = std::min(1.0f, k / 120.0f) * std::min(1.0f, (n - k) / 400.0f), ph = 2 * M_PI * f * k / RATE;
      return (sinf(ph) * 0.8f + sinf(2 * ph) * 0.2f) * env * 18000;
    }, nt[0]);
  queueTone(std::move(t), gainFor(vol));
}

void alarmBeep() {
  if (!dev) return;
  std::vector<int16_t> t;
  const int n = RATE * 90 / 1000, gap = RATE * 70 / 1000;
  for (int b = 0; b < 4; b++)
    for (int k = 0; k < n + gap; k++) {
      const float env = k < n ? std::min(1.0f, k / 40.0f) * std::min(1.0f, (n - k) / 40.0f) : 0;
      t.push_back((int16_t)((sinf(2 * M_PI * 2000 * k / RATE) * 0.8f + sinf(2 * M_PI * 4000 * k / RATE) * 0.2f) * env * 20000));
    }
  queueTone(std::move(t), gainFor(std::max(vol, 70)));  // never quieter than 70 %
}

// Pulled from the audio callback as it plays (the board's sound task does it every 20 ms): a pet line decoded
// beforehand, or the AI's voice arriving from the network (silence while it waits).
void stream(Pull pull, void* ctx) {
  if (!dev) return;
  lockDev();
  sPull = pull, sCtx = ctx;
  voice.clear(), voiceAt = 0;
  unlockDev();
}

void stopStream() { stream(nullptr, nullptr); }

bool outputSince(uint32_t ms) { return nowMs() - lastOutMs < (int64_t)ms; }

bool streaming() {
  if (!dev) return false;
  lockDev();
  const bool s = sPull != nullptr || voiceAt < voice.size();
  unlockDev();
  return s;
}
}  // namespace sound

// The mic's block plus what the speaker played 25 ms before (SIM_ECHO dB: 0 = the board's level, the dog's voice
// at an average -30..-35 dBFS at the mic) and the room's tail (a 50 ms loop at 0.5: ~0.5 s to fade 60 dB).
void simEcho(int16_t* b, int n) {
  static const char* e = getenv("SIM_ECHO");
  if (!e) return;
  static const float g = powf(10.0f, (float)atof(e) / 20.0f);
  static float tail[800];  // the room: the last 50 ms of what came back
  static int ti = 0;
  static int64_t wantIdx = -1;
  const int64_t idx = sound::echoIdx, start = sound::echoStart, at = sound::echoAtUs;
  // Playing now: the last chunk's first sample came at `at`, the rest follow at 16 kHz; 25 ms later it is heard.
  const int64_t playing = std::min(idx, start + (sound::nowUs() - at) * 16 / 1000);
  const int64_t from = playing - 400 - n;
  if (wantIdx < 0 || llabs(from - wantIdx) > 3200) wantIdx = from;  // a steady read position (no jumps)
  for (int i = 0; i < n; i++, wantIdx++) {
    const float sp = wantIdx >= 0 && wantIdx < idx && idx - wantIdx < sound::ECHO_RING
                         ? sound::echoRing[wantIdx & (sound::ECHO_RING - 1)] * g : 0;
    const float v = sp + 0.5f * tail[ti];
    tail[ti] = v, ti = (ti + 1) % 800;
    b[i] = (int16_t)std::max(-32768.0f, std::min(32767.0f, b[i] + v));
  }
}


void simLed(uint8_t r, uint8_t g, uint8_t b) {
  live::ledRgb = (uint32_t)r << 16 | (uint32_t)g << 8 | b;
  live::ledNew = true;
}

// SIM_MIC=live: the computer's own microphone (SDL capture, 16 kHz mono) instead of files (sim/stubs.cpp calls this).
namespace {
SDL_AudioDeviceID micDev = 0;
std::atomic<mic::Sink> micSink{nullptr};
int16_t micBlock[160];
int micFill = 0;
int micPeak = 0, micBlocks = 0;
// SIM_MIC=live keeps ONLY the moments around the wake word (privacy: never the room non-stop,
// never plain conversation): 6 s before and 4 s after each call, or a near miss that was a real try (a score of 0.5
// or more), longer while events follow each other (30 s at most), as FLAC in
// $SIM_MIC_REC/win-<time>-<call|near>-<score>.flac, only when SIM_MIC_REC names a folder.
constexpr float NEAR_BAR = 0.5f;
constexpr int REC_RATE = 16000, PRE = 6 * REC_RATE, POST = 4 * REC_RATE, WIN_MAX = 30 * REC_RATE;
bool recOn = false;
std::mutex recMu;
std::vector<int16_t> recRing(PRE);       // the last 6 s
size_t recPos = 0;                       // samples written to the ring, ever
std::vector<int16_t> recWin;             // the window being kept
size_t recUntil = 0;                     // keep appending up to this sample count (recPos)
std::string recName;

void recFlush(std::vector<int16_t> pcm, std::string name) {  // a thread: ffmpeg writes the FLAC (lossless)
  std::thread([pcm = std::move(pcm), name] {
    const std::string dir = std::string(getenv("SIM_MIC_REC")) + "/";
    const std::string cmd = "ffmpeg -v error -y -f s16le -ar 16000 -ac 1 -i - -compression_level 8 '" + dir +
                            name + ".flac'";
    FILE* f = popen(cmd.c_str(), "w");
    if (!f) return;
    fwrite(pcm.data(), 2, pcm.size(), f);
    pclose(f);
    fprintf(stderr, "[sim] kept %.1f s around the wake word: %s.flac\n", pcm.size() / (float)REC_RATE, name.c_str());
  }).detach();
}

void recFeed(const int16_t* in, int n) {
  std::lock_guard<std::mutex> g(recMu);
  for (int i = 0; i < n; i++) {
    recRing[recPos % PRE] = in[i];
    recPos++;
    if (recWin.empty()) continue;
    recWin.push_back(in[i]);
    if (recPos >= recUntil || recWin.size() >= (size_t)WIN_MAX) {  // 4 s after the last event (30 s at most)
      recFlush(std::move(recWin), recName);
      recWin.clear(), recUntil = 0;
    }
  }
}
}  // namespace

void simWakeEvent(const char* kind, float score) {
  if (!recOn || (strcmp(kind, "call") && score < NEAR_BAR)) return;
  std::lock_guard<std::mutex> g(recMu);
  if (recWin.empty()) {  // a new window: the last 6 s first
    const size_t have = std::min(recPos, (size_t)PRE);
    recWin.reserve(PRE + POST);
    for (size_t k = recPos - have; k < recPos; k++) recWin.push_back(recRing[k % PRE]);
    char b[96];
    snprintf(b, sizeof b, "win-%ld-%s-%.2f", (long)time(nullptr), kind, score);
    recName = b;
  } else if (!strcmp(kind, "call") && recName.find("-call-") == std::string::npos) {
    char b[96];  // a near miss that became a call: the window is named after the call
    snprintf(b, sizeof b, "win-%ld-call-%.2f", (long)time(nullptr), score);
    recName = b;
  }
  recUntil = recPos + POST;
}

namespace {
void micCallback(void*, Uint8* bytes, int len) {
  const int16_t* in = (const int16_t*)bytes;
  if (recOn) recFeed(in, len / 2);
  for (int i = 0; i < len / 2; i++) {
    micPeak = std::max(micPeak, std::abs((int)in[i]));
    if (++micBlocks >= 16000 * 10) {  // every 10 s: is the microphone hearing anything (permission, input device)?
      fprintf(stderr, "[sim] mic peak %.0f dBFS in the last 10 s\n", micPeak ? 20 * log10f(micPeak / 32768.0f) : -96.0f);
      micPeak = 0, micBlocks = 0;
    }
    micBlock[micFill++] = in[i];
    if (micFill == 160) {
      micFill = 0;
      const mic::Sink k = micSink;
      if (k) k(micBlock, 160);
    }
  }
}
}  // namespace
bool simLiveSound() { return true; }  // PawBoop's loops and music play here (sfx.cpp)

bool simLiveMic(mic::Sink sink, bool on) {
  micSink = on ? sink : nullptr;
  if (!micDev) {
    SDL_AudioSpec want = {}, got = {};
    want.freq = 16000, want.format = AUDIO_S16SYS, want.channels = 1, want.samples = 160, want.callback = micCallback;
    // The system's default input, by its real name; never a phone (an "iPhone Microphone" offered through
    // Continuity): then the Mac's own microphone, which is what a board on the desk would hear.
    char* defName = nullptr;
    SDL_AudioSpec defSpec;
    if (SDL_GetDefaultAudioInfo(&defName, &defSpec, 1) != 0) defName = nullptr;
    std::string name = defName ? defName : "";
    if (defName) SDL_free(defName);
    if (name.empty() || name.find("iPhone") != std::string::npos) {
      std::string pick;
      for (int i = 0; i < SDL_GetNumAudioDevices(1); i++) {
        const char* n = SDL_GetAudioDeviceName(i, 1);
        if (!n || strstr(n, "iPhone")) continue;
        if (strstr(n, "MacBook") || strstr(n, "Built-in")) { pick = n; break; }
        if (pick.empty() && strstr(n, "Microphone")) pick = n;
      }
      if (!pick.empty()) name = pick;
    }
    micDev = SDL_OpenAudioDevice(name.empty() ? nullptr : name.c_str(), 1, &want, &got, 0);
    if (!micDev) return fprintf(stderr, "[sim] no microphone: %s\n", SDL_GetError()), false;
    if (getenv("SIM_MIC_REC") && *getenv("SIM_MIC_REC") && strcmp(getenv("SIM_MIC_REC"), "0")) {  // a folder
      recOn = true;
      fprintf(stderr, "[sim] keeping only ~10 s of the microphone around each call or near miss of %.1f+ (SIM_MIC_REC/win-*.flac)\n", NEAR_BAR);
    }
    fprintf(stderr, "[sim] microphone: %s, %d Hz\n", name.empty() ? "the system's default input" : name.c_str(), got.freq);
  }
  SDL_PauseAudioDevice(micDev, on ? 0 : 1);
  return true;
}
