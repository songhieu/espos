#!/usr/bin/env bash
# The dog assistant end to end: a fresh EspOS server (its own data folder) wired to this kit's gateway, then
# assist_bench.py plays fixtures/turns.json through it.
#
#   bench/run_e2e.sh <label> [full|short] [--voice | --voice-only]
#
# Environment (defaults in brackets):
#   GATEWAY      the kit's gateway                      [http://127.0.0.1:8080]
#   KEY_FILE     a file holding one gateway API key      (required)
#   GATEWAY_LOG  the gateway's log, for token counts     [logs/gateway.log]
#   ESPOS_SRC    the EspOS server source                 [../../server: this repository's]
#   WORK         where the server binary and data go     [bench/work]
#   LLM_MODEL / STT_MODEL / TTS_MODEL  ids in config.yaml [gemma-4-e2b / whisper / vieneu-turbo]
#   AI_TIMEOUT   ESPOS_AI_TIMEOUT                         [60]
set -euo pipefail
cd "$(dirname "$0")"
label=${1:?usage: run_e2e.sh <label> [full|short] [--voice|--voice-only]}
prompt=${2:-short}
shift $(( $# >= 2 ? 2 : 1 ))

GATEWAY=${GATEWAY:-http://127.0.0.1:8080}
KEY_FILE=${KEY_FILE:?set KEY_FILE to a file with a gateway API key}
GATEWAY_LOG=${GATEWAY_LOG:-../logs/gateway.log}
ESPOS_SRC=${ESPOS_SRC:-$(cd ../.. && pwd)/server}
WORK=${WORK:-$(pwd)/work}
PORT=${ESPOS_PORT:-8095}
mkdir -p "$WORK"
rm -rf "$WORK/data-$label"
mkdir -p "$WORK/data-$label"
[[ -f "$WORK/pw.txt" ]] || { openssl rand -hex 16 > "$WORK/pw.txt"; chmod 600 "$WORK/pw.txt"; }

echo "==> build the EspOS server"
(cd "$ESPOS_SRC" && go build -o "$WORK/espos" ./cmd/espos)

key=$(cat "$KEY_FILE")
short=""
[[ $prompt == short ]] && short=short
echo "==> start it (prompt: $prompt)"
ESPOS_ADDR=127.0.0.1:$PORT ESPOS_DATA="$WORK/data-$label" ESPOS_PASSWORD="$(cat "$WORK/pw.txt")" \
ESPOS_AI_URL="$GATEWAY/v1" ESPOS_AI_KEY="$key" ESPOS_AI_MODEL="${LLM_MODEL:-gemma-4-e2b}" \
ESPOS_AI_PROMPT="$short" ESPOS_AI_TIMEOUT="${AI_TIMEOUT:-60}" \
ESPOS_STT_URL="$GATEWAY/v1" ESPOS_STT_KEY="$key" ESPOS_STT_MODEL="${STT_MODEL:-whisper}" ESPOS_STT_LANG=vi \
ESPOS_TTS_API=espos ESPOS_TTS_URL="$GATEWAY/v1/tts" ESPOS_TTS_KEY="$key" ESPOS_TTS_MODEL="${TTS_MODEL:-vieneu-turbo}" \
  "$WORK/espos" > "$WORK/espos-$label.log" 2>&1 &
pid=$!
trap 'kill $pid 2>/dev/null || true' EXIT
for _ in $(seq 1 60); do curl -s -o /dev/null "http://127.0.0.1:$PORT/" && break; sleep 1; done
sleep 3   # market_tick fills the prices once at start

python3 assist_bench.py --espos "http://127.0.0.1:$PORT" --password-file "$WORK/pw.txt" \
  --db "$WORK/data-$label/espos.db" --gateway "$GATEWAY" --key-file "$KEY_FILE" --gateway-log "$GATEWAY_LOG" \
  --label "$label" "$@"
