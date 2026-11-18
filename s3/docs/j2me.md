# The J2ME virtual machine (S3)

EspOS S3 runs old phone games (`.jar` files for CLDC 1.1 / MIDP 2.0) on its own small Java virtual machine.
This page is the VM core: `src/j2me/jvm.h` (the API), `src/j2me/jvm.cpp` and `src/j2me/vm_*.cpp` (and
`vm_inflate.h`, the deflate decoder the PNG decoder uses too), the class
library's `java.*` part in `j2me/classlib/java`, the host runner in `tools/j2me` and the tests in `j2me/test`.
The MIDP layer (`javax.microedition.*`, `src/j2me/midp_*`) is built on top of it.

Plain C++17, no exceptions, no RTTI, no libraries (the JAR inflater is our own). The same files build for the
computer (clang, `tools/j2me`) and for the board (`ESP_PLATFORM`: xtensa gcc of ESP-IDF 5.5 / Arduino-ESP32 3.x).

## 1. Build and test

```
j2me/build.sh                       # the class library -> j2me/build/classlib (folder) + j2me/build/classlib.jar
j2me/build.sh release               # the same without debug info (javac -g:none) -> j2me/build/classlib.jar only
make -C tools/j2me                  # the VM runner -> tools/j2me/build/j2me-run
make -C tools/j2me asan             # the same with AddressSanitizer + UBSan -> tools/j2me/build-asan/j2me-run
j2me/test/run.sh                    # builds what it needs, runs every test: PASS/FAIL per test
j2me/test/run.sh asan               # every test under ASan + UBSan
j2me/test/run.sh gcstress           # a full GC at every allocation (finds objects C++ forgot to keep alive)
j2me/test/run.sh -bench             # also the benchmark (bytecodes/s)
```

`j2me-run [-cp folder_or_jar[:...]] [-Xmx 2m] [-Xss slots] [-bench] [-gcstress N] [-boot classlib] Class [args]`
loads `j2me/build/classlib` first (found next to the executable), runs `main`, prints `System.out`/`err`, and exits
with the program's `System.exit` code, 1 when the main thread died of an exception (its stack trace is printed), 2
for a missing class or a deadlock (every thread waits forever; a thread dump is printed).

The class library is compiled with JDK 17's javac as `-source 8 -target 8` with an empty boot class path, so
`java.lang` comes from our sources and nothing of the JDK leaks in. No lambdas and no `x.new Inner()` in the class
library or tests (they need `invokedynamic` / `java.util.Objects`); string `+` is fine (javac 8 uses
`StringBuilder`).

The tests (`j2me/test`): `Test` (the first one), `ArithTest`, `StringTest`, `ArrayTest`, `ExceptionTest`,
`ObjectTest`, `CollectionsTest`, `JsrTest` (jsr/ret `finally` the way Java 1.1-1.4 compilers made it, class file
version 45.3, assembled by `gen_jsr.py`), `ThreadTest`, `GcTest` (60 MB through a 2 MB heap with live data kept
everywhere, then `OutOfMemoryError` caught), `CallbackTest` (natives calling back into Java, see section 5),
`ResourceTest` (a deflated and a stored JAR), `InflateTest` (`tools/j2me/inflate_test.cpp`: 43 streams made by
Python's zlib in every block style, a PNG-like zlib stream, plus cut-short and damaged ones) and `BenchTest`. The
deterministic tests print their values and `run.sh` checks that the output is identical to what the JDK prints for
the same class files.

## 2. Memory

Everything the VM allocates goes through `vmMalloc` (`heap_caps_aligned_alloc(8, n, MALLOC_CAP_SPIRAM)` on the board,
`malloc` elsewhere): Java objects, class files, constant pools, method tables, thread stacks, and the VM's own maps
(`PVec`/`PMap` are std containers with that allocator). Internal RAM is left alone. Only short-lived `std::string`s
(messages, names in natives) come from the normal heap.

**Objects** (`Object` in jvm.h): a 16-byte header (`cls`, `mark` for the collector, `length` for arrays), then
the instance fields, one 8-byte `Slot` each (long and double too; superclass fields first; `Field::slot` is the
index: `obj->slots()[f->slot].i`), or an array's elements packed at their natural size (`elems<int8_t>()` for
boolean and byte, `uint16_t` char, `int16_t` short, `int32_t`, `float`, `int64_t`, `double`, `Object*`). Objects are
malloc'd one by one and never move, so `Object*` and element pointers stay valid across collections, and
`System.identityHashCode` is the address, mixed.

