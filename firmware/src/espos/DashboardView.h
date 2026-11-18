#pragma once

#include <cstdint>

class GfxRenderer;

// Device-drawn EspOS dashboard (the sleep screen when dash.enabled=1).
// Reads the cached GET /v1/device/dashboard JSON from EspOSStore's DASH_FILE.
namespace espos::dashboard {

bool hasCache();

// What the sleep screen shows, from sleep.mode and what is on the device.
enum class Mode : uint8_t { Dashboard, Images, Cover };
// images falls back to the dashboard without pictures; cover and auto use the cover only
// while a book is open and not finished.
Mode effectiveMode();

// Whether EspOS draws the sleep screen itself and may wake on a timer: linked, enabled,
// and the dashboard cache or the pictures it needs are there (cover mode never is).
bool sleepScreenEnabled();
// True when EspOS owns the sleep screen (dashboard or placeholder) instead of CrossPoint's.
bool ownsSleepScreen();

// What the sleeping device does next.
struct WakePlan {
  uint32_t sleepSec = 0;    // seconds until the next wake; 0 = wake on the power button only
  int64_t nextFetchMs = 0;  // epoch ms of the next data download (footer); 0 = unknown
  bool clock = false;       // the clock is redrawn every minute, so the screen may show it
  bool saving = false;      // battery below the saving level: no timed wakes until charged
};
WakePlan planWake(bool usbPowered);
// Seconds until the next timer wake (planWake().sleepSec).
uint32_t nextWakeSeconds(bool usbPowered);
// The battery level ("dash.save_pct", 0 = never) under which the reader stops waking itself.
int saveBelowPercent();
// True on battery below that level: no automatic syncs, no minute clock.
bool batterySaving(bool usbPowered);
// Whether this wake should download fresh data (the cached copy is due, retries are spaced out).
bool fetchDue(bool usbPowered);
// Call right before a download attempt so failures back off instead of retrying every wake.
void noteFetchAttempt();

// Draws the cached dashboard into the framebuffer (no panel refresh) in the chosen
// layout (dash.layout: desk, calendar or portrait). Sets the renderer orientation for
// that layout and leaves it set; callers that keep drawing restore their own.
bool draw(GfxRenderer& renderer, const WakePlan& plan);
// EspOS-styled sleep screen used when the dashboard has nothing to show yet
// (not linked, or no data downloaded). Always draws; caller pushes the buffer.
void drawPlaceholder(GfxRenderer& renderer);

}  // namespace espos::dashboard
