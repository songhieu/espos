#!/usr/bin/env bash
# Cài bộ AI chạy tại chỗ trên Mac Apple Silicon: giọng nói (VieNeu-TTS, Piper), mô hình trả lời (Gemma 4)
# và nghe giọng nói (Whisper). Tất cả chạy trên máy, không gọi dịch vụ ngoài. Chạy lại nhiều lần cũng không sao.
#
#   ./install.sh                         xem máy, gợi ý bộ phù hợp, hỏi từng bước
#   ./install.sh --yes                   cài đúng bộ gợi ý, không hỏi
#   ./install.sh --llm e2b --stt turbo --tts turbo,piper --yes
#   ./install.sh --check                 chỉ xem máy và in gợi ý
#   ./install.sh --no-selftest --keep-build --keep-config
#
# --llm  e2b | e4b | none          Gemma 4 E2B (2,6 GB) hoặc E4B (4,2 GB)
# --stt  turbo | small | none      Whisper large-v3-turbo (0,57 GB, nghe tiếng Việt tốt) hoặc small (0,19 GB)
# --tts  turbo,nano,piper | none   VieNeu Turbo / Nano (Python), Piper
set -euo pipefail
cd "$(dirname "$0")"
ROOT=$(pwd)

# Phiên bản đã thử (đổi ở đây khi nâng cấp; xem HANDOFF.md)
LLAMA_TAG=v1.0.0
WHISPER_TAG=v1.9.5

say()  { printf '\n\033[1;32m==> %s\033[0m\n' "$*"; }
info() { printf '    %s\n' "$*"; }
warn() { printf '\033[1;33m!!  %s\033[0m\n' "$*" >&2; }
die()  { printf '\033[1;31mLỗi: %s\033[0m\n' "$*" >&2; exit 1; }

# ── tham số ──────────────────────────────────────────────────────────────────
YES=0; CHECK=0; SELFTEST=1; KEEP_BUILD=0; KEEP_CONFIG=0
OPT_LLM=""; OPT_STT=""; OPT_TTS=""
while [ $# -gt 0 ]; do
  case "$1" in
    --yes|-y) YES=1 ;;
    --check) CHECK=1 ;;
    --no-selftest) SELFTEST=0 ;;
    --keep-build) KEEP_BUILD=1 ;;
    --keep-config) KEEP_CONFIG=1 ;;
    --llm) OPT_LLM=${2:?}; shift ;;
    --stt) OPT_STT=${2:?}; shift ;;
    --tts) OPT_TTS=${2:?}; shift ;;
    -h|--help) sed -n '2,15p' "$0"; exit 0 ;;
    *) die "không hiểu tham số $1 (xem ./install.sh --help)" ;;
  esac
  shift
done
[ -t 0 ] || YES=1   # không có bàn phím (chạy từ script): dùng gợi ý

ask() {  # ask "câu hỏi" mặc_định → in câu trả lời
  local q=$1 def=$2 ans=""
  if [ $YES = 1 ]; then echo "$def"; return; fi
  printf '%s [%s]: ' "$q" "$def" > /dev/tty
  read -r ans < /dev/tty || true
  echo "${ans:-$def}"
}

# ── 1. xem máy ───────────────────────────────────────────────────────────────
say "Xem máy"
[ "$(uname -s)" = Darwin ] || die "bộ cài này dành cho macOS"
ARCH=$(uname -m)
CHIP=$(sysctl -n machdep.cpu.brand_string 2>/dev/null || echo "?")
RAM_GB=$(( $(sysctl -n hw.memsize) / 1073741824 ))
GPU_CORES=$(system_profiler SPDisplaysDataType 2>/dev/null | awk -F': ' '/Total Number of Cores/{print $2; exit}')
GPU_CORES=${GPU_CORES:-?}
free_mb() { df -Pm "$ROOT" | awk 'NR==2{print $4}'; }
FREE_MB=$(free_mb)
info "Chip: $CHIP ($ARCH), GPU $GPU_CORES nhân, RAM ${RAM_GB} GB, ổ đĩa còn trống $(( FREE_MB / 1024 )).$(( FREE_MB % 1024 * 10 / 1024 )) GB"
info "macOS $(sw_vers -productVersion)"
[ "$ARCH" = arm64 ] || warn "Mac Intel: không có GPU Metal cho mô hình, Gemma sẽ rất chậm. Nên chỉ cài TTS (Piper)."

