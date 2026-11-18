#pragma once
// PawBoop Chạy: Biscuit's three-lane dodge run (the PawBoop phone game's "RUN", the game designer's handoff
// pawboop_run_handoff: BRIEF_RUN.md, sim/run.html). An installable package of EspOS: engine "run"
// (docs/ecosystem.md). Every file is read through pkg::open(), so the same code runs from the internal flash
// and from the memory card. docs/run.md: files, drawing, numbers.
//
//   pkg::run engine "run"  ->  run::open(id, arg)
//   dev console "RUNBENCH" ->  run::bench(id)
namespace run {

// The game screen. arg: nullptr / "" = the start card; a map name ("bazaar", "construction", "carnival") =
// start there; "bench" = run::bench() on this screen; "pet" = opened from PawBoop's pet screen ("Chạy lấy xu"):
// over it, with "Về nhà" under the cards and a word on where the coins go.
void open(const char* pkgId, const char* arg);
bool isOpen();

// Speed test for the board: opens the game (if it is not open), times decoding and scaling on the UI thread,
// then lets her run by herself for 30 s: 10 s as by default (smooth road scale), 10 s with the plain x2 scale,
// 10 s with a second decoder on core 1. Prints "RUNBENCH ..." lines (Serial and TRACE): decode / scale /
// sprites / compose / push ms, pictures a second, road pictures a second and how often one came late, card read
// times, the delay from a move to the picture that shows it. Ends with "RUNBENCH done".
void bench(const char* pkgId = "vn.pawboop.run");

// Developer switches (dev console / simulator scripts): "auto" (she plays by herself: long runs, map links),
// "manual", "god" (no hits), "warp <metres>" (jump ahead on the road), "end" (lose every life now),
// "smooth 0|1" (road scale), "helper 0|1" (the core 1 decoder), "stats" (print the stats now), "frame" (draw one
// picture whole through LVGL: the game goes straight to the panel, so a SHOT needs this first). false: unknown.
bool dev(const char* cmd);

}  // namespace run
