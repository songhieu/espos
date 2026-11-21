// The developer's espos.dev account, for packages that go to their own devices (espos login, upload, apps).
//
// The CLI holds a developer key, never a password: the person makes the key on the web (Account › Developer keys),
// where it is shown once, and pastes it into `espos login`. The server keeps only its sha256 and can revoke it.
// The key is kept in <config dir>/credentials.json (mode 0600), or read from ESPOS_DEV_KEY (CI); it is never printed,
// never put on a command line and only ever sent over HTTPS (or to this machine, for a local server).
import fs from "node:fs";
import path from "node:path";
import { CliError, EXIT } from "./out.js";
import { configDir } from "./repo.js";

export const DEFAULT_API = "https://api.espos.dev";
export const KEY_RE = /^espos_dev_[0-9a-f]{64}$/;

export interface Creds {
  api: string;
  key: string;
  /** where the key came from: the environment or the saved file */
  from: "env" | "file";
}

export const credFile = () => path.join(configDir(), "credentials.json");

/** The base URL: --api, then ESPOS_API, then the one saved with the key, then espos.dev. HTTPS, or HTTP to this machine. */
export function apiBase(opt?: string, saved?: string): string {
  const raw = (opt || process.env.ESPOS_API || saved || DEFAULT_API).trim().replace(/\/+$/, "");
  let u: URL;
  try {
    u = new URL(raw);
  } catch {
    throw new CliError(`--api ${raw}: not a URL`, EXIT.USAGE);
  }
  const local = ["localhost", "127.0.0.1", "[::1]"].includes(u.hostname);
  if (u.protocol !== "https:" && !(u.protocol === "http:" && local))
    throw new CliError(`${raw}: the key is only sent over https:// (or http:// to this machine)`, EXIT.USAGE);
  return raw;
}

function readSaved(): { api?: string; key?: string } | null {
  try {
    return JSON.parse(fs.readFileSync(credFile(), "utf8"));
  } catch {
    return null;
  }
}

/** The key to use, or null: ESPOS_DEV_KEY first, then the saved one. */
export function loadCreds(apiOpt?: string): Creds | null {
  const env = (process.env.ESPOS_DEV_KEY ?? "").trim();
  const saved = readSaved();
  if (env) {
    if (!KEY_RE.test(env)) throw new CliError("ESPOS_DEV_KEY is not a developer key (espos_dev_ and 64 hex digits)", EXIT.USAGE);
    return { api: apiBase(apiOpt, saved?.api), key: env, from: "env" };
  }
  if (saved?.key && KEY_RE.test(saved.key)) return { api: apiBase(apiOpt, saved.api), key: saved.key, from: "file" };
  return null;
}

export function needCreds(apiOpt?: string): Creds {
  const c = loadCreds(apiOpt);
  if (!c) throw new CliError("not signed in to espos.dev", EXIT.USAGE, "make a developer key on espos.dev (Account › Developer keys), then run: espos login");
  return c;
}

export function saveCreds(api: string, key: string): string {
  const f = credFile();
  fs.mkdirSync(path.dirname(f), { recursive: true, mode: 0o700 });
  const tmp = f + ".tmp";
  fs.writeFileSync(tmp, JSON.stringify({ api, key, saved_at: new Date().toISOString() }, null, 2) + "\n", { mode: 0o600 });
  fs.chmodSync(tmp, 0o600);
  fs.renameSync(tmp, f);
  return f;
}

export function deleteCreds(): boolean {
  try {
    fs.rmSync(credFile());
    return true;
  } catch {
    return false;
  }
}

/** A refusal from the server, with its stable code (the workflows' "code"), in English. */
export class ServerError extends CliError {
  constructor(
    readonly status: number,
    readonly srvCode: string,
    message: string,
    readonly problems: { code: string; msg: string }[] = [],
    hint?: string,
  ) {
    super(message, EXIT.FAIL, hint);
  }
}

