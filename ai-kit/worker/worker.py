"""TTS worker: loads ONE engine and serves raw 16-bit mono PCM on 127.0.0.1.

Started and supervised by the Go gateway (one process = one request at a time).
It is never exposed directly: every request must carry the X-Worker-Token the
gateway generated at start-up.

    python worker.py --engine vieneu-turbo --port 9101 --threads 2
    python worker.py --engine piper --port 9201 --threads 1 \
        --voice vais1000=../models/piper/vi_VN-vais1000-medium.onnx
    python worker.py --prefetch          # download VieNeu models, then exit
"""
import argparse
import asyncio
import hmac
import logging
import os
import queue
import sys
import threading
import time
import unicodedata
from urllib.parse import quote

import numpy as np

log = logging.getLogger("worker")
TOKEN = os.environ.get("WORKER_TOKEN", "")
WARMUP_TEXT = "Xin chào."


def slug(s: str) -> str:
    """'Hải Đăng' -> 'hai-dang' (ASCII id that is easy to type on an ESP32)."""
    s = s.replace("đ", "d").replace("Đ", "D")
    s = unicodedata.normalize("NFKD", s).encode("ascii", "ignore").decode()
    return "-".join(s.lower().split())


def pcm16(x: np.ndarray) -> bytes:
    return (np.clip(np.asarray(x, dtype=np.float32), -1.0, 1.0) * 32767.0).astype("<i2").tobytes()


class Engine:
    """voices: list of {id, label}; resolve() maps id / alias / slug -> canonical id."""

    voices: list
    default_voice: str

    def _build_index(self, aliases=None):
        self._index = {}
        for v in self.voices:
            self._index[v["id"]] = v["id"]
            self._index[slug(v["id"])] = v["id"]
            v["slug"] = slug(v["id"])
        for alias, real in (aliases or {}).items():
            if real in self._index:
                self._index.setdefault(alias, real)
                self._index.setdefault(slug(alias), real)

    def resolve(self, voice):
        if not voice:
            return self.default_voice
        return self._index.get(voice) or self._index.get(slug(voice))

    def native_rate(self, voice) -> int:
        raise NotImplementedError

    def stream(self, text, voice):
        raise NotImplementedError


class VieneuEngine(Engine):
    def __init__(self, mode: str, threads: int):
        from vieneu import Vieneu
        self.tts = Vieneu(mode=mode, threads=threads)
        self.voices = [{"id": vid, "label": label} for label, vid in self.tts.list_preset_voices()]
        self.default_voice = self.tts._default_voice
        self._build_index(getattr(self.tts, "_voice_aliases", {}))

    def native_rate(self, voice):
        return int(self.tts.sample_rate)

    def stream(self, text, voice):
        for chunk in self.tts.infer_stream(text, voice=voice):
            if chunk is not None and len(chunk):
                yield np.asarray(chunk, dtype=np.float32)


def short_espeak_dir():
    """Piper's espeak-ng data folder, by a short path. espeak-ng ignores a data path longer than about 160
    characters and then fails with "phontab: No such file", which happens when the kit sits in a deep folder.
    A symlink in ~/.cache keeps the path short."""
    from piper.phonemize_espeak import ESPEAK_DATA_DIR
    real = str(ESPEAK_DATA_DIR)
    if len(real) < 150:
        return real
    link = os.path.join(os.path.expanduser("~"), ".cache", "espos-ai-kit", "espeak-ng-data")
    try:
        os.makedirs(os.path.dirname(link), exist_ok=True)
        if os.path.islink(link) and os.readlink(link) != real:
            os.remove(link)
        if not os.path.exists(link):
            os.symlink(real, link)
        return link
    except OSError as e:
        log.warning("espeak-ng data path is long (%d characters) and no short link could be made: %s", len(real), e)
        return real


class PiperEngine(Engine):
    def __init__(self, voices: dict, threads: int, default_voice: str = ""):
        import onnxruntime as ort
        from piper import PiperVoice
        self.models = {}
        espeak = short_espeak_dir()
        for vid, path in voices.items():
            v = PiperVoice.load(path, espeak_data_dir=espeak)
            if threads > 0:  # one small thread pool per request instead of grabbing every core
                so = ort.SessionOptions()
                so.intra_op_num_threads = threads
                so.inter_op_num_threads = 1
                v.session = ort.InferenceSession(str(path), sess_options=so,
                                                 providers=["CPUExecutionProvider"])
            self.models[vid] = v
        if not self.models:
            raise SystemExit("piper: no --voice given")
        self.voices = [{"id": vid, "label": f"Piper {vid} ({m.config.sample_rate} Hz)"}
                       for vid, m in self.models.items()]
        self.default_voice = default_voice if default_voice in self.models else next(iter(self.models))
        self._build_index()

    def native_rate(self, voice):
        return int(self.models[voice].config.sample_rate)

    def stream(self, text, voice):
        for ch in self.models[voice].synthesize(text):
            yield np.asarray(ch.audio_float_array, dtype=np.float32)


def build_engine(a) -> Engine:
    if a.engine == "vieneu-turbo":
        return VieneuEngine("v3turbo", a.threads)
    if a.engine == "vieneu-nano":
        return VieneuEngine("v3nano", a.threads)
    if a.engine == "piper":
        voices = {}
        for spec in a.voice:
            vid, _, path = spec.partition("=")
            if not path or not os.path.isfile(path):
                raise SystemExit(f"piper voice file not found: {spec!r}")
            voices[vid] = path
        return PiperEngine(voices, a.threads, a.default_voice)
    raise SystemExit(f"unknown engine {a.engine!r}")


