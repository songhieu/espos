"""A demo recording from held-out voices (never trained on): noise, speech, confusable words and two calls, as one
16 kHz WAV, then the board's code (host/wakecli) runs on it.

    .venv/bin/python train/make_demo.py MODEL.ewm [out.wav]
"""
import json
import os
import subprocess
import sys

import numpy as np
import soundfile as sf

sys.path.insert(0, os.path.dirname(__file__))
import augment  # noqa: E402
import build_sets as BS  # noqa: E402

SR = 16000


def main():
    ewm = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(augment.DATA, "demo.wav")
    recs = [r for r in BS.load_manifest() if r["split"] == "test"]
    rng = np.random.default_rng(3)

    def pick(kind, text=None, voice=None):
        c = [r for r in recs if r["kind"] == kind and (text is None or r["text"] == text)
             and (voice is None or r["voice"] == voice)]
        r = c[rng.integers(0, len(c))]
        x, _ = sf.read(os.path.join(BS.TTS, r["f"]), dtype="float32")
        return x, r

    parts, script = [], []
    t = 0.0

    def add(x, what):
        nonlocal t
        parts.append(x)
        if what:
            script.append((round(t, 2), round(t + len(x) / SR, 2), what))
        t += len(x) / SR

    def gap(s):
        add(np.zeros(int(s * SR), np.float32), None)

    gap(1.5)
    for kind, text in (("neg", None), ("conf", "câu cá ngoài sông"), ("w2", "Gâu gâu!"), ("req", None),
                       ("conf", "ở đâu vậy?"), ("conf", "Ngâu."), ("w1", "Gâu."), ("req", None), ("neg", None),
                       ("conf", "con gấu bông"), ("w3", "Gâu gâu gâu!")):
        voice = None
        if kind == "req" and script:
            voice = script[-1][2].split(" | ")[1]
        try:
            x, r = pick(kind, text, voice)
        except ValueError:
            x, r = pick(kind, None, voice)
        add(x, f"{r['kind']}: {r['text']} | {r['voice']}")
        gap(0.15 if kind.startswith("w") else 1.2)
    y = np.concatenate(parts)
    pools = augment.Pools()
    y = augment.reverb(rng, y, pools, wet=0.5)
    bg, kind = pools.segment(rng, len(y), "noise")
    y = y + bg / (augment.rms(bg) + 1e-9) * augment.rms(y) * 10 ** (-15 / 20)
    sf.write(out, augment.to_pcm16(y, peak_db=-8), SR, subtype="PCM_16")
    print(f"{out}: {len(y) / SR:.1f} s, room reverb + {kind} noise at 15 dB SNR")
    for a, b, w in script:
        print(f"  {a:6.2f}-{b:6.2f}  {w}")
    cli = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "host", "wakecli")
    print(subprocess.run([cli, ewm, out], capture_output=True, text=True).stdout)
    json.dump(script, open(out + ".json", "w"), ensure_ascii=False)


if __name__ == "__main__":
    main()
