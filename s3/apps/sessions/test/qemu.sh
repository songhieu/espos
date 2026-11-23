#!/bin/bash
# Sessions in QEMU: the real firmware on Espressif's emulated ESP32-S3 (docs/simulator.md), against the local mock
# (mock_server.py, reached from the guest at 10.0.2.2): pictures of the main screens in both orientations.
#
#   apps/sessions/test/qemu.sh [35|28] [OUTDIR]       SKIPBUILD=1: use the last build in BUILD
#
# Its own build folder (BUILD, default .pio/build-sessions), image, card, serial port and monitor socket: it never
# touches build-qemu/ or anyone else's QEMU. The device is provisioned with the mock as its server and the mock's
# device credential (its own sync fails there: the mock speaks plain http; the app's requests go through). QEMU is
# stopped when the script ends (one run, closed right after).
set -euo pipefail
cd "$(dirname "$0")/../../.."
BOARD=${1:-35}
OUT=${2:-$(mktemp -d "${TMPDIR:-/tmp}/sessions-qemu.XXXXXX")}
ENV=s3desk-qemu; [ "$BOARD" = 28 ] && ENV=s3desk-28-qemu
BUILD=${BUILD:-.pio/build-sessions}
PORT=${PORT:-$([ "$BOARD" = 28 ] && echo 5592 || echo 5591)}
MOCK=${MOCK:-$([ "$BOARD" = 28 ] && echo 8792 || echo 8791)}
Q=${QEMU:-../tools-ext/qemu/qemu/bin/qemu-system-xtensa}
mkdir -p "$OUT"
[ -n "${SKIPBUILD:-}" ] || PLATFORMIO_BUILD_DIR="$BUILD" ~/.platformio/penv/bin/pio run -e $ENV -j 4 | grep -E "error|SUCCESS|FAILED"
python3 apps/sessions/tools/build.py >/dev/null
python3 apps/sessions/tools/package.py "$OUT/sessions.espk" >/dev/null
rm -rf "$OUT/qdata" && cp -R sim/data-live "$OUT/qdata"
python3 tools/espk.py --install-sim "$OUT/qdata" "$OUT/sessions.espk" --vol flash >/dev/null
mkdir -p "$OUT/qdata/sd"  # an empty card when the data has none
python3 tools/qemu/mkimage.py "$OUT/flash.bin" --env $ENV --data "$OUT/qdata" --sd "$OUT/sd.img" --build "$BUILD/$ENV" >/dev/null
python3 apps/sessions/test/mock_server.py --port "$MOCK" >"$OUT/mock.log" 2>&1 &
MPID=$!
export DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/opt/sdl2/lib
QABS=$(cd "$(dirname "$Q")" && pwd)/$(basename "$Q")
# A unix socket path must be under 104 bytes: QEMU runs in OUT and names it relatively.
(cd "$OUT" && exec "$QABS" -machine esp32s3 -m 8M -global driver=ssi_psram,property=is_octal,value=true \
  -drive file=flash.bin,if=mtd,format=raw -drive file=sd.img,if=sd,format=raw -nic user,model=open_eth \
  -monitor unix:mon.sock,server,nowait -display none -serial "tcp:127.0.0.1:$PORT,server,nowait") >"$OUT/qemu.log" 2>&1 &
QPID=$!
trap 'kill $QPID $MPID 2>/dev/null || true' EXIT
DEV=$(python3 -c "import sys; sys.path.insert(0, 'apps/sessions/test'); import mock_server; print(mock_server.DEVICE)")
D() { python3 apps/sessions/test/qemu_drive.py "$PORT" "$OUT/mon.sock" "$OUT" "$@"; }
D "until:os built" "sleep:3" \
  "ESPOS:PROVISION {\"token\":\"$DEV\",\"api\":\"http://10.0.2.2:$MOCK\"}" "sleep:2"
for r in 1 0; do
  # "TAP 5 5": a tap on the status bar wakes a saver that started meanwhile (it covers an app opened under it)
  D "ROT $r" "sleep:4" \
    "TAP 5 5" "sleep:1" "APP dev.espos.sessions" "sleep:10" "shot:q$BOARD-list-r$r" "HOME" "sleep:2" \
    "TAP 5 5" "sleep:1" "APP dev.espos.sessions 900:s-3" "sleep:12" "shot:q$BOARD-term-r$r" \
    "TAP $([ $r = 1 ] && echo 190 $([ "$BOARD" = 28 ] && echo 332 || echo 292) || echo '110 452')" "sleep:4" "shot:q$BOARD-kb-r$r" "HOME" "sleep:2" \
    "TAP 5 5" "sleep:1" "APP dev.espos.sessions 900:s-1" "sleep:10" "shot:q$BOARD-perm-r$r" "HOME" "sleep:2"
done
grep -h "app dev.espos.sessions\|App error\|Guru\|abort\|stack" "$OUT/serial.log" | head -40 >"$OUT/report.txt" || true
echo "$OUT"
