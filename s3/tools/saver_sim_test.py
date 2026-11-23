#!/usr/bin/env python3
"""The simulator test of JavaScript screen saver apps (docs/app-slots.md §4, lane 4a, src/js/jssaver.*):
examples/saver-bubbles in the saver slot (frames move, a touch leaves, the screen turned, the night page at night and
the app again in the morning), then broken saver apps: one that throws, one too slow, one that eats memory, one that
does not parse; three stops in ten minutes block it with one notification, choosing it again lets it start.

  make -C sim BUILD=build-saver EXTRA="-fsanitize=address -fno-omit-frame-pointer"   (or any sim build)
  (cd examples/saver-bubbles && node ../../sdk/packages/cli/bin/espos.js build)
  python3 tools/saver_sim_test.py [--sim sim/build-saver/espos-sim] [--out DIR]

Exit status 0 when every check passes; screenshots (PNG) in OUT.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUBBLES = "dev.espos.bubbles"
ESPK = ROOT / "examples/saver-bubbles/dist" / f"{BUBBLES}-1.0.0-core.espk"

# Broken saver apps (fixtures): id -> saver.js
BROKEN = {
    "dev.espos.test.saver.throw": "let n = 0;\napp.onFrame = (g) => { g.clear('#203040'); if (++n > 3) throw new Error('bad frame ' + n); };",
    "dev.espos.test.saver.slow": "app.onFrame = (g) => { const t = Date.now(); while (Date.now() - t < 400) {} g.clear('#402020'); };",
    "dev.espos.test.saver.hog": "const keep = [];\napp.onFrame = (g) => { for (let i = 0; i < 2000; i++) keep.push(new Array(64).fill(i)); g.clear('#204020'); };",
    "dev.espos.test.saver.parse": "app.onFrame = (g) => { g.clear( ;",
    "dev.espos.test.saver.shapes": "app.fps(20);\napp.onFrame = (g) => { g.clear('#000000'); for (let i = 0; i < 900; i++) g.rect(i % 300, (i * 7) % 460, 4, 4, '#ffffff'); g.text('x'.repeat(5000), 0, 0); };",
}


def install(data):
    shutil.rmtree(data, ignore_errors=True)
    shutil.copytree(ROOT / "sim/data-apps", data)
    subprocess.run([sys.executable, str(ROOT / "tools/espk.py"), "--install-sim", str(data), str(ESPK), "--vol", "flash"],
                   check=True, capture_output=True)
    for pid, src in BROKEN.items():
        d = data / "fs/pkgs" / pid
        d.mkdir(parents=True)
        b = src.encode()
        (d / "saver.js").write_bytes(b)
        m = {"id": pid, "name": pid.split(".")[-1].title(), "version": "1.0.0", "code": 10000, "kind": "js", "engine": "js",
             "provides": ["saver"], "parts": ["core"], "files": [{"n": "saver.js", "s": len(b), "c": zlib.crc32(b)}]}
        (d / "manifest.json").write_text(json.dumps(m))
    s = json.loads((data / "settings.json").read_text())
    s = {k: v for k, v in s.items() if not k.startswith("app.") and not k.startswith("slot.")}
    s["sys.lang"] = "en"
    (data / "settings.json").write_text(json.dumps(s, ensure_ascii=False))


def set_slot(data, pid):
    s = json.loads((data / "settings.json").read_text())
    s["slot.saver"] = pid
    (data / "settings.json").write_text(json.dumps(s, ensure_ascii=False))


def png(ppm, out):
    try:
        from PIL import Image
        Image.open(ppm).save(out)
        return out
    except Exception:  # noqa: BLE001
        return ppm


def differs(a, b):
    try:
        from PIL import Image, ImageChops
        return ImageChops.difference(Image.open(a).convert("RGB"), Image.open(b).convert("RGB")).getbbox() is not None
    except Exception:  # noqa: BLE001
        return Path(a).read_bytes() != Path(b).read_bytes()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sim", default=str(ROOT / "sim/build-saver/espos-sim"))
    ap.add_argument("--out", default=str(ROOT / "sim/shots/saver-apps"))
    a = ap.parse_args()
    out = Path(a.out)
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    data = ROOT / "sim/data-saverapps"
    fails = []

    def expect(ok, what):
        print(("ok    " if ok else "FAIL  ") + what)
        if not ok:
            fails.append(what)

    def sim(name, script):
        o = out / name
        o.mkdir()
        (o / "script.txt").write_text(script)
        env = dict(os.environ, SIM_DATA=str(data), ASAN_OPTIONS="detect_leaks=0:abort_on_error=1", SDL_VIDEODRIVER="dummy")
        p = subprocess.run([a.sim, str(o / "script.txt"), str(o)], env=env, capture_output=True, text=True, timeout=600)
        (o / "sim.log").write_text(p.stderr)
        expect(p.returncode == 0, f"{name}: the simulator exits 0 (got {p.returncode})")
        expect("AddressSanitizer" not in p.stderr and "runtime error" not in p.stderr, f"{name}: no ASan/UBSan report")
        for f in o.glob("*.ppm"):
            png(f, out / f"{name}-{f.stem}.png")
        return p.stderr, o

    try:
        install(data)
        set_slot(data, BUBBLES)
        # 1. Bubbles: frames move; a touch leaves; the screen turned; the night page at night, the app in the morning.
        log, o = sim("bubbles", """clock 2026-11-22 10:00
