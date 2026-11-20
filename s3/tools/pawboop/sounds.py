"""PawBoop's game sounds for the board (2026-11-20, owner: "game chó ko có tiếng ... bên game gốc có tiếng đó"):
the phone game's sound kit (the game's hand-off folder, read only) into the packages, never the firmware.

  sfx.pba    PBA1 (formats.h): Opus 16 kHz mono, 60 ms packets; a clip's `on` = the sound's moment ("eat_crunch"),
             its variants are clips of the same moment (the board picks one, never the same twice in a row)
               vn.pawboop.dog.<dog>  the dog's own HOME sounds (the kit's dogs/<dog>/, batch 11/12)
               vn.pawboop.pet        the screen's taps, the coins, the toys, the generic dog sounds, music_menu
               vn.pawboop.run        the runner's sounds (jumps, slides, swipes, hits, coins, game over)
  music.pba  vn.pawboop.map.<map>: its tune ("music") and its ambience ("amb")
  sfx.cue    vn.pawboop.pet / vn.pawboop.run: which sound plays when (src/pet/sfx.cpp's struct Cue, 60 bytes a cue;
             the tables below), with the phone game's own volumes and pitches (the call's dB, the kit's volume_db
             applied to the sound itself as AudioManager does; the call's pitch x the kit's pitch_variation)

The sounds keep their loudness: only the format changes (Ogg Vorbis 44.1 kHz -> Opus 16 kHz mono; a stereo
file is mixed to mono). Nothing is levelled. The packages are the published ones with these files added and the
version bumped (everything else copied byte for byte).

  python3 tools/pawboop/sounds.py [--only pet,run,dogs,maps] [--out DIR] [--catalog] [--sim DIR] [--wav DIR]
    --out   where the new packages go (default: build-pkgs/espos/s3 beside the published ones)
    --wav   also write what the board's decoder gives for every clip (16 kHz WAV) there, to listen / compare
"""
import argparse
import json
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import espk  # noqa: E402

KIT = Path("handoff/audio")  # what the phone game loads (its manifest.json)
OUT = ROOT / "build-pkgs/espos/s3"  # the published packages are read from here
DEST = [OUT]                          # the new ones go here (--out: elsewhere, to try them first)
FF = "/opt/local/bin/ffmpeg"
RATE = 16000
KBPS_FX, KBPS_MUSIC = 24, 32
DOGS = ["bao", "momo", "boop", "leaf", "volt", "byte", "shiba", "corgi", "husky", "golden"]


def run(a, **k):
    return subprocess.run(a, capture_output=True, check=True, **k)


def ogg_packets(path):
    """the Opus packets of an Ogg Opus file, its pre-skip (48 kHz samples) and its length in 48 kHz samples"""
    b = Path(path).read_bytes()
    o, pk, cur, gran = 0, [], b"", 0
    while o < len(b):
        assert b[o:o + 4] == b"OggS", "not an Ogg page"
        g = struct.unpack_from("<q", b, o + 6)[0]
        ns = b[o + 26]
        lac = b[o + 27:o + 27 + ns]
        p = o + 27 + ns
        for n in lac:
            cur += b[p:p + n]
            p += n
            if n < 255:
                pk.append(cur)
                cur = b""
        if g >= 0:
            gran = g
        o = p
    assert pk[0][:8] == b"OpusHead" and pk[1][:8] == b"OpusTags"
    pre = struct.unpack_from("<H", pk[0], 10)[0]
    return pk[2:], pre, gran - pre


def encode(src, kbps, tmp, wav_dir=None, name=None):
    """an .ogg of the kit -> (packets, pre_skip at 16 kHz, samples at 16 kHz); the level is left as it is"""
    og = Path(tmp) / "x.opus"
    run([FF, "-hide_banner", "-loglevel", "error", "-y", "-i", str(src), "-ac", "1", "-ar", str(RATE),
         "-c:a", "libopus", "-b:a", f"{kbps}k", "-vbr", "on", "-application", "audio", "-frame_duration", "60", str(og)])
    pk, pre, n48 = ogg_packets(og)
    assert pre % 3 == 0 and all(0 < len(p) < 65536 for p in pk)
    if wav_dir:
        run([FF, "-hide_banner", "-loglevel", "error", "-y", "-c:a", "libopus", "-i", str(og), "-ac", "1",
             "-ar", str(RATE), "-c:a", "pcm_s16le", str(Path(wav_dir) / f"{name}.wav")])
    return pk, pre // 3, n48 // 3


