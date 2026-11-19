#!/usr/bin/env python3
"""Screens of every app in one language, for docs/i18n.md's checks. Run from s3/:

  python3 tools/i18n_tour.py TAG [OUTDIR] [--live] [--packs] [--rot N] [--sim PATH] [--data DIR]

Writes a simulator script (every screen shot as NN-name, then `overflow NN-name`) and, unless --live, runs the
headless simulator (sim/build-i18n, data sim/data-i18n with the language packs installed) and makes contact sheets
OUTDIR/sheet-N.png (8 screens each) and OUTDIR/overflow.txt (labels cut, outside their box or overlapping).
--live prints the script's path for tools/sim-live.sh instead (the visible window). --packs builds the language
packages again and installs them into sim/data-i18n first. --rot N: the whole tour turned that way (1 and 3:
landscape, the gestures moved to the 480 x 320 screen); --sim another simulator build, --data another data folder
(the tour writes sys.lang into it).
"""
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

STEPS = [
    ("home", []),
    ("home2", ["drag 290 260 30 260 250", "wait 1200"]),
    ("library", ["drag 290 260 30 260 250", "wait 1200", "drag 290 260 30 260 250", "wait 1200"]),
    ("search", ["home", "wait 900", "drag 160 200 160 330 300", "wait 900"]),
    ("settings", ["homeswipe", "wait 900", "home", "wait 900", "app settings", "wait 1200"]),
    ("settings-b", ["drag 160 420 160 80 400", "wait 800"]),
    ("settings-c", ["drag 160 420 160 80 400", "wait 800"]),
]
for page in ["wifi", "account", "look", "home", "display", "saver", "sound", "notif", "time", "storage", "update",
             "name", "about", "lang", "desk", "remote", "city", "calendars", "news", "markets", "photos"]:
    STEPS.append((f"set-{page}", ["home", "wait 700", f"app settings {page}", "wait 1500"]))
    if page in ("look", "display", "time", "notif", "lang", "desk", "saver", "home"):
        STEPS.append((f"set-{page}-b", ["drag 160 420 160 80 400", "wait 800"]))
for app in ["store", "weather", "cal", "clock", "notes", "markets", "news", "photos", "remind", "lunar", "calc"]:
    STEPS.append((app, ["home", "wait 700", f"app {app}", "wait 1800"]))
    STEPS.append((f"{app}-b", ["drag 160 420 160 80 400", "wait 800"]))
STEPS += [
    ("store-app", ["home", "wait 700", "app store dev.espos.focus", "wait 1800"]),
    ("pet", ["home", "wait 700", "pet", "wait 3000", "dismiss", "wait 3500"]),
    ("pet-book", ["home", "wait 700", "pet book", "wait 2500"]),
    ("saver", ["home", "wait 700", "saver", "wait 2500"]),
    ("saveredit", ["saveredit", "wait 2000"]),
    ("land", ["wake", "wait 800", "home", "wait 700", "rot 1", "wait 2500"]),
    ("edit", ["rot 0", "wait 1500", "home", "wait 900", "hold 160 300 160 300 10 1300", "wait 900"]),
    ("notifcenter", ["homeswipe", "wait 900", "home", "wait 900", "drag 60 4 60 360 300", "wait 900"]),
    ("control", ["homeswipe", "wait 900", "home", "wait 900", "drag 280 4 280 360 300", "wait 900"]),
]


# The portrait gestures and their landscape places (the 480 x 320 screen: pages swiped across the middle, lists
# scrolled in the middle, the pulls from the status bar's two ends).
LAND = {
    "drag 290 260 30 260 250": "drag 380 200 60 200 250",
    "drag 160 200 160 330 300": "drag 200 120 200 280 300",
    "drag 160 420 160 80 400": "drag 240 290 240 70 400",
    "hold 160 300 160 300 10 1300": "hold 200 290 200 290 10 1300",
    "drag 60 4 60 360 300": "drag 60 4 60 300 300",
    "drag 280 4 280 360 300": "drag 420 4 420 300 300",
}


