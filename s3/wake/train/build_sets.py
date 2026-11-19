"""Build the training / validation / test sets from the TTS clips: compose each clip in a 3 s scene (speech before,
the request after, room, noise, level), run the C front end, save int8 features + labels.

    .venv/bin/python train/build_sets.py [--variants 6] [--workers 6]

Writes DATA/sets/{train,val,test}_{pos,neg}.npz: X (N, 300, 40) int8; lab (N, 4) int32 = kind (0 neg, 1 w1,
2 w2, 3 w3, 4 tone), on, off (samples, -1 for negatives), cond (test: 0 clean, 1 noisy 10 dB, 2 noisy 5 dB,
3 noisy 0 dB, 4 far: reverb + 10 dB); meta.json lists the voices of each split.
"""
import argparse
import json
import os
import sys
from multiprocessing import Pool

import numpy as np
import soundfile as sf

sys.path.insert(0, os.path.dirname(__file__))
import augment  # noqa: E402
from wfe import FE  # noqa: E402

DATA = augment.DATA
TTS = os.path.join(DATA, "tts")
SR = 16000
T = 300                      # frames per example (3.0 s)
NS = T * 160
KIND = {"w1": 1, "w2": 2, "w3": 3, "tone": 4, "conf": 0, "neg": 0}

# Voices kept out of training. test: never seen; val: for picking the checkpoint and thresholds.
TEST = {"turbo:Ngọc Trân", "turbo:Thái Sơn", "turbo:Quỳnh Anh", "turbo:Quốc Tuấn", "nano:Ái Hân", "nano:Adam",
        "say:Linh", "piper:25hours_single-low"} | {f"piper:vivos-x_low#{i}" for i in range(55, 65)} \
    | {f"clone:massive_{i:03d}" for i in range(20, 28)} | {f"clone:fleurs_dev_{i:03d}" for i in range(25, 32)}
VAL = {"turbo:Minh Đức", "turbo:Mỹ Duyên", "nano:Mỹ Duyên"} | {f"piper:vivos-x_low#{i}" for i in range(48, 55)} \
    | {f"clone:massive_{i:03d}" for i in range(16, 20)} | {f"clone:fleurs_dev_{i:03d}" for i in range(20, 25)}


FAMILIES = ["turbo", "nano", "vivos", "piper", "say", "clone", "mix"]


def family(r):
    if r["engine"] == "piper":
        return 2 if "vivos" in r["voice"] else 3
    return FAMILIES.index(r["engine"])


def split_of(voice):
    return "test" if voice in TEST else "val" if voice in VAL else "train"


TONE_SYL = ("gấu", "gầu", "gàu", "gẫu", "gậu", "gẩu", "gáu", "gạu", "gảu")


def load_manifest():
    recs, seen = [], set()
    for fn in sorted(os.listdir(TTS)):
        if fn.startswith("manifest_") and fn.endswith(".jsonl"):
            for line in open(os.path.join(TTS, fn)):
                r = json.loads(line)
                if r["f"] in seen or not os.path.exists(os.path.join(TTS, r["f"])):
                    continue
                seen.add(r["f"])
                # Piper's Vietnamese voices drop the tones ("Missing phoneme" for the tone marks): their "con gấu
                # bông" says "con gâu bông", which holds the wake word. Not a negative: leave those out.
                if r["engine"] == "piper" and r["kind"] in ("conf", "tone") and any(t in r["text"].lower() for t in TONE_SYL):
                    continue
                r["split"] = split_of(r["voice"])
                recs.append(r)
    return recs


_G = {}


def _init(recs):
    _G["pools"] = augment.Pools()
    _G["fe"] = FE()
    _G["recs"] = recs
    by_voice = {}
    for i, r in enumerate(recs):
        if r["kind"] == "req":
            by_voice.setdefault(r["voice"], []).append(i)
    _G["req"] = by_voice
    _G["ctx"] = [i for i, r in enumerate(recs) if r["kind"] in ("neg", "req")]
    real = []
    for fn in ("req_massive.npz",):
        p = os.path.join(DATA, "aug", fn)
        if os.path.exists(p):
            z = np.load(p)
            real += [z[k].astype(np.float32) / 32768.0 for k in z.files]
    _G["real"] = real


def _read(r):
    x, _ = sf.read(os.path.join(TTS, r["f"]), dtype="float32")
    return x


