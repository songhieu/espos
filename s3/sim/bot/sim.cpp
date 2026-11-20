// EspOS Bot simulator: the board's own code (src/bot: app, talk, ui, link, ear, spk, store, usb...) on the computer.
//   - a window with the 128x64 OLED (black glass, a little glow, the panel's brightness) and the BOOT button:
//     Space or a click (a short press talks; 5 s held forgets the WiFi), S saves a picture, Q quits;
//   - the computer's microphone into the same front end and "Gâu" network (src/wake), its speaker for the voice and
//     the cues (src/bot/spk.cpp's ring);
//   - this terminal is the USB serial line (ESPOS:HELLO / INFO / PROVISION / CLAIM / FACTORY_RESET and the console);
//   - the setup page at http://localhost:8099 (net_sim.cpp); the server is tools/bot/fixture_server.py unless
//     BOT_SIM_LIVE=1 (live AI calls: only with an OK).
//   - --headless SCRIPT: no window, no sound devices; the script's key presses, waits, audio files and serial lines
//     in, pictures (.frames, made into PNG/GIF by tools/bot/shots.py) and the speaker's sound (speaker.wav) out.
// tools/bot/sim.sh builds and starts it (docs/bot.md).
#include <SDL.h>
#include <unistd.h>
#include <zlib.h>

#include <Arduino.h>
#include <Preferences.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <deque>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "../../src/bot/app.h"
#include "../../src/bot/config.h"
#include "../../src/bot/ear.h"
#include "../../src/bot/gfx.h"
#include "../../src/bot/net.h"
#include "../../src/bot/oled.h"
#include "../../src/bot/spk.h"
#include "../../src/bot/talk.h"
#include "../../src/bot/ui.h"
#include "../../src/trace.h"

// ---------------------------------------------------------------- Arduino on the computer
namespace {
const auto t0 = std::chrono::steady_clock::now();
std::atomic<bool> bootDown{false}, volUpDown{false}, volDownDown{false};
std::mutex serMu;
std::deque<char> serIn;
std::atomic<bool> quit{false};
}  // namespace

