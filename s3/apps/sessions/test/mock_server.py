#!/usr/bin/env python3
"""A local stand-in for the EspOS server's Sessions routes (s3/docs/desktop-protocol.md §9; the answers' shapes as
docs/desktop-sessions-mock.json), for the simulator: SIM_API_BASE=http://127.0.0.1:PORT makes the app's
{espos}/v1/device/app/dev.espos.sessions/* calls land here, SIM_SYNC_URL=http://127.0.0.1:PORT/v1/device/sync makes
the simulator's sync fetch the app's token (the package secret espos_token) from here.

    python3 apps/sessions/test/mock_server.py [--port 8790] [--scene normal]

Scenes (POST /mock/scene {"name": ...} switches at run time): normal, fast (one session prints 50 lines a second),
share_off, offline, view, estop, not_paired, disabled, empty, locked (AI routes 402), risky_perm,
notallowed (every send 403 sender_not_allowed: the Mac's allow_from leaves this device out, macs[].allowed not sent),
disallowed (the same with macs[].allowed false), allowed (macs[].allowed true), oddstatus (sends answer 503, no JSON code),
turnedon (the first send 403 sender_not_allowed, then the person turns the device on at the Mac: macs[].allowed true).
GET /mock/log: what the app sent (sends, suggest, compose, dictate), newest last. Nothing here leaves the machine.
"""
import argparse
import json
import os
import re
import random
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

ROOT = Path(__file__).resolve().parents[3]
APP = "/v1/device/app/dev.espos.sessions"
DEVICE = "5e" * 32  # the simulator's device credential (src/js/jsnet.cpp deviceAuth under SIM; SIM_DEVICE_TOKEN overrides)
TOKEN = "sess-" + "5e" * 20  # the later app-scoped token (desktop-protocol.md §9.0, off in v1): accepted too
RISKY = json.loads((ROOT / "docs" / "desktop-risky.json").read_text())
MAC = 900
COLS = 100

lock = threading.Condition()
state = {"scene": "normal", "rev": 1, "log": [], "nonces": {}, "acked": False}
now_ms = lambda: int(time.time() * 1000)

# ---------------------------------------------------------------- screens: markup → line + colour runs
CLS = {"dim": (0, 0, 2), "or": (0x1000000 | 0xD77757, 0, 0), "gr": (3, 0, 0), "rd": (2, 0, 0), "bl": (5, 0, 0),
       "pu": (6, 0, 0), "b": (0, 0, 1), "add": (0, 0x1000000 | 0x13361F, 0), "del": (0, 0x1000000 | 0x4A1D1F, 0)}


def line(mk):
    text, runs, pos = "", [], 0
    for m in re.finditer(r"\{([a-z ]+):([^{}]*)\}|([^{]+)", mk):
        if m.group(3) is not None:
            text += m.group(3)
            pos += len(m.group(3))
            continue
        seg = m.group(2)
        fg = bg = attr = 0
        for c in m.group(1).split():
            f, b, a = CLS.get(c, (0, 0, 0))
            fg, bg, attr = f or fg, b or bg, attr | a
        if seg:
            runs.append([pos, len(seg), fg, bg, attr])
        text += seg
        pos += len(seg)
    out = {"t": text}
    if runs:
        out["r"] = runs[:24]
    return out


def box(*inner, col="dim"):
    w = COLS - 2
    return [f"{{{col}:╭{'─' * w}╮}}"] + [f"{{{col}:│}} {s}{' ' * max(0, w - 2 - len(re.sub(r'{[a-z ]+:|}', '', s)))} {{{col}:│}}" for s in inner] + [f"{{{col}:╰{'─' * w}╯}}"]