def compose(rng, rec, cond=None):
    """Place the clip so that its speech ends at 1.6..2.7 s of a 3 s scene. Returns (audio, on, off)."""
    recs, pools = _G["recs"], _G["pools"]
    x = _read(rec)
    on, off = int(rec["on"] * SR), int(rec["off"] * SR)
    test = cond is not None
    f = 1.0
    if not test and rng.random() < 0.6:
        f = rng.uniform(0.82, 1.2)
        x = augment.speed(x, f)
        on, off = int(on / f), int(off / f)
    if not test and rng.random() < 0.15:  # child-like voice: higher pitch, same timing
        x = augment.pitch_keep_time(x, rng.uniform(2.5, 6.0))
    scene = np.zeros(NS + SR, dtype=np.float32)
    end = int(rng.uniform(1.6, 2.7) * SR)
    start = end - off
    if start < 0:  # a long clip: keep its end
        x, on, off, start = x[-start:], max(0, on + start), off + start, 0
    seg = x[: NS + SR - start]
    scene[start:start + len(seg)] += seg
    # speech after: the same voice's request (or a real person's, or nothing)
    u = rng.random()
    if rec["kind"] != "req" and u < 0.65:
        reqs = _G["req"].get(rec["voice"], [])
        if reqs and u < 0.5:
            y = _read(recs[reqs[rng.integers(0, len(reqs))]])
            if f != 1.0:
                y = augment.speed(y, f)
        elif _G["real"]:
            y = _G["real"][rng.integers(0, len(_G["real"]))]
        else:
            y = None
        if y is not None:
            a = start + off + int(rng.uniform(0.0, 0.45) * SR)
            if a < len(scene):
                y = y[: len(scene) - a] * 10 ** (rng.uniform(-4, 3) / 20)
                scene[a:a + len(y)] += y
    # speech before (another person, or the same voice talking): ends 0.15..0.8 s before
    if rng.random() < 0.35 and start + on > int(0.8 * SR):
        c = recs[_G["ctx"][rng.integers(0, len(_G["ctx"]))]]
        y = _read(c)
        b = start + on - int(rng.uniform(0.15, 0.8) * SR)
        y = y[-b:] * 10 ** (rng.uniform(-8, 2) / 20)
        scene[b - len(y):b] += y
    scene = scene[:NS]
    on_s, off_s = start + on, start + off
    if test:
        if cond == 0:
            z = scene
        elif cond in (1, 2, 3):
            z = augment.degrade(rng, scene, pools, p_reverb=0.0, p_noise=1.0, snr={1: (10, 10), 2: (5, 5), 3: (0, 0)}[cond],
                                p_eq=0.0)
        else:
            z = augment.reverb(rng, scene, pools, wet=0.9)
            z = augment.degrade(rng, z, pools, p_reverb=0.0, p_noise=1.0, snr=(10, 10), p_eq=1.0)
        pk = -12
    else:
        z = augment.degrade(rng, scene, pools, p_reverb=0.55, p_noise=0.85, snr=(-3, 30), p_eq=0.5)
        pk = rng.uniform(-36, -2)
    pcm = augment.to_pcm16(z, peak_db=pk)
    return pcm, on_s, off_s


def _job(args):
    idx, seed, cond = args
    rng = np.random.default_rng(seed)
    rec = _G["recs"][idx]
    pcm, on, off = compose(rng, rec, cond)
    X = _G["fe"].run(pcm)
    k = KIND[rec["kind"]]
    lab = [k, on if k else -1, off if k else -1, -1 if cond is None else cond]
    return X[:T], lab


def build(recs, jobs, out, workers):
    with Pool(workers, initializer=_init, initargs=(recs,)) as p:
        res = p.map(_job, jobs, chunksize=64)
    X = np.stack([r[0] for r in res])
    lab = np.array([r[1] for r in res], dtype=np.int32)
    fam = np.array([family(recs[j[0]]) for j in jobs], dtype=np.int8)
    vlist = sorted({r["voice"] for r in recs})
    voice = np.array([vlist.index(recs[j[0]]["voice"]) for j in jobs], dtype=np.int16)
    np.savez(out, X=X, lab=lab, fam=fam, voice=voice)
    print(f"{out}: {X.shape}, kinds {np.bincount(lab[:, 0], minlength=5).tolist()}", flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--variants", type=int, default=6)
    ap.add_argument("--neg_variants", type=int, default=2)
    ap.add_argument("--workers", type=int, default=6)
    ap.add_argument("--only", default="")
    a = ap.parse_args()
    recs = load_manifest()
    os.makedirs(os.path.join(DATA, "sets"), exist_ok=True)
    voices = {s: sorted({r["voice"] for r in recs if r["split"] == s}) for s in ("train", "val", "test")}
    json.dump(voices, open(os.path.join(DATA, "sets", "meta.json"), "w"), ensure_ascii=False, indent=1)
    print({s: len(v) for s, v in voices.items()})
    rng = np.random.default_rng(7)
    for split in ("train", "val", "test"):
        if a.only and split != a.only:
            continue
        pos = [i for i, r in enumerate(recs) if r["split"] == split and r["kind"] in ("w1", "w2", "w3")]
        neg = [i for i, r in enumerate(recs) if r["split"] == split and r["kind"] in ("conf", "neg", "tone")]
        if split == "train":
            neg = [i for i in neg if recs[i]["kind"] != "tone"]  # an emphatic bark can sound like these
            # vivos (64 low-quality speakers) would drown the rest: fewer copies of it, more of the others
            pj = [(i, int(rng.integers(1 << 31)), None) for i in pos
                  for _ in range(a.variants // 2 if family(recs[i]) == 2 else a.variants)]
            nj = [(i, int(rng.integers(1 << 31)), None) for i in neg for _ in range(a.neg_variants)]
        else:
            pj = [(i, int(rng.integers(1 << 31)), c) for i in pos for c in range(5)]
            nj = [(i, int(rng.integers(1 << 31)), c) for i in neg for c in (0, 4)]
        build(recs, pj, os.path.join(DATA, "sets", f"{split}_pos.npz"), a.workers)
        build(recs, nj, os.path.join(DATA, "sets", f"{split}_neg.npz"), a.workers)
        json.dump([recs[i]["text"] for i, _, _ in nj], open(os.path.join(DATA, "sets", f"{split}_neg_texts.json"), "w"),
                  ensure_ascii=False)
        json.dump([recs[i]["text"] for i, _, _ in pj], open(os.path.join(DATA, "sets", f"{split}_pos_texts.json"), "w"),
                  ensure_ascii=False)


if __name__ == "__main__":
    main()
