// espos ai (assistant apps, docs/assistant-providers.md §3.3): `espos ai test` against local fake providers (a good
// one passes everything; a careless one fails by name), `espos ai secret` against a local fake API (the secret it
// makes, sends once and shows once). Everything on 127.0.0.1: no AI, no espos.dev.
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import http from "node:http";
import os from "node:os";
import path from "node:path";
import { test } from "node:test";
import { verifySignature } from "../dist/assistant.js";
import { newSecret } from "../dist/commands/ai.js";
import fs from "node:fs";
import { checkManifest } from "../dist/validate.js";
import { BIN, ROOT, tmp } from "./helpers.mjs";

const SECRET = "esps_" + "k".repeat(43);

/** espos, without blocking the event loop (the fake servers answer from this process). */
function run(args, env = {}) {
  return new Promise((resolve) => {
    const p = spawn(process.execPath, [BIN, ...args], {
      env: { ...process.env, NO_COLOR: "1", ESPOS_CONFIG_DIR: path.join(os.tmpdir(), "espos-test-config-ai"), ...env },
    });
    let out = "";
    let err = "";
    p.stdout.on("data", (d) => (out += d));
    p.stderr.on("data", (d) => (err += d));
    p.on("close", (code) => {
      let json;
      try {
        json = JSON.parse(out);
      } catch {}
      resolve({ code, stdout: out, stderr: err, json });
    });
  });
}

function serve(handler) {
  return new Promise((resolve) => {
    const s = http.createServer((req, res) => {
      let body = "";
      req.on("data", (d) => (body += d));
      req.on("end", () => handler(req, res, body));
    });
    s.listen(0, "127.0.0.1", () => resolve(s));
  });
}

const send = (res, status, obj) => {
  res.writeHead(status, { "Content-Type": "application/json" });
  res.end(JSON.stringify(obj));
};

const MANIFEST = { protocol: ["1"], name: "Test AI", endpoint: "https://ai.example.com/turn", scopes: { required: ["turn.text", "locale"] } };

/** A provider that follows the protocol. careless: takes any signature, acts on a turn with nothing heard. */
function provider(careless) {
  return (req, res, body) => {
    if (req.url === "/.well-known/espos-assistant.json") return send(res, 200, MANIFEST);
    const turn = JSON.parse(body || "{}");
    const now = Math.floor(Date.now() / 1000);
    if (!careless && !verifySignature(req.headers["espos-signature"] ?? "", SECRET, req.headers["espos-request-id"] ?? "", body, now)) {
      return send(res, 401, { error: { code: "unauthorized" } });
    }
    const text = turn.turn?.input?.text ?? "";
    const r = { v: "1", request_id: turn.request_id, heard: text, say: { text: "Light rain in Berlin, 4 degrees." }, mood: "happy", topic: "weather" };
    if (/alarm/i.test(text)) {
      r.say.text = "Alarm set for 6:30 tomorrow.";
      r.topic = "alarm";
      r.actions = [{ id: "a1", type: "alarm.add", args: { time: "06:30", days: "" } }];
    }
    if (turn.held?.actions) r.held = { actions: "yes" };
    if (!text) {
      r.say.text = "Say that again?";
      if (careless) r.actions = [{ id: "a1", type: "alarm.delete", args: { ref: "al_1" } }];
    }
    send(res, 200, r);
  };
}

test("a provider that follows the protocol passes every check", async () => {
  const s = await serve(provider(false));
  const url = `http://127.0.0.1:${s.address().port}/turn`;
  const r = await run(["ai", "test", url, "--json"], { ESPOS_SIGNING_SECRET: SECRET });
  s.close();
  assert.equal(r.code, 0, r.stdout + r.stderr);
  assert.equal(r.json.ok, true);
  assert.equal(r.json.passed, r.json.total);
  assert.deepEqual(r.json.results.map((x) => x.name), ["manifest", "turn: weather", "turn: set an alarm", "turn: nothing heard", "turn: held delete", "signature: a wrong one refused"]);
});

test("a careless provider fails by name", async () => {
  const s = await serve(provider(true));
  const url = `http://127.0.0.1:${s.address().port}/turn`;
  const r = await run(["ai", "test", url, "--json"], { ESPOS_SIGNING_SECRET: SECRET });
  s.close();
  assert.equal(r.code, 1);
  const failed = r.json.results.filter((x) => !x.ok).map((x) => x.name);
  assert.deepEqual(failed, ["turn: nothing heard", "signature: a wrong one refused"]);
});

test("http only for localhost; a missing endpoint is a usage error", async () => {
  const r = await run(["ai", "test", "http://ai.example.com/turn"], { ESPOS_SIGNING_SECRET: SECRET });
  assert.equal(r.code, 2);
  assert.match(r.stderr, /only over https/);
});

