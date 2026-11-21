"""Export and evaluate a model with the second round's calls (outputs gau, gau2, kiki, lulu, cun, milu), the way the
board runs it (int8, the C runtime's numbers).

    .venv/bin/python train/evaluate_calls.py --name c6a [--quick]

1. export DATA/models/<name>.ewm (version 2 header: names and default thresholds of all six outputs)
2. C runtime vs numpy int8 on test clips (every output)
3. recall per call on held-out voices (5 conditions); a call firing another call's output ("cross")
4. confusables per clip (held-out voices), the texts that fire each output most
5. false wakes per hour on the long held-out audio (feats/eval), per output, and real dog barks
6. thresholds per sensitivity (low, normal, high) for the new calls: the lowest threshold whose false wakes per hour
   stay under a budget, written to wake.json's "thr" shape
Writes wake/reports/<name>.json and <name>.md.
"""
import argparse
import glob
import json
import os
import shutil
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
import evaluate as EV  # noqa: E402
import model as M  # noqa: E402
import train as TR  # noqa: E402

DATA = TR.DATA
WAKE = EV.WAKE
THRS = [0.5, 0.6, 0.7, 0.8, 0.9, 0.95, 0.97, 0.98, 0.99]
LABEL = {"gau": "gâu", "gau2": "gâu gâu", "kiki": "ki ki", "lulu": "lu lu", "cun": "cún ơi", "milu": "milu"}
# False wakes an hour of dense held-out speech, music and noise each new call may add, per sensitivity.
BUDGET = {"low": 0.1, "normal": 0.5, "high": 1.0}


def positives(o, kind):
    if o == 0:
        return (kind >= 1) & (kind <= 3)
    if o == 1:
        return (kind == 2) | (kind == 3)
    return kind == TR.CALL_KIND[o]


def hit_rate(P, lab, o, thr):
    """Share of output o's positives where it crosses thr between on and off + 0.6 s."""
    sel = positives(o, lab[:, 0])
    if not sel.any():
        return float("nan")
    S = P.shape[1]
    t = TR.step_times(S, 3)[None, :]
    win = (t >= lab[:, 1:2]) & (t <= lab[:, 2:3] + int(0.6 * 16000))
    return float((np.where(win, P[:, :, o], 0).max(1)[sel] >= thr).mean())