uint32_t millis() {
  return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
}
uint32_t micros() {
  return (uint32_t)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
}
void delay(uint32_t ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
long random(long m) { return m ? rand() % m : 0; }
long random(long a, long b) { return a + random(b - a); }
void pinMode(int, int) {}
int digitalRead(int pin) {
  if (pin == bot::pins::BOOT) return bootDown ? LOW : HIGH;
  if (pin == bot::pins::VOL_UP) return volUpDown ? LOW : HIGH;
  if (pin == bot::pins::VOL_DOWN) return volDownDown ? LOW : HIGH;
  return HIGH;
}
SerialClass Serial;
EspClass ESP;
int SerialClass::available() {
  std::lock_guard<std::mutex> l(serMu);
  return (int)serIn.size();
}
int SerialClass::read() {
  std::lock_guard<std::mutex> l(serMu);
  if (serIn.empty()) return -1;
  const char c = serIn.front();
  serIn.pop_front();
  return (unsigned char)c;
}
void EspClass::restart() {
  fprintf(stderr, "[bot-sim] restart: the simulator ends (start it again)\n");
  quit = true;
}
static void serialLine(const std::string& l) {
  std::lock_guard<std::mutex> g(serMu);
  for (char c : l) serIn.push_back(c);
  serIn.push_back('\n');
}

namespace bot {
// BOT_SIM_TIME=HH:MM: the clock starts at that local time on 2026-11-20 (time zone +7, as the bot's default) and runs
// on from there; otherwise the computer's clock.
int64_t simClockMs() {
  static int64_t fake = -1;
  if (fake < 0) {
    fake = 0;
    if (const char* t = getenv("BOT_SIM_TIME")) {
      int h = 0, m = 0;
      if (sscanf(t, "%d:%d", &h, &m) == 2) fake = (1795107600LL + h * 3600 + m * 60) * 1000;  // 1795107600: 2026-11-20 00:00 at +7
    }
  }
  if (fake) return fake + millis();
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

static std::vector<uint8_t> modelBytes;
const uint8_t* simModel(uint32_t& size) {
  const char* p = getenv("BOT_SIM_MODEL");
  std::ifstream f(p ? p : "wake/pkg/dev.espos.wake.gau/gau.ewm", std::ios::binary);
  modelBytes.assign(std::istreambuf_iterator<char>(f), {});
  size = (uint32_t)modelBytes.size();
  if (!size) fprintf(stderr, "[bot-sim] no wake model (wake/pkg/dev.espos.wake.gau/gau.ewm): BOOT only\n");
  return size ? modelBytes.data() : nullptr;
}

// ---------------------------------------------------------------- the OLED (src/bot/oled.h)
namespace oled {
static uint8_t shown[1024];
static int bright = 60;
static bool on = true;
static uint32_t pushes = 0;
bool begin() { return true; }
bool ok() { return true; }
void push(const uint8_t* fb) { memcpy(shown, fb, sizeof shown), pushes++; }
void pushAll(const uint8_t* fb) { push(fb); }
void brightness(int pct) { bright = pct; }
void power(bool o) { on = o; }
uint32_t errors() { return 0; }
}  // namespace oled
}  // namespace bot

// ---------------------------------------------------------------- pictures
namespace {
std::string outDir = "sim/bot/out";

void putBE(std::string& s, uint32_t v) {
  for (int i = 3; i >= 0; i--) s += (char)(v >> (i * 8));
}
void chunk(std::string& png, const char* type, const std::string& data) {
  putBE(png, (uint32_t)data.size());
  std::string td = std::string(type, 4) + data;
  png += td;
  putBE(png, (uint32_t)crc32(0, (const Bytef*)td.data(), (uInt)td.size()));
}
// The panel as a PNG at 3x (the colours tools/bot/shots.py uses).
bool savePng(const std::string& path, const uint8_t* fb) {
  const int S = 3, W = 128 * S, H = 64 * S;
  std::string raw;
  for (int y = 0; y < H; y++) {
    raw += '\0';
    for (int x = 0; x < W; x++) {
      const int px = x / S, py = y / S;
      const bool lit = fb[(py >> 3) * 128 + px] >> (py & 7) & 1;
      raw += (char)(lit ? 236 : 8), raw += (char)(lit ? 244 : 9), raw += (char)(lit ? 255 : 12);
    }
  }
  uLongf zl = compressBound(raw.size());
  std::string z(zl, '\0');
  compress2((Bytef*)&z[0], &zl, (const Bytef*)raw.data(), raw.size(), 6);
  z.resize(zl);
  std::string png = "\x89PNG\r\n\x1a\n", ih;
  putBE(ih, W), putBE(ih, H);
  ih += (char)8, ih += (char)2, ih += '\0', ih += '\0', ih += '\0';
  chunk(png, "IHDR", ih), chunk(png, "IDAT", z), chunk(png, "IEND", "");
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) return false;
  fwrite(png.data(), 1, png.size(), f);
  fclose(f);
  return true;
}

// The window as it looks (S, or the script's "window NAME"): RGB rows from the renderer.
bool saveWindowPng(const std::string& path, SDL_Renderer* r) {
  int w = 0, h = 0;
  SDL_GetRendererOutputSize(r, &w, &h);
  std::vector<uint8_t> px((size_t)w * h * 3);
  if (SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_RGB24, px.data(), w * 3)) return false;
  std::string raw;
  for (int y = 0; y < h; y++) raw += '\0', raw.append((const char*)px.data() + (size_t)y * w * 3, (size_t)w * 3);
  uLongf zl = compressBound(raw.size());
  std::string z(zl, '\0');
  compress2((Bytef*)&z[0], &zl, (const Bytef*)raw.data(), raw.size(), 6);
  z.resize(zl);
  std::string png = "\x89PNG\r\n\x1a\n", ih;
  putBE(ih, w), putBE(ih, h);
  ih += (char)8, ih += (char)2, ih += '\0', ih += '\0', ih += '\0';
  chunk(png, "IHDR", ih), chunk(png, "IDAT", z), chunk(png, "IEND", "");
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) return false;
  fwrite(png.data(), 1, png.size(), f);
  fclose(f);
  return true;
}

