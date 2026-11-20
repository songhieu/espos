#pragma once
// The game's sounds (docs/pawboop.md, "Sounds"): the phone game's sound kit on the board. Short effects, loops
// (snoring, footsteps), the music and the world's ambience are mixed with the dog's voice into ONE stream
// (sound::stream, 16 kHz). The sounds are packages' files, never the firmware's:
//   sfx.pba    in a dog's package (its own barks, groans, crunches), in vn.pawboop.pet (the screen's taps, the
//              coins, the toys, the home tune) and in vn.pawboop.run (the runner's jumps, slides, hits)
//   music.pba  in a map's package: its tune ("music") and its ambience ("amb")
//   sfx.cue    in vn.pawboop.pet / vn.pawboop.run: which sound plays when (a picture clip's sounds at their
//              times, the game's events, the music of a place), with the phone game's volumes and pitches.
// Sounds absent from the packages are skipped: older packages play as before, without them. A moment's sound is
// one of its variants at random, never the same one twice in a row (as the phone game).
// Effects and music are silent with Do Not Disturb, in quiet hours, at volume 0, while the assistant listens or
// speaks, while the game is not on screen, and when turned off in the dog's book. The dog's voice is not.
#include "../sound.h"

namespace pet {
namespace sfx {

// A game is on screen (the pet: run false; Coin Run over it: run true): its packages' sounds and its cue table.
// Cheap when they are loaded already; the last one to call it owns frame().
void open(const char* dogPkg, bool run);
void close(bool run);  // that game closed: everything stops, the decoded sounds are freed

// The voice's stream (voice.cpp) joins the mix; nullptr: the voice stops (10 ms fade), the rest goes on.
void voice(sound::Pull pull, void* ctx);

// A picture clip starts (the pet's play()), then each of its pictures (10 a second, from 0 again after a loop):
// its sounds follow its pictures; its loops (snoring) last as long as it shows.
void clip(const char* name);
void picture(int i);
// A moment of the game ("tap", "coin", "jump"...), as sfx.cue says. A loop's moment: on starts it, off ends it.
bool event(const char* name, bool on = true);
void loopPitch(const char* name, float pitch);  // a loop's moment: its speed (the footsteps follow the run)
// The tune of a place ("home", "menu"), or a map's (its package's music.pba, with its ambience). nullptr: none.
void music(const char* name);
void world(const char* mapPkg);
// Home (the pet game): the home tune, and the ambience of the last map a run was in (the phone game never stops
// its world's ambience; before any run, the map the run starts in).
void home();
// The speaker plays this mix (pulled in the last 100 ms) and it carried no voice and no effect for `ms`: only a tune
// or an ambience. The wake word listens through a tune (with a higher bar), never through the dog's lines, the
// game's effects or another stream (the assistant's voice). Tune and ambience stop after 30 s without a touch.
bool bedOnly(uint32_t ms);

// Every UI frame of the owning game: pauses the effects and the music while it is not on screen (or the
// assistant listens / speaks, quiet hours...) and brings them back after.
void frame(bool run, bool onScreen);

bool effectsOn();
bool musicOn();
void setEffects(bool on);
void setMusic(bool on);

}  // namespace sfx
}  // namespace pet
