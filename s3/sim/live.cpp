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
#include <vector>

#include "desk.h"
#include "realsize.h"
#include "sound.h"

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
int vol = 45;
std::vector<int16_t> tones, voice, clickWave;  // what plays next (filled under the device lock)
size_t toneAt = 0, voiceAt = 0;
int clickAt = -1;
float gainNow = 1;

// As applyVolume(): 0 % mutes; 1..100 % = DAC register 0x83..0xD7, 0.5 dB a step, 0xBF = 0 dB. The loudest
// (+12 dB) is the computer's full scale.
float gainFor(int v) {
  if (v <= 0) return 0;
  const int reg = 0x83 + v * (0xD7 - 0x83) / 100;
  return powf(10.0f, ((reg - 0xBF) * 0.5f - 12.0f) / 20.0f);
}

void callback(void*, Uint8* bytes, int len) {
  int16_t* out = (int16_t*)bytes;
  const int n = len / 2;
  for (int i = 0; i < n; i++) {
    int v = 0;
    if (toneAt < tones.size()) v += tones[toneAt++];
    else if (!tones.empty()) tones.clear(), toneAt = 0, gainNow = gainFor(vol);  // after an alarm at 70 %
    if (voiceAt < voice.size()) v += voice[voiceAt++];
    if (clickAt >= 0) {
      v += clickWave[clickAt++];
      if (clickAt >= (int)clickWave.size()) clickAt = -1;
    }
    out[i] = (int16_t)std::max(-32768, std::min(32767, (int)(v * gainNow)));
  }
}

bool allowed() { return dev && vol > 0 && !desk::settingInt("s3.dnd", 0); }

void queueTone(std::vector<int16_t>&& t, float gain) {
  SDL_LockAudioDevice(dev);
  tones = std::move(t), toneAt = 0;
  voice.clear(), voiceAt = 0;  // chime, test and alarm stop the stream
  gainNow = gain;
  SDL_UnlockAudioDevice(dev);
}

void note(std::vector<int16_t>& o, int n, float (*wave)(int, int, float), float hz) {
  for (int k = 0; k < n; k++) o.push_back((int16_t)wave(k, n, hz));
}
}  // namespace

bool begin() {
  SDL_AudioSpec want = {}, got = {};
  want.freq = RATE, want.format = AUDIO_S16SYS, want.channels = 1, want.samples = 320, want.callback = callback;
  dev = SDL_OpenAudioDevice(nullptr, 0, &want, &got, 0);
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
  SDL_PauseAudioDevice(dev, 0);
  return true;
}

void setVolume(int percent) {
  vol = std::max(0, std::min(100, percent));
  if (!dev) return;
  SDL_LockAudioDevice(dev);
  gainNow = gainFor(vol);
  SDL_UnlockAudioDevice(dev);
}

void click() {
  if (!allowed() || !desk::settingInt("s3.click", 1)) return;
  SDL_LockAudioDevice(dev);
  clickAt = 0;
  SDL_UnlockAudioDevice(dev);
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

// The simulator's pet decodes a whole line before it streams (src/pet/voice.cpp): taken in one go here.
void stream(Pull pull, void* ctx) {
  if (!dev || !pull) return;
  std::vector<int16_t> v;
  short buf[320];
  for (int n; (n = pull(buf, 320, ctx)) > 0;) v.insert(v.end(), buf, buf + n);
  SDL_LockAudioDevice(dev);
  voice = std::move(v), voiceAt = 0;
  SDL_UnlockAudioDevice(dev);
}

void stopStream() {
  if (!dev) return;
  SDL_LockAudioDevice(dev);
  voice.clear(), voiceAt = 0;
  SDL_UnlockAudioDevice(dev);
}

bool streaming() {
  if (!dev) return false;
  SDL_LockAudioDevice(dev);
  const bool s = voiceAt < voice.size();
  SDL_UnlockAudioDevice(dev);
  return s;
}
}  // namespace sound
