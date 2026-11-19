"""The keyword spotter (MLX, channels last) and its int8 export.

    stem   causal Conv1d(40 -> ch, k0 frames, stride) + BN + ReLU          (one step = stride frames = 30 ms)
    block  causal depthwise Conv1d(k, dilation d) + BN + ReLU, pointwise ch -> ch + BN, + input, ReLU
    head   pointwise ch -> hc + BN + ReLU, then a linear layer to nout logits per step

Every layer only looks back in time, so the board runs it one step at a time (rt/wake_rt.c) and gets the same
numbers the training saw on whole clips.
"""
import json
import struct
import zlib

import mlx.core as mx
import mlx.nn as nn
import numpy as np

IN_SCALE = 1.0 / 32.0  # feature q (int8) -> float input


class KWS(nn.Module):
    def __init__(self, ch=64, k0=5, stride=3, k=5, dil=(1, 2, 4, 4), hc=32, nout=2):
        super().__init__()
        self.cfg = dict(ch=ch, k0=k0, stride=stride, k=k, dil=list(dil), hc=hc, nout=nout)
        self.stem = nn.Conv1d(40, ch, k0, stride=stride, bias=False)
        self.bn0 = nn.BatchNorm(ch)
        self.dw = [nn.Conv1d(ch, ch, k, dilation=d, groups=ch, bias=False) for d in dil]
        self.bnd = [nn.BatchNorm(ch) for _ in dil]
        self.pw = [nn.Conv1d(ch, ch, 1, bias=False) for _ in dil]
        self.bnp = [nn.BatchNorm(ch) for _ in dil]
        self.head = nn.Conv1d(ch, hc, 1, bias=False)
        self.bnh = nn.BatchNorm(hc)
        self.out = nn.Linear(hc, nout)

    def __call__(self, x, taps=None):
        """x: (N, T, 40) float -> (N, steps, nout) logits. taps: list to receive the activations (for calibration)."""
        c = self.cfg
        x = mx.pad(x, [(0, 0), (c["k0"] - 1, 0), (0, 0)])
        h = nn.relu(self.bn0(self.stem(x)))
        if taps is not None:
            taps.append(h)
        for i, d in enumerate(c["dil"]):
            z = mx.pad(h, [(0, 0), ((c["k"] - 1) * d, 0), (0, 0)])
            z = nn.relu(self.bnd[i](self.dw[i](z)))
            if taps is not None:
                taps.append(z)
            h = nn.relu(self.bnp[i](self.pw[i](z)) + h)
            if taps is not None:
                taps.append(h)
        y = nn.relu(self.bnh(self.head(h)))
        if taps is not None:
            taps.append(y)
        return self.out(y)


def receptive_field(cfg):
    return cfg["k0"] + cfg["stride"] * (cfg["k"] - 1) * sum(cfg["dil"])


# ---------- export ----------

def _fold(conv_w, bn):
    """conv weight (out, k, in) and BN -> folded weight, bias (float64)."""
    w = np.array(conv_w, dtype=np.float64)
    g = np.array(bn.weight, dtype=np.float64)
    b = np.array(bn.bias, dtype=np.float64)
    mu = np.array(bn.running_mean, dtype=np.float64)
    var = np.array(bn.running_var, dtype=np.float64)
    s = g / np.sqrt(var + bn.eps)
    return w * s[:, None, None], b - mu * s


def _q_weights(w):
    """Per output channel symmetric int8. w: (out, ...) -> int8, scale (out,)."""
    flat = w.reshape(w.shape[0], -1)
    mx_ = np.maximum(np.abs(flat).max(1), 1e-8)
    s = mx_ / 127.0
    q = np.clip(np.round(flat / s[:, None]), -127, 127).astype(np.int8)
    return q.reshape(w.shape), s


def float_params(model):
    """Folded float weights in the runtime's layout."""
    c = model.cfg
    P = {"cfg": c}
    w, b = _fold(model.stem.weight, model.bn0)          # (ch, k0, 40)
    P["stem"] = (w, b)
    P["blocks"] = []
    for i in range(len(c["dil"])):
        wd, bd = _fold(model.dw[i].weight, model.bnd[i])  # (ch, k, 1)
        wp, bp = _fold(model.pw[i].weight, model.bnp[i])  # (ch, 1, ch)
        P["blocks"].append((wd[:, :, 0], bd, wp[:, 0, :], bp))
    wh, bh = _fold(model.head.weight, model.bnh)         # (hc, 1, ch)
    P["head"] = (wh[:, 0, :], bh)
    P["out"] = (np.array(model.out.weight, dtype=np.float64), np.array(model.out.bias, dtype=np.float64))
    return P


def calibrate(model, X, pct=99.99):
    """Activation scales (uint8, zero point 0) from a calibration batch of features (N, T, 40) int8."""
    taps = []
    model.eval()
    model(mx.array(X.astype(np.float32) * IN_SCALE), taps)
    return [max(float(np.percentile(np.array(t), pct)), 1e-3) / 255.0 for t in taps]


