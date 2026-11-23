// Assistant apps (EspOS Assistant Protocol v1, docs/assistant-providers.md §3.3): set up the provider of your own
// assistant app on espos.dev (secret, endpoint, status, pause, resume) and test a provider from your machine.
// The server side is web/backend (GET and POST /v1/dev/ai, guard require_dev_key); `espos ai test` needs no server:
// it signs example turns itself and checks the answers with the same rules the server applies (assistant.ts).
import crypto from "node:crypto";
import fs from "node:fs";
import path from "node:path";
import type { CmdSpec, Ctx } from "../args.js";
import { call, needCreds, readSecret } from "../account.js";
import { cleanResponse, signTurn } from "../assistant.js";
import { checkDef } from "../jsonschema.js";
import { sdkDir } from "../repo.js";
import { bold, CliError, dim, emitJson, EXIT, green, red, say, state, yellow } from "../out.js";

const API_OPT = { name: "api", type: "string" as const, arg: "URL", desc: "the server (default https://api.espos.dev, or the one saved by espos login)", env: "ESPOS_API" };

interface AppRow {
  pkg: string;
  name: string;
  set_up: boolean;
  endpoint?: string;
  secret_last4?: string;
  rotating_until_ms?: number;
  state?: string;
  last_status?: string;
  last_ok_ms?: number;
  resting_until_ms?: number;
  calls_24h?: { n: number; failed: Record<string, number>; median_ms: number };
}

/** A new signing secret: "esps_" and 43 URL-safe letters (32 random bytes). */
export function newSecret(): string {
  return "esps_" + crypto.randomBytes(32).toString("base64url");
}

function needPkg(c: Ctx): string {
  const pkg = c.args[0] ?? "";
  if (!/^u\.[a-z0-9][a-z0-9._-]{0,57}$/.test(pkg)) {
    throw new CliError(`${pkg || "(none)"}: your assistant app's package id as your devices know it ("u." + its id)`, EXIT.USAGE, "espos ai status lists them");
  }
  return pkg;
}

async function setUp(c: Ctx, body: Record<string, unknown>) {
  return call<{ pkg: string; endpoint: string; secret_last4: string; state: string; rotated: boolean }>(needCreds(c.opts.api), "POST", "/v1/dev/ai", body);
}

async function secretCmd(c: Ctx): Promise<number> {
  const pkg = needPkg(c);
  const secret = newSecret();
  const body: Record<string, unknown> = { pkg, secret };
  if (c.opts.endpoint) body.endpoint = c.opts.endpoint;
  const r = await setUp(c, body);
  if (state.json) {
    emitJson({ ok: true, ...r, secret });
    return EXIT.OK;
  }
  say(`Signing secret for ${bold(pkg)} (shown once):`);
  say(`  ${secret}`);
  say(`Put it in your provider as ESPOS_SIGNING_SECRET.`);
  if (r.rotated) say(dim("The previous secret still signs turns for 24 hours: switch your provider over within that time."));
  if (r.endpoint) say(dim(`Turns go to ${r.endpoint}.`));
  return EXIT.OK;
}

async function endpointCmd(c: Ctx): Promise<number> {
  const pkg = needPkg(c);
  const url = c.args[1] ?? "";
  if (!url) throw new CliError("give the endpoint URL (https, a public host)", EXIT.USAGE);
  const r = await setUp(c, { pkg, endpoint: url });
  if (state.json) emitJson({ ok: true, ...r });
  else say(`${bold(pkg)}: turns go to ${r.endpoint}`);
  return EXIT.OK;
}

async function stateCmd(c: Ctx, s: "active" | "disabled"): Promise<number> {
  const pkg = needPkg(c);
  const r = await setUp(c, { pkg, state: s });
  if (state.json) emitJson({ ok: true, ...r });
  else say(`${bold(pkg)}: ${s === "active" ? green("answering") : yellow("paused (PawBoop answers instead, or nobody)")}`);
  return EXIT.OK;
}

const ago = (ms?: number) => {
  if (!ms) return "never";
  const s = Math.max(0, Math.round((Date.now() - ms) / 1000));
  return s < 120 ? `${s} s ago` : s < 7200 ? `${Math.round(s / 60)} minutes ago` : `${Math.round(s / 3600)} hours ago`;
};

