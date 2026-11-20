#!/bin/sh
# EspOS Bot simulator (docs/bot.md): the board's own code (src/bot) in a window, with this Mac's microphone and
# speaker, this terminal as the USB line, the setup page at http://localhost:8099, and a local stand-in server
# (tools/bot/fixture_server.py on 127.0.0.1:8098: canned replies, the Mac's own voice; nothing leaves the Mac).
#   tools/bot/sim.sh                      the window (Space = BOOT, S = picture, Q = quit)
#   tools/bot/sim.sh --provision          linked to the stand-in server first (as the USB installer would)
#   tools/bot/sim.sh --reset              a new bot: no WiFi, no link, no settings (the setup WiFi page opens)
#   tools/bot/sim.sh --headless SCRIPT    no window: SCRIPT's lines (sim/bot/scripts/*.txt) -> sim/bot/out/<name>/
#   BOT_SIM_LIVE=1 tools/bot/sim.sh       the real api.espos.dev (live AI and TTS calls: only with an OK)
#   BOT_SIM_TRACE=1                       the trace in the terminal as it happens
set -e
cd "$(dirname "$0")/../.."
DATA=${BOT_SIM_DATA:-sim/bot/data-live}
ARGS=""
PROVISION=0
while [ $# -gt 0 ]; do
  case "$1" in
    --reset) rm -rf "$DATA"; shift ;;
    --provision) PROVISION=1; shift ;;
    --headless) NAME=$(basename "$2" .txt); OUT=sim/bot/out/$NAME; rm -rf "$OUT"; mkdir -p "$OUT"
      export BOT_SIM_PORTAL_PORT=${BOT_SIM_PORTAL_PORT:-8199}  # a window may hold 8099
      DATA=${BOT_SIM_DATA:-sim/bot/data-$NAME}; rm -rf "$DATA"; ARGS="$ARGS --headless $2 --out $OUT"; shift 2 ;;
    *) ARGS="$ARGS $1"; shift ;;
  esac
done
make -C sim/bot -j10 >/dev/null
if [ ! -f sim/bot/audio/question.wav ] && command -v say >/dev/null; then  # a question in the Mac's own voice
  mkdir -p sim/bot/audio
  say -v Linh -o sim/bot/audio/q.aiff "Hôm nay trời thế nào?" 2>/dev/null || say -o sim/bot/audio/q.aiff "How is the weather today?"
  afconvert -f WAVE -d LEI16@16000 -c 1 sim/bot/audio/q.aiff sim/bot/audio/question.wav && rm -f sim/bot/audio/q.aiff
fi
if [ -z "$BOT_SIM_LIVE" ] && ! curl -s -m 1 http://127.0.0.1:8098/_fixture/state >/dev/null 2>&1; then
  python3 tools/bot/fixture_server.py --port 8098 >/dev/null 2>&1 &
  FIX=$!
  trap 'kill $FIX 2>/dev/null' EXIT INT TERM
  for i in 1 2 3 4 5 6 7 8 9 10; do curl -s -m 1 http://127.0.0.1:8098/_fixture/state >/dev/null 2>&1 && break; sleep 0.2; done
fi
mkdir -p "$DATA"
if [ "$PROVISION" = 1 ]; then  # as the USB installer: a token, the WiFi, the server (the stand-in's fixture bot)
  TOK=$(printf 'b0%.0s' $(seq 32))
  printf 'bot.token\t%s\nbot.api\thttp://127.0.0.1:8098\nbot.wifi\t%s\nbot.lang\t1\n' "$TOK" \
    "$(printf '[{"s":"Home WiFi","p":"x"}]' | xxd -p | tr -d '\n')" >> "$DATA/nvs.txt"
fi
RC=0
BOT_SIM_DATA="$DATA" sim/bot/build-sim/bot-sim $ARGS || RC=$?
case "$ARGS" in *--headless*) python3 tools/bot/shots.py "$OUT" "$OUT" >/dev/null && echo "pictures: $OUT/ui.html";; esac
exit $RC
