#!/bin/sh
# Runs the MIDP layer's tests on this PC's JDK: java.* from the JDK, the device natives replaced by the pure-Java
# stand-in test/midp/host/com/espos/j2me/Native.java. Every test prints "<Name>: OK (n checks)" or
# "<Name>: FAILED k of n" (and one FAIL line per failed check); this script exits non-zero when any fails.
#   j2me/test/midp/run-host.sh            all tests
#   j2me/test/midp/run-host.sh GameTest   the ones whose name contains GameTest
set -e
J=$(cd "$(dirname "$0")/../.." && pwd)
OUT="${TMPDIR:-/tmp}/midp-host-tests"
rm -rf "$OUT"
mkdir -p "$OUT"
LIST="$OUT/sources.txt"
# the library without the device's Native, then the tests and the stand-in Native
find "$J/classlib/javax" "$J/classlib/com" -name '*.java' ! -path '*/com/espos/j2me/Native.java' > "$LIST"
find "$J/test/midp" -name '*.java' >> "$LIST"
javac -nowarn -Xlint:none -encoding UTF-8 -d "$OUT" @"$LIST"
TESTS="javax.microedition.lcdui.game.GameTest javax.microedition.lcdui.LcduiTest javax.microedition.rms.RmsTest javax.microedition.media.MediaTest"
fail=0
for t in $TESTS; do
  case "$t" in *"$1"*) ;; *) continue ;; esac
  java -cp "$OUT" "$t" || fail=1
done
exit $fail