def pba(clips):
    """clips: [(name, on, packets, samples)] (one pre-skip for all) -> PBA1 bytes (texts empty)"""
    pres = {c[4] for c in clips}
    assert len(pres) == 1, f"pre-skips differ: {pres}"
    tab, data = [], b""
    for name, on, pk, ns, _ in clips:
        assert len(name.encode()) <= 15 and len(on.encode()) <= 15, (name, on)
        blob = b"".join(struct.pack("<H", len(p)) + p for p in pk)
        tab.append((name, on, len(data), len(blob), ns, len(pk)))
        data += blob
    texts = b"\0"
    head_len = 20 + 48 * len(tab)
    head = struct.pack("<4sHBBHHII", b"PBA1", RATE, 1, 60, len(tab), pres.pop(), head_len, head_len + len(texts))
    rows = b"".join(struct.pack("<16s16sIIIHH", n.encode(), o.encode(), off, nb, ns, npk, 0)
                    for n, o, off, nb, ns, npk in tab)
    return head + rows + texts + data


def bump(v):
    x, y, z = map(int, v.split("."))
    return f"{x}.{y}.{z + 1}"


def code_of(v):
    x, y, z = map(int, v.split("."))
    return x * 10000 + y * 100 + z


def latest(pid):
    """the newest built .espk of a package: (version, path, suffix) — a core part is named <id>-<v>-core.espk"""
    best = None
    for p in OUT.glob(f"{pid}-*.espk"):
        rest = p.name[len(pid) + 1:-5]
        ver, _, part = rest.partition("-")
        if part not in ("", "core") or len(ver.split(".")) != 3 or not all(x.isdigit() for x in ver.split(".")):
            continue
        if best is None or tuple(map(int, ver.split("."))) > tuple(map(int, best[0].split("."))):
            best = (ver, p, "-core" if part else "")
    return best


def repack(pid, extra, note):
    """the newest package of pid + extra files {name: bytes}, version bumped. -> new path"""
    ver, src, suffix = latest(pid)
    m, files, b = espk.read(src)
    assert m["version"] == ver
    to = bump(ver)
    with tempfile.TemporaryDirectory() as tmp:
        t = Path(tmp)
        for name, off, size, crc in files:
            if name in extra:
                continue
            (t / name).parent.mkdir(parents=True, exist_ok=True)
            (t / name).write_bytes(b[off:off + size])
        for name, data in extra.items():
            (t / name).parent.mkdir(parents=True, exist_ok=True)
            (t / name).write_bytes(data)
        m2 = dict(m, version=to, code=code_of(to))
        (t / "manifest.json").write_text(json.dumps(m2, ensure_ascii=False))
        p = espk.build(t, DEST[0] / f"{pid}-{to}{suffix}.espk")
    jpg = OUT / f"{pid}-{ver}.jpg"
    if jpg.exists():
        shutil.copyfile(jpg, DEST[0] / f"{pid}-{to}.jpg")
    print(f"  {pid} {ver} -> {to}: {note}")
    return p


# ---------------------------------------------------------------- which sounds, when (the phone game's own calls)
# The device's moments, with the Godot code that plays them (godot_kit/: care_play = gameplay/scripts/pb_care_play.gd,
# care_ui = ui/scripts/pb_care_ui.gd, pb_run / pb_player = gameplay/scripts/). A cue: "t" ms from the clip's start
# (an event: its delay), "s" the sound (a dog's own kind first, then the shared ones), "db" the call's own dB (the
# kit's volume_db is in the sound already), "p" the call's pitch (range), "cd" ms (PBAudio.dog_sfx's cooldowns),
# "or" the next name of the call's list (played while "s" cools down: _sfx's behaviour), "pr" a chance in %,
# "once" only on the clip's first pass, "loop" fade in s / "out" fade out s for a loop.
CD = {"content_groan": 2200, "happy_sigh": 3000, "whine_beg": 2500, "excited_yips": 600, "play_growl": 1600,
      "play_bark": 500, "sniff": 700, "yawn_squeak": 3000, "lick_slurp": 500, "eat_crunch": 1800, "drink_lap": 1600,
      "grumble_talk": 2500, "sneeze": 1200, "leg_thump": 250}  # pb_audio.gd DOG_SFX_COOLDOWN (dog_sfx calls only)


