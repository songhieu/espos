// Device commands against a fake dev console on a pseudo-terminal (test/fixtures/fake_s3.py): no hardware is opened.
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import { after, before, test } from "node:test";
import { PNG } from "pngjs";
import { here, espos, python, tmp } from "./helpers.mjs";

let serialOk = true;
try {
  await import("serialport");
} catch {
  serialOk = false;
}
const skip = (!python && "python3 not found") || (!serialOk && "serialport not installed") || (process.platform === "win32" && "no pty");

function fake(kind, log) {
  return new Promise((resolve, reject) => {
    const p = spawn(python, [path.join(here, "fixtures/fake_s3.py"), "--kind", kind, "--log", log], { stdio: ["pipe", "pipe", "inherit"] });
    p.stdout.once("data", (d) => resolve({ proc: p, port: String(d).trim().split("\n")[0] }));
    p.once("error", reject);
  });
}

const fakes = [];
let dir, s3, x4, silent;
before(async () => {
  if (skip) return;
  dir = tmp("dev");
  s3 = await fake("s3", path.join(dir, "s3.log"));
  x4 = await fake("x4", path.join(dir, "x4.log"));
  silent = await fake("none", path.join(dir, "none.log"));
  fakes.push(s3, x4, silent);
});
after(() => {
  for (const f of fakes) f.proc.stdin.end();
});
const cfg = () => ({ configDir: path.join(dir, "cfg") });

test("info, mem, app, tap on an S3", { skip }, () => {
  const i = espos(["device", "info", "--port", s3.port, "--json"], cfg());
  assert.equal(i.code, 0, i.stderr);
  assert.equal(i.json.kind, "s3");
  assert.equal(i.json.board, "ES3C35P");
  const m = espos(["device", "mem", "--port", s3.port, "--json"], cfg());
  assert.equal(m.code, 0, m.stderr);
  assert.match(m.json.lines[0], /^MEM internal/);
  const a = espos(["device", "app", "dev.espos.dice", "--port", s3.port], cfg());
  assert.equal(a.code, 0, a.stderr);
  assert.match(a.stdout, /app open dev\.espos\.dice/);
  assert.equal(espos(["device", "tap", "160", "300", "--port", s3.port, "--wait", "0.2"], cfg()).code, 0);
  assert.equal(espos(["device", "tap", "x", "300", "--port", s3.port], cfg()).code, 2);
  // 0.4.0 reads ids up to 51 characters; longer is refused before anything is sent
  const long = espos(["device", "app", "vn.example.averyveryverylongname", "--port", s3.port], cfg());
  assert.equal(long.code, 0, long.stderr);
  assert.doesNotMatch(long.stderr, /23 characters/);
  assert.equal(espos(["device", "app", "x".repeat(52), "--port", s3.port], cfg()).code, 2);
  const log = fs.readFileSync(path.join(dir, "s3.log"), "utf8");
  assert.match(log, /^APP dev\.espos\.dice$/m);
  assert.match(log, /^TAP 160 300$/m);
});

test("shot: RGB565 + the top layer composited into a PNG", { skip }, () => {
  const out = path.join(dir, "shot.png");
  const r = espos(["device", "shot", out, "--port", s3.port], cfg());
  assert.equal(r.code, 0, r.stderr);
  const png = PNG.sync.read(fs.readFileSync(out));
  assert.deepEqual([png.width, png.height], [4, 2]);
  assert.deepEqual([...png.data.subarray(0, 4)], [255, 0, 0, 255]); // top layer's opaque red over red
  assert.deepEqual([...png.data.subarray(4, 8)], [0, 255, 0, 255]); // green from RGB565
});

test("a port that answers without kind s3 (the X4) is refused after HELLO only", { skip }, () => {
  const r = espos(["device", "app", "x", "--port", x4.port], cfg());
  assert.equal(r.code, 4);
  assert.match(r.stderr, /not an EspOS S3/);
  const log = fs.readFileSync(path.join(dir, "x4.log"), "utf8").trim().split("\n");
  assert.ok(log.length >= 1 && log.every((l) => l.startsWith("ESPOS:HELLO ")), log.join("|"));
});

test("a port that does not answer is refused", { skip }, () => {
  const r = espos(["device", "mem", "--port", silent.port], cfg());
  assert.equal(r.code, 4);
});

test("a denied port is never opened, even with --port", { skip }, () => {
  fs.writeFileSync(path.join(dir, "s3.log"), "");
  const r = espos(["device", "info", "--port", s3.port], { ...cfg(), env: { ESPOS_DENY_PORTS: path.basename(s3.port) } });
  assert.equal(r.code, 4);
  assert.match(r.stderr, /deny list/);
  assert.equal(fs.readFileSync(path.join(dir, "s3.log"), "utf8"), "");
  // a name on the deny list, in both macOS spellings
  for (const p of ["/dev/cu.usbmodem9901", "/dev/tty.usbmodem9901"]) {
    const d = espos(["device", "info", "--port", p], { ...cfg(), env: { ESPOS_DENY_PORTS: "usbmodem9901" } });
    assert.equal(d.code, 4);
    assert.match(d.stderr, /deny list/);
  }
});

test("device list never opens ports without --probe", { skip }, () => {
  const r = espos(["device", "list", "--json"], { ...cfg(), env: { ESPOS_DENY_PORTS: "usbmodem9901" } });
  assert.equal(r.code, 0, r.stderr);
  assert.ok(Array.isArray(r.json.ports));
  assert.ok(r.json.deny_list.includes("usbmodem9901"));
});