# ── 2. danh mục model (dung lượng thật trên HuggingFace, kiểm 2026-11-21) ─────
# tên | file | url | MB
llm_info() {
  case "$1" in
    e2b) echo "gemma-4-e2b|gemma-4-E2B-it-qat-UD-Q4_K_XL.gguf|https://huggingface.co/unsloth/gemma-4-E2B-it-qat-GGUF/resolve/main/gemma-4-E2B-it-qat-UD-Q4_K_XL.gguf|2499" ;;
    e4b) echo "gemma-4-e4b|gemma-4-E4B-it-qat-UD-Q4_K_XL.gguf|https://huggingface.co/unsloth/gemma-4-E4B-it-qat-GGUF/resolve/main/gemma-4-E4B-it-qat-UD-Q4_K_XL.gguf|4025" ;;
  esac
}
stt_info() {
  case "$1" in
    turbo) echo "whisper|ggml-large-v3-turbo-q5_0.bin|https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large-v3-turbo-q5_0.bin|548" ;;
    small) echo "whisper|ggml-small-q5_1.bin|https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small-q5_1.bin|182" ;;
  esac
}
VAD_URL=https://huggingface.co/ggml-org/whisper-vad/resolve/main/ggml-silero-v6.2.0.bin
VAD_FILE=ggml-silero-v6.2.0.bin
field() { echo "$1" | cut -d'|' -f"$2"; }

