#!/usr/bin/env python3
"""The store catalog keeps every package's part choices (group, label, labels, default) and add-on fields
(addon_of, set, needs) when it is built again from the .espk files (apps/pack.py, tools/i18n.py catalog).
2026-11-18 a rebuild dropped run 1.0.1's roads group and the store made every road set required.

  python3 tools/test_catalog.py            the cases below in a temporary folder, then build-pkgs/espos/s3/catalog.json
"""
import json
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import espk  # noqa: E402

BASE = "https://example.invalid/apps-s3"
fails = 0


def check(cond, what):
    global fails
    print(("ok   " if cond else "FAIL ") + what)
    fails += not cond


def part(folder, man, name="data.bin"):
    d = Path(tempfile.mkdtemp(dir=folder))
    (d / "manifest.json").write_text(json.dumps(man, ensure_ascii=False))
    (d / name).write_bytes(b"x" * 64)
    out = Path(folder) / f"{man['id']}-{man['version']}-{man.get('part', 'core')}.espk"
    espk.build(d, out)
    return out


def run_pack(out, files):
    r = subprocess.run([sys.executable, str(ROOT / "apps/pack.py"), str(out), BASE] + [str(f) for f in files],
                       cwd=ROOT, capture_output=True, text=True)
    cat = json.loads((out / "catalog.json").read_text()) if r.returncode == 0 else None
    return r, cat


def entry(cat, pid):
    return next(e for e in cat["packages"] if e["id"] == pid)


CORE = {"id": "t.run", "name": "T", "version": "1.0.0", "code": 10000, "kind": "native", "engine": "run",
        "category": "game", "part": "core", "addon_of": "t.pet"}


def parts_without_choice(folder):  # like run 1.0.1: the roads parts' manifests carry no group
    return [part(folder, CORE)] + [part(folder, {"id": "t.run", "version": "1.0.0", "code": 10000, "part": p})
                                   for p in ("roads10", "roads20")]


with tempfile.TemporaryDirectory() as t:
    t = Path(t)
    # 1. the builder's list beside the parts: the choice comes back
    src, out = t / "src1", t / "out1"
    src.mkdir(), out.mkdir()
    files = parts_without_choice(src)
    (src / "t.run-1.0.0-catalog.json").write_text(json.dumps({"parts": [
        {"id": "core", "req": True},
        {"id": "roads10", "group": "roads", "label": "Light", "labels": {"vi": "Nhẹ"}, "default": True},
        {"id": "roads20", "group": "roads", "label": "Smooth", "default": False}]}))
    r, cat = run_pack(out, files)
    check(r.returncode == 0, "1 builder's list: catalog built")
    if cat:
        ps = {p["id"]: p for p in entry(cat, "t.run")["parts"]}
        check(ps["roads10"].get("group") == "roads" and ps["roads10"].get("default") is True, "1 group and default kept")
        check(ps["roads10"].get("labels") == {"vi": "Nhẹ"} and ps["roads20"].get("label") == "Smooth", "1 labels kept")
        check(entry(cat, "t.run").get("addon_of") == "t.pet", "1 addon_of kept")
    # 2. no list, but the catalog before had the same version with its choice: kept
    src2, out2 = t / "src2", t / "out2"
    src2.mkdir(), out2.mkdir()
    files2 = parts_without_choice(src2)
    old = {"packages": [dict(CORE, parts=[{"id": "core", "req": True},
                                          {"id": "roads10", "group": "roads", "label": "Light", "default": True},
                                          {"id": "roads20", "group": "roads", "label": "Smooth"}])]}
    (out2 / "catalog.json").write_text(json.dumps(old))
    r, cat = run_pack(out2, files2)
    check(r.returncode == 0 and cat and entry(cat, "t.run")["parts"][1].get("group") == "roads", "2 kept from the catalog before")
    # 3. nowhere to take the choice from: refused, never a catalog where every road set is required
    src3, out3 = t / "src3", t / "out3"
    src3.mkdir(), out3.mkdir()
    r, cat = run_pack(out3, parts_without_choice(src3))
    check(r.returncode != 0 and "neither required nor in a group" in r.stderr + r.stdout, "3 a catalog without the choice is refused")
    # 4. the choice in the parts' own manifests (tools/pawboop_run/make_pkgs.py since 2026-11-18); add-on fields
    src4, out4 = t / "src4", t / "out4"
    src4.mkdir(), out4.mkdir()
    files4 = [part(src4, dict(CORE, id="t.map", set="map", needs="t.run")),
              part(src4, {"id": "t.map", "version": "1.0.0", "code": 10000, "part": "hi", "group": "q", "label": "Hi",
                          "default": True})]
    r, cat = run_pack(out4, files4)
    e = entry(cat, "t.map") if cat else {}
    check(r.returncode == 0 and e.get("set") == "map" and e.get("needs") == "t.run" and e.get("addon_of") == "t.pet",
          "4 set, needs, addon_of from the manifest")
    check(bool(e) and e["parts"][1].get("group") == "q", "4 group from the part's manifest")

    # 6. the admin's paid marks (docs/lock.md) survive a rebuild; a part's private key only for the same version
    src6, out6 = t / "src6", t / "out6"
    src6.mkdir(), out6.mkdir()
    files6 = [part(src6, dict(CORE, id="t.dog", set="dog"))]
    old6 = {"packages": [dict(CORE, id="t.dog", set="dog", paid=True, free_ids=["a", "b"],
                              parts=[{"id": "core", "req": True, "key": "priv/t.dog-1.0.0-core.espk"}])]}
    (out6 / "catalog.json").write_text(json.dumps(old6))
    r, cat = run_pack(out6, files6)
    e = entry(cat, "t.dog") if cat else {}
    check(r.returncode == 0 and e.get("paid") is True and e.get("free_ids") == ["a", "b"], "6 paid and free_ids kept")
    check(bool(e) and e["parts"][0].get("key") == "priv/t.dog-1.0.0-core.espk", "6 the part's private key kept (same version)")
    files6b = [part(src6, dict(CORE, id="t.dog", set="dog", version="1.0.1", code=10001))]
    r, cat = run_pack(out6, files6b)
    e = entry(cat, "t.dog") if cat else {}
    check(r.returncode == 0 and e.get("paid") is True and "key" not in e["parts"][0], "6 a new version: still paid, no stale key")

# 5. the real catalog
real = ROOT / "build-pkgs/espos/s3/catalog.json"
if real.exists():
    errs = espk.check_catalog(json.loads(real.read_text()), real.parent)
    check(not errs, "5 build-pkgs/espos/s3/catalog.json" + ("" if not errs else ": " + "; ".join(errs[:5])))

print("all good" if not fails else f"{fails} failed")
sys.exit(1 if fails else 0)
