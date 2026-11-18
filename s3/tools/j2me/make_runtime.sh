#!/bin/sh
# The Java games' runtime package dev.espos.j2me (docs/j2me.md): the class library (java.*, javax.microedition.*,
# com.nokia.*) compiled without debug info into classlib.jar, packed as build-pkgs/dev.espos.j2me-<version>.espk.
#   tools/j2me/make_runtime.sh [version] [code]      (default 1.0.0 10000)
set -e
cd "$(dirname "$0")/../.."
VER=${1:-1.0.0}
CODE=${2:-10000}
OUT=build-pkgs/j2me-runtime
rm -rf "$OUT" && mkdir -p "$OUT/pkg" "$OUT/classes"
find j2me/classlib -name "*.java" > "$OUT/sources.txt"
javac -nowarn -Xlint:-options -encoding UTF-8 -source 8 -target 8 -g:none -bootclasspath "$OUT/classes" \
  -d "$OUT/classes" @"$OUT/sources.txt"
(cd "$OUT/classes" && zip -q -r -9 -X ../pkg/classlib.jar .)
cat > "$OUT/pkg/manifest.json" <<JSON
{"id": "dev.espos.j2me", "name": "Bộ chạy game Java", "version": "$VER", "code": $CODE,
 "part": "core", "kind": "native", "engine": "j2me", "type": "pack", "min_fw": 400, "category": "game", "author": "EspOS",
 "desc": "Thư viện để máy chạy game Java (J2ME) của điện thoại cũ. Tự cài cùng game Java đầu tiên.",
 "icon": {"bg": ["#5b6270", "#2b2f3a"], "glyph": "grid"}}
JSON
python3 tools/espk.py "$OUT/pkg" "build-pkgs/dev.espos.j2me-$VER.espk"
python3 tools/espk.py --check "build-pkgs/dev.espos.j2me-$VER.espk"
