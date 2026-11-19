#!/usr/bin/env python3
"""Link the simulator to an EspOS account as a device of its own ("EspOS S3 (sim)"), the way a board pairs: a random
device token (kept in memory only), a 6-digit code the account's owner confirms on espos.dev, then the simulator runs
with the token in its environment (SIM_DEVICE_TOKEN) so the dog assistant's turns go to the live server.

    python3 wake/tools/simpair.py -- SIM_BINARY [ARGS...]     (other SIM_* variables pass through; the simulator is
                                                               opened again whenever it ends, until this is stopped)

Remove the "EspOS S3 (sim)" device on the web afterwards. The token is never printed or written to a file.
"""
import hashlib
import json
import os
import secrets
import sys
import time
import urllib.error
import urllib.request

API = os.environ.get("SIM_API", "https://api.espos.dev")
WEB = os.environ.get("SIM_WEB", "https://espos.dev")


def call(method, path, body=None, tok=None):
    h = {"Content-Type": "application/json", "User-Agent": "EspOS-S3-sim"}
    if tok:
        h["Authorization"] = "Device " + tok
    req = urllib.request.Request(API + path, data=json.dumps(body).encode() if body is not None else None,
                                 headers=h, method=method)
    try:
        with urllib.request.urlopen(req, timeout=20) as r:
            return r.status, json.loads(r.read() or b"{}")
    except urllib.error.HTTPError as e:
        try:
            return e.code, json.loads(e.read() or b"{}")
        except Exception:
            return e.code, {}


def main():
    argv = sys.argv[1:]
    if "--" in argv:
        argv = argv[argv.index("--") + 1:]
    if not argv:
        sys.exit(__doc__)
    tok = secrets.token_hex(32)
    for _ in range(4):
        code = "%06d" % secrets.randbelow(1000000)
        st, d = call("POST", "/v1/pair/start", {"code": code, "token_hash": hashlib.sha256(tok.encode()).hexdigest(),
                                                "name": "EspOS S3 (sim)", "kind": "s3"})
        if st == 200:
            break
        print(f"pair/start {st} {d}", file=sys.stderr)
    else:
        sys.exit("could not get a pairing code")
    print(f"PAIR CODE {code}  ->  {WEB}/app#/pair?code={code}  (confirm it on the web; the code lives 10 minutes)", flush=True)
    t0 = time.time()
    while time.time() - t0 < 1800:
        st, d = call("GET", "/v1/device/whoami", tok=tok)
        if st == 200:
            print(f"linked: device {d.get('device', {}).get('id', '?')}", flush=True)
            break
        time.sleep(3)
    else:
        sys.exit("not confirmed in 30 minutes")
    env = dict(os.environ, SIM_DEVICE_TOKEN=tok)
    # The window closed (or anything else ended it): open it again, linked the same. Stop with Ctrl-C / kill.
    import subprocess
    n = 0
    while True:
        n += 1
        rc = subprocess.call(argv, env=env)
        print(f"[simpair] the simulator ended (exit {rc}, run {n}); opening it again", flush=True)
        time.sleep(2)


if __name__ == "__main__":
    main()
