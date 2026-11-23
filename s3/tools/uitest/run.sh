#!/bin/bash
# Screens of the level-2 UI nodes (tools/uitest/app) in the simulator: tools/uitest/run.sh [espos-sim] [OUTDIR]
set -euo pipefail
cd "$(dirname "$0")/../.."
SIM=${1:-sim/build/espos-sim}
OUT=${2:-$(mktemp -d "${TMPDIR:-/tmp}/uitest.XXXXXX")}
mkdir -p "$OUT"
rm -rf "$OUT/data" && cp -R sim/data "$OUT/data"
python3 tools/espk.py tools/uitest/app "$OUT/uitest.espk" >/dev/null
python3 tools/espk.py --install-sim "$OUT/data" "$OUT/uitest.espk" --vol flash >/dev/null
cat >"$OUT/script.txt" <<EOS
rot 1
app dev.espos.uitest
wait 1500
shot land-sell
tap 300 120
wait 800
shot land-tap
app dev.espos.uitest sheet
wait 1500
shot land-sheet
backswipe
wait 800
shot land-back
app dev.espos.uitest orders
wait 1200
shot land-orders
rot 0
app dev.espos.uitest
wait 1500
shot port-sell
quit
EOS
SIM_DATA="$OUT/data" "$SIM" "$OUT/script.txt" "$OUT" >"$OUT/sim.log" 2>&1 || { tail -30 "$OUT/sim.log"; exit 1; }
for f in "$OUT"/*.ppm; do python3 -c "from PIL import Image; import sys; Image.open(sys.argv[1]).save(sys.argv[1][:-4] + '.png')" "$f"; done
grep -E "App error|app dev.espos.uitest" "$OUT/sim.log" | head -20 || true
echo "$OUT"
