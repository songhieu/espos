// The one validator: good packages pass, each rule fires on its bad case.
import assert from "node:assert/strict";
import { test } from "node:test";
import { buildEspk, crc32, readEspk } from "../dist/espk.js";
import { fromPlain, parse, PyObj } from "../dist/pyjson.js";
import { checkCode, checkPackage, RULES } from "../dist/validate.js";
import { goodJs, goodMain } from "./helpers.mjs";

const pkg = (m, files = { "main.js": goodMain }) => {
  const fs = Object.entries(files).map(([name, d]) => ({ name, data: Buffer.from(d) }));
  return readEspk(buildEspk(m instanceof PyObj ? m : fromPlain(m), fs), "t.espk");
};
const codes = (ps, level) => ps.filter((p) => !level || p.level === level).map((p) => p.code);
const errorsOf = (m, files) => codes(checkPackage([pkg(m, files)]), "error");
const warningsOf = (m, files) => codes(checkPackage([pkg(m, files)]), "warning");

// a 120x120 baseline JPEG header (SOF0) and variants
const jpeg = (sof = 0xc0, w = 120, h = 120) =>
  Buffer.from([0xff, 0xd8, 0xff, 0xe0, 0, 16, 0x4a, 0x46, 0x49, 0x46, 0, 1, 1, 0, 0, 1, 0, 1, 0, 0, 0xff, sof, 0, 11, 8, h >> 8, h & 255, w >> 8, w & 255, 1, 1, 0x11, 0, 0xff, 0xd9]);

test("a good JavaScript package has no errors or warnings", () => {
  const ps = checkPackage([pkg(goodJs(), { "main.js": goodMain, "icon.jpg": jpeg() })]);
  assert.deepEqual(ps.filter((p) => p.level !== "info"), []);
});

test("every rule has a code, a level and a source", () => {
  const seen = new Set();
  for (const r of RULES) {
    assert.match(r.code, /^[MFCPU]\d{3}$/); // U: the private-upload rules (espos upload)
    assert.ok(["error", "warning", "info"].includes(r.level));
    assert.ok(r.text && r.from);
    assert.ok(!seen.has(r.code), r.code);
    seen.add(r.code);
  }
});

const BAD = [
  ["part missing", { part: undefined }, "M027"],
  ["id uppercase", { id: "vn.Test.app" }, "M001"],
  ["id too long", { id: "vn." + "a".repeat(46) }, "M001"],
  ["id with slash", { id: "vn/x" }, "M001"],
  ["name empty", { name: "" }, "M010"],
  ["name 41 characters", { name: "x".repeat(41) }, "M010"],
  ["version with uppercase", { version: "1.0.0-Beta" }, "M011"],
  ["version not text", { version: 1 }, "M011"],
  ["code zero", { code: 0 }, "M012"],
  ["code bool", { code: true }, "M012"],
  ["code too big", { code: 2_000_000_000 }, "M012"],
  ["kind unknown", { kind: "wasm" }, "M013"],
  ["engine run for js", { engine: "run" }, "M014"],
  ["engine missing", { engine: undefined }, "M014"],
  ["min_fw missing", { min_fw: undefined }, "M015"],
  ["min_fw negative", { min_fw: -1 }, "M015"],
  ["category missing", { category: undefined }, "M017"],
  ["author too long", { author: "a".repeat(41) }, "M018"],
  ["desc too long", { desc: "a".repeat(301) }, "M018"],
  ["perm unknown", { perms: ["camera"] }, "M019"],
  ["permissions (0.2.3)", { perms: undefined, permissions: ["sound"] }, "M021"],
  ["permissions differ from perms", { permissions: ["notify"] }, "M021"],
  ["icon three colours", { icon: { bg: ["#000000", "#111111", "#222222"] } }, "M022"],
  ["icon short colour", { icon: { bg: ["#000", "#111111"] } }, "M022"],
  ["icon glyph unknown", { icon: { bg: ["#000000", "#111111"], glyph: "rocket" } }, "M022"],
  ["type service", { type: "service" }, "M024"],
  ["type driver", { type: "driver" }, "M024"],
  ["type unknown", { type: "game" }, "M024"],
  ["addon_of itself", { addon_of: "vn.test.app" }, "M025"],
  ["ellipsis", { desc: "Đang tải…" }, "M026"],
];
for (const [what, patch, code] of BAD)
  test(`error ${code}: ${what}`, () => {
    const m = goodJs(patch);
    for (const k of Object.keys(patch)) if (patch[k] === undefined) delete m[k];
    assert.ok(errorsOf(m).includes(code), `${code} expected, got ${errorsOf(m)}`);
  });