def c(t, s, db=0, p=None, **k):
    q = {"t": t, "s": s, "db": db}
    if p is not None:
        q["p"] = p
    q.update(k)
    return q


def dsfx(t, s, db=0, p=None, **k):  # a dog_sfx / _sfx call: its kind's cooldown
    return c(t, s, db, p, cd=CD.get(s, 500), **k)


PET_CLIPS = {
    # trick_start:826 h_alert + bark_short -6 p1.2
    "alert": [c(0, "bark_short", -6, 1.2, once=1)],
    # _react:2008 head tap: h_giggle + giggle_pant -8; ear: giggle_pant -6 p1.15
    "giggle": [c(0, "giggle_pant", -8)],
    "ear_rub": [c(0, "giggle_pant", -6, 1.15)],
    # belly_rub: content_groan -5; after 0.8 s leg_thump -6 p0.95-1.1 (about every 0.48 s); release bark_happy -6
    "belly": [dsfx(0, "content_groan", -5)] + [dsfx(t, "leg_thump", -6, [0.95, 1.1]) for t in (800, 1280, 1760, 2240, 2720)]
             + [c(3000, "bark_happy", -6)],
    # bored v1: h_sad + whine_beg -7 p0.95
    "plead": [dsfx(200, "whine_beg", -7, 0.95, once=1, **{"or": "sad_whimper"})],
    # _picky:3755 E+0.9 s bark_short -8 p0.75 (the reluctant bite is silent)
    "yuck": [c(900, "bark_short", -8, 0.75)],
    # _turn_away bark_short -10 p0.8; hungry v2 the tummy rumble content_groan -5 p0.7
    "sulk": [c(0, "bark_short", -10, 0.8, once=1), dsfx(2000, "content_groan", -5, 0.7, once=1)],
    # _sneeze:1800 +0.35 s sneeze -4 p0.95-1.15
    "sick": [dsfx(350, "sneeze", -4, [0.95, 1.15])],
    # eat_lick_bowl: lick_slurp -7 p0.95-1.1 at its frames 8, 24, 50 (30 fps; the others cool down)
    "lick": [dsfx(t, "lick_slurp", -7, [0.95, 1.1]) for t in (270, 800, 1670)],
    # _eat_real:3605 eat_scoop_loop: a crunch every 8 frames (30 fps) _sfx("eat_crunch", -3, 0.95-1.08,
    # ["bone", "pup_rescue"]): the dog's own crunch every 1.8 s, the list's "bone" between; eat_up_chew:
    # eat_crunch -6 at its frame 12, lick_slurp -8 at frame 31; then the scoops again
    "eat": [dsfx(t, "eat_crunch", -3, [0.95, 1.08], **{"or": "bone"}) for t in range(300, 3700, 267)]
           + [dsfx(4200, "eat_crunch", -6), dsfx(4830, "lick_slurp", -8)]
           + [dsfx(t, "eat_crunch", -3, [0.95, 1.08], **{"or": "bone"}) for t in range(5000, 5700, 267)],
    # bath_start:3818 bucket_slosh -6; _in_pool bucket_slosh -8 p1.1; the foam's bubble_pops -12 p0.9-1.2;
    # _rinse:4173 sprinkler_hiss -8
    "bath": [c(0, "bucket_slosh", -6), c(1100, "bucket_slosh", -8, 1.1), c(1400, "bubble_pops", -12, [0.9, 1.2]),
             c(1700, "bubble_pops", -12, [0.9, 1.2]), c(1900, "sprinkler_hiss", -8)],
    # R+2.2 h_shake, R+2.45 bucket_slosh -8 p1.2; R+3.75 the towel: content_groan -6 (or happy_sigh)
    "shake": [c(450, "bucket_slosh", -8, 1.2), dsfx(1800, "content_groan", -6, **{"or": "happy_sigh"})],
    # nap:4732 t0.9 happy_sigh -4 (or giggle_pant); t2.2 dog_loop snore_soft -6 (fade in 0.6, out 0.8)
    "sleep": [dsfx(900, "happy_sigh", -4, **{"or": "giggle_pant"}), c(2000, "snore_soft", -6, loop=0.6, out=0.8)],
    # _nap_wake:4800 yawn_squeak -4
    "wake": [dsfx(0, "yawn_squeak", -4)],
    # sleepy: h_lie + happy_sigh -6 p0.85
    "lie_down": [dsfx(1000, "happy_sigh", -6, 0.85)],
    # pb_ball_play: throw play_bark -3 (or yip_jump) + turn_whoosh -8 p1.25 + pant_happy -8; bounces
    # bounce_beachball -12+min(vy,4) p1.25-1.45; leap yip_jump -5; grab boop_bark -6 p1.2; back: excited_yips -3
    "ball_fetch": [dsfx(300, "play_bark", -3, **{"or": "yip_jump"}), c(300, "turn_whoosh", -8, 1.25),
                   c(500, "pant_happy", -8, loop=0.6, out=0.8), c(700, "yip_jump", -5),
                   c(1200, "bounce_beachball", -8, [1.25, 1.45]), c(1700, "bounce_beachball", -11, [1.25, 1.45]),
                   c(3000, "boop_bark", -6, 1.2), dsfx(5600, "excited_yips", -3)],
    # waiting for the throw: bow / hop + bark_happy -6 p1.1
    "ball_ready": [c(300, "bark_happy", -6, 1.1, once=1)],
    # pb_toy_play: the bone 0.8 s later excited_yips -5; picked up play_growl -9 p1.15
    "bone_get": [dsfx(800, "excited_yips", -5), dsfx(2500, "play_growl", -9, 1.15)],
    # h_chew: rubber_duck_squeak -10 p1.05-1.5 (or squeaky_bark) at its frames 6.5 ... 84.1 (30 fps)
    "bone_chew": [c(t, "rubber_duck_squeak", -10, [1.05, 1.5], **{"or": "squeaky_bark"})
                  for t in (217, 383, 550, 717, 883, 1567, 1767, 1967, 2343, 2497, 2650, 2803)],
    # auto "beg":1530 h_beg + whine_beg -6 p1.05 (or sad_whimper)
    "beg": [dsfx(0, "whine_beg", -6, 1.05, **{"or": "sad_whimper"})],
    # care_play:2864 play_growl -3 (or boop_bark); yanks every 0.9-1.6 s play_growl -4 p0.95-1.08; the dog wins:
    # play_bark -3
    "tug": [dsfx(200, "play_growl", -3, **{"or": "boop_bark"})]
           + [dsfx(t, "play_growl", -4, [0.95, 1.08], **{"or": "boop_bark"}) for t in (1300, 2500, 3700)]
           + [dsfx(4800, "play_bark", -3)],
    # pb_toy_air: aim excited_yips -10 p1.2; throw play_bark -4 + turn_whoosh -8 p1.35 + pant -8; leap excited_yips
    # -4 p1.1 (or yip_jump); catch play_bark -6 p1.15 (or boop_bark); back excited_yips -3
    "frisbee": [dsfx(0, "excited_yips", -10, 1.2), dsfx(400, "play_bark", -4), c(400, "turn_whoosh", -8, 1.35),
                c(500, "pant_happy", -8, loop=0.6, out=0.8), dsfx(1500, "excited_yips", -4, 1.1, **{"or": "yip_jump"}),
                dsfx(1900, "play_bark", -6, 1.15, **{"or": "boop_bark"}), dsfx(3800, "excited_yips", -3)],
    # paw: h_high_five; +0.45 s high_paw_clap -4
    "high_five": [c(450, "high_paw_clap", -4)],
    # do_trick:4720 bark_happy -4 (or tut_success)
    "sit_spin": [c(0, "bark_happy", -4)],
    # _auto_nudge:1631 giggle_pant -14 p1.5; +0.9 s boop_bark -6 p1.3 (or squeaky_bark)
    "nudge": [c(0, "giggle_pant", -14, 1.5), c(900, "boop_bark", -6, 1.3)],
    # _butterfly:1737 +1.9 s h_hop + yip_jump -6 (or bark_short)
    "look_up": [c(1900, "yip_jump", -6)],
    # _auto_bone:1582 +0.8 s yip_jump -6; +1.5 / 2.2 / 2.9 s rubber_duck_squeak -4 p0.95-1.25
    "pounce": [c(800, "yip_jump", -6)] + [c(t, "rubber_duck_squeak", -4, [0.95, 1.25]) for t in (1500, 2200, 2900)],
}
PET_EVENTS = {
    "tap": [c(0, "ui_tap")],                      # care_ui:346 every round care button
    "sheet": [c(0, "menu_open", -4)],             # care_ui:784 FOOD opens the pantry
    # care_play:5503 the bowl lands (cargo_crate_drop -8 p1.3), _pour: 6 x score_tick -14 p1.4-1.7, 0.12 s apart
    "feed": [c(0, "cargo_crate_drop", -8, 1.3)] + [c(t, "score_tick", -14, [1.4, 1.7]) for t in range(100, 701, 120)],
    "love": [c(500, "bark_happy", -4, 1.15)],     # _eat_real: love only, E+0.5 s
    "no_coins": [c(0, "combo_break", -6)],        # care_ui.feed:765 "NEED n COINS"
    "no_tired": [dsfx(1500, "yawn_squeak", -5)],  # _refuse:627 sleepy
    "no_hungry": [dsfx(1700, "whine_beg", -6, **{"or": "sad_whimper"})],  # _refuse: hungry
    "levelup": [c(0, "level_up", -2)],            # care_ui._on_level:1649
    "gift": [c(0, "level_up", -2), c(0, "bark_happy", -2)],  # pb_home:2540 the daily gift opens
    "coin": [c(0, "coin_1", -2)],                 # pb_ui:2113 coins_fly
    "arrived": [c(0, "coin_1", -2)],
}
PET_MUSIC = {"home": {"s": "music_menu", "db": 0}}  # HOME keeps the menu's tune (pb_run.gd:717)

