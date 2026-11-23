#!/usr/bin/env python3
"""The simulator test of app settings (docs/app-platform-plan.md lane 1e): apps/hello-settings installed in a copy of
sim/data-apps, the form on the device, values synced to a local mock of device_sync (tools/mock_sync.py) and back,
a secret delivered by the "server" and acknowledged, the JS app reading its values and hearing app.onSettings.

  make -C sim BUILD=build-appset EXTRA="-fsanitize=address -fno-omit-frame-pointer"
  python3 tools/appset_sim_test.py [--sim sim/build-appset/espos-sim] [--out DIR] [--rot 1] [--board28]

Exit status 0 when every check passes; screenshots (PNG) in OUT.
"""
import argparse
import json
import os
import shutil
import socket
import subprocess
import sys
import time
import urllib.request
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
APP = ROOT / "apps/hello-settings"
ID = "dev.espos.hello.settings"


def install(data):
    """apps/hello-settings as the installer leaves a JS app on LittleFS (manifest.json with parts and files)."""
    shutil.rmtree(data, ignore_errors=True)
    shutil.copytree(ROOT / "sim/data-apps", data)
    d = data / "fs/pkgs" / ID
    d.mkdir(parents=True)
    main = (APP / "main.js").read_bytes()
    (d / "main.js").write_bytes(main)
    m = json.loads((APP / "manifest.json").read_text())
    m.pop("part", None)
    m["parts"] = ["core"]
    m["files"] = [{"n": "main.js", "s": len(main), "c": zlib.crc32(main)}]
    (d / "manifest.json").write_text(json.dumps(m, ensure_ascii=False))
    s = json.loads((data / "settings.json").read_text())
    s = {k: v for k, v in s.items() if not k.startswith("app.")}
    s["sys.lang"] = "en"
    (data / "settings.json").write_text(json.dumps(s, ensure_ascii=False))


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def post(port, path, obj):
    r = urllib.request.Request(f"http://127.0.0.1:{port}{path}", json.dumps(obj).encode(),
                               {"Content-Type": "application/json"})
    return json.loads(urllib.request.urlopen(r, timeout=10).read())


def state(port):
    return json.loads(urllib.request.urlopen(f"http://127.0.0.1:{port}/state", timeout=10).read())


def png(ppm, out):
    try:
        from PIL import Image
        Image.open(ppm).save(out)
        return out
    except Exception:  # noqa: BLE001
        return ppm


def run_sim(sim, data, script, out, port, extra_env=None):
    env = dict(os.environ, SIM_DATA=str(data), SIM_SYNC_URL=f"http://127.0.0.1:{port}/v1/device/sync",
               SIM_SYNC_MS="4000", ASAN_OPTIONS="detect_leaks=0:abort_on_error=1", SDL_VIDEODRIVER="dummy")
    env.update(extra_env or {})
    (out / "script.txt").write_text(script)
    p = subprocess.run([str(sim), str(out / "script.txt"), str(out)], env=env, capture_output=True, text=True,
                       timeout=900)
    (out / "sim.log").write_text(p.stderr)
    return p


TOK = "dev.espos.test.tokens"  # a fixture like dev.espos.sessions: a server secret delivered by the sync


def install_tokens(data):
    d = data / "fs/pkgs" / TOK
    d.mkdir(parents=True)
    main = b"app.title = 'Tokens'; app.render = () => ui.text(JSON.stringify(settings.get('espos_token')));"
    (d / "main.js").write_bytes(main)
    m = {"id": TOK, "name": "Token test", "version": "1.0.0", "code": 10000, "kind": "js", "engine": "js",
         "perms": ["net:api.espos.dev"], "parts": ["core"],
         "settings": [{"key": "espos_token", "type": "secret", "where": "server", "label": "EspOS token"}],
         "auth": {"espos": {"bearer": "espos_token", "send_to": [f"https://api.espos.dev/v1/device/app/{TOK}/*"]}},
         "files": [{"n": "main.js", "s": len(main), "c": zlib.crc32(main)}]}
    (d / "manifest.json").write_text(json.dumps(m))


