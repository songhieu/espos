// espos login / logout / whoami / upload / apps against a local stand-in for the espos.dev routes (/v1/dev/*), and the
// private-upload rules (U001, U002) of the validator. The real server side is web/backend (confirm_user_pkg & co).
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import fs from "node:fs";
import http from "node:http";
import path from "node:path";
import { after, before, test } from "node:test";
import { buildEspk, readEspk } from "../dist/espk.js";
import { fromPlain } from "../dist/pyjson.js";
import { checkPackage, checkPrivate, privateId } from "../dist/validate.js";
import { BIN, goodJs, goodMain, tmp, writePkg } from "./helpers.mjs";

const KEY = "espos_dev_" + "a1".repeat(32);
const OTHER = "espos_dev_" + "d4".repeat(32);

// ---------------------------------------------------------------- the stand-in server
const srv = { reqs: [], files: new Map(), pkgs: [], devices: [{ id: 13, name: "Desk", fw: "espos-s3-0.5.7" }, { id: 15, name: "Kitchen", fw: "espos-s3-0.3.0" }], failConfirm: null };
let server;
let API;

function reply(res, code, body) {
  res.writeHead(code, { "Content-Type": "application/json" });
  res.end(JSON.stringify(body));
}

before(async () => {
  server = http.createServer((req, res) => {
    const chunks = [];
    req.on("data", (c) => chunks.push(c));
    req.on("end", () => {
      const raw = Buffer.concat(chunks);
      const u = new URL(req.url, "http://x");
      srv.reqs.push({ method: req.method, path: u.pathname, search: u.search, auth: req.headers.authorization ?? "", size: raw.length });
      if (req.method === "PUT" && u.pathname.startsWith("/put/")) {
        srv.files.set(decodeURIComponent(u.pathname.slice(5)), raw);
        res.writeHead(200);
        return res.end();
      }
      if (!u.pathname.startsWith("/v1/dev/")) return reply(res, 404, { error: "no route" });
      if (req.headers.authorization !== `Bearer ${KEY}`) return reply(res, 401, { error: "Khoá nhà phát triển không đúng hoặc đã bị thu hồi", code: "bad_key" });
      const body = raw.length ? JSON.parse(raw.toString("utf8")) : {};
      if (u.pathname === "/v1/dev/me")
        return reply(res, 200, { user_id: 1, email: "dev@example.com", key: { id: 1, label: "Laptop" }, devices: srv.devices, pkgs: srv.pkgs.length, limits: { pkgs: 30, bytes: 4194304, jar_bytes: 2097152, keys: 5 } });
      if (u.pathname === "/v1/dev/pkgs") {
        const out = { pkgs: srv.pkgs, max: 30, max_bytes: 4194304 };
        if (u.searchParams.get("devices") === "1")
          out.devices = srv.devices.map((d) => ({ ...d, installed: d.id === 13 && srv.pkgs[0] ? { [srv.pkgs[0].pkg_id]: srv.pkgs[0].code } : {}, wants: {}, job: null }));
        return reply(res, 200, out);
      }
      if (u.pathname === "/v1/dev/pkgs/upload") {
        const key = `upkg/u1/in/${Date.now()}/core.espk`;
        return reply(res, 201, { key, url: `${API}/put/${encodeURIComponent(key)}`, icon_key: "", icon_url: "", max_bytes: 4194304 });
      }
      if (u.pathname === "/v1/dev/pkgs/confirm") {
        srv.lastConfirm = body;
        if (srv.failConfirm) return reply(res, srv.failConfirm.status, srv.failConfirm.body);
        const bytes = srv.files.get(body.key);
        if (!bytes) return reply(res, 409, { error: "x", code: "pkg_missing_file" });
        const p = readEspk(bytes);
        const errs = [...checkPackage([p]), ...checkPrivate(p)].filter((x) => x.level === "error");
        if (errs.length) return reply(res, 422, { error: "x", code: "pkg_invalid", params: { rule: errs[0].code, detail: errs[0].msg }, problems: errs.map((e) => ({ code: e.code, msg: e.msg })) });
        const id = privateId(p.plain.id);
        const old = srv.pkgs.find((x) => x.pkg_id === id);
        if (old && p.plain.code <= old.code) return reply(res, 409, { error: "x", code: "pkg_not_newer", params: { code: p.plain.code, have: old.code } });
        const row = { id: old?.id ?? srv.pkgs.length + 1, pkg_id: id, src_id: p.plain.id, name: p.plain.name, version: p.plain.version, code: p.plain.code, size: bytes.length, sha256: p.sha256, kind: p.plain.kind, engine: p.plain.engine, perms: p.plain.perms ?? [], min_fw: p.plain.min_fw };
        srv.pkgs = [row, ...srv.pkgs.filter((x) => x.pkg_id !== id)];
        const asked = body.install === "all" ? srv.devices : Array.isArray(body.install) ? srv.devices.filter((d) => body.install.includes(d.id)) : old ? [srv.devices[0]] : [];
        const devices = asked.map((d) => ({ id: d.id, name: d.name, fw: d.fw, action: old ? "update" : d.fw.endsWith("0.3.0") && p.plain.min_fw > 300 ? "wait_fw" : "install" }));
        return reply(res, 200, { pkg: row, replaced: !!old, devices });
      }
      if (u.pathname === "/v1/dev/pkgs/install") {
        srv.lastInstall = body;
        const row = srv.pkgs.find((x) => x.pkg_id === body.pkg_id);
        if (!row) return reply(res, 404, { error: "x", code: "pkg_not_found" });
        const asked = body.devices === "all" ? srv.devices : srv.devices.filter((d) => body.devices.includes(d.id));
        return reply(res, 200, { ok: true, pkg_id: row.pkg_id, code: row.code, min_fw: row.min_fw, devices: asked.map((d) => ({ id: d.id, name: d.name, fw: d.fw, action: "install" })) });
      }
      if (u.pathname === "/v1/dev/pkgs/delete") {
        const n = srv.pkgs.length;
        srv.pkgs = srv.pkgs.filter((x) => x.pkg_id !== body.pkg_id);
        if (srv.pkgs.length === n) return reply(res, 404, { error: "x", code: "pkg_not_found" });
        return reply(res, 200, { ok: true, devices: 1, pkg_id: body.pkg_id });
      }
      return reply(res, 404, { error: "no route" });
    });
  });
  await new Promise((r) => server.listen(0, "127.0.0.1", r));
  API = `http://127.0.0.1:${server.address().port}`;
});
after(() => server.close());