async function statusCmd(c: Ctx): Promise<number> {
  const r = await call<{ apps: AppRow[] }>(needCreds(c.opts.api), "GET", "/v1/dev/ai");
  if (state.json) {
    emitJson({ ok: true, apps: r.apps });
    return EXIT.OK;
  }
  if (!r.apps.length) {
    say('No assistant apps: upload one whose manifest says "provides": ["assistant"] (espos upload).');
    return EXIT.OK;
  }
  for (const a of r.apps) {
    say(`${bold(a.pkg)}  ${a.name}`);
    if (!a.set_up) {
      say(`  ${yellow("not set up")}: espos ai secret ${a.pkg} --endpoint <https URL>`);
      continue;
    }
    const st = a.state === "disabled" ? yellow("paused") : (a.resting_until_ms ?? 0) > Date.now() ? yellow("resting") : green("active");
    say(`  ${st}  ${a.endpoint}  secret ending ${a.secret_last4}${(a.rotating_until_ms ?? 0) > Date.now() ? dim(" (previous one valid until " + new Date(a.rotating_until_ms!).toISOString() + ")") : ""}`);
    const k = a.calls_24h;
    const failed = k ? Object.entries(k.failed).map(([code, n]) => `${code} ${n}`).join(", ") : "";
    say(`  last answer ${ago(a.last_ok_ms)}${a.last_status && a.last_status !== "ok" ? `, last status ${red(a.last_status)}` : ""}; 24 h: ${k?.n ?? 0} turns, median ${k?.median_ms ?? 0} ms${failed ? `, failed: ${failed}` : ""}`);
  }
  return EXIT.OK;
}

// ---------------------------------------------------------------- test

interface Example {
  turn: Record<string, any>;
  manifest: Record<string, any>;
}

const sdkFile = (name: string) => path.join(sdkDir(), "schemas", name);

const readJson = (p: string) => JSON.parse(fs.readFileSync(p, "utf8"));

interface Scenario {
  name: string;
  make(base: Record<string, any>): Record<string, any>;
  check?(resp: Record<string, any>): string;
  sign?: "wrong";
  wantStatus?: number[];
}

/** The turns `espos ai test` sends: built from the SDK's example turn, each changing what matters. */
export function scenarios(): Scenario[] {
  const text = (t: string) => (b: Record<string, any>) => {
    b.turn.input = { text: t, text_from: "typed" };
    b.turn.source = "typed";
    return b;
  };
  return [
    { name: "turn: weather", make: text("What's the weather like?") },
    {
      name: "turn: set an alarm",
      make: (b) => {
        text("Set an alarm for 6:30 tomorrow")(b);
        b.capabilities.actions = ["alarm.add", "alarm.update", "alarm.delete", "memory"];
        return b;
      },
      check: (r) => (r.actions?.some((a: any) => a.type === "alarm.add") ? "" : "no alarm.add action"),
    },
    {
      name: "turn: nothing heard",
      make: (b) => {
        b.turn.input = {};
        b.turn.source = "voice";
        return b;
      },
      check: (r) => (r.actions?.length ? "actions on a turn with nothing heard" : ""),
    },
    {
      name: "turn: held delete",
      make: (b) => {
        text("yes, delete them")(b);
        b.held = { actions: { say: "delete alarm #1; delete alarm #2" } };
        return b;
      },
      check: (r) => (r.held?.actions === "yes" ? "" : 'held.actions is not "yes"'),
    },
    { name: "signature: a wrong one refused", make: text("Hello"), sign: "wrong", wantStatus: [401, 403] },
  ];
}