def make_app(engine: Engine, engine_name: str):
    from fastapi import FastAPI, Request
    from fastapi.responses import JSONResponse, StreamingResponse
    import soxr

    app = FastAPI(docs_url=None, redoc_url=None, openapi_url=None)
    lock = threading.Lock()  # strictly one synthesis at a time per process

    def authorized(req: Request) -> bool:
        return bool(TOKEN) and hmac.compare_digest(req.headers.get("x-worker-token", ""), TOKEN)

    @app.get("/info")
    def info(req: Request):
        if not authorized(req):
            return JSONResponse({"error": "forbidden"}, 403)
        return {
            "engine": engine_name,
            "default_voice": engine.default_voice,
            "voices": [dict(v, sample_rate=engine.native_rate(v["id"])) for v in engine.voices],
        }

    @app.post("/synth")
    async def synth(req: Request):
        if not authorized(req):
            return JSONResponse({"error": "forbidden"}, 403)
        body = await req.json()
        text = (body.get("text") or "").strip()
        if not text:
            return JSONResponse({"error": "text is empty"}, 400)
        voice = engine.resolve(body.get("voice"))
        if voice is None:
            return JSONResponse({"error": f"unknown voice {body.get('voice')!r}",
                                 "voices": [v["id"] for v in engine.voices]}, 400)
        native = engine.native_rate(voice)
        out_sr = int(body.get("sample_rate") or native)
        quiet = bool(body.get("quiet"))  # keep-warm pings from the gateway
        if not 8000 <= out_sr <= 48000:
            return JSONResponse({"error": "sample_rate must be 8000..48000"}, 400)

        # Synthesis runs in its own thread and feeds a queue. If the client goes
        # away, the response generator is cancelled, sets `cancel`, and the
        # producer stops at the next chunk boundary and releases the lock.
        cancel = threading.Event()
        q: "queue.Queue" = queue.Queue()

        def produce():
            t0 = time.perf_counter()
            first = None
            n = 0
            try:
                with lock:
                    if cancel.is_set():
                        return
                    rs = None if out_sr == native else soxr.ResampleStream(native, out_sr, 1, dtype="float32", quality="HQ")
                    for x in engine.stream(text, voice):
                        if cancel.is_set():
                            log.info("voice=%s cancelled (client disconnected)", voice)
                            return
                        if rs is not None:
                            x = rs.resample_chunk(x)
                        if len(x):
                            if first is None:
                                first = time.perf_counter() - t0
                            n += len(x)
                            q.put(pcm16(x))
                    if rs is not None:
                        tail = rs.resample_chunk(np.zeros(0, dtype=np.float32), last=True)
                        if len(tail):
                            n += len(tail)
                            q.put(pcm16(tail))
                el = time.perf_counter() - t0
                if not quiet:
                    log.info("voice=%s chars=%d first=%.0fms total=%.2fs audio=%.2fs",
                             voice, len(text), (first or el) * 1000, el, n / out_sr)
            except Exception as e:
                log.exception("synthesis failed")
                q.put(e)
            finally:
                q.put(None)

        threading.Thread(target=produce, daemon=True).start()

        async def gen():
            try:
                while True:
                    item = await asyncio.to_thread(q.get)
                    if item is None:
                        return
                    if isinstance(item, Exception):
                        raise item
                    yield item
            finally:
                cancel.set()

        # HTTP headers are latin-1: send the ASCII slug, plus the real name percent-encoded.
        return StreamingResponse(gen(), media_type="audio/pcm", headers={
            "X-Sample-Rate": str(out_sr), "X-Voice": slug(voice),
            "X-Voice-Name": quote(voice), "X-Engine": engine_name})

    return app


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--engine", choices=["vieneu-turbo", "vieneu-nano", "piper"])
    ap.add_argument("--port", type=int)
    ap.add_argument("--threads", type=int, default=0)
    ap.add_argument("--voice", action="append", default=[], help="piper: id=path/to/model.onnx")
    ap.add_argument("--default-voice", default="")
    ap.add_argument("--prefetch", action="store_true", help="download VieNeu models and exit")
    a = ap.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(levelname)s %(message)s", stream=sys.stdout)
    noisy = ("httpx", "httpcore", "huggingface_hub", "urllib3")

    class Quiet(logging.Filter):  # handler-level, so libraries resetting logger levels can't undo it
        def filter(self, r):
            return r.levelno >= logging.WARNING or not r.name.startswith(noisy)

    for h in logging.getLogger().handlers:
        h.addFilter(Quiet())

    if a.prefetch:
        for mode in ("v3turbo", "v3nano"):
            print(f"prefetch {mode} ...", flush=True)
            VieneuEngine(mode, 1)
        print("prefetch done", flush=True)
        return
    if not a.engine or not a.port:
        ap.error("--engine and --port are required")
    if not TOKEN:
        raise SystemExit("WORKER_TOKEN is not set (the gateway sets it)")

    # Exit if the gateway dies without stopping us (kill -9, crash), so no
    # orphan keeps the port and the RAM.
    parent = os.getppid()

    def watch_parent():
        while True:
            time.sleep(1)
            if os.getppid() != parent:
                os._exit(0)

    threading.Thread(target=watch_parent, daemon=True).start()

    t = time.perf_counter()
    engine = build_engine(a)
    for v in ([engine.default_voice] if a.engine != "piper" else [v["id"] for v in engine.voices]):
        for _ in engine.stream(WARMUP_TEXT, v):
            pass
    log.info("%s ready in %.1fs, %d voices", a.engine, time.perf_counter() - t, len(engine.voices))

    import uvicorn
    uvicorn.run(make_app(engine, a.engine), host="127.0.0.1", port=a.port, log_level="warning")


if __name__ == "__main__":
    main()