// A GIF being recorded (tools/bot/shots.py's .frames format: "<n> <ms>\n" then n pictures of 1024 bytes).
struct Rec {
  std::string name;
  uint32_t until = 0, last = 0;
  std::vector<uint8_t> frames;
  int n = 0;
};
std::vector<Rec> recs;
void recStep() {
  const uint32_t now = millis();
  for (auto it = recs.begin(); it != recs.end();) {
    if (now - it->last >= 40) {
      it->frames.insert(it->frames.end(), bot::oled::shown, bot::oled::shown + 1024), it->n++, it->last = now;
    }
    if ((int32_t)(now - it->until) >= 0) {
      FILE* f = fopen((outDir + "/" + it->name + ".frames").c_str(), "wb");
      if (f) fprintf(f, "%d 40\n", it->n), fwrite(it->frames.data(), 1, it->frames.size(), f), fclose(f);
      it = recs.erase(it);
    } else {
      ++it;
    }
  }
}

// ---------------------------------------------------------------- sound
std::mutex micMu;
std::vector<int16_t> sayPcm;  // a script's "say" file being played into the mic
size_t sayPos = 0;
int16_t micBlock[160];
int micFill = 0;
std::vector<int16_t> spkOut;  // headless: what the speaker played (speaker.wav)

void micSamples(const int16_t* in, int n) {
  for (int i = 0; i < n; i++) {
    int32_t v = in ? in[i] : 0;
    {
      std::lock_guard<std::mutex> l(micMu);
      if (sayPos < sayPcm.size()) v += sayPcm[sayPos++];
    }
    micBlock[micFill++] = (int16_t)std::max(-32767, std::min(32767, (int)v));
    if (micFill == 160) bot::ear::feed(micBlock), micFill = 0;
  }
}
void micCallback(void*, Uint8* b, int len) { micSamples((const int16_t*)b, len / 2); }
void spkCallback(void*, Uint8* b, int len) { bot::spk::pull((int16_t*)b, len / 2); }

bool readWav16k(const std::string& path, std::vector<int16_t>& pcm) {
  SDL_AudioSpec spec;
  Uint8* buf = nullptr;
  Uint32 len = 0;
  if (!SDL_LoadWAV(path.c_str(), &spec, &buf, &len)) return false;
  SDL_AudioCVT cvt;
  SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq, AUDIO_S16SYS, 1, 16000);
  cvt.len = (int)len;
  std::vector<Uint8> work(len * (cvt.len_mult > 0 ? cvt.len_mult : 1) + 16);
  memcpy(work.data(), buf, len);
  cvt.buf = work.data();
  if (cvt.needed) SDL_ConvertAudio(&cvt);
  const int out = cvt.needed ? cvt.len_cvt : (int)len;
  pcm.assign((int16_t*)work.data(), (int16_t*)work.data() + out / 2);
  SDL_FreeWAV(buf);
  return true;
}

void writeWav(const std::string& path, const std::vector<int16_t>& pcm) {
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) return;
  const uint32_t data = (uint32_t)pcm.size() * 2, rate = 16000, br = 32000, fmt = 16, riff = 36 + data;
  const uint16_t one = 1, ba = 2, bits = 16;
  fwrite("RIFF", 1, 4, f), fwrite(&riff, 4, 1, f), fwrite("WAVEfmt ", 1, 8, f), fwrite(&fmt, 4, 1, f);
  fwrite(&one, 2, 1, f), fwrite(&one, 2, 1, f), fwrite(&rate, 4, 1, f), fwrite(&br, 4, 1, f), fwrite(&ba, 2, 1, f);
  fwrite(&bits, 2, 1, f), fwrite("data", 1, 4, f), fwrite(&data, 4, 1, f), fwrite(pcm.data(), 2, pcm.size(), f);
  fclose(f);
}

