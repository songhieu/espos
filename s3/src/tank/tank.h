#pragma once
// Hồ cá cảnh: the aquarium game, and the saver's living background (the hand-off of 2026-11-19, handoff_tank:
// BRIEF_TANK.md; the browser page sim/tank.html is the reference for look and behaviour). An installable package of
// EspOS with engine "tank": its data (tank.json) and pictures (PBU1 packs) are files of the package, read through
// pkg::open(), from the internal flash or the memory card; the water's motion and drawing are this firmware's.
// docs/tank.md: files, drawing, numbers, the saver background.
//
//   pkg::run engine "tank"           -> tank::open(id, arg)
//   saver page {"bg":{"t":"tank"}}   -> tank::saverBg(parent, w, h)   (src/desksaver.cpp drawBg)
//   dev console "TANK <cmd>", simulator "tank <cmd>" -> tank::dev(cmd)
#include <lvgl.h>

namespace tank {

void open(const char* pkgId, const char* arg);
bool isOpen();
// The installed aquarium package (engine "tank"), or nullptr.
const char* installedId();
// The saver's background: the user's own tank, animated (about 12 pictures a second) while it is shown, under the
// page's clock and widgets. A child of parent, w x h. nullptr when no aquarium is installed (or no memory).
lv_obj_t* saverBg(lv_obj_t* parent, int w, int h);
// Developer commands: "bench [N]" (frame times with N fish, 5/12/20 when not given), "free LOOK LIST" (a free tank as
// the page's showcase, "neon*7,guppy*2"), "game" (back to the game), "time H", "coins N", "xp N", "look ID",
// "night 0|1|-", "speed K", "fps N", "tear 0|1", "stats", "shot PATH" (the panel's picture as a PPM), "reset".
bool dev(const char* cmd);

}  // namespace tank
