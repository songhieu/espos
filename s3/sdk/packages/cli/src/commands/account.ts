// Your own packages on espos.dev: login, logout, whoami, upload, apps (list, install, rm).
// The server side is web/backend (routes /v1/dev/*, guard require_dev_key); the web's "My apps" tab shows the same.
import os from "node:os";
import type { CmdSpec, Ctx } from "../args.js";
import { apiBase, call, confirm, credFile, deleteCreds, KEY_RE, loadCreds, needCreds, putSigned, readSecret, saveCreds, ServerError, type Creds } from "../account.js";
import { groupById, hasErrors, kb, packagesFrom, printProblems, summary } from "../common.js";
import type { Problem } from "../espk.js";
import { bold, CliError, dim, emitJson, EXIT, green, note, red, say, state, yellow } from "../out.js";
import { checkPrivate, privateId } from "../validate.js";

interface Device {
  id: number;
  name: string;
  fw: string;
  last_sync?: string;
}
interface Me {
  user_id: number;
  email: string;
  key: { id: number; label: string };
  devices: Device[];
  pkgs: number;
  limits: { pkgs: number; bytes: number; jar_bytes: number };
}
interface Pushed {
  id: number;
  name: string;
  action: "install" | "update" | "wait_fw" | "full";
  fw: string;
}
interface UserPkg {
  id: number;
  pkg_id: string;
  src_id: string;
  name: string;
  version: string;
  code: number;
  size: number;
  kind: string;
  engine: string;
  perms: string[];
  min_fw: number;
}

/** A path for messages, with the home folder as "~". */
const tilde = (p: string) => (p.startsWith(os.homedir() + "/") ? "~" + p.slice(os.homedir().length) : p);
const fwName = (code: number) => `${Math.floor(code / 10000)}.${Math.floor(code / 100) % 100}.${code % 100}`;
const shortFw = (fw: string) => fw.replace(/^espos-[a-z0-9]+-/, "") || "unknown";
const kindName = (k: string, e: string) => (k === "js" ? "JavaScript app" : e === "j2me" ? "Java game" : `${k}/${e}`);

function devLine(d: Pushed, minFw: number): string {
  const who = `${d.name} (#${d.id})`;
  switch (d.action) {
    case "install":
      return `  ${green("install")}  ${who}: at its next sync (about a minute when it is on)`;
    case "update":
      return `  ${green("update")}   ${who}: at its next sync`;
    case "wait_fw":
      return `  ${yellow("waiting")}  ${who}: runs ${shortFw(d.fw)}; this package needs ${fwName(minFw)}, it installs after a firmware update`;
    case "full":
      return `  ${red("skipped")}  ${who}: its package list is full (64 entries); remove something on the web first`;
  }
  return `  ${d.action} ${who}`;
}

// ---------------------------------------------------------------- login, logout, whoami

async function loginCmd(c: Ctx): Promise<number> {
  let key = "";
  if (c.opts.keyStdin) {
    let s = "";
    for await (const chunk of process.stdin) s += chunk;
    key = s.trim();
  } else if ((process.env.ESPOS_DEV_KEY ?? "").trim()) key = (process.env.ESPOS_DEV_KEY ?? "").trim();
  else if (process.stdin.isTTY) {
    note("Make a key on espos.dev: Account › Developer keys › New key. It is shown once.");
    key = await readSecret("Developer key: ");
  } else throw new CliError("no key: paste it when asked (in a terminal), or pipe it with --key-stdin", EXIT.USAGE);
  if (!KEY_RE.test(key)) throw new CliError("that is not a developer key (it starts with espos_dev_ and has 64 hex digits after it)", EXIT.USAGE);
  const saved = (() => {
    try {
      return loadCreds()?.api;
    } catch {
      return undefined;
    }
  })();
  const creds: Creds = { api: apiBase(c.opts.api, saved), key, from: "env" };
  const me = await call<Me>(creds, "GET", "/v1/dev/me");
  const file = saveCreds(creds.api, key);
  if (state.json) {
    emitJson({ ok: true, api: creds.api, email: me.email, key_label: me.key.label, devices: me.devices, saved: file });
    return EXIT.OK;
  }
  say(`${green("signed in")} to ${creds.api}${me.email ? ` as ${me.email}` : ""} with the key "${me.key.label}"`);
  say(`  saved in ${tilde(file)} (only you can read it); espos logout forgets it`);
  printDevices(me.devices);
  return EXIT.OK;
}

