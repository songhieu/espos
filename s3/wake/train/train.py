"""Train the keyword spotter on the sets from build_sets.py and the corpus features from fetch.py (MLX, Apple GPU).

    .venv/bin/python train/train.py --name m64 --ch 64 --steps 12000

Targets per 30 ms step (t = time the step has heard up to):
  out 0 "gau"  (any gâu):    max over [on + 0.25 s, off + 0.35 s] -> 1;  steps before on + 0.05 s -> 0
  out 1 "gau2" (gâu gâu):    w2/w3: max over [off - 0.1 s, off + 0.35 s] -> 1, steps before on + 0.3 s -> 0;
                             w1: every step up to off + 1.0 s -> 0
  negatives (TTS confusables, corpus speech, music, noise): every step -> 0 (mean + the clip's worst step)
"""
import argparse
import glob
import json
import os
import sys
import time

import mlx.core as mx
import mlx.nn as nn
import mlx.optimizers as optim
import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
import model as M  # noqa: E402

DATA = os.path.expanduser(os.environ.get("WAKE_DATA", "wake-data"))
T = 300
SR = 16000


class Corpus:
    """Random 3 s crops of the training corpus features (all of feats/train/*), last 5 % held out for validation."""

    def __init__(self, split="train", hold=0.05, src=None):
        self.parts = []
        src = src or os.environ.get("WAKE_CORPUS", "train")
        for d in sorted(glob.glob(os.path.join(DATA, "feats", src, "*"))):
            p = os.path.join(d, "frames.i8")
            if not os.path.exists(os.path.join(d, "meta.json")):
                continue  # still being written
            a = np.memmap(p, dtype=np.int8, mode="r").reshape(-1, 40)
            n = len(a)
            cut = int(n * (1 - hold))
            a = a[:cut] if split == "train" else a[cut:]
            if len(a) > T:
                self.parts.append((os.path.basename(d), a))
        # sqrt of the length: a big source (26 h of music) does not drown the small ones
        self.w = np.sqrt(np.array([len(a) for _, a in self.parts], dtype=np.float64))
        self.w /= self.w.sum()

    def hours(self):
        return {n: round(len(a) / 360000, 2) for n, a in self.parts}

    def sample(self, rng, n):
        out = np.empty((n, T, 40), np.int8)
        ks = rng.choice(len(self.parts), size=n, p=self.w)
        for i, k in enumerate(ks):
            a = self.parts[k][1]
            s = rng.integers(0, len(a) - T)
            out[i] = a[s:s + T]
        return out


def step_times(steps, stride):
    return (np.arange(steps) * stride + 1) * 160  # samples heard by each step


def targets(lab, steps, stride):
    """-> pos window mask (N, S, 2), neg mask (N, S, 2), has_pos (N, 2)."""
    t = step_times(steps, stride)[None, :]
    kind, on, off = lab[:, 0:1], lab[:, 1:2], lab[:, 2:3]
    N = len(lab)
    pw = np.zeros((N, steps, 2), bool)
    ng = np.zeros((N, steps, 2), bool)
    isw = (kind >= 1) & (kind <= 3)
    neg = ~isw & (kind != 4)
    pw[:, :, 0] = isw & (t >= on + 0.25 * SR) & (t <= off + 0.35 * SR)
    ng[:, :, 0] = (isw & (t < on + 0.05 * SR)) | neg
    w23 = (kind == 2) | (kind == 3)
    pw[:, :, 1] = w23 & (t >= off - 0.1 * SR) & (t <= off + 0.35 * SR)
    ng[:, :, 1] = (w23 & (t < on + 0.3 * SR)) | ((kind == 1) & (t < off + 1.0 * SR)) | neg
    return pw, ng, pw.any(1)


def loss_fn(model, x, pw, ng, hp, w_hard):
    lg = model(x)  # (N, S, 2)
    big = mx.array(-1e4, dtype=mx.float32)
    mpos = mx.max(mx.where(pw, lg, big), axis=1)  # (N, 2)
    lpos = nn.losses.binary_cross_entropy(mpos, mx.ones_like(mpos), reduction="none")
    lpos = (lpos * hp).sum() / mx.maximum(hp.sum(), 1)
    lneg_all = nn.losses.binary_cross_entropy(lg, mx.zeros_like(lg), reduction="none")
    lneg = (lneg_all * ng).sum() / mx.maximum(ng.sum(), 1)
    mneg = mx.max(mx.where(ng, lg, big), axis=1)
    hn = ng.any(axis=1)
    lhard = nn.losses.binary_cross_entropy(mneg, mx.zeros_like(mneg), reduction="none")
    lhard = (lhard * hn).sum() / mx.maximum(hn.sum(), 1)
    return lpos + lneg * 4.0 + w_hard * lhard


