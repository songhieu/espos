# MIDP layer tests

Tests for the MIDP 2.0 classes in `j2me/classlib/javax/microedition/**` and `j2me/classlib/com/nokia/**`.
Each is a plain `main()` class in the package it tests (so it can use package-private helpers). Each prints one
`FAIL <what>: got X want Y` line per failed check and ends with `<Name>: OK (<n> checks)` or
`<Name>: FAILED <k> of <n>`; the exit status is 0 only when everything passed.

| Test | What it checks |
| --- | --- |
| `javax/microedition/lcdui/game/GameTest.java` | Sprite reference pixel under all 8 transforms, collision rectangles under all 8, rectangle and pixel-level collisions (sprite / tiled layer / image, transformed frames), frame sequences, `setImage`, TiledLayer cells, animated tiles, tile sets, LayerManager order. Expected values are worked out by hand in the comments. |
| `javax/microedition/lcdui/LcduiTest.java` | Form focus and scrolling keys, item commands on the soft keys, ItemStateListener, touch taps vs drags, TextField / TextBox multi-tap input (modes, `*`, held keys, maxSize, NUMERIC, DECIMAL, PASSWORD), List IMPLICIT / EXCLUSIVE / MULTIPLE, Alert (dismiss, listener, modal with two commands, the dimmed screen behind, indicator), Gauge, DateField editing, CustomItem, POPUP ChoiceGroup; every screen painted once. |
| `javax/microedition/rms/RmsTest.java` | The record store file format round trip, record ids, open counts, enumerations (filter, comparator, keepUpdated), exceptions. |
| `javax/microedition/media/MediaTest.java` | Player states and events, loop counts, controls, content types, tone sequences. |

`host/com/espos/j2me/Native.java` stands in for the device's natives (`src/j2me/midp.cpp`): pure Java, it
really draws fills, lines, blits (all 8 transforms) and `drawRGB` into the pixel arrays, draws text as a block a
character (6 px wide; fonts 12 / 14 / 16 px), and records the soft key labels, tones and vibration for the tests to
read. It is never built into the device's class library.

## Running

On this computer's JDK (no VM needed):

    j2me/test/midp/run-host.sh            # all
    j2me/test/midp/run-host.sh GameTest   # names containing GameTest

On EspOS's own J2ME VM (the host runner from `tools/j2me`):

    make -C tools/j2me                    # once: tools/j2me/build/j2me-run
    j2me/test/midp/run-vm.sh              # builds the classlib + tests, then runs them all

`run-vm.sh` calls `j2me/build-midp.sh tests`, which compiles the tests into `j2me/build/test-midp` and puts a copy
of the class library with the stand-in `Native` into `j2me/build/test-midp-boot`; one test by hand:

    tools/j2me/build/j2me-run -boot j2me/build/test-midp-boot -cp j2me/build/test-midp javax.microedition.lcdui.game.GameTest

GameTest, RmsTest and MediaTest never call a native, so they also run with the real class library
(`-boot` left out); LcduiTest needs the stand-in.
