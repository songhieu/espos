# "Gâu" wake word (EspOS S3)

Say "Gâu gâu" (or "Gâu") to call the dog assistant, usually straight into the request: "Gâu, mấy giờ rồi?". This
folder holds the keyword spotter's training: data, tools, evaluation reports, the package, and the device design
(`FIRMWARE.md`). The C code that runs on the board is in `../src/wake` and is the same file the tools use here.

```
../src/wake/wake_fe.c,h  front end: 16 kHz PCM -> 40 log-mel bands (int8) every 10 ms (the firmware's own file)
../src/wake/wake_rt.c,h  runtime: int8 streaming network, one step per 30 ms, two outputs ("gâu", "gâu gâu")
host/              Makefile: libwake.dylib (for the Python tools) and wakecli (run a .ewm on a WAV), from ../src/wake
pkg/               the package dev.espos.wake.gau (gau.ewm + wake.json thresholds); tools/espk.py builds it
tools/s3.py        an S3 board over USB: put the package, record its mic (WAKEREC), console commands
train/             data generation, fetching, set building, training (MLX), export + evaluation
models/            exported models (.ewm, tens of KB; not in this repository)
reports/           evaluation reports (<model>.md / .json)
FIRMWARE.md        device flow, settings, package, firmware changes with sizes, RAM and CPU
```

## 1. Why this approach

| option | flash | RAM | custom Vietnamese word | licence | verdict |
|---|---|---|---|---|---|
| ESP-SR WakeNet (wn9/wn10) | library + 290 KB-1 MB model partition | ~20-50 KB internal + PSRAM | only through Espressif's paid training service (weeks, thousands of recordings); no training tools are public | "Espressif MIT": closed binaries, Espressif chips only | no: we cannot train "gâu" ourselves, and the AFE/AEC extras cost CPU and flash we do not have |
| microWakeWord (ESPHome) | TFLite Micro + ops: ~100-150 KB | arena 20-50 KB | yes (open training) | Apache 2.0 | close, but TFLite Micro alone would eat most of the ~125 KB left in the app image, and its trainer needs TensorFlow (GBs on a 95 % full disk) |
| openWakeWord | needs a 1.3 M-parameter embedding model | MBs | yes | Apache 2.0 | too big for the MCU |
| **own small streaming KWS** (chosen) | **3.8 KB of code** (measured) + a ~38 KB model in a package | **~14 KB state in PSRAM, <1 KB internal** | yes, trained here | ours (same as EspOS) | the runtime is two plain C files; the model ships as `dev.espos.wake.gau` and can be replaced or joined by other words without a firmware change |

The network follows the design that works for microWakeWord and Google's streaming KWS: a strided stem over the
log-mel frames, then depthwise-separable 1-D convolutions with growing dilation, all causal, so the board runs it
one 30 ms step at a time with tiny ring buffers. Weights are int8 per channel, activations uint8; each layer
rescales with one float multiply per channel (the S3 has an FPU), which keeps the C code short and makes the board's
numbers identical to the trainer's int8 model (checked: max logit difference 0 on test clips).

Two outputs: `gau` (fires after any "gâu") and `gau2` (fires only after "gâu gâu" / "gâu gâu gâu"). Vietnamese is
monosyllabic, so one "gâu" is easy to hit by accident (câu, cao, đâu, sau, ngâu, gấu...): the default is `gau2`,
and `gau` is offered as the "more sensitive" setting with a stricter threshold.

## 2. Data

All generated or fetched into a local data folder (`$WAKE_DATA`, default `wake-data`; not in the repo, ~2 GB):

