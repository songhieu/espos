// A small argument parser and the command table format (also what `espos help --json` prints).
import { CliError, EXIT } from "./out.js";

export interface OptSpec {
  name: string; // long name without "--"
  short?: string; // one letter without "-"
  type: "bool" | "string" | "number";
  desc: string;
  arg?: string; // placeholder shown in help ("FILE")
  repeat?: boolean;
  values?: string[];
  default?: string | number | boolean;
  env?: string; // an environment variable that sets the same thing
}
export interface ArgSpec {
  name: string;
  desc: string;
  optional?: boolean;
  variadic?: boolean;
}
export interface Ctx {
  args: string[];
  opts: Record<string, any>;
  /** what came after "--" */
  rest: string[];
  cmd: CmdSpec;
  path: string[];
}
export interface CmdSpec {
  name: string;
  summary: string;
  description?: string;
  args?: ArgSpec[];
  opts?: OptSpec[];
  examples?: string[];
  notes?: string[];
  /** prints JSON with --json */
  json?: boolean;
  /** macOS only, needs hardware, preview... (shown in help) */
  platform?: string;
  sub?: CmdSpec[];
  /** "--" passes the rest to the wrapped tool */
  passthrough?: string;
  run?: (ctx: Ctx) => Promise<number | void> | number | void;
}

export const GLOBAL_OPTS: OptSpec[] = [
  { name: "help", short: "h", type: "bool", desc: "Show help for the command." },
  { name: "json", type: "bool", desc: "Machine-readable output on stdout (commands marked json)." },
  { name: "root", type: "string", arg: "DIR", desc: "The s3desk firmware repo (default: found from the current folder or the CLI's own place).", env: "ESPOS_ROOT" },
  { name: "quiet", short: "q", type: "bool", desc: "Less output." },
  { name: "no-color", type: "bool", desc: "No colours (also NO_COLOR=1)." },
];

/** Splits argv for one command: options (its own + global), positionals, "--" rest. */
export function parseArgs(argv: string[], cmd: CmdSpec): { args: string[]; opts: Record<string, any>; rest: string[] } {
  const specs = [...(cmd.opts ?? []), ...GLOBAL_OPTS];
  const byLong = new Map(specs.map((s) => [s.name, s]));
  const byShort = new Map(specs.filter((s) => s.short).map((s) => [s.short as string, s]));
  const opts: Record<string, any> = {};
  const args: string[] = [];
  let rest: string[] = [];
  const put = (s: OptSpec, v: string | boolean, raw: string) => {
    let val: any = v;
    if (s.type === "number") {
      val = Number(v);
      if (typeof v !== "string" || v.trim() === "" || !Number.isFinite(val)) throw new CliError(`${raw}: a number is needed, got ${JSON.stringify(v)}`, EXIT.USAGE);
    }
    if (s.values && typeof val === "string" && !s.values.includes(val))
      throw new CliError(`${raw}: one of ${s.values.join(", ")} (got ${JSON.stringify(val)})`, EXIT.USAGE);
    const key = camel(s.name);
    if (s.repeat) (opts[key] ??= []).push(val);
    else opts[key] = val;
  };
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (a === "--") {
      rest = argv.slice(i + 1);
      break;
    }
    if (a.startsWith("--") && a.length > 2) {
      const eq = a.indexOf("=");
      let name = eq > 0 ? a.slice(2, eq) : a.slice(2);
      let s = byLong.get(name);
      if (!s && name.startsWith("no-") && byLong.get(name.slice(3))?.type === "bool") {
        s = byLong.get(name.slice(3))!;
        opts[camel(s.name)] = false;
        continue;
      }
      if (!s) throw new CliError(`unknown option --${name} (espos ${cmdPathHint(cmd)} --help)`, EXIT.USAGE);
      if (s.type === "bool") {
        if (eq > 0) throw new CliError(`--${name} takes no value`, EXIT.USAGE);
        put(s, true, a);
      } else {
        const v = eq > 0 ? a.slice(eq + 1) : argv[++i];
        if (v === undefined) throw new CliError(`--${name} needs a value${s.arg ? " (" + s.arg + ")" : ""}`, EXIT.USAGE);
        put(s, v, `--${name}`);
      }
      name = "";
      continue;
    }
    if (a.startsWith("-") && a.length === 2 && !/[0-9]/.test(a[1])) {
      const s = byShort.get(a[1]);
      if (!s) throw new CliError(`unknown option ${a}`, EXIT.USAGE);
      if (s.type === "bool") put(s, true, a);
      else {
        const v = argv[++i];
        if (v === undefined) throw new CliError(`${a} needs a value`, EXIT.USAGE);
        put(s, v, a);
      }
      continue;
    }
    args.push(a);
  }
  for (const s of specs) {
    const key = camel(s.name);
    if (opts[key] === undefined && s.env && process.env[s.env] !== undefined && process.env[s.env] !== "") {
      put(s, s.type === "bool" ? process.env[s.env] !== "0" : (process.env[s.env] as string), `$${s.env}`);
    }
    if (opts[key] === undefined && s.default !== undefined) opts[key] = s.default;
  }
  // positionals
  const specArgs = cmd.args ?? [];
  const need = specArgs.filter((x) => !x.optional && !x.variadic).length;
  if (!opts.help) {
    if (args.length < need) throw new CliError(`missing ${specArgs[args.length].name.toUpperCase()} (espos ${cmdPathHint(cmd)} --help)`, EXIT.USAGE);
    const variadic = specArgs.some((x) => x.variadic);
    if (!variadic && args.length > specArgs.length) throw new CliError(`unexpected argument ${JSON.stringify(args[specArgs.length])}`, EXIT.USAGE);
  }
  return { args, opts, rest };
}