START = [
    "{dim:> fix the build error in web/app and run the tests}", "",
    "{fg:⏺} I'll check the build first.", "",
    "{gr:⏺} {b:Bash}(npm run build)",
    "  {dim:⎿}  > web@0.9.3 build",
    "     > tsc -b && vite build",
    "     {rd:src/n2/desktop/Page.tsx(212,18): error TS2345: Argument of type 'string | undefined' is not assignable to parameter of type 'string'.}",
    "", "{fg:⏺} The page passes a name that can be missing. I'll fall back to a default title.", "",
    "{gr:⏺} {b:Update}(src/n2/desktop/Page.tsx)",
    "  {dim:⎿}  Updated {b:src/n2/desktop/Page.tsx} with 1 addition and 1 removal",
    "     {dim:210}    const row = byId(id)",
    "     {del:211 -  setTitle(row.name)}",
    "     {add:211 +  setTitle(row.name ?? t('Untitled'))}", "",
    "{gr:⏺} {b:Bash}(npm run build && npm test)",
    "  {dim:⎿}  {gr:✓} 412 modules transformed.",
    "     {gr:✓} built in 4.81s",
    "      {b:Test Files}  {gr:38 passed} (38)",
    "           {b:Tests}  {gr:291 passed} (291)", "",
]
TAIL_RUN = ["{or:✻ Brewing} {dim:(42s · ↓ 2.1k tokens · esc to interrupt)}", ""] + box(">") + ["  {pu:⏵⏵ auto mode on} {dim:(shift+tab to cycle)}"]


def perm_box(cmd, why):
    return box("{b:Bash command}", "", "  " + cmd, "  {dim:" + why + "}", "", "Do you want to proceed?", "{or:❯ 1. Yes}",
               "  2. Yes, and don't ask again for {b:git push} commands in {b:~/code/espos}",
               "  3. No, and tell Claude what to do differently {dim:(esc)}", col="or")


class Screen:
    def __init__(self, sid, lines):
        self.epoch = "%08x" % random.getrandbits(32)
        self.seq = 0
        self.lines = {}  # n -> (seq, line)
        self.top = 1
        for mk in lines:
            self.add(mk)
        self.init = False

    def add(self, mk):
        self.at = now_ms()
        self.seq += 1
        n = max(self.lines) + 1 if self.lines else 1
        self.lines[n] = (self.seq, line(mk))
        if not getattr(self, "init", True):  # not the lines the screen starts with
            LAT["made"][(id(self), self.seq)] = (self.at, "keys" if mk.startswith("{dim:keys:") else "line")
        self.top = max(1, n - 39)
        return n

    def set(self, n, mk):
        self.at = now_ms()
        self.seq += 1
        self.lines[n] = (self.seq, line(mk))
        LAT["made"][(id(self), self.seq)] = (self.at, "line")

    def answer(self, since):
        t = now_ms()
        for n in self.lines:
            sq = self.lines[n][0]
            if sq > since and (id(self), sq) in LAT["made"] and (id(self), sq) not in LAT["out"]:
                LAT["out"][(id(self), sq)] = t
        keep = sorted(self.lines)[-1000:]
        if since <= 0 or since > self.seq:
            pick = keep[-300:]
            full = True
        else:
            pick = [n for n in keep if self.lines[n][0] > since]
            full = False
        return {"epoch": self.epoch, "seq": self.seq, "at": getattr(self, "at", now_ms()), "full": full, "cols": COLS, "rows": 40, "top": self.top,
                "cur": [max(self.lines), 0], "lines": [dict(n=n, **self.lines[n][1]) for n in pick]}


def sessions_for(scene):
    t = now_ms()
    cmd = "rm -rf build dist ~/.cache/espos" if scene == "risky_perm" else "git push origin main"
    ss = [
        {"mac": MAC, "id": "s-1", "name": "espos", "cwd": "~/code/espos", "state": "perm", "since_ms": t - 120000,
         "tool": "Bash", "term": "iterm", "can_reply": True, "last": "Build and tests pass. I'll push the fix.",
         "req": {"id": "r-1", "kind": "perm", "tool": "Bash", "cmd": cmd, "desc": "Push the settings fix to GitHub" if scene != "risky_perm" else "Clean the old build outputs",
                 "cwd": "~/code/espos", "deadline_ms": t + 522000, "risky": scene == "risky_perm"}},
        {"mac": MAC, "id": "s-2", "name": "web", "cwd": "~/code/web", "state": "input", "since_ms": t - 300000, "term": "iterm",
         "can_reply": True, "last": "Should I also update the docs page for the new route, or leave the docs for later?"},
        {"mac": MAC, "id": "s-4", "name": "case", "cwd": "~/code/case", "state": "error", "since_ms": t - 60000, "term": "iterm",
         "last": "API error: usage limit reached. It resets at 16:20."},
        {"mac": MAC, "id": "s-3", "name": "s3desk", "cwd": "~/code/s3desk", "state": "run", "since_ms": t - 42000, "tool": "Bash",
         "term": "iterm", "can_reply": True, "last": "Running the simulator tests with ASan"},
        {"mac": MAC, "id": "s-5", "name": "desktop", "cwd": "~/code/desktop", "state": "idle", "since_ms": t - 1080000, "term": "iterm",
         "can_reply": True, "last": "Done. The release notes are in RELEASING.md."},
    ]
    return ss


