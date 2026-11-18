#!/bin/sh
# EspOS on this computer, live: a window with the 320x480 screen, the mouse as the finger, the speaker, and
# the run game's decoders at the board's speed with the board's PSRAM (sim/main.cpp, sim/live.cpp).
#   tools/sim-live.sh                 the board's speed
#   tools/sim-live.sh --fast          the computer's speed
#   tools/sim-live.sh --zoom 2        window size (points per screen pixel; default: the largest that fits)
#   tools/sim-live.sh SCRIPT          a script's commands first (sim/main.cpp lists them), then by hand
#   tools/sim-live.sh --reset         a fresh copy of the data first
# Keys: arrows = swipe (the run game), H = home, B = back, S = screenshot (sim/shots), M = memory, Q = quit.
# Commands can be typed in this terminal too (help). Data: sim/data-live, made from sim/data-run the first time
# with the run game's roads20 on its memory card (as on the board); SIM_DATA=dir for another copy.
set -e
cd "$(dirname "$0")/.."
DATA=${SIM_DATA:-sim/data-live}
if [ "$1" = "--reset" ]; then
  shift
  rm -rf "$DATA"
fi
make -C sim -j10 BUILD=build-live live >/dev/null
if [ ! -d "$DATA" ]; then
  cp -R sim/data-run "$DATA"
  PKG=build-pkgs/vn.pawboop.run-1.0.1
  if [ -f "$PKG-core.espk" ] && [ -f "$PKG-roads20.espk" ]; then
    python3 tools/espk.py --install-sim "$DATA" "$PKG-core.espk" "$PKG-roads20.espk" >/dev/null
  fi
fi
SIM_DATA="$DATA" exec sim/build-live/espos-live "$@"
