#!/bin/bash
# The ping under the saver, in QEMU: the real firmware's desk.cpp (its hold while a screen that asked for the
# connection's RAM is open, the ping under the saver, the connection closed after it) against saver-dash-mock.py (plain
# http at 10.0.2.2: ESPOS_QEMU builds speak it, desk.cpp http()). The owner's path (0.6.2): the aquarium app open,
# the saver over it, a Claude widget on an aquarium page.
#   1. the aquarium in front for 100 s: no ping (the hold, as before);
#   2. the saver over it: pings again; the mock's dashboard changes (89 % -> 4 %), the ping hears the rev, the sync
#      brings it, the saver's widget is drawn again while the saver stays up; internal RAM's lowest (MEM) meanwhile.
#   sim/scripts/saver-dash-qemu.sh [OUT] [35|28]   the env s3desk-qemu (s3desk-28-qemu) built in BUILD first
# HOME_FROM=sim/scripts/widget-resize-pages.sim: its resized Home pages come down too, and Home is shot at the end in
# both turns.
# Its own image, card, serial port, monitor socket and mock port; QEMU and the mock are stopped at the end.
set -euo pipefail
cd "$(dirname "$0")/../.."
OUT=${1:-$(mktemp -d "${TMPDIR:-/tmp}/saver-dash-qemu.XXXXXX")}
BOARD=${2:-35}
ENV=s3desk-qemu; BG=bg35; [ "$BOARD" = 28 ] && ENV=s3desk-28-qemu BG=bg28
BUILD=${BUILD:-.pio/build-sessions}
PORT=${PORT:-$([ "$BOARD" = 28 ] && echo 5594 || echo 5593)}
MOCK=${MOCK:-$([ "$BOARD" = 28 ] && echo 8797 || echo 8795)}
Q=${QEMU:-../tools-ext/qemu/qemu/bin/qemu-system-xtensa}
TANK="build-pkgs/espos/s3/vn.pawboop.tank-1.0.0-core.espk build-pkgs/espos/s3/vn.pawboop.tank-1.0.0-$BG.espk"
mkdir -p "$OUT"
rm -rf "$OUT/qdata" && cp -R sim/data-live "$OUT/qdata" && mkdir -p "$OUT/qdata/sd"
python3 tools/espk.py --install-sim "$OUT/qdata" $TANK --vol sd >/dev/null
python3 tools/qemu/mkimage.py "$OUT/flash.bin" --env $ENV --data "$OUT/qdata" --sd "$OUT/sd.img" --build "$BUILD/$ENV" >/dev/null
python3 sim/scripts/saver-dash-mock.py "$MOCK" >"$OUT/mock.log" 2>&1 &
MPID=$!
export DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/opt/sdl2/lib
QABS=$(cd "$(dirname "$Q")" && pwd)/$(basename "$Q")
(cd "$OUT" && exec "$QABS" -machine esp32s3 -m 8M -global driver=ssi_psram,property=is_octal,value=true \
  -drive file=flash.bin,if=mtd,format=raw -drive file=sd.img,if=sd,format=raw -nic user,model=open_eth \
  -monitor unix:mon.sock,server,nowait -display none -serial "tcp:127.0.0.1:$PORT,server,nowait") >"$OUT/qemu.log" 2>&1 &
QPID=$!
trap 'kill $QPID $MPID 2>/dev/null || true' EXIT
D() { python3 apps/sessions/test/qemu_drive.py "$PORT" "$OUT/mon.sock" "$OUT" "$@"; }
st() { curl -s "http://127.0.0.1:$MOCK/mock/state"; }
pings() { st | python3 -c "import json,sys; s=json.load(sys.stdin); print(len(s['pings']), len(s['syncs']), s['now'])"; }
TOKEN=$(python3 -c "print('5e' * 32)")
# The trace goes to the serial port only after TOUCHLOG 1 (the boot's own lines come before anyone asks).
D "until:sysmon" "sleep:8" "TOUCHLOG 1" "sleep:1" "ESPOS:PROVISION {\"token\":\"$TOKEN\",\"api\":\"http://10.0.2.2:$MOCK\"}" "until:sync ok" "sleep:4" "MEM"
echo "after the first sync: pings/syncs/ms $(pings)" | tee "$OUT/report.txt"
D "APP vn.pawboop.tank" "sleep:6" "shot:tank-app"
p1=$(pings)
D "sleep:100" "MEM"
p2=$(pings)
echo "1. the aquarium in front, 100 s: pings/syncs/ms $p1 -> $p2" | tee -a "$OUT/report.txt"
D "SAVER" "sleep:6" "shot:saver-before" "sleep:60"
p3=$(pings)
echo "2a. the saver over it, 66 s: pings/syncs/ms $p2 -> $p3" | tee -a "$OUT/report.txt"
curl -s -X POST "http://127.0.0.1:$MOCK/mock/switch" >/dev/null
D "until:widget drawn with the new dashboard" "sleep:8" "shot:saver-after" "MEM" "sleep:60" "MEM"
p4=$(pings)
echo "2b. the switch, then 60 s more: pings/syncs/ms $p3 -> $p4" | tee -a "$OUT/report.txt"
D "HOME" "sleep:3" "TAP 5 5" "sleep:50" "MEM" "shot:home-r0"
echo "3. back at Home: pings/syncs/ms $(pings)" | tee -a "$OUT/report.txt"
if [ -n "${HOME_FROM:-}" ]; then  # the resized pages: each page in portrait, then landscape
  D "TAP 5 5" "sleep:1" "DRAG 300 240 30 240 300" "sleep:3" "shot:home-r0-p2" "DRAG 300 240 30 240 300" "sleep:3" "shot:home-r0-p3" \
    "DRAG 300 240 30 240 300" "sleep:3" "shot:home-r0-p4" "DRAG 300 240 30 240 300" "sleep:3" "shot:home-r0-p5" \
    "DRAG 300 240 30 240 300" "sleep:3" "shot:home-r0-p6" "ROT 1" "sleep:6" "TAP 5 5" "sleep:2" "HOME" "sleep:3" "shot:home-r1-p1" \
    "DRAG 400 160 40 160 300" "sleep:3" "shot:home-r1-p2" "DRAG 400 160 40 160 300" "sleep:3" "shot:home-r1-p3" "ROT 0" "sleep:5"
fi
st >"$OUT/mock-state.json"
grep -h "ping: \|sync ok\|kept connection closed\|widget drawn\|saver on\|saver off\|tank: saver\|MEM \|http \|Guru\|abort\|panic" "$OUT/serial.log" \
  | grep -v "MEM lowest internal since the last MEM: 0" >>"$OUT/report.txt" || true
echo "$OUT"