def load_set(name, fam=False):
    z = np.load(os.path.join(DATA, "sets", name + ".npz"))
    if fam:
        return z["X"], z["lab"], (z["fam"], z["voice"])
    return z["X"], z["lab"]


def family_weights(fv):
    """Sampling weights: every voice the same share, a Piper vivos speaker (64 low-quality toneless voices that
    would otherwise make half of the data) a third of that."""
    fam, voice = fv
    w = np.zeros(len(fam))
    for v in np.unique(voice):
        m = voice == v
        w[m] = (0.33 if fam[m][0] == 2 else 1.0) / m.sum()
    return w / w.sum()


def evaluate(model, X, lab, stride, bs=512):
    """Max probability inside the positive window (or over all steps for negatives), per output."""
    model.eval()
    res = []
    for i in range(0, len(X), bs):
        lg = model(mx.array(X[i:i + bs].astype(np.float32) * M.IN_SCALE))
        p = np.array(mx.sigmoid(lg))
        steps = p.shape[1]
        pw, ng, hp = targets(lab[i:i + bs], steps, stride)
        r = np.zeros((len(p), 4), np.float32)
        r[:, 0:2] = np.where(pw, p, 0).max(1)       # best inside the window
        r[:, 2:4] = np.where(ng, p, 0).max(1)       # worst where it must stay quiet
        res.append(r)
    model.train()
    return np.concatenate(res)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--name", default="m64")
    ap.add_argument("--ch", type=int, default=64)
    ap.add_argument("--hc", type=int, default=32)
    ap.add_argument("--dil", default="1,2,4,4")
    ap.add_argument("--k", type=int, default=5)
    ap.add_argument("--steps", type=int, default=12000)
    ap.add_argument("--bs", type=int, default=256)
    ap.add_argument("--lr", type=float, default=2e-3)
    ap.add_argument("--hard", type=float, default=1.0)
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--init", default="", help="start from this model's weights (fine-tuning)")
    ap.add_argument("--mined", type=float, default=0.0, help="share of each batch from sets/mined.npz")
    ap.add_argument("--warmup", type=int, default=500)
    ap.add_argument("--speaker", type=float, default=0.0, help="share of the positives from sets/speaker_train.npz")
    ap.add_argument("--spkneg", type=float, default=0.0, help="share of each batch from sets/speaker_neg.npz")
    a = ap.parse_args()
    rng = np.random.default_rng(a.seed)
    mx.random.seed(a.seed)
    out_dir = os.path.join(DATA, "models")
    os.makedirs(out_dir, exist_ok=True)

    Xp, Lp, Fp = load_set("train_pos", fam=True)
    Xn, Ln, Fn = load_set("train_neg", fam=True)
    wp, wn = family_weights(Fp), family_weights(Fn)
    print("positives per family", np.bincount(Fp[0]).tolist(), "share", [round(float(wp[Fp[0] == f].sum()), 3)
                                                                         for f in range(6)], flush=True)
    Vp, VLp = load_set("val_pos")
    Vn, VLn = load_set("val_neg")
    corp = Corpus("train")
    vcorp = Corpus("val")
    print("pos", Xp.shape, "tts neg", Xn.shape, "corpus h", corp.hours(), flush=True)
    vneg_c = vcorp.sample(np.random.default_rng(1), 3000)
    vneg_lab = np.zeros((len(vneg_c), 4), np.int32)
    vneg_lab[:, 1:] = -1

    if a.init:
        model = M.load(os.path.join(out_dir, a.init))
        model.train()
    else:
        model = M.KWS(ch=a.ch, hc=a.hc, k=a.k, dil=tuple(int(v) for v in a.dil.split(",")))
    Xm = np.load(os.path.join(DATA, "sets", "mined.npz"))["X"] if a.mined > 0 else None
    Xo = Lo = None
    if a.speaker > 0:
        z = np.load(os.path.join(DATA, "sets", "speaker_train.npz"))
        Xo, Lo = z["X"], z["lab"]
        print("speaker positives", Xo.shape, flush=True)
    Xon = np.load(os.path.join(DATA, "sets", "speaker_neg.npz"))["X"] if a.spkneg > 0 else None
    if Xm is not None:
        print("mined negatives", Xm.shape, flush=True)
    cfg = model.cfg
    print("cfg", cfg, "receptive field", M.receptive_field(cfg) * 10, "ms", flush=True)
    nparams = sum(v.size for _, v in nn.utils.tree_flatten(model.parameters()))
    print("params", nparams, flush=True)
    sched = optim.join_schedules([optim.linear_schedule(1e-5, a.lr, a.warmup),
                                  optim.cosine_decay(a.lr, a.steps - a.warmup, 1e-5)], [a.warmup])
    opt = optim.AdamW(learning_rate=sched, weight_decay=1e-4)
    lg_fn = nn.value_and_grad(model, loss_fn)
    stride = cfg["stride"]
    steps_per = (T - 1) // stride + 1
    n_pos, n_tn = int(a.bs * 0.35), int(a.bs * 0.25)
    n_m = int(a.bs * a.mined) if Xm is not None and len(Xm) else 0
    n_c = a.bs - n_pos - n_tn - n_m
    best = (-1, None)
    t0 = time.time()
    for it in range(1, a.steps + 1):
        n_own = int(n_pos * a.speaker) if Xo is not None else 0
        ip = rng.choice(len(Xp), n_pos - n_own, p=wp)
        iN = rng.choice(len(Xn), n_tn, p=wn)
        xc = corp.sample(rng, n_c)
        if n_m:
            xc = np.concatenate([xc, Xm[rng.integers(0, len(Xm), n_m)]])
        if Xon is not None:
            xc[: int(a.bs * a.spkneg)] = Xon[rng.integers(0, len(Xon), int(a.bs * a.spkneg))]
        if n_own:
            io = rng.integers(0, len(Xo), n_own)
            X = np.concatenate([Xp[ip], Xo[io], Xn[iN], xc])
            lab = np.concatenate([Lp[ip], Lo[io], Ln[iN], np.tile(np.array([[0, -1, -1, -1]], np.int32), (len(xc), 1))])
        else:
            X = np.concatenate([Xp[ip], Xn[iN], xc])
            lab = np.concatenate([Lp[ip], Ln[iN], np.tile(np.array([[0, -1, -1, -1]], np.int32), (len(xc), 1))])
        # feature-domain augmentation: a little time masking / level shift
        if rng.random() < 0.5:
            X = X.astype(np.int16)
            X += rng.integers(-6, 7, size=(len(X), 1, 1)).astype(np.int16)
            X = np.clip(X, -128, 127).astype(np.int8)
        if rng.random() < 0.3:  # frequency masking: a few neighbouring bands at the clip's floor
            X = X.copy()
            for i in rng.integers(0, len(X), len(X) // 4):
                b0 = rng.integers(0, 37)
                X[i, :, b0:b0 + rng.integers(1, 4)] = X[i].min()
        pw, ng, hp = targets(lab, steps_per, stride)
        loss, grads = lg_fn(model, mx.array(X.astype(np.float32) * M.IN_SCALE), mx.array(pw), mx.array(ng),
                            mx.array(hp.astype(np.float32)), a.hard)
        opt.update(model, grads)
        mx.eval(model.parameters(), opt.state)
        if it % 200 == 0:
            print(f"it {it} loss {loss.item():.4f} lr {opt.learning_rate.item():.2e} {time.time() - t0:.0f}s", flush=True)
        if it % 1000 == 0 or it == a.steps:
            rp = evaluate(model, Vp, VLp, stride)
            rn = evaluate(model, Vn, VLn, stride)
            rc = evaluate(model, vneg_c, vneg_lab, stride)
            k = VLp[:, 0]
            msg = []
            score = 0
            for thr in (0.5, 0.8, 0.9):
                r0 = (rp[:, 0] >= thr).mean()
                r1 = (rp[k >= 2, 1] >= thr).mean()
                fa_t = (rn[VLn[:, 0] == 0, 2] >= thr).mean()
                fa_c = (rc[:, 2] >= thr).mean()
                fa_c1 = (rc[:, 3] >= thr).mean()
                msg.append(f"thr {thr}: rec gau {r0:.3f} gau2 {r1:.3f} | fa conf {fa_t:.4f} corpus {fa_c:.4f}/{fa_c1:.4f}")
                if thr == 0.8:
                    score = r0 + r1 - 5 * (fa_t + fa_c + fa_c1)
            print(f"== val it {it}: " + " || ".join(msg), flush=True)
            if score > best[0]:
                best = (score, it)
                M.save(model, os.path.join(out_dir, a.name))
    M.save(model, os.path.join(out_dir, a.name + "_last"))
    print("best", best, flush=True)
    json.dump({"best_it": best[1], "score": best[0], "args": vars(a)}, open(os.path.join(out_dir, a.name + ".train.json"), "w"))


if __name__ == "__main__":
    main()
