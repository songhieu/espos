#!/bin/sh
# Builds the simulator (sim/Makefile, BUILD dir) and links its objects with the run game's script driver
# (tools/pawboop_run/simdrv.cpp) in place of sim/main.o, then runs SCRIPT -> OUTDIR with SIM_DATA (default
# sim/data-run; install the parts on its memory card: tools/pawboop_run/make_pkgs.py --sim sim/data-run).
#   tools/pawboop_run/sim.sh SCRIPT OUTDIR            (from s3/)
#   BUILD=build-run-asan EXTRA="-fsanitize=address -fno-omit-frame-pointer" tools/pawboop_run/sim.sh ...
#   PKGSTUB=1 ...   link tools/pawboop_run/pkgstub.cpp instead of src/pkg (when src/pkg does not build)
set -e
cd "$(dirname "$0")/../.."
BUILD=${BUILD:-build-run}
if [ -n "$PKGSTUB" ]; then
  make -k -C sim -j10 BUILD="$BUILD" EXTRA="$EXTRA" all >/dev/null 2>&1 || true
else
  make -C sim -j10 BUILD="$BUILD" EXTRA="$EXTRA" all >/dev/null
fi
LV=.pio/libdeps/s3desk/lvgl
AJ=.pio/libdeps/s3desk/ArduinoJson/src
CXX="c++ -O1 -g $EXTRA -DSIM -DLV_CONF_INCLUDE_SIMPLE -Isim/shim -Iinclude -I$LV -Isrc -I$AJ -w -std=gnu++17
  -DARDUINOJSON_ENABLE_ARDUINO_STRING=1 -DARDUINOJSON_ENABLE_ARDUINO_STREAM=0 -DARDUINOJSON_ENABLE_ARDUINO_PRINT=0
  -DARDUINOJSON_ENABLE_PROGMEM=0"
$CXX -c tools/pawboop_run/simdrv.cpp -o "sim/$BUILD/simdrv.o"
if [ -n "$PKGSTUB" ]; then
  $CXX -c tools/pawboop_run/pkgstub.cpp -o "sim/$BUILD/pkgstub.o"
  OBJS="$(find "sim/$BUILD/app" "sim/$BUILD/opus" "sim/$BUILD/qjs" -name '*.o' ! -path '*/app/pkg/*' 2>/dev/null) sim/$BUILD/pkgstub.o"
else
  OBJS=$(find "sim/$BUILD/app" "sim/$BUILD/opus" "sim/$BUILD/qjs" -name '*.o' 2>/dev/null)
fi
if ! c++ $EXTRA $OBJS "sim/$BUILD/sim/stubs.o" "sim/$BUILD/simdrv.o" "sim/$BUILD/liblvgl.a" -lz -o "sim/$BUILD/run-sim" 2>/dev/null; then
  # The store screen (app_more.cpp) needs the package system's new parts: left out with the stub.
  OBJS=$(echo "$OBJS" | tr ' ' '\n' | grep -v '/app_more.cpp.o$' | tr '\n' ' ')
  c++ $EXTRA $OBJS "sim/$BUILD/sim/stubs.o" "sim/$BUILD/simdrv.o" "sim/$BUILD/liblvgl.a" -lz -o "sim/$BUILD/run-sim"
fi
[ -n "$1" ] || exit 0
mkdir -p "$2"
SIM_DATA=${SIM_DATA:-sim/data-run} "sim/$BUILD/run-sim" "$1" "$2"
