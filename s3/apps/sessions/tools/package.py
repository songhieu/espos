#!/usr/bin/env python3
"""The Sessions app as one .espk, the same way apps/pack.py packs it for the store (manifest in the package format,
main.js, the pkg/ files): for the simulator and QEMU tests.

    python3 apps/sessions/tools/package.py OUT.espk     (from s3desk; run tools/build.py first)
"""
import json
import shutil
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HERE.parents[1] / "tools"))
import espk  # noqa: E402


def main(out):
    m = json.loads((HERE / "manifest.json").read_text())
    a, b, c = (list(map(int, m["version"].split("."))) + [0, 0, 0])[:3]
    pm = {"id": m["id"], "name": m["name"], "version": m["version"], "code": m.get("code", a * 10000 + b * 100 + c),
          "kind": "js", "engine": "js", "min_fw": m.get("min_fw", 300), "category": m.get("category", "tool"),
          "author": m.get("author", "EspOS"), "desc": m.get("desc", ""), "icon": m.get("icon", {}),
          "perms": m.get("perms", []), "part": "core"}
    for k in ("i18n", "settings", "auth", "api"):
        if m.get(k):
            pm[k] = m[k]
    with tempfile.TemporaryDirectory() as tmp:
        t = Path(tmp)
        (t / "manifest.json").write_text(json.dumps(pm, ensure_ascii=False))
        shutil.copy(HERE / "main.js", t / "main.js")
        shutil.copytree(HERE / "pkg", t, dirs_exist_ok=True)
        f = espk.build(t, Path(out))
    print(f"{out}: {f.stat().st_size} B")


if __name__ == "__main__":
    main(sys.argv[1])
