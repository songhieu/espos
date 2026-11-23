"""PawBoop on a desk without a memory card (owner, 2026-11-21: "cách 2 đi"): the dogs and the pet game in two parts,
so a desk sold without a card gets the dog and its assistant in the 7,352 KB app area, and a desk with a card
gets everything. The parts are the newest built packages split, every file kept byte for byte:

  vn.pawboop.dog.<dog>  core  pet.pbs, voice.pba (lite: 2 lines a moment, no Coin Run lines), assist.pba, icon.jpg
                        game  run.pbs, sfx.pba, voice_full.pba (every line)        "opt": true
  vn.pawboop.pet        core  pawboop_ui.pbu, the bubble fonts, sfx.cue, icon.jpg
                        game  sfx.pba (the shared sounds and the home tune)        "opt": true

An "opt" part (src/pkg/store.cpp, firmware 0.5.12) is downloaded with its core when a memory card is in and the
volume has the room; a desk without a card takes the core only. The pet engine plays voice_full.pba when it is
there, else voice.pba; Coin Run needs the dog's game part (the pet game says so). min_fw 512: firmware before it
would install the core alone and lose Coin Run, so it keeps the versions it has.

  python3 tools/pawboop/parts.py [--out DIR] [--only pet,dogs] [--catalog] [--sim DIR]
"""
import argparse
import collections
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

OUT = ROOT / "build-pkgs/espos/s3"  # the published packages are read from here
DOGS = ["bao", "momo", "boop", "leaf", "volt", "byte", "shiba", "corgi", "husky", "golden"]
MIN_FW = 512  # firmware 0.5.12: "opt" parts, voice_full.pba
KEEP = 2      # lines kept a moment in the lite voice
GAME_LABEL = {"pet": ("Sounds and music", "Âm thanh và nhạc"),
              "dog": ("Coin Run, sounds and all its lines", "Chạy lấy xu, âm thanh và mọi câu nói")}


def latest(pid):
    """the newest built package of pid: (version, {name: bytes}, manifest) of its core (a single-file package's file)"""
    best = None
    for p in OUT.glob(f"{pid}-*.espk"):
        rest = p.name[len(pid) + 1:-5]
        ver, _, part = rest.partition("-")
        if part not in ("", "core") or len(ver.split(".")) != 3 or not all(x.isdigit() for x in ver.split(".")):
            continue
        if best is None or tuple(map(int, ver.split("."))) > tuple(map(int, best[0].split("."))):
            best = (ver, p)
    m, files, b = espk.read(best[1])
    if any(f.name.startswith(f"{pid}-{best[0]}-") and not f.name.endswith("-core.espk") for f in OUT.glob("*.espk")):
        sys.exit(f"{pid} {best[0]} has parts already")
    return best[0], {n: b[o:o + s] for n, o, s, c in files}, m


def pba_parse(v):
    magic, rate, ch, pms, n, pre, toff, doff = struct.unpack_from("<4sHBBHHII", v, 0)
    assert magic == b"PBA1" and toff == 20 + 48 * n
    clips = [struct.unpack_from("<16s16sIIIHH", v, 20 + 48 * i) for i in range(n)]
    return (rate, ch, pms, pre), clips, v[toff:doff], v[doff:]