RUN_EVENTS = {
    "tap": [c(0, "ui_tap")],
    "start": [c(0, "go_horn")],                   # start_with_intro: go_horn at GO (the device has no intro)
    "left": [c(0, "swipe_left")],                 # pb_player._change_lane:640
    "right": [c(0, "swipe_right")],
    "jump": [c(0, "jump_boing"), c(0, "yip_jump", pr=35)],  # pb_player:741
    "land": [c(0, "land_plop", -4)],              # pb_player:1547
    "slide": [c(0, "slide_skid")],                # pb_player:772
    "coin1": [c(0, "coin_1")], "coin2": [c(0, "coin_2")], "coin3": [c(0, "coin_3")],  # play_coin(streak)
    "heart": [c(0, "purchase_unlock", -2)],       # +1 LIFE (_maybe_life:1909: life_up -> purchase_unlock)
    # a bump: pb_player.stumble stumble_trip + whine_hurt -6, _on_bump stumble_oops -2
    "hit": [c(0, "stumble_trip"), c(0, "whine_hurt", -6), c(0, "stumble_oops", -2)],
    "crash": [c(0, "crash_bonk"), c(350, "fall_over")],  # _on_crash:1845, _run_over:2004
    "results": [c(0, "game_over_womp", -3), c(0, "sad_whimper")],  # _show_results:2307
    "best": [c(0, "new_record", -2), c(0, "howl_win")],
    "gate": [c(0, "gate_whoosh", -3)],            # _gate_moment:2369
    "run": [c(0, "footsteps_loop", -6, loop=0.15, out=0.2)],  # _start_game_now:981, _tick_run:1222
}
RUN_MUSIC = {"menu": {"s": "music_menu", "db": 0}, "world": {"db": 0}, "amb": {"db": 0}}
WORLD_TUNE = {"city": "music_run"}  # no music_city: the run's own tune (PBAudio.world_music)

