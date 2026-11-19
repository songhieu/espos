"""Generate the TTS part of the wake-word data, locally (no API key, no load on the live service queue).

Runs with the TTS worker's Python, which has VieNeu and Piper installed:
    python3 train/gen_tts.py --engine turbo --out wake-data/tts      (a Python with VieNeu-TTS and Piper)
Engines: turbo (VieNeu v3 Turbo, 25 voices), nano (VieNeu v3 Nano, 11 voices), piper (vivos 65 speakers + vais1000 +
25hours), say (macOS "Linh").

Writes 16 kHz mono 16-bit FLAC clips trimmed to 50 ms around the speech, and appends one JSON line per clip to
OUT/manifest_<engine>.jsonl: {"f", "kind", "text", "voice", "engine", "cfg", "dur", "on", "off"} (on/off in seconds).
Re-running skips clips that already exist.
"""
import argparse
import hashlib
import json
import os
import random
import subprocess
import sys
import tempfile
from math import gcd

import numpy as np
import soundfile as sf
from scipy.signal import resample_poly

sys.path.insert(0, os.path.dirname(__file__))
import texts  # noqa: E402

SR = 16000
PIPER_DIR = os.path.expanduser(os.environ.get("PIPER_DIR", "piper"))
MAXDUR = {"w1": 1.3, "w2": 1.9, "w3": 2.6, "conf": 3.0, "tone": 2.0, "req": 5.0, "neg": 7.0}