async function post(url: string, secret: string, req: Record<string, any>, wrong: boolean, timeoutMs: number) {
  const body = JSON.stringify(req);
  const ts = Math.floor(Date.now() / 1000);
  const sig = signTurn(wrong ? secret + "x" : secret, ts, req.request_id, body);
  const ctl = new AbortController();
  const timer = setTimeout(() => ctl.abort(), timeoutMs);
  const t0 = Date.now();
  try {
    const res = await fetch(url, {
      method: "POST",
      redirect: "manual",
      signal: ctl.signal,
      headers: {
        "Content-Type": "application/json", Accept: "application/json", "EspOS-Protocol": "1",
        "EspOS-Request-Id": req.request_id, "EspOS-Timestamp": String(ts), "EspOS-Signature": `t=${ts},v1=${sig}`,
      },
      body,
    });
    const text = await res.text();
    let json: any = null;
    try {
      json = JSON.parse(text);
    } catch {
      json = null;
    }
    return { status: res.status, json, ms: Date.now() - t0, error: "" };
  } catch (e) {
    return { status: 0, json: null, ms: Date.now() - t0, error: ctl.signal.aborted ? `no answer within ${timeoutMs / 1000} s` : (e as Error).message };
  } finally {
    clearTimeout(timer);
  }
}

interface Result {
  name: string;
  ok: boolean;
  ms: number;
  detail: string;
}

async function testCmd(c: Ctx): Promise<number> {
  const url = c.args[0] ?? "";
  let u: URL;
  try {
    u = new URL(url);
  } catch {
    throw new CliError("give your provider's endpoint URL", EXIT.USAGE);
  }
  const local = ["localhost", "127.0.0.1", "[::1]"].includes(u.hostname);
  if (u.protocol !== "https:" && !local) throw new CliError(`${u.origin}: the hosted service sends turns only over https`, EXIT.USAGE);
  let secret = String(c.opts.secret ?? process.env.ESPOS_SIGNING_SECRET ?? "");
  if (!secret) secret = (await readSecret("Signing secret (espos ai secret made it): ")).trim();
  if (!secret) throw new CliError("no signing secret", EXIT.USAGE, "--secret, ESPOS_SIGNING_SECRET, or paste it when asked");
  const timeoutMs = Math.round(Number(c.opts.timeout ?? 12) * 1000);
  const schema = readJson(sdkFile("assistant-v1.schema.json"));
  const ex: Example = readJson(sdkFile("assistant-v1-examples.json"));
  const results: Result[] = [];
  // the manifest (optional, but the store asks for it)
  const m: { status: number; json: any } = await fetch(new URL("/.well-known/espos-assistant.json", u.origin), { redirect: "manual" }).then(
    async (r) => ({ status: r.status, json: r.ok ? await r.json().catch(() => null) : null }),
    () => ({ status: 0, json: null }),
  );
  if (m.json) {
    const p = checkDef(schema, "manifest", m.json);
    results.push({ name: "manifest", ok: p.length === 0, ms: 0, detail: p.length ? p.slice(0, 3).join("; ") : `protocol ${(m.json.protocol ?? []).join(",")}${m.json.output?.stream ? ", stream" : ""}` });
  } else {
    results.push({ name: "manifest", ok: false, ms: 0, detail: `no /.well-known/espos-assistant.json (${m.status || "unreachable"})` });
  }
  for (const s of scenarios()) {
    const req = s.make(structuredClone(ex.turn));
    req.request_id = "req_" + crypto.randomBytes(8).toString("hex");
    req.sent_ms = Date.now();
    req.deadline_ms = req.sent_ms + timeoutMs;
    const r = await post(url, secret, req, s.sign === "wrong", timeoutMs);
    if (s.wantStatus) {
      const ok = s.wantStatus.includes(r.status);
      results.push({ name: s.name, ok, ms: r.ms, detail: ok ? `refused (${r.status})` : `answered ${r.status || r.error}: a provider must refuse a wrong signature` });
      continue;
    }
    if (r.error || r.status !== 200) {
      results.push({ name: s.name, ok: false, ms: r.ms, detail: r.error || `HTTP ${r.status}${r.status >= 300 && r.status < 400 ? " (redirects are not followed)" : ""}` });
      continue;
    }
    const cleaned = cleanResponse(r.json, req);
    if (!cleaned.ok) {
      results.push({ name: s.name, ok: false, ms: r.ms, detail: `refused by the server's check: ${cleaned.error}` });
      continue;
    }
    const why = s.check?.(cleaned.response!) ?? "";
    const p = checkDef(schema, "response", r.json);
    const notes = [...cleaned.dropped.map((d) => `dropped ${d}`), ...p.slice(0, 2)];
    const resp = cleaned.response!;
    const summary = `mood ${resp.mood}${resp.actions.length ? `, ${resp.actions.length} action(s) ${resp.actions.map((a: any) => a.type).join(" ")}` : ""}`;
    results.push({ name: s.name, ok: !why && p.length === 0, ms: r.ms, detail: why || (notes.length ? notes.join("; ") : summary) });
  }
  const passed = results.filter((r) => r.ok).length;
  if (state.json) {
    emitJson({ ok: passed === results.length, url, passed, total: results.length, results });
  } else {
    for (const r of results) {
      say(`  ${r.name.padEnd(32)} ${r.ok ? green("ok  ") : red("FAIL")} ${r.ms ? String(r.ms).padStart(5) + " ms  " : "          "}${r.detail}`);
    }
    say(`${passed} of ${results.length} passed`);
  }
  return passed === results.length ? EXIT.OK : EXIT.FAIL;
}

