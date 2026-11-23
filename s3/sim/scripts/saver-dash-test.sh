#!/bin/sh
# The saver's widgets follow a new dashboard while the saver stays up (0.6.2; the owner's saver over the aquarium showed
# Claude at 89 % while the server had 4 %). The owner's path: the aquarium app open, the saver over it (the Control
# Center's Saver tile), a Claude widget on an aquarium page. A local mock (saver-dash-mock.py) is the server: its
# dashboard goes from 89 % / 61 % to 4 % / 1 % at its 4th sync.
#   sim/scripts/saver-dash-test.sh SIM LIVESIM OUT
# SIM: a script simulator build (AddressSanitizer), LIVESIM: the live one at the board's speed (make -C sim live), run
# headless. The simulator's desk is a stand-in: the ping and its hold are desk.cpp's, checked in QEMU
# (saver-dash-qemu.sh). Needs the aquarium's build-pkgs/espos/s3/vn.pawboop.tank-1.0.0-{core,bg35}.espk.
# Output: OUT/<run>/log.txt, pictures, OUT/report.txt.
set -u
cd "$(dirname "$0")/../.."
SIM=${1:?usage: saver-dash-test.sh SIM LIVESIM OUT}
LIVE=${2:?usage: saver-dash-test.sh SIM LIVESIM OUT}
OUT=${3:?usage: saver-dash-test.sh SIM LIVESIM OUT}
PORT=${PORT:-8794}
TANK="build-pkgs/espos/s3/vn.pawboop.tank-1.0.0-core.espk build-pkgs/espos/s3/vn.pawboop.tank-1.0.0-${TANKBG:-bg35}.espk"  # TANKBG=bg28 for a 2.8" SIM
mkdir -p "$OUT"
: >"$OUT/report.txt"
say() { echo "$*" | tee -a "$OUT/report.txt"; }
mock() {  # mock SWITCH_AT: a fresh mock (its counts from zero)
  [ -n "${M:-}" ] && kill "$M" 2>/dev/null && sleep 0.3
  python3 sim/scripts/saver-dash-mock.py "$PORT" --switch-at-sync "$1" >"$OUT/mock.log" 2>&1 &
  M=$!
  for i in $(seq 1 50); do curl -s "http://127.0.0.1:$PORT/mock/state" >/dev/null && break; sleep 0.1; done
}
trap 'kill $M 2>/dev/null || true' EXIT
data() {  # data DIR [BASE]: BASE (sim/data) with the aquarium on the card
  rm -rf "$1" && cp -R "${2:-sim/data}" "$1"
  python3 tools/espk.py --install-sim "$1" $TANK --vol sd >/dev/null
}
check() {  # check NAME LOG
  n=$(grep -c "saver: 1 widget drawn with the new dashboard" "$2")
  d=$(grep -c "sim sync: a new dashboard" "$2")
  off=$(grep -c "saver off" "$2")
  asan=$(grep -c "ERROR: AddressSanitizer\|LeakSanitizer" "$2")
  err=$(grep -c "App error" "$2")
  ok=PASS
  [ "$n" -ge 1 ] && [ "$d" -ge 2 ] && [ "$off" = 0 ] && [ "$asan" = 0 ] && [ "$err" = 0 ] || ok=FAIL
  say "$ok $1: dashboards $d, widget redrawn $n, saver off $off, sanitizer $asan, app errors $err"
}

# 1. The script simulator, both turns: the aquarium app, the saver over it, the switch, the widget redrawn.
for r in 0 1; do
  name=script-r$r
  mkdir -p "$OUT/$name"
  data "$OUT/$name/data"
  mock 4
  cat >"$OUT/$name/script.txt" <<EOF
rot $r
wait 1500
app vn.pawboop.tank
wait 3000
saver
wait 4000
shot before
wait 24000
shot after
mem end
quit
EOF
  SIM_DATA="$OUT/$name/data" SIM_SYNC_URL="http://127.0.0.1:$PORT/v1/device/sync" "$SIM" "$OUT/$name/script.txt" "$OUT/$name" \
    >"$OUT/$name/log.txt" 2>&1 || say "FAIL $name: the simulator stopped"
  check "$name" "$OUT/$name/log.txt"
