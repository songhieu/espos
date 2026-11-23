#!/usr/bin/env python3
"""A product preset's first boot in the simulator (docs/app-slots.md §17.5): each delivery bundle
(tools/make_bundle.py --preset) becomes a unit off the line, a simulator data folder with the bundle's LittleFS
file and its card and nothing else. It checks that the preset applied (every key, the packages on the card, the
assistant slot), the three-way merge of a newer version (the person's own change stays, an untouched key follows,
a new key comes, a new package is wanted), and with a setup build (-DESPOS_ONBOARD=1) which step follows "Wi-Fi":
the dog's for a preset with an assistant, never for one without. Screenshots in OUT/<bundle>/.

  python3 tools/preset_sim_test.py OUT BUNDLE... [--sim sim/build/espos-sim] [--onb sim/build-onb/espos-sim]
      [--card-only]   (the preset only on the card, not in LittleFS: the first boot copies it in)
"""
import argparse
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
fails = 0


def check(ok, what):
    global fails
    print(("ok   " if ok else "FAIL ") + what)
    fails += 0 if ok else 1


def unit(bundle, card_only):
    """A simulator data folder for a unit off the line: the bundle's preset in fs/sys (unless card_only) and its card."""
    d = Path(tempfile.mkdtemp(prefix="preset-", dir=ROOT / "sim"))
    (d / "fs/sys").mkdir(parents=True)
    if not card_only:
        shutil.copy(bundle / "preset.json", d / "fs/sys/preset.json")
    shutil.copytree(bundle / "card", d / "sd")
    return d


def run(sim, data, script, out):
    out.mkdir(parents=True, exist_ok=True)
    (out / "script.txt").write_text(script)
    env = {"SIM_DATA": str(data), "PATH": "/usr/bin:/bin:/opt/homebrew/bin", "SIM_NO_PACKS": "1", "HOME": str(Path.home())}
    r = subprocess.run([str(sim), str(out / "script.txt"), str(out)], env=env, capture_output=True, text=True, timeout=600)
    (out / "log.txt").write_text(r.stdout + r.stderr)
    for p in out.glob("*.ppm"):
        subprocess.run(["sips", "-s", "format", "png", str(p), "--out", str(p.with_suffix(".png"))], capture_output=True)
        p.unlink()
    return r.stdout + r.stderr


def dumped(out, name):
    f = out / f"{name}.txt"
    return f.read_text() if f.exists() else None