# ── 3. gợi ý theo máy ────────────────────────────────────────────────────────
# chỗ trống tính cả các model đã tải sẵn (chạy lại install.sh không bị gợi ý nhỏ đi)
SPACE=$FREE_MB
for f in models/llm/*.gguf models/stt/*.bin; do [ -s "$f" ] && SPACE=$(( SPACE + $(du -m "$f" | cut -f1) )); done
REC_LLM=none; REC_STT=none; REC_TTS=piper; REC_SLOTS=1; REC_TURBO=1
if [ "$ARCH" = arm64 ]; then
  if   [ $RAM_GB -ge 24 ] && [ $SPACE -ge 9000 ]; then REC_LLM=e4b; REC_STT=turbo; REC_TTS=turbo,piper; REC_SLOTS=4; REC_TURBO=2
  elif [ $RAM_GB -ge 12 ] && [ $SPACE -ge 4500 ]; then REC_LLM=e2b; REC_STT=turbo; REC_TTS=turbo,piper; REC_SLOTS=2
  elif [ $RAM_GB -ge 8 ]  && [ $SPACE -ge 3500 ]; then REC_LLM=e2b; REC_STT=small; REC_TTS=piper; REC_SLOTS=1
  fi
fi
info "Gợi ý cho máy này: mô hình trả lời = $REC_LLM, nghe giọng = $REC_STT, giọng đọc = $REC_TTS"
info "  (E2B: ~3,5 GB RAM, trả lời 1 lượt của chó trợ lý ~2-6 s trên M1 Pro; E4B: khôn hơn, chậm hơn (chưa đo), cần ~6 GB RAM)"
[ $CHECK = 1 ] && exit 0

# ── 4. chọn ──────────────────────────────────────────────────────────────────
pick() {  # pick "tiêu đề" mặc_định lựa_chọn...  (in ra key đã chọn)
  local title=$1 def=$2; shift 2
  if [ $YES = 1 ]; then echo "$def"; return; fi
  { echo; echo "$title"; } > /dev/tty
  local i=1 k
  for k in "$@"; do printf '   %d) %s\n' $i "$k" > /dev/tty; i=$((i+1)); done
  local n; n=$(ask "Chọn số (Enter = $def)" "")
  if [ -z "$n" ]; then echo "$def"; return; fi
  i=1
  for k in "$@"; do [ "$n" = $i ] && { echo "${k%% *}"; return; }; i=$((i+1)); done
  echo "$def"
}
LLM=${OPT_LLM:-$(pick "Mô hình trả lời (Gemma 4, chạy bằng GPU):" "$REC_LLM" "e2b   Gemma 4 E2B, 2,6 GB — nhẹ, nhanh" "e4b   Gemma 4 E4B, 4,2 GB — khôn hơn, chậm hơn" "none  không cài")}
STT=${OPT_STT:-$(pick "Nghe giọng nói (Whisper):" "$REC_STT" "turbo  large-v3-turbo, 0,57 GB — nghe tiếng Việt tốt" "small  small, 0,19 GB — nhẹ, sai nhiều hơn" "none   không cài")}
TTS=${OPT_TTS:-$(ask "Giọng đọc, nhiều cái cách nhau dấu phẩy (turbo, nano, piper, none)" "$REC_TTS")}
case "$LLM" in e2b|e4b|none) ;; *) die "--llm phải là e2b, e4b hoặc none" ;; esac
case "$STT" in turbo|small|none) ;; *) die "--stt phải là turbo, small hoặc none" ;; esac
has_tts() { case ",$TTS," in *",$1,"*) return 0 ;; esac; return 1; }
[ "$TTS" = none ] && TTS=""
for t in $(echo "$TTS" | tr ',' ' '); do case $t in turbo|nano|piper) ;; *) die "giọng đọc không có: $t" ;; esac; done
[ -z "$TTS" ] && [ "$LLM" = none ] && [ "$STT" = none ] && die "chưa chọn gì để cài"
info "Sẽ cài: mô hình trả lời = $LLM, nghe giọng = $STT, giọng đọc = ${TTS:-không}"

# ── 5. đủ chỗ trên ổ đĩa? ────────────────────────────────────────────────────
need=1024   # chừa lại 1 GB
need_file() { [ -s "$1" ] || need=$(( need + $2 )); }
[ "$LLM" != none ] && need_file "models/llm/$(field "$(llm_info $LLM)" 2)" "$(field "$(llm_info $LLM)" 4)"
[ "$STT" != none ] && need_file "models/stt/$(field "$(stt_info $STT)" 2)" "$(field "$(stt_info $STT)" 4)"
[ "$LLM" != none ] && [ ! -x bin/llm-server ] && need=$(( need + 400 ))   # mã nguồn + build (xóa sau khi xong)
[ "$STT" != none ] && [ ! -x bin/stt-server ] && need=$(( need + 200 ))
has_tts turbo || has_tts nano && [ ! -d worker/.venv ] && need=$(( need + 900 ))
has_tts turbo && [ ! -d "$HOME/.cache/huggingface/hub/models--pnnbao-ump--VieNeu-TTS-v3-Turbo" ] && need=$(( need + 600 ))
has_tts piper && need_file models/piper/vi_VN-vais1000-medium.onnx 64
if [ "$(free_mb)" -lt $need ]; then
  die "ổ đĩa còn $(( $(free_mb) / 1024 )) GB, cần khoảng $(( need / 1024 + 1 )) GB (gồm 1 GB chừa lại). Dọn bớt rồi chạy lại, hoặc chọn model nhỏ hơn."
fi
info "Cần thêm khoảng $(( need / 1024 )).$(( need % 1024 * 10 / 1024 )) GB, ổ còn $(( $(free_mb) / 1024 )) GB: đủ."

# ── 6. công cụ ───────────────────────────────────────────────────────────────
say "Công cụ build"
xcode-select -p >/dev/null 2>&1 || die "chưa có Xcode Command Line Tools: chạy  xcode-select --install  rồi chạy lại"
missing=""
command -v go >/dev/null || missing="$missing go"
{ [ "$LLM" != none ] || [ "$STT" != none ]; } && ! command -v cmake >/dev/null && missing="$missing cmake"
{ has_tts turbo || has_tts nano || has_tts piper; } && ! command -v uv >/dev/null && missing="$missing uv"
if [ -n "$missing" ]; then
  command -v brew >/dev/null || die "thiếu:$missing — cài Homebrew (https://brew.sh) rồi chạy lại"
  say "brew install$missing"
  brew install $missing
fi
command -v cloudflared >/dev/null || info "(chưa có cloudflared: chỉ cần khi bật tunnel, cài bằng: brew install cloudflared)"

# ── 7. tải file ──────────────────────────────────────────────────────────────
fetch() {  # fetch url đích: tải tiếp được nếu bị ngắt, kiểm dung lượng
  local url=$1 dst=$2
  [ -s "$dst" ] && { info "đã có $(basename "$dst")"; return; }
  mkdir -p "$(dirname "$dst")"
  info "tải $(basename "$dst") ..."
  curl -fL --retry 5 --retry-delay 3 -C - --progress-bar -o "$dst.part" "$url" || die "tải $url thất bại (chạy lại để tải tiếp)"
  mv "$dst.part" "$dst"
}
if [ "$LLM" != none ]; then
  say "Tải Gemma 4 $(echo $LLM | tr a-z A-Z) (Apache-2.0)"
  fetch "$(field "$(llm_info $LLM)" 3)" "models/llm/$(field "$(llm_info $LLM)" 2)"
fi
if [ "$STT" != none ]; then
  say "Tải Whisper ($STT) và Silero VAD"
  fetch "$(field "$(stt_info $STT)" 3)" "models/stt/$(field "$(stt_info $STT)" 2)"
  fetch "$VAD_URL" "models/stt/$VAD_FILE"
fi

# ── 8. build llm-server / stt-server ─────────────────────────────────────────
src() {  # src tên repo tag → third_party/tên
  local name=$1 repo=$2 tag=$3
  [ -f "third_party/$name/CMakeLists.txt" ] && return
  info "tải mã nguồn $name $tag"
  mkdir -p third_party
  curl -fsSL "https://github.com/ggml-org/$repo/archive/refs/tags/$tag.tar.gz" | tar xz -C third_party \
    --exclude='*/models/*.gguf' --exclude='*/docs/*' --exclude='*/samples/*' --exclude='*/bindings/*' \
    --exclude='*/tools/server/public*' --exclude='*/tools/server/webui*'
  mv "third_party/$repo-${tag#v}" "third_party/$name"
}
build() {  # build llm|stt
  local what=$1
  say "Build $what-server (Metal)"
  cmake -S "$what" -B "build/$what" -DCMAKE_BUILD_TYPE=Release > "build-$what.log" 2>&1 || die "cmake $what thất bại, xem build-$what.log"
  cmake --build "build/$what" --target "$what-server" -j "$(sysctl -n hw.ncpu)" >> "build-$what.log" 2>&1 || die "build $what thất bại, xem build-$what.log"
  mkdir -p bin
  rm -f "bin/$what-server"   # thay file mới (ghi đè lên file đang chạy thì macOS giết tiến trình)
  cp "build/$what/$what-server" "bin/$what-server"
  rm -f "build-$what.log"
  [ $KEEP_BUILD = 1 ] || rm -rf "build/$what"
}
if [ "$LLM" != none ] && [ ! -x bin/llm-server ] || { [ "$LLM" != none ] && [ llm/llm-server.cpp -nt bin/llm-server ]; }; then
  src llama.cpp llama.cpp "$LLAMA_TAG"; build llm
