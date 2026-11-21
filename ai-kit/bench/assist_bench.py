#!/usr/bin/env python3
"""End-to-end benchmark of the EspOS dog assistant on this kit (Python 3 standard library only).

It drives a running EspOS server (../server in the EspOS repository) whose ESPOS_AI_* / ESPOS_STT_* / ESPOS_TTS_* point at
this kit's gateway, the way a person would: it signs in, creates a desk (kind s3) with a city and Vietnamese,
seeds the news cache from fixtures/news.json, then plays the turns of fixtures/turns.json one by one through
the web chat route (text) and, with --voice, through the device route (a WAV recording made by the kit's own TTS;
the answer comes back as streamed voice). After each turn it reads the server's tables (alarms, notes, reminders,
volume, the person's name, memories) and checks that the action asked for really happened.

    python3 assist_bench.py --espos http://127.0.0.1:8095 --password-file pw.txt --db DATA/espos.db \
        --gateway http://127.0.0.1:8090 --key-file key.txt --gateway-log gateway.log --label full --voice

Results: results/assist-<label>.json (every turn) and a summary on stdout.
"""
import argparse, base64, hashlib, io, json, os, re, secrets, sqlite3, statistics, sys, time, urllib.error, urllib.request, wave

HERE = os.path.dirname(os.path.abspath(__file__))


def http(method, url, body=None, headers=None, timeout=180, raw=False):
    """(status, headers, body bytes, seconds to first byte, seconds total)"""
    data = body if isinstance(body, (bytes, type(None))) else json.dumps(body).encode()
    h = {"Content-Type": "application/json"} if data is not None and not isinstance(body, bytes) else {}
    h.update(headers or {})
    req = urllib.request.Request(url, data=data, method=method, headers=h)
    t0 = time.time()
    try:
        resp = urllib.request.urlopen(req, timeout=timeout)
    except urllib.error.HTTPError as e:
        resp = e
    first = None
    chunks = []
    while True:
        b = resp.read(4096) if hasattr(resp, "read") else b""
        if not b:
            break
        if first is None:
            first = time.time() - t0
        chunks.append(b)
    total = time.time() - t0
    return resp.status if hasattr(resp, "status") else resp.code, dict(resp.headers), b"".join(chunks), first or total, total


class Espos:
    def __init__(self, base, user, password, db):
        self.base, self.db = base.rstrip("/"), db
        st, _, body, _, _ = http("POST", self.base + "/auth/login", {"email": user, "password": password})
        if st != 200:
            sys.exit("sign-in failed: %s %s" % (st, body[:200]))
        self.token = json.loads(body)["token"]

    def api(self, method, path, body=None):
        st, _, out, _, total = http(method, self.base + path, body, {"Authorization": "Bearer " + self.token})
        try:
            j = json.loads(out)
        except Exception:
            j = {"raw": out[:300].decode("utf-8", "replace")}
        return st, j, total

    # ---- the server's tables (one SQLite file, rows as JSON documents) ----
    def rows(self, table, **where):
        con = sqlite3.connect("file:%s?mode=ro" % self.db, uri=True, timeout=10)
        q = "SELECT id, data FROM docs WHERE tbl = ?"
        args = [table]
        for k, v in where.items():
            q += " AND json_extract(data, '$.%s') = ?" % k
            args.append(v)
        out = []
        for rid, data in con.execute(q, args):
            d = json.loads(data)
            d["id"] = rid
            out.append(d)
        con.close()
        return out

    def write_row(self, table, data):
        con = sqlite3.connect(self.db, timeout=10)
        now = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
        con.execute("INSERT INTO docs (tbl, created_at, updated_at, data) VALUES (?, ?, ?, ?)",
                    (table, now, now, json.dumps(data, ensure_ascii=False)))
        con.commit()
        con.close()

    def state(self, device_id):
        st = {r["skey"]: r.get("svalue", "") for r in self.rows("device_settings", device_id=device_id)}
        def js(k):
            try:
                v = json.loads(st.get(k) or "[]")
                return v if isinstance(v, list) else []
            except Exception:
                return []
        prof = self.rows("user_profiles", user_id=1)
        return {"alarms": js("s3.alarms"), "notes": js("s3.notes"), "todo": js("s3.todo"), "volume": st.get("s3.volume", ""),
                "name": (prof[0].get("call_name") if prof else "") or "",
                "memories": [m.get("fact", "") for m in self.rows("assist_memories", user_id=1)]}