SCREENS = {}


def reset(scene):
    state["scene"] = scene
    state["turned_on"] = False
    state["sessions"] = [] if scene in ("empty", "estop") else sessions_for(scene)
    s1 = [x for x in state["sessions"] if x["id"] == "s-1"]
    SCREENS.clear()
    SCREENS["s-1"] = Screen("s-1", START + ["{fg:⏺} Build and tests pass. I'll push the fix.", ""] + perm_box(s1[0]["req"]["cmd"] if s1 else "git push origin main", "Push the settings fix to GitHub"))
    SCREENS["s-2"] = Screen("s-2", ["{dim:> add the device app route for the sessions app, with tests}", "", "{gr:⏺} {b:Write}(backend/routes/device_app.go)",
                                    "  {dim:⎿}  Wrote 84 lines to {b:backend/routes/device_app.go}", "", "{gr:⏺} {b:Bash}(go test ./routes/...)",
                                    "  {dim:⎿}  ok   espos/backend/routes  0.412s", "",
                                    "{fg:⏺} The route and its tests are in. Should I also update the docs page for the new route, or leave the docs for later?", ""] + box(">") + ["  {pu:⏵⏵ auto mode on} {dim:(shift+tab to cycle)}"])
    SCREENS["s-3"] = Screen("s-3", START + ["{fg:⏺} Build and tests pass. Now the docs site build.", "", "{gr:⏺} {b:Bash}(cd site && npm run build)", "  {dim:⎿}  Running"] + TAIL_RUN)
    SCREENS["s-5"] = Screen("s-5", ["{fg:⏺} Done. The release notes are in RELEASING.md."] + box(">"))
    state["rev"] += 1


def macs():
    sc, t = state["scene"], now_ms()
    m = {"id": MAC, "name": "MacBook Pro", "online": True, "mode": "on", "seen_ms": t - 2000, "stream": True, "confirm_at": "either",
         "cmds": dict({"claude.approve": "confirm", "claude.deny": "safe", "claude.reply": "confirm", "claude.key": "confirm"},
                      **({} if os.environ.get("MOCK_SERVER") == "old" else {"claude.keys": "confirm"}))}
    if sc == "share_off":
        m.update(stream=False, stream_why="off_on_mac")
    if sc == "offline":
        m.update(online=False, seen_ms=t - 540000)
    if sc == "view":
        m.update(mode="view", stream=True)
    if sc == "estop":
        m.update(mode="off", online=False, stream=False, stream_why="offline")
    if sc in ("disallowed", "allowed"):  # the Mac's list of devices that may send commands (desktop's allow_from)
        m["allowed"] = sc == "allowed"
    if sc == "turnedon" and state.get("turned_on"):
        m["allowed"] = True
    return [m]


