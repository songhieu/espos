"""Download the negative / background corpora, turn them into front-end features, drop the audio.

    .venv/bin/python train/fetch.py rir chime music_pool babble fleurs_vi ...   (or: all)

Writes DATA/feats/<split>/<name>/{frames.i8, utt.npy, meta.json}: int8 features (N x 40) from fe/wake_fe.c, utt = first
frame of each utterance; DATA/aug/* audio pools for augmentation. Downloads go to DATA/dl and are deleted after use.
Licences: training uses CC-BY / CC0 sources only; NC or unclear sources (VIVOS, ESC-50, AudioSet) are eval only.
"""
import io
import json
import os
import subprocess
import sys
import tarfile
import urllib.request

import numpy as np
import soundfile as sf
from scipy.signal import resample_poly

sys.path.insert(0, os.path.dirname(__file__))
import augment  # noqa: E402
from wfe import FE  # noqa: E402

DATA = augment.DATA
HF = "https://huggingface.co/datasets"
SR = 16000

# name: (split, url(s), kind, licence, options)
JOBS = {
    "fleurs_vi": ("train", [f"{HF}/google/fleurs/resolve/main/data/vi_vn/audio/train.tar.gz",
                            f"{HF}/google/fleurs/resolve/main/data/vi_vn/audio/dev.tar.gz"], "tar", "CC-BY-4.0", {}),
    "fleurs_vi_test": ("eval", [f"{HF}/google/fleurs/resolve/main/data/vi_vn/audio/test.tar.gz"], "tar", "CC-BY-4.0", {}),
    "fleurs_en": ("train", [f"{HF}/google/fleurs/resolve/main/data/en_us/audio/dev.tar.gz"], "tar", "CC-BY-4.0", {}),
    "fleurs_en_test": ("eval", [f"{HF}/google/fleurs/resolve/main/data/en_us/audio/test.tar.gz"], "tar", "CC-BY-4.0", {}),
    "fpt": ("train", [f"{HF}/doof-ferb/fpt_fosd/resolve/main/data/train-0000{i}-of-00002.parquet" for i in range(2)],
            "parquet", "CC-BY-4.0", {}),
    "massive": ("train", [f"{HF}/doof-ferb/Speech-MASSIVE_vie/resolve/main/data/validation-00000-of-00001.parquet"],
                "parquet", "CC-BY-4.0", {"keep_audio": 1500}),
    "massive_test": ("eval", [f"{HF}/doof-ferb/Speech-MASSIVE_vie/resolve/main/data/test-00000-of-00001.parquet"],
                     "parquet", "CC-BY-4.0", {}),
    "vlsp": ("train", [f"{HF}/doof-ferb/vlsp2020_vinai_100h/resolve/main/data/train-0000{i}-of-00035.parquet"
                       for i in range(2)], "parquet", "CC-BY-4.0", {}),
    "giga": ("train", [f"{HF}/doof-ferb/gigaspeech2_vie/resolve/main/data/validation-00000-of-00003.parquet"],
             "parquet", "CC-BY-4.0", {}),
    "giga_test": ("eval", [f"{HF}/doof-ferb/gigaspeech2_vie/resolve/main/data/test-00000-of-00005.parquet"],
                  "parquet", "CC-BY-4.0", {}),
    "music": ("train", [f"{HF}/benjamin-paine/free-music-archive-commercial-16khz-full/resolve/main/data/"
                        "train-00000-of-00013.parquet"], "parquet", "CC-BY (FMA commercial-use tracks)",
              {"pool": "music", "pool_min": 60, "max_sec": 120}),
    "music_test": ("eval", [f"{HF}/benjamin-paine/free-music-archive-commercial-16khz-full/resolve/main/data/"
                            "train-00012-of-00013.parquet"], "parquet", "CC-BY", {"max_sec": 60, "max_hours": 3}),
    "vivos_test": ("eval", [f"{HF}/AILAB-VNUHCM/vivos/resolve/main/data/vivos.tar.gz"], "tar", "CC-BY-NC-SA-4.0 (eval only)",
                   {"only": "/test/"}),
    "esc50": ("eval", [f"{HF}/ashraq/esc50/resolve/main/data/train-0000{i}-of-00002-{h}.parquet"
                       for i, h in ((0, "2f1ab7b824ec751f"), (1, "27425e5c1846b494"))], "parquet",
              "CC-BY-NC-3.0 (eval only)", {"esc": True}),
    "audioset": ("eval", [f"{HF}/agkphysics/AudioSet/resolve/main/data/eval/00.parquet"], "parquet",
                 "YouTube audio (eval only)", {}),
}


