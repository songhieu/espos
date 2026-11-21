#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
[[ -x bin/tts-gateway ]] || { echo "Chưa build — chạy ./setup.sh trước" >&2; exit 1; }
exec ./bin/tts-gateway -config "${1:-config.yaml}"
