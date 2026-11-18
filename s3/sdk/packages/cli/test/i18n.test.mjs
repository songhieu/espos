// Languages (docs/i18n.md): "i18n" names in manifests, the engines lang and font, app.lang in the typings. The SDK
// validator, tools/espk.py and scripts/publish_pkg.py must agree on what they refuse; espos check warns about the
// rest (tags the S3 never shows, letters it cannot draw, language packs it would not use).
import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import { test } from "node:test";
import { buildEspk, readEspk } from "../dist/espk.js";
import { fromPlain } from "../dist/pyjson.js";
import { checkPackage, ENGINES, LANG_TAGS } from "../dist/validate.js";
import { espos, goodJs, goodMain, hasRoot, here, python, ROOT, tmp, writePkg } from "./helpers.mjs";

const ESPK_PY = path.join(ROOT, "tools/espk.py");
const PUBLISH_PY = path.resolve(ROOT, "../scripts/publish_pkg.py");
const canPy = hasRoot && python && fs.existsSync(ESPK_PY);
const SKIP = !canPy && "needs python3 and the s3desk repo";

const app = (i18n, extra = {}) => goodJs({ name: "Hello", desc: "Says hello", i18n, ...extra });
const langPack = (extra = {}) => ({
  id: "dev.espos.lang.de", name: "German", version: "1.0.1", code: 10001, kind: "native", engine: "lang", min_fw: 500,
  category: "tool", author: "EspOS", desc: "German for EspOS", icon: { bg: ["#60a5fa", "#2563eb"], glyph: "globe" },
  type: "pack", lang: "de", i18n: { en: { name: "German" }, de: { name: "Deutsch" } }, part: "core", ...extra,
});
const fontPack = (extra = {}) => ({
  id: "dev.espos.font.jp", name: "Japanese font", version: "1.0.0", code: 10000, kind: "native", engine: "font", min_fw: 500,
  category: "tool", author: "EspOS", desc: "Letters for Japanese", icon: { bg: ["#60a5fa", "#2563eb"], glyph: "font" },
  type: "pack", lang: "ja", part: "core", ...extra,
});
const langFiles = { "strings.bin": "fake strings" };
const fontFiles = { "r14.bin": "fake font", "LICENSE.txt": "OFL" };

/** The SDK's problems for one package folder, and what espk.py says when it builds the same folder. */
function both(manifest, files) {
  for (const k of Object.keys(manifest)) if (manifest[k] === undefined) delete manifest[k];
  const sorted = Object.entries(files).sort(([a], [b]) => (a < b ? -1 : 1)).map(([name, d]) => ({ name, data: Buffer.from(d) }));
  const sdk = checkPackage([readEspk(buildEspk(fromPlain(manifest), sorted), "t.espk")]);
  const t = tmp("i18n");
  const dir = writePkg(path.join(t, "pkg"), manifest, files);
  const r = spawnSync(python, [ESPK_PY, dir, path.join(t, "out.espk")], { encoding: "utf8" });
  return { sdk, errors: sdk.filter((p) => p.level === "error").map((p) => p.code), warnings: sdk.filter((p) => p.level === "warning"), py: { code: r.status, out: r.stdout + r.stderr } };
}

// ---------------------------------------------------------------- what both refuse

