#pragma once

class GfxRenderer;
class MappedInputManager;

namespace espos {
// Serial "CMD:OPEN <screen>" (debug/automation): opens an EspOS screen directly so it can
// be captured with CMD:SCREENSHOT. Screens: home, setup, apps, sync, pair, welcome,
// dashset, stats, ota, sleep (draws the sleep screen without sleeping).
void debugOpen(const char* screen, GfxRenderer& renderer, MappedInputManager& input);
}  // namespace espos
