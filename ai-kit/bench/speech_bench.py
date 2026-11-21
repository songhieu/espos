#!/usr/bin/env python3
"""Speech-to-text and text-to-speech timings (Python 3 standard library only).

    python3 speech_bench.py --gateway http://127.0.0.1:8080 --key-file key.txt \
        --stt-model ../models/stt/ggml-large-v3-turbo-q5_0.bin --vad ../models/stt/ggml-silero-v6.2.0.bin \
        [--wav-dir DIR_WITH_REAL_RECORDINGS] --label m1pro

1. The questions of fixtures/turns.json are read by the gateway's TTS (two voices) into 16 kHz WAVs: a stand-in for
   a person (cleaner than a real microphone, so the error rates are a best case).
2. Each WAV goes through a fresh bin/stt-server (own port) with the encoder window fitted to the clip (default) and
   with the full 30 s window: time per clip and the character error rate against the text.
3. Real recordings (--wav-dir, any *.wav) are timed too; their text is printed, there is no reference.
4. TTS: time to the first audio byte and real-time factor for a few replies, per TTS model of the gateway.
Results: results/speech-<label>.json and a table on stdout.
"""
import argparse, glob, io, json, os, re, statistics, subprocess, sys, time, unicodedata, urllib.request, uuid, wave

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = 9412


def http(method, url, body=None, headers=None, timeout=120):
    req = urllib.request.Request(url, data=body, method=method, headers=headers or {})
    t0 = time.time()
    r = urllib.request.urlopen(req, timeout=timeout)
    first = None
    chunks = []
    while True:
        b = r.read(4096)
        if not b:
            break
        if first is None:
            first = time.time() - t0
        chunks.append(b)
    return r.status, dict(r.headers), b"".join(chunks), first, time.time() - t0


def wav_bytes(pcm, rate=16000, pad_s=0.25):
    pad = b"\0\0" * int(rate * pad_s)
    buf = io.BytesIO()
    w = wave.open(buf, "wb")
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(rate)
    w.writeframes(pad + pcm + pad)
    w.close()
    return buf.getvalue()


def multipart(fields, file_bytes):
    b = uuid.uuid4().hex
    out = io.BytesIO()
    for k, v in fields.items():
        out.write(("--%s\r\nContent-Disposition: form-data; name=\"%s\"\r\n\r\n%s\r\n" % (b, k, v)).encode())
    out.write(("--%s\r\nContent-Disposition: form-data; name=\"file\"; filename=\"a.wav\"\r\nContent-Type: audio/wav\r\n\r\n" % b).encode())
    out.write(file_bytes)
    out.write(("\r\n--%s--\r\n" % b).encode())
    return out.getvalue(), "multipart/form-data; boundary=" + b


def norm(s):
    s = unicodedata.normalize("NFC", s.lower())
    s = re.sub(r"[^\w\s]", " ", s)
    return " ".join(s.split())


def cer(ref, hyp):
    a, b = norm(ref), norm(hyp)
    if not a:
        return 0.0 if not b else 1.0
    prev = list(range(len(b) + 1))
    for i, ca in enumerate(a, 1):
        cur = [i] + [0] * len(b)
        for j, cb in enumerate(b, 1):
            cur[j] = min(prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (ca != cb))
        prev = cur
    return prev[-1] / len(a)


def stt_server(binary, model, vad, audio_ctx):
    args = [binary, "--model", model, "--port", str(PORT), "--audio-ctx", str(audio_ctx)]
    if vad:
        args += ["--vad-model", vad]
    p = subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for _ in range(240):
        try:
            urllib.request.urlopen("http://127.0.0.1:%d/health" % PORT, timeout=2)
            return p
        except Exception:
            time.sleep(0.5)
    p.kill()
    sys.exit("stt-server did not start")