# ---- checks: each takes (before, after, answer) and says (ok, why) ----

def alarm_at(state, h, m):
    return [a for a in state["alarms"] if int(a.get("h", -1)) == h and int(a.get("m", 0)) == m]

def has_words(text, *words):
    t = text.lower()
    return all(w.lower() in t for w in words)

CHECKS = {
    "any": lambda b, a, r: (bool(r.get("reply")), "replied"),
    "topic": None,  # handled from "topic" in the turn
    "hour_in_reply": lambda b, a, r: (any(re.search(r"\b%d\b" % h, r.get("reply", "")) for h in {time.localtime().tm_hour, time.localtime().tm_hour % 12 or 12}), "the hour"),
    "city_in_reply": lambda b, a, r: (has_words(r.get("reply", ""), "Hồ Chí Minh") or has_words(r.get("reply", ""), "Sài Gòn"), "names the city"),
    "news_long": lambda b, a, r: (len(r.get("reply", "").split()) >= 50, "a news summary (>= 50 words)"),
    "says_no": lambda b, a, r: (any(w in r.get("reply", "").lower() for w in ["chưa", "không có", "không thấy", "chẳng có"]), "says there is none"),
    "alarm_630_once": lambda b, a, r: (any(x.get("days", "") == "" for x in alarm_at(a, 6, 30)), "alarm 06:30 once"),
    "alarm_1900_weekdays": lambda b, a, r: (any(x.get("days", "") == "12345" for x in alarm_at(a, 19, 0)), "alarm 19:00 Mon-Fri"),
    "alarm_lists": lambda b, a, r: (("6" in r.get("reply", "")) and ("7" in r.get("reply", "") or "19" in r.get("reply", "")), "names both alarms"),
    "alarm_645_not_630": lambda b, a, r: (bool(alarm_at(a, 6, 45)) and not alarm_at(a, 6, 30), "06:30 moved to 06:45"),
    "alarm_1900_off": lambda b, a, r: (any(int(x.get("on", 1)) == 0 for x in alarm_at(a, 19, 0)), "19:00 switched off"),
    "alarm_645_gone": lambda b, a, r: (not alarm_at(a, 6, 45) and bool(alarm_at(a, 19, 0)), "06:45 deleted, 19:00 kept"),
    "note_added": lambda b, a, r: (len(a["notes"]) == len(b["notes"]) + 1 and any("sữa" in json.dumps(n, ensure_ascii=False) for n in a["notes"]), "a note with sữa"),
    "note_appended": lambda b, a, r: (len(a["notes"]) == len(b["notes"]) and any("cà phê" in json.dumps(n, ensure_ascii=False).lower() for n in a["notes"]), "cà phê added to the same note"),
    "todo_added": lambda b, a, r: (len(a["todo"]) == len(b["todo"]) + 1 and any("mẹ" in json.dumps(t, ensure_ascii=False) for t in a["todo"]), "a reminder about mẹ"),
    "todo_done": lambda b, a, r: (any("mẹ" in json.dumps(t, ensure_ascii=False) and (t.get("done") in (True, 1, "1")) for t in a["todo"]), "the reminder is done"),
    "volume_70": lambda b, a, r: (str(a["volume"]) == "70", "s3.volume = 70"),
    "name_nam": lambda b, a, r: (a["name"].strip().lower() in ("nam", "nam"), "call_name = Nam"),
    "remember_pho": lambda b, a, r: (any("phở" in m.lower() for m in a["memories"]), "a memory about phở"),
    "recall": lambda b, a, r: (has_words(r.get("reply", ""), "phở") or has_words(r.get("reply", ""), "nam"), "recalls phở or the name"),
    "no_math": lambda b, a, r: (not re.search(r"x\s*(=|bằng|là)\s*2\b", r.get("reply", "").lower()), "does not solve the homework"),
    "guess_game": lambda b, a, r: (bool(r.get("reply")), "plays"),
}


