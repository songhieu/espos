// Declarative packages (faces, widgets, themes; docs/profiles.md §3-4): the SDK validator and tools/espk.py must
// agree. Every case runs through both: checkPackage here, and espk.py building the same folder (its build runs
// check_manifest and check_decl_file; --check reads only the manifest).
import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import { test } from "node:test";
import { buildEspk, readEspk } from "../dist/espk.js";
import { fromPlain } from "../dist/pyjson.js";
import { checkPackage } from "../dist/validate.js";
import { hasRoot, espos, python, ROOT, tmp, writePkg } from "./helpers.mjs";

const ESPK_PY = path.join(ROOT, "tools/espk.py");
const canPy = hasRoot && python && fs.existsSync(ESPK_PY);
const SKIP = !canPy && "needs python3 and the s3desk repo";
const SAMPLES = ["dev.espos.face.bigclock-1.0.0.espk", "dev.espos.face.today-1.0.0.espk", "dev.espos.widget.quote-1.0.0.espk"].map((n) => path.join(ROOT, "build-pkgs", n));
const SAMPLE_DIRS = ["x4-face-bigclock", "x4-face-today", "x4-widget-quote"].map((n) => path.join(ROOT, "build-pkgs", n));
const haveSamples = canPy && SAMPLES.every((f) => fs.existsSync(f));

// ---------------------------------------------------------------- a good face, and ways to break it

const face = (extra = {}) => ({
  id: "vn.test.face", name: "Mặt thử", version: "1.0.0", code: 10000, type: "face", runtime: "decl", api: { min: 1, target: 1 },
  requires: [], screens: ["800x480", "480x800"], refresh: "minute", perms: ["data:weather", "data:calendar"], author: "Test",
  desc: "Một mặt màn chờ để thử", entry: { face: "face.json" }, part: "core", ...extra,
});
const text = (s, extra = {}) => ({ t: "text", x: 0, y: 0, w: 400, h: 40, text: s, size: 25, ...extra });
// group > list > group: three deep, the most there may be
const items = () => [
  text("{time.hm}", { y: 40, w: 800, h: 140, size: 125, font: "digits", bold: true, align: "center" }),
  { t: "line", x: 200, y: 286, x2: 600, y2: 286, lw: 3 },
  { t: "box", x: 10, y: 10, w: 100, h: 50, fill: "muted", r: 6 },
  { t: "circle", x: 50, y: 50, r: 10, stroke: "ink" },
  { t: "bar", x: 10, y: 400, w: 300, h: 12, value: "{battery.pct}" },
  { t: "image", x: 700, y: 10, w: 80, h: 80, src: "img/logo.bmp" },
  { t: "image", x: 700, y: 100, w: 80, h: 80, src: "img/{weather.icon}.bmp" }, // a binding: resolved on the device
  { t: "group", if: "weather.ok", x: 0, y: 300, items: [
    { t: "icon", x: 250, y: 0, w: 80, h: 80, name: "{weather.icon}" },
    { t: "list", of: "weather.days", x: 0, y: 90, dy: 30, items: [{ t: "group", items: [text("{item.d} {item.min}°")] }] },
  ] },
];
const layoutDoc = () => ({ decl: 1, layouts: [{ size: [800, 480], bg: "paper", items: items() }, { size: [480, 800], items: items() }] });
const faceFiles = (doc = layoutDoc()) => ({ "face.json": typeof doc === "string" ? doc : JSON.stringify(doc), "img/logo.bmp": "BM fake bitmap" });

/** Both validators on one package folder: {sdk: error codes, py: {code, stderr}}. */
function both(manifest, files) {
  const sdk = checkPackage([readEspk(buildEspk(fromPlain(manifest), Object.entries(files).sort(([a], [b]) => (a < b ? -1 : 1)).map(([name, d]) => ({ name, data: Buffer.from(d) }))), "t.espk")]);
  const t = tmp("decl");
  const dir = writePkg(path.join(t, "pkg"), manifest, files);
  const r = spawnSync(python, [ESPK_PY, dir, path.join(t, "out.espk")], { encoding: "utf8" });
  return { sdk, errors: sdk.filter((p) => p.level === "error").map((p) => p.code), py: { code: r.status, out: r.stdout + r.stderr } };
}

const deep = (n) => {
  // n groups inside one another, a text in the innermost
  let it = text("x");
  for (let k = 0; k < n; k++) it = { t: "group", items: [it] };
  return it;
};
const withItems = (extra) => {
  const d = layoutDoc();
  d.layouts[0].items.push(...extra);
  return d;
};