done

# 2. Over the saver (0.6.2: the saver hides Home under it, the editor shows a still aquarium): a notification, the dog's
# pop-up by its wake word (PawBoop from sim/data-calls, SIM_FAKE_ASSIST=voice, SIM_MIC: a spoken question), the editor,
# then wake: Home again. Look at the pictures: r2-*.png.
name=round2
mkdir -p "$OUT/$name"
data "$OUT/$name/data" sim/data-calls
mock 4
cat >"$OUT/$name/script.txt" <<EOF
wait 2500
saver
wait 4000
shot r2-1-saver
notif post clock Alarm|Time to stretch||r2
wait 1500
shot r2-2-notif
WAKEFIRE
realwait 9000
shot r2-3-dog
wait 12000
shot r2-4-after-dog
saveredit
wait 2000
shot r2-5-editor
wake
wait 1500
shot r2-6-home
quit
EOF
SIM_FAKE_ASSIST=voice SIM_MIC=sim/bot/audio/question.wav SIM_DATA="$OUT/$name/data" SIM_SYNC_URL="http://127.0.0.1:$PORT/v1/device/sync" \
  "$SIM" "$OUT/$name/script.txt" "$OUT/$name" >"$OUT/$name/log.txt" 2>&1 || say "FAIL $name: the simulator stopped"
asan=$(grep -c "ERROR: AddressSanitizer\|LeakSanitizer" "$OUT/$name/log.txt"); err=$(grep -c "App error" "$OUT/$name/log.txt")
say "$([ "$asan" = 0 ] && [ "$err" = 0 ] && echo PASS || echo FAIL) $name: sanitizer $asan, app errors $err; $(grep -c 'talk: heard\|assistant: ' "$OUT/$name/log.txt") assistant lines, saver on/off $(grep -c 'saver on' "$OUT/$name/log.txt")/$(grep -c 'saver off' "$OUT/$name/log.txt")"
[ -n "${NOLIVE:-}" ] && { for p in "$OUT"/*/*.ppm; do [ -e "$p" ] && python3 -c "from PIL import Image; import sys; Image.open(sys.argv[1]).save(sys.argv[2])" "$p" "${p%.ppm}.png" && rm -f "$p"; done; for d in "$OUT"/*/data; do rm -rf "$d"; done; echo "pictures: $OUT"; exit 0; }

# 3. The live simulator at the board's speed (real time, the board's PSRAM model), headless: the same for 70 s.
name=live
mkdir -p "$OUT/$name"
data "$OUT/$name/data"
mock 6
cat >"$OUT/$name/script.txt" <<EOF
wait 2000
app vn.pawboop.tank
wait 4000
mem tank-app
saver
wait 5000
shot before
mem saver
wait 60000
shot after
mem end
quit
EOF
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SIM_DATA="$OUT/$name/data" SIM_SYNC_URL="http://127.0.0.1:$PORT/v1/device/sync" \
  "$LIVE" --out "$OUT/$name" "$OUT/$name/script.txt" >"$OUT/$name/log.txt" 2>&1 </dev/null || say "FAIL $name: the simulator stopped"
check "$name" "$OUT/$name/log.txt"
grep -h "^\[sim\] PSRAM\|mem \|tank.*fps\|tank: " "$OUT/$name/log.txt" | head -20 | sed 's/^/  /' | tee -a "$OUT/report.txt"
for p in "$OUT"/*/*.ppm; do [ -e "$p" ] && python3 -c "from PIL import Image; import sys; Image.open(sys.argv[1]).save(sys.argv[2])" "$p" "${p%.ppm}.png" && rm -f "$p"; done
for d in "$OUT"/*/data; do rm -rf "$d"; done
echo "pictures: $OUT"
