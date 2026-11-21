#!/bin/sh
# EspOS's real firmware in Espressif's QEMU (docs/simulator.md): an emulated ESP32-S3 (both cores, octal PSRAM
# 8 MB, 16 MB flash, a memory card, Ethernet to the internet through this computer), the screen in a window, the
# mouse as the finger.
#   tools/qemu/run.sh              build env s3desk-qemu, put it into build-qemu/flash.bin (its data kept, as
#                                  flashing the board), run with the viewer (tools/qemu/view.cpp: the screen, the
#                                  mouse, keys; dev commands typed here)
#   tools/qemu/run.sh --keep       the image as it is, no build
#   tools/qemu/run.sh --fresh      a new image: data from sim/data-live (settings, packages, the card), unlinked
#   tools/qemu/run.sh --sdl        QEMU's own window instead (no mouse), the dev console on this terminal
#   tools/qemu/run.sh --tcp 5555   QEMU's own window, the dev console on a port (tools/qemu/console.py)
#   tools/qemu/run.sh --headless   no window, the dev console on port 5555
#   tools/qemu/run.sh --board 28   the 2.8" board's firmware (env s3desk-28-qemu); its own image, card, sockets
#                                  ("-28"), serial port 5556 and web page 8081, so it runs beside the 3.5" one
#   tools/qemu/run.sh --v2         partition table v2 (docs/partitions.md): its own image build-qemu/flash[-28]-v2.bin;
#                                  the same firmware (an image runs on both tables). To switch a v1 image with its data:
#                                  tools/migrate_v2.py --image build-qemu/flash.bin --build .pio/build/s3desk-qemu
# Remote control (Settings › Điều khiển từ xa): the device's page is at http://127.0.0.1:8080 (port 80 forwarded).
# Screenshots: S in the viewer, or tools/qemu/console.py PORT shot:FILE.ppm (QEMU's monitor).
# QEMU: ../tools-ext/qemu (esp-develop-9.2.2-20261109, aarch64 macOS, sha256 35d0b689...).
set -e
cd "$(dirname "$0")/../.."
Q=${QEMU:-../tools-ext/qemu/qemu/bin/qemu-system-xtensa}
MODE=view
KEEP=0
FRESH=0
PORT=5555
ENV=s3desk-qemu
IMG=build-qemu/flash.bin
VIEWARGS=""
T=""      # "-28": the 2.8" board's files
WEB=8080
TABLE=""  # --v2: mkimage's --table
while [ $# -gt 0 ]; do
  case "$1" in
    --keep) KEEP=1 ;;
    --fresh) FRESH=1 ;;
    --sdl) MODE=sdl ;;
    --tcp) MODE=tcp; PORT=$2; shift ;;
    --headless) MODE=headless ;;
    --board) [ "$2" = 28 ] && ENV=s3desk-28-qemu T=-28 PORT=5556 WEB=8081 VIEWARGS="--board 28"; shift ;;
    --v2) TABLE="--table partitions_v2.csv" ;;
  esac
  shift
done
mkdir -p build-qemu
IMG=build-qemu/flash$T${TABLE:+-v2}.bin
SD=build-qemu/sd$T.img
if [ $KEEP = 0 ] || [ ! -f $IMG ]; then
  ~/.platformio/penv/bin/pio run -e $ENV | grep -E "error|SUCCESS|FAILED"
  if [ $FRESH = 1 ] || [ ! -f $IMG ]; then
    python3 tools/qemu/mkimage.py $IMG --env $ENV --sd $SD $TABLE
  else
    python3 tools/qemu/mkimage.py $IMG --env $ENV --app-only $TABLE
  fi
fi
# The prebuilt QEMU looks for Homebrew's sdl2-compat; plain SDL2 2.x is the same library.
export DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/opt/sdl2/lib
COMMON="-machine esp32s3 -m 8M -global driver=ssi_psram,property=is_octal,value=true
  -drive file=$IMG,if=mtd,format=raw -drive file=$SD,if=sd,format=raw -nic user,model=open_eth,hostfwd=tcp:127.0.0.1:$WEB-:80
  -monitor unix:build-qemu/monitor$T.sock,server,nowait"
case $MODE in
  sdl) exec "$Q" $COMMON -display sdl -serial mon:stdio ;;
  tcp) exec "$Q" $COMMON -display sdl -serial "tcp:127.0.0.1:$PORT,server,nowait" ;;
  headless) exec "$Q" $COMMON -display none -serial "tcp:127.0.0.1:$PORT,server,nowait" ;;
esac
# The viewer: QEMU without a window, the serial port waiting for the viewer (nothing is lost before it connects).
if [ ! -x build-qemu/view ] || [ tools/qemu/view.cpp -nt build-qemu/view ] || [ sim/realsize.cpp -nt build-qemu/view ]; then
  SDL=/opt/homebrew/bin/sdl2-config
  c++ -O2 -std=c++17 $($SDL --cflags) tools/qemu/view.cpp sim/realsize.cpp $($SDL --libs) -lz \
    -framework ApplicationServices -o build-qemu/view
fi
rm -f build-qemu/qmp$T.sock
"$Q" $COMMON -display none -serial "tcp:127.0.0.1:$PORT,server,wait" -qmp unix:build-qemu/qmp$T.sock,server,nowait &
QPID=$!
trap 'kill $QPID 2>/dev/null' EXIT INT TERM
build-qemu/view "$PORT" build-qemu/qmp$T.sock $VIEWARGS