fi
if [ "$STT" != none ] && [ ! -x bin/stt-server ] || { [ "$STT" != none ] && [ stt/stt-server.cpp -nt bin/stt-server ]; }; then
  src whisper.cpp whisper.cpp "$WHISPER_TAG"; build stt
fi

# ── 9. giọng đọc (Python) ────────────────────────────────────────────────────
if has_tts turbo || has_tts nano || has_tts piper; then
  say "Python env cho giọng đọc (worker/.venv)"
  (cd worker && uv sync --python 3.12)
fi
if has_tts piper; then
  say "Tải giọng Piper tiếng Việt"
  B=https://huggingface.co/rhasspy/piper-voices/resolve/main/vi/vi_VN/vais1000/medium/vi_VN-vais1000-medium
  fetch "$B.onnx" models/piper/vi_VN-vais1000-medium.onnx
  fetch "$B.onnx.json" models/piper/vi_VN-vais1000-medium.onnx.json
fi
if has_tts turbo || has_tts nano; then
  say "Tải model VieNeu (lần đầu mất vài phút)"
  worker/.venv/bin/python worker/worker.py --prefetch 2>&1 | grep -iv warn || true
fi

# ── 10. gateway + config.yaml ────────────────────────────────────────────────
say "Build gateway"
mkdir -p bin
(cd gateway && go build -o ../bin/tts-gateway.new .) && mv bin/tts-gateway.new bin/tts-gateway