def transcribe(wav):
    body, ctype = multipart({"model": "whisper", "language": "vi", "response_format": "verbose_json"}, wav)
    st, h, out, _, total = http("POST", "http://127.0.0.1:%d/v1/audio/transcriptions" % PORT, body, {"Content-Type": ctype})
    j = json.loads(out)
    return j["text"], j["timings"]["process_ms"], j["duration"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gateway", default="http://127.0.0.1:8080")
    ap.add_argument("--key-file", required=True)
    ap.add_argument("--stt-bin", default=os.path.join(HERE, "..", "bin", "stt-server"))
    ap.add_argument("--stt-model", required=True)
    ap.add_argument("--vad", default="")
    ap.add_argument("--wav-dir", default="")
    ap.add_argument("--voices", default="mai-anh,hai-dang")
    ap.add_argument("--tts-models", default="vieneu-turbo")
    ap.add_argument("--ctx-list", default="-1,0", help="stt-server --audio-ctx values to compare (-1 fit, 0 full 30 s, N frames)")
    ap.add_argument("--no-tts", action="store_true")
    ap.add_argument("--label", default="run")
    a = ap.parse_args()
    key = open(a.key_file).read().strip()
    auth = {"Authorization": "Bearer " + key, "Content-Type": "application/json"}
    turns = [t["say"] for t in json.load(open(os.path.join(HERE, "fixtures", "turns.json")))]
    res = {"label": a.label, "when": time.strftime("%Y-%m-%d %H:%M"), "loadavg_start": os.getloadavg(), "stt": {}, "real": [], "tts": {}}

    # ---- 1. the questions as speech ----
    clips = []
    for vi, voice in enumerate(a.voices.split(",")):
        for i, text in enumerate(turns):
            if vi and i % 2:   # the second voice reads every other question
                continue
            body = json.dumps({"model": "vieneu-turbo", "voice": voice, "text": text, "format": "pcm", "sample_rate": 16000, "stream": False}).encode()
            st, h, pcm, _, _ = http("POST", a.gateway + "/v1/tts", body, auth)
            clips.append({"voice": voice, "text": text, "wav": wav_bytes(pcm), "seconds": round(len(pcm) / 32000 + 0.5, 2)})
    print("%d question clips, %.1f s of speech" % (len(clips), sum(c["seconds"] for c in clips)), file=sys.stderr)

    # ---- 2. speech to text, two encoder windows ----
    for ctx in [int(x) for x in a.ctx_list.split(",")]:
        name = {-1: "fitted", 0: "full30s"}.get(ctx, "ctx%d" % ctx)
        srv = stt_server(a.stt_bin, a.stt_model, a.vad, ctx)
        transcribe(clips[0]["wav"])   # warm
        rows = []
        for c in clips:
            text, ms, dur = transcribe(c["wav"])
            rows.append({"voice": c["voice"], "ref": c["text"], "hyp": text, "ms": ms, "audio_s": round(dur, 2), "cer": round(cer(c["text"], text), 3)})
        srv.terminate(); srv.wait()
        res["stt"][name] = {"rows": rows, "median_ms": statistics.median(r["ms"] for r in rows),
                            "p90_ms": sorted(r["ms"] for r in rows)[int(len(rows) * 0.9) - 1],
                            "mean_cer": round(statistics.mean(r["cer"] for r in rows), 3),
                            "exact": sum(r["cer"] == 0 for r in rows)}
        s = res["stt"][name]
        print("stt %-8s median %4d ms  p90 %4d ms  mean CER %.1f%%  exact %d/%d" % (name, s["median_ms"], s["p90_ms"], s["mean_cer"] * 100, s["exact"], len(rows)), file=sys.stderr)
        for r in rows:
            if r["cer"] > 0.05:
                print("    %.0f%%  %s  ->  %s" % (r["cer"] * 100, r["ref"], r["hyp"]), file=sys.stderr)

    # ---- 3. real recordings (no reference text) ----
    if a.wav_dir:
        srv = stt_server(a.stt_bin, a.stt_model, a.vad, -1)
        for f in sorted(glob.glob(os.path.join(a.wav_dir, "*.wav"))):
            text, ms, dur = transcribe(open(f, "rb").read())
            res["real"].append({"file": os.path.basename(f), "audio_s": round(dur, 1), "ms": ms, "text": text[:200]})
            print("real %-12s %5.1f s audio -> %4d ms: %s" % (os.path.basename(f), dur, ms, text[:90]), file=sys.stderr)
        srv.terminate(); srv.wait()

    # ---- 4. text to speech: first audio ----
    replies = ["Gâu! Bây giờ là hai giờ chiều.",
               "Ở TP Hồ Chí Minh hôm nay trời ít mây, ba mươi ba độ, chiều có thể mưa rào.",
               "Đã đặt báo thức sáu giờ ba mươi sáng mai nha. Ngủ ngon nhé chủ ơi!",
               "Báo lấy lúc bảy giờ sáng nay. Chính phủ thí điểm thu hồi tài sản liên quan tội phạm. Ở Nha Trang, một người đàn ông tử vong khi cứu người đuối nước. Giá vàng tăng nhẹ trong phiên sáng."]
    for model in ([] if a.no_tts else a.tts_models.split(",")):
        rows = []
        for text in replies * 2:
            body = json.dumps({"model": model, "text": text, "format": "pcm", "sample_rate": 16000, "stream": True}).encode()
            st, h, pcm, first, total = http("POST", a.gateway + "/v1/tts", body, auth)
            audio = len(pcm) / 32000
            rows.append({"chars": len(text), "first_ms": round(first * 1000), "total_ms": round(total * 1000), "audio_s": round(audio, 2), "rtf": round(total / audio, 3)})
        rows = rows[len(replies):]   # the second round (the first warms up)
        res["tts"][model] = {"rows": rows, "median_first_ms": statistics.median(r["first_ms"] for r in rows),
                             "median_rtf": statistics.median(r["rtf"] for r in rows)}
        print("tts %-13s first audio median %d ms, RTF %.2f  %s" % (model, res["tts"][model]["median_first_ms"], res["tts"][model]["median_rtf"],
              [(r["chars"], r["first_ms"]) for r in rows]), file=sys.stderr)

    path = os.path.join(HERE, "results", "speech-%s.json" % a.label)
    json.dump(res, open(path, "w"), ensure_ascii=False, indent=1)
    print("wrote", path, file=sys.stderr)


if __name__ == "__main__":
    main()