def check_turn(turn, before, after, ans):
    """[(ok, what, kind)]: kind "topic" (the answer's topic label) or "result" (what the turn had to do or say)."""
    results = []
    for name in turn.get("check", []):
        ok, why = CHECKS[name](before, after, ans)
        results.append((ok, why, "result"))
    topics = turn.get("topic")
    if topics:
        results.append((ans.get("topic") in topics, "topic %s in %s" % (ans.get("topic"), topics), "topic"))
    return results


def tts_wav(gateway, key, text, voice):
    """A 16 kHz WAV of the question, read by the kit's own TTS (stands in for a person speaking)."""
    st, h, pcm, _, _ = http("POST", gateway + "/v1/tts", {"model": "vieneu-turbo", "voice": voice, "text": text,
                                                         "format": "pcm", "sample_rate": 16000, "stream": False},
                            {"Authorization": "Bearer " + key})
    if st != 200:
        raise RuntimeError("tts %s: %s" % (st, pcm[:200]))
    silence = b"\0\0" * 4000  # a quarter second before and after, like a real push-to-talk clip
    buf = io.BytesIO()
    w = wave.open(buf, "wb")
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(16000)
    w.writeframes(silence + pcm + silence)
    w.close()
    return buf.getvalue()


def log_tail(path, pos):
    """New lines of the gateway log since pos: (lines, new pos)."""
    if not path or not os.path.exists(path):
        return [], pos
    with open(path, "rb") as f:
        f.seek(pos)
        data = f.read()
        return data.decode("utf-8", "replace").splitlines(), pos + len(data)