THREADS=$(( $(sysctl -n hw.perflevel0.physicalcpu 2>/dev/null || echo 4) ))
[ $THREADS -gt 8 ] && THREADS=8
models=$(mktemp)
{
  echo "models:"
  if has_tts turbo; then cat <<EOF
  - id: vieneu-turbo          # giọng tốt nhất, 48 kHz, stream ~0.2-0.3 s là có tiếng
    engine: vieneu-turbo
    workers: $REC_TURBO
    threads: 4
EOF
  fi
  if has_tts nano; then cat <<EOF
  - id: vieneu-nano           # nhẹ hơn, chất lượng thấp hơn, 24 kHz
    engine: vieneu-nano
    workers: 1
    threads: 2
EOF
  fi
  if has_tts piper; then cat <<EOF
  - id: piper                 # rất nhanh, rất nhẹ, giọng "máy" hơn
    engine: piper
    workers: 2
    threads: 1
    default_voice: vais1000
    voices:
      vais1000: models/piper/vi_VN-vais1000-medium.onnx
EOF
  fi
  if [ "$LLM" != none ]; then cat <<EOF
  - id: $(field "$(llm_info $LLM)" 1)           # Gemma 4: POST /v1/chat/completions (OpenAI)
    engine: llm
    model: models/llm/$(field "$(llm_info $LLM)" 2)
    workers: 1
    slots: $REC_SLOTS                  # số câu hỏi trả lời cùng lúc (chung 1 lần nạp model)
    ctx: 16384                # token tối đa mỗi câu (câu hỏi + trả lời)
    threads: 4
EOF
  fi
  if [ "$STT" != none ]; then cat <<EOF
  - id: whisper               # POST /v1/audio/transcriptions (OpenAI)
    engine: stt
    model: models/stt/$(field "$(stt_info $STT)" 2)
    vad_model: models/stt/$VAD_FILE
    language: vi
    workers: 1
    slots: 1
    threads: 4
EOF
  fi
} > "$models"

if [ ! -f config.yaml ]; then
  key=$(openssl rand -hex 24)
  sed "s/CHANGE_ME/$key/" config.example.yaml > config.yaml
  chmod 600 config.yaml
  NEWKEY=$key
elif [ $KEEP_CONFIG = 0 ] && [ $YES = 0 ]; then
  case "$(ask "config.yaml đã có. Ghi lại danh sách model theo lựa chọn vừa rồi? (key, tunnel giữ nguyên; bản cũ lưu ở config.yaml.bak) y/n" y)" in
    y|Y|yes|có|co) ;;
    *) KEEP_CONFIG=1 ;;
  esac