wait 1500
mem before
saver
wait 1500
shot 01-saver
wait 1200
shot 02-saver-later
mem saver-on
tap 160 240
wait 800
shot 03-woken
mem after
rot 1
wait 800
saver
wait 1500
shot 04-landscape
tap 240 160
wait 600
rot 0
wait 800
clock 2026-11-22 23:30
saver
wait 1500
shot 05-night
clock 2026-11-23 07:30
wait 2500
shot 06-morning
tap 160 240
wait 600
app settings defaults
wait 1000
shot 07-defaults
quit
""")
        started = log.count(f"saver app {BUBBLES} started")
        expect(started == 3, f"bubbles: started three times (saver, landscape, morning) (got {started})")
        expect(f"saver app {BUBBLES} ended:" in log and "frames in" in log, "bubbles: ends with its frame count when touched")
        expect(differs(o / "01-saver.ppm", o / "02-saver-later.ppm"), "bubbles: the frames move")
        expect(differs(o / "03-woken.ppm", o / "02-saver-later.ppm"), "bubbles: a touch leaves the saver")
        expect("saver page" in log.split("clock 2026-11-22 23:30")[-1] or "saver page port 1 night" in log,
               "bubbles: the night page at night")
        night = log[log.find("saver page port 1 night"):] if "saver page port 1 night" in log else ""
        expect(f"saver app {BUBBLES} started" in night, "bubbles: the app again in the morning")
        expect(f"saver app {BUBBLES} error" not in log and f"saver app {BUBBLES} start (" not in log,
               "bubbles: never stopped for a fault")

        # 2. A saver that throws: the saver pages take over; three stops in ten minutes block it, with one notification;
        #    choosing it again in Default apps lets it start.
        set_slot(data, "dev.espos.test.saver.throw")
        log, o = sim("throw", """clock 2026-11-22 10:00
wait 1500
saver
wait 1500
shot 01-pages-after-throw
tap 160 240
wait 600
saver
wait 1500
tap 160 240
wait 600
saver
wait 1500
tap 160 240
wait 600
saver
wait 1500
shot 02-blocked
tap 160 240
wait 600
notif center
wait 800
shot 03-center
notif close
wait 400
app settings defaults
wait 1000
tap 160 135
wait 1000
shot 04-picker
quit
""")
        expect(log.count("saver app dev.espos.test.saver.throw error: Error: bad frame 4") == 3, "throw: stopped by its throw three times")
        expect("the app saver stopped (Error: bad frame 4): the saver pages show" in log, "throw: the saver pages take over")
        expect("stopped 3 times in 10 minutes, not started again" in log, "throw: blocked after three stops")
        expect("not started: stopped 3 times in 10 minutes" in log, "throw: the fourth time it is not started")

        # 3. Too slow, too much memory, does not parse, too many shapes.
        for pid, want in [("dev.espos.test.saver.slow", "a frame took over 200 ms"),
                          ("dev.espos.test.saver.hog", "out of memory"),
                          ("dev.espos.test.saver.parse", "saver app dev.espos.test.saver.parse start (saver.js): SyntaxError")]:
            set_slot(data, pid)
            name = pid.split(".")[-1]
            log, o = sim(name, """wait 1500
saver
wait 3000
shot 01-saver
tap 160 240
wait 600
quit
""")
            expect(want in log, f"{name}: stopped ({want})")
            expect("saver page" in log, f"{name}: the saver pages show instead")
        set_slot(data, "dev.espos.test.saver.shapes")
        log, o = sim("shapes", """wait 1500
saver
wait 2000
shot 01-saver
tap 160 240
wait 600
quit
""")
        expect("(shapes left out)" in log, "shapes: over 600 shapes and 4 KB of text are left out")
        expect("saver app dev.espos.test.saver.shapes error" not in log, "shapes: still runs")
    finally:
        shutil.rmtree(data, ignore_errors=True)
    print(f"{len(fails)} failed" if fails else "all checks passed")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