def quantize(P, act_scales):
    """-> dict of int8 weights and float multipliers exactly as the runtime uses them."""
    c = P["cfg"]
    it = iter(act_scales)
    Q = {"cfg": c, "blocks": []}
    w, b = P["stem"]
    s_out = next(it)
    wq, ws = _q_weights(w)
    Q["stem"] = (wq.reshape(w.shape[0], -1), ws * IN_SCALE / s_out, b / s_out)  # (ch, k0*40) frame-major
    s_in = s_out
    for (wd, bd, wp, bp) in P["blocks"]:
        s_z = next(it)
        s_h = next(it)
        dq, ds = _q_weights(wd)
        pq, ps = _q_weights(wp)
        Q["blocks"].append((dq, ds * s_in / s_z, bd / s_z, pq, ps * s_z / s_h, bp / s_h, s_in / s_h))
        s_in = s_h
    wh, bh = P["head"]
    s_y = next(it)
    hq, hs = _q_weights(wh)
    Q["head"] = (hq, hs * s_in / s_y, bh / s_y)
    wo, bo = P["out"]
    oq, os_ = _q_weights(wo)
    Q["out"] = (oq, os_ * s_y, bo)
    return Q


def q8(y):
    """The runtime's ReLU + round + clamp, in float32 like the C code."""
    y = y.astype(np.float32)
    return np.where(y <= 0, 0, np.minimum(np.floor(y + np.float32(0.5)), 255)).astype(np.int32)


def run_int8(Q, X):
    """Bit-faithful numpy model of rt/wake_rt.c on whole clips. X: (N, T, 40) int8 -> logits (N, steps, nout)."""
    c = Q["cfg"]
    N, T, _ = X.shape
    k0, st = c["k0"], c["stride"]
    xp = np.concatenate([np.zeros((N, k0 - 1, 40), np.int32), X.astype(np.int32)], 1)
    steps = (T - 1) // st + 1
    win = np.stack([xp[:, j * st: j * st + k0, :].reshape(N, -1) for j in range(steps)], 1)  # (N, S, k0*40)
    wq, m, b = Q["stem"]
    acc = win @ wq.astype(np.int32).T
    h = q8(acc.astype(np.float32) * m.astype(np.float32) + b.astype(np.float32))
    for i, d in enumerate(c["dil"]):
        dq, dm, db, pq, pm, pb, r = Q["blocks"][i]
        k = c["k"]
        hp = np.concatenate([np.zeros((N, (k - 1) * d, h.shape[2]), np.int32), h], 1)
        acc = sum(hp[:, t * d: t * d + steps, :] * dq[:, t].astype(np.int32) for t in range(k))
        z = q8(acc.astype(np.float32) * dm.astype(np.float32) + db.astype(np.float32))
        acc = z @ pq.astype(np.int32).T
        h = q8(acc.astype(np.float32) * pm.astype(np.float32) + pb.astype(np.float32)
               + h.astype(np.float32) * np.float32(r))
    hq, hm, hb = Q["head"]
    y = q8((h @ hq.astype(np.int32).T).astype(np.float32) * hm.astype(np.float32) + hb.astype(np.float32))
    oq, om, ob = Q["out"]
    return (y @ oq.astype(np.int32).T).astype(np.float32) * om.astype(np.float32) + ob.astype(np.float32)


HDR_FMT = "<IHHfffBBBBBBBBHH8B4f48sII"


def write_ewm(Q, path, fe=(400.0, 19.0, 9.5), thr=(0.5, 0.5), names=("gau", "gau2"), smooth=3, refractory=50):
    c = Q["cfg"]
    body = bytearray()

    def pad():
        while len(body) % 4:
            body.append(0)

    def layer(wq, m, b):
        pad()
        body.extend(np.ascontiguousarray(wq, dtype=np.int8).tobytes())
        pad()
        body.extend(np.asarray(m, dtype="<f4").tobytes())
        body.extend(np.asarray(b, dtype="<f4").tobytes())

    layer(*Q["stem"])
    for dq, dm, db, pq, pm, pb, r in Q["blocks"]:
        layer(dq, dm, db)
        layer(pq, pm, pb)
        body.extend(struct.pack("<f", float(r)))
    layer(*Q["head"])
    layer(*Q["out"])
    pad()
    dil = list(c["dil"]) + [0] * (8 - len(c["dil"]))
    thr = list(thr) + [1.0] * (4 - len(thr))
    nm = b"".join(n.encode()[:11].ljust(12, b"\0") for n in names) + b"\0" * 12 * (4 - len(names))
    hsize = struct.calcsize(HDR_FMT)
    total = hsize + len(body)
    crc = zlib.crc32(bytes(body)) & 0xFFFFFFFF
    hdr = struct.pack(HDR_FMT, 0x314D5745, 1, hsize, fe[0], fe[1], fe[2], 40, c["k0"], c["stride"], len(c["dil"]),
                      c["k"], c["hc"], c["nout"], smooth, c["ch"], refractory, *dil, *thr, nm, total, crc)
    with open(path, "wb") as f:
        f.write(hdr)
        f.write(body)
    return total


def save(model, path):
    model.save_weights(path + ".safetensors")
    json.dump(model.cfg, open(path + ".json", "w"))


def load(path):
    cfg = json.load(open(path + ".json"))
    m = KWS(**cfg)
    m.load_weights(path + ".safetensors")
    m.eval()
    return m