| part | source | licence | use |
|---|---|---|---|
| positives "gâu", "gâu gâu", "gâu gâu gâu" (15 spellings/prosodies) | 200 voices: VieNeu-TTS v3 Turbo (25 presets x 3 temperatures), v3 Nano (11), Piper vivos (65 speakers), vais1000, 25hours, macOS "Linh", VieNeu cloning 60 real speakers (one clip each from Speech-MASSIVE and FLEURS, CC-BY), 37 voices mixed from two training voices' speaker embeddings | generated locally with the same models as tts.espos.dev (no API key, the live service is not touched) | split **by voice**: 148 train, 19 val, 33 test (4 Turbo presets incl. a Central and a Southern accent, 2 Nano, Linh, 25hours, 10 vivos, 15 cloned people) |
| requests after the wake word | ~80 assistant requests in the same voice; real people's commands (Speech-MASSIVE vi) | CC-BY 4.0 | glued 0-0.45 s after the wake word |
| hard negatives | ~115 confusable syllables and phrases (câu, cao, đâu, sau, ngâu, dâu, trâu, gạo, "con gấu bông", "ở đâu vậy", other animal sounds, "Ok Google"...) in every voice | generated | train / test by voice |
| other tones alone ("Gấu!", "Gầu.") | generated | | **not trained** (an emphatic bark can sound like these), only measured |
| negative speech | FLEURS vi + en, FPT Open Speech, VLSP2020 (2 shards), GigaSpeech2-vi validation, Speech-MASSIVE vi | CC-BY 4.0 | training negatives (35 % also with room + noise) |
| music | FMA tracks cleared for commercial use | CC-BY | training negatives, background mix |
| real dog barks | Wikimedia Commons recordings | CC BY-SA 3.0/4.0 | training negatives |
| rooms, noise | MIT impulse response survey (270 IRs), CHiME-3 backgrounds (cafe, street, bus, pedestrian), FLEURS babble, synthetic noise and hum | CC-BY 4.0 | augmentation |
| held-out long audio | FLEURS vi/en test, Speech-MASSIVE test, GigaSpeech2-vi test, FMA (other shard), VIVOS test, AudioSet eval, ESC-50 | CC-BY; VIVOS, ESC-50 NC and AudioSet (YouTube) **eval only** | false accepts per hour |

Piper's Vietnamese voices drop the tones (their phoneme map has no tone marks), so their "con gấu bông" says "con
gâu bông": such Piper clips are left out instead of being used as negatives.

Augmentation per positive (8 variants, 4 for the 64 low-quality vivos speakers; training samples every voice equally,
a vivos speaker at a third): speed 0.82-1.2, a child-like pitch shift (+2.5..+6 semitones) for
15 %, speech before it (another person or the same voice), the request after it, room reverb (55 %), EQ / band
limits (50 %), background noise, music or babble at -3..30 dB SNR (85 %), level -36..-2 dBFS peak.

## 3. Reproduce

```
cd s3/wake
make -C host                                                   # libwake.dylib + wakecli
uv venv --python 3.12 .venv && VIRTUAL_ENV=$PWD/.venv uv pip install numpy scipy soundfile mlx pyarrow
PY=python3                                                   # with VieNeu-TTS and Piper installed
$PY train/gen_tts.py --engine turbo|nano|piper|say|clone|mix    # positives + hard negatives (hours)
.venv/bin/python train/fetch.py all barks                       # corpora -> int8 features (about an hour of downloads)
.venv/bin/python train/build_sets.py                            # composed 3 s scenes -> sets/*.npz
.venv/bin/python train/train.py --name m64c --steps 20000
.venv/bin/python train/mine.py --name m64c --thr 0.2            # hard negatives: 3 s windows where it fires
.venv/bin/python train/train.py --name m64e --steps 24000 --mined 0.12
.venv/bin/python train/evaluate.py --name m64e                  # -> models/m64e.ewm, reports/m64e.md
.venv/bin/python train/make_demo.py models/m64e.ewm             # held-out voices in a noisy room, through wakecli
host/wakecli models/m64.ewm some.wav                            # the board's code on a recording
```

### More calls: "ki ki", "lu lu", "cún ơi", "milu" (2026-11-21)

The same pipeline with a second round of texts (`texts.WAKE2`: kinds k2 l2 c2 m2, and `texts.CONF2`: laughs and cries
like "hi hi" and "hu hu", one-sound neighbours, and the "<name> ơi" pattern Vietnamese uses all day). The first
round's clips and sets are kept; the second round goes to `sets/*_pos2.npz` / `*_neg2.npz`, which train.py appends.
One model, six outputs: `gau`, `gau2`, `kiki`, `lulu`, `cun`, `milu`; each call is a negative for every other output.

