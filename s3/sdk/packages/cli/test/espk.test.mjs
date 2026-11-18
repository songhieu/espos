// The .espk file: round trip, and byte-identical to tools/espk.py (build and --install-sim).
import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import { test } from "node:test";
import { buildEspk, catalogEntry, folderFiles, installToFolder, readEspk } from "../dist/espk.js";
import { fromPlain } from "../dist/pyjson.js";
import { goodJs, goodMain, hasRoot, espos, python, ROOT, tmp, writePkg } from "./helpers.mjs";

const ESPK_PY = path.join(ROOT, "tools/espk.py");
const canPy = hasRoot && python && fs.existsSync(ESPK_PY);

test("build then read gives the same manifest and files", () => {
  const files = [
    { name: "main.js", data: Buffer.from(goodMain) },
    { name: "data/level 1.bin", data: Buffer.alloc(33, 7) },
    { name: "empty", data: Buffer.alloc(0) },
  ];
  const b = buildEspk(fromPlain(goodJs()), files);
  const r = readEspk(b, "x.espk");
  assert.deepEqual(r.problems, []);
  assert.equal(r.plain.id, "vn.test.app");
  assert.deepEqual(r.files.map((f) => [f.name, f.size, f.off % 16]), [["main.js", goodMain.length, 0], ["data/level 1.bin", 33, 0], ["empty", 0, 0]]);
  assert.deepEqual(r.files[1].data, Buffer.alloc(33, 7));
});

/** The example apps' manifests converted to the package format the way apps/pack.py does it. */
function convertedExamples(dir) {
  const r = spawnSync(python, ["-c", `
import json, shutil, sys
from pathlib import Path
out = Path(sys.argv[1]); apps = Path(sys.argv[2])
CATEGORY = {"dev.espos.dice": "game", "dev.espos.vocab": "study", "dev.espos.water": "health"}
def code_of(v):
    a, b, c = (list(map(int, v.split("."))) + [0, 0, 0])[:3]; return a * 10000 + b * 100 + c
for d in sorted(apps.iterdir()):
    if not (d / "manifest.json").exists(): continue
    m = json.loads((d / "manifest.json").read_text())
    pm = {"id": m["id"], "name": m["name"], "version": m["version"], "code": m.get("code", code_of(m["version"])),
          "kind": "js", "engine": "js", "min_fw": m.get("min_fw", 300),
          "category": m.get("category", CATEGORY.get(m["id"], "tool")), "author": m.get("author", "EspOS"),
          "desc": m.get("desc", ""), "icon": m.get("icon", {}), "perms": m.get("perms", m.get("permissions", [])), "part": "core"}
    t = out / d.name; t.mkdir()
    (t / "manifest.json").write_text(json.dumps(pm, ensure_ascii=False))
    shutil.copy(d / "main.js", t / "main.js")
    if (d / "icon.jpg").exists(): shutil.copy(d / "icon.jpg", t / "icon.jpg")
    print(d.name)
`, dir, path.join(ROOT, "apps")], { encoding: "utf8" });
  assert.equal(r.status, 0, r.stderr);
  return r.stdout.trim().split("\n");
}

function pyBuild(folder, out) {
  const r = spawnSync(python, [ESPK_PY, folder, out], { encoding: "utf8" });
  assert.equal(r.status, 0, r.stderr);
}

test("espos build is byte-identical to tools/espk.py for the example apps", { skip: !canPy && "needs python3 and the s3desk repo" }, () => {
  const t = tmp("compat");
  const names = convertedExamples(t);
  assert.ok(names.length >= 4, names.join(","));
  for (const n of names) {
    pyBuild(path.join(t, n), path.join(t, `${n}-py.espk`));
    const r = espos(["build", path.join(t, n), "--out", path.join(t, `${n}-js.espk`), "--quiet"]);
    assert.equal(r.code, 0, r.stdout + r.stderr);
    assert.deepEqual(fs.readFileSync(path.join(t, `${n}-js.espk`)), fs.readFileSync(path.join(t, `${n}-py.espk`)), n);
  }
});