# The board's names are at most 15 bytes (pba_clip_t.on): these three are shortened, in the files and the cues.
SHORT = {"rubber_duck_squeak": "duck_squeak", "cargo_crate_drop": "crate_drop", "bounce_beachball": "ball_bounce"}


def shortened(table):
    for qs in table.values():
        for q in qs:
            for k in ("s", "or"):
                if k in q:
                    q[k] = SHORT.get(q[k], q[k])
    return table


DOG_KINDS = sorted({q["s"] for qs in list(PET_CLIPS.values()) + list(PET_EVENTS.values()) + list(RUN_EVENTS.values())
                    for q in qs} | {q["or"] for qs in PET_CLIPS.values() for q in qs if "or" in q}
                   | {"sad_whimper", "howl_win", "whine_hurt"})


# ---------------------------------------------------------------- the files
def cue_table(clips, events, music):
    """sfx.cue for src/pet/sfx.cpp: "PBC1", u16 count, u16 0, then 60-byte records (its struct Cue); a clip's cues
    together, in time order"""
    rows = []

    def add(name, q, typ):
        p = q.get("p", [1, 1])
        loop = 1 + round(q["loop"] * 20) if "loop" in q else 0
        for k in ("s", "or"):
            assert len(q.get(k, "").encode()) <= 15, q
        rows.append(struct.pack("<16s16s16sHHbBBBBBBB", name.encode(), q.get("s", "").encode(), q.get("or", "").encode(),
                                q.get("t", 0), q.get("cd", 0), round(q.get("db", 0)), round(p[0] * 100), round(p[1] * 100),
                                typ, loop, round(q.get("out", 0.2) * 20), q.get("once", 0), q.get("pr", 0)))
    for name, qs in clips.items():
        for q in sorted(qs, key=lambda q: q["t"]):
            add(name, q, 0)
    for name, qs in events.items():
        for q in qs:
            add(name, q, 1)
    for name, q in music.items():
        add(name, q, 2)
    return struct.pack("<4sHH", b"PBC1", len(rows), 0) + b"".join(rows)


