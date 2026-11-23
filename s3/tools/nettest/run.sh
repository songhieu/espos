#!/bin/bash
# net.request end to end in the simulator (src/js/jsnet.cpp, app-platform-plan.md lane 1f):
#   tools/nettest/run.sh [path/to/espos-sim]
#   LANFORM=1 tools/nettest/run.sh ...   the address and the keys come through the OS's LAN page (src/lanform.cpp)
#     instead: a wrong PIN is refused, the right one saves all three, the page then refuses to be used again
# Starts tools/nettest/server.mjs, installs the test app (tools/nettest/app) into a copy of sim/data, runs it, then
# checks the results it posted, that every case passed, and that the test secrets never reached the simulator's output.
# ASan: build the simulator with EXTRA="-fsanitize=address -g" into its own BUILD dir and pass that binary.
set -euo pipefail
cd "$(dirname "$0")/../.."
SIM=${1:-sim/build/espos-sim}
PORT=${NETTEST_PORT:-8788}
OUT=$(mktemp -d "${TMPDIR:-/tmp}/nettest.XXXXXX")
node tools/nettest/server.mjs "$PORT" >"$OUT/server.log" 2>&1 &
SRV=$!
trap 'kill $SRV 2>/dev/null || true' EXIT
cp -R sim/data "$OUT/data"
python3 tools/espk.py tools/nettest/app "$OUT/nettest.espk" >/dev/null
python3 tools/espk.py --install-sim "$OUT/data" "$OUT/nettest.espk" --vol flash >/dev/null
if [ -z "${LANFORM:-}" ]; then
  cat >"$OUT/script.txt" <<EOF
set app.dev.espos.nettest.base http://127.0.0.1:$PORT
secret dev.espos.nettest ck ck_test
secret dev.espos.nettest cs cs_test
app dev.espos.nettest $PORT
realwait 14000
shot nettest
quit
EOF
else
  cat >"$OUT/script.txt" <<EOF
lanform dev.espos.nettest
realwait 1500
shot lanform
realwait 12000
home
wait 800
app dev.espos.nettest $PORT
realwait 14000
shot nettest
quit
EOF
fi
for i in $(seq 1 50); do curl -s "http://127.0.0.1:$PORT/results" >/dev/null && break; sleep 0.1; done
if [ -z "${LANFORM:-}" ]; then
  SIM_DATA="$OUT/data" "$SIM" "$OUT/script.txt" "$OUT" >"$OUT/sim.log" 2>&1 || { echo "simulator failed:"; tail -30 "$OUT/sim.log"; exit 1; }
else
  LP=${LANFORM_PORT:-18080}
  SIM_LANFORM_PIN=246810 SIM_LANFORM_PORT=$LP SIM_DATA="$OUT/data" "$SIM" "$OUT/script.txt" "$OUT" >"$OUT/sim.log" 2>&1 &
  SIMPID=$!
  F="http://127.0.0.1:$LP"
  for i in $(seq 1 100); do curl -s "$F/" >"$OUT/lan-page.html" 2>/dev/null && [ -s "$OUT/lan-page.html" ] && break; sleep 0.1; done
  curl -s --data-urlencode pin=000000 --data-urlencode ck=wrong "$F/save" >"$OUT/lan-wrong.html"
  curl -s --data-urlencode "base=http://127.0.0.1:$PORT" --data-urlencode ck=ck_test --data-urlencode cs=cs_test \
    --data-urlencode pin=246810 "$F/save" >"$OUT/lan-save.html"
  curl -s "$F/" >"$OUT/lan-again.html" || true
  wait $SIMPID || { echo "simulator failed:"; tail -30 "$OUT/sim.log"; exit 1; }
  python3 - "$OUT" <<'PY'
import sys
out = sys.argv[1]
page, wrong, save, again = (open(f"{out}/lan-{n}.html", errors="replace").read() for n in ("page", "wrong", "save", "again"))
checks = [
    ("the page asks for the address, both keys and the PIN", all(f'name="{k}"' in page for k in ("base", "ck", "cs")) and "name=pin" in page),
    ("a wrong PIN is refused", "Wrong PIN" in wrong),
    ("the right PIN saves 3 fields", "(3)" in save),
    ("the page is closed after one save", "used already" in again),
    ("the trace has the save, not the values", "lanform: dev.espos.nettest, 3 fields saved" in open(f"{out}/sim.log", errors="replace").read()),
]
for name, ok in checks:
    print(("pass " if ok else "FAIL ") + "lanform: " + name)
sys.exit(0 if all(ok for _, ok in checks) else 1)
PY
fi
curl -s "http://127.0.0.1:$PORT/results" >"$OUT/results.json"
python3 - "$OUT" <<'EOF'
import json, sys
out = sys.argv[1]
r = json.load(open(f"{out}/results.json"))
rep = (r.get("report") or {}).get("results") or []
bad = [x for x in rep if not x["pass"]]
for x in rep:
    print(("pass " if x["pass"] else "FAIL ") + x["name"] + ("" if x["pass"] else f"  (got {x.get('got')}: {x.get('msg', '')})"))
log = open(f"{out}/sim.log", errors="replace").read()
leak = [s for s in ("ck_test", "cs_test", "Y2tfdGVzdDpjc190ZXN0") if s in log]
print(f"{len(rep) - len(bad)} of {len(rep)} passed; secrets in the simulator's output: {leak or 'none'}; out: {out}")
sys.exit(0 if rep and not bad and not leak else 1)
EOF
