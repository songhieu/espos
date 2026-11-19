"""Hard-negative mining: run a trained model over the training corpora (speech, music, barks; not the held-out 5 %)
and keep 3 s windows where it fires. train.py --mined then mixes them into every batch.

    .venv/bin/python train/mine.py --name m64c [--thr 0.3] [--max 30000]
Writes DATA/sets/mined.npz (X: N x 300 x 40 int8).
"""
import argparse
import os
import sys

import mlx.core as mx
import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
import model as M  # noqa: E402
import train as TR  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--name", required=True)
    ap.add_argument("--thr", type=float, default=0.3)
    ap.add_argument("--max", type=int, default=30000)
    a = ap.parse_args()
    model = M.load(os.path.join(TR.DATA, "models", a.name))
    st = model.cfg["stride"]
    corp = TR.Corpus("train")
    rng = np.random.default_rng(0)
    hits = []
    chunk, ctx = 30000, 300
    for name, arr in corp.parts:
        n_before = len(hits)
        for s in range(0, len(arr) - 300, chunk):
            a0 = max(0, s - ctx)
            seg = np.asarray(arr[a0:s + chunk])
            p = np.array(mx.sigmoid(model(mx.array(seg[None].astype(np.float32) * M.IN_SCALE))))[0]
            sc = p.max(1)
            j0 = (s - a0) // st
            for j in np.nonzero(sc[j0:] >= a.thr)[0] + j0:
                f = j * st + a0                      # frame where it fired
                end = f + int(rng.integers(5, 60))  # the window ends a little after it
                b = end - 300
                if b < 0 or end > len(arr):
                    continue
                hits.append((float(sc[j]), name, b))
        print(f"{name}: {len(hits) - n_before} windows over {len(arr) / 360000:.1f} h", flush=True)
    # one window per firing region (they come in runs of neighbouring steps)
    hits.sort(key=lambda h: (h[1], h[2]))
    kept, last = [], {}
    for h in hits:
        if h[2] - last.get(h[1], -10 ** 9) >= 50:
            kept.append(h)
            last[h[1]] = h[2]
    kept.sort(key=lambda h: -h[0])
    kept = kept[: a.max]
    parts = dict(corp.parts)
    X = np.stack([np.asarray(parts[n][b:b + 300]) for _, n, b in kept]) if kept else np.zeros((0, 300, 40), np.int8)
    np.savez(os.path.join(TR.DATA, "sets", "mined.npz"), X=X)
    print(f"mined {len(X)} windows (score >= {a.thr})", flush=True)


if __name__ == "__main__":
    main()
