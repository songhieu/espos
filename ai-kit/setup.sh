#!/usr/bin/env bash
# Tên cũ của bộ cài: giờ là install.sh (giọng đọc + Gemma + Whisper, chọn theo máy). Giữ file này để lệnh cũ vẫn chạy.
#   ./setup.sh            = ./install.sh --tts turbo,piper --llm none --stt none   (như trước: chỉ TTS)
#   ./setup.sh --all      = ./install.sh                                          (hỏi, gợi ý theo máy)
set -euo pipefail
cd "$(dirname "$0")"
if [ "${1:-}" = "--all" ]; then shift; exec ./install.sh "$@"; fi
keep=""
[ -f config.yaml ] && keep=--keep-config   # như setup.sh cũ: không đụng config.yaml đã có
exec ./install.sh --tts "${TTS:-turbo,piper}" --llm none --stt none $keep "$@"
