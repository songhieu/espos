"""Audio augmentation for the wake-word data (float32 mono, 16 kHz, about -1..1).

Pools live in DATA/aug: rir.npz (MIT impulse response survey, CC-BY 4.0), noise_*.npy (CHiME-3 backgrounds, CC-BY 4.0,
int16), music.npy (FMA commercial-use tracks, CC-BY, int16), babble.npy (FLEURS speech, CC-BY 4.0, int16).
"""
import glob
import os

import numpy as np
from scipy.signal import fftconvolve, lfilter, resample_poly

SR = 16000
DATA = os.path.expanduser(os.environ.get("WAKE_DATA", "wake-data"))


class Pools:
    def __init__(self, root=os.path.join(DATA, "aug")):
        self.rirs = []
        p = os.path.join(root, "rir.npz")
        if os.path.exists(p):
            z = np.load(p)
            self.rirs = [z[k].astype(np.float32) for k in z.files]
        self.noise = {}
        for f in sorted(glob.glob(os.path.join(root, "noise_*.npy"))):
            self.noise[os.path.basename(f)[6:-4]] = np.load(f, mmap_mode="r")
        self.music = np.load(os.path.join(root, "music.npy"), mmap_mode="r") if os.path.exists(
            os.path.join(root, "music.npy")) else None
        self.babble = np.load(os.path.join(root, "babble.npy"), mmap_mode="r") if os.path.exists(
            os.path.join(root, "babble.npy")) else None

    def segment(self, rng, n, kind=None):
        """n samples of background: a noise environment, music, babble or synthetic noise."""
        kinds = [k for k in ("noise", "music", "babble") if (self.noise if k == "noise" else getattr(self, k)) is not None
                 and (k != "noise" or self.noise)]
        kinds.append("synth")
        kind = kind or rng.choice(kinds, p=_probs(kinds))
        if kind == "noise":
            src = self.noise[rng.choice(sorted(self.noise))]
        elif kind == "music":
            src = self.music
        elif kind == "babble":
            src = self.babble
        else:
            return synth_noise(rng, n), "synth"
        if len(src) <= n:
            reps = n // max(1, len(src)) + 1
            return np.tile(np.asarray(src, dtype=np.float32) / 32768.0, reps)[:n], kind
        a = int(rng.integers(0, len(src) - n))
        return np.asarray(src[a:a + n], dtype=np.float32) / 32768.0, kind


def _probs(kinds):
    w = {"noise": 0.35, "music": 0.3, "babble": 0.2, "synth": 0.15}
    p = np.array([w[k] for k in kinds])
    return p / p.sum()


def synth_noise(rng, n):
    """White, pink or brown noise, or a hum, normalised to RMS 0.1."""
    t = rng.integers(0, 4)
    w = rng.normal(size=n).astype(np.float32)
    if t == 1:  # pink-ish
        w = lfilter([0.049922035, -0.095993537, 0.050612699, -0.004408786], [1, -2.494956002, 2.017265875, -0.522189400], w)
    elif t == 2:  # brown
        w = np.cumsum(w)
        w = w - np.convolve(w, np.ones(400) / 400, mode="same")
    elif t == 3:  # mains hum + harmonics + a little hiss
        f = rng.choice([50.0, 60.0])
        tt = np.arange(n) / SR
        w = sum(np.sin(2 * np.pi * f * k * tt + rng.uniform(0, 6.28)) / k for k in range(1, 6)) + 0.05 * w
    w = np.asarray(w, dtype=np.float32)
    return w / (np.sqrt((w ** 2).mean()) + 1e-9) * 0.1


def rms(x):
    return float(np.sqrt((x.astype(np.float64) ** 2).mean() + 1e-12))


def reverb(rng, x, pools, wet=None):
    if not pools.rirs:
        return x
    h = pools.rirs[int(rng.integers(0, len(pools.rirs)))]
    k = int(np.argmax(np.abs(h)))
    h = h[max(0, k - 16):]
    h = h / (np.abs(h).max() + 1e-9)
    y = fftconvolve(x, h)[: len(x) + 0].astype(np.float32)
    wet = rng.uniform(0.3, 1.0) if wet is None else wet
    y = wet * y / (rms(y) + 1e-9) * rms(x) + (1 - wet) * x
    return y.astype(np.float32)


def biquad_shelf(rng, x):
    """A random tilt: phone-like band limit, a speaker/mic cavity bump, or a low/high cut."""
    from scipy.signal import butter, sosfilt
    c = rng.integers(0, 4)
    if c == 0:
        sos = butter(2, [rng.uniform(80, 300), rng.uniform(3500, 7500)], btype="band", fs=SR, output="sos")
    elif c == 1:
        sos = butter(1, rng.uniform(100, 400), btype="high", fs=SR, output="sos")
    elif c == 2:
        sos = butter(2, rng.uniform(2500, 7000), btype="low", fs=SR, output="sos")
    else:
        return x
    return sosfilt(sos, x).astype(np.float32)


def speed(x, factor):
    """Change speed and pitch together (resampling), factor > 1 = faster and higher."""
    if abs(factor - 1) < 0.01:
        return x
    up = 100
    down = int(round(100 * factor))
    return resample_poly(x, up, down).astype(np.float32)


def pitch_keep_time(x, semitones):
    """Pitch shift keeping the length roughly (resample + crude overlap-add time stretch)."""
    if abs(semitones) < 0.1:
        return x
    f = 2 ** (semitones / 12)
    y = speed(x, f)  # higher pitch, shorter
    # stretch back by f with a simple WSOLA-free overlap-add (30 ms grains, 50 % hop)
    g = 480
    hop_out = g // 2
    hop_in = hop_out / f
    win = np.hanning(g).astype(np.float32)
    n_out = int(len(y) * f)
    out = np.zeros(n_out + g, dtype=np.float32)
    norm = np.zeros(n_out + g, dtype=np.float32)
    i = 0
    while True:
        a = int(i * hop_in)
        o = i * hop_out
        if a + g > len(y) or o + g > len(out):
            break
        out[o:o + g] += y[a:a + g] * win
        norm[o:o + g] += win
        i += 1
    out = out[:n_out] / np.maximum(norm[:n_out], 1e-3)
    return out.astype(np.float32)


def to_pcm16(x, rng=None, peak_db=None):
    """Scale to a random level (peak_db: the speech peak in dBFS), clip like a real ADC, int16."""
    if peak_db is not None:
        x = x / (np.abs(x).max() + 1e-9) * 10 ** (peak_db / 20)
    return (np.clip(x, -1, 1) * 32767).astype(np.int16)


def degrade(rng, x, pools, p_reverb=0.5, p_noise=0.8, snr=(0, 25), p_eq=0.5):
    """The usual chain for a clip already placed in its context: reverb, EQ, background noise."""
    if rng.random() < p_reverb:
        x = reverb(rng, x, pools)
    if rng.random() < p_eq:
        x = biquad_shelf(rng, x)
    if rng.random() < p_noise:
        bg, _ = pools.segment(rng, len(x))
        s = rms(x)
        snr_db = rng.uniform(*snr)
        x = x + bg / (rms(bg) + 1e-9) * s * 10 ** (-snr_db / 20)
    return x.astype(np.float32)
