#!/usr/bin/env python3
"""A local stand-in for the server's device_sync (docs/app-slots.md §15.2), for the simulator's sync test of app
settings (lane 1e). The simulator syncs against it with SIM_SYNC_URL=http://127.0.0.1:PORT/v1/device/sync.

  python3 tools/mock_sync.py PORT --pkg apps/hello-settings [--pkg ...]

What it does, as the real one does:
  - app.<id>.<key> values the device sends are checked with the SDK's own checkValue (node, sdk/packages/cli/dist)
    against the package's manifest; good ones are kept (newest wins), others refused and listed in /state;
  - every answer carries all app.* rows kept, and the server secrets not acked yet ("secrets": [{app, key, v, ms}]);
  - "secret_acks" [{app, key, ms}] mark a secret delivered when ms matches; "app_secrets" is kept as reported.
Test hooks (the web's part): POST /web/set {"k", "v"}, POST /web/secret {"app", "key", "v"} (either with "at_sync": N:
applied when the device's Nth sync arrives), GET /state.
"""
import json
import subprocess
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CHECK = ROOT / "sdk/packages/cli/dist/settings.js"
schemas = {}  # pkg id -> {key: field}
state = {"settings": {}, "app_secrets": None, "acks": [], "refused": [], "secrets": [], "syncs": 0, "bodies": [],
         "later": []}  # later: web changes waiting for sync number "at_sync" (the test's timeline)


def check(pairs):
    """[(field, value)] -> [error or ""], by the SDK's checkValue (the rules every side shares)."""
    if not pairs:
        return []
    js = ("import {checkValue} from %s; let s = ''; process.stdin.on('data', d => s += d);"
          "process.stdin.on('end', () => console.log(JSON.stringify(JSON.parse(s).map(([f, v]) => checkValue(f, v)))));"
          % json.dumps(CHECK.as_uri()))
    out = subprocess.run(["node", "--input-type=module", "-e", js], input=json.dumps(pairs), capture_output=True,
                         text=True, timeout=60)
    return json.loads(out.stdout)


def accept(items):
    """[(k, v)] -> kept or refused (app.* only through the schema)."""
    pairs, where = [], []
    for k, v in items:
        if not k.startswith("app."):
            state["settings"][k] = v
            continue
        app, _, key = k[4:].rpartition(".")
        f = schemas.get(app, {}).get(key)
        if not f or f.get("type") in ("secret", "section"):
            state["refused"].append({"k": k, "v": v, "why": "unknown_app_key"})
            continue
        if v == "":  # "" = not set: kept, so every device hears the reset
            state["settings"][k] = v
            continue
        pairs.append([f, v])
        where.append(k)
    for k, (f, v), err in zip(where, pairs, check(pairs)):
        if err:
            state["refused"].append({"k": k, "v": v, "why": err})
        else:
            state["settings"][k] = v


class H(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def reply(self, obj, code=200):
        b = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(b)))
        self.end_headers()
        self.wfile.write(b)

    def do_GET(self):
        self.reply(state if self.path == "/state" else {"error": "not_found"}, 200 if self.path == "/state" else 404)

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers.get("Content-Length", 0))) or b"{}")
        if self.path == "/v1/device/sync":
            state["syncs"] += 1
            state["bodies"] = (state["bodies"] + [body])[-5:]
            for w in [w for w in state["later"] if w["at_sync"] <= state["syncs"]]:
                state["later"].remove(w)
                if "k" in w:
                    accept([(w["k"], w["v"])])
                else:
                    state["secrets"].append({"app": w["app"], "key": w["key"], "v": w["v"], "ms": int(time.time() * 1000)})
            accept([(s["k"], s["v"]) for s in body.get("settings", [])])
            if "app_secrets" in body:
                state["app_secrets"] = body["app_secrets"]
            for a in body.get("secret_acks", []):
                state["acks"].append(a)
                state["secrets"] = [s for s in state["secrets"]
                                    if not (s["app"] == a["app"] and s["key"] == a["key"] and s["ms"] == a["ms"])]
            sys.stderr.write("mock sync #%d: %d settings up, app_secrets %s, %d acks\n" % (
                state["syncs"], len(body.get("settings", [])), json.dumps(body.get("app_secrets")),
                len(body.get("secret_acks", []))))
            return self.reply({"settings": {k: v for k, v in state["settings"].items() if k.startswith("app.")},
                               "settings_rev": state["syncs"], "secrets": state["secrets"]})
        if body.get("at_sync"):  # from that sync on (the test's timeline)
            state["later"].append(body)
            return self.reply({"ok": True, "later": True})
        if self.path == "/web/set":
            accept([(body["k"], body["v"])])
            return self.reply({"ok": True, "refused": state["refused"][-1:]})
        if self.path == "/web/secret":
            state["secrets"].append({"app": body["app"], "key": body["key"], "v": body["v"], "ms": int(time.time() * 1000)})
            return self.reply({"ok": True})
        self.reply({"error": "not_found"}, 404)


def main():
    port = int(sys.argv[1])
    args = sys.argv[2:]
    for i, a in enumerate(args):
        if a == "--pkg":
            m = json.loads((Path(args[i + 1]) / "manifest.json").read_text())
            schemas[m["id"]] = {f["key"]: f for f in m.get("settings", []) if f.get("key")}
    ThreadingHTTPServer(("127.0.0.1", port), H).serve_forever()


if __name__ == "__main__":
    main()
