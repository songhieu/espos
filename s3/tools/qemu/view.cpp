// A window on the firmware running in QEMU (tools/qemu/run.sh starts both; docs/simulator.md).
// QEMU runs without its own window. This shows the screen the firmware drew (the frame it keeps in QEMU's panel
// memory at 0x20000000, read through QMP's pmemsave about 30 times a second) and sends the mouse to the firmware
// as finger samples (dev console "TOUCH d|u X Y" on the serial port), so the touch goes through the board's own
// queue, gestures and click. The window shows the board's screen at its real size (sim/realsize.h; Z = enlarged).
// Keys: arrows = a swipe in the middle, H = home swipe, B or Backspace = back swipe, S = a screenshot
// (build-qemu/shot-*.png), Z = real size / enlarged, Q = quit. The firmware's serial output is printed here, and lines
// typed here go to its dev console (HELP), and so do lines sent to build-qemu/console.sock (other tools:
// `printf 'APP pet\n' | nc -U build-qemu/console.sock`).
//   build-qemu/view SERIAL_PORT QMP_SOCKET
#include <SDL.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <zlib.h>

#include "../../sim/realsize.h"

#include <algorithm>
#include <cmath>
#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {
int serialFd = -1, qmpFd = -1;
std::mutex sendMtx;
std::string FB_FILE = "build-qemu/.frame.bin", CONSOLE = "build-qemu/console.sock";  // "-28" added for that board
int W = 320, H = 480;               // the panel (the window shows it)
const int MARGIN = 14;
int showX = 0, showW = 320;         // where the UI is on it (the 2.8" board: 213 px from x 13)
const int LW = 320, LH = 480;       // the UI (touches go in its coordinates)
float pitch = PANEL_MM_PER_PX;      // the panel's pixel pitch (mm)
// The firmware's rotation ("lcd: rotation N" on its serial port): the panel stays portrait, as the glass sits in the
// hand; the window shows it turned the right way up (N x 90 degrees clockwise) and turns the mouse back.
std::atomic<int> viewRot{0};

int connectTcp(int port) {
  for (int k = 0; k < 200; k++) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a = {};
    a.sin_family = AF_INET, a.sin_port = htons(port), a.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (connect(s, (sockaddr*)&a, sizeof a) == 0) return s;
    close(s);
    usleep(50000);
  }
  return -1;
}

int connectUnix(const char* path) {
  for (int k = 0; k < 200; k++) {
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un a = {};
    a.sun_family = AF_UNIX;
    snprintf(a.sun_path, sizeof a.sun_path, "%s", path);
    if (connect(s, (sockaddr*)&a, sizeof a) == 0) return s;
    close(s);
    usleep(50000);
  }
  return -1;
}

void sendLine(const std::string& l) {
  std::lock_guard<std::mutex> g(sendMtx);
  const std::string s = l + "\n";
  if (write(serialFd, s.data(), s.size()) < 0) {
  }
}

// One QMP command; waits for its {"return"...} (or an error) line, skipping events.
bool qmp(const std::string& json) {
  const std::string s = json + "\n";
  if (write(qmpFd, s.data(), s.size()) < 0) return false;
  std::string line;
  char c;
  while (read(qmpFd, &c, 1) == 1) {
    if (c != '\n') {
      line += c;
      continue;
    }
    if (line.find("\"return\"") != std::string::npos) return true;
    if (line.find("\"error\"") != std::string::npos) return fprintf(stderr, "[view] QMP: %s\n", line.c_str()), false;
    line.clear();
  }
  return false;
}