test("type widget/face/theme goes to the declarative rules (decl.test.mjs), not M024", () => {
  // an app manifest with only its type changed: no runtime, no entry, no layout file
  for (const type of ["widget", "face"]) {
    const errs = errorsOf(goodJs({ type }));
    assert.ok(!errs.includes("M024"), errs.join());
    for (const c of ["M040", "M045", "M046", "F020"]) assert.ok(errs.includes(c), `${type}: ${c} expected, got ${errs}`); // perms ["sound"] is not a data perm
    for (const c of ["M013", "M014", "M015", "M017"]) assert.ok(!errs.includes(c), `${type}: app rule ${c} applied`);
    const warns = warningsOf(goodJs({ type }));
    assert.ok(warns.includes("M047"), warns.join()); // kind, engine, min_fw, category are app fields
  }
  const theme = errorsOf(goodJs({ type: "theme", runtime: "decl", perms: [], entry: { theme: "theme.json" } }));
  assert.deepEqual(theme, [], "a theme's file is not checked (as espk.py)");
});

test("M012: code written as a float (1.0) is refused, as Python's isinstance(int) would", () => {
  const text = JSON.stringify(goodJs()).replace('"code":10000', '"code":10000.0');
  assert.ok(text.includes("10000.0"));
  assert.ok(errorsOf(parse(text)).includes("M012"));
});

test("permissions next to the same perms is only a note (kept for 0.2.3)", () => {
  const ps = checkPackage([pkg(goodJs({ permissions: ["sound"] }))]);
  assert.deepEqual(ps.filter((p) => p.level !== "info"), []);
  assert.ok(ps.some((p) => p.code === "M021" && p.level === "info"));
});

test("warnings: reverse domain, APP's 23 characters, reserved perms, unknown fields, v2 fields, no icon", () => {
  assert.ok(warningsOf(goodJs({ id: "myapp" })).includes("M002"));
  assert.ok(warningsOf(goodJs({ id: "vn.example.averyveryverylongname" })).includes("M003"));
  assert.ok(!warningsOf(goodJs({ id: "vn.example.averyveryverylongname", min_fw: 400 })).includes("M003"));
  assert.ok(warningsOf(goodJs({ perms: ["net"] })).includes("M020"));
  assert.ok(warningsOf(goodJs({ colour: "red" })).includes("M030"));
  assert.ok(warningsOf(goodJs({ api: { min: 1 } })).includes("M031"));
  const m = goodJs();
  delete m.icon;
  assert.ok(warningsOf(m).includes("M023"));
});

test("j2me games: game.jar, the j2me field, min_fw 400", () => {
  const g = { id: "jar.test", name: "Game", version: "1.0", code: 10000, kind: "native", engine: "j2me", min_fw: 400, category: "game", icon: { bg: ["#000000", "#111111"] }, perms: ["sound"], j2me: { midlet: "a.B", keypad: "full" }, part: "core" };
  assert.deepEqual(errorsOf(g, { "game.jar": "PK" }), []);
  assert.ok(errorsOf(g, { "x.bin": "1" }).includes("F003"));
  assert.ok(warningsOf({ ...g, j2me: undefined }, { "game.jar": "PK" }).includes("M029"));
  assert.ok(warningsOf({ ...g, j2me: { midlet: "a.B", keypad: "tiny" } }, { "game.jar": "PK" }).includes("M029"));
  assert.ok(warningsOf({ ...g, min_fw: 300 }, { "game.jar": "PK" }).includes("M016"));
  assert.deepEqual(errorsOf({ ...g, type: "pack", j2me: undefined }, { "classlib.jar": "PK" }), []);
});