test("byte-identical on a tricky folder: nested and hidden folders, Unicode names, floats, key order", { skip: !canPy && "needs python3 and the s3desk repo" }, () => {
  const t = tmp("tricky");
  const dir = path.join(t, "pkg");
  const manifest = '{"id": "vn.test.tricky", "name": "Thử ✓", "version": "1.0.0", "code": 10000, "kind": "js", "engine": "js", "min_fw": 300, "category": "tool", "part": "core", "x": {"b": 1.0, "a": 1e-05, "9": 1e16, "1": [1.5, -0.0]}, "y": "tab\\tq\\"\\u0001"}';
  writePkg(dir, manifest, {
    "main.js": goodMain,
    "a-c": "1",
    "a/b": "2",
    "Z.txt": "3",
    "é.bin": "4",
    "ẹ/ö.dat": "5",
    "data/sub/deep.bin": Buffer.alloc(100, 1),
    ".hidden": "skip",
    "data/manifest.json": "{}",
    ".dir/inside.txt": "not packed (hidden folder)",
  });
  pyBuild(dir, path.join(t, "py.espk"));
  const r = espos(["build", dir, "--out", path.join(t, "js.espk"), "--quiet"]);
  assert.equal(r.code, 0, r.stdout + r.stderr);
  assert.deepEqual(fs.readFileSync(path.join(t, "js.espk")), fs.readFileSync(path.join(t, "py.espk")));
  // no dotfiles, nothing in hidden folders, no manifest.json at any depth; Python's part-by-part order
  assert.deepEqual(folderFiles(dir).map((f) => f.name), ["Z.txt", "a/b", "a-c", "data/sub/deep.bin", "main.js", "é.bin", "ẹ/ö.dat"]);
});

test("install into a simulator data folder matches tools/espk.py --install-sim", { skip: !canPy && "needs python3 and the s3desk repo" }, () => {
  const t = tmp("install");
  const dir = writePkg(path.join(t, "pkg"), '{"id": "vn.test.inst", "name": "Cài", "version": "1.0.0", "code": 1, "kind": "js", "engine": "js", "min_fw": 300, "category": "tool", "f": 2.0, "part": "core"}', { "main.js": goodMain, "d/x.bin": "abc" });
  const pkgFile = path.join(t, "p.espk");
  pyBuild(dir, pkgFile);
  for (const vol of ["sd", "flash"]) {
    const py = path.join(t, `py-${vol}`), js = path.join(t, `js-${vol}`);
    const r = spawnSync(python, [ESPK_PY, "--install-sim", py, pkgFile, "--vol", vol], { encoding: "utf8" });
    assert.equal(r.status, 0, r.stderr);
    installToFolder(js, [readEspk(fs.readFileSync(pkgFile), pkgFile)], vol);
    const sub = vol === "sd" ? "sd/espos/pkgs/vn.test.inst" : "fs/pkgs/vn.test.inst";
    for (const f of ["manifest.json", "main.js", "d/x.bin"]) assert.deepEqual(fs.readFileSync(path.join(js, sub, f)), fs.readFileSync(path.join(py, sub, f)), `${vol} ${f}`);
  }
});

test("catalog entry: parts, req, groups with one default, base URL", () => {
  const core = readEspk(buildEspk(fromPlain(goodJs()), [{ name: "main.js", data: Buffer.from(goodMain) }]), "app-1.0.0-core.espk");
  const a = readEspk(buildEspk(fromPlain({ id: "vn.test.app", version: "1.0.0", code: 10000, part: "hd", group: "q", label: "Đẹp" }), [{ name: "hd/x", data: Buffer.from("1") }]), "app-1.0.0-hd.espk");
  const e = catalogEntry([a, core], { base: "https://x.test/apps/" });
  assert.equal(e.id, "vn.test.app");
  assert.deepEqual(e.parts.map((p) => [p.id, p.req ?? null, p.group ?? null, p.default ?? null, p.url]), [
    ["core", true, null, null, "https://x.test/apps/app-1.0.0-core.espk"],
    ["hd", null, "q", true, "https://x.test/apps/app-1.0.0-hd.espk"],
  ]);
  assert.equal(e.parts[0].sha256, core.sha256);
});

test("espos inspect --json shows manifest, files and CRCs", () => {
  const t = tmp("inspect");
  const f = path.join(t, "a.espk");
  fs.writeFileSync(f, buildEspk(fromPlain(goodJs()), [{ name: "main.js", data: Buffer.from(goodMain) }]));
  const r = espos(["inspect", f, "--json"]);
  assert.equal(r.code, 0, r.stderr);
  assert.equal(r.json.manifest.id, "vn.test.app");
  assert.equal(r.json.files[0].name, "main.js");
  assert.match(r.json.files[0].crc32, /^[0-9a-f]{8}$/);
  assert.match(r.json.sha256, /^[0-9a-f]{64}$/);
});