def c_parity(Q, ewm, X, nout):
    import ctypes
    lib = ctypes.CDLL(os.path.join(WAKE, "host", "libwake.dylib"))
    blob = open(ewm, "rb").read()
    mb = ctypes.create_string_buffer(blob, len(blob))
    st = ctypes.create_string_buffer(int(lib.ewm_state_size(mb)) + 16)
    m = ctypes.create_string_buffer(16384)
    e = lib.ewm_init(m, mb, len(blob), st)
    assert e == 0, f"ewm_init {e}"
    logits = (ctypes.c_float * 8).in_dll(lib, "ewm_last_logits")
    worst, n = 0.0, 0
    for x in X:
        lib.ewm_reset(m)
        ref = M.run_int8(Q, x[None])[0]
        j = 0
        for f in range(len(x)):
            fr = np.ascontiguousarray(x[f])
            if lib.ewm_push(m, fr.ctypes.data_as(ctypes.c_void_p), None) >= 0:
                worst = max(worst, max(abs(logits[o] - ref[j, o]) for o in range(nout)))
                j += 1
                n += 1
    return worst, n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--name", required=True)
    ap.add_argument("--quick", action="store_true")
    ap.add_argument("--smooth", type=int, default=1)
    a = ap.parse_args()
    mdir = os.path.join(DATA, "models")
    model = M.load(os.path.join(mdir, a.name))
    cfg = model.cfg
    nout = cfg["nout"]
    names = TR.NAMES[:nout]
    rf = M.receptive_field(cfg)
    rep = {"name": a.name, "cfg": cfg, "outputs": names, "smooth": a.smooth}

    # 1. export
    Xp, _ = TR.load_set("train_pos")
    Xn, _ = TR.load_set("train_neg")
    rng = np.random.default_rng(0)
    corp = TR.Corpus("train")
    cal = np.concatenate([Xp[rng.integers(0, len(Xp), 900)], Xn[rng.integers(0, len(Xn), 500)], corp.sample(rng, 600)])
    Q = M.quantize(M.float_params(model), M.calibrate(model, cal))
    ewm = os.path.join(mdir, a.name + ".ewm")
    rep["model_bytes"] = M.write_ewm(Q, ewm, names=names, thr=[0.5] * nout, smooth=a.smooth)
    shutil.copy(ewm, os.path.join(WAKE, "models", a.name + ".ewm"))
    del Xp, Xn

    # 2. C parity
    Vp, VLp = TR.load_set("test_pos")
    pick = np.concatenate([np.nonzero(VLp[:, 0] == k)[0][:3] for k in (1, 2, 5, 6, 7, 8)])
    worst, n = c_parity(Q, ewm, Vp[pick], nout)
    rep["c_parity_max_logit_diff"] = worst
    print(f"model {rep['model_bytes']} B; C vs numpy int8: {worst:.2e} over {n} steps", flush=True)

    # 3. recall per call and condition; cross-fires
    Pq = EV.int8_probs(Q, Vp, k=a.smooth)
    rec = {}
    for thr in THRS:
        rec[thr] = {}
        for c, cn in enumerate(EV.COND):
            sel = VLp[:, 3] == c
            rec[thr][cn] = {nm: round(hit_rate(Pq[sel], VLp[sel], o, thr), 4) for o, nm in enumerate(names)}
    rep["recall"] = rec
    mxp = Pq.max(1)
    cross = {}
    for thr in (0.7, 0.8, 0.9):
        cross[thr] = {}
        for o, nm in enumerate(names):
            if o < 2:
                other = VLp[:, 0] >= 5  # the new calls on a "gâu" output
            else:
                other = ~positives(o, VLp[:, 0])
            cross[thr][nm] = round(float((mxp[other, o] >= thr).mean()), 4)
    rep["cross_fires"] = cross
    del Vp

    # 4. confusables and sentences (held-out voices)
    Tn, TLn = TR.load_set("test_neg")
    tx = []
    for f in ("test_neg_texts.json", "test_neg2_texts.json"):
        p = os.path.join(DATA, "sets", f)
        if os.path.exists(p):
            tx += json.load(open(p))
    Pn = EV.int8_probs(Q, Tn, k=a.smooth).max(1)
    conf = {thr: {nm: round(float((Pn[TLn[:, 0] == 0, o] >= thr).mean()), 4) for o, nm in enumerate(names)}
            for thr in THRS}
    rep["confusables_per_clip"] = conf
    worst_t = {}
    if len(tx) == len(Tn):
        for o, nm in enumerate(names):
            w = {}
            for i in np.argsort(-Pn[:, o])[:300]:
                if Pn[i, o] >= 0.8:
                    w[tx[i]] = w.get(tx[i], 0) + 1
            worst_t[nm] = dict(sorted(w.items(), key=lambda kv: -kv[1])[:10])
    rep["confusables_firing_most_at_0.8"] = worst_t
    del Tn

    # 5. long audio
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
            dog = [i for i, l in enumerate(meta.get("labels", [])) if l == "dog"]
            if dog:
                Xd = np.stack([np.pad(fr[utt[i]:utt[i] + 500], [(0, max(0, 500 - len(fr[utt[i]:utt[i] + 500]))), (0, 0)],
                                      constant_values=-124)[:500] for i in dog])
                Pd = EV.int8_probs(Q, Xd, k=a.smooth).max(1)
                fa["esc50_dog_clips"] = {str(t): [int((Pd[:, o] >= t).sum()) for o in range(nout)] + [len(dog)]
                                         for t in THRS}
        ev, hours = EV.stream_events(Q, fr, THRS, cfg["stride"], rf, k=a.smooth)
        fa[name] = {"hours": round(hours, 2), "count": {str(t): v for t, v in ev.items()}}
        print(name, round(hours, 2), "h", {t: ev[t] for t in (0.8, 0.9)}, flush=True)
    tot = sum(v["hours"] for v in fa.values() if "hours" in v)
    fa["ALL"] = {"hours": round(tot, 2), "per_hour": {
        str(t): [round(sum(v["count"][str(t)][o] for v in fa.values() if "count" in v) / max(tot, 1e-9), 3)
                 for o in range(nout)] for t in THRS}}
    rep["false_accepts"] = fa

    # 6. thresholds per sensitivity for the new calls
    thr_out = {}
    for o in range(2, nout):
        nm = names[o]
        row = []
        for sens in ("low", "normal", "high"):
            ok = [t for t in THRS if fa["ALL"]["per_hour"][str(t)][o] <= BUDGET[sens]]
            row.append(min(ok) if ok else 2.0)
        thr_out[nm] = row
    rep["thresholds"] = thr_out
    os.makedirs(os.path.join(WAKE, "reports"), exist_ok=True)
    json.dump(rep, open(os.path.join(WAKE, "reports", a.name + ".json"), "w"), ensure_ascii=False, indent=1)
    write_md(rep, os.path.join(WAKE, "reports", a.name + ".md"))
    print("thresholds", thr_out, flush=True)


