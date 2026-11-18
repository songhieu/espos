#pragma once
// The live simulator's window, input and speaker (SDL2): sim/live.cpp. sim/main.cpp built with SIM_LIVE drives it.
#include <cstdint>
#include <string>
#include <vector>

namespace live {
extern bool on;  // the window is up (real time instead of script time)

bool begin(int w, int h, float zoom);  // zoom 0: the largest that fits the screen
void end();
uint32_t ms();  // the wall clock
void sleepMs(int n);

// The screen (RGB565, as the panel gets it). The window follows its size (rotation).
void present(const uint16_t* fb, int w, int h);
void setTitle(const std::string& t);

// Everything since the last call. The finger: the mouse's left button on the screen. Keys come back as script
// commands ("swipe up", "home", ...). false: the window was closed.
struct Finger {
  bool changed = false, down = false;
  int x = 0, y = 0;
};
bool events(Finger& f, std::vector<std::string>& commands);

void shootWindow(const std::string& path);  // the next picture of the whole window into a PNG
bool savePng(const std::string& path, const uint16_t* fb, int w, int h);
}  // namespace live