/** espos, run without blocking this process (the stand-in server answers from here). */
function run(args, o = {}) {
  return new Promise((resolve) => {
    const p = spawn(process.execPath, [BIN, ...args], {
      cwd: o.cwd ?? process.cwd(),
      env: { ...process.env, NO_COLOR: "1", ESPOS_CONFIG_DIR: o.config, ESPOS_API: API, ESPOS_DEV_KEY: "", ...(o.env ?? {}) },
    });
    let stdout = "", stderr = "";
    p.stdout.on("data", (c) => (stdout += c));
    p.stderr.on("data", (c) => (stderr += c));
    if (o.input !== undefined) p.stdin.end(o.input);
    else p.stdin.end();
    p.on("close", (code) => {
      let json;
      if (args.includes("--json")) try { json = JSON.parse(stdout); } catch { /* not JSON */ }
      resolve({ code, stdout, stderr, json });
    });
  });
}

const app = (dir, extra = {}, files = {}) => writePkg(dir, goodJs({ id: "vn.test.timer", ...extra }), { "main.js": goodMain, ...files });

// ---------------------------------------------------------------- the rules

test("checkPrivate: what a private upload may be (U001, U002), and the private id", () => {
  const pkg = (m, files = [{ name: "main.js", data: Buffer.from(goodMain) }]) => readEspk(buildEspk(fromPlain(m), files), "t.espk");
  assert.deepEqual(checkPrivate(pkg(goodJs())), []);
  assert.equal(privateId("vn.test.app"), "u.vn.test.app");
  assert.equal(privateId("u.mine"), "u.mine");
  const codes = (m, f) => checkPrivate(pkg(m, f)).map((p) => p.code);
  assert.deepEqual(codes(goodJs({ kind: "native", engine: "pet" })), ["U002"]);
  assert.deepEqual(codes(goodJs({ type: "pack" })), ["U002"]);
  assert.deepEqual(codes(goodJs({ addon_of: "vn.pawboop.pet" })), ["U002"]);
  assert.deepEqual(codes(goodJs({ part: "extra" })), ["U002"]);
  assert.deepEqual(codes(goodJs({ id: "vn.test." + "a".repeat(39) })), ["U002"], "47 characters + u. is over 48");
  assert.deepEqual(codes(goodJs({ id: "vn.test." + "a".repeat(38) })), []);
  const game = { ...goodJs(), kind: "native", engine: "j2me", min_fw: 400, category: "game", perms: ["sound", "notify"], j2me: { midlet: "a.B" } };
  assert.deepEqual(codes(game, [{ name: "game.jar", data: Buffer.from("PK\x03\x04") }]), ["U002"]);
  const bigJar = Buffer.alloc(2 * 1048576 + 1);
  bigJar.write("PK\x03\x04");
  assert.deepEqual(codes({ ...game, perms: ["sound"] }, [{ name: "game.jar", data: bigJar }]), ["U001"]);
  const big = Buffer.alloc(4 * 1048576, 0x20);
  assert.ok(codes(goodJs(), [{ name: "main.js", data: Buffer.from(goodMain) }, { name: "big.bin", data: big }]).includes("U001"));
});

