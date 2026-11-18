#!/bin/sh
# Builds the VM runner and the class library, compiles every test, runs each on the VM and prints PASS/FAIL.
# A test passes when it exits 0 and prints "ALL OK"; the deterministic ones must also print exactly what the JDK
# prints for the same class files. docs/j2me.md.
#   j2me/test/run.sh            normal build (tools/j2me/build)
#   j2me/test/run.sh asan       AddressSanitizer + UBSan build (tools/j2me/build-asan)
#   j2me/test/run.sh -bench     also print the benchmark (bytecodes/s)
#   j2me/test/run.sh gcstress   a full GC at every allocation (GcTest, CallbackTest: every 64th): finds missing roots
cd "$(dirname "$0")/../.." || exit 2   # s3/
MODE=build
BENCH=0
STRESS=
for a in "$@"; do
  case "$a" in
    asan) MODE=build-asan ;;
    -bench) BENCH=1 ;;
    gcstress) STRESS="-gcstress 1" ;;
  esac
done
if [ "$MODE" = build-asan ]; then make -s -C tools/j2me asan -j8 || exit 2; else make -s -C tools/j2me -j8 || exit 2; fi
RUN="tools/j2me/$MODE/j2me-run"
B=j2me/build
T=$B/test
EMPTY=$B/.empty-bootclasspath
mkdir -p "$T" "$EMPTY" "$B/classlib"
JAVAC="javac -source 8 -target 8 -nowarn -Xlint:-options -encoding UTF-8"
# the class library's java/** (javax/** is built by j2me/build.sh)
$JAVAC -bootclasspath "$EMPTY" -d "$B/classlib" $(find j2me/classlib/java -name '*.java') || exit 2
# test JARs: res.jar (deflated) and stored.jar (not compressed)
R=$B/test-res
rm -rf "$R" && mkdir -p "$R/jar/pkg" "$R/stored/stored"
cp -R j2me/test/res/res "$R/jar/"
$JAVAC -bootclasspath "$B/classlib" -d "$R/jar" j2me/test/res/pkg/FromJar.java || exit 2
python3 -c "import sys; sys.stdout.buffer.write(bytes(i % 256 for i in range(10240)))" > "$R/jar/pkg/data.bin"
i=0; : > "$R/jar/big.txt"; python3 -c "print(''.join('line %d\n' % i for i in range(10000)), end='')" > "$R/jar/big.txt"
printf 'stored entry\n' > "$R/stored/stored/plain.txt"
(cd "$R/jar" && jar cfM ../res.jar .) && (cd "$R/stored" && jar cfM0 ../stored.jar .) || exit 2
# tests
python3 j2me/test/gen_jsr.py "$T/Jsr.class" || exit 2
$JAVAC -bootclasspath "$B/classlib" -cp "$T" -d "$T" j2me/test/*.java || exit 2

pass=0; fail=0
run() {  # name compare-with-jdk [runner options]
  name=$1; cmp=$2; shift 2
  out=$($RUN $STRESS "$@" -cp "$T" $name 2>"$B/$name.err"); code=$?
  ok=1
  [ $code -eq 0 ] || ok=0
  echo "$out" | grep -q "^ALL OK$" || ok=0
  why=""
  if [ $ok = 1 ] && [ $cmp = jdk ] && command -v java >/dev/null; then
    ref=$(java -Xss8m -cp "$T" $name 2>/dev/null)
    if [ "$out" != "$ref" ]; then
      ok=0; why=" (differs from the JDK)"
      echo "$out" > "$B/$name.vm.txt"; echo "$ref" > "$B/$name.jdk.txt"
      diff "$B/$name.jdk.txt" "$B/$name.vm.txt" | head -20
    fi
  fi
  if [ $ok = 1 ]; then pass=$((pass + 1)); echo "PASS $name"; else
    fail=$((fail + 1)); echo "FAIL $name (exit $code)$why"; echo "$out" | grep -v "^ALL OK$" | tail -15; tail -15 "$B/$name.err"; fi
}
run Test none
run ArithTest jdk
run StringTest jdk
run ArrayTest jdk
run ExceptionTest jdk
run ObjectTest jdk
run CollectionsTest jdk
run JsrTest jdk
run ThreadTest none
S_SAVE=$STRESS; [ -n "$STRESS" ] && STRESS="-gcstress 64"; run GcTest none -Xmx2m; STRESS=$S_SAVE
S_SAVE=$STRESS; [ -n "$STRESS" ] && STRESS="-gcstress 64"; run CallbackTest none -Xmx4m; STRESS=$S_SAVE
T_SAVE=$T; T="$T:$R/res.jar:$R/stored.jar"; run ResourceTest none; T=$T_SAVE
# the deflate decoder (JARs, PNG) against zlib's own streams
python3 j2me/test/gen_inflate.py "$B/inflate" || exit 2
if "tools/j2me/$MODE/inflate-test" "$B/inflate" > "$B/inflate.out" 2>&1; then
  pass=$((pass + 1)); echo "PASS InflateTest ($(tail -1 "$B/inflate.out"))"
else
  fail=$((fail + 1)); echo "FAIL InflateTest"; grep -v "^PASS" "$B/inflate.out" | tail -15
fi
if [ $BENCH = 1 ]; then
  echo "--- benchmark ($MODE)"
  $RUN -bench -cp "$T" BenchTest
  $RUN -bench -cp "$T" Test | tail -1
fi
echo "$pass passed, $fail failed ($MODE${STRESS:+, $STRESS})"
[ $fail -eq 0 ]
