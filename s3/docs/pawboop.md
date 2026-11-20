# PawBoop on EspOS

Biscuit, the dog of the PawBoop phone game, as an app of the S3 desk (ES3C35P). The game is the game designer's handoff
(`pawboop_board_handoff`, BRIEF.md): same screen pieces, clips at 10 pictures a second, her 41 Vietnamese lines,
the same rules (game.json). Nothing about the rules or her lines was changed.

## Ten dogs, eight maps, the voices, the dog assistant (hand-off 3, 2026-11-18)

From the game designer's `handoff_all/` (BRIEF_ALL.md): all ten dogs of the phone game with their 29 pet clips and run clips,
176 Vietnamese lines each (44 moments x 4; Biscuit 179), the run's eight maps in the phone game's ring, and the dog
as the device's assistant (115 clips: 21 moments x 2 lines + the 73-clip number kit). The owner: every map and every
dog on the app (it uses the memory card now), each dog its own character and reactions, the dog as every device's
assistant (alarm, greetings by time of day, weather, habits, the number kit; no device, calendar or reminder lines).
Rules, lines and loudness unchanged.

**Packages** (`tools/pawboop/make_all.py` -> `build-pkgs/espos/s3/`, catalog via `tools/i18n.py catalog`, not uploaded;
files copied unchanged and checked against the hand-off's SHA-256 lists):

| Package | What | .espk |
|---|---|---|
| `vn.pawboop.pet` 2.0.2 | the game: `pawboop_ui.pbu` (the game designer's re-pack `handoff_ui2/`: no `say_*` bubbles, no "Biscuit" in `screen`/`splash`, `name_<dog>` and `wake_<dog>` pieces), `font/b12.bin` `b14.bin` `s11.bin` (Lexend, the page's font, every letter of every dog's lines, with its kerning: font file format 1, 51 KB), icon | 1,267,686 |
| `vn.pawboop.run` 2.0.0 | add-on: `core/run_things.pbs` (26 things), `core/run_ui.pbu`, `run.json` (8 maps); roads come with the maps | 4,248,101 |
| `vn.pawboop.dog.<dog>` 1.0.0 x 10 | add-on, `"set": "dog"`: `pet.pbs`, `run.pbs`, `voice.pba`, `assist.pba`, `icon.jpg` (the phone game's portrait) | 5.98-7.67 MB |
| `vn.pawboop.map.<map>` 1.0.0 x 8 | add-on, `"set": "map"`, `"needs": "vn.pawboop.run"`: `road_<map>.pbv` (20/s) + `link_<map>_<next>.pbv` (the gate to the next map of the ring), icon | 4.15-4.88 MB |

Dog keys (dogs.json order): bao (Bao), momo, boop, leaf, volt, byte, shiba (Rocket), corgi (Biscuit), husky (Frost),
golden (Sunny). The store shows PawBoop's page with its add-ons on shelves: "Add-ons" (Coin Run), "Dogs", "Run maps"
(`set`, sorted by name); a map is offered once Coin Run is in (`needs`). min_fw 501 (firmware 0.5.1).

**The chosen dog** (`src/pet/dogs.cpp`): setting `s3.pet_dog` (the key; synced like every `s3.*` setting). "" or a
dog not installed = the first installed one. It is the pet, the runner (`run.pbs`) and the assistant's voice. The
chooser (`src/pet/chooser.cpp`, "Choose a dog"): a tap on the dog's name in the game's header (the paw badge still
opens the book), "Choose another dog" in the book, or by itself when no dog is installed. Ten cards (portrait, name,
state, character); an installed dog is chosen with a tap; one that is not downloads with a tap (to the card when one
is in), shows its progress, then becomes the dog. `pet::switchDog()` closes the game, the book and the pop-up, lets
the old dog's pictures and voice go, loads the new dog's packs and opens the game again; a change from the web is
applied once nothing shows the old dog. PawBoop 1.0's own Biscuit packs (`biscuit_M.pbs`...) still play when no dog
package is installed.

**Lines** (`src/pet/voice.cpp`): a moment's line at random, never the one picked last for that moment nor the line
said just before (`voice::of`, 40 taps in the simulator: no repeat). The two beats: a tap on the dog while it is busy
on its own (`pounce`, `nudge`, `look_up`): the innocent face (`alert` or `plead`) with a `caught_1` line, held while
the line plays, then `giggle` (70 %) or `sulk` with the `caught_2` of the same index (`voice::answerTo`). The run says
`run_start` (start card), `run_hit` (a heart lost), `run_over` (dizzy), `run_best` (a new best, on the end card).

**Bubbles**: drawn from the line's text (the page's sayWrap/sayBox/bubble: one row up to 280 px, else the two rows
nearest in width, pill #2b2f3a radius 13, Lexend bold 12, the game's tail pieces), by `src/pet/text.cpp` straight
into the game's buffer with LVGL's glyphs. The header's name is the pack's `name_<dog>` piece (drawn in Lexend bold
14 at x 42 when absent); the splash's line is drawn translated ("{0} is waking up", no dots, Lexend semibold 11, centred,
middle y 266), not the English `wake_<dog>` piece. With PawBoop 1.0's pack ("Biscuit" baked in) both are covered first. Alignment (2026-11-18): drawn
text uses fractional advances with Lexend's kerning (as the page's canvas: a bubble came out 5 px wider with LVGL's
whole-pixel advances) and the page's "middle" line (2 px higher than LVGL's line box); bubbles now match pet.html to
1 px, the static pieces exactly (the header 24 px lower for the status bar). The pop-up's bubble point touches the
bubble, its chips are 44 px (two lines), the alarm card's words are centred, the run's "‹ ›" sit beside the card. The pop-up, the alarm card and the widget show the dog's words
in Lexend too (every Vietnamese letter without the Vietnamese font package).