# Portrait 3.5" (320 x 480) places, from the shots: the form's rows, the keyboard's keys.
KB_SAVE, KB_BACK = "286 454", "293 402"
FORM = f"""wait 1000
app settings appform:{ID}
wait 800
shot 01-form-top
wait 3500
shot 01b-form-after-sync
tap 270 237
tap 147 181
tap 150 100
wait 600
""" + f"tap {KB_BACK}\n" * 6 + f"""type Bob
shot 02-text-editor
tap {KB_SAVE}
wait 600
drag 160 420 160 120
wait 400
drag 160 420 160 120
wait 600
tap 160 312
wait 600
type https://shop.example.com
tap {KB_SAVE}
wait 800
tap 160 200
wait 600
tap 160 119
wait 600
type kitchen
tap {KB_SAVE}
wait 800
shot 03-list
back
wait 600
tap 160 400
wait 600
type sk_test_1234ABCD
wait 1800
shot 04-secret-editor
tap {KB_SAVE}
wait 800
shot 05-form-bottom
tap 160 135
wait 600
shot 06-time
back
wait 600
back
wait 600
back
wait 600
app settings apps
wait 800
shot 07-apps
app settings app:{ID}
wait 800
shot 08-app-page
wait 4500
"""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sim", default=str(ROOT / "sim/build-appset/espos-sim"))
    ap.add_argument("--out", default=str(ROOT / "sim/shots/appset"))
    a = ap.parse_args()
    out = Path(a.out)
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    data = ROOT / "sim/data-appset"
    install(data)
    install_tokens(data)
    port = free_port()
    mock = subprocess.Popen([sys.executable, str(ROOT / "tools/mock_sync.py"), str(port), "--pkg", str(APP)],
                            stderr=open(out / "mock.log", "w"))
    time.sleep(1.0)
    fails = []

    def expect(ok, what):
        print(("ok    " if ok else "FAIL  ") + what)
        if not ok:
            fails.append(what)

    def sim(name, script, check_asan=True):
        o = out / name
        o.mkdir()
        p = run_sim(a.sim, data, script, o, port)
        expect(p.returncode == 0, f"{name}: the simulator exits 0 (got {p.returncode})")
        log = (o / "sim.log").read_text()
        if check_asan:
            expect("AddressSanitizer" not in log and "runtime error" not in log, f"{name}: no ASan/UBSan report")
        for f in o.glob("*.ppm"):
            png(f, out / f"{name}-{f.stem}.png")
        return log

    k = lambda key: f"app.{ID}.{key}"  # noqa: E731
    try:
        # 1. The form on the device: values typed there go up and pass the SDK's rules on the "server".
        post(port, "/web/set", {"k": k("name"), "v": "Ana"})
        r = post(port, "/web/set", {"k": k("speed"), "v": "11"})
        expect(bool(r["refused"]) and r["refused"][-1]["k"] == k("speed"), "the mock refuses speed 11 (SDK checkValue)")
        log = sim("run1", FORM)
        expect(f"sim sync: setting {k('name')} = Ana (from the server)" in log, "the web's name arrives on the device")
        st = state(port)
        want = {k("name"): "Bob", k("units"): "f", k("seconds"): "1", k("shop_url"): "https://shop.example.com",
                k("tags"): '["kitchen"]'}
        for key, v in want.items():
            expect(st["settings"].get(key) == v, f"server has {key} = {v} (got {st['settings'].get(key)!r})")
        refused = [x for x in st["refused"] if x["k"] != k("speed") or x["v"] != "11"]
        expect(not refused, f"nothing the device sent was refused (got {refused})")
        expect(st["app_secrets"] == {ID: ["api_key"]}, f"app_secrets reports the device key (got {st['app_secrets']})")
        expect("sk_test" not in json.dumps(st["bodies"]) and "sk_test" not in log, "the secret's value never leaves "
               "the device nor reaches the trace")

        # 2. From the server: a web change heard by the running app (app.onSettings), a delivered server secret and
        # its ack, and re-pointing (the shop address changes on the web: the device key bound to it is cleared).
        base = st["syncs"]
        post(port, "/web/set", {"k": k("speed"), "v": "7", "at_sync": base + 2})
        post(port, "/web/secret", {"app": TOK, "key": "espos_token", "v": "tok_" + "ab12" * 16, "at_sync": base + 2})
        post(port, "/web/set", {"k": k("shop_url"), "v": "https://other.example.com", "at_sync": base + 3})
        log = sim("run2", f"""wait 4500
secret {ID} api_key sk_live_9876
app {ID}
wait 1000
shot 01-app
wait 4000
shot 02-app-speed
wait 4000
shot 03-app-repointed
wait 4500
app settings appform:{TOK}
wait 800
shot 04-server-secret
""")
        st = state(port)
        expect(f"sim sync: setting {k('name')} = Bob (from the server)" in log, "a fresh device gets its values back")
        expect(f"sim sync: setting {k('speed')} = 7 (from the server)" in log, "the web's speed 7 arrives")
        expect(f"secret: {TOK} espos_token set" in log, "the server secret is stored on the device")
        expect(any(x["app"] == TOK and x["key"] == "espos_token" for x in st["acks"]), "the device acks it")
        expect(not st["secrets"], f"nothing left to deliver (got {st['secrets']})")
        expect(f"appset: {ID} api_key cleared" in log, "re-pointing clears the device key when shop_url changes")
        expect(st["app_secrets"] == {TOK: ["espos_token"]}, f"app_secrets after both (got {st['app_secrets']})")
        expect("tok_ab12" not in log, "the token never reaches the trace")

        # 3. Landscape: the same form in two columns.
        sim("run3", f"""rot 1
wait 1500
app settings appform:{ID}
wait 800
shot 01-form-landscape
""")
        # 4. German (about 30% longer than English): the form, the secret screen, nothing cut (the "overflow" check).
        langs = out / "langs"
        subprocess.run([sys.executable, str(ROOT / "tools/i18n.py"), "build", "de", "--out", str(langs)], cwd=ROOT,
                       check=True, stdout=subprocess.DEVNULL)
        for f in langs.glob("dev.espos.lang.de-*.espk"):
            subprocess.run([sys.executable, str(ROOT / "tools/espk.py"), "--install-sim", str(data), str(f), "--vol",
                            "flash"], cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
        log = sim("run4", f"""set sys.lang de
wait 3000
app settings appform:{ID}
wait 800
shot 01-form-de
overflow form-de
drag 160 420 160 120
wait 400
drag 160 420 160 120
wait 600
shot 02-form-de-bottom
overflow form-de-bottom
app settings app:{ID}
wait 800
shot 03-app-de
overflow app-de
""")
        cut = [ln for ln in log.splitlines() if ln.startswith("OVERFLOW")]
        expect(log.count("overflow form-de") >= 2 and not cut, f"German: no cut or overlapping text ({cut[:6]})")
        print("shots:\n  " + "\n  ".join(str(x) for x in sorted(out.glob("*.png"))))
    finally:
        mock.terminate()
    print(f"{len(fails)} failed" if fails else "all passed")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