def main():
    a = argparse.ArgumentParser()
    a.add_argument("out")
    a.add_argument("bundles", nargs="+")
    a.add_argument("--sim", default=str(ROOT / "sim/build/espos-sim"))
    a.add_argument("--onb", help="a simulator built with -DESPOS_ONBOARD=1: the setup's steps")
    a.add_argument("--card-only", action="store_true")
    o = a.parse_args()
    for b in map(Path, o.bundles):
        man = json.loads((b / "manifest.json").read_text())
        dev = json.loads((b / "preset.json").read_text())
        name = f"{b.name}{'-card' if o.card_only else ''}"
        print(f"== {name}: {dev['id']} v{dev['version']} for {dev['board']}")
        keys = sorted(dev["settings"])
        # 1. the first boot: every key, the packages, the slot; then the person changes one key, and v2 comes over USB
        own = "s3.saver"  # the person's own change (both presets set it)
        v2 = json.loads(json.dumps(dev))
        v2["version"] = dev["version"] + 1
        untouched, moved = "s3.rot", "0" if dev["settings"].get("s3.rot") != "0" else "1"
        v2["settings"][own] = "600"
        v2["settings"][untouched] = moved  # a key the person never touched follows v2...
        follow = "s3.click"  # ...and a new key v2 brings
        v2["settings"][follow] = "0"
        v2["settings"]["s3.dnd"] = "1"
        v2["packages"] = dev["packages"] + [{"id": "dev.espos.lang.de", "parts": ["core"]}]
        lines = ["wait 3500", "PRESET", "shot 01-home"]
        lines += [f"dumpset {k} a-{k}" for k in keys]
        lines += [f"PKGSTATUS {p['id']}" for p in dev["packages"]]
        lines += ["drag 280 4 280 360 300", "wait 900", "shot 02-control", "drag 160 420 160 40 250", "wait 700",
                  "saver", "wait 2500", "shot 03-saver", "wake", "wait 800",
                  f"set {own} 300", "PRESET " + json.dumps(v2, separators=(",", ":"), ensure_ascii=False), "wait 500", "PRESET",
                  f"dumpset {own} b-own", f"dumpset {follow} b-follow", "dumpset s3.dnd b-dnd", f"dumpset {untouched} b-untouched",
                  "dumpset s3.pkgs b-pkgs", "quit"]
        out = Path(o.out) / name
        data = unit(b, o.card_only)
        log = run(o.sim, data, "\n".join(lines) + "\n", out)
        shutil.rmtree(data)
        check(f'ESPOS:PRESET {{"id":"{dev["id"]}","v":{dev["version"]}' in log, f"{name}: the preset applied at the first boot ({dev['id']} v{dev['version']})")
        bad = [k for k in keys if dumped(out, f"a-{k}") != dev["settings"][k]]
        check(not bad, f"{name}: all {len(keys)} keys hold the preset's values" + (f" (not: {', '.join(bad)})" if bad else ""))
        for p in dev["packages"]:
            check(f"PKGSTATUS {p['id']} installed" in log, f"{name}: {p['id']} installed from the card")
        check(("slot.assistant" not in dev["settings"]) or dumped(out, "a-slot.assistant") == dev["settings"]["slot.assistant"],
              f"{name}: the assistant slot is {dev['settings'].get('slot.assistant', '(default)')}")
        check(f'ESPOS:PRESET {{"id":"{dev["id"]}","v":{v2["version"]}' in log, f"{name}: v{v2['version']} applied")
        check(dumped(out, "b-own") == "300", f"{name}: the person's own {own} (300) stays over v2's 600")
        check(dumped(out, "b-follow") == "0" and dumped(out, "b-dnd") == "1", f"{name}: keys new in v2 come in (s3.click, s3.dnd)")
        check(dumped(out, "b-untouched") == moved, f"{name}: {untouched}, never touched, follows v2 ({dev['settings'].get(untouched)} -> {moved})")
        pk = json.loads(dumped(out, "b-pkgs") or "[]")
        check(any(e.get("id") == "dev.espos.lang.de" for e in pk), f"{name}: the package new in v2 is wanted (s3.pkgs)")
        check(not any(e.get("id") in [p["id"] for p in dev["packages"]] and e.get("rm") for e in pk), f"{name}: nothing removed")
        if o.onb:
            # 2. the setup: which step comes after Wi-Fi (the language is asked by these presets)
            lines = ["wait 3500", "onb state", "shot 10-setup-first", "onb step 2", "wait 1200", "onb state", "shot 11-after-wifi", "quit"]
            out2 = Path(o.out) / (name + "-setup")
            data = unit(b, o.card_only)
            log = run(o.onb, data, "\n".join(lines) + "\n", out2)
            shutil.rmtree(data)
            steps = [l for l in log.splitlines() if l.startswith("ESPOS:ONBOARD")]
            after = json.loads(steps[-1].split(" ", 1)[1])["step"] if steps else -1
            dog = dev["settings"].get("slot.assistant", "none") != "none"
            want = 2 if dog else 5  # the dog's step, or (dog, name and call skipped) the city
            check(after == want, f"{name}: after Wi-Fi the setup shows step {after} ({'the dog' if after == 2 else 'the city' if after == 5 else '?'}), want {want}")
        print()
    print("all passed" if not fails else f"{fails} failed")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
