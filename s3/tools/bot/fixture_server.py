#!/usr/bin/env python3
"""A stand-in espos.dev for the EspOS Bot simulator (tools/bot/sim.sh): the bot's routes with the shapes the real
server answers (web/scripts/test-bot.py, bot-keys.go.txt), no AI and no network. The dog's replies are canned; its
voice is this Mac's own `say` (Vietnamese voice "Linh" when installed), at 16 kHz.

  python3 tools/bot/fixture_server.py [--port 8098]

Routes: POST /v1/pair/start, GET /v1/device/whoami, POST /v1/device/sync, GET /v1/device/ping,
POST /v1/device/assist (?voice=0 | ?stream=1).
Knobs for tests (the "web" side):
  POST /_fixture/set      {"k": "bot.style", "v": "pixel"}   a setting changed on the web (the ping says so)
  POST /_fixture/reply    {"reply": "...", "mood": "happy", "asks": false}   the next answer
  POST /_fixture/claim    the pending claim is entered on the web (a token "c1"*32 waits for it)
  POST /_fixture/pair     {"code": "123456"} (or {} for the latest) the pairing code is entered on the web
  POST /_fixture/lic      {"ai_left": 0}                      the trial's turns left (0: the next turn gets 402)
  GET  /_fixture/state    what the server holds
"""
import argparse
import base64
import hashlib
import io
import json
import os
import shutil
import subprocess
import tempfile
import threading
import time
import wave
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

DEFAULTS = {
    "bot.style": "eyes",
    "bot.screens": '[{"id":"clock_bold","s":20},{"id":"date_page","s":8},{"id":"weather","s":8},{"id":"eyes","s":20},{"id":"aquarium","s":15}]',
    "bot.bright": "60", "bot.flip": "0", "bot.night": "22:00-07:00", "bot.dog": "", "bot.wake": "2", "bot.follow": "1", "bot.voice": "1",
    "bot.volume": "60", "bot.alarms": "", "bot.ota": "stable", "sys.lang": "vi",
    "dash.city": "Hà Nội", "dash.lat": "21.03", "dash.lon": "105.85", "sys.tz_min": "420",
}
REPLIES = [
    ("Chào bạn! Mình là chó con giả lập đây. Hôm nay bạn thấy thế nào?", "happy", True),
    ("Bây giờ là buổi tối rồi đó. Nhớ uống nước nha!", "happy", False),
    ("Hà Nội hôm nay có mây rải rác, hai mươi tám độ. Chiều có thể mưa rào nhẹ.", "stand", False),
    ("Mình chưa biết điều đó, nhưng mình sẽ học thêm nha.", "sad", False),
]
lock = threading.Lock()
S = {"settings": dict(DEFAULTS), "rev": int(time.time() * 1000), "claimed": False, "ai_left": 50, "turn": 0,
     "next": None, "log": [], "pairs": {}, "paired": set()}


def now_ms():
    return int(time.time() * 1000)


VOICES = None


def say_pcm(text):
    """16 kHz 16-bit mono PCM of text from macOS `say` (None when it is not there)."""
    if not shutil.which("say") or not shutil.which("afconvert"):
        return None
    global VOICES
    if VOICES is None:  # asked once (it takes a while)
        VOICES = subprocess.run(["say", "-v", "?"], capture_output=True, text=True).stdout
    with tempfile.TemporaryDirectory() as d:
        aiff, wav = os.path.join(d, "a.aiff"), os.path.join(d, "a.wav")
        cmd = ["say", "-o", aiff] + (["-v", "Linh"] if "Linh" in VOICES else []) + [text]
        if subprocess.run(cmd, capture_output=True).returncode:
            return None
        if subprocess.run(["afconvert", "-f", "WAVE", "-d", "LEI16@16000", "-c", "1", aiff, wav], capture_output=True).returncode:
            return None
        with wave.open(wav) as w:
            return w.readframes(w.getnframes())