// [what, manifest, files, the SDK's code, what espk.py says]
const BROKEN = [
  ["runtime missing", face({ runtime: undefined }), faceFiles(), "M040", /runtime: "decl"/],
  ["runtime js", face({ runtime: "js" }), faceFiles(), "M040", /runtime: "decl"/],
  ["api min 2 (a level no device has)", face({ api: { min: 2, target: 2 } }), faceFiles(), "M041", /api: \{"min": 1\.\.1/],
  ["api not an object", face({ api: 1 }), faceFiles(), "M041", /api: \{"min"/],
  ["requires an unknown capability", face({ requires: ["screen.colour"] }), faceFiles(), "M042", /requires: a list of/],
  ["bad screen string", face({ screens: ["800*480", "480x800"] }), faceFiles(), "M043", /screens: sizes like 800x480, not '800\*480'/],
  ["unknown perm", face({ perms: ["data:weather", "data:stocks"] }), faceFiles(), "M045", /perms: unknown 'data:stocks'/],
  ["an app perm", face({ perms: ["sound"] }), faceFiles(), "M045", /perms: unknown 'sound'/],
  ["bad refresh", face({ refresh: "second" }), faceFiles(), "M044", /refresh: one of/],
  ["entry missing", face({ entry: undefined }), faceFiles(), "M046", /entry: \{"face": "face\.json"\}/],
  ["entry missing: no layout file either", face({ entry: undefined }), faceFiles(), "F020", /entry: the layout file is missing/],
  ["entry under another type", face({ entry: { widget: "face.json" } }), faceFiles(), "M046", /entry: \{"face"/],
  ["entry names a file that is not there", face({ entry: { face: "other.json" } }), faceFiles(), "F020", /other\.json: the layout file is missing/],
  ["name missing", face({ name: undefined }), faceFiles(), "M010", /name: missing/],
  ['"…" in the manifest', face({ desc: "Đang tải…" }), faceFiles(), "M026", /no "…" in texts/],
  ["layout file over 12 KB", face(), faceFiles({ ...layoutDoc(), lists: { pad: ["x".repeat(12300)] } }), "F021", /bytes, at most 12288/],
  ["layout file not JSON", face(), faceFiles('{"decl": 1, "layouts": ['), "F022", /face\.json: not JSON/],
  ["layout without decl", face(), faceFiles({ layouts: layoutDoc().layouts }), "F023", /"decl": 1 is missing/],
  ["layout of level 2", face(), faceFiles({ ...layoutDoc(), decl: 2 }), "F023", /"decl": 1 is missing/],
  ["no layouts", face({ screens: undefined }), faceFiles({ decl: 1, layouts: [] }), "F024", /needs a list of layouts/],
  ["layout size not [w, h]", face({ screens: undefined }), faceFiles({ decl: 1, layouts: [{ size: [800], items: [] }] }), "F025", /layouts\[0\]\.size must be \[w, h\]/],
  ["layout size 0", face({ screens: undefined }), faceFiles({ decl: 1, layouts: [{ size: [0, 480], items: [] }] }), "F025", /size must be \[w, h\]/],
  ["layout size written as a float", face({ screens: ["800x480"] }), faceFiles('{"decl": 1, "layouts": [{"size": [800.0, 480], "items": []}]}'), "F025", /size must be \[w, h\]/],
  ["unknown item type", face(), faceFiles(withItems([{ t: "star", x: 0, y: 0 }])), "F026", /items\[\d+\]: t must be one of/],
  ["an item that is not an object", face(), faceFiles(withItems(["text"])), "F026", /t must be one of/],
  ["items not a list", face(), faceFiles(withItems([{ t: "group", items: { t: "text" } }])), "F026", /items must be a list/],
  ["nesting 4 deep", face(), faceFiles(withItems([deep(4)])), "F028", /lists and groups go 3 deep at most/],
  ["screens do not match the layouts", face({ screens: ["800x480"] }), faceFiles(), "F030", /screens \['800x480'\] do not match the layouts/],
  ["screens list a layout twice", face({ screens: ["800x480", "800x480", "480x800"] }), faceFiles(), "F030", /do not match the layouts/],
  ["missing image", face(), faceFiles(withItems([{ t: "image", x: 0, y: 0, w: 10, h: 10, src: "img/none.bmp" }])), "F027", /picture 'img\/none\.bmp' is not in the package/],
  ['"…" in an item', face(), faceFiles(withItems([text("Đang tải…")])), "F029", /no "…" in texts/],
  ['"…" deep in a group', face(), faceFiles(withItems([{ t: "group", items: [{ t: "list", of: "cal.events", items: [text("{item.title}…")] }] }])), "F029", /no "…" in texts/],
  ["type service (not declarative)", face({ type: "service" }), faceFiles(), "M024", /type: "app" \(the default\) or "pack"/],
];

for (const [what, m, files, code, pyMsg] of BROKEN)
  test(`both refuse: ${what} (${code})`, { skip: SKIP }, () => {
    for (const k of Object.keys(m)) if (m[k] === undefined) delete m[k];
    const r = both(m, files);
    assert.ok(r.errors.includes(code), `SDK: ${code} expected, got ${r.errors.join(", ") || "no errors"}`);
    assert.notEqual(r.py.code, 0, `espk.py accepted it:\n${r.py.out}`);
    assert.match(r.py.out, /problems:/, `espk.py crashed instead of listing problems:\n${r.py.out}`);
    assert.match(r.py.out, pyMsg);
  });

test("stricter than espk.py: a BOM in the layout file (the device's reader does not skip it), decl written as 1.0 or true", () => {
  const sdk = (doc) => checkPackage([readEspk(buildEspk(fromPlain(face()), [{ name: "face.json", data: Buffer.from(doc) }]), "t.espk")]).filter((p) => p.level === "error").map((p) => p.code);
  const good = JSON.stringify(layoutDoc()).replace(/"img\/logo\.bmp"/g, '"img/{x}.bmp"');
  assert.deepEqual(sdk(good), []);
  assert.ok(sdk("﻿" + good).includes("F022"), "BOM");
  assert.ok(sdk(good.replace('"decl":1', '"decl":1.0')).includes("F023"));
  assert.ok(sdk(good.replace('"decl":1', '"decl":true')).includes("F023"));
});

// [what, manifest, files]: both accept, and the SDK has no warnings either
const GOOD = [
  ["a face with two layouts, pictures, bindings, nesting 3 deep", face(), faceFiles()],
  ["a widget with every optional field left out", { id: "vn.test.widget", name: "Tiện ích", version: "1.0.0", code: 1, type: "widget", runtime: "decl", entry: { widget: "w.json" }, part: "core" },
    { "w.json": JSON.stringify({ decl: 1, layouts: [{ size: [456, 210] }] }) }],
  ["a face without screens", face({ screens: undefined, refresh: "data", perms: [] }), faceFiles()],
  ["a face asking for a colour screen and data:markets", face({ requires: ["screen.color"], perms: ["data:markets", "data:reading", "data:news"] }), faceFiles()],
  ["an image with no src (draws nothing)", face(), faceFiles(withItems([{ t: "image", x: 0, y: 0, w: 1, h: 1 }]))],
  ["a theme (its file is not checked)", { id: "vn.test.theme", name: "Giấy", version: "1.0.0", code: 1, type: "theme", runtime: "decl", api: { min: 1 }, entry: { theme: "theme.json" }, part: "core" },
    { "theme.json": JSON.stringify({ decl: 1, eink: { skin: "line", bars: "invert" } }) }],
];

for (const [what, m, files] of GOOD)
  test(`both accept: ${what}`, { skip: SKIP }, () => {
    for (const k of Object.keys(m)) if (m[k] === undefined) delete m[k];
    const r = both(m, files);
    assert.equal(r.py.code, 0, `espk.py refused it:\n${r.py.out}`);
    assert.deepEqual(r.sdk.filter((p) => p.level !== "info"), []);
  });

// ---------------------------------------------------------------- the real sample packages

test("the sample faces and widget pass both validators (.espk files and folders)", { skip: !haveSamples && "needs python3, the s3desk repo and build-pkgs/" }, () => {
  for (const f of SAMPLES) {
    const ps = checkPackage([readEspk(fs.readFileSync(f), f)]);
    assert.deepEqual(ps.filter((p) => p.level === "error"), [], path.basename(f));
  }
  const py = spawnSync(python, [ESPK_PY, "--check", ...SAMPLES], { encoding: "utf8" });
  assert.equal(py.status, 0, py.stderr);
  const k = espos(["check", ...SAMPLES, "--json"]);
  assert.equal(k.code, 0, k.stdout + k.stderr);
  assert.equal(k.json.ok, true);
  assert.deepEqual(k.json.results.map((r) => r.errors), [0, 0, 0]);
  // the folders they were built from: espk.py's build (all its decl checks) and espos build make the same bytes
  for (const dir of SAMPLE_DIRS.filter((d) => fs.existsSync(path.join(d, "manifest.json")))) {
    const t = tmp("sample");
    const r = spawnSync(python, [ESPK_PY, dir, path.join(t, "py.espk")], { encoding: "utf8" });
    assert.equal(r.status, 0, r.stderr);
    const b = espos(["build", dir, "--out", path.join(t, "js.espk"), "--quiet"]);
    assert.equal(b.code, 0, b.stdout + b.stderr);
    assert.deepEqual(fs.readFileSync(path.join(t, "js.espk")), fs.readFileSync(path.join(t, "py.espk")), path.basename(dir));
  }
});