// [what, manifest, files, the SDK's code, what espk.py says]
const BROKEN = [
  ["i18n is a list", app([{ de: "Hallo" }]), { "main.js": goodMain }, "M035", /i18n: \{"<language tag>"/],
  ["i18n is text", app("Hallo"), { "main.js": goodMain }, "M035", /i18n: \{"<language tag>"/],
  ["a language that is not an object", app({ de: "Hallo" }), { "main.js": goodMain }, "M035", /i18n\.de: \{"name"/],
  ["an empty name", app({ de: { name: "" } }), { "main.js": goodMain }, "M035", /i18n\.de\.name: text of 1 to 40 characters/],
  ["a name of spaces", app({ de: { name: "   " } }), { "main.js": goodMain }, "M035", /i18n\.de\.name: text of 1 to 40/],
  ["a name of 41 letters", app({ fr: { name: "x".repeat(41) } }), { "main.js": goodMain }, "M035", /i18n\.fr\.name: text of 1 to 40/],
  ["a name that is a number", app({ es: { name: 7 } }), { "main.js": goodMain }, "M035", /i18n\.es\.name: text of 1 to 40/],
  ["a desc of 301 letters", app({ de: { name: "Hallo", desc: "y".repeat(301) } }), { "main.js": goodMain }, "M035", /i18n\.de\.desc: text of at most 300/],
  ["the ellipsis character in a translation", app({ de: { name: "Hallo", desc: "Lädt…" } }), { "main.js": goodMain }, "M026", /no "…" in texts/],
  ["a font pack with the ellipsis character", fontPack({ i18n: { ja: { name: "日本語…" } } }), fontFiles, "M026", /no "…" in texts/],
];

for (const [what, m, files, code, pyMsg] of BROKEN)
  test(`both refuse: ${what} (${code})`, { skip: SKIP }, () => {
    const r = both(m, files);
    assert.ok(r.errors.includes(code), `SDK: ${code} expected, got ${r.errors.join(", ") || "no errors"}`);
    assert.notEqual(r.py.code, 0, `espk.py accepted it:\n${r.py.out}`);
    assert.match(r.py.out, /problems:/);
    assert.match(r.py.out, pyMsg);
  });

// ---------------------------------------------------------------- what both accept (the SDK maybe with a warning)

// [what, manifest, files, the SDK's warnings (codes, in any order)]
const ACCEPTED = [
  ["names and descriptions in three languages", app({ de: { name: "Hallo", desc: "Grüßt in der Sprache des Geräts" }, vi: { name: "Xin chào", desc: "Chào bằng tiếng của máy" }, ru: { name: "Привет", desc: "Здоровается на языке устройства" } }), { "main.js": goodMain }, []],
  ["only names, an English one too, and every tag the S3 shows", app(Object.fromEntries(LANG_TAGS.map((t) => [t, { name: `Hello ${t}` }]))), { "main.js": goodMain }, []],
  ["Chinese, Japanese and Korean (their font packs are not checked letter by letter)", app({ "zh-Hans": { name: "你好" }, ja: { name: "こんにちは" }, ko: { name: "안녕하세요" } }), { "main.js": goodMain }, []],
  ["an empty i18n", app({}), { "main.js": goodMain }, []],
  ["a tag the S3 does not show (pt)", app({ pt: { name: "Olá" } }), { "main.js": goodMain }, ["M036"]],
  ["a tag in the wrong case (zh-hans)", app({ "zh-hans": { name: "你好" } }), { "main.js": goodMain }, ["M036"]],
  ["a field the device does not read", app({ de: { name: "Hallo", title: "Hallo" } }), { "main.js": goodMain }, ["M036"]],
  ["German with a letter outside Latin-1", app({ de: { name: "Łódź" } }), { "main.js": goodMain }, ["M037"]],
  ["Russian written in Ukrainian letters", app({ ru: { name: "Привіт" } }), { "main.js": goodMain }, ["M037"]],
  ["Vietnamese in a Spanish entry", app({ es: { name: "Tiếng Việt", desc: "Chào bằng tiếng của máy" } }), { "main.js": goodMain }, ["M037", "M037"]],
  ["a language pack like tools/i18n.py makes", langPack(), langFiles, []],
  ["a font pack like tools/i18n.py makes", fontPack(), fontFiles, []],
  ["a language pack whose id the device never looks up", langPack({ id: "vn.you.lang.de" }), langFiles, ["M038"]],
  ["a language pack for a language the S3 lacks", langPack({ id: "dev.espos.lang.it", lang: "it" }), langFiles, ["M038"]],
  ["a language pack that is not a pack", langPack({ type: undefined }), langFiles, ["M038"]],
  ["a language pack naming another font", langPack({ id: "dev.espos.lang.ja", lang: "ja", font: "dev.espos.font.sc" }), langFiles, ["M038"]],
  ["a font pack under another id", fontPack({ id: "dev.espos.font.jp2" }), fontFiles, ["M038"]],
  ["a language pack for firmware 0.3.0", langPack({ min_fw: 300 }), langFiles, ["M016"]],
  ["a language pack without strings.bin", langPack(), { "other.bin": "x" }, ["F011"]],
  ["a font pack without font files", fontPack(), { "LICENSE.txt": "OFL" }, ["F011"]],
];

for (const [what, m, files, warns] of ACCEPTED)
  test(`both accept: ${what}${warns.length ? ` (SDK warns ${[...new Set(warns)].join(", ")})` : ""}`, { skip: SKIP }, () => {
    const r = both(m, files);
    assert.equal(r.py.code, 0, `espk.py refused it:\n${r.py.out}`);
    assert.deepEqual(r.errors, [], JSON.stringify(r.sdk));
    assert.deepEqual(r.warnings.map((p) => p.code).sort(), [...warns].sort(), JSON.stringify(r.warnings));
  });

test("a wrong tag gets a suggestion", () => {
  const msgs = (i18n) => checkPackage([readEspk(buildEspk(fromPlain(app(i18n)), [{ name: "main.js", data: Buffer.from(goodMain) }]), "t.espk")]).filter((p) => p.code === "M036").map((p) => p.msg).join("\n");
  assert.match(msgs({ pt: { name: "Olá" } }), /did you mean "pt-BR"/);
  assert.match(msgs({ zh: { name: "你好" } }), /did you mean "zh-Hans"/);
  assert.match(msgs({ ZH_HANS: { name: "你好" } }), /did you mean "zh-Hans"/);
  assert.doesNotMatch(msgs({ it: { name: "Ciao" } }), /did you mean/);
});

test("only the language packs' ids may have capitals", () => {
  const codes = (m, files) => checkPackage([readEspk(buildEspk(fromPlain(m), Object.entries(files).map(([name, d]) => ({ name, data: Buffer.from(d) }))), "t.espk")]).filter((p) => p.level === "error").map((p) => p.code);
  assert.deepEqual(codes(langPack({ id: "dev.espos.lang.pt-BR", lang: "pt-BR" }), langFiles), []);
  assert.deepEqual(codes(langPack({ id: "dev.espos.lang.zh-Hans", lang: "zh-Hans", font: "dev.espos.font.sc" }), langFiles), []);
  assert.ok(codes(goodJs({ id: "vn.test.App" }), { "main.js": goodMain }).includes("M001"));
});

// ---------------------------------------------------------------- the same lists everywhere

test("the language tags are the firmware's, tools/espk.py's and publish_pkg.py's", { skip: SKIP }, () => {
  const cpp = fs.readFileSync(path.join(ROOT, "src/i18n.cpp"), "utf8");
  const table = cpp.slice(cpp.indexOf("const Lang LANGS[] = {"), cpp.indexOf("};", cpp.indexOf("const Lang LANGS[] = {")));
  assert.deepEqual([...table.matchAll(/\{"([A-Za-z-]+)",/g)].map((m) => m[1]), LANG_TAGS);
  const py = spawnSync(python, ["-c", "import sys, json; sys.path.insert(0, sys.argv[1]); import espk; print(json.dumps(list(espk.LANG_TAGS))); print(json.dumps(sorted(espk.ENGINES)))", path.join(ROOT, "tools")], { encoding: "utf8" });
  const [tags, engines] = py.stdout.trim().split("\n").map((s) => JSON.parse(s));
  assert.deepEqual(tags, LANG_TAGS);
  assert.deepEqual(engines, [...ENGINES].sort());
  if (fs.existsSync(PUBLISH_PY)) assert.match(fs.readFileSync(PUBLISH_PY, "utf8"), new RegExp(`LANG_TAGS = \\(${LANG_TAGS.map((t) => `"${t}"`).join(", ")}\\)`));
});

test("the manifest schema lists the language tags and the engines lang and font", () => {
  const s = JSON.parse(fs.readFileSync(path.resolve(here, "../../sdk/schemas/manifest.schema.json"), "utf8"));
  assert.deepEqual(s.properties.i18n.propertyNames.enum, LANG_TAGS);
  assert.deepEqual(s.properties.lang.enum, LANG_TAGS.slice(1));
  assert.ok(s.properties.engine.enum.includes("lang") && s.properties.engine.enum.includes("font"));
  const id = new RegExp(s.properties.id.pattern);
  assert.ok(id.test("dev.espos.lang.pt-BR") && id.test("vn.you.app") && !id.test("vn.You.app"));
});

test("the typings give app.lang the firmware's tags, and app.screen", { skip: !hasRoot && "no repo" }, () => {
  const dts = fs.readFileSync(path.resolve(here, "../../sdk/espos.d.ts"), "utf8");
  const appDecl = dts.slice(dts.indexOf("interface EspOSApp {"), dts.indexOf("}", dts.indexOf("readonly lang")));
  assert.match(appDecl, /readonly lang: EspOSLang;/);
  assert.match(appDecl, /readonly screen: EspOSScreen;/);
  const union = dts.match(/type EspOSLang = ([^;]+);/)[1];
  assert.deepEqual([...union.matchAll(/"([^"]+)"/g)].map((m) => m[1]), LANG_TAGS);
  const cpp = fs.readFileSync(path.join(ROOT, "src/js/jsapp.cpp"), "utf8");
  assert.match(cpp, /JS_SetPropertyStr\(run\.ctx, a, "lang", JS_NewString\(run\.ctx, i18n::lang\(\)\)\)/);
  assert.match(cpp, /JS_SetPropertyStr\(run\.ctx, a, "screen", sc\)/);
  assert.doesNotMatch(dts, /Ứng dụng|0\.3\.0 to 0\.4\.0/);
});

// ---------------------------------------------------------------- the real packages and the catalog entry

const REAL_DIR = path.join(ROOT, "build-pkgs/espos/s3");
const REAL = canPy && fs.existsSync(REAL_DIR) ? fs.readdirSync(path.join(ROOT, "build-pkgs/espos/s3")).filter((n) => /^dev\.espos\.(lang|font)\..+\.espk$/.test(n)).map((n) => path.join(ROOT, "build-pkgs/espos/s3", n)) : [];

test("the language and font packs tools/i18n.py built pass both validators with no warning", { skip: (!REAL.length && "needs build-pkgs/espos/s3 (tools/i18n.py build all, font all)") || false }, () => {
  const py = spawnSync(python, [ESPK_PY, "--check", ...REAL], { encoding: "utf8" });
  assert.equal(py.status, 0, py.stdout + py.stderr);
  for (const f of REAL) {
    const p = checkPackage([readEspk(fs.readFileSync(f), path.basename(f))]).filter((x) => x.level !== "info");
    assert.deepEqual(p, [], `${path.basename(f)}: ${JSON.stringify(p)}`);
  }
});

test("espos store entry keeps i18n, lang and font like tools/espk.py --entry", { skip: SKIP }, () => {
  const t = tmp("entry");
  const dir = writePkg(path.join(t, "lang"), langPack(), langFiles);
  const b = espos(["build", dir, "--json"]);
  assert.equal(b.code, 0, b.stdout + b.stderr);
  const e = espos(["store", "entry", b.json.out, "--base", "https://example.test/apps", "--json"]);
  assert.equal(e.code, 0, e.stderr);
  const py = spawnSync(python, [ESPK_PY, "--entry", "https://example.test/apps", b.json.out], { encoding: "utf8" });
  assert.equal(py.status, 0, py.stderr);
  const sdk = JSON.parse(e.stdout);
  const pye = JSON.parse(py.stdout);
  for (const k of ["i18n", "lang", "engine", "type", "min_fw"]) assert.deepEqual(sdk[k], pye[k], k);
  assert.deepEqual(sdk.i18n, langPack().i18n);
});

test("store publish (dry run) keeps i18n in the catalog and takes a language pack's id", { skip: !(canPy && fs.existsSync(PUBLISH_PY)) && "needs scripts/publish_pkg.py" }, () => {
  const t = tmp("pub");
  const a = espos(["build", writePkg(path.join(t, "app"), app({ de: { name: "Hallo" } }), { "main.js": goodMain }), "--json"]);
  const l = espos(["build", writePkg(path.join(t, "lang"), langPack({ id: "dev.espos.lang.pt-BR", lang: "pt-BR", i18n: { "pt-BR": { name: "Português (Brasil)" } } }), langFiles), "--json"]);
  fs.writeFileSync(path.join(t, "catalog.json"), JSON.stringify({ packages: [] }));
  for (const [b, id, i18n] of [[a, "vn.test.app", { de: { name: "Hallo" } }], [l, "dev.espos.lang.pt-BR", { "pt-BR": { name: "Português (Brasil)" } }]]) {
    const p = espos(["store", "publish", b.json.out, "--catalog", path.join(t, "catalog.json"), "--out", path.join(t, "merged.json")], { env: { ESPOS_BBK: "" } });
    assert.equal(p.code, 0, p.stdout + p.stderr);
    const e = JSON.parse(fs.readFileSync(path.join(t, "merged.json"), "utf8")).packages.find((x) => x.id === id);
    assert.deepEqual(e.i18n, i18n);
  }
  // publish_pkg.py checks i18n itself too (a package made without the tools)
  const bad = path.join(t, "bad.espk");
  fs.writeFileSync(bad, buildEspk(fromPlain(app({ de: { name: "" } })), [{ name: "main.js", data: Buffer.from(goodMain) }]));
  const r = spawnSync(python, [PUBLISH_PY, "--dry-run", "--catalog", path.join(t, "catalog.json"), bad], { encoding: "utf8" });
  assert.notEqual(r.status, 0, r.stdout + r.stderr);
  assert.match(r.stdout + r.stderr, /i18n\.de\.name: text of 1 to 40 characters/);
});

test("publish_pkg.py keeps i18n in a v2 entry too (faces and widgets)", { skip: !(canPy && fs.existsSync(PUBLISH_PY)) && "needs scripts/publish_pkg.py" }, () => {
  const t = tmp("pub2");
  const widget = { id: "vn.test.quote", name: "Quote", version: "1.0.0", code: 10000, type: "widget", runtime: "decl", entry: { widget: "w.json" },
    author: "Test", desc: "A saying a day", i18n: { de: { name: "Zitat" } }, part: "core" };
  const file = path.join(t, "w.espk");
  fs.writeFileSync(file, buildEspk(fromPlain(widget), [{ name: "w.json", data: Buffer.from(JSON.stringify({ decl: 1, layouts: [{ size: [456, 210] }] })) }]));
  fs.writeFileSync(path.join(t, "v2.json"), JSON.stringify({ v: 2, packages: [] }));
  const r = spawnSync(python, [PUBLISH_PY, "--dry-run", "--catalog", path.join(t, "v2.json"), "--out", path.join(t, "merged.json"), file], { encoding: "utf8" });
  assert.equal(r.status, 0, r.stdout + r.stderr);
  const e = JSON.parse(fs.readFileSync(path.join(t, "merged.json"), "utf8")).packages.find((x) => x.id === "vn.test.quote");
  assert.deepEqual(e.i18n, { de: { name: "Zitat" } });
});