let currentPath = "";
export const setCmdPath = (p: string) => (currentPath = p);
const cmdPathHint = (cmd: CmdSpec) => currentPath || cmd.name;

export function camel(s: string): string {
  return s.replace(/-([a-z])/g, (_, c) => c.toUpperCase());
}

/** "espos device shot [OUT] [options]" */
export function usageLine(cmd: CmdSpec, path: string[]): string {
  const usageArgs = (cmd.args ?? []).map((a) => {
    const n = a.name.toUpperCase() + (a.variadic ? "..." : "");
    return a.optional || a.variadic ? `[${n}]` : n;
  });
  return `${["espos", ...path].join(" ")}${cmd.sub ? " <command>" : ""}${usageArgs.length ? " " + usageArgs.join(" ") : ""}${cmd.opts?.length ? " [options]" : ""}${cmd.passthrough ? " [-- ...]" : ""}`;
}

/** Help text of one command. */
export function helpText(cmd: CmdSpec, path: string[]): string {
  const full = ["espos", ...path].join(" ");
  const lines: string[] = [];
  const usageArgs = (cmd.args ?? []).map((a) => {
    const n = a.name.toUpperCase() + (a.variadic ? "..." : "");
    return a.optional || a.variadic ? `[${n}]` : n;
  });
  lines.push(usageLine(cmd, path));
  lines.push("");
  lines.push(cmd.summary);
  if (cmd.description) lines.push("", cmd.description);
  if (cmd.platform) lines.push("", `Platform: ${cmd.platform}`);
  if (cmd.sub) {
    lines.push("", "Commands:");
    const w = Math.max(...cmd.sub.map((s) => s.name.length));
    for (const s of cmd.sub) lines.push(`  ${s.name.padEnd(w)}  ${s.summary}`);
  }
  if (cmd.args?.length) {
    lines.push("", "Arguments:");
    const w = Math.max(...cmd.args.map((a) => a.name.length));
    for (const a of cmd.args) lines.push(`  ${a.name.toUpperCase().padEnd(w)}  ${a.desc}`);
  }
  const optLine = (o: OptSpec) => {
    const head = `${o.short ? "-" + o.short + ", " : "    "}--${o.name}${o.type !== "bool" ? " " + (o.arg ?? (o.type === "number" ? "N" : "VALUE")) : ""}`;
    const extra = [o.values ? `one of: ${o.values.join(", ")}` : "", o.default !== undefined && o.type !== "bool" ? `default ${o.default}` : "", o.env ? `env ${o.env}` : "", o.repeat ? "repeatable" : ""]
      .filter(Boolean)
      .join("; ");
    return [head, o.desc + (extra ? ` (${extra})` : "")];
  };
  const own = (cmd.opts ?? []).map(optLine);
  const glob = GLOBAL_OPTS.filter((o) => cmd.json || o.name !== "json").map(optLine);
  const w = Math.max(...[...own, ...glob].map((x) => x[0].length));
  if (own.length) {
    lines.push("", "Options:");
    for (const [h, d] of own) lines.push(`  ${h.padEnd(w)}  ${d}`);
  }
  lines.push("", "Global options:");
  for (const [h, d] of glob) lines.push(`  ${h.padEnd(w)}  ${d}`);
  if (cmd.passthrough) lines.push("", `After --: ${cmd.passthrough}`);
  if (cmd.notes?.length) {
    lines.push("", "Notes:");
    for (const n of cmd.notes) lines.push(`  - ${n}`);
  }
  if (cmd.examples?.length) {
    lines.push("", "Examples:");
    for (const e of cmd.examples) lines.push(`  ${e}`);
  }
  return lines.join("\n");
}
