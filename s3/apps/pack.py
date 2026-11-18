"""Packs each app folder (manifest.json + main.js [+ icon.jpg]) for the store and writes catalog.json.
Run from s3desk: python3 apps/pack.py OUTDIR BASE_URL [EXTRA.espk ...]

Writes <id>-<version>-core.espk for every app (tools/espk.py, docs/packages.md) and catalog.json ("packages").
EXTRA .espk files (native packages such as vn.pawboop.pet, all parts of one package together) are added to
"packages" with their sizes and SHA-256; apps/catalog-text.json gives some of them English store texts.
Names and descriptions in other languages: "i18n": {"vi": {"name": ..., "desc": ...}} in a manifest (docs/i18n.md).
The languages (tools/i18n.py build all) and fonts (tools/i18n.py font all) are such EXTRA packages too."""
import hashlib
import json
import shutil
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
import espk  # noqa: E402

CATEGORY = {"dev.espos.dice": "game", "dev.espos.vocab": "study", "dev.espos.water": "health"}


def code_of(version):
    a, b, c = (list(map(int, version.split("."))) + [0, 0, 0])[:3]
    return a * 10000 + b * 100 + c


out, base = Path(sys.argv[1]), sys.argv[2].rstrip("/")
extra = sys.argv[3:]
out.mkdir(parents=True, exist_ok=True)
# The catalog as it was: a package of the same version keeps what it said about its parts' choices and its add-on
# place even when its .espk files do not carry it (run 1.0.1's groups lived only in its builder's list).
old = {}
if (out / "catalog.json").exists():
    old = {e["id"]: e for e in json.loads((out / "catalog.json").read_text()).get("packages", [])}
catalog = {"packages": []}
for d in sorted(p for p in Path(__file__).parent.iterdir() if (p / "manifest.json").exists()):
    m = json.loads((d / "manifest.json").read_text())
    # The .espk: the manifest in the package format.
    pm = {"id": m["id"], "name": m["name"], "version": m["version"], "code": m.get("code", code_of(m["version"])),
          "kind": "js", "engine": "js", "min_fw": m.get("min_fw", 300),
          "category": m.get("category", CATEGORY.get(m["id"], "tool")), "author": m.get("author", "EspOS"),
          "desc": m.get("desc", ""), "icon": m.get("icon", {}),
          "perms": m.get("perms", m.get("permissions", [])), "part": "core"}
    if m.get("i18n"):  # names and descriptions in other languages (docs/i18n.md)
        pm["i18n"] = m["i18n"]
    with tempfile.TemporaryDirectory() as tmp:
        t = Path(tmp)
        (t / "manifest.json").write_text(json.dumps(pm, ensure_ascii=False))
        shutil.copy(d / "main.js", t / "main.js")
        if (d / "icon.jpg").exists():
            shutil.copy(d / "icon.jpg", t / "icon.jpg")
            shutil.copy(d / "icon.jpg", out / f"{m['id']}-{m['version']}.jpg")
        f = espk.build(t, out / f"{m['id']}-{m['version']}-core.espk")
    b = f.read_bytes()
    e = {k: pm[k] for k in ("id", "name", "version", "code", "kind", "engine", "min_fw", "category", "author", "desc",
                            "icon", "perms", "i18n") if k in pm}
    if (out / f"{m['id']}-{m['version']}.jpg").exists():
        e["icon_url"] = f"{base}/{m['id']}-{m['version']}.jpg"
    e["parts"] = [{"id": "core", "req": True, "url": f"{base}/{f.name}", "size": len(b),
                   "sha256": hashlib.sha256(b).hexdigest()}]
    catalog["packages"].append(e)
    print(f"{f.name:44} {len(b):6} B")

# Native packages built elsewhere (tools/espk.py), grouped by id.
groups = {}
for p in extra:
    m, files, b = espk.read(p)
    groups.setdefault(m["id"], []).append(p)
for pid, paths in groups.items():
    paths.sort(key=lambda p: 0 if espk.read(p)[0].get("part", "core") == "core" else 1)
    for p in paths:
        shutil.copy(p, out / Path(p).name) if Path(p).resolve().parent != out.resolve() else None
    e = espk.entry(base, paths)
    # Part choices kept beside the parts by the package's builder (<id>-<version>-catalog.json, e.g. the run game's
    # roads10/roads20 group): never lost when the catalog is built again from the .espk files.
    for d in {Path(x).resolve().parent for x in paths} | {Path(__file__).resolve().parent.parent / "build-pkgs"}:
        side = d / f"{e['id']}-{e['version']}-catalog.json"
        if side.exists():
            sp = {q["id"]: q for q in json.loads(side.read_text()).get("parts", [])}
            for part in e["parts"]:
                for k in espk.PART_KEYS:
                    if k in sp.get(part["id"], {}):
                        part.setdefault(k, sp[part["id"]][k])
            break
    # The store's picture: <id>-<version>.jpg next to the parts (120x120 baseline JPEG).
    jpg = Path(paths[0]).parent / f"{e['id']}-{e['version']}.jpg"
    if jpg.exists():
        if jpg.resolve().parent != out.resolve():
            shutil.copy(jpg, out / jpg.name)
        e["icon_url"] = f"{base}/{jpg.name}"
    catalog["packages"].append(e)

# Store texts for packages built elsewhere (apps/catalog-text.json): English, with "i18n" for other languages.
overlay = json.loads((Path(__file__).parent / "catalog-text.json").read_text())
for e in catalog["packages"]:
    for k, v in overlay.get(e["id"], {}).items():
        if k == "parts":  # part texts by part id (English label, "labels" in other languages): merged, never replacing
            for part in e["parts"]:
                part.update({pk: pv for pk, pv in v.get(part["id"], {}).items() if pk in espk.PART_KEYS})
        else:
            e[k] = v

for e in catalog["packages"]:
    o = old.get(e["id"])
    if not o or o.get("code") != e.get("code"):
        continue
    op = {q["id"]: q for q in o.get("parts", [])}
    for part in e["parts"]:
        for k in espk.PART_KEYS:
            if k in op.get(part["id"], {}):
                part.setdefault(k, op[part["id"]][k])
    for k in ("addon_of", "set", "needs"):
        if k in o:
            e.setdefault(k, o[k])

errs = espk.check_catalog(catalog, out)
if errs:
    sys.exit("catalog.json refused:\n  " + "\n  ".join(errs))

(out / "catalog.json").write_text(json.dumps(catalog, ensure_ascii=False, indent=1))
print(f"catalog.json: {len(catalog['packages'])} packages")
