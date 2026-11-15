#!/usr/bin/env bash
# Collects a full-flash bundle for the InkDesk web installer after
# `pio run -e default`: bootloader, partition table, OTA boot selector and app,
# plus manifest.json with the flash offsets (see partitions.csv).
#
# Usage: scripts/inkdesk_bundle.sh [env] [out_dir]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ENV_NAME="${1:-default}"
OUT="${2:-$ROOT/.pio/bundle-$ENV_NAME}"
BUILD="$ROOT/.pio/build/$ENV_NAME"
VERSION="$(sed -n 's/.*FW_VERSION = "\(.*\)";/\1/p' "$ROOT/src/inkdesk/InkDeskStore.h")"

BOOT_APP0="$(ls "$HOME"/.platformio/packages/framework-arduinoespressif32*/tools/partitions/boot_app0.bin 2>/dev/null | head -1 || true)"
for f in "$BUILD/bootloader.bin" "$BUILD/partitions.bin" "$BUILD/firmware.bin" "$BOOT_APP0"; do
  if [ -z "$f" ] || [ ! -f "$f" ]; then
    echo "missing build output: ${f:-boot_app0.bin}" >&2
    exit 1
  fi
done

# The app must fit one OTA slot (app0/app1 are 0x640000 in partitions.csv).
APP_SIZE=$(wc -c <"$BUILD/firmware.bin" | tr -d ' ')
if [ "$APP_SIZE" -gt $((0x640000)) ]; then
  echo "firmware.bin ($APP_SIZE bytes) does not fit the 0x640000 app slot" >&2
  exit 1
fi

mkdir -p "$OUT"
cp "$BUILD/bootloader.bin" "$OUT/bootloader.bin"
cp "$BUILD/partitions.bin" "$OUT/partitions.bin"
cp "$BOOT_APP0" "$OUT/boot_app0.bin"
cp "$BUILD/firmware.bin" "$OUT/firmware.bin"

entry() {
  local file="$1" offset="$2"
  local size sha
  size=$(wc -c <"$OUT/$file" | tr -d ' ')
  sha=$(shasum -a 256 "$OUT/$file" | cut -d' ' -f1)
  printf '    {"file": "%s", "offset": %d, "size": %d, "sha256": "%s"}' "$file" "$offset" "$size" "$sha"
}

{
  echo '{'
  echo "  \"version\": \"$VERSION\","
  echo '  "chip": "esp32c3",'
  echo '  "flash_size": "16MB",'
  echo '  "parts": ['
  entry bootloader.bin $((0x0)); echo ','
  entry partitions.bin $((0x8000)); echo ','
  entry boot_app0.bin $((0xe000)); echo ','
  entry firmware.bin $((0x10000)); echo
  echo '  ]'
  echo '}'
} >"$OUT/manifest.json"

echo "Bundle written to $OUT"
cat "$OUT/manifest.json"