**Voice files from the card**: only a voice file's header, clip table and texts are in PSRAM (~20 KB); a line's packets
are read when it is said (on the voice task). Clips play as sequences with gaps (`voice::say`): the line, 260 ms, the
kit's clips 20 ms apart, decoded into one stream.

**The assistant** (`src/pet/assist.cpp`, pop-up `summon.cpp`):

| Moment | When |
|---|---|
| `alarm_1/2/3` + "bây giờ là ..." | an alarm rings (its 1st, 2nd, 3rd+ time: again after each snooze), on the alarm's screen (the dog in a card under the time, its words beside it); again every 45 s with its other line; the beeps wait while it talks |
| `alarm_snooze` + "báo thức lúc ..." / `alarm_off` | "Snooze 9 min" (the time it rings again) / "Turn off", in the pop-up |
| `hi_morning` 4-10, `hi_noon` 11-12, `hi_afternoon` 13-17, `hi_evening` 18-21, `good_night` 22-23, `late_night` 0-3, `long_time` (3+ days) | the first touch at Home in a new part of the day, or after 3 h away; the night ones also in quiet hours (someone is touching the device), never in Do Not Disturb |
| `w_air` (AQI > 150), `w_rain` (rain/storm now or in the next two forecast slots, or 60 %+), `w_hot` (34 C+), `w_cold` (17 C-), `w_nice` + "ngoài trời đang ... độ" | with the first daytime greeting of the day; `w_rain` alone when rain comes (at most every 6 h); the "Weather?" chip |
| `h_water` (every 2 h, 8-21 h), `h_move` (90 min at the device), `h_eyes` (every hour at the device), `h_meal` (11:30, 18:30) | while someone is at the device (touched in the last 30 min) and it shows Home or the saver (wakes it); 20 min between pop-ups; never in quiet hours |
| the kit alone | the "What time is it?" chip |

