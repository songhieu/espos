#pragma once

#include <cstdint>
#include <string>
#include <vector>

class GfxRenderer;

// Sleep-screen pictures uploaded on the EspOS web: 1-bit BMPs at panel size
// (480x800 portrait or 800x480 landscape), already dithered by the browser, kept in DIR as
// "<id>.bmp". sleep.order picks the next one; sleep.rotate changes them at each sleep or on a
// timer while the device sleeps.
namespace espos::wallpapers {

constexpr const char* DIR = "/.crosspoint/espos/wall";

std::vector<int32_t> ids();  // ascending (upload order)
bool any();
std::string pathFor(int32_t id);
// Deletes the pictures the web no longer lists.
void removeExcept(const std::vector<int32_t>& keep);

// Moves to the next picture (sleep.order: seq or random) and stamps the change time.
void advance();
// Minutes between timed changes while sleeping, or 0 when pictures change only at sleep.
int rotateMinutes();
// Seconds until the next timed change (0 when not timed).
int64_t secondsToRotate();

// Draws the current picture, plus a thin time/battery strip at the bottom when `strip`.
// Sets the renderer orientation from the picture's shape and leaves it set.
bool draw(GfxRenderer& r, bool strip);

}  // namespace espos::wallpapers