test("files: main.js, icon rules, names, empty part", () => {
  assert.ok(errorsOf(goodJs(), { "x.js": goodMain }).includes("F002"));
  assert.ok(errorsOf(goodJs(), { "main.js": goodMain, "icon.jpg": jpeg(0xc2) }).includes("F004")); // progressive
  assert.ok(errorsOf(goodJs(), { "main.js": goodMain, "icon.jpg": jpeg(0xc0, 120, 100) }).includes("F004")); // not square
  assert.ok(errorsOf(goodJs(), { "main.js": goodMain, "icon.jpg": "not a jpeg" }).includes("F004"));
  assert.ok(warningsOf(goodJs(), { "main.js": goodMain, "icon.jpg": jpeg(0xc0, 64, 64) }).includes("F005"));
  assert.ok(errorsOf(goodJs(), { "main.js": goodMain, "a..b": "x" }).some((c) => c === "P015" || c === "F001"));
  assert.ok(errorsOf(goodJs(), { "main.js": goodMain, "a:b": "x" }).some((c) => c === "P015" || c === "F001"));
  assert.ok(errorsOf(goodJs(), { "main.js": "x".repeat(512 * 1024) }).includes("F006"));
  assert.ok(errorsOf({ id: "vn.test.app", version: "1.0.0", code: 10000, part: "roads" }, {}).includes("F007"));
});

test("code: syntax, modules, permissions vs APIs, API level 1", () => {
  const c = (src, perms = []) => codes(checkCode(src, perms));
  assert.ok(c("app.render = () => ui.text('a'").includes("C001"));
  assert.ok(c("import x from './x';\napp.title = x;").includes("C002"));
  assert.ok(c("export const a = 1;").includes("C002"));
  assert.ok(c("notify('a', 'b');").includes("C003"));
  assert.ok(c("notify.schedule(Date.now() + 1000, 'a');").includes("C003"));
  assert.ok(c("app.badge(2);").includes("C003"));
  assert.ok(!c("notify('a');", ["notify"]).includes("C003"));
  assert.ok(c("sound.chime();").includes("C004"));
  assert.ok(!c("sound.chime();", ["sound"]).includes("C004"));
  assert.ok(c("const s = 'sound.chime()'; // sound.beep()\n/* notify(1) */").every((x) => x !== "C003" && x !== "C004"), "strings and comments are not calls");
  assert.ok(c("x", ["sound"]).includes("C005"));
  assert.ok(c("console.log(1);").includes("C006"));
  assert.ok(c("setTimeout(() => 1, 10);").includes("C006"));
  assert.ok(c("async function f() { await 1; }").includes("C007"));
  assert.ok(c("p.then(() => 1);").includes("C007"));
  assert.ok(c("ui.text('Đang tải…');").includes("C008"));
  assert.ok(!c("const re = /notify\\(/; const t = `${sound}`;").includes("C001"));
});

test(".espk structure: magic, flags, CRC, alignment, trailing bytes", () => {
  const good = buildEspk(fromPlain(goodJs()), [{ name: "main.js", data: Buffer.from(goodMain) }]);
  assert.deepEqual(readEspk(good).problems, []);
  const bad = (f) => {
    const b = Buffer.from(good);
    f(b);
    return codes(readEspk(b).problems);
  };
  assert.ok(bad((b) => b.write("JUNK", 0, "latin1")).includes("P001"));
  assert.ok(bad((b) => b.writeUInt16LE(2, 4)).includes("P002"));
  assert.ok(bad((b) => b.writeUInt16LE(1, 6)).includes("P003"));
  const t = 16 + ((good.readUInt32LE(8) + 15) & ~15);
  assert.ok(bad((b) => (b[b.length - 1] ^= 1)).includes("P021"));
  assert.ok(bad((b) => b.writeUInt32LE(b.readUInt32LE(t + 52) + 1, t + 52)).includes("P017"));
  assert.ok(codes(readEspk(Buffer.concat([good, Buffer.alloc(8)])).problems).includes("P022"));
  assert.equal(crc32(Buffer.from("123456789")), 0xcbf43926);
});

test("parts: one core, same version, a default per group, no file in two parts", () => {
  const core = pkg(goodJs({ parts: [{ id: "a", group: "g", label: "A", default: true }, { id: "b", group: "g", label: "B", default: true }] }));
  const a = pkg({ id: "vn.test.app", version: "1.0.0", code: 10000, part: "a" }, { "a/x.bin": "1" });
  const b = pkg({ id: "vn.test.app", version: "1.0.0", code: 10000, part: "b" }, { "b/x.bin": "1" });
  assert.ok(codes(checkPackage([core, a, b]), "error").includes("M034"));
  const b2 = pkg({ id: "vn.test.app", version: "1.0.1", code: 10001, part: "b" }, { "main.js": "1" });
  const errs = codes(checkPackage([pkg(goodJs()), b2]), "error");
  assert.ok(errs.includes("M033"));
  assert.ok(codes(checkPackage([pkg(goodJs()), pkg(goodJs())]), "error").includes("M033"));
});
