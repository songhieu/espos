#!/bin/bash
# The Sessions app in the simulator against the local mock of the server's Sessions routes (mock_server.py):
#
#   apps/sessions/test/run.sh                    every scenario (pictures + checks), simulator sim/build-sessions/espos-sim
#   SHOTS="list term perm" ROT="1 0" apps/sessions/test/run.sh       some screens, landscape (1) and portrait (0)
#   LANGS="vi de" ...                            the same screens in other languages (their packages from build-pkgs/)
#   THEME=dark ...                               the OS theme (s3.theme: color, dark, light)
#   SIM=sim/build-x/espos-sim OUT=/tmp/x ...     another simulator build / output folder
# The app's requests carry the simulator's device credential (src/js/jsnet.cpp deviceAuth under SIM): the mock
# refuses anything else on its routes, and the authtest scenario checks it never goes anywhere else.
#
# Build the simulator with AddressSanitizer first:
#   make -C sim -j4 BUILD=build-sessions EXTRA="-fsanitize=address -fno-omit-frame-pointer"
# Output: OUT/<name>.png pictures, OUT/<name>.log the simulator's logs (ASan reports end up there), OUT/report.txt.
set -euo pipefail
cd "$(dirname "$0")/../../.."
SIM=${SIM:-sim/build-sessions/espos-sim}
OUT=${OUT:-$(mktemp -d "${TMPDIR:-/tmp}/sessions.XXXXXX")}
PORT=${PORT:-8790}
mkdir -p "$OUT"
python3 apps/sessions/tools/build.py ${DEV:+--dev} >"$OUT/build.txt"
python3 apps/sessions/tools/package.py "$OUT/sessions.espk" >>"$OUT/build.txt"
# MOCK_SERVER=old: the server before the 11-22 latency work (400 ms screen loop, keys wait for the Mac, no "keys")
MOCK_SERVER=${MOCK_SERVER:-new} python3 apps/sessions/test/mock_server.py --port "$PORT" >"$OUT/mock.log" 2>&1 &
MOCK=$!
trap 'kill $MOCK 2>/dev/null || true' EXIT
for i in $(seq 1 50); do curl -s "http://127.0.0.1:$PORT/mock/log" >/dev/null && break; sleep 0.1; done
python3 tools/espk.py apps/sessions/test/authtest "$OUT/authtest.espk" >/dev/null
API="http://127.0.0.1:$PORT"
: >"$OUT/report.txt"
say() { echo "$*" | tee -a "$OUT/report.txt"; }