// ---------------------------------------------------------------- the script
std::vector<std::string> script;
size_t scriptAt = 0;
uint32_t scriptWaitUntil = 0, holdUntil = 0;
std::atomic<bool>* holdKey = &bootDown;
int fails = 0;

const char* stateName(bot::ui::State s) {
  static const char* N[] = {"standby", "night", "listen", "think", "talk", "happy", "sad", "setup_ap", "claim", "alarm", "note"};
  return N[(int)s];
}

SDL_Renderer* ren = nullptr;
void render(const std::string& capture);

// One step of the script when its time has come; false when it has ended.
bool scriptStep() {
  const uint32_t now = millis();
  if (holdUntil && (int32_t)(now - holdUntil) >= 0) *holdKey = false, holdUntil = 0, holdKey = &bootDown;
  if (scriptWaitUntil && (int32_t)(now - scriptWaitUntil) < 0) return true;
  scriptWaitUntil = 0;
  while (scriptAt < script.size()) {
    std::string line = script[scriptAt++];
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    for (size_t p; (p = line.find("${")) != std::string::npos;) {  // ${NAME}: the environment's value
      const size_t e = line.find('}', p);
      if (e == std::string::npos) break;
      const char* v = getenv(line.substr(p + 2, e - p - 2).c_str());
      line.replace(p, e - p + 1, v ? v : "");
    }
    if (line.empty() || line[0] == '#') continue;
    std::istringstream in(line);
    std::string cmd, rest;
    in >> cmd;
    std::getline(in, rest);
    if (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
    fprintf(stderr, "[script %4.1fs] %s\n", now / 1000.0, line.c_str());
    if (cmd == "wait") {
      scriptWaitUntil = now + (uint32_t)atoi(rest.c_str());
      return true;
    } else if (cmd == "volup" || cmd == "voldown") {  // a press of a volume switch
      (cmd == "volup" ? volUpDown : volDownDown) = true;
      holdKey = cmd == "volup" ? &volUpDown : &volDownDown, holdUntil = now + 120;
      scriptWaitUntil = now + 200;
      return true;
    } else if (cmd == "press") {
      bootDown = true, holdUntil = now + 120;
      scriptWaitUntil = now + 300;
      return true;
    } else if (cmd == "hold") {
      bootDown = true, holdUntil = now + (uint32_t)atoi(rest.c_str());
      scriptWaitUntil = holdUntil + 200;
      return true;
    } else if (cmd == "say") {
      std::vector<int16_t> pcm;
      if (!readWav16k(rest, pcm)) fprintf(stderr, "[bot-sim] can't read %s\n", rest.c_str()), fails++;
      std::lock_guard<std::mutex> l(micMu);
      sayPcm = pcm, sayPos = 0;
    } else if (cmd == "serial") {
      serialLine(rest);
    } else if (cmd == "shot") {
      savePng(outDir + "/" + rest + ".png", bot::oled::shown);
      FILE* f = fopen((outDir + "/" + rest + ".frames").c_str(), "wb");
      if (f) fprintf(f, "1 40\n"), fwrite(bot::oled::shown, 1, 1024, f), fclose(f);
    } else if (cmd == "window") {  // the whole window as it looks (not headless)
      if (ren) render(outDir + "/" + rest + ".png");
    } else if (cmd == "gif") {
      std::istringstream r(rest);
      Rec rec;
      uint32_t ms = 3000;
      r >> rec.name >> ms;
      rec.until = now + ms, rec.last = now - 40;
      recs.push_back(rec);
    } else if (cmd == "expect") {  // expect ui <state> | expect talk active|idle
      std::istringstream r(rest);
      std::string what, want;
      r >> what >> want;
      std::string got = what == "ui"       ? stateName(bot::ui::state())
                        : what == "talk"   ? (bot::talk::active() ? "active" : "idle")
                        : what == "portal" ? (bot::net::portal() ? "on" : "off")
                        : what == "online" ? (bot::net::online() ? "yes" : "no")
                                           : "?";
      if (got != want) {
        fails++;
        fprintf(stderr, "[bot-sim] FAIL line %zu: expected %s %s, got %s\n", scriptAt, what.c_str(), want.c_str(), got.c_str());
      }
    } else if (cmd == "curl") {  // a phone or the web, in the background: curl's own arguments
      system(("curl -s -m 10 " + rest + " >/dev/null 2>&1 &").c_str());
    } else if (cmd == "page") {  // the setup page's text as a phone would see it now, to the terminal
      system("curl -s -m 5 http://localhost:${BOT_SIM_PORTAL_PORT:-8099}/status | sed -e 's/<[^>]*>/ /g' | "
             "grep -o 'Đang kết nối WiFi…\\|Sai mật khẩu WiFi\\|Không thấy mạng WiFi\\|Đã kết nối!\\|Chọn WiFi cho máy' "
             "| head -1 | sed 's/^/[page] /' >&2 &");
    } else if (cmd == "quit") {  // the simulator ends (otherwise a window stays, by hand)
      scriptAt = script.size();
      quit = true;
    } else {
      fprintf(stderr, "[bot-sim] unknown script line: %s\n", line.c_str());
      fails++;
    }
  }
  return !recs.empty() || scriptWaitUntil;
}

// ---------------------------------------------------------------- the window
SDL_Window* win = nullptr;
SDL_Texture *texCrisp = nullptr, *texGlow = nullptr;
int zoom = 6;
SDL_Rect panel, button;
const int M = 28;  // the bezel around the panel

void layout() {
  panel = {M, M, 128 * zoom, 64 * zoom};
  const int bs = 54;
  button = {M + panel.w / 2 - bs / 2, M + panel.h + 26, bs, bs};
  SDL_SetWindowSize(win, panel.w + 2 * M, panel.h + 2 * M + 26 + bs + 10);
}

void fillCircle(int cx, int cy, int r) {
  for (int dy = -r; dy <= r; dy++) {
    const int dx = (int)sqrt((double)(r * r - dy * dy));
    SDL_RenderDrawLine(ren, cx - dx, cy + dy, cx + dx, cy + dy);
  }
}

void render(const std::string& capture) {
  static uint32_t px[128 * 64];
  const float b = bot::oled::on ? 0.75f + 0.25f * bot::oled::bright / 100.0f : 0;  // the panel's contrast
  const uint8_t r = (uint8_t)(238 * b), g = (uint8_t)(245 * b), bl = (uint8_t)(255 * b);
  for (int y = 0; y < 64; y++)
    for (int x = 0; x < 128; x++)
      px[y * 128 + x] = bot::oled::shown[(y >> 3) * 128 + x] >> (y & 7) & 1 ? (0xFFu << 24 | r << 16 | g << 8 | bl) : 0;
  SDL_UpdateTexture(texCrisp, nullptr, px, 128 * 4);
  SDL_UpdateTexture(texGlow, nullptr, px, 128 * 4);
  SDL_SetRenderDrawColor(ren, 24, 26, 31, 255);  // the desk
  SDL_RenderClear(ren);
  SDL_SetRenderDrawColor(ren, 4, 4, 6, 255);  // the module's black frame
  SDL_Rect frame = {M / 3, M / 3, panel.w + 2 * M - 2 * (M / 3), panel.h + 2 * M - 2 * (M / 3)};
  SDL_RenderFillRect(ren, &frame);
  SDL_SetRenderDrawColor(ren, 7, 9, 13, 255);  // the glass
  SDL_RenderFillRect(ren, &panel);
  // the glow: the same picture, blurred by the linear scaling from a smaller copy, added faintly
  SDL_SetTextureBlendMode(texGlow, SDL_BLENDMODE_ADD);
  SDL_SetTextureAlphaMod(texGlow, 60);
  SDL_Rect gr = {panel.x - zoom, panel.y - zoom, panel.w + 2 * zoom, panel.h + 2 * zoom};
  SDL_RenderCopy(ren, texGlow, nullptr, &gr);
  // the pixels, with the thin dark lines between them a real panel has
  SDL_SetTextureBlendMode(texCrisp, SDL_BLENDMODE_BLEND);
  SDL_RenderCopy(ren, texCrisp, nullptr, &panel);
  if (zoom >= 4) {
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, 7, 9, 13, 60);
    for (int x = 0; x <= 128; x++) SDL_RenderDrawLine(ren, panel.x + x * zoom, panel.y, panel.x + x * zoom, panel.y + panel.h);
    for (int y = 0; y <= 64; y++) SDL_RenderDrawLine(ren, panel.x, panel.y + y * zoom, panel.x + panel.w, panel.y + y * zoom);
  }
  // BOOT
  SDL_SetRenderDrawColor(ren, 52, 56, 64, 255);
  fillCircle(button.x + button.w / 2, button.y + button.h / 2, button.w / 2);
  SDL_SetRenderDrawColor(ren, bootDown ? 70 : 120, bootDown ? 75 : 128, bootDown ? 85 : 140, 255);
  fillCircle(button.x + button.w / 2, button.y + button.h / 2 + (bootDown ? 1 : 0), button.w / 2 - 6);
  if (!capture.empty() && saveWindowPng(capture, ren)) fprintf(stderr, "[bot-sim] %s\n", capture.c_str());
  SDL_RenderPresent(ren);
}