/** The server's codes in English (its "error" is Vietnamese, for the web). */
function explain(status: number, d: Record<string, any>): { msg: string; hint?: string } {
  const p = (d.params ?? {}) as Record<string, any>;
  switch (d.code) {
    case "bad_key":
    case "not_signed_in":
      return { msg: "the developer key is wrong or was revoked", hint: "make a new one on espos.dev (Account › Developer keys), then run: espos login" };
    case "account_disabled":
      return { msg: "this account is blocked or being deleted" };
    case "pkg_too_big":
      return { msg: `the package is over ${p.max ?? 4194304} bytes (U001)` };
    case "pkg_limit":
      return { msg: `the account already has ${p.max ?? 30} packages`, hint: "remove one first: espos apps rm <id>" };
    case "pkg_not_newer":
      return { msg: `code ${p.code} is not higher than the uploaded version's ${p.have}`, hint: 'raise "code" in manifest.json (and "version"), build, upload again' };
    case "pkg_missing_file":
      return { msg: "the server did not get the file", hint: "upload again" };
    case "pkg_invalid":
      return { msg: `the server refused the package (${p.rule}): ${p.detail}` };
    case "pkg_not_found":
      return { msg: "no such package in your account", hint: "espos apps lists them" };
    case "pkg_id_mismatch":
    case "pkg_path":
    case "pkg_unreadable":
      return { msg: `the upload was refused (${d.code})`, hint: "upload again" };
    case "devices_invalid":
      return { msg: "say which devices: --device ID (repeat it) or none for all" };
  }
  if (status === 429) return { msg: "too many requests", hint: "wait a minute and try again" };
  if (status === 401) return { msg: "the developer key is wrong or was revoked", hint: "espos login" };
  if (status === 404) return { msg: "this server has no developer routes (an older or self-hosted server?)" };
  return { msg: `the server answered ${status}${typeof d.error === "string" && /^[\x20-\x7e]*$/.test(d.error) && d.error ? `: ${d.error}` : ""}` };
}

/** One call to the API with the developer key. */
export async function call<T = any>(c: Creds, method: "GET" | "POST", route: string, body?: unknown): Promise<T> {
  let res: Response;
  try {
    res = await fetch(c.api + route, {
      method,
      headers: { Authorization: `Bearer ${c.key}`, Accept: "application/json", ...(body !== undefined ? { "Content-Type": "application/json" } : {}) },
      body: body !== undefined ? JSON.stringify(body) : undefined,
    });
  } catch (e) {
    throw new CliError(`cannot reach ${c.api}: ${(e as Error).message}`, EXIT.FAIL, "check the network (or --api / ESPOS_API)");
  }
  const text = await res.text();
  let d: any = {};
  try {
    d = text ? JSON.parse(text) : {};
  } catch {
    d = {};
  }
  if (!res.ok) {
    const e = explain(res.status, d);
    throw new ServerError(res.status, String(d.code ?? ""), e.msg, Array.isArray(d.problems) ? d.problems : [], e.hint);
  }
  return d as T;
}

/** PUT bytes to a signed upload URL (no key: the URL carries its own signature). */
export async function putSigned(url: string, data: Uint8Array): Promise<void> {
  const u = new URL(url);
  if (u.protocol !== "https:" && !["localhost", "127.0.0.1", "[::1]"].includes(u.hostname)) throw new CliError(`refusing to upload to ${u.origin} (not https)`);
  let res: Response;
  try {
    res = await fetch(url, { method: "PUT", headers: { "Content-Type": "application/octet-stream" }, body: data });
  } catch (e) {
    throw new CliError(`upload failed: ${(e as Error).message}`, EXIT.FAIL, "try again");
  }
  if (!res.ok) throw new CliError(`upload failed (${res.status})`, EXIT.FAIL, "try again; a signed upload URL works for 15 minutes");
}

/** Reads a line from the terminal without showing it (the key), or all of stdin when it is not a terminal. */
export async function readSecret(prompt: string): Promise<string> {
  const stdin = process.stdin;
  if (!stdin.isTTY) {
    let s = "";
    for await (const chunk of stdin) s += chunk;
    return s.trim();
  }
  process.stderr.write(prompt);
  return new Promise((resolve, reject) => {
    let s = "";
    stdin.setRawMode(true);
    stdin.resume();
    stdin.setEncoding("utf8");
    const done = (v: string | null) => {
      stdin.setRawMode(false);
      stdin.pause();
      stdin.removeListener("data", on);
      process.stderr.write("\n");
      if (v === null) reject(new CliError("cancelled", EXIT.USAGE));
      else resolve(v.trim());
    };
    const on = (ch: string) => {
      for (const c of ch) {
        if (c === "\r" || c === "\n") return done(s);
        if (c === "\u0003" || c === "\u0004") return done(null);
        if (c === "\u007f" || c === "\b") s = s.slice(0, -1);
        else if (c >= " ") s += c;
      }
    };
    stdin.on("data", on);
  });
}

/** Asks yes/no on the terminal; false when stdin is not a terminal. */
export async function confirm(question: string): Promise<boolean> {
  if (!process.stdin.isTTY) return false;
  const rl = (await import("node:readline")).createInterface({ input: process.stdin, output: process.stderr });
  const a: string = await new Promise((r) => rl.question(`${question} [y/N] `, r));
  rl.close();
  return /^y(es)?$/i.test(a.trim());
}