def script(tag, rot=0):
    lines = ["# tools/i18n_tour.py " + tag, "wait 1500", f"set sys.lang {tag}", "wait 4000", f"rot {rot}", "wait 800"]
    for i, (name, steps) in enumerate(STEPS):
        if name == "land" and rot:  # already turned: only leave the saver editor
            lines += ["wake", "wait 800", "home", "wait 700"]
            continue
        if rot % 2:
            steps = [LAND.get(s, s) for s in steps]
        steps = [f"rot {rot}" if s == "rot 0" else s for s in steps]
        lines += steps
        shot = f"{i:02d}-{name}"
        lines += [f"shot {shot}", f"overflow {shot}"]
    lines += ["rot 0", "wait 800", "home"]
    return "\n".join(lines) + "\n"


def sheets(out):
    from PIL import Image, ImageDraw
    shots = sorted(out.glob("*.ppm"))
    per = 8
    for k in range(0, len(shots), per):
        group = shots[k:k + per]
        ims = [Image.open(p).convert("RGB") for p in group]
        w = max(i.size[0] for i in ims)
        h = max(i.size[1] for i in ims)
        cols = 4
        rows = (len(ims) + cols - 1) // cols
        sheet = Image.new("RGB", (cols * (w + 8), rows * (h + 22)), "white")
        d = ImageDraw.Draw(sheet)
        for j, (p, im) in enumerate(zip(group, ims)):
            x, y = (j % cols) * (w + 8), (j // cols) * (h + 22)
            d.text((x + 4, y + 4), p.stem, fill="black")
            sheet.paste(im, (x, y + 18))
        sheet.save(out / f"sheet-{k // per}.png")


def refresh_packs():
    """The language packages built again (tools/i18n.py build all) and put into sim/data-i18n (Máy: LittleFS)."""
    out = ROOT / "build-pkgs" / "espos" / "s3"
    subprocess.run([sys.executable, str(ROOT / "tools/i18n.py"), "build", "all"], cwd=ROOT, check=True,
                   stdout=subprocess.DEVNULL)
    for f in sorted(out.glob("dev.espos.lang.*.espk")):
        if "lang.ko" in f.name:  # Korean stays out: it is the download test (docs/i18n.md)
            continue
        subprocess.run([sys.executable, str(ROOT / "tools/espk.py"), "--install-sim", "sim/data-i18n", str(f), "--vol",
                        "flash"], cwd=ROOT, check=True, stdout=subprocess.DEVNULL)


def option(argv, name, default):
    if name in argv:
        i = argv.index(name)
        v = argv[i + 1]
        del argv[i:i + 2]
        return v
    return default


def main(argv):
    live = "--live" in argv
    if "--packs" in argv:
        refresh_packs()
    argv = [a for a in argv if a not in ("--live", "--packs")]
    rot = int(option(argv, "--rot", "0"))
    sim = option(argv, "--sim", str(ROOT / "sim/build-i18n/espos-sim"))
    data = option(argv, "--data", "sim/data-i18n")
    tag = argv[0]
    out = Path(argv[1]) if len(argv) > 1 else ROOT / "sim" / "shots" / (f"i18n-{tag}" + (f"-rot{rot}" if rot else ""))
    out.mkdir(parents=True, exist_ok=True)
    sc = out / "tour.txt"
    sc.write_text(script(tag, rot))
    if live:
        print(sc)
        return
    for p in out.glob("*.ppm"):
        p.unlink()
    env = {"SIM_DATA": data, "SDL_VIDEODRIVER": "dummy", "SDL_AUDIODRIVER": "dummy", "PATH": "/usr/bin:/bin",
           "OVERFLOW_ROWS": os.environ.get("OVERFLOW_ROWS", "")}  # 1: rows that only fit by scrolling sideways too
    r = subprocess.run([sim, str(sc), str(out)], cwd=ROOT, env=env,
                       capture_output=True, text=True, timeout=900)
    lines, shot = [], ""
    for l in r.stderr.splitlines():
        if l.startswith("shot "):
            shot = l[5:]
        elif l.startswith("OVERFLOW"):
            lines.append(l)
        elif "fitText:" in l:
            lines.append(f"FIT {shot} {l[l.index('fitText:'):]}")
    (out / "overflow.txt").write_text("\n".join(lines) + "\n")
    sheets(out)
    print(f"{tag}: {len(list(out.glob('*.ppm')))} screens, {len(lines)} overflow lines -> {out}")


if __name__ == "__main__":
    main(sys.argv[1:])