def loud(wav_bytes):
    """The question's loudest 10 ms (dBFS), to tell silence from speech."""
    try:
        with wave.open(io.BytesIO(wav_bytes)) as w:
            pcm = w.readframes(w.getnframes())
    except Exception:
        return -96
    import array
    import math
    a = array.array("h", pcm)
    best = 0
    for i in range(0, len(a) - 160, 160):
        p = sum(x * x for x in a[i:i + 160]) / 160
        best = max(best, p)
    return 10 * math.log10(best + 1e-9) - 90.309


class H(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        pass

    def body(self):
        n = int(self.headers.get("Content-Length") or 0)
        return self.rfile.read(n) if n else b""

    def send_json(self, code, obj, extra=None):
        data = json.dumps(obj, ensure_ascii=False).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(data)

    def token(self):
        a = self.headers.get("Authorization", "")
        return a[7:] if a.startswith("Device ") else ""

    def linked(self):
        t = self.token()
        if len(t) != 64:
            return "unauthorized"
        if t == "c1" * 32 and not S["claimed"]:
            return "not_linked"
        h = hashlib.sha256(t.encode()).hexdigest()
        if any(p["hash"] == h for p in S["pairs"].values()) and h not in S["paired"]:
            return "not_linked"  # a pairing token, its code not entered yet
        return ""

    def do_GET(self):
        u = urlparse(self.path)
        if u.path == "/v1/device/ping":
            err = self.linked()
            if err:
                return self.send_json(401, {"error": err})
            with lock:
                return self.send_json(200, {"rev": S["rev"]})
        if u.path == "/_fixture/state":
            with lock:
                st = {k: v for k, v in S.items() if k not in ("log", "paired")}
                st.update({"log": S["log"][-20:], "paired": len(S["paired"])})
                return self.send_json(200, st)
        if u.path == "/v1/device/whoami":
            err = self.linked()
            if err:
                return self.send_json(401, {"error": err})
            return self.send_json(200, {"device": {"id": 70, "name": "Bot (giả lập)", "user": "sim@example.com"}})
        self.send_json(404, {"error": "not_found"})

    def do_POST(self):
        u = urlparse(self.path)
        q = {k: v[0] for k, v in parse_qs(u.query).items()}
        raw = self.body()
        if u.path.startswith("/_fixture/"):
            d = json.loads(raw or b"{}")
            with lock:
                if u.path == "/_fixture/set":
                    S["settings"][d["k"]] = str(d["v"])
                    S["rev"] = now_ms()
                elif u.path == "/_fixture/reply":
                    S["next"] = (d.get("reply", ""), d.get("mood", "stand"), bool(d.get("asks")))
                elif u.path == "/_fixture/claim":
                    S["claimed"] = True
                    S["rev"] = now_ms()
                elif u.path == "/_fixture/pair":
                    c = d.get("code") or (max(S["pairs"], key=lambda k: S["pairs"][k]["at"]) if S["pairs"] else "")
                    if c in S["pairs"]:
                        S["paired"].add(S["pairs"][c]["hash"])
                        S["log"].append(f"pair: code {c} entered")
                elif u.path == "/_fixture/lic":
                    S["ai_left"] = int(d.get("ai_left", S["ai_left"]))
                    S["rev"] = now_ms()
            return self.send_json(200, {"ok": True})
        if u.path == "/v1/pair/start":
            d = json.loads(raw or b"{}")
            code, h = str(d.get("code", "")), str(d.get("token_hash", ""))
            if len(code) != 6 or not code.isdigit() or len(h) != 64:
                return self.send_json(422, {"error": "invalid"})
            with lock:
                S["pairs"][code] = {"hash": h, "kind": d.get("kind"), "at": time.time()}
                S["log"].append(f"pair/start {code} kind {d.get('kind')}")
            return self.send_json(200, {"ok": True, "expires_s": 600})
        err = self.linked()
        if err:
            return self.send_json(401, {"error": err})
        if u.path == "/v1/device/sync":
            d = json.loads(raw or b"{}")
            with lock:
                for kv in d.get("settings", []):
                    if kv.get("k") in DEFAULTS:
                        S["settings"][kv["k"]] = str(kv.get("v", ""))
                        S["rev"] = max(S["rev"], now_ms())
                S["log"].append(f"sync fw {d.get('fw')} board {d.get('diag', {}).get('board')}")
                lic = {"plan": "trial", "ai": "trial" if S["ai_left"] > 0 else "off", "ai_left": S["ai_left"],
                       "trial_end_ms": now_ms() + 7 * 86400000, "ent": [], "buy_url": "https://espos.dev/app#buy"}
                return self.send_json(200, {
                    "now_ms": now_ms(), "tz_min": int(S["settings"]["sys.tz_min"]),
                    "device": {"id": 70, "name": "Bot (giả lập)", "user": "sim"},
                    "settings_rev": S["rev"], "settings": S["settings"],
                    "weather": {"city": S["settings"]["dash.city"], "t": 28, "icon": "cloudsun", "desc": "Mây rải rác",
                                "hi": 31, "lo": 24, "rain": 40, "hum": 78, "at": now_ms()},
                    "dashboard": {"lunar": "12/10", "lunar_year": "Bính Ngọ", "weekday": "Thứ Sáu", "date": "20.11.2026",
                                  "gold": {"src": "sjc", "at": now_ms(), "items": [{"c": "SJC", "n": "Vàng SJC", "b": 148.5, "s": 150.5}]},
                                  "fx": {"at": now_ms(), "items": [{"c": "USD", "b": 26100, "s": 26350}]},
                                  "news": {"top": ["Hà Nội: chiều nay có mưa rào rải rác, nhiệt độ giảm còn 22 độ vào ban đêm",
                                                   "Giá vàng SJC tăng nhẹ đầu phiên sáng nay"]}},
                    "lic": lic})
        if u.path == "/v1/device/assist":
            with lock:
                if S["ai_left"] <= 0:
                    return self.send_json(402, {"locked": "trial_ended", "buy_url": "https://espos.dev/app#buy"})
                level = loud(raw)
                if level < -45 and q.get("wake") == "1":
                    S["log"].append(f"assist: silent ({level:.0f} dBFS)")
                    return self.send_json(200, {"silent": True})
                if S["next"]:
                    reply, mood, asks = S["next"]
                    S["next"] = None
                else:
                    reply, mood, asks = REPLIES[S["turn"] % len(REPLIES)]
                S["turn"] += 1
                S["ai_left"] -= 1
                cid = q.get("cid") or f"sim{S['turn']:04d}"
                ans = {"heard": f"(giả lập) câu hỏi {S['turn']}", "reply": reply, "mood": mood, "asks": asks, "cid": cid,
                       "done": [], "saved": False, "bot_short": True}
                S["log"].append(f"assist #{S['turn']}: {level:.0f} dBFS, voice {q.get('voice', '1')}, stream {q.get('stream', '0')}")
            if q.get("voice") != "0" and q.get("stream") == "1":
                pcm = say_pcm(reply)
                if pcm is not None:
                    self.send_response(200)
                    self.send_header("Content-Type", "audio/pcm")
                    self.send_header("X-Sample-Rate", "16000")
                    self.send_header("X-Assist", base64.b64encode(json.dumps(ans, ensure_ascii=False).encode()).decode())
                    self.send_header("Content-Length", str(len(pcm)))
                    self.end_headers()
                    for i in range(0, len(pcm), 3200):  # about as fast as a TTS streams it
                        self.wfile.write(pcm[i:i + 3200])
                        self.wfile.flush()
                        time.sleep(0.02)
                    return
            return self.send_json(200, ans)
        self.send_json(404, {"error": "not_found"})


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8098)
    a = ap.parse_args()
    srv = ThreadingHTTPServer(("127.0.0.1", a.port), H)
    threading.Thread(target=say_pcm, args=("Xin chào",), daemon=True).start()  # warms the Mac's voice up
    print(f"fixture server on http://127.0.0.1:{a.port}", flush=True)
    srv.serve_forever()


if __name__ == "__main__":
    main()