def write_md(rep, path):
    names = rep["outputs"]
    L = [f"# Wake model {rep['name']} (calls: " + ", ".join(LABEL[n] for n in names) + ")", "",
         f"- config: {rep['cfg']}", f"- model file {rep['model_bytes']} B",
         f"- C runtime vs numpy int8: max logit difference {rep['c_parity_max_logit_diff']:.2e}", "",
         "## Recall on held-out voices (int8)", "",
         "| thr | condition | " + " | ".join(LABEL[n] for n in names) + " |", "|---|---|" + "---|" * len(names)]
    for t, row in rep["recall"].items():
        for cn, v in row.items():
            L.append(f"| {t} | {cn} | " + " | ".join(f"{v[n]:.3f}" for n in names) + " |")
    L += ["", "## A call firing another call's output (share of the other calls' clips)", "",
          "| thr | " + " | ".join(LABEL[n] for n in names) + " |", "|---|" + "---|" * len(names)]
    for t, v in rep["cross_fires"].items():
        L.append(f"| {t} | " + " | ".join(str(v[n]) for n in names) + " |")
    L += ["", "## False wakes per hour of held-out audio (" + str(rep["false_accepts"]["ALL"]["hours"]) + " h)", "",
          "| thr | " + " | ".join(LABEL[n] for n in names) + " |", "|---|" + "---|" * len(names)]
    for t, v in rep["false_accepts"]["ALL"]["per_hour"].items():
        L.append(f"| {t} | " + " | ".join(str(x) for x in v) + " |")
    if "esc50_dog_clips" in rep["false_accepts"]:
        d = rep["false_accepts"]["esc50_dog_clips"]
        L += ["", "Real dog barks (ESC-50 clips firing, per output, of " + str(d["0.8"][-1]) + "): " +
              "; ".join(f"thr {t}: {v[:-1]}" for t, v in d.items() if t in ("0.8", "0.9", "0.95"))]
    L += ["", "## Confusables and sentences, held-out voices (share of clips firing)", "",
          "| thr | " + " | ".join(LABEL[n] for n in names) + " |", "|---|" + "---|" * len(names)]
    for t, v in rep["confusables_per_clip"].items():
        L.append(f"| {t} | " + " | ".join(str(v[n]) for n in names) + " |")
    for n, w in rep.get("confusables_firing_most_at_0.8", {}).items():
        if w:
            L.append(f"- {LABEL[n]}: " + ", ".join(f"{k} ({c})" for k, c in w.items()))
    L += ["", "## Thresholds per sensitivity (low, normal, high), budget " +
          ", ".join(f"{k} {v}/h" for k, v in BUDGET.items()), ""]
    for n, v in rep["thresholds"].items():
        L.append(f"- {LABEL[n]}: {v}")
    open(path, "w").write("\n".join(L) + "\n")


if __name__ == "__main__":
    main()