def manifest():
    m = json.loads((KIT / "manifest.json").read_text())
    return {s["name"]: s for s in m["sounds"]}


def to16k(src, db, tmp):
    """the kit's file at its game volume (volume_db, as AudioManager plays it), 16 kHz mono float"""
    import numpy as np
    stereo = int(run(["/opt/local/bin/ffprobe", "-v", "error", "-select_streams", "a:0", "-show_entries",
                      "stream=channels", "-of", "csv=p=0", str(src)]).stdout.decode().strip() or 1) > 1
    af = (["pan=mono|c0=0.5*c0+0.5*c1"] if stereo else []) + [f"volume={db}dB", f"aresample={RATE}:resampler=soxr"]
    raw = run([FF, "-hide_banner", "-loglevel", "error", "-i", str(src), "-af", ",".join(af), "-ac", "1",
               "-ar", str(RATE), "-f", "f32le", "-"]).stdout
    return np.frombuffer(raw, np.float32)


def opus_of(x, kbps, tmp, wav_dir=None, name=None):
    import numpy as np
    w = Path(tmp) / "seg.wav"
    pcm = np.clip(np.round(x * 32767), -32768, 32767).astype("<i2").tobytes()
    import wave
    with wave.open(str(w), "wb") as f:
        f.setnchannels(1), f.setsampwidth(2), f.setframerate(RATE)
        f.writeframes(pcm)
    pk, pre, n = encode(w, kbps, tmp, wav_dir, name)
    assert abs(n - len(x)) <= 2, (name, n, len(x))
    return pk, pre, len(x)


def clips_of(S, name, on, kbps, tmp, wav_dir=None, split=False):
    """a kit sound -> PBA clips: each variant a clip of moment `on`; a tune that loops from loop_offset: its start
    ("on") and its looping part ("on_l")"""
    e = S[name]
    files = e.get("files") or [e["file"]]
    out = []
    for i, f in enumerate(files):
        x = to16k(KIT / f, e.get("volume_db", 0), tmp)
        nm = on if len(files) == 1 else f"{on[:12]}_{i + 1}"
        off = int(round(e.get("loop_offset", 0) * RATE)) if split else 0
        if off > 0:
            for part, seg in ((on, x[:off]), (f"{on}_l", x[off:])):
                pk, pre, n = opus_of(seg, kbps, tmp, wav_dir, part)
                out.append((part, part, pk, n, pre))
        else:
            pk, pre, n = opus_of(x, kbps, tmp, wav_dir, nm)
            out.append((nm, on, pk, n, pre))
    return out


def shared_names(events, clips, music):
    names = set()
    for qs in list(events.values()) + list(clips.values()):
        for q in qs:
            names.add(q["s"])
            if "or" in q:
                names.add(q["or"])
    return names, {v["s"] for v in music.values() if "s" in v}


