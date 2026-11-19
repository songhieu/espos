"""Export a trained model to int8 (.ewm) and evaluate it the way the board runs it.

    .venv/bin/python train/evaluate.py --name m64 [--quick]

1. fold BN, calibrate activation scales, quantise, write DATA/models/<name>.ewm (+ a copy in wake/models/)
2. check the numpy int8 model against the C runtime (host/libwake.dylib) on test clips: logits must match
3. recall on the held-out voices (5 conditions), float vs int8
4. false accepts per hour on long held-out audio (streamed through the int8 model with the board's smoothing
   and refractory), per clip on confusable words, other tones of gâu, real dog barks (ESC-50)
Writes wake/reports/<name>.json and <name>.md.
"""
import argparse
import ctypes
import glob
import json
import os
import shutil
import sys

import mlx.core as mx
import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
import model as M  # noqa: E402
import train as TR  # noqa: E402

DATA = TR.DATA
HERE = os.path.dirname(os.path.abspath(__file__))
WAKE = os.path.dirname(HERE)
THRS = [0.5, 0.7, 0.8, 0.9, 0.95, 0.97, 0.98, 0.99]
COND = ["clean", "noise 10 dB", "noise 5 dB", "noise 0 dB", "far + 10 dB"]


def smooth(p, k):
    if k <= 1:
        return p
    c = np.cumsum(np.pad(p, [(0, 0), (k - 1, 0), (0, 0)]), axis=1)
    out = c[:, k - 1:] - np.pad(c, [(0, 0), (1, 0), (0, 0)])[:, :c.shape[1] - k + 1]
    return out / k


def int8_probs(Q, X, bs=256, k=3):
    res = []
    for i in range(0, len(X), bs):
        lg = M.run_int8(Q, X[i:i + bs])
        res.append(1 / (1 + np.exp(-lg)))
    return smooth(np.concatenate(res), k)


def float_probs(model, X, bs=512, k=3):
    res = []
    for i in range(0, len(X), bs):
        res.append(np.array(mx.sigmoid(model(mx.array(X[i:i + bs].astype(np.float32) * M.IN_SCALE)))))
    return smooth(np.concatenate(res), k)


