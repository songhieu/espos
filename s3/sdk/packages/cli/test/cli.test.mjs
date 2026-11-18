// The command line: templates (create -> build -> check), the command reference, store, firmware, exit codes.
import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { test } from "node:test";
import { readEspk } from "../dist/espk.js";
import { checkPackage } from "../dist/validate.js";
import { goodJs, goodMain, hasRoot, espos, python, ROOT, tmp, writePkg } from "./helpers.mjs";

const JAR = process.env.ESPOS_TEST_JAR ?? "MegaBlock-1.0.0.jar";

for (const t of ["js-app", "js-app-ts"])
  test(`create --template ${t} -> build -> check`, () => {
    const d = tmp(t);
    const dir = path.join(d, "my-app");
    const c = espos(["create", dir, "--template", t, "--id", "vn.test.myapp"]);
    assert.equal(c.code, 0, c.stdout + c.stderr);
    for (const f of ["manifest.json", "espos.json", "icon.jpg", "README.md", "sim/smoke.txt", ".gitignore"]) assert.ok(fs.existsSync(path.join(dir, f)), f);
    if (t === "js-app-ts") assert.ok(fs.existsSync(path.join(dir, "types/espos.d.ts")));
    const b = espos(["build", dir, "--json"]);
    assert.equal(b.code, 0, b.stdout + b.stderr);
    assert.equal(b.json.ok, true);
    assert.ok(fs.existsSync(b.json.out));
    assert.deepEqual(b.json.problems.filter((p) => p.level !== "info"), []);
    const p = readEspk(fs.readFileSync(b.json.out), b.json.out);
    assert.deepEqual(p.files.map((f) => f.name), ["icon.jpg", "main.js"]);
    if (t === "js-app-ts") assert.doesNotMatch(p.files[1].data.toString(), /\bimport\b|: number/, "bundled to plain JS");
    const k = espos(["check", dir, b.json.out, "--json"]);
    assert.equal(k.code, 0, k.stdout + k.stderr);
    assert.equal(k.json.ok, true);
    assert.equal(k.json.results.length, 2);
  });

test("create --template j2me-game with --jar, and without a jar (build says what is missing)", () => {
  const d = tmp("j2me");
  const bare = espos(["create", path.join(d, "bare"), "-t", "j2me-game"]);
  assert.equal(bare.code, 0, bare.stderr);
  const b0 = espos(["build", path.join(d, "bare")]);
  assert.equal(b0.code, 1);
  assert.match(b0.stderr, /game\.jar not found/);
  if (!fs.existsSync(JAR)) return;
  const c = espos(["create", path.join(d, "mb"), "-t", "j2me-game", "--jar", JAR, "--license", "Apache-2.0", "--desc", "Xếp gạch"]);
  assert.equal(c.code, 0, c.stdout + c.stderr);
  const b = espos(["build", path.join(d, "mb"), "--json"]);
  assert.equal(b.code, 0, b.stdout + b.stderr);
  const p = readEspk(fs.readFileSync(b.json.out));
  assert.deepEqual(p.files.map((f) => f.name), ["game.jar", "icon.jpg"]);
  assert.deepEqual(checkPackage([p]).filter((x) => x.level !== "info"), []);
});

test("create refuses a folder that is not empty, and unknown templates", () => {
  const d = tmp("refuse");
  fs.writeFileSync(path.join(d, "x"), "1");
  assert.equal(espos(["create", d]).code, 2);
  assert.equal(espos(["create", path.join(d, "n"), "-t", "widget"]).code, 2);
});

test("TypeScript: a type error fails the build and nothing is written", () => {
  const d = tmp("tserr");
  const dir = path.join(d, "app");
  assert.equal(espos(["create", dir, "-t", "js-app-ts", "--id", "vn.test.ts"]).code, 0);
  fs.appendFileSync(path.join(dir, "src/main.ts"), "\nui.text(1, { size: 'big' });\nconsole.log('x');\n");
  const b = espos(["build", dir]);
  assert.equal(b.code, 1);
  assert.match(b.stdout, /TS2322|TS2769|TS2345/);
  assert.match(b.stdout, /TS2584|TS2304/, "console is not in API level 1");
  assert.ok(!fs.existsSync(path.join(dir, "dist")));
  const ok = espos(["build", dir, "--no-typecheck"]);
  assert.equal(ok.code, 0, ok.stdout + ok.stderr); // C006 (console) is a warning
});

test("a plain folder writes its package next to it, never inside", () => {
  const d = tmp("plain");
  const dir = writePkg(path.join(d, "app"), goodJs(), { "main.js": goodMain });
  const b = espos(["build", dir, "--json"]);
  assert.equal(b.code, 0, b.stderr);
  assert.equal(path.dirname(b.json.out), d);
});

test("check fails on errors (exit 1), --strict on warnings, --rules lists the rules", () => {
  const d = tmp("chk");
  const bad = writePkg(path.join(d, "bad"), { ...goodJs(), code: "1" }, { "main.js": goodMain });
  const r = espos(["check", bad, "--json"]);
  assert.equal(r.code, 1);
  assert.ok(r.json.results[0].problems.some((p) => p.code === "M012"));
  const warn = writePkg(path.join(d, "warn"), goodJs({ id: "noreverse" }), { "main.js": goodMain });
  assert.equal(espos(["check", warn]).code, 0);
  assert.equal(espos(["check", warn, "--strict"]).code, 1);
  const rules = espos(["check", "--rules", "--json"]);
  assert.ok(rules.json.length > 40);
});

