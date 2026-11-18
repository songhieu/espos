// A headless simulator run (needs a built s3/sim; ESPOS_TEST_SIM_BUILD=NAME builds sim/NAME first, else the
// last sim/build/espos-sim is used as it is).
import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { test } from "node:test";
import { goodJs, hasRoot, espos, ROOT, tmp, writePkg } from "./helpers.mjs";

const build = process.env.ESPOS_TEST_SIM_BUILD;
const bin = path.join(ROOT, "sim", build ?? "build", "espos-sim");
const skip = (!hasRoot && "no s3desk repo") || (!build && !fs.existsSync(bin) && "sim/build/espos-sim is not built") || (process.platform !== "darwin" && "the simulator builds on macOS only today");
const simArgs = (data) => ["--headless", "--data", data, ...(build ? ["--sim-build", build] : ["--no-build"]), "--timeout", "120"];

test("a template app opens in the headless simulator, with screenshots", { skip, timeout: 600000 }, () => {
  const d = tmp("sim");
  const dir = path.join(d, "app");
  assert.equal(espos(["create", dir, "--id", "vn.test.simapp"]).code, 0);
  fs.writeFileSync(path.join(dir, "sim/smoke.txt"), "wait 500\nshot before\ntap 200 227\nwait 500\nshot after\n");
  const r = espos(["sim", dir, ...simArgs(path.join(d, "data"))], { cwd: ROOT, timeout: 600000 });
  assert.equal(r.code, 0, r.stdout + r.stderr);
  assert.match(r.stdout, /OK: vn\.test\.simapp opened/);
  for (const s of ["open", "before", "after"]) assert.ok(fs.existsSync(path.join(dir, ".espos/shots", `${s}.png`)), s);
  assert.ok(fs.existsSync(path.join(d, "data/fs/pkgs/vn.test.simapp/main.js")));
});

test("an app that throws makes the run fail (exit 1) with its error", { skip, timeout: 600000 }, () => {
  const d = tmp("simbad");
  const dir = writePkg(path.join(d, "bad"), goodJs({ id: "vn.test.simbad", perms: [] }), { "main.js": "app.render = () => ui.text('x');\nnosuchfunction();\n" });
  const r = espos(["sim", dir, ...simArgs(path.join(d, "data"))], { cwd: ROOT, timeout: 600000 });
  assert.equal(r.code, 1, r.stdout + r.stderr);
  assert.match(r.stdout + r.stderr, /vn\.test\.simbad: .*nosuchfunction/);
});

test("sim --board 28 points to QEMU", { skip: !hasRoot && "no repo" }, () => {
  const r = espos(["sim", "--board", "28"], { cwd: ROOT });
  assert.equal(r.code, 2);
  assert.match(r.stderr, /qemu --board 28/);
});
