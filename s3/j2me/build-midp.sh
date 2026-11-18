#!/bin/sh
# Compiles the MIDP layer (j2me/classlib/javax/** and j2me/classlib/com/**) against the VM's java.* classes
# (j2me/build/classlib, built by the VM's own build) into j2me/build/classlib.
#   j2me/build-midp.sh          compile
#   j2me/build-midp.sh tests    also compile the logic tests in j2me/test/midp into j2me/build/test-midp
set -e
J=$(cd "$(dirname "$0")" && pwd)
OUT="$J/build/classlib"
if [ ! -d "$OUT/java/lang" ]; then
  echo "build-midp: $OUT has no java.* classes yet (build the VM's classlib first)" >&2
  exit 1
fi
LIST=$(mktemp "${TMPDIR:-/tmp}/midp-srcs.XXXXXX")
trap 'rm -f "$LIST"' EXIT
find "$J/classlib/javax" "$J/classlib/com" -name '*.java' | sort > "$LIST"
echo "build-midp: $(wc -l < "$LIST" | tr -d ' ') files"
# -source 8 with -bootclasspath warns that the bootclasspath is not set the usual way; -Xlint:-options hides that.
javac -nowarn -Xlint:-options -encoding UTF-8 -source 8 -target 8 -bootclasspath "$OUT" -d "$OUT" @"$LIST"
echo "build-midp: ok -> $OUT"

if [ "$1" = "tests" ]; then
  # The tests sit in the packages they test (package-private helpers): they compile against the classlib into
  # build/test-midp. test/midp/host/.../Native.java (the stand-in natives the UI test reads back) does not go
  # there: it replaces the real Native in a copy of the classlib, build/test-midp-boot, for j2me-run -boot.
  T="$J/build/test-midp" B="$J/build/test-midp-boot"
  rm -rf "$T" "$B"
  mkdir -p "$T" "$B"
  find "$J/test/midp" -name '*.java' | sort > "$LIST"
  javac -nowarn -Xlint:-options -encoding UTF-8 -source 8 -target 8 -bootclasspath "$OUT" -d "$T" @"$LIST"
  cp -R "$OUT/." "$B/"
  mv "$T/com/espos/j2me/Native.class" "$B/com/espos/j2me/Native.class"
  echo "build-midp: tests ok -> $T (boot with the stand-in natives: $B)"
fi
