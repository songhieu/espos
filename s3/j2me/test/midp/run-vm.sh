#!/bin/sh
# Runs the MIDP layer's tests on EspOS's own J2ME VM (the host runner tools/j2me/build/j2me-run, built with
# `make -C tools/j2me`), with the class library from j2me/build/classlib and the stand-in natives
# (test/midp/host/.../Native.java) in place of the device's. Same output and exit status as run-host.sh.
#   j2me/test/midp/run-vm.sh            all tests
#   j2me/test/midp/run-vm.sh GameTest   the ones whose name contains GameTest
set -e
J=$(cd "$(dirname "$0")/../.." && pwd)
RUN="$J/../tools/j2me/build/j2me-run"
[ -x "$RUN" ] || { echo "run-vm: build the runner first: make -C tools/j2me" >&2; exit 2; }
sh "$J/build-midp.sh" tests > /dev/null
TESTS="javax.microedition.lcdui.game.GameTest javax.microedition.lcdui.LcduiTest javax.microedition.rms.RmsTest javax.microedition.media.MediaTest"
fail=0
for t in $TESTS; do
  case "$t" in *"$1"*) ;; *) continue ;; esac
  "$RUN" -boot "$J/build/test-midp-boot" -cp "$J/build/test-midp" "$t" || fail=1
done
exit $fail