scene() { curl -s -X POST -d "{\"name\":\"$1\"}" "$API/mock/scene" >/dev/null; }
langdata() {  # a copy of the simulator's data with a language's packages (the language and its font)
  local d="$OUT/base-$1"
  if [ ! -d "$d" ]; then
    cp -R sim/data "$d"
    local font
    font=$(python3 -c "import sys; sys.path.insert(0, 'tools'); import i18n; print(next((l[5] or '') for l in i18n.LANGS if l[0] == sys.argv[1]))" "$1")
    for p in "dev.espos.lang.$1" $font; do
      local f
      f=$(ls "${LANGDIR:-build-pkgs/espos/s3}"/"$p"-*.espk build-pkgs/espos/s3/"$p"-*.espk 2>/dev/null | head -1)
      [ -n "$f" ] || { echo "no package $p in build-pkgs/espos/s3 (tools/i18n.py build all)" >&2; exit 1; }
      python3 tools/espk.py --install-sim "$d" "$f" --vol flash >/dev/null
    done
  fi
  echo "$d"
}
# run NAME LANG SCRIPT: a fresh copy of the data, the app installed, its token, the script, then quit.
run() {
  local base=sim/data
  [ "$2" = en ] || base=$(langdata "$2")
  rm -rf "$OUT/data-$1" && cp -R "$base" "$OUT/data-$1"
  python3 tools/espk.py --install-sim "$OUT/data-$1" "$OUT/sessions.espk" --vol flash >/dev/null
  [ -z "${NOTES:-}" ] || SIM_CLOCK="${CLOCK:-}" python3 apps/sessions/test/notes.py "$OUT/data-$1/dash.json" "$NOTES"
  # the voice of spoken announcements: the simulator answers GET /v1/device/tts from net/_v1_device_tts (a short tone)
  mkdir -p "$OUT/data-$1/net" && python3 -c "
import math, struct, sys
open(sys.argv[1], 'wb').write(b''.join(struct.pack('<h', int(6000 * math.sin(i * 2 * math.pi * 440 / 16000))) for i in range(8000)))
" "$OUT/data-$1/net/_v1_device_tts"
  if [ -n "${TTS_STATUS:-}" ]; then  # TTS_STATUS=402|429: the voice refused (no AI licence, the day's 60 used)
    echo "$TTS_STATUS" >"$OUT/data-$1/net/_v1_device_tts.status"
    echo '{"error":"lic","locked":"no_ai","buy_url":"https://espos.dev/buy"}' >"$OUT/data-$1/net/_v1_device_tts"
  fi
  { [ -z "${PRE:-}" ] || echo "$PRE" | tr ';' '\n'  # PRE: script lines before anything else (e.g. set s3.dnd 1)
    echo "set sys.lang $2"
    [ -z "${THEME:-}" ] || echo "set s3.theme $THEME"  # THEME=dark|light|color: the OS theme
    echo "set dash.quiet_from 00:00"  # no quiet hours (the simulator runs on the computer's clock); popquiet sets DND
    echo "set dash.quiet_to 00:00"
    echo "wait 2000"
    echo "$3"; echo quit; } >"$OUT/$1.txt"
  python3 tools/espk.py --install-sim "$OUT/data-$1" "$OUT/authtest.espk" --vol flash >/dev/null
  local env=(SIM_API_BASE="$API" SIM_DATA="$OUT/data-$1" SIM_SYNC_URL="$API/v1/device/sync")
  [ -z "${CLOCK:-}" ] || env+=(SIM_CLOCK="$CLOCK")  # CLOCK="2026-11-22 14:30:00": the device's wall clock (night, quiet hours)
  env "${env[@]}" ${SIMENV:-} "$SIM" "$OUT/$1.txt" "$OUT" >"$OUT/$1.log" 2>&1 || { say "FAIL $1: the simulator stopped"; tail -30 "$OUT/$1.log"; return 1; }
  if grep -q "ERROR: AddressSanitizer\|LeakSanitizer" "$OUT/$1.log"; then say "FAIL $1: AddressSanitizer"; grep -A12 "ERROR: AddressSanitizer" "$OUT/$1.log" | head -20; fi
  grep -h "App error" "$OUT/$1.log" | sed "s/^/FAIL $1: /" | tee -a "$OUT/report.txt" || true
  for p in "$OUT"/*.ppm; do [ -e "$p" ] && python3 -c "from PIL import Image; import sys; Image.open(sys.argv[1]).save(sys.argv[2])" "$p" "${p%.ppm}.png" && rm -f "$p"; done
  return 0
}
log() { curl -s "$API/mock/log"; }
# sends [FROM]: how many /send requests reached the mock since the FROM-th one, and what they were
sends() { log | python3 -c "
import json, sys
l = [x for x in json.load(sys.stdin)['log'] if x.get('route') == 'send'][int(sys.argv[1]):]
print(len(l), [(x.get('kind'), x.get('key') or x.get('keys') or '') for x in l])" "${1:-0}"; }
# lockcheck NAME SCENE ARG STEPS MIN MAX WANT [NOT]: per turn, MIN..MAX sends, WANT (a regex) in the run's log, NOT absent
lockcheck() {
  local name=$1 sc=$2 arg=$3 steps=$4 lo=$5 hi=$6 want=$7 not=${8:-}
  for r in ${ROT:-1 0}; do
    local a b n ok=PASS
    a=$(sends | cut -d' ' -f1)
    LANGS=en shot "$name" "$sc" "$arg" "$steps" "$r"
    b=$(sends "$a"); n=${b%% *}
    { [ "$n" -ge "$lo" ] && [ "$n" -le "$hi" ]; } || ok=FAIL
    grep -Eq "$want" "$OUT/$name-en-r$r.log" || ok=FAIL
    [ -z "$not" ] || ! grep -Eq "$not" "$OUT/$name-en-r$r.log" || ok=FAIL
    say "$ok $name r$r: sends $b; $(grep -Eo "$want" "$OUT/$name-en-r$r.log" | sort | uniq -c | tr -s ' ' | tr '\n' ' ')"
  done
}

# tapat/downat labels in the language of the run: the app's own texts (i18n.json), else the OS's (i18n/<lang>.json);
# "os:LABEL" takes the OS's (the pop-up card is the OS's: its Approve is not the app's in every language)
localize() {  # localize LANG STEPS
  [ "$1" = en ] && { echo "$2" | sed -E 's/(tapat|downat) os:/\1 /g'; return; }
  python3 - "$1" "$2" <<'PY'
import json, re, sys
lang, steps = sys.argv[1], sys.argv[2]
app = json.load(open("apps/sessions/i18n.json")).get(lang, {})
os_ = json.load(open(f"i18n/{lang}.json"))
def word(w):  # "os:Approve": the OS's own word (the pop-up card's buttons), else the app's first
    if w.startswith("os:"):
        return os_.get(w[3:]) or w[3:]
    return app.get(w) or os_.get(w) or w
print(re.sub(r"\b(tapat|downat) ([^;]+)", lambda m: m.group(1) + " " + word(m.group(2)), steps))
PY
}

# The screens: SCENE ARG STEPS (ARG: app.arg, "900:s-1" opens that session; STEPS: script lines, ; between them)
shot() {  # shot NAME SCENE ARG STEPS [ROTATIONS]
  local name=$1 sc=$2 arg=$3 steps=$4 rots=${5:-${ROT:-1 0}}
  for lang in ${LANGS:-en}; do
    for r in $rots; do
      local n="$name-$lang-r$r"
      scene "$sc"
      run "$n" "$lang" "rot $r
wait 500
app dev.espos.sessions $arg
realwait 2500
$(localize "$lang" "$steps" | tr ';' '\n')
shot $n
overflow $n" || true
    done
  done
}

ALL="authtest list term perm risky typing kb keys useanswer hold zoom offline view notallowed notallowedperm disallowed disallowedlist turnedon oddstatus estop notpaired disabled empty shareoff locked fast pophome popsaver popapp poprisky popquiet popcenter popapprove popnotallowed popodd popgone"
# the prompt row's mic (an icon, no word to tap): its centre per turn (r1 landscape, r0 portrait)
mic() { [ "$1" = 0 ] && echo "251 458" || echo "411 298"; }
# A pop-up while the app is closed (NOTES: the server's notes in the dashboard, apps/sessions/test/notes.py).
popup() {  # popup NAME NOTES STEPS [ROTATIONS]       POPSCENE: the mock's scene (normal)
  local name=$1 kind=$2 steps=$3 rots=${4:-${ROT:-1 0}}
  for lang in ${LANGS:-en}; do
    for r in $rots; do
      local n="$name-$lang-r$r"
      scene "${POPSCENE:-normal}"
      NOTES=$kind run "$n" "$lang" "rot $r
wait 1500
$(localize "$lang" "$steps" | tr ';' '\n')
shot $n
overflow $n" || true
    done
  done
}
for s in ${SHOTS:-$ALL}; do
  case $s in
    authtest)  # the device credential: only to the calling app's own path
      scene normal
      run authtest en "app dev.espos.authtest
realwait 5000
shot authtest" || true
      log | python3 -c "
import json, sys
log = json.load(sys.stdin)['log']
echo = [x for x in log if x.get('route') == 'echo']
rep = [x for x in echo if x['path'].endswith('/report')]
res = rep[-1]['body']['results'] if rep else []
bad = [r for r in res if r['got'] != r['want']]
creds = {x['path']: x['auth'] for x in echo if not x['path'].endswith('/report')}
leak = [p for p, a in creds.items() if a and not p.startswith('/v1/device/app/dev.espos.authtest/')]
ok = len(res) == 10 and not bad and not leak and creds.get('/v1/device/app/dev.espos.authtest/echo') == 'Device' \
     and creds.get('/v1/device/app/dev.espos.authtest/echo/plain') == ''
print(('PASS' if ok else 'FAIL') + ' authtest:', len(res), 'cases,', 'wrong:', bad, 'credential outside its path:', leak)
" | tee -a "$OUT/report.txt" ;;
    list) shot list normal "" "" ;;
    term) shot term normal 900:s-3 "realwait 2000" ;;
    perm) shot perm normal 900:s-1 "" ;;
    risky) shot risky risky_perm 900:s-1 "tapat Approve;realwait 800" ;;
    typing) shot typing normal 900:s-2 "tapat Message Claude;realwait 800;type update the docs too;realwait 600" ;;
    kb) shot kb normal 900:s-3 "tapat Message Claude;realwait 1200" ;;
    keys)  # taps while a send is out go together as one kind "keys"
          shot keys normal 900:s-3 "tapat Message Claude;realwait 900;tapat Tab;tapat Tab;tapat Mode;tapat Enter;realwait 2500" 1
          say "keys: $(log | python3 -c "
import json, sys
l = [x for x in json.load(sys.stdin)['log'] if x.get('route') == 'send']
print([(x.get('kind'), x.get('key') or x.get('keys')) for x in l])")" ;;
    useanswer)  # a permission waits: Enter and Mode are answered "use Approve or Deny" (the app, then the server)
          shot useanswer normal 900:s-1 "tapat Message Claude;realwait 900;tapat Enter;realwait 300" 1
          shot useanswer2 normal 900:s-1 "tapat Message Claude;realwait 900;tapat Mode;realwait 1200" 1
          say "use answer: $(grep -h 'Use Approve or Deny' "$OUT"/useanswer*-r1.log | head -2 | tr '\n' ' ')$(log | python3 -c "
import json, sys
print([x.get('key') for x in json.load(sys.stdin)['log'] if x.get('route') == 'send'])")" ;;
    hold)  # Esc goes only after a 1 s hold, with ack_risky
          shot hold normal 900:s-3 "tapat Message Claude;realwait 900;tapat Esc;realwait 400;downat Esc;realwait 1300;up;realwait 800" 1
          say "hold: $(log | python3 -c "
import json, sys
print([(x.get('key'), x.get('ack_risky')) for x in json.load(sys.stdin)['log'] if x.get('route') == 'send'])")" ;;
    zoom) shot zoom normal 900:s-3 "tapat A+;realwait 400;tapat A+;realwait 800" ;;
    offline) shot offline offline "" "" ;;
    view) shot view view 900:s-1 "" ;;
    notallowed)  # keys refused by the Mac's device list (403 sender_not_allowed): one clear reason, the waiting keys
                 # dropped, the key row and the prompt gone (a tap on Tab finds none), no retries for 6 s
          lockcheck notallowed notallowed 900:s-3 "tapat Message Claude;realwait 900;tapat Tab;tapat Tab;tapat Mode;tapat Enter;realwait 2000;tapat Tab;realwait 6000" \
            1 2 'refused 403 sender_not_allowed|tapat: no "Tab" on screen' ;;
    notallowedperm)  # Approve refused the same way: Approve/Deny give way to the reason; "Try again" lets one more go
          lockcheck notallowedperm notallowed 900:s-1 "tapat Approve;realwait 3000" 1 1 'refused 403 sender_not_allowed'
          lockcheck tryagain notallowed 900:s-1 "tapat Approve;realwait 2500;tapat Try again;realwait 800;tapat Approve;realwait 2500" \
            2 2 'refused 403 sender_not_allowed' ;;
    disallowed)  # the server says macs[].allowed false: locked from the start, nothing sent, no Try again
          lockcheck disallowed disallowed 900:s-1 "tapat Approve;realwait 1500;tapat Try again;realwait 800" \
            0 0 'tapat: no "Approve" on screen' 'tapat "Try again" wall' ;;
    disallowedlist) lockcheck disallowedlist disallowed "" "tapat Approve;realwait 1000" 0 0 'tapat: no "Approve" on screen' ;;
    turnedon)  # refused once, then turned on at the Mac (macs[].allowed true): Approve is back by itself and goes
          lockcheck turnedon turnedon 900:s-1 "tapat Approve;realwait 4000;tapat Approve;realwait 2500" 2 2 'refused 403 sender_not_allowed' ;;
    lockshots)  # the two locked screens in LANGS (the pictures only): after a refused Approve, and the list with allowed false
          shot lockperm notallowed 900:s-1 "tapat Approve;realwait 3000"
          shot locklist disallowed "" "" ;;
    oddstatus)  # an answer with no known code (503): "Not sent (503).", the waiting keys dropped, not locked
          lockcheck oddstatus oddstatus 900:s-3 "tapat Message Claude;realwait 900;tapat Tab;tapat Tab;tapat Mode;realwait 2500;tapat Tab;realwait 700" \
            2 3 'refused 503 -' 'tapat: no "Tab" on screen' ;;
    estop) shot estop estop "" "" ;;
    notpaired) shot notpaired not_paired "" "" ;;
    disabled) shot disabled disabled "" "" ;;
    empty) shot empty empty "" "" ;;
    shareoff) shot shareoff share_off 900:s-2 "" ;;
    locked) shot locked locked 900:s-2 "tapat Message Claude;realwait 800;type ok do it;realwait 300;tapat AI;realwait 2500" ;;
    fast) shot fast fast 900:s-3 "realwait 8000" ;;
    fastlong) shot fastlong fast 900:s-3 "realwait 60000" 1
          grep -h "lines kept, the oldest go" "$OUT"/fastlong-*.log | head -1 | sed "s/^/ring: /" | tee -a "$OUT/report.txt" ;;
    dictate) [ -f "$OUT/dictate.wav" ] || /usr/bin/say -o "$OUT/dictate.wav" --data-format=LEI16@16000 "update the docs too and add a curl example"
          for r in 1 0; do SIMENV="SIM_MIC=$OUT/dictate.wav" shot dictate normal 900:s-2 "down $(mic $r);realwait 3500;up;realwait 3000" $r; done
          say "dictation: $(log | python3 -c "import json,sys; l=[x for x in json.load(sys.stdin)['log'] if x.get('route')=='dictate']; print(l[-1] if l else 'none')")" ;;
    latency)  # what the device and the server add: a line a second from the "Mac", Mode every 3 s. MOCK_SERVER=old|new,
              # OLDAPP=1 for an app from before the 11-22 work (its Keys bar). Times on the wall clock: the mock's and the
              # simulator's tap lines ("tapat ... wall <ms>").
          if [ -n "${OLDAPP:-}" ]; then steps="tapat Keys;realwait 600"; else steps="tapat Message Claude;realwait 900"; fi
          for i in 1 2 3 4 5 6 7 8 9 10; do steps="$steps;tapat Mode;realwait 3000"; done
          shot latency latency 900:s-3 "$steps;realwait 2500" 1
          curl -s "$API/mock/latency" > "$OUT/latency.json"
          python3 - "$OUT/latency.json" "$OUT/latency-en-r1.log" "${MOCK_SERVER:-new}" "${OLDAPP:+old}" <<'PY' | tee -a "$OUT/report.txt"
import json, re, sys
lat = json.load(open(sys.argv[1]))
taps = [int(m.group(1)) for m in re.finditer(r'tapat "Mode" wall (\d+)', open(sys.argv[2]).read())]
sends = [r[0] for r in lat["req"] if r[1] == "send"]
keyl = lat["key_lines"]  # [made (= the send arrived), first sent to the device]
def st(xs):
    xs = sorted(xs)
    return "avg %d, p90 %d, max %d ms (%d)" % (sum(xs) / len(xs), xs[int(len(xs) * .9)], xs[-1], len(xs)) if xs else "none"
d_send, d_eff = [], []
for t in taps:
    s = next((x for x in sends if x >= t), None)
    k = next((x for x in keyl if x[0] >= t and x[1]), None)
    if s: d_send.append(s - t)
    if k: d_eff.append(k[1] - t)
la = lat["line_age"]
print("latency, server %s, app %s:" % (sys.argv[3], sys.argv[4] or "new"))
print("  new line made -> sent to the S3: avg %s, p90 %s, max %s ms (%s lines)" % (la["avg"], la["p90"], la["max"], la["n"]))
print("  tap -> key at the server: " + st(d_send))
print("  tap -> its effect sent to the S3: " + st(d_eff))
PY
          grep -h "latency:" "$OUT"/latency-*.log | sed "s/^/  app's own line: /" | tee -a "$OUT/report.txt" || true ;;
    pophome) popup pophome perm "wait 500" ;;
    announce) CLOCK="2026-11-22 14:30:00" popup announce perm "wait 5000" 1
              grep -h "popnote: saying\|popnote: speaking\|popnote: no voice" "$OUT"/announce-*.log | sed "s/^/announce: /" | tee -a "$OUT/report.txt" || true ;;
    announcemerge) CLOCK="2026-11-22 14:30:00" popup announcemerge two "wait 5000" 1
              grep -h "popnote: saying" "$OUT"/announcemerge-*.log | sed "s/^/announce merged: /" | tee -a "$OUT/report.txt" || true ;;
    announcelock)  # the voice refused (402/429): asked once, then not again until the next UTC day or a licence change
      d="$OUT/announcelock-dash"; rm -rf "$d"; mkdir -p "$d"
      for k in wait:"2026-11-22 14:32:30" next:"2026-11-23 14:40:00" late:"2026-11-23 14:42:30"; do
        cp sim/data/dash.json "$d/${k%%:*}.json"; SIM_CLOCK="${k#*:}" python3 apps/sessions/test/notes.py "$d/${k%%:*}.json" "${k%%:*}"
      done
      scene normal
      TTS_STATUS=402 CLOCK="2026-11-22 14:30:00" NOTES=perm run announcelock en "wait 6000
wait 125000
dash $d/wait.json
wait 6000
clock 2026-11-23 14:40:00
wait 125000
dash $d/next.json
wait 6000
lock lic {\"ai\":\"off\"}
wait 125000
dash $d/late.json
wait 6000
shot announcelock" || true
      grep -h "popnote: saying\|popnote: no voice\|popnote: not said" "$OUT/announcelock.log" | sed "s/^/announce refused: /" | tee -a "$OUT/report.txt" || true ;;
    announcequiet) PRE="set s3.dnd 1" CLOCK="2026-11-22 14:30:00" popup announcequiet perm "wait 5000" 1
              grep -h "popnote: saying\|popnote: not said" "$OUT"/announcequiet-*.log | sed "s/^/announce in DND: /" | tee -a "$OUT/report.txt" || true ;;
    status) popup status status "wait 800;notif center;wait 800" ;;
    awake) shot awake normal 900:s-3 "set s3.saver 8;wait 20000" 1
           say "awake: $(grep -c 'saver on' "$OUT"/awake-*.log | head -1) saver starts over the open app (want 0)" ;;
    voicesend) [ -f "$OUT/dictate.wav" ] || /usr/bin/say -o "$OUT/dictate.wav" --data-format=LEI16@16000 "update the docs too and add a curl example"
          SIMENV="SIM_MIC=$OUT/dictate.wav" shot voicesend normal 900:s-2 "down $(mic 1);realwait 3500;up;realwait 9000" 1
          say "voice send: $(log | python3 -c "
import json, sys
l = json.load(sys.stdin)['log']
d = [(x.get('expect') or 'text') for x in l if x.get('route') == 'dictate']
s = [x.get('text') for x in l if x.get('route') == 'send']
print('dictate', d, 'then sent', s[-1:] if s else 'nothing')")" ;;
    popsaver) popup popsaver perm "saver;wait 800" ;;
    popapp) popup popapp two "app weather;wait 800" ;;
    poprisky) popup poprisky risky "wait 500;tapat os:Approve;wait 400" ;;
    popquiet) popup popquiet perm "set s3.dnd 1;wait 800;home;wait 500" ;;
    popcenter) popup popcenter perm "set s3.dnd 1;wait 800;notif center;wait 800" ;;
    popnotallowed|popodd|popgone)  # the pop-up's Approve refused (0.6.3, popnote refusal()): the reason on the card in
                                   # words (403 sender_not_allowed, 200 refused req_gone), a 503 with no code only "Not done"
      sc=notallowed want="not done (403 sender_not_allowed)"
      [ $s = popodd ] && sc=oddstatus want="not done (503 -)"
      [ $s = popgone ] && sc=reqgone want="not done (200 req_gone)"
      POPSCENE=$sc popup $s perm "wait 500;tapat os:Approve;wait 400;tapat os:Approve;realwait 1200"
      for f in "$OUT"/$s-*.log; do
        ok=FAIL; grep -q "popnote: $want" "$f" && ! grep -q "AddressSanitizer\|App error" "$f" && ok=PASS
        say "$ok $(basename "$f" .log): $(grep -o "popnote: not done ([^)]*)" "$f" | head -1)"
      done ;;
    popapprove) popup popapprove perm "wait 500;tapat os:Approve;wait 400;tapat os:Approve;realwait 2500"
                say "approve from the pop-up: $(log | python3 -c "import json,sys; l=[x for x in json.load(sys.stdin)['log'] if x.get('route')=='send']; print(l[-1] if l else 'none')")" ;;
  esac
done
say "pictures: $OUT"
