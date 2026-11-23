#!/usr/bin/env python3
"""device_sync + device_ping stand-in for the saver's Claude widget check (0.6.2; the owner's saver over the aquarium
stood at 89 % while the server had 4 %): the simulator (SIM_SYNC_URL=http://127.0.0.1:PORT/v1/device/sync) and the
firmware in QEMU (provisioned with api http://10.0.2.2:PORT) sync and ping here.

    saver-dash-mock.py PORT [--switch-at-sync N]

Every answer carries the saver page (an aquarium background, the time, a Claude widget M), no quiet hours, and the
dashboard: Claude at 89 % (5 hours) / 61 % (week) until the switch, then 4 % / 1 %. The switch: POST /mock/switch, or
at the Nth sync (--switch-at-sync). It raises the rev, as the server folds the desktop's usage_ms into it, so a device
that pings hears it and syncs. GET /mock/state: the syncs and pings (ms since the mock started), the rev, the
dashboard shown. Nothing leaves the machine.
"""
import argparse
import json
import os
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

T0 = time.time()
lock = threading.Lock()
state = {"rev": 5, "switched": False, "syncs": [], "pings": [], "switch_at": 0}

PAGES = {"v": 1, "cycle": 0, "light": 60, "night": "day",
         "p": [{"bg": {"t": "tank"}, "e": [
             {"t": "time", "x": 0, "y": 24, "w": 320, "h": 70, "a": "c", "b": 1, "c": "ffffff"},
             {"t": "w", "k": "claude", "s": "M", "x": 10, "y": 320, "sk": "glass"}]}],
         "l": [{"bg": {"t": "tank"}, "e": [
             {"t": "time", "x": 12, "y": 12, "w": 220, "h": 60, "a": "l", "b": 1, "c": "ffffff"},
             {"t": "w", "k": "claude", "s": "M", "x": 168, "y": 170, "sk": "glass"}]}]}
SETTINGS = {"s3.saver_pages": json.dumps(PAGES, separators=(",", ":")), "slot.saver": "builtin",
            "dash.quiet_from": "00:00", "dash.quiet_to": "00:00", "s3.saver": "600"}
# HOME_FROM=FILE: its "set s3.home ..." and "set s3.home_l ..." lines go down too (resized widgets on Home in QEMU).
if os.environ.get("HOME_FROM"):
    for line in open(os.environ["HOME_FROM"], encoding="utf-8"):
        for k in ("s3.home", "s3.home_l"):
            if line.startswith("set " + k + " "):
                SETTINGS[k] = line[len("set " + k + " "):].strip()


def ms():
    return int((time.time() - T0) * 1000)


def claude(five, week):
    now = int(time.time() * 1000)
    lt = time.localtime()
    hm = time.strftime("%H:%M", lt)
    seen = {"hm": hm, "wd": (lt.tm_wday + 1) % 7, "dd": 0, "dm": time.strftime("%d/%m", lt)}
    return {"online": True, "seen_ms": now, "mode": "on", "name": "MacBook Pro", "seen": seen, "waiting": 0,
            "sessions": 2, "as_of_ms": now, "tokens": 2412000, "usd": 18.37,
            "days": [820000, 1500000, 0, 2900000, 640000, 1210000, 2412000],
            "five": {"pct": five, "resets_s": 5400, "at": seen}, "week": {"pct": week, "resets_s": 258000, "at": seen}}


# What the Mac sent: fixed between switches (the server's dashboard changes only when the desktop's usage does).
BEFORE, AFTER = claude(89, 61), claude(4, 1)


def dashboard():
    return {"now_ms": int(time.time() * 1000), "tz_min": 420, "claude": AFTER if state["switched"] else BEFORE}


class H(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def answer(self, code, obj):
        b = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(b)))
        self.end_headers()
        self.wfile.write(b)

    def body(self):
        n = int(self.headers.get("Content-Length") or 0)
        return self.rfile.read(n) if n else b""

    def do_GET(self):
        if self.path.startswith("/v1/device/ping"):
            with lock:
                state["pings"].append(ms())
                return self.answer(200, {"rev": state["rev"]})
        if self.path == "/mock/state":
            with lock:
                return self.answer(200, dict(state, now=ms()))
        self.answer(404, {"error": "no_route"})

    def do_POST(self):
        self.body()
        if self.path.startswith("/v1/device/sync"):
            with lock:
                state["syncs"].append(ms())
                if state["switch_at"] and len(state["syncs"]) == state["switch_at"] and not state["switched"]:
                    state["switched"], state["rev"] = True, state["rev"] + 1
                return self.answer(200, {"settings": SETTINGS, "settings_rev": state["rev"], "dashboard": dashboard(),
                                         "now_ms": int(time.time() * 1000), "tz_min": 420})
        if self.path == "/mock/switch":
            with lock:
                state["switched"], state["rev"] = True, state["rev"] + 1
                state["switched_at"] = ms()
                return self.answer(200, {"ok": True, "rev": state["rev"]})
        self.answer(404, {"error": "no_route"})


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port", type=int)
    ap.add_argument("--switch-at-sync", type=int, default=0)
    a = ap.parse_args()
    state["switch_at"] = a.switch_at_sync
    ThreadingHTTPServer(("127.0.0.1", a.port), H).serve_forever()


if __name__ == "__main__":
    main()
