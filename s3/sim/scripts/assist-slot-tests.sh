#!/bin/sh
# The assistant and saver slot checks of lane 4b (docs/app-slots.md §16.1), as ai-assist ran them before the 0.6.0 RC:
#   sim/scripts/assist-slot-tests.sh SIM OUT       e.g. sim/scripts/assist-slot-tests.sh sim/build-rc35/espos-sim /tmp/slot
# SIM is a portrait 3.5" simulator build. Each check gets a fresh copy of sim/data-calls (PawBoop and its dogs on the
# card, the "Gâu" listener, the wake word on) in sim/data-slot, removed at the end. Needs
# examples/assistant-echo/dist/dev.espos.ai.echo-1.0.0-core.espk (espos build in examples/assistant-echo) and the
# aquarium's build-pkgs/espos/s3/vn.pawboop.tank-1.0.0-{core,bg35}.espk. No network: SIM_FAKE_ASSIST answers turns.
# What to look at: OUT/*/log.txt (grep "assistant: |talk: |slots:"), OUT/*/*.ppm, OUT/defaults/d-*.txt.
set -u
cd "$(dirname "$0")/../.."
SIM=${1:?usage: assist-slot-tests.sh SIM OUT}
OUT=${2:?usage: assist-slot-tests.sh SIM OUT}
D=sim/data-slot
ECHO=examples/assistant-echo/dist/dev.espos.ai.echo-1.0.0-core.espk
TANK="build-pkgs/espos/s3/vn.pawboop.tank-1.0.0-core.espk build-pkgs/espos/s3/vn.pawboop.tank-1.0.0-bg35.espk"
data() {  # data SLOT_ASSISTANT [tank]
  rm -rf $D && cp -R sim/data-calls $D
  python3 tools/espk.py --install-sim $D $ECHO --vol flash >/dev/null
  [ "${2:-}" = tank ] && python3 tools/espk.py --install-sim $D $TANK --vol sd >/dev/null
  python3 - "$D/settings.json" "$1" <<'PY'
import json, sys
p, a = sys.argv[1], sys.argv[2]
d = json.load(open(p)); d['slot.assistant'] = a; d.pop('slot.saver', None)
json.dump(d, open(p, 'w'), ensure_ascii=False)
PY
}
run() {  # run NAME SCRIPT ENV...
  name=$1 script=$2; shift 2
  mkdir -p "$OUT/$name"
  env SIM_DATA=$D "$@" timeout 300 "$SIM" "sim/scripts/$script" "$OUT/$name" > "$OUT/$name/log.txt" 2>&1
  echo "$name: exit $?"
}
rm -rf "$OUT" && mkdir -p "$OUT"
data dev.espos.ai.echo
run turns assist-turns.txt SIM_FAKE_ASSIST=voice
data vn.pawboop.pet
run calls assist-calls.txt SIM_FAKE_ASSIST=voice SIM_MIC=sim/bot/audio/question.wav
data vn.pawboop.pet tank
run defaults assist-defaults.txt
for f in "$OUT"/defaults/d-*.txt; do echo "$(basename "$f"): $(cat "$f")"; done
rm -rf $D
