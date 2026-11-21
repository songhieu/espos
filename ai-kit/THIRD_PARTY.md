# Third-party software and models

The EspOS AI kit (this folder) is licensed under the GNU Affero General Public License v3.0 (LICENSE).
It contains no third-party code. `install.sh` downloads the following at pinned versions; each keeps its own licence.

## Downloaded and built from source

| What | Version | Licence | Used for |
|---|---|---|---|
| [llama.cpp](https://github.com/ggml-org/llama.cpp) (with ggml, cpp-httplib, nlohmann/json) | v1.0.0 | MIT | `bin/llm-server` |
| [whisper.cpp](https://github.com/ggml-org/whisper.cpp) (with ggml, cpp-httplib, nlohmann/json) | v1.9.5 | MIT | `bin/stt-server` |

`bin/llm-server` and `bin/stt-server` link these libraries statically. If you give the binaries to someone, include
the MIT licence texts from `third_party/llama.cpp/LICENSE` and `third_party/whisper.cpp/LICENSE`.

## Python packages (the TTS worker, `worker/pyproject.toml`, installed from PyPI by `uv`)

| Package | Version | Licence |
|---|---|---|
| [vieneu](https://github.com/pnnbao97/VieNeu-TTS) (VieNeu-TTS) | 3.8.4 | Apache-2.0 |
| [piper-tts](https://github.com/OHF-Voice/piper1-gpl) | 1.8.0 | GPL-3.0-or-later |
| fastapi, uvicorn, soxr, numpy and their dependencies | see `worker/uv.lock` | their own (MIT, BSD-3-Clause; soxr: LGPL-2.1-or-later) |

The worker runs piper-tts in its own process. Piper is GPL-3.0: if you bundle the worker with Piper in a product you
sell, read its licence first, or turn Piper off (`--tts turbo` without `piper`).

## Models (downloaded from Hugging Face; nothing is gated)

| Model | Licence | Used for |
|---|---|---|
| [Gemma 4 E2B / E4B, QAT 4-bit GGUF by unsloth](https://huggingface.co/unsloth/gemma-4-E2B-it-qat-GGUF) (from [google/gemma-4-E2B-it](https://huggingface.co/google/gemma-4-E2B-it)) | Apache-2.0 | the model that answers |
| [Whisper large-v3-turbo / small, ggml](https://huggingface.co/ggerganov/whisper.cpp) (OpenAI Whisper) | MIT | speech to text |
| [Silero VAD v6.2.0, ggml](https://huggingface.co/ggml-org/whisper-vad) | MIT | cuts silence before Whisper |
| [VieNeu-TTS v3 Turbo / Nano](https://huggingface.co/pnnbao-ump/VieNeu-TTS-v3-Turbo) and [MOSS Audio Tokenizer Nano](https://huggingface.co/OpenMOSS-Team/MOSS-Audio-Tokenizer-Nano-ONNX) | Apache-2.0 | the voice |
| [Piper voice vi_VN vais1000 medium](https://huggingface.co/rhasspy/piper-voices/tree/main/vi/vi_VN/vais1000/medium) | MIT (voices repository); dataset VAIS-1000 CC BY 4.0 | the fast voice |

VieNeu-TTS adds an inaudible audio watermark to the voices it makes (the library's default).

## Not included

The bench fixture `bench/fixtures/news.json` is made-up sample news. `bench/make_news_fixture.py` fetches real
VnExpress headlines into `bench/fixtures/news.local.json` on your machine only (git-ignored).