export const aiCommand: CmdSpec = {
  name: "ai",
  summary: "Assistant apps: set up your provider on espos.dev (secret, endpoint, status, pause) and test it from here.",
  description:
    "An assistant app is a package whose manifest says \"provides\": [\"assistant\"]; its brain runs at your provider, which gets each turn as a signed EspOS Assistant Protocol v1 request. Your own private assistant apps (espos upload) are set up here; store apps are set up by review. The signing secret is made on your machine, sent once and shown once; a new one keeps the old one valid for 24 hours.",
  sub: [
    {
      name: "secret",
      summary: "Make a new signing secret for your assistant app and show it once (the previous one stays valid 24 hours).",
      args: [{ name: "pkg", desc: 'the app\'s package id on your devices ("u." + its id)' }],
      opts: [{ name: "endpoint", type: "string", arg: "URL", desc: "also set where turns go (needed the first time)" }, API_OPT],
      json: true,
      examples: ["espos ai secret u.homeai --endpoint https://ai.example.com/espos/turn", "espos ai secret u.homeai"],
      run: secretCmd,
    },
    {
      name: "endpoint",
      summary: "Set where your assistant app's turns go (https, a public host).",
      args: [{ name: "pkg", desc: "the app's package id" }, { name: "url", desc: "the endpoint" }],
      opts: [API_OPT],
      json: true,
      run: endpointCmd,
    },
    { name: "status", summary: "Your assistant apps: endpoint, secret's last 4, state, the last day's turns and failures.", opts: [API_OPT], json: true, run: statusCmd },
    { name: "pause", summary: "Stop sending turns to your provider (devices fall back).", args: [{ name: "pkg", desc: "the app's package id" }], opts: [API_OPT], json: true, run: (c) => stateCmd(c, "disabled") },
    { name: "resume", summary: "Send turns to your provider again.", args: [{ name: "pkg", desc: "the app's package id" }], opts: [API_OPT], json: true, run: (c) => stateCmd(c, "active") },
    {
      name: "test",
      summary: "Send signed example turns to your provider and check its answers as the server will (no device, no espos.dev).",
      description:
        "Checks the manifest (/.well-known/espos-assistant.json), then sends: a weather question, an alarm request, a turn with nothing heard, the answer to a held delete, and a turn with a wrong signature (which must be refused). Each answer is checked with the server's own rules (what would be dropped, what fails the turn) and the schema, within the deadline. Redirects are not followed. http is allowed only for localhost.",
      args: [{ name: "url", desc: "your provider's endpoint" }],
      opts: [
        { name: "secret", type: "string", arg: "S", desc: "the signing secret (or ESPOS_SIGNING_SECRET, or paste it when asked)", env: "ESPOS_SIGNING_SECRET" },
        { name: "timeout", type: "string", arg: "S", desc: "seconds to wait for each answer (default 12, the server's deadline)" },
      ],
      json: true,
      examples: ["espos ai test https://ai.example.com/espos/turn", "ESPOS_SIGNING_SECRET=<secret> espos ai test http://localhost:8787/turn"],
      run: testCmd,
    },
  ],
};
