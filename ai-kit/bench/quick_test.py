#!/usr/bin/env python3
"""Tự kiểm tra nhanh sau khi cài (install.sh gọi): mỗi model đang bật trả lời được không, và nhanh cỡ nào.

    KEY=<api key> python3 bench/quick_test.py --gateway http://127.0.0.1:8080
"""
import argparse, io, json, os, sys, time, urllib.request, uuid, wave


def call(url, body=None, headers=None, raw=False, timeout=300):
    data = body if isinstance(body, (bytes, type(None))) else json.dumps(body).encode()
    req = urllib.request.Request(url, data=data, headers=headers or {})
    t0 = time.time()
    r = urllib.request.urlopen(req, timeout=timeout)
    first = None
    out = b""
    while True:
        b = r.read(4096)
        if not b:
            break
        if first is None:
            first = time.time() - t0
        out += b
    return out if raw else json.loads(out), first or 0, time.time() - t0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gateway", default="http://127.0.0.1:8080")
    a = ap.parse_args()
    key = os.environ.get("KEY", "")
    h = {"Authorization": "Bearer " + key, "Content-Type": "application/json"}
    g = a.gateway.rstrip("/")
    health, _, _ = call(g + "/health")
    kinds = {}
    for mid, m in health["models"].items():
        kinds.setdefault(m["kind"], []).append(mid)
    print("    model đang chạy: " + ", ".join("%s (%s)" % (m, k) for k, ms in kinds.items() for m in ms))
    fails = 0
    pcm_question = None

    if "tts" in kinds:
        tts = kinds["tts"][0]
        _, first, total = call(g + "/v1/tts", {"model": tts, "text": "Xin chào, tôi là trợ lý ở nhà của bạn.", "format": "pcm", "sample_rate": 16000}, h, raw=True)
        print("    giọng đọc  %-14s có tiếng sau %4d ms" % (tts, first * 1000))
        pcm_question, _, _ = call(g + "/v1/tts", {"model": tts, "text": "Đặt báo thức sáu giờ sáng mai.", "format": "pcm", "sample_rate": 16000, "stream": False}, h, raw=True)

    if "stt" in kinds:
        stt = kinds["stt"][0]
        pcm = pcm_question or b"\0\0" * 16000
        buf = io.BytesIO()
        w = wave.open(buf, "wb"); w.setnchannels(1); w.setsampwidth(2); w.setframerate(16000); w.writeframes(pcm); w.close()
        b = uuid.uuid4().hex
        body = ("--%s\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\n%s\r\n--%s\r\nContent-Disposition: form-data; name=\"file\"; filename=\"a.wav\"\r\n\r\n" % (b, stt, b)).encode() + buf.getvalue() + ("\r\n--%s--\r\n" % b).encode()
        out, _, total = call(g + "/v1/audio/transcriptions", body, {"Authorization": "Bearer " + key, "Content-Type": "multipart/form-data; boundary=" + b})
        print("    nghe giọng %-14s %4d ms: \"%s\"" % (stt, total * 1000, out.get("text", "")))

    if "llm" in kinds:
        llm = kinds["llm"][0]
        sysmsg = "Bạn là chú chó trợ lý tên Boop. Trả về JSON {\"reply\": \"...\", \"actions\": []}. Đặt báo thức: {\"do\": \"alarm_add\", \"h\": 6, \"m\": 0}."
        for i, q in enumerate(["Đặt báo thức sáu giờ sáng mai.", "Chào Boop, hôm nay vui không?"]):
            out, _, total = call(g + "/v1/chat/completions", {"model": llm, "max_tokens": 120, "response_format": {"type": "json_object"},
                                                               "messages": [{"role": "system", "content": sysmsg}, {"role": "user", "content": q}]}, h)
            text = out["choices"][0]["message"]["content"]
            try:
                json.loads(text)
                ok = "JSON đúng"
            except Exception:
                ok = "JSON SAI"
                fails += 1
            t = out.get("timings", {})
            print("    trả lời    %-14s %4d ms (%s, %s tok/s): %s" % (llm, total * 1000, ok, t.get("predicted_per_second", "?"), text[:90].replace("\n", " ")))
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
