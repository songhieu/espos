"""Export a trained model to the board's int8 .ewm only (no evaluation): DATA/models/<name>.ewm.

    .venv/bin/python train/export.py NAME
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
import model as M  # noqa: E402
import train as TR  # noqa: E402


def main():
    name = sys.argv[1]
    model = M.load(os.path.join(TR.DATA, "models", name))
    Xp, _ = TR.load_set("train_pos")
    Xn, _ = TR.load_set("train_neg")
    rng = np.random.default_rng(0)
    corp = TR.Corpus("train")
    cal = np.concatenate([Xp[rng.integers(0, len(Xp), 600)], Xn[rng.integers(0, len(Xn), 400)], corp.sample(rng, 600)])
    Q = M.quantize(M.float_params(model), M.calibrate(model, cal))
    out = os.path.join(TR.DATA, "models", name + ".ewm")
    print(out, M.write_ewm(Q, out, names=TR.NAMES[: model.cfg["nout"]], thr=[0.5] * model.cfg["nout"], smooth=1), "bytes")


if __name__ == "__main__":
    main()