def to16k(x, sr):
    x = np.asarray(x, dtype=np.float32).reshape(-1)
    if sr != SR:
        g = gcd(SR, sr)
        x = resample_poly(x, SR // g, sr // g).astype(np.float32)
    return x


def speech_span(x):
    """First and last 10 ms frame whose RMS is within 35 dB of the loudest frame."""
    n = len(x) // 160
    if n < 3:
        return None
    rms = np.sqrt((x[: n * 160].reshape(n, 160) ** 2).mean(1) + 1e-12)
    th = max(rms.max() * 10 ** (-35 / 20), 3e-4)
    idx = np.nonzero(rms > th)[0]
    if len(idx) == 0:
        return None
    return idx[0] * 160, (idx[-1] + 1) * 160


class Out:
    def __init__(self, root, engine):
        self.root = root
        self.engine = engine
        os.makedirs(root, exist_ok=True)
        self.man = open(os.path.join(root, f"manifest_{engine}.jsonl"), "a")
        self.n = self.skipped = self.bad = 0

    def path(self, kind, voice, text, cfg):
        h = hashlib.sha1(f"{self.engine}|{voice}|{text}|{json.dumps(cfg, sort_keys=True)}".encode()).hexdigest()[:12]
        d = os.path.join(self.root, self.engine, kind)
        os.makedirs(d, exist_ok=True)
        return os.path.join(d, h + ".flac")

    def exists(self, kind, voice, text, cfg):
        if os.path.exists(self.path(kind, voice, text, cfg)):
            self.skipped += 1
            return True
        return False

    def save(self, kind, voice, text, cfg, x):
        span = speech_span(x)
        if span is None:
            self.bad += 1
            return
        a, b = span
        dur = (b - a) / SR
        if dur < 0.12 or dur > MAXDUR[kind]:
            self.bad += 1
            return
        m = int(0.05 * SR)
        a0, b0 = max(0, a - m), min(len(x), b + m)
        y = x[a0:b0]
        pk = np.abs(y).max()
        if pk > 0:
            y = y * (0.7 / pk)
        p = self.path(kind, voice, text, cfg)
        sf.write(p, (y * 32767).astype(np.int16), SR, format="FLAC")
        rec = {"f": os.path.relpath(p, self.root), "kind": kind, "text": text, "voice": f"{self.engine}:{voice}",
               "engine": self.engine, "cfg": cfg, "dur": round(len(y) / SR, 3),
               "on": round((a - a0) / SR, 3), "off": round((b - a0) / SR, 3)}
        self.man.write(json.dumps(rec, ensure_ascii=False) + "\n")
        self.man.flush()
        self.n += 1
        if self.n % 100 == 0:
            print(f"[{self.engine}] {self.n} saved, {self.skipped} skipped, {self.bad} bad", flush=True)


def jobs_for(voice, rng, wake_cfgs, other_cfg, n_req, n_neg, conf_frac=1.0):
    """(kind, text, cfg) list for one voice."""
    out = []
    for kind, lst in texts.WAKE.items():
        for t in lst:
            for c in wake_cfgs:
                out.append((kind, t, c))
    for t in rng.sample(texts.REQ, n_req):
        out.append(("req", t, other_cfg(rng)))
    conf = texts.CONF if conf_frac >= 1 else rng.sample(texts.CONF, int(len(texts.CONF) * conf_frac))
    for t in conf:
        out.append(("conf", t, other_cfg(rng)))
    for t in texts.TONE:
        out.append(("tone", t, other_cfg(rng)))
    for t in rng.sample(texts.NEG, n_neg):
        out.append(("neg", t, other_cfg(rng)))
    return out


def run_vieneu(mode, out, threads):
    from vieneu import Vieneu
    tts = Vieneu(mode=mode, threads=threads)
    voices = [v for _, v in tts.list_preset_voices()]
    print(f"{mode}: {len(voices)} voices", flush=True)
    rng = random.Random(1 if mode == "v3turbo" else 2)
    wake_t = [0.7, 1.0, 1.3] if mode == "v3turbo" else [0.8, 1.2]
    for v in voices:
        for kind, text, cfg in jobs_for(v, rng, [{"t": t} for t in wake_t],
                                        lambda r: {"t": round(r.uniform(0.7, 1.3), 2)}, 12, 6):
            if out.exists(kind, v, text, cfg):
                continue
            try:
                w = tts.infer(text, voice=v, temperature=cfg["t"], apply_watermark=False)
            except Exception as e:  # a voice or text the engine refuses: skip it
                print("fail", v, text, e, flush=True)
                continue
            out.save(kind, v, text, cfg, to16k(w, tts.sample_rate))


def run_clone(out, threads):
    """VieNeu v3 Turbo cloning real people's voices (DATA/refs: one clip per speaker from Speech-MASSIVE and FLEURS,
    both CC-BY 4.0): many more timbres, ages and accents than the presets."""
    import glob
    from vieneu import Vieneu
    tts = Vieneu(mode="v3turbo", threads=threads)
    refs = sorted(glob.glob(os.path.join(os.path.dirname(out.root.rstrip("/")), "refs", "*.wav")))
    print(f"clone: {len(refs)} reference voices", flush=True)
    rng = random.Random(5)
    for ref in refs:
        v = os.path.basename(ref)[:-4]
        for kind, text, cfg in jobs_for(v, rng, [{"t": 0.8}, {"t": 1.1}],
                                        lambda r: {"t": round(r.uniform(0.7, 1.2), 2)}, 8, 3, conf_frac=0.5):
            if out.exists(kind, v, text, cfg):
                continue
            try:
                w = tts.infer(text, ref_audio=ref, temperature=cfg["t"], apply_watermark=False)
            except Exception as e:
                print("fail", v, text, e, flush=True)
                continue
            out.save(kind, v, text, cfg, to16k(w, tts.sample_rate))


MIX_EXCLUDE = {"Ngọc Trân", "Thái Sơn", "Quỳnh Anh", "Quốc Tuấn", "Minh Đức", "Mỹ Duyên"}  # test / val voices


def run_mix(out, threads, n_voices=150):
    """New voices for training only: VieNeu v3 Turbo with a speaker embedding mixed from two training voices
    (presets and cloned training speakers), the accent/prosody codes of one of them or none."""
    import glob
    from vieneu import Vieneu
    tts = Vieneu(mode="v3turbo", threads=threads)
    parents = []
    for name, p in tts._preset_voices.items():
        if name not in MIX_EXCLUDE and p.get("speaker_emb") is not None:
            parents.append((name, np.asarray(p["speaker_emb"], np.float32), p.get("codes")))
    root = os.path.join(os.path.dirname(out.root.rstrip("/")), "refs")
    for ref in sorted(glob.glob(os.path.join(root, "*.wav"))):
        v = os.path.basename(ref)[:-4]
        k = int(v.rsplit("_", 1)[1])
        if (v.startswith("massive") and k >= 16) or (v.startswith("fleurs") and k >= 20):
            continue  # val / test speakers
        try:
            emb, codes = tts._enroll_reference(ref, denoise=True, use_ref_codes=True)
            parents.append((v, np.asarray(emb, np.float32).reshape(-1), codes))
        except Exception as e:
            print("enroll failed", v, e, flush=True)
    print(f"mix: {len(parents)} parents", flush=True)
    rng = random.Random(6)
    for i in range(n_voices):
        a, b = rng.sample(parents, 2)
        w = rng.uniform(0.25, 0.75)
        e = w * a[1] + (1 - w) * b[1]
        e = e / (np.linalg.norm(e) + 1e-9) * (w * np.linalg.norm(a[1]) + (1 - w) * np.linalg.norm(b[1]))
        voice = {"speaker_emb": e.astype(np.float32), "codes": a[2] if rng.random() < 0.6 else None}
        vname = f"mix{i:03d}"
        jobs = [(k, t, {"t": round(rng.uniform(0.7, 1.2), 2)}) for k, lst in texts.WAKE.items() for t in lst]
        jobs += [("conf", t, {"t": 1.0}) for t in rng.sample(texts.CONF, 12)]
        jobs += [("req", t, {"t": 1.0}) for t in rng.sample(texts.REQ, 3)]
        for kind, text, cfg in jobs:
            if out.exists(kind, vname, text, cfg):
                continue
            try:
                wv = tts.infer(text, voice=voice, use_ref_codes=voice["codes"] is not None, temperature=cfg["t"],
                               apply_watermark=False)
            except Exception as ex:
                print("fail", vname, text, ex, flush=True)
                continue
            out.save(kind, vname, text, cfg, to16k(wv, tts.sample_rate))


def run_piper(out):
    import onnxruntime as ort
    from piper import PiperVoice, SynthesisConfig
    rng = random.Random(3)
    models = {}
    for name in ["vivos-x_low", "vais1000-medium", "25hours_single-low"]:
        path = os.path.join(PIPER_DIR, f"vi_VN-{name}.onnx")
        v = PiperVoice.load(path)
        so = ort.SessionOptions()
        so.intra_op_num_threads = 2
        v.session = ort.InferenceSession(path, sess_options=so, providers=["CPUExecutionProvider"])
        models[name] = v
    speakers = [("vivos-x_low", s) for s in range(models["vivos-x_low"].config.num_speakers)]
    speakers += [("vais1000-medium", None), ("25hours_single-low", None)]

    def cfg_rand(r):
        return {"ls": round(r.uniform(0.85, 1.35), 2), "ns": round(r.uniform(0.4, 0.9), 2),
                "nw": round(r.uniform(0.5, 1.0), 2)}

    wake_cfgs = [{"ls": 0.9, "ns": 0.5, "nw": 0.6}, {"ls": 1.1, "ns": 0.75, "nw": 0.8},
                 {"ls": 1.35, "ns": 0.9, "nw": 1.0}]
    for name, spk in speakers:
        vname = name if spk is None else f"{name}#{spk}"
        voice = models[name]
        for kind, text, cfg in jobs_for(vname, rng, wake_cfgs, cfg_rand, 10, 6):
            if out.exists(kind, vname, text, cfg):
                continue
            sc = SynthesisConfig(speaker_id=spk, length_scale=cfg["ls"], noise_scale=cfg["ns"],
                                 noise_w_scale=cfg["nw"])
            chunks = [c.audio_float_array for c in voice.synthesize(text, syn_config=sc)]
            if not chunks:
                out.bad += 1
                continue
            out.save(kind, vname, text, cfg, to16k(np.concatenate(chunks), voice.config.sample_rate))


def run_say(out):
    rng = random.Random(4)
    with tempfile.TemporaryDirectory() as td:
        tmp = os.path.join(td, "s.wav")
        for kind, text, cfg in jobs_for("Linh", rng, [{"r": r} for r in (150, 185, 220, 260)],
                                        lambda r: {"r": r.choice([160, 190, 220])}, 25, 10):
            if out.exists(kind, "Linh", text, cfg):
                continue
            subprocess.run(["say", "-v", "Linh", "-r", str(cfg["r"]), "-o", tmp, "--data-format=LEI16@16000", text],
                           check=True)
            x, sr = sf.read(tmp, dtype="float32")
            out.save(kind, "Linh", text, cfg, to16k(x, sr))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--engine", required=True, choices=["turbo", "nano", "piper", "say", "clone", "mix"])
    ap.add_argument("--out", default=os.path.expanduser("wake-data/tts"))
    ap.add_argument("--threads", type=int, default=4)
    a = ap.parse_args()
    out = Out(a.out, a.engine)
    if a.engine == "turbo":
        run_vieneu("v3turbo", out, a.threads)
    elif a.engine == "nano":
        run_vieneu("v3nano", out, a.threads)
    elif a.engine == "piper":
        run_piper(out)
    elif a.engine == "clone":
        run_clone(out, a.threads)
    elif a.engine == "mix":
        run_mix(out, a.threads)
    else:
        run_say(out)
    print(f"[{a.engine}] done: {out.n} saved, {out.skipped} skipped, {out.bad} bad", flush=True)


if __name__ == "__main__":
    main()