def gateway_calls(lines):
    chats, stts = [], []
    for l in lines:
        m = re.search(r" chat key=\S+ model=\S+ status=(\d+) .*?total=(\d+)ms.*?prompt=(\d+) cached=(\d+) completion=(\d+)", l)
        if m:
            chats.append({"status": int(m[1]), "ms": int(m[2]), "prompt": int(m[3]), "cached": int(m[4]), "completion": int(m[5])})
        m = re.search(r" stt key=\S+ model=\S+ status=(\d+) audio=([\d.]+)s process=(\d+)ms", l)
        if m:
            stts.append({"status": int(m[1]), "audio_s": float(m[2]), "ms": int(m[3])})
    return chats, stts


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--espos", default="http://127.0.0.1:8095")
    ap.add_argument("--user", default="admin")
    ap.add_argument("--password-file", required=True)
    ap.add_argument("--db", required=True, help="the EspOS server's espos.db")
    ap.add_argument("--gateway", default="http://127.0.0.1:8090")
    ap.add_argument("--key-file", required=True, help="a gateway API key (for the TTS that reads the questions)")
    ap.add_argument("--gateway-log", default="", help="the gateway's log: per-turn token counts")
    ap.add_argument("--turns", default=os.path.join(HERE, "fixtures", "turns.json"))
    ap.add_argument("--label", default="run")
    ap.add_argument("--voice", action="store_true", help="also play the voice turns through the device route")
    ap.add_argument("--voice-only", action="store_true")
    ap.add_argument("--question-voice", default="mai-anh", help="the TTS voice that reads the questions")
    ap.add_argument("--gap", type=float, default=2.5, help="seconds between turns (a person does not ask every second)")
    a = ap.parse_args()

    password = open(a.password_file).read().strip()
    key = open(a.key_file).read().strip()
    es = Espos(a.espos, a.user, password, a.db)
    turns = json.load(open(a.turns))

    # ---- a desk in Ho Chi Minh City that speaks Vietnamese, today's news ----
    dev_token = secrets.token_hex(32)
    st, dev, _ = es.api("POST", "/v1/devices", {"name": "Bàn làm việc", "kind": "s3",
                                                "token_hash": hashlib.sha256(dev_token.encode()).hexdigest()})
    if st not in (200, 201):
        sys.exit("create device: %s %s" % (st, dev))
    dev_id = dev.get("id") or dev.get("device", {}).get("id")
    st, out, _ = es.api("POST", "/v1/devices/%d/settings" % dev_id, {"changes": {
        "sys.lang": "vi", "dash.city": "TP Hồ Chí Minh", "dash.lat": "10.7769", "dash.lon": "106.7009", "sys.tz_min": "420"}})
    if st != 200:
        sys.exit("settings: %s %s" % (st, out))
    local = os.path.join(HERE, "fixtures", "news.local.json")   # make_news_fixture.py: today's real headlines
    news = json.load(open(local if os.path.exists(local) else os.path.join(HERE, "fixtures", "news.json")))
    have = {r["slug"] for r in es.rows("news_cache")}
    now_ms = int(time.time() * 1000)
    for slug, items in news.items():
        if slug.startswith("_"):   # "_note"
            continue
        if slug not in have:
            es.write_row("news_cache", {"slug": slug, "items": items, "fetched_ms": now_ms})
    print("device %d ready; %d news sections" % (dev_id, len([k for k in news if not k.startswith("_")])), file=sys.stderr)

    log_pos = os.path.getsize(a.gateway_log) if a.gateway_log and os.path.exists(a.gateway_log) else 0
    report = {"label": a.label, "started": time.strftime("%Y-%m-%d %H:%M:%S"), "loadavg_start": os.getloadavg(), "device": dev_id, "turns": []}

    def play(mode, i, turn):
        nonlocal log_pos
        before = es.state(dev_id)
        _, log_pos = log_tail(a.gateway_log, log_pos)
        rec = {"mode": mode, "n": i + 1, "say": turn["say"]}
        waited = 0
        if mode == "text":
            while True:   # the server allows a few turns a minute per route: wait out a 429 (not counted)
                st, ans, total = es.api("POST", "/v1/devices/%d/assist" % dev_id, {"text": turn["say"]})
                if st != 429 or waited >= 90:
                    break
                time.sleep(5)
                waited += 5
            rec.update(status=st, seconds=round(total, 2), rate_limited_s=waited)
        else:
            wav = tts_wav(a.gateway, key, turn["say"], a.question_voice)
            while True:
                st, h, pcm, first, total = http("POST", a.espos + "/v1/device/assist?stream=1", wav,
                                                {"Authorization": "Device " + dev_token, "Content-Type": "audio/wav"})
                if st != 429 or waited >= 90:
                    break
                time.sleep(5)
                waited += 5
            ans = {}
            xa = h.get("X-Assist") or h.get("x-assist")
            if xa:   # the turn's JSON answer, base64 (device_assist/prep)
                try:
                    ans = json.loads(base64.b64decode(xa))
                except Exception:
                    ans = {"raw": xa[:200]}
            elif pcm[:1] == b"{":
                ans = json.loads(pcm)
            rate = int(h.get("X-Sample-Rate") or 16000)
            voiced = h.get("Content-Type", "").startswith("audio")
            rec.update(status=st, seconds=round(total, 2), first_sound=round(first, 2) if voiced else None,
                       audio_s=round(len(pcm) / 2 / rate, 2) if voiced else 0, wav_s=round((len(wav) - 44) / 32000, 2))
        time.sleep(0.3)  # let the apply step's writes land
        after = es.state(dev_id)
        lines, log_pos = log_tail(a.gateway_log, log_pos)
        chats, stts = gateway_calls(lines)
        checks = check_turn(turn, before, after, ans) if st == 200 else [(False, "status %s %s" % (st, ans.get("error")), "result")]
        rec.update(heard=ans.get("heard"), reply=ans.get("reply"), topic=ans.get("topic"), mood=ans.get("mood"),
                   server_ms=ans.get("ms"), llm_calls=chats, stt_calls=stts,
                   json_first_try=len(chats) == 1 and st == 200, ok=all(c[0] for c in checks if c[2] == "result"),
                   topic_ok=all(c[0] for c in checks if c[2] == "topic"),
                   checks=[{"ok": c[0], "what": c[1], "kind": c[2]} for c in checks])
        mark = ("ok " if rec["ok"] else "BAD") + ("" if rec["topic_ok"] else "~")
        extra = (" first_sound=%ss" % rec.get("first_sound")) if mode == "voice" else ""
        tok = (" prompt=%d cached=%d out=%d" % (chats[-1]["prompt"], chats[-1]["cached"], chats[-1]["completion"])) if chats else ""
        print("%s %-5s %2d %5.1fs%s%s  %s -> %s" % (mark, mode, i + 1, rec["seconds"], extra, tok, turn["say"], (rec["reply"] or "")[:110]), file=sys.stderr)
        if mode == "voice":
            print("              heard: %s" % rec.get("heard"), file=sys.stderr)
        report["turns"].append(rec)
        time.sleep(a.gap)

    modes = (["voice"] if a.voice_only else ["text"] + (["voice"] if a.voice else []))
    for mode in modes:
        for i, t in enumerate(turns):
            if mode == "voice" and t.get("text_only"):
                continue
            play(mode, i, t)
        if mode == "text" and "voice" in modes:
            # clear what the text pass did, so the voice pass checks the same things from scratch
            es.api("POST", "/v1/devices/%d/settings" % dev_id, {"changes": {"s3.alarms": "[]", "s3.notes": "[]", "s3.todo": "[]", "s3.volume": "50"}})

    # ---- summary ----
    def summary(mode):
        ts = [t for t in report["turns"] if t["mode"] == mode]
        if not ts:
            return None
        secs = [t["seconds"] for t in ts if t["status"] == 200]
        s = {"turns": len(ts), "result_ok": sum(t["ok"] for t in ts), "topic_ok": sum(t["topic_ok"] for t in ts),
             "http_200": sum(t["status"] == 200 for t in ts),
             "json_first_try": sum(t["json_first_try"] for t in ts),
             "median_s": round(statistics.median(secs), 2) if secs else None,
             "p90_s": round(sorted(secs)[int(len(secs) * 0.9) - 1], 2) if len(secs) >= 2 else None,
             "max_s": max(secs) if secs else None}
        prompts = [c["prompt"] for t in ts for c in t["llm_calls"]]
        cached = [c["cached"] for t in ts for c in t["llm_calls"]]
        if prompts:
            s["median_prompt_tokens"] = int(statistics.median(prompts))
            s["median_cached_tokens"] = int(statistics.median(cached))
        if mode == "voice":
            fs = [t["first_sound"] for t in ts if t.get("first_sound")]
            if fs:
                s["median_first_sound_s"] = round(statistics.median(fs), 2)
                s["p90_first_sound_s"] = round(sorted(fs)[int(len(fs) * 0.9) - 1], 2) if len(fs) >= 2 else None
        return s
    report["summary"] = {m: summary(m) for m in modes}
    report["loadavg_end"] = os.getloadavg()
    os.makedirs(os.path.join(HERE, "results"), exist_ok=True)
    path = os.path.join(HERE, "results", "assist-%s.json" % a.label)
    json.dump(report, open(path, "w"), ensure_ascii=False, indent=1)
    print(json.dumps(report["summary"], ensure_ascii=False, indent=1))
    print("wrote", path, file=sys.stderr)


if __name__ == "__main__":
    main()
