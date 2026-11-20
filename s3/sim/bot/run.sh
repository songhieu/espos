#!/bin/sh
# EspOS Bot's screens on the computer: builds sim/bot/host.cpp with the firmware's drawing code (address sanitizer on),
# runs it, and makes tools/bot/out/ui.html (every state, every look, both languages; GIFs for the moving ones).
set -e
cd "$(dirname "$0")/../.."
B=sim/bot/build
mkdir -p "$B" "$B/frames"
rm -f "$B"/frames/*.frames
cc -std=c99 -O1 -g -fsanitize=address -c src/bot/vendor/qrcodegen.c -o "$B/qrcodegen.o"
AJ=.pio/libdeps/s3desk/ArduinoJson/src
F="-std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -Wall -Wextra -Wno-unused-parameter -Wno-unknown-pragmas -I$AJ"
c++ $F -c src/bot/art.cpp -o "$B/art.o"
c++ $F sim/bot/test.cpp src/bot/logic.cpp src/bot/gfx.cpp src/bot/ui.cpp src/lunar.cpp "$B/art.o" "$B/qrcodegen.o" -o "$B/test"
"$B/test"
# the turn (src/bot/talk.cpp) with everything around it played by sim/bot/flow.cpp, on sim/shim's Arduino stand-in
c++ $F -Isim/shim -DARDUINOJSON_ENABLE_ARDUINO_STRING=1 -DARDUINOJSON_ENABLE_ARDUINO_STREAM=0 \
  -DARDUINOJSON_ENABLE_ARDUINO_PRINT=0 -DARDUINOJSON_ENABLE_PROGMEM=0 \
  sim/bot/flow.cpp src/bot/talk.cpp src/bot/logic.cpp src/bot/gfx.cpp src/bot/ui.cpp src/lunar.cpp "$B/art.o" "$B/qrcodegen.o" -o "$B/flow"
"$B/flow"
c++ $F sim/bot/host.cpp src/bot/gfx.cpp src/bot/ui.cpp src/lunar.cpp "$B/art.o" "$B/qrcodegen.o" -o "$B/host"
"$B/host" "$B/frames"
python3 tools/bot/shots.py "$B/frames" tools/bot/out
