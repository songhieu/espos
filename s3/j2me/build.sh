#!/bin/sh
# Builds the J2ME class library (j2me/classlib: java/** and the MIDP layer's javax/**, com/**). docs/j2me.md.
#   j2me/build.sh           with debug info (line numbers in stack traces): j2me/build/classlib (a folder the VM reads
#                           with addDir, also what tests compile against) and j2me/build/classlib.jar
#   j2me/build.sh release   without debug info (javac -g:none), smallest: only j2me/build/classlib.jar, built from
#                           scratch in j2me/build/classlib-release (the device's dev.espos.j2me package ships it)
# Needs a JDK 9+ javac (17 here): -source/-target 8 with an empty boot class path, so java.lang comes from our own
# sources and nothing from the JDK leaks in. java/** is built first; the rest against it.
set -e
cd "$(dirname "$0")"
MODE=${1:-debug}
EMPTY=build/.empty-bootclasspath
JAVAC="javac -source 8 -target 8 -nowarn -Xlint:-options -encoding UTF-8"
if [ "$MODE" = release ]; then
  OUT=build/classlib-release
  JAVAC="$JAVAC -g:none"
  rm -rf "$OUT"
else
  OUT=build/classlib
fi
mkdir -p "$OUT" "$EMPTY"
$JAVAC -bootclasspath "$EMPTY" -d "$OUT" $(find classlib/java -name '*.java')
echo "java/**: $(find classlib/java -name '*.java' | wc -l | tr -d ' ') sources"
REST=$(find classlib -name '*.java' -not -path 'classlib/java/*')
if [ -n "$REST" ]; then
  if $JAVAC -bootclasspath "$OUT" -sourcepath classlib -d "$OUT" $REST; then
    echo "javax/**, com/**: $(echo "$REST" | wc -l | tr -d ' ') sources"
  else
    echo "build.sh: javax/** or com/** did not compile (java/** is built)" >&2
    exit 1
  fi
fi
rm -f build/classlib.jar
(cd "$OUT" && jar cfM ../classlib.jar .)
echo "build/classlib.jar ($MODE): $(wc -c < build/classlib.jar | tr -d ' ') bytes, $(cd "$OUT" && find . -name '*.class' | wc -l | tr -d ' ') classes"
