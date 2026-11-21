# EspOS AI kit

Run the EspOS dog assistant's AI on your own Mac, with nothing sent to a cloud service:

- **The model that answers**: Gemma 4 (E2B or E4B) on the GPU, through `bin/llm-server`, a small C++ server on the
  llama.cpp library with an OpenAI-style `POST /v1/chat/completions`.
- **Speech to text**: Whisper large-v3-turbo through `bin/stt-server` (C++ on whisper.cpp), OpenAI-style
  `POST /v1/audio/transcriptions`.
- **The voice**: VieNeu-TTS (Turbo, Nano) and Piper, `POST /v1/tts` and OpenAI-style `POST /v1/audio/speech`.

One Go gateway serves all three behind API keys, with queues, restarts and an optional Cloudflare Tunnel.
The EspOS server (`../server`) talks to it with `ESPOS_AI_*`, `ESPOS_STT_*` and `ESPOS_TTS_*`.

**Full guide:** [espos.dev/docs/ai/local](https://espos.dev/docs/ai/local/) (also in this repository:
`s3/site/src/content/docs/ai/local.mdx`). Vietnamese README: [README.vi.md](README.vi.md).
Measured on an M1 Pro: [BENCHMARK.md](BENCHMARK.md) (Vietnamese).

## Quick start

Needs a Mac with Apple Silicon (M1 or later), [Homebrew](https://brew.sh) and the Xcode Command Line Tools
(`xcode-select --install`). About 5 GB of free disk for the 16 GB set.

```bash
git clone https://github.com/songhieu/espos.git
cd espos/ai-kit
./install.sh        # looks at the Mac, suggests a set, asks, downloads, builds, writes config.yaml, tests
./run.sh            # the gateway on the port in config.yaml (8080)
```

`install.sh --yes` takes the suggestion without asking; `./install.sh --help` lists every flag.
The installer speaks Vietnamese; the choices are plain words (`e2b`, `turbo`, `piper`).

Then in the EspOS server's `.env` (put an API key from `config.yaml` in place of `<KEY>`):

```sh
ESPOS_AI_URL=http://127.0.0.1:8080/v1
ESPOS_AI_KEY=<KEY>
ESPOS_AI_MODEL=gemma-4-e2b
ESPOS_AI_PROMPT=short
ESPOS_AI_TIMEOUT=60
ESPOS_STT_URL=http://127.0.0.1:8080/v1
ESPOS_STT_KEY=<KEY>
ESPOS_STT_MODEL=whisper
ESPOS_STT_LANG=vi
ESPOS_TTS_API=espos
ESPOS_TTS_URL=http://127.0.0.1:8080/v1/tts
ESPOS_TTS_KEY=<KEY>
ESPOS_TTS_MODEL=vieneu-turbo
```

## Layout

| Folder | What |
|---|---|
| `gateway/` | Go: API keys, queues, worker supervision, Cloudflare Tunnel; `ai.go` = the LLM and STT routes |
| `llm/` | `llm-server.cpp` (Gemma on llama.cpp), `check_template.py` |
| `stt/` | `stt-server.cpp` (Whisper on whisper.cpp) |
| `worker/` | Python TTS worker (VieNeu-TTS, Piper) |
| `bench/` | benchmarks: raw model, speech, the dog assistant end to end through an EspOS server |
| `install.sh`, `run.sh`, `service.sh` | install, run, run as a login service (launchd) |

Not in git (made by `install.sh`): `config.yaml` (your keys), `models/`, `bin/`, `third_party/`, `build/`,
`worker/.venv/`, `logs/`.

## Licence

GNU Affero General Public License v3.0 ([LICENSE](LICENSE)). What `install.sh` downloads keeps its own licence:
see [THIRD_PARTY.md](THIRD_PARTY.md).