test("help --json: every command with its options (the docs site's reference)", () => {
  const r = espos(["help", "--json"]);
  assert.equal(r.code, 0);
  const names = r.json.commands.map((c) => c.name);
  for (const n of ["create", "build", "check", "inspect", "jar", "sim", "qemu", "device", "firmware", "j2me", "store", "doctor", "help", "types"]) assert.ok(names.includes(n), n);
  const device = r.json.commands.find((c) => c.name === "device");
  assert.deepEqual(device.commands.map((c) => c.name), ["list", "info", "shot", "logs", "app", "tap", "drag", "home", "trace", "mem", "js", "send", "console"]);
  const build = r.json.commands.find((c) => c.name === "build");
  assert.ok(build.options.some((o) => o.name === "--out" && o.short === "-o"));
  assert.ok(r.json.exit_codes["4"]);
  for (const c of r.json.commands) assert.ok(c.summary, c.name);
});

test("usage errors exit 2; every command has --help", () => {
  assert.equal(espos(["nope"]).code, 2);
  assert.equal(espos(["build", "--nope"]).code, 2);
  assert.equal(espos(["jar"]).code, 2);
  const ref = espos(["help", "--json"]).json;
  const walk = (cs, p) => cs.flatMap((c) => (c.commands ? walk(c.commands, [...p, c.name]) : [[...p, c.name]]));
  for (const cmd of walk(ref.commands, [])) {
    const r = espos([...cmd, "--help"]);
    assert.equal(r.code, 0, cmd.join(" "));
    assert.match(r.stdout, new RegExp(`^espos ${cmd.join(" ")}`), cmd.join(" "));
  }
});

test("store entry and store publish (dry run against a local catalog)", { skip: !(hasRoot && python && fs.existsSync(path.resolve(ROOT, "../scripts/publish_pkg.py"))) && "needs scripts/publish_pkg.py" }, () => {
  const d = tmp("store");
  const dir = writePkg(path.join(d, "app"), goodJs(), { "main.js": goodMain });
  const b = espos(["build", dir, "--json"]);
  const e = espos(["store", "entry", b.json.out, "--base", "https://example.test/apps", "--json"]);
  assert.equal(e.code, 0, e.stderr);
  const entry = JSON.parse(e.stdout);
  assert.equal(entry.parts[0].url, `https://example.test/apps/${path.basename(b.json.out)}`);
  fs.writeFileSync(path.join(d, "catalog.json"), JSON.stringify({ apps: [{ id: "old" }], packages: [] }));
  const p = espos(["store", "publish", b.json.out, "--catalog", path.join(d, "catalog.json"), "--out", path.join(d, "merged.json")], { env: { ESPOS_BBK: "bbk_must_not_be_used" } });
  assert.equal(p.code, 0, p.stdout + p.stderr);
  assert.doesNotMatch(p.stdout + p.stderr, /bbk_must_not_be_used/);
  const merged = JSON.parse(fs.readFileSync(path.join(d, "merged.json"), "utf8"));
  assert.equal(merged.apps.length, 1);
  assert.equal(merged.packages[0].id, "vn.test.app");
  // --yes without a key refuses before running anything
  const y = espos(["store", "publish", b.json.out, "--yes", "--catalog", path.join(d, "catalog.json")], { env: { ESPOS_BBK: "" } });
  assert.equal(y.code, 2);
  // a package with errors is refused before publish_pkg.py
  const bad = writePkg(path.join(d, "bad"), { ...goodJs(), category: "games" }, { "main.js": goodMain });
  assert.equal(espos(["store", "publish", bad, "--catalog", path.join(d, "catalog.json")]).code, 1);
});

test("firmware size against the app slot", { skip: !(hasRoot && fs.existsSync(path.join(ROOT, ".pio/build/s3desk/firmware.bin"))) && "no firmware.bin" }, () => {
  const r = espos(["firmware", "size", "--json"], { cwd: ROOT });
  assert.ok(r.code === 0 || r.code === 1);
  assert.equal(r.json.slot, 3_932_160);
  assert.equal(r.json.ok, r.json.size <= r.json.slot);
  const d = tmp("fw");
  fs.writeFileSync(path.join(d, "big.bin"), Buffer.alloc(3_932_161));
  assert.equal(espos(["firmware", "size", "--file", path.join(d, "big.bin")], { cwd: ROOT }).code, 1);
});

test("tool wrappers print what they would run (--dry-run)", { skip: !hasRoot && "no repo" }, () => {
  assert.match(espos(["j2me", "runtime", "1.0.1", "10001", "--dry-run"], { cwd: ROOT }).stdout, /make_runtime\.sh 1\.0\.1 10001/);
  assert.match(espos(["j2me", "vm-test", "--gcstress", "--dry-run"], { cwd: ROOT }).stdout, /test\/run\.sh gcstress/);
  assert.match(espos(["j2me", "test", "--dry-run"], { cwd: ROOT }).stdout, /build-midp\.sh tests/);
  assert.match(espos(["firmware", "build", "--env", "s3desk-qemu", "--dry-run"], { cwd: ROOT }).stdout, /pio run -e s3desk-qemu/);
});

test("doctor --json", () => {
  const r = espos(["doctor", "--json"]);
  assert.ok(r.json && Array.isArray(r.json.items));
  assert.ok(r.json.items.find((i) => i.name === "node").ok);
});