def ticker():
    """The working session prints: a line every 2 s (fast: 50 a second), its spinner line counting up."""
    k = 0
    while True:
        time.sleep(0.02 if state["scene"] == "fast" else 1.0 if state["scene"] == "latency" else 2.0)
        with lock:
            s = SCREENS.get("s-3")
            if not s or state["scene"] in ("share_off", "estop", "offline"):
                continue
            k += 1
            last = max(s.lines)
            # insert above the spinner + box (the last 6 lines move down)
            tail = [s.lines[n][1] for n in sorted(s.lines)[-6:]]
            tail_seq = [s.lines[n][0] for n in sorted(s.lines)[-6:]]
            colour = ["{gr:✓}", "{dim:·}", "{bl:→}", "{or:!}"][k % 4]
            mk = f"     {colour} [{k:05d}] vite: transforming src/n2/part{k % 97}.tsx ({k * 37 % 900} ms) " + ("very long line " * (k % 9))
            for i, n in enumerate(sorted(s.lines)[-6:]):
                pass
            first_tail = sorted(s.lines)[-6]
            s.set(first_tail, mk)
            for i, tl in enumerate(tail):
                s.seq += 1
                s.lines[first_tail + 1 + i] = (s.seq, tl)
                k0 = (id(s), tail_seq[i])  # a line moved down before it went out: it is still that line (latency test)
                if k0 in LAT["made"] and k0 not in LAT["out"]:
                    LAT["made"][(id(s), s.seq)] = LAT["made"].pop(k0)
            s.top = max(1, max(s.lines) - 39)
            sp = max(s.lines) - 5
            s.lines[sp] = (s.seq + 1, line(f"{{or:✻ Brewing}} {{dim:({42 + k // 2}s · ↓ {2.1 + k / 100:.1f}k tokens · esc to interrupt)}}"))
            s.seq += 1
            s.at = now_ms()
            lock.notify_all()


# The latency test (run.sh latency): when each line was made / first sent, and when each request came in.
LAT = {"made": {}, "out": {}, "req": []}


def latency():
    def stats(xs):
        xs = sorted(xs)
        return {"n": len(xs), "avg": round(sum(xs) / len(xs)) if xs else None, "p90": xs[int(len(xs) * 0.9)] if xs else None,
                "max": xs[-1] if xs else None}
    lines = [LAT["out"][k] - t for k, (t, kind) in LAT["made"].items() if kind == "line" and k in LAT["out"]]
    keys = {k: (t, LAT["out"].get(k)) for k, (t, kind) in LAT["made"].items() if kind == "keys"}
    return {"line_age": stats(lines), "key_lines": sorted(keys.values()), "req": LAT["req"]}


# MOCK_SERVER=old: the server before the 11-22 work (the screen hold looks every 400 ms, a key waits ~1.5 s for the
# Mac's result, no kind "keys"); new (default): looks every 150 ms, keys answer at once, kind "keys" (desktop's change).
OLD = os.environ.get("MOCK_SERVER") == "old"


def hold(s, since, wait):
    """The server's long poll: look for a frame newer than `since` every 400 ms (old) or 150 ms (new), up to wait s."""
    end = time.time() + wait
    while time.time() < end:
        with lock:
            if s.seq != since:
                return
        time.sleep(min(0.4 if OLD else 0.15, max(0.0, end - time.time())))


def risky(text):
    for p in RISKY["text"]:
        if re.search(p["re"], text, re.I if p.get("i") else 0):
            return p["why"]
    return ""