test("espos ai secret makes a secret, sends it once and shows it once", async () => {
  let got;
  const s = await serve((req, res, body) => {
    if (req.method === "POST" && req.url === "/v1/dev/ai" && req.headers.authorization === "Bearer espos_dev_" + "a1".repeat(32)) {
      got = JSON.parse(body);
      return send(res, 200, { pkg: got.pkg, endpoint: got.endpoint, secret_last4: got.secret.slice(-4), state: "active", rotated: false });
    }
    send(res, 404, {});
  });
  const api = `http://127.0.0.1:${s.address().port}`;
  const r = await run(["ai", "secret", "u.homeai", "--endpoint", "https://ai.example.com/turn", "--api", api], { ESPOS_DEV_KEY: "espos_dev_" + "a1".repeat(32) });
  s.close();
  assert.equal(r.code, 0, r.stderr);
  assert.match(got.secret, /^esps_[A-Za-z0-9_-]{43}$/);
  assert.equal(got.endpoint, "https://ai.example.com/turn");
  assert.ok(r.stdout.includes(got.secret), "shown once");
  assert.equal(r.stdout.split(got.secret).length - 1, 1);
});

test("newSecret", () => {
  const a = newSecret();
  assert.match(a, /^esps_[A-Za-z0-9_-]{43}$/);
  assert.notEqual(a, newSecret());
});

/** Starts a provider/server.mjs on a free port; resolves with its URL once it listens. */
function startProvider(file) {
  return new Promise((resolve, reject) => {
    const port = 20000 + Math.floor(Math.random() * 20000);
    const p = spawn(process.execPath, [file], { env: { ...process.env, ESPOS_SIGNING_SECRET: SECRET, PORT: String(port) } });
    p.stdout.on("data", (d) => String(d).includes("answers on") && resolve({ url: `http://127.0.0.1:${port}/espos/turn`, stop: () => p.kill() }));
    p.on("error", reject);
    setTimeout(() => reject(new Error("the provider did not start")), 5000);
  });
}

test("espos create --template assistant makes a provider that espos ai test passes", async () => {
  const d = tmp("assistant");
  const r = await run(["create", path.join(d, "my-ai"), "--template", "assistant", "--id", "vn.you.myai", "--name", "My AI"]);
  assert.equal(r.code, 0, r.stdout + r.stderr);
  const m = JSON.parse(fs.readFileSync(path.join(d, "my-ai", "manifest.json"), "utf8"));
  assert.deepEqual(m.provides, ["assistant"]);
  assert.ok(m.assistant.provider.startsWith("https://"));
  const prov = await startProvider(path.join(d, "my-ai", "provider", "server.mjs"));
  const t = await run(["ai", "test", prov.url, "--json"], { ESPOS_SIGNING_SECRET: SECRET });
  prov.stop();
  assert.equal(t.json?.passed, t.json?.total, t.stdout + t.stderr);
});

test("the repo's example (examples/assistant-echo) passes espos ai test", { skip: !fs.existsSync(path.join(ROOT, "examples/assistant-echo")) && "no repo" }, async () => {
  const prov = await startProvider(path.join(ROOT, "examples/assistant-echo/provider/server.mjs"));
  const t = await run(["ai", "test", prov.url, "--json"], { ESPOS_SIGNING_SECRET: SECRET });
  prov.stop();
  assert.equal(t.json?.passed, 6, t.stdout + t.stderr);
});

test("assistant permissions: only for assistant apps, and an assistant app needs its https provider", () => {
  const codes = (m) => checkManifest(m, "manifest.json").map((p) => p.code);
  const base = { id: "vn.you.x", name: "X", version: "1.0.0", code: 10000, kind: "js", engine: "js", min_fw: 300, icon: { bg: "#000000", glyph: "grid" } };
  assert.ok(codes({ ...base, perms: ["assist.alarms"] }).includes("M049"));
  assert.ok(codes({ ...base, provides: ["assistant"], perms: ["assist.alarms"] }).includes("M050"));
  const ok = codes({ ...base, provides: ["assistant"], assistant: { provider: "https://ai.example.com/t" }, perms: ["assist.alarms", "alarms.write", "data:weather", "location:city"] });
  assert.ok(!ok.includes("M049") && !ok.includes("M050") && !ok.includes("M048"), ok.join(","));
  assert.ok(codes({ ...base, provides: ["assistant"], assistant: { provider: "http://ai.example.com/t" } }).includes("M050"));
});

test("the scope table's permissions are exactly the ones an assistant app may declare", async () => {
  const { ASSIST_PERM } = await import("../dist/validate.js");
  const t = JSON.parse(fs.readFileSync(path.resolve(BIN, "../../../sdk/schemas/assistant-v1-scopes.json"), "utf8"));
  for (const p of Object.keys(t.permissions)) {
    const data = /^(location:city|data:)/.test(p);
    assert.equal(ASSIST_PERM.test(p), !data, p);
  }
});