function printDevices(ds: Device[]): void {
  if (!ds.length) {
    say("  no EspOS S3 linked to this account yet: link one in its Settings › EspOS account");
    return;
  }
  say(`  S3 devices: ${ds.map((d) => `${d.name} (#${d.id}, ${shortFw(d.fw)})`).join(", ")}`);
}

async function logoutCmd(c: Ctx): Promise<number> {
  const had = deleteCreds();
  if (state.json) emitJson({ ok: true, removed: had, file: credFile() });
  else if (had) {
    say(`forgot the key (${tilde(credFile())})`);
    say("  it still works until you revoke it on espos.dev (Account › Developer keys)");
  } else say("no saved key");
  if (process.env.ESPOS_DEV_KEY) note("ESPOS_DEV_KEY is still set in this shell");
  return EXIT.OK;
}

async function whoamiCmd(c: Ctx): Promise<number> {
  const creds = needCreds(c.opts.api);
  const me = await call<Me>(creds, "GET", "/v1/dev/me");
  if (state.json) {
    emitJson({ ok: true, api: creds.api, from: creds.from, email: me.email, key_label: me.key.label, devices: me.devices, pkgs: me.pkgs, limits: me.limits });
    return EXIT.OK;
  }
  say(`${me.email || `account #${me.user_id}`} on ${creds.api} (key "${me.key.label}"${creds.from === "env" ? ", from ESPOS_DEV_KEY" : ""})`);
  printDevices(me.devices);
  say(`  ${me.pkgs} of ${me.limits.pkgs} packages uploaded; at most ${kb(me.limits.bytes)} each`);
  return EXIT.OK;
}

// ---------------------------------------------------------------- upload

async function uploadCmd(c: Ctx): Promise<number> {
  const targets = c.args.length ? c.args : ["."];
  const devices: number[] = (c.opts.device ?? []).map((x: string) => {
    const n = Number(x);
    if (!Number.isInteger(n) || n <= 0) throw new CliError(`--device ${x}: a device number (espos whoami lists them)`, EXIT.USAGE);
    return n;
  });
  if (devices.length && c.opts.install === false) throw new CliError("--device and --no-install go against each other", EXIT.USAGE);
  const creds = c.opts.dryRun ? null : needCreds(c.opts.api);
  const { pkgs, problems } = await packagesFrom(targets, { write: true });
  const groups = groupById(pkgs);
  if (groups.length !== 1 || groups[0].length !== 1)
    throw new CliError(`give one package (one folder or one .espk): got ${pkgs.length} files of ${groups.length} package${groups.length === 1 ? "" : "s"}`, EXIT.USAGE);
  const p = groups[0][0];
  const all: Problem[] = [...problems, ...(problems.some((x) => x.level === "error") ? [] : checkPrivate(p))];
  printProblems(all.filter((x) => x.level !== "info"));
  if (hasErrors(all)) {
    note(red("refused") + `: ${summary(all)}; nothing uploaded`);
    if (state.json) emitJson({ ok: false, problems: all });
    return EXIT.FAIL;
  }
  const m = p.plain;
  const id = privateId(String(m.id));
  const perms: string[] = Array.isArray(m.perms) ? m.perms : [];
  note(`${bold(id)} ${m.version} (code ${m.code}): ${kindName(m.kind, m.engine)}, ${kb(p.bytes.length)}, permissions: ${perms.length ? perms.join(", ") : "none"}`);
  if (id !== m.id) note(dim(`  your devices install it as ${id} (private: "u." is added to ${m.id})`));
  if (!creds) {
    if (state.json) emitJson({ ok: true, dry_run: true, pkg_id: id, version: m.version, code: m.code, size: p.bytes.length, sha256: p.sha256, perms });
    else say("dry run: checked, nothing uploaded");
    return EXIT.OK;
  }
  // a new package or an update: an update goes where it is already installed (and to --device)
  const list = await call<{ pkgs: UserPkg[] }>(creds, "GET", "/v1/dev/pkgs");
  const old = list.pkgs.find((x) => x.pkg_id === id);
  if (old && Number(m.code) <= old.code)
    throw new CliError(`code ${m.code} is not higher than the uploaded version's (${old.version}, code ${old.code})`, EXIT.FAIL, 'raise "code" in manifest.json (and "version"), then upload again');
  const install: unknown = devices.length ? devices : old || c.opts.install === false ? "" : "all";
  note(`uploading ${kb(p.bytes.length)} to ${creds.api}`);
  const s = await call<{ key: string; url: string }>(creds, "POST", "/v1/dev/pkgs/upload", { size: p.bytes.length });
  await putSigned(s.url, p.bytes);
  let r: { pkg: UserPkg & { sha256: string }; replaced: boolean; devices: Pushed[]; devices_error?: string };
  try {
    r = await call(creds, "POST", "/v1/dev/pkgs/confirm", { key: s.key, install });
  } catch (e) {
    if (e instanceof ServerError && e.problems.length) printProblems(e.problems.map((x) => ({ level: "error", code: x.code, where: "server", msg: x.msg })));
    throw e;
  }
  if (r.pkg.sha256 && r.pkg.sha256 !== p.sha256 && r.pkg.pkg_id === m.id) note(yellow("warning: ") + "the server's sha256 differs from the file's");
  if (state.json) {
    emitJson({ ok: true, pkg: r.pkg, replaced: r.replaced, devices: r.devices ?? [], devices_error: r.devices_error ?? null });
    return EXIT.OK;
  }
  say(`${green(r.replaced ? "updated" : "uploaded")} ${r.pkg.pkg_id} ${r.pkg.version} (code ${r.pkg.code})${old ? `, was ${old.version}` : ""}`);
  if (r.devices_error) say(yellow("  the package is kept, but the devices were not told: ") + `run espos apps install ${r.pkg.pkg_id}`);
  else if (!r.devices?.length)
    say(install === "" ? `  no device asked for it: espos apps install ${r.pkg.pkg_id} [--device ID]` : "  no EspOS S3 on this account to send it to (or each one already has it)");
  else for (const d of r.devices) say(devLine(d, r.pkg.min_fw));
  return EXIT.OK;
}

// ---------------------------------------------------------------- apps

interface DevState {
  id: number;
  name: string;
  fw: string;
  installed: Record<string, number>;
  wants: Record<string, { code: number; rm: boolean }>;
  job: { id: string; state: string; pct?: number; err?: string } | null;
}

async function appsListCmd(c: Ctx): Promise<number> {
  const creds = needCreds(c.opts.api);
  const r = await call<{ pkgs: UserPkg[]; devices: DevState[]; max: number }>(creds, "GET", "/v1/dev/pkgs?devices=1");
  if (state.json) {
    emitJson({ ok: true, ...r });
    return EXIT.OK;
  }
  if (!r.pkgs.length) {
    say("no packages yet: espos upload (in your app's folder) sends one");
    return EXIT.OK;
  }
  const w = Math.max(...r.pkgs.map((x) => x.pkg_id.length), 2);
  say(dim(`${"ID".padEnd(w)}  ${"VERSION".padEnd(10)} ${"CODE".padStart(10)}  ${"SIZE".padStart(8)}  KIND             PERMISSIONS`));
  for (const x of r.pkgs)
    say(`${x.pkg_id.padEnd(w)}  ${x.version.padEnd(10)} ${String(x.code).padStart(10)}  ${kb(x.size).padStart(8)}  ${kindName(x.kind, x.engine).padEnd(16)} ${x.perms.join(", ") || "none"}`);
  say(dim(`${r.pkgs.length} of ${r.max}`));
  for (const d of r.devices ?? []) {
    say(`\n${bold(d.name)} (#${d.id}, ${shortFw(d.fw)})`);
    for (const x of r.pkgs) {
      const have = d.installed[x.pkg_id];
      const want = d.wants[x.pkg_id];
      const job = d.job?.id === x.pkg_id ? d.job : null;
      let st = "";
      if (job) st = job.state === "error" ? red(`install failed: ${job.err ?? ""}`) : `${job.state} ${job.pct ?? 0}%`;
      else if (want?.rm) st = yellow("being removed");
      else if (have && have >= x.code) st = green(`installed (code ${have})`);
      else if (have && want && want.code >= x.code) st = yellow(`update asked (has ${have})`);
      else if (have) st = yellow(`older copy (code ${have}): espos apps install ${x.pkg_id} --device ${d.id}`);
      else if (want) st = yellow("install asked, at its next sync");
      else continue;
      say(`  ${x.pkg_id.padEnd(w)}  ${st}`);
    }
  }
  return EXIT.OK;
}

const deviceList = (c: Ctx): unknown => {
  const ds: string[] = c.opts.device ?? [];
  if (!ds.length) return "all";
  return ds.map((x) => {
    const n = Number(x);
    if (!Number.isInteger(n) || n <= 0) throw new CliError(`--device ${x}: a device number (espos whoami lists them)`, EXIT.USAGE);
    return n;
  });
};

async function appsInstallCmd(c: Ctx): Promise<number> {
  const creds = needCreds(c.opts.api);
  const id = privateId(c.args[0]);
  const r = await call<{ pkg_id: string; code: number; min_fw: number; devices: Pushed[] }>(creds, "POST", "/v1/dev/pkgs/install", { pkg_id: id, devices: deviceList(c) });
  if (state.json) {
    emitJson({ ok: true, ...r });
    return EXIT.OK;
  }
  if (!r.devices.length) say(`${id}: every device asked for already has it (or has it on the way)`);
  else for (const d of r.devices) say(devLine(d, r.min_fw));
  return EXIT.OK;
}

async function appsRmCmd(c: Ctx): Promise<number> {
  const creds = needCreds(c.opts.api);
  const id = privateId(c.args[0]);
  if (!c.opts.yes) {
    if (!process.stdin.isTTY) throw new CliError(`removing ${id} needs --yes (it is deleted from your account and from your devices)`, EXIT.USAGE);
    if (!(await confirm(`Delete ${id} from your account and remove it from your devices?`))) {
      say("nothing removed");
      return EXIT.OK;
    }
  }
  const r = await call<{ ok: boolean; devices: number; pkg_id: string }>(creds, "POST", "/v1/dev/pkgs/delete", { pkg_id: id });
  if (state.json) emitJson({ ok: true, pkg_id: r.pkg_id, devices: r.devices });
  else say(`${green("deleted")} ${r.pkg_id}; ${r.devices ? `${r.devices} device${r.devices === 1 ? "" : "s"} remove${r.devices === 1 ? "s" : ""} it at the next sync` : "no device had it"}`);
  return EXIT.OK;
}

// ---------------------------------------------------------------- specs

const API_OPT = { name: "api", type: "string" as const, arg: "URL", desc: "the server (default https://api.espos.dev, or the one saved by espos login)", env: "ESPOS_API" };
const DEVICE_OPT = { name: "device", type: "string" as const, arg: "ID", desc: "a device number from espos whoami (repeat for more)", repeat: true };

export const accountCommands: CmdSpec[] = [
  {
    name: "login",
    summary: "Save a developer key from espos.dev, for espos upload and espos apps (your own devices).",
    description:
      "Make the key on the web (Account › Developer keys › New key; it is shown once) and paste it when asked; nothing is shown while you paste. The key is checked with the server, then kept in credentials.json in the config folder (ESPOS_CONFIG_DIR, default ~/.config/espos), readable only by you. ESPOS_DEV_KEY in the environment is used instead when it is set (CI). Never your password.",
    opts: [
      { name: "key-stdin", type: "bool", desc: "read the key from stdin (a pipe) instead of asking" },
      API_OPT,
    ],
    json: true,
    examples: ["espos login", "pbpaste | espos login --key-stdin", "espos login --api http://localhost:8787"],
    run: loginCmd,
  },
  {
    name: "logout",
    summary: "Forget the saved developer key (revoke it on espos.dev to stop it working).",
    json: true,
    run: logoutCmd,
  },
  {
    name: "whoami",
    summary: "The account the developer key belongs to, its S3 devices and the upload limits.",
    opts: [API_OPT],
    json: true,
    run: whoamiCmd,
  },
  {
    name: "upload",
    summary: "Check a package and upload it to your espos.dev account: private, it installs only on your own devices.",
    description:
      'Builds a project folder (or takes an .espk), runs the validator plus the private-upload rules (U001, U002), and uploads it with the developer key. The server checks the package again. Your devices install it as "u." + its id. A new package goes to all your S3 devices unless --device or --no-install says otherwise; an update (a higher "code") goes to every device that has it, at its next sync. Nobody else sees it, and it is not in the store.',
    args: [{ name: "target", desc: "the project folder or .espk (default .)", optional: true }],
    opts: [
      DEVICE_OPT,
      { name: "install", type: "bool", desc: "ask the devices to install a new package (--no-install: only keep it in the account)", default: true },
      { name: "dry-run", type: "bool", desc: "check and print what would be uploaded; no key needed, nothing sent" },
      API_OPT,
    ],
    json: true,
    examples: ["espos upload", "espos upload dist/vn.you.timer-1.0.1-core.espk --device 13", "espos upload --dry-run"],
    notes: ["Limits: 4 MB a package, 2 MB for a Java game's jar, 30 packages an account. Raise \"code\" for every new version."],
    run: uploadCmd,
  },
  {
    name: "apps",
    summary: "Your uploaded packages and where they are installed; install or remove them.",
    opts: [API_OPT],
    json: true,
    run: appsListCmd,
    sub: [
      {
        name: "list",
        summary: "List your uploaded packages, and each S3 device's copy of them.",
        opts: [API_OPT],
        json: true,
        run: appsListCmd,
      },
      {
        name: "install",
        summary: "Ask your devices for one of your packages (all S3 devices, or --device).",
        args: [{ name: "id", desc: "the package id (u.vn.you.timer, or vn.you.timer)" }],
        opts: [DEVICE_OPT, API_OPT],
        json: true,
        examples: ["espos apps install u.vn.you.timer", "espos apps install vn.you.timer --device 13"],
        run: appsInstallCmd,
      },
      {
        name: "rm",
        summary: "Delete one of your packages from the account; your devices remove it at their next sync.",
        args: [{ name: "id", desc: "the package id (u.vn.you.timer, or vn.you.timer)" }],
        opts: [{ name: "yes", type: "bool", desc: "do not ask (needed when not in a terminal)" }, API_OPT],
        json: true,
        examples: ["espos apps rm u.vn.you.timer", "espos apps rm vn.you.timer --yes"],
        run: appsRmCmd,
      },
    ],
  },
];