def pitch_ranges(S, table, dog_any):
    """the call's pitch x the kit's pitch_variation: "p" [lo, hi]"""
    for qs in table.values():
        for q in qs:
            e = S.get(q["s"]) or S.get(f"{dog_any}_{q['s']}")
            pv = (e or {}).get("pitch_variation", 0) or 0
            p = q.get("p", 1.0)
            lo, hi = (p, p) if not isinstance(p, list) else p
            q["p"] = [round(lo * (1 - pv), 3), round(hi * (1 + pv), 3)]
    return table


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default="pet,run,dogs,maps")
    ap.add_argument("--catalog", action="store_true")
    ap.add_argument("--sim")
    ap.add_argument("--wav")
    ap.add_argument("--out", type=Path)
    a = ap.parse_args()
    if a.out:
        a.out.mkdir(parents=True, exist_ok=True)
        DEST[0] = a.out
    only = set(a.only.split(","))
    S = manifest()
    built = []
    wav = Path(a.wav) if a.wav else None
    with tempfile.TemporaryDirectory() as tmp:
        def wdir(sub):
            if not wav:
                return None
            d = wav / sub
            d.mkdir(parents=True, exist_ok=True)
            return d

        dog_names = set(DOG_KINDS)
        if "pet" in only:
            names, tunes = shared_names(PET_EVENTS, PET_CLIPS, PET_MUSIC)
            names = sorted(n for n in names if n in S)  # the shared ones (a dog's own kinds are in its package)
            clips = []
            for n in names:
                clips += clips_of(S, n, SHORT.get(n, n), KBPS_FX, tmp, wdir("pet"))
            for n in sorted(tunes):
                clips += clips_of(S, n, n, KBPS_MUSIC, tmp, wdir("pet"), split=True)
            cues = {"v": 1, "clips": shortened(pitch_ranges(S, json.loads(json.dumps(PET_CLIPS)), "corgi")),
                    "events": shortened(pitch_ranges(S, json.loads(json.dumps(PET_EVENTS)), "corgi")), "music": PET_MUSIC}
            built.append(repack("vn.pawboop.pet", {"sfx.pba": pba(clips), "sfx.cue": cue_table(cues["clips"], cues["events"], cues["music"])},
                                f"{len(clips)} sound clips ({', '.join(names)}; {', '.join(sorted(tunes))})"))
        if "run" in only:
            names, tunes = shared_names(RUN_EVENTS, {}, RUN_MUSIC)
            pet_names, _ = shared_names(PET_EVENTS, PET_CLIPS, PET_MUSIC)
            names = sorted(n for n in names if n in S and n not in pet_names)  # the pet's are loaded with the run too
            clips = []
            for n in names:
                clips += clips_of(S, n, SHORT.get(n, n), KBPS_FX, tmp, wdir("run"))
            cues = {"v": 1, "clips": {}, "events": shortened(pitch_ranges(S, json.loads(json.dumps(RUN_EVENTS)), "corgi")),
                    "music": RUN_MUSIC}
            built.append(repack("vn.pawboop.run", {"sfx.pba": pba(clips), "sfx.cue": cue_table(cues["clips"], cues["events"], cues["music"])},
                                f"{len(clips)} sound clips ({', '.join(names)})"))
        if "dogs" in only:
            for d in DOGS:
                clips = []
                kinds = sorted(k for k in dog_names if f"{d}_{k}" in S)
                for k in kinds:
                    clips += clips_of(S, f"{d}_{k}", k, KBPS_FX, tmp, wdir(d))
                built.append(repack(f"vn.pawboop.dog.{d}", {"sfx.pba": pba(clips)}, f"{len(clips)} clips of {len(kinds)} kinds"))
        if "maps" in only:
            for w in ["bazaar", "construction", "carnival", "city", "lantern", "park", "snow", "beach"]:
                tune = WORLD_TUNE.get(w, f"music_{w}")
                clips = clips_of(S, tune, "music", KBPS_MUSIC, tmp, wdir(w), split=True)
                # the tune's clips are named after their moment: "music" + "music_l"
                clips += clips_of(S, f"amb_{w}", "amb", KBPS_MUSIC, tmp, wdir(w))
                built.append(repack(f"vn.pawboop.map.{w}", {"music.pba": pba(clips)}, f"{tune} + amb_{w}"))
    for p in built:
        print(f"{p.name:44} {p.stat().st_size:>11,} B")
    if a.catalog:
        subprocess.run([sys.executable, str(ROOT / "tools/i18n.py"), "catalog"], cwd=ROOT, check=True)
    if a.sim:
        subprocess.run([sys.executable, str(ROOT / "tools/espk.py"), "--install-sim", a.sim] + [str(p) for p in built], check=True)


if __name__ == "__main__":
    main()