fi
if [ $KEEP_CONFIG = 1 ]; then
  info "giữ nguyên danh sách model trong config.yaml"
else
  ./bin/tts-gateway -config config.yaml -set-models "$models"
  chmod 600 config.yaml config.yaml.bak 2>/dev/null || true
  say "Đã ghi các model vào config.yaml (bản cũ: config.yaml.bak)"
fi
rm -f "$models"
./bin/tts-gateway -config config.yaml -check | sed 's/^/    /'

# ── 11. tự kiểm tra + đo nhanh ───────────────────────────────────────────────
if [ $SELFTEST = 1 ]; then
  say "Tự kiểm tra (gateway tạm ở 127.0.0.1:18080, không bật tunnel)"
  mkdir -p logs
  ./bin/tts-gateway -config config.yaml -listen 127.0.0.1:18080 -no-tunnel > logs/selftest.log 2>&1 &
  gw=$!
  trap 'kill $gw 2>/dev/null || true' EXIT
  ok=0
  for _ in $(seq 1 900); do
    if curl -s 127.0.0.1:18080/health | grep -q '"status": "ok"'; then ok=1; break; fi
    kill -0 $gw 2>/dev/null || break
    # a model that keeps failing to start: say which, do not wait 15 minutes
    bad=$(grep -o '\[[^]#]*#[0-9]*\] worker exited: exited during start-up' logs/selftest.log 2>/dev/null | sed 's/#.*//; s/\[//' | sort | uniq -c | awk '$1 >= 3 {print $2}' | tr '\n' ' ')
    if [ -n "$bad" ]; then warn "model không khởi động được: $bad(xem logs/selftest.log)"; break; fi
    sleep 1
  done
  if [ $ok = 1 ]; then
    key=$(awk '/^api_keys:/{f=1} f && /key:/{gsub(/["'"'"']/,"",$2); print $2; exit}' config.yaml)
    KEY="$key" python3 bench/quick_test.py --gateway http://127.0.0.1:18080 || warn "tự kiểm tra có lỗi, xem logs/selftest.log"
  else
    warn "gateway không lên được, xem logs/selftest.log"
  fi
  kill $gw 2>/dev/null || true
  wait $gw 2>/dev/null || true
  trap - EXIT
fi

# ── 12. xong ─────────────────────────────────────────────────────────────────
say "Xong."
[ -n "${NEWKEY:-}" ] && { info "API key mới (lưu lại; thêm/xóa key trong config.yaml):"; info "    $NEWKEY"; }
cat <<EOF

    Chạy:            ./run.sh            (hoặc ./service.sh install để tự chạy khi mở máy)
    Kiểm tra:        curl http://127.0.0.1:8080/health
    Đo kỹ hơn:       xem BENCHMARK.md (bench/llm_bench.py, bench/speech_bench.py, bench/run_e2e.sh)

    Nối vào máy chủ EspOS (file .env của nó), thay <KEY> bằng API key trong config.yaml:
      ESPOS_AI_URL=http://127.0.0.1:8080/v1   ESPOS_AI_KEY=<KEY>   ESPOS_AI_MODEL=$( [ "$LLM" != none ] && field "$(llm_info $LLM)" 1 || echo gemma-4-e2b)
      ESPOS_AI_PROMPT=short                   ESPOS_AI_TIMEOUT=60
      ESPOS_STT_URL=http://127.0.0.1:8080/v1  ESPOS_STT_KEY=<KEY>  ESPOS_STT_MODEL=whisper  ESPOS_STT_LANG=vi
      ESPOS_TTS_API=espos  ESPOS_TTS_URL=http://127.0.0.1:8080/v1/tts  ESPOS_TTS_KEY=<KEY>  ESPOS_TTS_MODEL=vieneu-turbo
EOF