The number kit exactly as `dogs.json` / `sim/assist.html` timeClips/tempClips: `t_now n_<1..12> u_gio` then `u_ruoi`
(:30) / `u_dung` (:00) / `n_<m> u_phut`, then `p_sang` (4-10) `p_trua` (11-12) `p_chieu` (13-17) `p_toi` (18-21)
`p_dem`; alarm time `t_alarm` + the same; temperature `t_out n_<0..59> u_do`. The kit's words show under the line in
orange, lower case, as the page. Settings: `s3.assist` = `{"on","greet","weather","alarm","water","move","eyes","meal"}`
(minutes, 0 = off), switches in the dog's book ("ASSISTANT"). Alarm hook: `alarms::setHook` (alarm.cpp tells ringing /
snoozed / off). The saver no longer covers a ringing alarm (os.cpp).

**Memory** (simulator, the board's PSRAM model, everything on the card): PSRAM at the run's start 3729 KB in use, as
before (3736). For that the game lets the card's big pieces go (`pk::trim`: screen and splash, 600 KB) when the run
opens, and the run reads its start cards (`menu_<map>`, 110 KB each) one at a time. Fonts 37 KB, a dog's tables
~50 KB, a voice file's table ~20 KB in PSRAM. Internal RAM: nothing new kept.

**Tests** (`tools/pawboop/tests/`, `SIM_DATA=sim/data-dogs SIM_NO_PACKS=1 SIM_FW_CODE=501`, `SIM_CLOCK="YYYY-MM-DD
HH:MM:SS"` sets the simulator's wall clock, script command `clock`): `smoke`, `dogs` (every dog: game, two beats, its
run), `maps` (live simulator: every map, its gate, the start card's arrows), `alarm` (three rings, snoozes, off),
`assist` (greetings, chips, a habit, good night, late night, long time), `store`, `download` (`sim/data-dogs2`: a dog
from the chooser, a map's page). `make_all.py --sim DIR` installs everything on a simulator card.

### Board fix 0.5.4 (2026-11-18): PawBoop froze on a test board

- Cause: `text.cpp` asked LVGL for the bitmap of a letter no font of the chain had (PawBoop 1.0's pack has no Lexend
  files, so the OS's fonts drew the bubble and "…" was missing). LVGL answers a placeholder with `resolved_font` NULL;
  `lv_font_get_glyph_bitmap` then hits `LV_ASSERT_NULL`, and `LV_ASSERT_HANDLER` is `while(1);`: the UI stopped for good
  (serial: `lv_font_get_glyph_bitmap: Asserted at expression: font_p != NULL`). Fix: a letter without a font is a gap,
  never drawn; a "…" the fonts lack becomes "..." (`forFont`). Reproduced and fixed in the simulator
  (`SIM_DATA=sim/data-dogs-old`, the line alert_4) and on the board.
- A dog's picture records are read the first time a picture is used (they lie before each picture, spread over the
  5-7 MB file): switching to a dog on the card took 836 ms on the UI thread, now 15 ms.
- The store's PawBoop page drew itself and the list under it again for every add-on icon that arrived (1.1-1.6 s each,
  ~20 s of stalls); now once when all are in (one 1.5 s build when it opens). The chooser updates the download's
  percent and the arriving pictures in place instead of building its ten cards again every 0.7 s.
- Coin Run 2.0 with no map installed: the pill opens the store at the Run maps shelf ("Add a map for Coin Run first")
  instead of the dead end "No roads installed".
- Measured on the board (0.5.4, PawBoop 1.0 in flash + Bao on the card): game 100 pictures/10 s, compose ~39 ms, decode
  ~39 ms, tap to picture 150-220 ms (taps that start a line; 50-100 ms otherwise), voice underruns 0, internal RAM 59 KB
  minimum, PSRAM 3.5 MB free in the game; no assert over a 2-minute tap soak.

## Sounds: the phone game's sound kit (2026-11-20)

Owner: "game chó ko có tiếng ... bên game gốc có tiếng đó". The ESP32 hand-off had the voice only; the Godot game
(`the game's hand-off folder`, read only) plays a sound kit through `AudioManager.gd`. Its sounds are now in
the packages (`tools/pawboop/sounds.py`), played by `src/pet/sfx.cpp`:

- `sfx.pba` (PBA1, Opus 16 kHz mono, a clip's `on` = the sound, variants = clips of the same `on`): the dog's own home
  sounds in `vn.pawboop.dog.<dog>` 1.0.3 (barks, groans, crunches, snore and pant loops: 42 clips, ~150 KB), the
  shared ones and `music_menu` in `vn.pawboop.pet` 2.0.3, the runner's in `vn.pawboop.run` 2.0.1. A map's tune and
  ambience: `music.pba` in `vn.pawboop.map.<map>` 1.0.1 (`music` + `music_l`, the part the tune loops from its
  loop_offset; `amb`). City has no music_city: music_run, as the game.
- `sfx.cue` (pet, run): which sound when, from the game's own calls (the tables in `sounds.py` cite file:line): a
  picture clip's sounds at their times, the screen's and the run's moments, the places' tunes. The call's dB and pitch;
  the kit's volume_db is applied to the sound itself (as AudioManager plays it), its pitch_variation widens the pitch
  range. A random variant, never the same twice in a row; dog_sfx's cooldowns, and the game's quirk kept: a crunch that
  is cooling down plays the list's next name ("bone") instead.
- Mixing: one stream (`sound::stream`) carries the dog's voice, up to 10 effects (decoded whole into a PSRAM cache, 512
  KB at most), the tune and the ambience (streamed by task `petfx`, core 0, PSRAM stack); the tune ducks 7.5 dB under
  the voice (the game's compressor). lib/opus has one global scratch: every opus call holds `voice::opusLock()`.
- Silent (not softer: the assistant hears the room): the dog's pop-up up or a turn on, Do Not Disturb, quiet hours,
  volume 0, the game not on screen (the book, the saver), and the book's two switches "Game sounds" (effects +
  ambience, the game's SOUND) and "Background music" (MUSIC). The dog's voice keeps its own switch. A peak limiter at
  full scale keeps the sum from clipping (the game's Master has hard limiters); a voice alone is never touched.
  `sfx::bedOnly(ms)`: this mix is what the speaker plays and it carried no voice and no effect for ms (only a tune /
  ambience): the wake word listens through a tune at a higher bar (agreed with wakeword, 2026-11-20). No touch for
  30 s: the tune and the ambience stop until the next touch (a tune masks a call from across the room). The owner
  (2026-11-20): tunes and ambience 18 dB quieter than the phone game's (`BED_DB`, a mixer gain; the files keep their
  level; effects and the dog's lines unchanged), on by default.
- Not ported (no such moment on the device): the announcer `vo_*` (the dog's Vietnamese lines take their place),
  Granny, the intro and countdown, puppies, power-ups, chaos, traffic.
- The game's quirks, kept (for the owner to decide): a dog sound on cooldown plays its call list's next name, so most
  eating crunches are the "bone" pickup sound; HOME plays music_menu and the ambience of the last world a run was in
  (the game never stops it; before any run, the run's start map); a light bump plays stumble_trip + whine_hurt +
  stumble_oops together; the eat clip's bowl and pour come before the device's eating picture starts.
- Tests: `tools/pawboop/tests/sounds.txt` (script simulator: one-shots, SIM_WAV) and `sounds-live.txt` (live
  simulator: loops, tunes, the run; SIM_WAV records the speaker in real time). Use a daytime `SIM_CLOCK` (quiet hours
  silence everything).

## What is where

| Path | What |
|---|---|
| `src/pet/pet.h` | public API (hooks below) |
| `src/pet/app.cpp` | the game screen: composes the designer's 320x480 screen into one RGB565 buffer in PSRAM shown by an `lv_image`, redraws only changed rows; behaviour ported from `sim/pet.html` |
| `src/pet/game.cpp`, `brain.cpp` | rules, state, saving (NVS namespace `pet`); clip roles and chains |
| `src/pet/dog.cpp`, `tjpgd_pb.c` | the dog's pictures: JPEG (a private TJpgDec copy, RGB565 out, fast Huffman) + mask -> soft-edge alpha, blob shadow, full or half size, 3-slot cache, background decoder task (core 0, stack in PSRAM) |
| `src/pet/voice.cpp` | Opus lines decoded by a task (core 0, stack in PSRAM) into rings, played through the sound mix (`sfx.cpp`) |
| `src/pet/sfx.cpp` | the game's sounds: the mix (voice + effects + tune + ambience), cue table, decoding task `petfx` |
| `src/pet/view.cpp` | `pet::View`: Biscuit playing clips inside any LVGL object (overlay, widget, book; the future assistant) |
| `src/pet/summon.cpp` | the assistant mockup overlay (no AI) |
| `src/pet/widget.cpp`, `book.cpp` | home widget; her book (needs, streak, moments album, switches) |
| `src/pet/packs.cpp` | finds and memory-maps the packs (`pb_dog`, `pb_ui`, `pb_voice` partitions); simulator: `sim/data/pawboop/` |
| `src/pet/pet_icon.c` | app icon (`tools/pawboop/make_icon.py`, from a picture of the pack) |
| `lib/opus/` | libopus 1.4 decoder files, fixed point, unchanged (BSD). Decodes the voice pack bit for bit like the Mac's libopus |
| `src/sound.cpp/.h` | + `sound::stream(pull, ctx)`, `stopStream()`, `streaming()` (clicks mix in, chime/test/alarm stop it) |
| `partitions_pawboop.csv` | the table with room for the packs (not in use yet, see below) |

Layout note: the OS status bar keeps the top 24 rows, so the game's header and needs sit 24 px lower and cover the
stage's top 24 rows (30 of 1120 pictures reach there: the pool in `bath`, the ball in `ball_fetch`/`ball_ready`).
From the stage down everything keeps the page's screen coordinates. A tap on the paw badge / "Biscuit" opens her book.

## Hooks (for the OS)

```
apps.cpp:  {"pet", "PawBoop", &ic_pet_52, &ic_pet_40, &ic_pet_28, app_pet::open, nullptr}   (done, #ifndef NO_PET)
wid.cpp:   kind "pet"; else if (k == "pet") pet::widget(c.o, w, h, it.size);               (done, #ifndef NO_PET)
OS loop:   pet::tick();             every 500 ms (cheap; calls closer than 400 ms are ignored)
CC tile:   pet::summon("cc");       gesture: pet::summon("gesture");   or apps::open("pet", "summon")
```
`pet::tick()` also runs from its own 500 ms LVGL timer once any PawBoop part was used (widget, app, summon), so the
two together are harmless. Reminders: tick() watches `s3.todo`; a reminder checked off on the device (someone
touched it in the last 5 s) makes Biscuit pop up and celebrate. No hook needed.

## Extras (built from the existing clips, lines and pieces)

- **Home widget** S/M/L: a still picture of her current mood (no animation on Home), her four needs with the game's
  icons, and (M/L) her own line for the mood. Redrawn when the mood changes; gauges every 30 s.
- **Notifications** when she becomes hungry, sleepy or bored: title in plain words, body = her own line (sulk,
  lie_down, plead). At most once per mood every 3 h, never in Do Not Disturb or quiet hours, never while the game is
  in front, not in the first minute after power-on. Switch in her book.
- **Morning greeting**: the first touch at Home between 6:00 and 11:00 (once a day, not in DND/quiet hours): she pops
  up with `hello` and the day's date and open reminders.
- **Reminder celebration**: `high_five` + its line + "Xong một việc rồi! Còn N việc".
- **Her book** (`apps::open("pet", "book")` or the paw badge): Biscuit moving, level/XP/coins, needs, days-in-a-row
  streak (any care action counts, needs the clock), a **moments album** (first meal she loved, first bath, first nap,
  first high five, first coin she found, each level, 3/7/30/100-day streaks; each card is the picture of the clip
  she was playing then), switches (voice on/off, notifications on/off), and the assistant demo button.
- **Coins she finds while you are away**: the "happy dog brings a coin" rule also runs while the app is closed
  (same timer, same 3-coin cap); they wait on the floor by her when you open the game.
- **Day's gift** with the real clock: once per calendar day on the first open (without a clock: 6 powered hours, as
  the brief). A new Biscuit gets her first gift the next day, so she starts with 50 coins as `start.board` says.
- Pressed button look (0.15 s darker), the page's 2-picture blend between clips.

Not changed: rules, numbers, lines, voice level. Needs fall only while the board is on (as the brief).

## Storage: the packs need a new partition table (USB once)

The packs are 7.5 MB and are read in place. The current table (`default_16MB.csv`: 2 x 6.25 MB apps, 3.4 MB
LittleFS) has no room, and a partition table cannot change over OTA. `platformio.ini` is NOT changed. Until the
switch, the app shows "Cần cắm cáp USB một lần để chép dữ liệu PawBoop (7,5 MB)" and everything else works.

`partitions_pawboop.csv` (checked with gen_esp32part.py). **Done on the dev board on 2026-11-16 over USB**
(full flash backup first: backup/s3-full-before-pawboop-20261116.bin, read in 1 MB chunks because one
16 MB read kept failing; NVS backup s3-nvs-before-pawboop-20261116.bin). `platformio.ini` now builds for this table.

| Name | Offset | Size | Holds |
|---|---|---|---|
| nvs | 0x9000 | 20 KB | unchanged: link token, WiFi, `pet` save |
| otadata | 0xE000 | 8 KB | reset by the switch |
| app0 / app1 | 0x10000 / 0x3D0000 | 3.75 MiB each | firmware 3.27 MB with PawBoop -> ~660 KB free (room for an app runtime) |
| pb_dog | 0x790000 | 5,349,376 | biscuit_M.pbs (the small dog only, owner's choice) |
| pb_ui | 0xCAA000 | 1,978,368 | pawboop_ui.pbu |
| pb_voice | 0xE8D000 | 200,704 | biscuit_voice_12k.pba (owner: lightest compression) |
| spiffs (LittleFS) | 0xEBE000 | 1.2 MiB | settings, dashboard cache, at most 8 photos (photo.cpp) |
| coredump | 0xFF0000 | 64 KB | unchanged |

The web installer's bundle (`tools/make_bundle.py`, fw-s3/latest.json from 0.2.2) carries the table and the packs, so a
new board gets everything in one USB install. OTA images must stay under 0x3C0000 = 3,932,160 bytes.

Measured on the board (2026-11-16, 0.2.2): 84-101 pictures/10 s, compose avg ~40 ms (max ~120), JPEG+mask decode avg
~33-45 ms on the background task, tap to picture ~135 ms, voice decode ~3 ms per 60 ms packet, 0 underruns after the
first packet is decoded before the speaker starts, first sound ~3 ms after speak. Packs mmap in 2 ms.

### Switch steps (board on USB, by hand; done once on the dev board)

```
cd s3
PY=~/.platformio/penv/bin/python
ET="$PY ~/.platformio/packages/tool-esptoolpy/esptool.py --chip esp32s3 -p $PORT -b 921600"
$ET flash_id                                   # must say ESP32-S3, 16MB (the X4 uses the same USB id)
# 1. On the device: Settings -> sync now (device-side setting changes not yet uploaded live in LittleFS).
# 2. Backups
$ET read_flash 0x9000 0x5000 backup/s3-nvs-before-pawboop.bin
$ET read_flash 0 0x1000000 backup/s3-full-before-pawboop.bin      # 16 MB, a few minutes
# 3. The table and a firmware built for it (set board_build.partitions = partitions_pawboop.csv, then pio run)
$PY ~/.platformio/packages/framework-arduinoespressif32/tools/gen_esp32part.py --flash-size 16MB \
    partitions_pawboop.csv /tmp/partitions_pawboop.bin
# 4. Write table, OTA data (boot app0), firmware, packs
$ET write_flash 0x8000 /tmp/partitions_pawboop.bin \
    0xe000 ~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin \
    0x10000 .pio/build/s3desk/firmware.bin \
    0x730000 sim/data/pawboop/biscuit_M.pbs \
    0xC4A000 sim/data/pawboop/pawboop_ui.pbu \
    0xE2D000 sim/data/pawboop/biscuit_voice_12k.pba
```
What happens: NVS stays (still linked, WiFi kept, game save kept). LittleFS moved and shrank, so it fails to mount and
`LittleFS.begin(true)` formats it: settings cache, dashboard cache and photos are rebuilt by the next sync. The old
app1 (rollback image) is overwritten by the packs. otadata is reset so app0 boots. Bootloader untouched.
After the switch OTA keeps working; OTA images must stay under 0x390000 = 3,735,552 bytes (`esp_ota_begin` refuses
bigger ones). Later pack updates could go over WiFi (erase + write a `pb_*` partition); not built.

## Measuring on the board (nothing measured yet)

All lines are in the TRACE history (`tools/s3cli.py PORT` -> `TRACE`):

| Trace line | What to read |
|---|---|
| `pet: packs ok (... N ms)` | mmap time at first use |
| `pet: P pics/10s, compose avg/max us, R rows/pic, late L \| decode N (ahead A, inline I) avg/max us` (every 10 s while the game is in front) | pictures a second (target 100/10 s), compose time (LVGL thread), JPEG+mask decode time, how many decodes the background task did vs inline (inline should be ~1 per clip change), late clock steps |
| `pet: voice L lines, P packets, decode avg/max us, underruns U, first sound F us` | Opus time per 60 ms packet, underruns, speak -> first samples |
| `pet: tap to picture N ms` | tap -> first changed picture on the buffer (+ one LVGL refresh) |
| existing `heap ...` Serial line, `slow loop`, `heap after` | push time per frame, internal heap/PSRAM, stalls |

Plan: (1) open PawBoop, hear `hello`, read TRACE; (2) 60 s with the game in front: 6 stat lines; targets >= 99
pics/10 s, decode avg < 40 ms (TJpgDec, estimate only), compose < 15 ms; (3) 10 lines while drawing: 0 underruns,
first sound < 100 ms; (4) 10 taps on PET: tap to picture < 150 ms; (5) one hour soak: no Guru Meditation, heap and
PSRAM flat; (6) eye check on the panel: colours (orange dog, not blue), no tearing on the stage, speaker not
distorting (if it does: lower `VOICE_GAIN` in voice.cpp, one gain for all lines).
If decode is too slow: put TJpgDec's 16 KB pool in internal RAM, or switch to Espressif's esp_new_jpeg (SIMD; a
prebuilt copy is in managed_components).

Memory while the game is in front (PSRAM): screen 292 KB, blend copy 171 KB, 3 picture slots 663 KB, 2 decoders
~230 KB (+291 KB once a half-size picture was drawn), voice ~234 KB (decoder 18 KB, opus scratch 120 KB, rings
96 KB). Closing the game frees the pictures. Internal RAM: +0.9 KB static; both task stacks are in PSRAM.

## Simulator

`make -C sim -j10` builds src/pet and lib/opus too; packs come from `sim/data/pawboop/` (or `PAWBOOP_PACKS=dir`).
`PAWBOOP_SPEED=240` makes time 240x faster (needs, coins found). `SIM_WAV=file.raw` writes everything streamed to the
speaker (16 kHz mono s16le). `app pet`, `app pet summon`, `app pet book` open the parts. Mac timings say nothing
about the board.