```
$PY train/gen_tts.py --engine turbo|nano|say|piper|clone --words2
$PY train/gen_tts.py --engine mix --words2 --mix_voices 37     # the first round's 37 mixed voices
.venv/bin/python train/build_sets.py --words2 --variants 4
.venv/bin/python train/train.py --name c6a --nout 6 --init m64d --steps 16000 --mined 0.12
.venv/bin/python train/evaluate_calls.py --name c6a            # -> reports/c6a.md, thresholds per sensitivity
```

## 4. Results

The package carries a model fine-tuned from m64d for the device (not in this repository). Below: m64d, the
general model.

### m64d

Full report: `reports/m64d.md` (other runs: m64a, m64c, m64e, m64e_last, m96b_mid). 33 held-out voices nobody trained on,
18.1 h of held-out audio for false wakes; numbers are the board's int8 code (bit-exact with `host/wakecli`).

| sensitivity (threshold) | "gâu gâu" recall: clean / 10 dB SNR / far + 10 dB | false wakes per hour (all 18 h) |
|---|---|---|
| low, "Less often" (0.99) | 74 % / 68 % / 56 % | 0.055 (1 in 18 h) |
| normal (0.98, the default) | 79 % / 77 % / 65 % | 0.28 |
| high, "More often" (0.95) | 85 % / 82 % / 74 % | 0.66 |
| "gâu" alone too, normal (0.99 on it) | 57 % for a single "gâu" | + 0.055 |

- Real dog barks (ESC-50, 40 clips of 5 s): 1 of 40 fires at 0.98, 0 of 40 at 0.99.
- Confusables said alone or in phrases (câu, cao, đâu, ngâu, con gấu bông...), held-out voices: 0.5 % of clips fire
  at 0.98 for "gâu gâu", 1.3 % for "gâu".
- "gấu", "gầu" said alone (not trained either way, an emphatic bark can sound like them): 19 % fire. A known gap.
- Recall varies a lot by voice: 52-100 % per voice at 0.98 (Southern and Central accents and one bad Piper voice are
  the low end). All positives are TTS so far: the real test is people's voices on the board's mic (`tools/s3.py rec`),
  and the next model should add recordings of real people.
- Model 37.6 KB, state 3.5 KB, 32.6 k MAC per 30 ms step; Mac: 0.4 ms CPU per second of audio.

## 5. Attribution (training data that asks for it)

- FLEURS (Google, CC-BY 4.0); FPT Open Speech Data, VLSP2020 VinAI 100h, GigaSpeech2-vi, Speech-MASSIVE vi (CC-BY 4.0,
  via the doof-ferb mirrors on Hugging Face); Free Music Archive tracks cleared for commercial use (CC-BY); MIT
  acoustical reverberation scene statistics survey impulse responses (CC-BY 4.0); CHiME-3 background noise (CC-BY 4.0).
- Dog barks from Wikimedia Commons (CC BY-SA 3.0/4.0): "Dog barking.webm", "Barking of a dog.ogg", "Barking of a dog
  2.ogg", "Perro ladrando.ogg", "Barking dog in Rome.ogg", "Sound of dogs, birds and crickets in early morning.wav",
  "Paisaje sonoro de lluvia con ladrido de perro" (1-3), "De paseo con la manada (paisaje sonoro).wav", "Rottweiler
  Barking.oga", "A dog making noises and barking.flac", "This audio file describes a particular Dog close in a
  home.ogg", "Perros peleando.ogg", "Barking.ogg", "Seis perros ladrando.wav", "Bark Huayra.ogg" (list with
  licences in wake-data/barks/meta.json).
- Eval only, never trained on: VIVOS (CC BY-NC-SA 4.0), ESC-50 (CC BY-NC 3.0), AudioSet eval clips (YouTube).