test("check --rules lists U001 and U002", async () => {
  const r = await run(["check", "--rules", "--json"], { config: tmp("cfg") });
  assert.equal(r.code, 0);
  assert.ok(r.json.some((x) => x.code === "U001") && r.json.some((x) => x.code === "U002"));
});

// ---------------------------------------------------------------- login, whoami, logout

test("login: checks the key, saves it readable only by you, never prints it; logout forgets it", async () => {
  const cfg = tmp("cfg");
  const bad = await run(["login", "--key-stdin"], { config: cfg, input: "not-a-key\n" });
  assert.equal(bad.code, 2, bad.stderr);
  const wrong = await run(["login", "--key-stdin"], { config: cfg, input: OTHER + "\n" });
  assert.equal(wrong.code, 1, wrong.stderr);
  assert.match(wrong.stderr, /wrong or was revoked/);
  assert.ok(!fs.existsSync(path.join(cfg, "credentials.json")), "a refused key is not saved");
  const ok = await run(["login", "--key-stdin"], { config: cfg, input: KEY + "\n" });
  assert.equal(ok.code, 0, ok.stderr);
  assert.match(ok.stdout, /signed in .* as dev@example\.com/);
  assert.match(ok.stdout, /Desk \(#13, 0\.5\.7\)/);
  for (const s of [ok.stdout, ok.stderr, wrong.stdout, wrong.stderr]) assert.ok(!s.includes("a1a1a1a1") && !s.includes("d4d4d4d4"), "the key is never printed");
  const f = path.join(cfg, "credentials.json");
  assert.equal(fs.statSync(f).mode & 0o777, 0o600);
  assert.equal(JSON.parse(fs.readFileSync(f, "utf8")).key, KEY);
  assert.equal(JSON.parse(fs.readFileSync(f, "utf8")).api, API);
  const who = await run(["whoami", "--json"], { config: cfg });
  assert.equal(who.code, 0, who.stderr);
  assert.equal(who.json.email, "dev@example.com");
  assert.equal(who.json.from, "file");
  assert.ok(srv.reqs.filter((r) => r.path.startsWith("/v1/dev/")).every((r) => r.auth.startsWith("Bearer espos_dev_")));
  const out = await run(["logout"], { config: cfg });
  assert.equal(out.code, 0);
  assert.ok(!fs.existsSync(f));
  const after = await run(["whoami"], { config: cfg });
  assert.equal(after.code, 2);
  assert.match(after.stderr, /espos login/);
});

test("ESPOS_DEV_KEY works without a saved key; a plain-http server that is not this machine is refused", async () => {
  const cfg = tmp("cfg");
  const who = await run(["whoami", "--json"], { config: cfg, env: { ESPOS_DEV_KEY: KEY } });
  assert.equal(who.code, 0, who.stderr);
  assert.equal(who.json.from, "env");
  const http2 = await run(["whoami"], { config: cfg, env: { ESPOS_DEV_KEY: KEY, ESPOS_API: "http://example.com" } });
  assert.equal(http2.code, 2);
  assert.match(http2.stderr, /only sent over https/);
});

// ---------------------------------------------------------------- upload, apps

test("upload: builds, checks, uploads the bytes, installs a new package on every S3; an update goes where it is", async () => {
  const cfg = tmp("cfg");
  const env = { ESPOS_DEV_KEY: KEY };
  const d = tmp("up");
  const dir = app(path.join(d, "timer"));
  const dry = await run(["upload", dir, "--dry-run", "--json"], { config: cfg });
  assert.equal(dry.code, 0, dry.stderr);
  assert.equal(dry.json.pkg_id, "u.vn.test.timer");
  const n0 = srv.reqs.length;
  const r = await run(["upload", dir, "--json"], { config: cfg, env });
  assert.equal(r.code, 0, r.stdout + r.stderr);
  assert.equal(r.json.pkg.pkg_id, "u.vn.test.timer");
  assert.deepEqual(r.json.devices.map((x) => x.id), [13, 15]);
  assert.equal(srv.lastConfirm.install, "all");
  const put = srv.reqs.slice(n0).find((x) => x.method === "PUT");
  const built = readEspk(fs.readFileSync(path.join(dir, `../vn.test.timer-1.0.0-core.espk`)));
  assert.equal(put.size, built.bytes.length, "the built package is what was uploaded");
  // the same code again: refused before anything is sent
  const n1 = srv.reqs.length;
  const again = await run(["upload", dir], { config: cfg, env });
  assert.equal(again.code, 1);
  assert.match(again.stderr, /not higher than the uploaded version/);
  assert.ok(!srv.reqs.slice(n1).some((x) => x.method === "PUT"), "nothing uploaded");
  // a higher code: an update, sent only where it is (no "install" asked)
  fs.writeFileSync(path.join(dir, "manifest.json"), JSON.stringify(goodJs({ id: "vn.test.timer", version: "1.0.1", code: 10001 })));
  const up = await run(["upload", dir], { config: cfg, env });
  assert.equal(up.code, 0, up.stderr);
  assert.match(up.stdout, /updated u\.vn\.test\.timer 1\.0\.1 \(code 10001\), was 1\.0\.0/);
  assert.match(up.stdout, /update .*Desk \(#13\)/);
  assert.equal(srv.lastConfirm.install, "");
  // --device picks the devices
  fs.writeFileSync(path.join(dir, "manifest.json"), JSON.stringify(goodJs({ id: "vn.test.timer", version: "1.0.2", code: 10002 })));
  const dev = await run(["upload", dir, "--device", "15"], { config: cfg, env });
  assert.equal(dev.code, 0, dev.stderr);
  assert.deepEqual(srv.lastConfirm.install, [15]);
});

test("upload refuses what the server would refuse, before sending anything; server refusals are explained", async () => {
  const cfg = tmp("cfg");
  const env = { ESPOS_DEV_KEY: KEY };
  const d = tmp("up2");
  const n0 = srv.reqs.length;
  const pet = await run(["upload", app(path.join(d, "pet"), { kind: "native", engine: "pet", id: "vn.test.pet" })], { config: cfg, env });
  assert.equal(pet.code, 1);
  assert.match(pet.stdout + pet.stderr, /U002/);
  const bad = await run(["upload", app(path.join(d, "bad"), { id: "vn.test.bad", code: 1.5 })], { config: cfg, env });
  assert.equal(bad.code, 1);
  assert.equal(srv.reqs.length, n0, "nothing reached the server");
  // the server says no (its own check): its problems are shown
  srv.failConfirm = { status: 422, body: { error: "x", code: "pkg_invalid", params: { rule: "F004", detail: "icon.jpg: not a baseline JPEG" }, problems: [{ code: "F004", msg: "icon.jpg: not a baseline JPEG" }] } };
  try {
    const r = await run(["upload", app(path.join(d, "ok"), { id: "vn.test.ok" })], { config: cfg, env });
    assert.equal(r.code, 1);
    assert.match(r.stdout + r.stderr, /F004/);
    assert.match(r.stderr, /the server refused the package \(F004\)/);
  } finally {
    srv.failConfirm = null;
  }
  const off = await run(["upload", app(path.join(d, "x"), { id: "vn.test.x" })], { config: cfg, env: { ESPOS_DEV_KEY: OTHER } });
  assert.equal(off.code, 1);
  assert.match(off.stderr, /wrong or was revoked/);
});

test("apps: list with each device's copy, install by either id, rm asks for --yes when not in a terminal", async () => {
  const cfg = tmp("cfg");
  const env = { ESPOS_DEV_KEY: KEY };
  const d = tmp("apps");
  srv.pkgs = [];
  assert.equal((await run(["upload", app(path.join(d, "a"), { id: "vn.test.apps" }), "--no-install"], { config: cfg, env })).code, 0);
  assert.equal(srv.lastConfirm.install, "");
  const l = await run(["apps", "--json"], { config: cfg, env });
  assert.equal(l.code, 0, l.stderr);
  assert.equal(l.json.pkgs[0].pkg_id, "u.vn.test.apps");
  assert.equal(srv.reqs.at(-1).search, "?devices=1");
  const lt = await run(["apps"], { config: cfg, env });
  assert.match(lt.stdout, /u\.vn\.test\.apps .*JavaScript app/);
  assert.match(lt.stdout, /Desk \(#13, 0\.5\.7\)\n {2}u\.vn\.test\.apps +installed \(code 10000\)/);
  const i = await run(["apps", "install", "vn.test.apps", "--device", "13"], { config: cfg, env });
  assert.equal(i.code, 0, i.stderr);
  assert.deepEqual(srv.lastInstall, { pkg_id: "u.vn.test.apps", devices: [13] });
  assert.match(i.stdout, /install .*Desk \(#13\)/);
  const no = await run(["apps", "rm", "u.vn.test.apps"], { config: cfg, env });
  assert.equal(no.code, 2);
  assert.match(no.stderr, /--yes/);
  const rm = await run(["apps", "rm", "u.vn.test.apps", "--yes"], { config: cfg, env });
  assert.equal(rm.code, 0, rm.stderr);
  assert.match(rm.stdout, /deleted u\.vn\.test\.apps/);
  const gone = await run(["apps", "rm", "u.vn.test.apps", "--yes"], { config: cfg, env });
  assert.equal(gone.code, 1);
  assert.match(gone.stderr, /no such package/);
});

test("help --json describes login, upload and apps", async () => {
  const r = await run(["help", "--json"], { config: tmp("cfg") });
  const names = r.json.commands.map((c) => c.name);
  for (const n of ["login", "logout", "whoami", "upload", "apps"]) assert.ok(names.includes(n), n);
  assert.ok(r.json.environment.ESPOS_DEV_KEY);
  assert.ok(!JSON.stringify(r.json).includes("\u2026"), "no ellipsis character");
});