**The heap limit** (`Vm::heapLimit`, default 3 MB on the board, 64 MB on the computer) counts the bytes of live
objects. Past it, after a full collection, the allocation throws `OutOfMemoryError` (a preallocated one). A
collection starts when the heap has grown by max(live, limit/4) since the last one.

**The collector** is mark-sweep. Roots: every thread's Java stack, the class statics and `java.lang.Class`
mirrors, interned strings, objects whose monitor is held, pending exceptions and thread objects, `addRoot()`
pointers, and the objects handed to C++ (`Vm::locals`, below). Java stacks are scanned **conservatively**: the
interpreter keeps no type per slot, so every slot from a thread's stack bottom to its top frame's `sp` counts as a
reference when it equals the address of a live object (binary search in the heap list, sorted at each collection).
A number that happens to equal an address only keeps that object alive a little longer. Heap objects themselves are
scanned exactly (`Class::refSlots`, reference arrays).

### The GC rule for natives

The collector only runs inside allocations: `alloc`, `newArray`, `newString*`, `intern`, `mirror`, `call`,
`throwNew`, `loadClass`/`initClass` (they may run Java), and `gc()`. So:

* A native's arguments are safe for the whole native (they stay on the Java stack).
* Every `Object*` the VM hands to C++ (what those functions return, `call()`'s result) stays alive until the native
  that is running returns. Outside natives (host code between `step()`s) they stay alive until the next
  `step()`/`runMain()`. So a native can allocate a string, then an array, then fill the array, without rooting
  anything.
* An `Object*` read out of a field or an array element is only as alive as that field: if you overwrite the field
  (or drop the object holding it) and then allocate, pass it to `vm.keep(o)` first.
* A native that allocates a lot in a loop can bound the kept list with `Vm::Scope s(vm);` (everything kept inside
  the scope is released when it ends).
* References kept across natives or `step()`s (the current `Displayable`, a listener) need
  `vm.addRoot(&ptr)` / `vm.removeRoot(&ptr)`.

`j2me/test/run.sh gcstress` runs every test with a collection at every allocation; run it after changing natives.

## 3. Classes

Classes come from JARs (`addJar(bytes, len, copy)`: the central directory is read once; entries are inflated on
demand by our raw-deflate decoder, stored entries copied; `copy=false` keeps the caller's bytes, e.g. memory-mapped
flash or a PSRAM buffer, which must then outlive the Vm) and folders (`addDir`), searched in the order added. On the
device the class library is its own package (`classlib.jar` from `j2me/build.sh release`, ~180 KB).
`resource(name, out)` reads any entry (a leading `/` is ignored); use the `PVec<uint8_t>` overload on the board.

`vm_inflate.h` exposes the decoder: `inflateRaw(src, len, std::vector<uint8_t>& out, sizeHint)` (and a
`PVec<uint8_t>` overload for PSRAM; the output grows as needed) for PNG IDAT data with the 2-byte zlib header
skipped (the Adler-32 after the last block is ignored); `false` for corrupt or cut-short data. Codes of up to 9 bits
decode through a 512-entry table; the tables live in PSRAM for the call (no static or big stack buffers).

Names (class, method, field, descriptors) are interned C strings (`Sym`): compare them with `==` against another
`Sym` (pointer) or a C string / `std::string` (characters). A class file stays in memory (`Class::bytes`): code and
string constants point into it. Linking lays out fields, builds the **vtable** (the superclass's, overrides in
place, new methods appended, then interface methods nobody implements so `invokevirtual` through an abstract class
finds a slot), and binds natives. `<clinit>` runs on first active use (`new`, static field access, static call,
`Class.forName`, `newInstance`), superclass first; an exception that is not an `Error` becomes
`ExceptionInInitializerError` (the cause's stack trace goes to the log) and later uses throw
`NoClassDefFoundError`. A thread that needs a class another thread is initializing waits for it.

Accepted: class file versions 45.3 to 52 (later ones load too unless they use `invokedynamic` or new constant
types). Preverifier `StackMap` attributes are ignored. There is no bytecode verifier: games are trusted.

## 4. The interpreter

`Vm::run` is one function with computed-goto dispatch (GCC and clang). Each JVM slot (local or operand) is one
8-byte `Slot`; long and double use two, the value in the first, so `dup2`/`pop2` work as in the spec. Every opcode
of Java 1.1-8 is there except `invokedynamic` (it throws `Error`): jsr/ret, wide, tableswitch/lookupswitch,
multianewarray, monitors, all conversions with Java's rules (NaN to 0, saturation), division by zero, MIN/-1.

**Quickening**: the first run of a field access, call, `new`, `checkcast`/`instanceof` or `anewarray` resolves the
constant pool entry and rewrites the instruction in the class file bytes into a quick form (`OP_*_Q` in
vm_internal.h): `getfield_q slot`, `invokevirtual_q` (vtable index), `invokedirect_q` (a direct target: static,
private, final, super, constructors), `invokeinterface_q` (a 256-entry global cache of class + method -> target),
`new_q` (class initialized). Static accesses are rewritten only once the class is initialized.

**Exceptions** raised by the VM are real Java exceptions (`NullPointerException`, `ArrayIndexOutOfBoundsException`,
`ArithmeticException`, `ClassCastException`, `NegativeArraySizeException`, `ArrayStoreException`,
`IllegalMonitorStateException`, `StackOverflowError`, `OutOfMemoryError`, `NoClassDefFoundError`,
`NoSuchMethodError`, `AbstractMethodError`...). Handlers are searched frame by frame; synchronized methods release
their monitor as the exception leaves them. `Throwable` records the frames (method + pc) when it is made;
`printStackTrace` and uncaught exceptions print `at Class.method(File.java:line)` (or `pc N` without line numbers).
The stack has a reserve (512 slots, 24 frames) so `StackOverflowError` can still be constructed.

Speed, on an Apple M-series computer (`run.sh -bench`): BenchTest (fixed-point maths, pixel loops, virtual and
interface calls, strings) about **620 million bytecodes/s**, 300 ms where HotSpot's own interpreter (`java -Xint`)
takes 223 ms. The board build does not count bytecodes (`J2ME_COUNT`), moves 4-byte halves of slots for int/float/
reference values (32-bit pointers), and compiles the cold files with `-Os` (`#pragma GCC optimize`): ~118 KB of
code for the VM core, 1 KB of internal RAM (`.bss`).

## 5. Threads, step() and nested calls

All Java threads are green threads on the caller's OS thread. A thread has its own Java stack (`Vm::stackSlots`
slots, default 8192 = 64 KB on the board) and frame array. A `Vm` is not thread-safe: use it from one OS thread (or
task) only; to drop a game and everything it allocated, delete its `Vm`.

**`step(budgetUs)`** runs ready threads round robin, a slice of up to 4 ms each, until the budget is used, then
returns. It returns `false` when no Java thread is left (host threads from `newThread` do not count) or after
`System.exit`. After each call `idleUs` tells the host how long nothing will run:

| `idleUs` | meaning |
|---|---|
| 0 | threads still have work: call `step` again soon |
| n | every thread sleeps or waits with a timeout: the earliest wakes in n µs (sleep that long, or until a host event) |
| `Vm::kForever` | every thread waits without a timeout (`wait()`, `join()`, a monitor): only a host call can change that |

`Thread.sleep`, `wait`, `join`, a contended monitor or `yield` give the turn away; a sleeping thread costs nothing.
`runMain` loops `step` and sleeps on `idleUs`. `Object.wait(ms)`/`notify`/`notifyAll`, `Thread.interrupt` (wakes a
sleeping or waiting thread with `InterruptedException`), `isAlive`, `join`, `currentThread`, `activeCount` work;
priorities are kept but ignored.

**`call(t, m, args)`** (natives and host code calling Java: the MIDP layer's `paint`, `keyPressed`, `startApp`)
runs `m` on thread `t` to its return before it comes back, without switching to other threads while `t` can run.
If the Java code blocks (a monitor another thread holds, `wait()`, `sleep()`, `join()`), the other threads run
(and the OS sleeps when they all sleep, through `Vm::idleSleep` or `usleep`) until `t` can go on: a `paint()` that
waits for the game thread's lock works. If nothing can ever wake it (every thread blocked, no timeout), the VM
throws `Error("deadlock")` into it instead of hanging. A thread whose innermost activation is a native is never
scheduled meanwhile. Call Java for host events on a host thread: `Thread* ev = vm.newThread("event")` once, then
`vm.call(*ev, m, args)` (the Java code sees `Thread.currentThread().getName()` = "event", and monitors work against
the game's threads). `call()` returns the result (references kept, see the GC rule); an exception is left in
`t.pending`: a native must then return at once (the interpreter throws it on), host code must look at it and clear
it. `Yield()` inside a nested call does nothing. Calling a blocking native (like `Thread.sleep`) directly through
`call()` does not block.

**Hosts with their own loop** (the device): `vm.startMain(cls)` (or create the MIDlet through `call()`), then
`step()` in the loop and sleep on `idleUs`; `vm.stop()` ends every Java thread at once (no more Java code runs, not
even `finally`): between steps, or from a native (it takes effect when the native returns); the next `step()`
returns false; then delete the Vm.

**C stack**: a Java call to Java takes none (one interpreter loop runs all of a thread's frames). Each *nested*
level does: a native calling back into Java (`native -> call -> run`), a class initializer (`run -> initClass ->
call -> run`), building an exception. Measured on the board build (`-fstack-usage`): `run` 240 bytes, `call` 320,
`initClass` 128, `makeThrowable` 128, `blockUntilRunnable` + `schedule` + `runSlice` (a blocked nested call running
the others) ~210; so about 0.7 KB per nested level plus the natives' own frames. A 16 KB task stack leaves room for
~15 levels; real games nest 2-4 (startApp -> paint, a static initializer chain).

## 6. Natives

```cpp
static void sqrt(j2me::Vm& vm, j2me::Thread& t, j2me::Slot* args, j2me::Slot& ret) { ret.d = std::sqrt(args[0].d); }
vm.registerNative("java/lang/Math.sqrt(D)D", sqrt);
```

* The Java side declares the method `native`. The key is the internal class name, `.`, the method name and its
  descriptor. Registering before or after the class loads both work; a missing native throws
  `Error("native method missing: ...")` when called.
* `args`: the receiver first (instance methods), then one `Slot` per argument, long and double too (`args[1].j`).
  The result goes in `ret` (`ret.i`, `.j`, `.f`, `.d`, `.a`).
* Throw with `vm.throwNew(t, "java/lang/IllegalArgumentException", "why")` and return at once.
* Block the calling thread (like `sleep`): set `t.state = TState::Sleeping; t.wakeAt = vm.nowUs() + us;` and return;
  the thread resumes after the call when it wakes. `vm.wait(t, obj, ms)` blocks on a monitor the same way.
* Strings: `vm.str(obj)` (UTF-8), `vm.strChars(obj, &p)` (the UTF-16 chars in place, no copy: returns the length,
  -1 for null), `vm.newString(utf8)`, `vm.newString16(chars, n)`, `vm.intern(utf8)`.
* Arrays: `arr->length`, `arr->elems<int32_t>()` (int[]), `elems<int8_t>()` (byte[] and boolean[]),
  `elems<uint16_t>()` (char[]), `elems<Object*>()`; new ones: `vm.newArray('I', nullptr, n)`, references
  `vm.newArray('L', elemClass, n)`. Booleans arrive as `args[i].i` (0/1), objects, Strings and arrays as `args[i].a`.
* Fields: `Field* f = vm.findField(cls, "x", "I");` once, then `obj->slots()[f->slot].i`; statics:
  `f->addr->i`.
* Calls back into Java: `Method* m = vm.findVirtual(obj->cls, "paint", "(Ljavax/microedition/lcdui/Graphics;)V");`
  then `Slot a[2]; a[0].a = obj; a[1].a = g; vm.call(t, m, a); if (t.pending) return;`
* Keep the GC rule (section 2).

The `java.*` natives are in `vm_natives.cpp` (`Natives::` + `Vm::registerBuiltins`): Object (getClass, hashCode,
wait/notify, clone for arrays), Class (getName, isInstance, isArray, isInterface, isAssignableFrom, forName,
newInstance, getResourceAsStream: a `ByteArrayInputStream` over the resource, names relative to the class's package
unless they start with `/`), String (decode/getBytes: UTF-8 when asked for, else ISO-8859-1; intern), System
(currentTimeMillis, arraycopy with overlap and type checks, identityHashCode, getProperty), Runtime (exit,
freeMemory, totalMemory, gc), Float/Double (bits, Java's `toString` with the shortest digits, `parseDouble` with
Java's syntax), Math (sin, cos, tan, sqrt, ceil, floor), Thread, Throwable (stack traces), PrintStream (to
`Vm::out`, or line by line to the log).

`System.getProperty`: `microedition.platform` = EspOS-S3, `microedition.configuration` = CLDC-1.1,
`microedition.profiles` = MIDP-2.0, `microedition.encoding` = ISO-8859-1, `microedition.locale` = en-US; the host
adds more with `vm.setProperty`. `java.util.TimeZone.getDefault()` reads `com.espos.tz.offset` (minutes east of
GMT, 420 for Vietnam) and `com.espos.tz.id`; without them it takes the C library's local offset (the board's
`TZ`). `System.exit` ends the VM (`vm.exited`, `vm.exitCode`) unless `vm.allowExit` is false
(MIDP: then it throws `SecurityException`); the host can also stop the VM by setting `vm.exited = true`.

## 7. The class library (java.*)

CLDC 1.1's `java.lang`, `java.lang.ref` (`WeakReference` holds its referent strongly), `java.io` (streams, Data
streams, `Reader`/`Writer`, `InputStreamReader`/`OutputStreamWriter` for UTF-8 and ISO-8859-1) and `java.util`
(`Vector`, `Stack`, `Hashtable`, `Enumeration`, `Random` with the JDK's sequence, `Date`, `Calendar` (Gregorian),
`TimeZone` (fixed offsets, no daylight saving), and MIDP's `Timer`/`TimerTask`). Plus a few things javac needs for
newer source: `StringBuilder`, `Object.clone()` (arrays). Edit the Java there, then `j2me/build.sh`.

## 8. Limits

* No `invokedynamic` (lambdas, Java 9+ string concatenation), no Java 8 default methods in interfaces, no
  reflection beyond CLDC's `Class`, no finalizers, no weak references that clear, no class unloading, interned
  strings live forever.
* No bytecode verifier and few access checks (private/protected are not enforced).
* Encodings: UTF-8 and ISO-8859-1 only (others throw `UnsupportedEncodingException`).
* One OS thread; a native that blocks the OS thread blocks every Java thread.
* At most 65535 call sites become `invokedirect_q` (the rest stay on the slower path).
* Stack depth: `stackSlots` per thread (the frame array holds `stackSlots/4` frames).