def lite_voice(v):
    """the lines of the pet game a dog says, KEEP a moment (the shortest), Coin Run's left out (it comes with the game
    part). caught_1 / caught_2 answer each other by index (voice.cpp answerTo): the same indexes for both."""
    head, clips, texts, data = pba_parse(v)
    on = lambda c: c[1].split(b"\0")[0].decode()
    name = lambda c: c[0].split(b"\0")[0].decode()
    by = collections.defaultdict(list)
    for c in clips:
        if not on(c).startswith("@run_"):
            by[on(c)].append(c)
    keep = set()
    for moment, cs in by.items():
        if moment in ("@caught_1", "@caught_2"):
            continue
        for c in sorted(cs, key=lambda c: c[4])[:KEEP]:
            keep.add(name(c))
    # the two beats: the pairs (caught_1<k>, caught_2<k>) shortest together
    one = {name(c)[len("caught_1"):]: c for c in by.get("@caught_1", [])}
    two = {name(c)[len("caught_2"):]: c for c in by.get("@caught_2", [])}
    pairs = sorted((one[k][4] + two[k][4], k) for k in one if k in two)
    for _, k in pairs[:KEEP]:
        keep.add("caught_1" + k), keep.add("caught_2" + k)
    kept = [c for c in clips if name(c) in keep]
    rows, out = [], b""
    for n_, o, off, nb, ns, npk, tx in kept:
        rows.append(struct.pack("<16s16sIIIHH", n_, o, len(out), nb, ns, npk, tx))
        out += data[off:off + nb]
    toff = 20 + 48 * len(kept)
    hdr = struct.pack("<4sHBBHHII", b"PBA1", head[0], head[1], head[2], len(kept), head[3], toff, toff + len(texts))
    return hdr + b"".join(rows) + texts + out, len(clips), len(kept)


def bump(v):
    x, y, z = map(int, v.split("."))
    return f"{x}.{y}.{z + 1}"


def code_of(v):
    x, y, z = map(int, v.split("."))
    return x * 10000 + y * 100 + z


def build_part(folder, man, files, out):
    for n, d in files.items():
        (folder / n).parent.mkdir(parents=True, exist_ok=True)
        (folder / n).write_bytes(d)
    (folder / "manifest.json").write_text(json.dumps(man, ensure_ascii=False))
    return espk.build(folder, out)


def split(pid, kind, dest):
    """pid's newest package -> its core and game parts, one version up. -> [paths]"""
    ver, files, m = latest(pid)
    to = bump(ver)
    if kind == "dog":
        full = files["voice.pba"]
        lite, was, now = lite_voice(full)
        core = {"pet.pbs": files["pet.pbs"], "voice.pba": lite, "assist.pba": files["assist.pba"], "icon.jpg": files["icon.jpg"]}
        game = {"run.pbs": files["run.pbs"], "sfx.pba": files["sfx.pba"], "voice_full.pba": full}
        note = f"voice {was} -> {now} lines in the core"
    else:
        core = {k: v for k, v in files.items() if k != "sfx.pba"}
        game = {"sfx.pba": files["sfx.pba"]}
        note = ""
    left = set(files) - set(core) - set(game)
    assert not left or (kind == "pet"), left
    m2 = dict(m, version=to, code=code_of(to), min_fw=max(MIN_FW, m.get("min_fw", 0)), part="core")
    en, vi = GAME_LABEL[kind]
    g = {"id": m["id"], "version": to, "code": code_of(to), "part": "game", "opt": True, "label": en, "labels": {"vi": vi}}
    out = []
    with tempfile.TemporaryDirectory() as t:
        out.append(build_part(Path(t) / "core", m2, core, dest / f"{pid}-{to}-core.espk"))
        out.append(build_part(Path(t) / "game", g, game, dest / f"{pid}-{to}-game.espk"))
    jpg = OUT / f"{pid}-{ver}.jpg"
    if jpg.exists():
        shutil.copyfile(jpg, dest / f"{pid}-{to}.jpg")
    sizes = [p.stat().st_size for p in out]
    print(f"  {pid} {ver} -> {to}: core {sizes[0]:,} B, game {sizes[1]:,} B {note}")
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", type=Path, default=OUT)
    ap.add_argument("--only", default="pet,dogs")
    ap.add_argument("--catalog", action="store_true")
    ap.add_argument("--sim")
    a = ap.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    built = []
    if "pet" in a.only:
        built += split("vn.pawboop.pet", "pet", a.out)
    if "dogs" in a.only:
        for d in DOGS:
            built += split(f"vn.pawboop.dog.{d}", "dog", a.out)
    if a.catalog:
        subprocess.run([sys.executable, str(ROOT / "tools/i18n.py"), "catalog"], cwd=ROOT, check=True)
    if a.sim:
        subprocess.run([sys.executable, str(ROOT / "tools/espk.py"), "--install-sim", a.sim] + [str(p) for p in built], check=True)


if __name__ == "__main__":
    main()