def dl(url, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    for _ in range(5):
        r = subprocess.run(["curl", "-sSL", "--retry", "5", "-C", "-", "-o", path, url])
        if r.returncode == 0:
            return path
    raise RuntimeError(f"download failed: {url}")


def decode(b, want_sr=SR):
    try:
        x, sr = sf.read(io.BytesIO(b), dtype="float32", always_2d=True)
    except Exception:
        p = subprocess.run(["ffmpeg", "-v", "quiet", "-i", "pipe:0", "-ac", "1", "-ar", str(want_sr), "-f", "s16le", "pipe:1"],
                           input=b, capture_output=True)
        if p.returncode or not p.stdout:
            return None
        return np.frombuffer(p.stdout, dtype=np.int16).astype(np.float32) / 32768.0
    x = x.mean(1)
    if sr != want_sr:
        from math import gcd
        g = gcd(sr, want_sr)
        x = resample_poly(x, want_sr // g, sr // g).astype(np.float32)
    return x


class Store:
    def __init__(self, split, name, licence):
        self.dir = os.path.join(DATA, "feats", split, name)
        os.makedirs(self.dir, exist_ok=True)
        self.f = open(os.path.join(self.dir, "frames.i8"), "wb")
        self.utt, self.labels = [], []
        self.n = 0
        self.licence = licence
        self.fe = FE()

    def add(self, pcm16, label=""):
        feats = self.fe.run(pcm16)
        if len(feats) == 0:
            return
        self.utt.append(self.n)
        self.labels.append(label)
        self.f.write(feats.tobytes())
        self.n += len(feats)

    def close(self, extra=None):
        self.f.close()
        np.save(os.path.join(self.dir, "utt.npy"), np.array(self.utt, dtype=np.int64))
        meta = {"frames": self.n, "hours": round(self.n / 100 / 3600, 3), "utts": len(self.utt), "licence": self.licence}
        if any(self.labels):
            meta["labels"] = self.labels
        meta.update(extra or {})
        json.dump(meta, open(os.path.join(self.dir, "meta.json"), "w"), ensure_ascii=False)
        print(f"  -> {self.dir}: {meta['hours']} h, {meta['utts']} utts", flush=True)


def level(rng, x, lo=-32, hi=-3):
    """A random speech level: the clip's peak at lo..hi dBFS."""
    return augment.to_pcm16(x, peak_db=rng.uniform(lo, hi))


def iter_tar(url, only=None):
    req = urllib.request.Request(url, headers={"User-Agent": "wake-data-fetch"})
    with urllib.request.urlopen(req) as resp:
        with tarfile.open(fileobj=resp, mode="r|gz") as tf:
            for m in tf:
                if not m.isfile() or not m.name.endswith((".wav", ".flac", ".mp3")):
                    continue
                if only and only not in m.name:
                    continue
                yield m.name, tf.extractfile(m).read()


def iter_parquet(path, esc=False):
    import pyarrow.parquet as pq
    pf = pq.ParquetFile(path)
    names = pf.schema_arrow.names
    acol = next(n for n in names if n in ("audio", "audio_filepath", "speech") or "audio" in n)
    cols = [acol] + [c for c in ("category", "target", "fold", "filename") if c in names]
    for batch in pf.iter_batches(batch_size=32, columns=cols):
        d = batch.to_pydict()
        for i, a in enumerate(d[acol]):
            b = a.get("bytes") if isinstance(a, dict) else a
            if not b:
                continue
            meta = {c: d[c][i] for c in cols[1:]}
            yield meta, b


def pools_or_none():
    try:
        return augment.Pools()
    except Exception:
        return None


def run_job(name):
    split, urls, kind, licence, opt = JOBS[name]
    rng = np.random.default_rng(abs(hash(name)) % 2 ** 32)
    st = Store(split, name, licence)
    pools = augment.Pools() if split == "train" else None
    keep = opt.get("keep_audio", 0)
    kept = []
    pool = [] if opt.get("pool") else None
    pool_target = opt.get("pool_min", 0) * 60 * SR
    max_hours = opt.get("max_hours", 1e9)
    for url in urls:
        print(f"[{name}] {url.rsplit('/', 1)[-1]}", flush=True)
        if kind == "tar":
            it = ((n, b, {}) for n, b in iter_tar(url, opt.get("only")))
        else:
            p = dl(url, os.path.join(DATA, "dl", name + "_" + url.rsplit("/", 1)[-1]))
            it = ((m.get("filename", ""), b, m) for m, b in iter_parquet(p))
        for nm, b, meta in it:
            x = decode(b)
            if x is None or len(x) < SR // 4:
                continue
            if opt.get("max_sec"):
                x = x[: opt["max_sec"] * SR]
            label = ""
            if opt.get("esc"):
                label = str(meta.get("category", ""))
            if pool is not None and sum(len(v) for v in pool) < pool_target:
                pool.append(level(rng, x, -20, -6))
                continue
            if keep and len(kept) < keep:
                kept.append(level(rng, x, -12, -3))
            st.add(level(rng, x), label)
            if split == "train" and pools is not None and rng.random() < 0.35:  # a degraded copy as well
                y = augment.degrade(rng, x, pools, p_reverb=0.7, p_noise=0.9, snr=(3, 25))
                st.add(level(rng, y), label)
            if st.n / 100 / 3600 >= max_hours:
                break
        if kind == "parquet":
            os.remove(p)
        if st.n / 100 / 3600 >= max_hours:
            break
    st.close()
    os.makedirs(os.path.join(DATA, "aug"), exist_ok=True)
    if pool:
        np.save(os.path.join(DATA, "aug", opt["pool"] + ".npy"), np.concatenate(pool))
    if kept:
        np.savez(os.path.join(DATA, "aug", f"req_{name}.npz"), *kept)


def job_rir():
    p = dl(f"{HF}/benjamin-paine/mit-impulse-response-survey-16khz/resolve/main/data/train-00000-of-00001.parquet",
           os.path.join(DATA, "dl", "rir.parquet"))
    rirs = []
    for _, b in iter_parquet(p):
        x = decode(b)
        if x is not None and len(x) > 100:
            rirs.append(x[: SR])  # first second is plenty
    os.makedirs(os.path.join(DATA, "aug"), exist_ok=True)
    np.savez(os.path.join(DATA, "aug", "rir.npz"), *rirs)
    os.remove(p)
    print(f"rir: {len(rirs)} impulse responses", flush=True)


def job_chime():
    base = f"{HF}/chimechallenge/chime3-background-noise/resolve/main/CHiME3/data/audio/16kHz/backgrounds/"
    files = {"cafe": "BGD_150203_010_CAF.CH1.wav", "street": "BGD_150203_010_STR.CH1.wav",
             "ped": "BGD_150203_010_PED.CH1.wav", "bus": "BGD_150204_010_BUS.CH1.wav"}
    os.makedirs(os.path.join(DATA, "aug"), exist_ok=True)
    for env, fn in files.items():
        p = os.path.join(DATA, "dl", fn)
        try:
            dl(base + fn, p)
            x, sr = sf.read(p, dtype="int16")
        except Exception as e:
            print("chime", env, "failed:", e, flush=True)
            continue
        x = x[: 12 * 60 * sr]
        np.save(os.path.join(DATA, "aug", f"noise_{env}.npy"), x)
        os.remove(p)
        print(f"chime {env}: {len(x) / sr / 60:.1f} min", flush=True)


def job_babble():
    """20 minutes of 4-talker babble from FLEURS vi dev (CC-BY 4.0)."""
    utts = []
    for _, b in iter_tar(f"{HF}/google/fleurs/resolve/main/data/vi_vn/audio/dev.tar.gz"):
        x = decode(b)
        if x is not None:
            utts.append(x / (np.abs(x).max() + 1e-9))
        if sum(len(u) for u in utts) > 4 * 20 * 60 * SR:
            break
    streams = np.array_split(np.arange(len(utts)), 4)
    parts = [np.concatenate([utts[i] for i in s]) for s in streams]
    n = min(len(p) for p in parts)
    bab = sum(p[:n] for p in parts)
    bab = bab / (np.abs(bab).max() + 1e-9) * 0.5
    np.save(os.path.join(DATA, "aug", "babble.npy"), (bab * 32767).astype(np.int16))
    print(f"babble: {n / SR / 60:.1f} min", flush=True)


def job_barks():
    """Real dog barks from Wikimedia Commons (CC BY-SA / CC0, DATA/barks/meta.json): a training negative, so a real
    dog in the room does not wake the device. Each recording as 4 s pieces, clean + 3 degraded copies."""
    import glob
    rng = np.random.default_rng(11)
    pools = augment.Pools()
    meta = json.load(open(os.path.join(DATA, "barks", "meta.json")))
    st = Store("train", "barks_commons", "CC BY-SA 3.0/4.0 (Wikimedia Commons): " + ", ".join(
        sorted({m["licence"] for m in meta.values()})))
    for f in sorted(glob.glob(os.path.join(DATA, "barks", "*.wav"))):
        x, sr = sf.read(f, dtype="float32")
        for a in range(0, max(1, len(x) - SR), 4 * SR):
            seg = x[a:a + 4 * SR]
            if augment.rms(seg) < 1e-3:
                continue
            st.add(level(rng, seg))
            for _ in range(3):
                st.add(level(rng, augment.degrade(rng, seg, pools, p_reverb=0.6, p_noise=0.8, snr=(0, 25))))
    st.close()


def main():
    names = sys.argv[1:]
    if names == ["all"]:
        names = ["rir", "chime", "babble", "music"] + [k for k in JOBS if k != "music"]
    for n in names:
        if n == "rir":
            job_rir()
        elif n == "chime":
            job_chime()
        elif n == "babble":
            job_babble()
        elif n == "barks":
            job_barks()
        else:
            run_job(n)


if __name__ == "__main__":
    main()