void usage() {
  fprintf(stderr,
          "bot-sim [--headless SCRIPT] [--script SCRIPT] [--zoom N] [--out DIR]\n"
          "  window: Space or the round button = BOOT (hold 5 s: forget the WiFi), S = picture, Q = quit\n"
          "  terminal: the USB lines (ESPOS:INFO, ESPOS:PROVISION {...}, HELP for the console)\n");
}

}  // namespace

int main(int argc, char** argv) {
  bool headless = false;
  std::string scriptPath;
  for (int i = 1; i < argc; i++) {
    const std::string a = argv[i];
    if ((a == "--headless" || a == "--script") && i + 1 < argc) headless = a == "--headless", scriptPath = argv[++i];
    else if (a == "--zoom" && i + 1 < argc) zoom = std::max(2, atoi(argv[++i]));
    else if (a == "--out" && i + 1 < argc) outDir = argv[++i];
    else return usage(), 2;
  }
  if (!scriptPath.empty()) {
    std::ifstream f(scriptPath);
    if (!f) return fprintf(stderr, "[bot-sim] no script %s\n", scriptPath.c_str()), 2;
    for (std::string l; std::getline(f, l);) script.push_back(l);
  }
  system(("mkdir -p '" + outDir + "'").c_str());
  const char* data = getenv("BOT_SIM_DATA");
  const std::string dataDir = data ? data : "sim/bot/data-live";
  system(("mkdir -p '" + dataDir + "'").c_str());
  Preferences::path(dataDir + "/nvs.txt");

  if (SDL_Init((headless ? 0 : SDL_INIT_VIDEO | SDL_INIT_AUDIO) | SDL_INIT_EVENTS) != 0)
    return fprintf(stderr, "[bot-sim] SDL: %s\n", SDL_GetError()), 1;
  if (getenv("BOT_SIM_TRACE")) trace::setEcho(true);
  bot::app::begin();

  // the terminal's lines are the USB serial line
  std::thread([] {
    char buf[16384];
    while (fgets(buf, sizeof buf, stdin)) {
      std::string l = buf;
      while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
      serialLine(l);
    }
  }).detach();

  SDL_AudioDeviceID micDev = 0, spkDev = 0;
  if (!headless) {
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    win = SDL_CreateWindow("EspOS Bot (simulator)  ·  Space = BOOT  ·  ↑ Vol+ (talk, twice: louder)  ·  ↓ Vol-  ·  S = picture  ·  Q = quit", SDL_WINDOWPOS_CENTERED,
                           SDL_WINDOWPOS_CENTERED, 100, 100, 0);
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    texCrisp = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 128, 64);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");  // the glow's copy: blurred by the scaling
    texGlow = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 128, 64);
    layout();
    SDL_AudioSpec want{}, got{};
    want.freq = 16000, want.format = AUDIO_S16SYS, want.channels = 1, want.samples = 160, want.callback = spkCallback;
    spkDev = SDL_OpenAudioDevice(nullptr, 0, &want, &got, 0);
    if (spkDev) SDL_PauseAudioDevice(spkDev, 0);
    want.callback = micCallback;
    micDev = SDL_OpenAudioDevice(nullptr, 1, &want, &got, 0);
    if (micDev) SDL_PauseAudioDevice(micDev, 0), fprintf(stderr, "[bot-sim] microphone on: say \"Gâu gâu\", or press Space\n");
    else fprintf(stderr, "[bot-sim] no microphone (%s): Space talks; a script's say lines still work\n", SDL_GetError());
  } else {
    // no sound devices: the mic hears the script's say files (silence otherwise), the speaker goes to speaker.wav
    std::thread([] {
      auto next = std::chrono::steady_clock::now();
      int16_t out[160];
      while (!quit) {
        micSamples(nullptr, 160);
        bot::spk::pull(out, 160);
        spkOut.insert(spkOut.end(), out, out + 160);
        next += std::chrono::milliseconds(10);
        std::this_thread::sleep_until(next);
      }
    }).detach();
  }

  bool scriptDone = script.empty();
  uint32_t drawnAt = 0;
  while (!quit) {
    bot::app::loop();
    if (!scriptDone && !scriptStep()) scriptDone = true;
    recStep();
    if (headless) {
      if (scriptDone && recs.empty()) break;
      continue;
    }
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) quit = true;
      else if (e.type == SDL_KEYDOWN && !e.key.repeat) {
        if (e.key.keysym.sym == SDLK_SPACE) bootDown = true;
        else if (e.key.keysym.sym == SDLK_UP) volUpDown = true;     // Vol+: the head pat
        else if (e.key.keysym.sym == SDLK_DOWN) volDownDown = true;
        else if (e.key.keysym.sym == SDLK_q) quit = true;
        else if (e.key.keysym.sym == SDLK_s) {
          char name[64];
          snprintf(name, sizeof name, "/shot-%u.png", (unsigned)(millis() / 100));
          if (savePng(outDir + name, bot::oled::shown)) fprintf(stderr, "[bot-sim] %s%s\n", outDir.c_str(), name);
          snprintf(name, sizeof name, "/window-%u.png", (unsigned)(millis() / 100));
          render(outDir + name);  // the window as it looks, read back before it is shown
        }
      } else if (e.type == SDL_KEYUP && e.key.keysym.sym == SDLK_SPACE) {
        bootDown = false;
      } else if (e.type == SDL_KEYUP && e.key.keysym.sym == SDLK_UP) {
        volUpDown = false;
      } else if (e.type == SDL_KEYUP && e.key.keysym.sym == SDLK_DOWN) {
        volDownDown = false;
      } else if (e.type == SDL_MOUSEBUTTONDOWN) {
        const int dx = e.button.x - (button.x + button.w / 2), dy = e.button.y - (button.y + button.h / 2);
        if (dx * dx + dy * dy <= button.w * button.w / 4) bootDown = true;
      } else if (e.type == SDL_MOUSEBUTTONUP) {
        bootDown = false;
      }
    }
    if (millis() - drawnAt >= 15) render(""), drawnAt = millis();
  }
  if (headless) {
    writeWav(outDir + "/speaker.wav", spkOut);
    fprintf(stderr, "[bot-sim] %s: %d failed\n", scriptPath.c_str(), fails);
  }
  if (micDev) SDL_CloseAudioDevice(micDev);
  if (spkDev) SDL_CloseAudioDevice(spkDev);
  SDL_Quit();
  fflush(stdout), fflush(stderr);
  _exit(fails ? 1 : 0);  // the server and terminal threads never return: no waiting on them at exit
}