class H(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def answer(self, code, obj=None):
        body = json.dumps(obj if obj is not None else {}, ensure_ascii=False).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body) if code != 204 else 0))
        self.end_headers()
        if code != 204:
            self.wfile.write(body)

    def body(self):
        n = int(self.headers.get("Content-Length") or 0)
        return self.rfile.read(n) if n else b""

    def gate(self):
        sc = state["scene"]
        if self.headers.get("Authorization", "") not in ("Device " + DEVICE, "Bearer " + TOKEN):
            self.answer(401, {"error": "unauthorized"})
            return False
        if sc == "not_paired":
            self.answer(404, {"error": "no_desktop"})
            return False
        if sc == "disabled":
            self.answer(503, {"error": "desktop_disabled"})
            return False
        return True

    def do_GET(self):
        u = urlparse(self.path)
        q = {k: v[0] for k, v in parse_qs(u.query).items()}
        if u.path.endswith("/echo") or "/echo/" in u.path:  # the auth tests: which credential reached which path
            auth = self.headers.get("Authorization", "")
            state["log"].append({"route": "echo", "path": u.path, "auth": auth.split(" ")[0] if auth else "", "ms": now_ms()})
            return self.answer(200, {"path": u.path, "auth": bool(auth)})
        if u.path == "/mock/latency":
            return self.answer(200, latency())
        if u.path.startswith(APP + "/"):
            LAT["req"].append([now_ms(), "GET", u.path[len(APP) + 1:]])
        if u.path == "/mock/log":
            return self.answer(200, {"log": state["log"][-50:], "scene": state["scene"]})
        if u.path == APP + "/sessions":
            if not self.gate():
                return
            rev, wait = int(q.get("rev", 0) or 0), min(4, int(q.get("wait", 0) or 0))
            with lock:
                if wait and rev == state["rev"]:
                    lock.wait(wait)
                return self.answer(200, {"rev": state["rev"], "now_ms": now_ms(), "macs": macs(), "sessions": state["sessions"]})
        if u.path == APP + "/screen":
            if not self.gate():
                return
            sc = state["scene"]
            if sc in ("share_off", "estop"):
                return self.answer(409, {"error": "not_streaming", "why": "off_on_mac" if sc == "share_off" else "offline"})
            s = SCREENS.get(q.get("s", ""))
            if not s:
                return self.answer(409, {"error": "not_streaming", "why": "ended"})
            if "before" in q:
                b, c = int(q["before"]), min(200, int(q.get("count", 100)))
                ns = [n for n in sorted(s.lines) if n < b][-c:]
                return self.answer(200, {"epoch": s.epoch, "lines": [dict(n=n, **s.lines[n][1]) for n in ns]})
            since, wait = int(q.get("since", 0) or 0), min(3.0, float(q.get("wait", 0) or 0))
            if OLD:
                wait = float(int(wait))
            if wait and since == s.seq:
                hold(s, since, wait)
            with lock:
                a = s.answer(since)
                if OLD:
                    a.pop("at", None)  # the old server had no "at"
                return self.answer(200, a)
        self.answer(404, {"error": "no_route"})

    def do_POST(self):
        u = urlparse(self.path)
        raw = self.body()
        if u.path == "/mock/scene":
            name = json.loads(raw or b"{}").get("name", "normal")
            with lock:
                reset(name)
                lock.notify_all()
            return self.answer(200, {"ok": True, "scene": name})
        if u.path == "/v1/device/sync":  # the simulator's sync (SIM_SYNC_URL): the app's token, until acked
            return self.answer(200, {"settings": [], "secrets": []})  # v1: no app token (the device credential)
        if "/echo" in u.path:  # the auth test's report (and anything else posted to an echo path)
            auth = self.headers.get("Authorization", "")
            state["log"].append({"route": "echo", "path": u.path, "auth": auth.split(" ")[0] if auth else "",
                                 "body": json.loads(raw or b"{}"), "ms": now_ms()})
            return self.answer(200, {"ok": True})
        if u.path == "/v1/device/dictate":
            qq = parse_qs(u.query)
            state["log"].append({"route": "dictate", "bytes": len(raw), "lang": qq.get("lang", [""])[0], "ms": now_ms(),
                                 "expect": qq.get("expect", [""])[0], "about": qq.get("about", [""])[0],
                                 "auth": self.headers.get("Authorization", "") == "Device " + DEVICE})
            if qq.get("expect", [""])[0] == "yesno":  # the voice confirm: this mock always hears "ok"
                return self.answer(200, {"text": "ok", "seconds": 0.6, "yesno": "confirm"})
            if state["scene"] == "locked":
                return self.answer(402, {"error": "trial_over", "locked": "trial_over", "buy_url": "https://espos.dev/buy"})
            if len(raw) < 44 + 8000:
                return self.answer(200, {"text": "", "seconds": 0})
            return self.answer(200, {"text": "update the docs too and add a curl example", "seconds": round((len(raw) - 44) / 32000, 1)})
        if not u.path.startswith(APP + "/") or not self.gate():
            return None if u.path.startswith(APP + "/") else self.answer(404, {"error": "no_route"})
        d = json.loads(raw or b"{}")
        route = u.path[len(APP) + 1:]
        state["log"].append(dict(route=route, ms=now_ms(), **d))
        if route == "send":
            LAT["req"].append([now_ms(), "send", d.get("kind"), d.get("key") or d.get("keys")])
            if state["scene"] in ("notallowed", "disallowed"):  # desk_queue refuse(403) as sess_app fin.go passes it on
                return self.answer(403, {"state": "refused", "err": "sender_not_allowed"})
            if state["scene"] == "turnedon" and not state.get("turned_on"):
                with lock:
                    state["turned_on"] = True
                    state["rev"] += 1
                    lock.notify_all()
                return self.answer(403, {"state": "refused", "err": "sender_not_allowed"})
            if state["scene"] == "oddstatus":
                return self.answer(503, {"message": "upstream unavailable"})
            n = d.get("nonce", "")
            if n in state["nonces"]:
                return self.answer(200, state["nonces"][n])
            kind = d.get("kind")
            keys = [d.get("key")] if kind == "key" else d.get("keys") or [] if kind == "keys" else []
            if kind == "keys" and (OLD or not 1 <= len(keys) <= 20):
                return self.answer(400, {"error": "bad_kind" if OLD else "too_many_keys"})
            why = risky(d.get("text", "")) if kind == "text" else ("interrupt" if any(k in ("esc", "ctrl_c") for k in keys) else "")
            if keys and any(k not in ("up", "down", "left", "right", "shift_up", "shift_down") for k in keys) and \
                    any(x["id"] == d.get("s") and x.get("req") for x in state["sessions"]):
                return self.answer(409, {"error": "use_answer", "req": next(x["req"]["id"] for x in state["sessions"] if x["id"] == d.get("s"))})
            if kind == "approve":
                for x in state["sessions"]:
                    if x["id"] == d.get("s") and x.get("req", {}).get("risky"):
                        why = "delete"
            if why and not d.get("ack_risky"):
                return self.answer(428, {"error": "risky", "why": why})
            with lock:
                ans = {"id": "%032x" % random.getrandbits(128), "state": "done"}
                for x in state["sessions"]:
                    if x["id"] != d.get("s"):
                        continue
                    sc = SCREENS.get(x["id"])
                    if kind in ("approve", "deny"):
                        if x.get("req", {}).get("id") != d.get("req"):
                            ans = {"id": ans["id"], "state": "refused", "err": "req_gone"}
                            break
                        x.pop("req", None)
                        x["state"] = "run" if kind == "approve" else "input"
                        x["since_ms"] = now_ms()
                        if sc:
                            sc.add("{gr:⏺} " + ("Pushed. Opening a pull request next." if kind == "approve" else "Understood, I won't run that. What should I do instead?"))
                    elif keys and sc:  # the Mac typed them: the session shows it (the key's effect, for the latency test)
                        sc.add("{dim:keys: " + " ".join(keys) + "}")
                    elif kind == "text" and sc:
                        for ln in d.get("text", "").split("\n"):
                            sc.add("{dim:> " + ln + "}")
                        sc.add("{fg:⏺} On it.")
                        x["state"], x["since_ms"] = "run", now_ms()
                state["rev"] += 1
                lock.notify_all()
            if keys and OLD:  # the old server waited for the Mac's own result
                time.sleep(1.5)
            if keys and not OLD:
                ans["state"] = "sent"
            state["nonces"][n] = ans
            return self.answer(200, ans)
        if route == "suggest":
            if state["scene"] == "locked":
                return self.answer(402, {"error": "trial_over", "locked": "trial_over", "buy_url": "https://espos.dev/buy"})
            if d.get("s") != "s-2":
                return self.answer(204)
            time.sleep(0.8)
            return self.answer(200, {"chips": ["Yes, update the docs page", "Leave the docs for later", "Update docs and add a curl example"], "for_seq": d.get("seq", 0)})
        if route == "compose":
            if state["scene"] == "locked":
                return self.answer(402, {"error": "trial_over", "locked": "trial_over", "buy_url": "https://espos.dev/buy"})
            time.sleep(0.8)
            rough = d.get("rough", "")
            text = "Yes, please update the docs page too, and add a curl example for the new route." if "doc" in rough else rough[:1].upper() + rough[1:].rstrip(".") + "."
            return self.answer(200, {"text": text})
        self.answer(404, {"error": "no_route"})


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8790)
    ap.add_argument("--scene", default="normal")
    a = ap.parse_args()
    reset(a.scene)
    threading.Thread(target=ticker, daemon=True).start()
    print(f"mock: http://127.0.0.1:{a.port} scene {a.scene}", flush=True)
    ThreadingHTTPServer(("127.0.0.1", a.port), H).serve_forever()


if __name__ == "__main__":
    main()