def stream_events(Q, frames, thr, stride, rf, refractory=50, k=3, chunk=6000):
    """Detections in a long int8 feature stream. Returns {thr: [count out0, count out1]} and the stream hours."""
    n = len(frames)
    probs = []
    s = 0
    ctx = (rf // stride + 2) * stride
    while s < n:
        a = max(0, s - ctx)
        seg = frames[a: s + chunk]
        p = int8_probs(Q, seg[None], k=1)[0]
        skip = (s - a) // stride
        probs.append(p[skip:])
        s += chunk
    p = smooth(np.concatenate(probs)[None], k)[0]
    out = {}
    for t in thr:
        cnt = []
        for o in range(p.shape[1]):
            c, quiet = 0, 0
            above = p[:, o] >= t
            for j in np.nonzero(above)[0]:
                if j >= quiet:
                    c += 1
                    quiet = j + refractory + 1
            cnt.append(c)
        out[t] = cnt
    return out, n / 360000


def c_parity(Q, ewm_path, X):
    lib = ctypes.CDLL(os.path.join(WAKE, "host", "libwake.dylib"))
    blob = open(ewm_path, "rb").read()
    mb = ctypes.create_string_buffer(blob, len(blob))
    st = ctypes.create_string_buffer(int(lib.ewm_state_size(mb)) + 16)
    m = ctypes.create_string_buffer(8192)
    assert lib.ewm_init(m, mb, len(blob), st) == 0
    logits = (ctypes.c_float * 4).in_dll(lib, "ewm_last_logits")
    worst, n = 0.0, 0
    for x in X:
        lib.ewm_reset(m)
        ref = M.run_int8(Q, x[None])[0]
        j = 0
        for f in range(len(x)):
            fr = np.ascontiguousarray(x[f])
            r = lib.ewm_push(m, fr.ctypes.data_as(ctypes.c_void_p), None)
            if r >= 0:
                worst = max(worst, max(abs(logits[o] - ref[j, o]) for o in range(ref.shape[1])))
                j += 1
                n += 1
    return worst, n


def recall_table(P, lab, thr):
    """P: smoothed probs (N, S, 2). Hit = crosses thr between on and off + 0.6 s; early = crosses before on."""
    S = P.shape[1]
    t = TR.step_times(S, 3)[None, :]
    on, off = lab[:, 1:2], lab[:, 2:3]
    win = (t >= on) & (t <= off + int(0.6 * 16000))
    early = t < on - 1600
    out = {}
    for o in range(2):
        sel = lab[:, 0] >= (1 if o == 0 else 2)
        hit = (np.where(win, P[:, :, o], 0).max(1) >= thr) & sel
        out[o] = (hit[sel].mean() if sel.any() else float("nan"),
                  ((np.where(early, P[:, :, o], 0).max(1) >= thr) & sel).sum())
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--name", default="m64")
    ap.add_argument("--quick", action="store_true")
    ap.add_argument("--smooth", type=int, default=1, help="steps averaged before the threshold (board: header)")
    a = ap.parse_args()
    mdir = os.path.join(DATA, "models")
    model = M.load(os.path.join(mdir, a.name))
    cfg = model.cfg
    rf = M.receptive_field(cfg)
    rep = {"name": a.name, "cfg": cfg, "receptive_field_ms": rf * 10, "smooth": a.smooth}

    # 1. export
    Xp, Lp = TR.load_set("train_pos")
    Xn, Ln = TR.load_set("train_neg")
    rng = np.random.default_rng(0)
    corp = TR.Corpus("train")
    cal = np.concatenate([Xp[rng.integers(0, len(Xp), 600)], Xn[rng.integers(0, len(Xn), 400)], corp.sample(rng, 600)])
    scales = M.calibrate(model, cal)
    Q = M.quantize(M.float_params(model), scales)
    ewm = os.path.join(mdir, a.name + ".ewm")
    size = M.write_ewm(Q, ewm, smooth=a.smooth)
    os.makedirs(os.path.join(WAKE, "models"), exist_ok=True)
    shutil.copy(ewm, os.path.join(WAKE, "models", a.name + ".ewm"))
    lib = ctypes.CDLL(os.path.join(WAKE, "host", "libwake.dylib"))
    state = int(lib.ewm_state_size(ctypes.create_string_buffer(open(ewm, "rb").read())))
    nparams = sum(int(np.prod(np.shape(v))) for v in [Q["stem"][0], Q["head"][0], Q["out"][0]] +
                  [b[0] for b in Q["blocks"]] + [b[3] for b in Q["blocks"]])
    macs = Q["stem"][0].size + sum(b[0].size + b[3].size for b in Q["blocks"]) + Q["head"][0].size + Q["out"][0].size
    rep.update(model_bytes=size, state_bytes=state, int8_weights=nparams, macs_per_step=int(macs),
               macs_per_second=int(macs * 100 / cfg["stride"]))
    print(f"model {size} B, state {state} B, {macs} MAC/step", flush=True)

    # 2. C parity
    Vp, VLp = TR.load_set("test_pos")
    worst, n = c_parity(Q, ewm, Vp[:12])
    rep["c_parity_max_logit_diff"] = worst
    print(f"C runtime vs numpy int8: max |logit diff| {worst:.2e} over {n} steps", flush=True)

    # 3. recall, held-out voices
    Pf = float_probs(model, Vp, k=a.smooth)
    Pq = int8_probs(Q, Vp, k=a.smooth)
    rec = {}
    for thr in THRS:
        row = {}
        for c, cn in enumerate(COND):
            sel = VLp[:, 3] == c
            rq = recall_table(Pq[sel], VLp[sel], thr)
            rf_ = recall_table(Pf[sel], VLp[sel], thr)
            row[cn] = {"gau": round(rq[0][0], 4), "gau2": round(rq[1][0], 4), "gau_float": round(rf_[0][0], 4),
                       "gau2_float": round(rf_[1][0], 4), "early": int(rq[0][1])}
        rec[thr] = row
    rep["recall"] = rec
    # which held-out voices miss
    sel = VLp[:, 3] <= 1
    per_kind = {}
    for kd, kn in ((1, "gâu"), (2, "gâu gâu"), (3, "gâu gâu gâu")):
        s = sel & (VLp[:, 0] == kd)
        per_kind[kn] = {str(t): round(float(recall_table(Pq[s], VLp[s], t)[0][0]), 4) for t in (0.8, 0.9)}
    rep["recall_by_kind_clean_10dB"] = per_kind
    z = np.load(os.path.join(DATA, "sets", "test_pos.npz"))
    if "fam" in z.files:
        fams = ["turbo", "nano", "vivos", "piper", "say", "clone"]
        per_fam = {}
        for f in np.unique(z["fam"]):
            s = sel & (z["fam"] == f)
            per_fam[fams[f]] = {str(t): [round(float(recall_table(Pq[s], VLp[s], t)[o][0]), 3) for o in (0, 1)]
                                for t in (0.8, 0.9, 0.95)}
        rep["recall_by_voice_family_clean_10dB"] = per_fam

    # 4. confusables / tones (held-out voices)
    Tn, TLn = TR.load_set("test_neg")
    Pn = int8_probs(Q, Tn, k=a.smooth)
    texts = json.load(open(os.path.join(DATA, "sets", "test_neg_texts.json"))) if os.path.exists(
        os.path.join(DATA, "sets", "test_neg_texts.json")) else None
    conf = {}
    for thr in THRS:
        mx0 = Pn.max(1)
        conf[thr] = {
            "confusable_and_sentences_gau": round(float((mx0[TLn[:, 0] == 0, 0] >= thr).mean()), 4),
            "confusable_and_sentences_gau2": round(float((mx0[TLn[:, 0] == 0, 1] >= thr).mean()), 4),
            "other_tone_alone_gau": round(float((mx0[TLn[:, 0] == 4, 0] >= thr).mean()), 4),
            "other_tone_alone_gau2": round(float((mx0[TLn[:, 0] == 4, 1] >= thr).mean()), 4),
        }
    rep["confusables_per_clip"] = conf
    if texts:
        mx0 = Pn.max(1)[:, 0]
        worst_t = {}
        for i in np.argsort(-mx0)[:400]:
            if mx0[i] >= 0.8:
                worst_t[texts[i]] = worst_t.get(texts[i], 0) + 1
        rep["confusables_firing_most_at_0.8"] = dict(sorted(worst_t.items(), key=lambda kv: -kv[1])[:15])

    # 5. long audio false accepts per hour
    fa = {}
    for d in sorted(glob.glob(os.path.join(DATA, "feats", "eval", "*"))):
        name = os.path.basename(d)
        if not os.path.exists(os.path.join(d, "meta.json")):
            continue
        fr = np.fromfile(os.path.join(d, "frames.i8"), dtype=np.int8).reshape(-1, 40)
        meta = json.load(open(os.path.join(d, "meta.json")))
        if a.quick:
            fr = fr[:360000]
        if name == "esc50":
            utt = np.load(os.path.join(d, "utt.npy"))
            labels = meta.get("labels", [])
            dog = [i for i, l in enumerate(labels) if l == "dog"]
            Xd = np.stack([np.pad(fr[utt[i]:utt[i] + 500], [(0, max(0, 500 - len(fr[utt[i]:utt[i] + 500]))), (0, 0)],
                                  constant_values=-124)[:500] for i in dog]) if dog else None
            if Xd is not None:
                Pd = int8_probs(Q, Xd, k=a.smooth).max(1)
                fa["esc50_dog_clips"] = {str(t): [int((Pd[:, 0] >= t).sum()), int((Pd[:, 1] >= t).sum()), len(dog)]
                                         for t in THRS}
        ev, hours = stream_events(Q, fr, THRS, cfg["stride"], rf, k=a.smooth)
        fa[name] = {"hours": round(hours, 2), "licence": meta.get("licence"),
                    "per_hour": {str(t): [round(c / hours, 3) for c in v] for t, v in ev.items()},
                    "count": {str(t): v for t, v in ev.items()}}
        print(name, round(hours, 2), "h", {t: ev[t] for t in (0.8, 0.9, 0.95)}, flush=True)
    tot_h = sum(v["hours"] for k, v in fa.items() if "hours" in v)
    fa["ALL"] = {"hours": round(tot_h, 2), "per_hour": {
        str(t): [round(sum(v["count"][str(t)][o] for k, v in fa.items() if "count" in v) / max(tot_h, 1e-9), 3)
                 for o in range(2)] for t in THRS}}
    rep["false_accepts"] = fa
    os.makedirs(os.path.join(WAKE, "reports"), exist_ok=True)
    json.dump(rep, open(os.path.join(WAKE, "reports", a.name + ".json"), "w"), ensure_ascii=False, indent=1)
    write_md(rep, os.path.join(WAKE, "reports", a.name + ".md"))
    print("report written", flush=True)


def write_md(rep, path):
    L = [f"# Wake model {rep['name']}", "",
         f"- config: {rep['cfg']}, receptive field {rep['receptive_field_ms']} ms",
         f"- model file {rep['model_bytes']} B, state {rep['state_bytes']} B, {rep['macs_per_step']} MAC per 30 ms step "
         f"({rep['macs_per_second'] / 1e6:.2f} M MAC/s)",
         f"- C runtime vs numpy int8: max logit difference {rep['c_parity_max_logit_diff']:.2e}", "",
         f"## Recall on held-out voices (int8, smoothing {rep['smooth']} step(s))", "",
         "| thr | " + " | ".join(f"{c} gâu / gâu gâu" for c in COND) + " |",
         "|---|" + "---|" * len(COND)]
    for t, row in rep["recall"].items():
        L.append(f"| {t} | " + " | ".join(f"{row[c]['gau']:.3f} / {row[c]['gau2']:.3f}" for c in COND) + " |")
    if rep.get("recall_by_voice_family_clean_10dB"):
        L += ["", "Recall by voice family (clean + 10 dB; gâu / gâu gâu): " + "; ".join(
            f"{k}: " + ", ".join(f"{t} {v[0]:.2f}/{v[1]:.2f}" for t, v in d.items())
            for k, d in rep["recall_by_voice_family_clean_10dB"].items())]
    L += ["", "## False accepts per hour (outputs gâu / gâu gâu)", "",
          "| audio | hours | " + " | ".join(str(t) for t in THRS) + " |", "|---|---|" + "---|" * len(THRS)]
    for k, v in rep["false_accepts"].items():
        if "per_hour" in v:
            L.append(f"| {k} | {v['hours']} | " + " | ".join(f"{v['per_hour'][str(t)][0]} / {v['per_hour'][str(t)][1]}"
                                                         for t in THRS) + " |")
    if "esc50_dog_clips" in rep["false_accepts"]:
        d = rep["false_accepts"]["esc50_dog_clips"]
        L += ["", "Real dog barks (ESC-50, 5 s clips firing / total): " +
              ", ".join(f"thr {t}: {v[0]}/{v[2]} gâu, {v[1]}/{v[2]} gâu gâu" for t, v in d.items())]
    L += ["", "## Confusable words and sentences, held-out voices (share of clips firing)", "",
          "| thr | confusables gâu | confusables gâu gâu | other tone alone gâu | other tone alone gâu gâu |", "|---|---|---|---|---|"]
    for t, v in rep["confusables_per_clip"].items():
        L.append(f"| {t} | {v['confusable_and_sentences_gau']} | {v['confusable_and_sentences_gau2']} | "
                 f"{v['other_tone_alone_gau']} | {v['other_tone_alone_gau2']} |")
    if rep.get("confusables_firing_most_at_0.8"):
        L += ["", "Most firing confusables at 0.8: " + ", ".join(f"{k} ({v})" for k, v in rep["confusables_firing_most_at_0.8"].items())]
    open(path, "w").write("\n".join(L) + "\n")


if __name__ == "__main__":
    main()