bool savePng(const char* path, const uint16_t* fb, int w, int h) {
  std::vector<uint8_t> raw;
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
  FILE* f = fopen(path, "wb");
  if (!f) return false;
  auto be = [](uint8_t* p, uint32_t v) { p[0] = v >> 24, p[1] = v >> 16, p[2] = v >> 8, p[3] = v; };
  auto chunk = [&](const char* t, const uint8_t* d, uint32_t n) {
    uint8_t b[8];
    be(b, n), memcpy(b + 4, t, 4);
    fwrite(b, 1, 8, f);
    if (n) fwrite(d, 1, n, f);
    uLong crc = crc32(0, (const Bytef*)t, 4);
    if (n) crc = crc32(crc, d, n);
    be(b, (uint32_t)crc);
    fwrite(b, 1, 4, f);
  };
  static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
  fwrite(sig, 1, 8, f);
  uint8_t ih[13];
  be(ih, w), be(ih + 4, h);
  ih[8] = 8, ih[9] = 2, ih[10] = ih[11] = ih[12] = 0;
  chunk("IHDR", ih, 13);
  chunk("IDAT", z.data(), (uint32_t)zn);
  chunk("IEND", nullptr, 0);
  fclose(f);
  return true;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) return fprintf(stderr, "usage: %s SERIAL_PORT QMP_SOCKET [--board 28]\n", argv[0]), 2;
  if (argc >= 5 && !strcmp(argv[3], "--board") && !strcmp(argv[4], "28"))
    W = 240, H = 320, showX = 13, showW = 213, pitch = 2.8f * 25.4f / 400.0f,  // ES3C28P: 2.8" over 400 px
    FB_FILE = "build-qemu/.frame-28.bin", CONSOLE = "build-qemu/console-28.sock";
  serialFd = connectTcp(atoi(argv[1]));
  qmpFd = connectUnix(argv[2]);
  if (serialFd < 0 || qmpFd < 0) return fprintf(stderr, "[view] no QEMU (serial %d, QMP %d)\n", serialFd, qmpFd), 1;
  qmp("{\"execute\":\"qmp_capabilities\"}");
  std::atomic<bool> quit{false};
  std::thread([&] {  // the firmware's serial output, to this terminal (and its rotation, for the picture)
    char b[4096];
    std::string tail;
    for (ssize_t n; (n = read(serialFd, b, sizeof b)) > 0;) {
      fwrite(b, 1, n, stdout), fflush(stdout);
      tail.append(b, n);
      for (size_t at; (at = tail.find("lcd: rotation ")) != std::string::npos && at + 14 < tail.size();) {
        const char d = tail[at + 14];
        if (d >= '0' && d <= '3') viewRot = d - '0';
        tail.erase(0, at + 15);
      }
      if (tail.size() > 64) tail.erase(0, tail.size() - 32);
    }
    quit = true;
  }).detach();
  std::thread([] {  // lines typed here, to its dev console
    for (std::string l; std::getline(std::cin, l);) sendLine(l);
  }).detach();
  std::thread([] {  // lines from other tools, to its dev console
    const char* path = CONSOLE.c_str();
    unlink(path);
    int srv = socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un a = {};
    a.sun_family = AF_UNIX;
    snprintf(a.sun_path, sizeof a.sun_path, "%s", path);
    if (bind(srv, (sockaddr*)&a, sizeof a) || listen(srv, 4)) return;
    for (;;) {
      int c = accept(srv, nullptr, nullptr);
      if (c < 0) continue;
      std::string line;
      char ch;
      while (read(c, &ch, 1) == 1) {
        if (ch == '\n') sendLine(line), line.clear();
        else line += ch;
      }
      if (!line.empty()) sendLine(line);
      close(c);
    }
  }).detach();

  if (SDL_Init(SDL_INIT_VIDEO) != 0) return fprintf(stderr, "[view] SDL: %s\n", SDL_GetError()), 1;
  SDL_Rect r;
  float big = 1, zoom = 1;  // points per panel pixel: enlarged (whole pixels), now
  bool real = true;
  if (SDL_GetDisplayUsableBounds(0, &r) == 0)
    for (float z : {2.0f, 1.5f, 1.0f})
      if (H * z + 2 * MARGIN + 40 <= r.h) {
        big = z;
        break;
      }
  zoom = big;
  SDL_Window* win = SDL_CreateWindow("EspOS · QEMU (firmware thật)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                     (int)(W * zoom) + 2 * MARGIN, (int)(H * zoom) + 2 * MARGIN, SDL_WINDOW_ALLOW_HIGHDPI);
  SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
  SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, W, H);
  // The board's real size on the display the window is on (again when it moves to another one), or enlarged.
  int shownRot = 0;
  auto resize = [&] {  // the window's shape follows the rotation
    const bool side = shownRot & 1;
    SDL_SetWindowSize(win, (int)((side ? H : W) * zoom + 0.5f) + 2 * MARGIN,
                      (int)((side ? W : H) * zoom + 0.5f) + 2 * MARGIN);
  };
  auto rezoom = [&] {
    float z = real ? realZoom(SDL_GetWindowDisplayIndex(win)) * pitch / PANEL_MM_PER_PX : big;
    if (z <= 0) z = big;
    if (fabsf(z - zoom) < 0.001f) return;
    zoom = z;
    resize();
  };
  auto turnWindow = [&] {
    shownRot = viewRot;
    resize();
  };
  rezoom();
  SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
  std::vector<uint16_t> fb(W * H);
  char path[512];
  realpath(".", path);
  const std::string fbPath = std::string(path) + "/" + FB_FILE;
  const std::string save = "{\"execute\":\"pmemsave\",\"arguments\":{\"val\":536870912,\"size\":" +
                           std::to_string(W * H * 2) + ",\"filename\":\"" + fbPath + "\"}}";
  bool down = false;
  uint32_t lastMove = 0, frames = 0, fpsAt = SDL_GetTicks();
  auto toPanel = [&](int mx, int my, int& x, int& y) {  // the window -> the panel's pixel -> the UI's
    const int vx = (int)((mx - MARGIN) / zoom), vy = (int)((my - MARGIN) / zoom);  // in the turned picture
    const int r = shownRot;
    const int px = r == 1 ? vy : r == 2 ? W - 1 - vx : r == 3 ? W - 1 - vy : vx;
    const int py = r == 1 ? H - 1 - vx : r == 2 ? H - 1 - vy : r == 3 ? vx : vy;
    x = std::max(0, std::min(LW - 1, (px - showX) * LW / showW));
    y = std::max(0, std::min(LH - 1, py * LH / H));
  };
  auto inside = [&](int mx, int my) {
    const bool side = shownRot & 1;
    return mx >= MARGIN && my >= MARGIN && mx < MARGIN + (side ? H : W) * zoom && my < MARGIN + (side ? W : H) * zoom;
  };
  printf("[view] real size %.2f points a pixel; mouse = finger; arrows = swipe, H = home, B = back, S = shot, "
         "Z = real size/enlarged, Q = quit; type dev commands here\n", zoom);
  while (!quit) {
    if (viewRot != shownRot) turnWindow();
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      int x, y;
      if (e.type == SDL_QUIT) quit = true;
      else if (e.type == SDL_WINDOWEVENT &&
               (e.window.event == SDL_WINDOWEVENT_DISPLAY_CHANGED || e.window.event == SDL_WINDOWEVENT_MOVED))
        rezoom();
      else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT && inside(e.button.x, e.button.y)) {
        toPanel(e.button.x, e.button.y, x, y);
        down = true, lastMove = SDL_GetTicks();
        sendLine("TOUCH d " + std::to_string(x) + " " + std::to_string(y));
      } else if (e.type == SDL_MOUSEMOTION && down && SDL_GetTicks() - lastMove >= 15) {
        toPanel(e.motion.x, e.motion.y, x, y);
        lastMove = SDL_GetTicks();
        sendLine("TOUCH d " + std::to_string(x) + " " + std::to_string(y));
      } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT && down) {
        toPanel(e.button.x, e.button.y, x, y);
        down = false;
        sendLine("TOUCH u " + std::to_string(x) + " " + std::to_string(y));
      } else if (e.type == SDL_KEYDOWN && !e.key.repeat) {
        switch (e.key.keysym.sym) {
          case SDLK_LEFT: sendLine("DRAG 160 260 100 260 80"); break;
          case SDLK_RIGHT: sendLine("DRAG 160 260 220 260 80"); break;
          case SDLK_UP: sendLine("DRAG 160 260 160 200 80"); break;
          case SDLK_DOWN: sendLine("DRAG 160 260 160 320 80"); break;
          case SDLK_h: sendLine("DRAG 160 476 160 370 120"); break;
          case SDLK_b:
          case SDLK_BACKSPACE: sendLine("DRAG 4 240 140 240 150"); break;
          case SDLK_s: {
            char n[64];
            const time_t t = time(nullptr);
            strftime(n, sizeof n, "build-qemu/shot-%Y%m%d-%H%M%S.png", localtime(&t));
            printf(savePng(n, fb.data(), W, H) ? "[view] %s\n" : "[view] could not write %s\n", n);
            break;
          }
          case SDLK_z: real = !real, rezoom(); break;
          case SDLK_q: quit = true; break;
          default: break;
        }
      }
    }
    // The frame the firmware keeps in the panel's memory.
    if (qmp(save)) {
      FILE* f = fopen(fbPath.c_str(), "rb");
      if (f) {
        if (fread(fb.data(), 2, fb.size(), f) == fb.size()) SDL_UpdateTexture(tex, nullptr, fb.data(), W * 2);
        fclose(f);
      }
    }
    int ww, wh, ow, oh;
    SDL_GetWindowSize(win, &ww, &wh);
    SDL_GetRendererOutputSize(ren, &ow, &oh);
    const float s = ww ? (float)ow / ww : 1;
    const float dev = zoom * s;  // device pixels a panel pixel: whole = square pixels, else smoothed
    SDL_SetTextureScaleMode(tex, fabsf(dev - roundf(dev)) < 0.01f ? SDL_ScaleModeNearest : SDL_ScaleModeLinear);
    SDL_SetRenderDrawColor(ren, 0x15, 0x17, 0x1c, 255);
    SDL_RenderClear(ren);
    SDL_Rect dst = {(int)(MARGIN * s), (int)(MARGIN * s), (int)(W * zoom * s + 0.5f), (int)(H * zoom * s + 0.5f)};
    if (shownRot & 1) {  // turned a quarter: the picture's box centred where the turned one goes
      dst.x = (int)(MARGIN * s + (H - W) * zoom * s / 2), dst.y = (int)(MARGIN * s + (W - H) * zoom * s / 2);
    }
    SDL_RenderCopyEx(ren, tex, nullptr, &dst, 90.0 * shownRot, nullptr, SDL_FLIP_NONE);
    SDL_RenderPresent(ren);
    frames++;
    if (SDL_GetTicks() - fpsAt >= 1000) {
      char t[120];
      snprintf(t, sizeof t, "EspOS · QEMU (firmware thật) · %u hình/giây", frames);
      SDL_SetWindowTitle(win, t);
      frames = 0, fpsAt = SDL_GetTicks();
    }
    SDL_Delay(25);
  }
  SDL_Quit();
  _exit(0);
}
