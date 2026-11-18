// Where things are: the s3desk firmware repo, the SDK, the tools the CLI wraps. Nothing here is tied to one machine:
// the repo is found by walking up for platformio.ini + src/ (or ESPOS_ROOT / --root), tools come from PATH or an
// environment variable.
import { spawn, spawnSync, type SpawnOptions } from "node:child_process";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { CliError, EXIT, note } from "./out.js";

const here = path.dirname(fileURLToPath(import.meta.url));
/** packages/cli */
export const CLI_DIR = path.resolve(here, "..");
export const TEMPLATES_DIR = path.join(CLI_DIR, "templates");

export function sdkDir(): string {
  // @espos/sdk: the workspace copy, or wherever node resolves it.
  const ws = path.resolve(CLI_DIR, "../sdk");
  if (fs.existsSync(path.join(ws, "espos.d.ts"))) return ws;
  const nm = path.resolve(CLI_DIR, "node_modules/@espos/sdk");
  if (fs.existsSync(path.join(nm, "espos.d.ts"))) return nm;
  const up = path.resolve(CLI_DIR, "../../node_modules/@espos/sdk");
  if (fs.existsSync(path.join(up, "espos.d.ts"))) return up;
  throw new CliError("@espos/sdk not found next to the CLI", EXIT.MISSING, "run npm install in the sdk folder");
}

export function cliVersion(): string {
  try {
    return JSON.parse(fs.readFileSync(path.join(CLI_DIR, "package.json"), "utf8")).version;
  } catch {
    return "0.0.0";
  }
}

const isRoot = (d: string) => fs.existsSync(path.join(d, "platformio.ini")) && fs.existsSync(path.join(d, "src"));

let rootOverride: string | undefined;
export const setRootOverride = (d: string | undefined) => (rootOverride = d);

/** The s3desk repo, or null. */
export function findRoot(from = process.cwd()): string | null {
  const given = rootOverride ?? process.env.ESPOS_ROOT;
  if (given) {
    const d = path.resolve(given);
    if (!isRoot(d)) throw new CliError(`${d} is not the s3desk firmware repo (no platformio.ini + src/)`, EXIT.MISSING);
    return d;
  }
  for (const start of [from, CLI_DIR]) {
    let d = path.resolve(start);
    for (;;) {
      if (isRoot(d)) return d;
      const up = path.dirname(d);
      if (up === d) break;
      d = up;
    }
  }
  return null;
}

export function needRoot(): string {
  const r = findRoot();
  if (!r)
    throw new CliError("the s3desk firmware repo was not found", EXIT.MISSING,
      "run inside the s3desk folder, pass --root DIR, or set ESPOS_ROOT=/path/to/s3desk");
  return r;
}

// ---------------------------------------------------------------- tools

export function which(cmd: string): string | null {
  if (cmd.includes("/")) return fs.existsSync(cmd) ? cmd : null;
  for (const d of (process.env.PATH ?? "").split(path.delimiter)) {
    if (!d) continue;
    const p = path.join(d, cmd);
    try {
      fs.accessSync(p, fs.constants.X_OK);
      if (fs.statSync(p).isFile()) return p;
    } catch {
      /* next */
    }
  }
  return null;
}

export function python(): string {
  const p = process.env.ESPOS_PYTHON || which("python3") || which("python");
  if (!p) throw new CliError("python3 not found", EXIT.MISSING, hint("python"));
  return p;
}

export function pio(): string | null {
  return process.env.ESPOS_PIO || which("pio") || first([path.join(os.homedir(), ".platformio/penv/bin/pio")]);
}

/** Espressif's QEMU: $QEMU (as tools/qemu/run.sh), else ../tools-ext/qemu next to the repo, else PATH. */
export function qemuBinary(root: string): string | null {
  return process.env.QEMU || first([path.resolve(root, "../tools-ext/qemu/qemu/bin/qemu-system-xtensa")]) || which("qemu-system-xtensa");
}

export function sdl2Config(): string | null {
  return process.env.SDL2_CONFIG || first(["/opt/homebrew/bin/sdl2-config", "/usr/local/bin/sdl2-config"]) || which("sdl2-config");
}

function first(ps: string[]): string | null {
  for (const p of ps) if (fs.existsSync(p)) return p;
  return null;
}

export const isMac = process.platform === "darwin";

/** What tools/qemu/run.sh sets for the prebuilt QEMU (it looks for Homebrew's sdl2-compat). */
export function qemuEnv(): NodeJS.ProcessEnv {
  return isMac && !process.env.DYLD_FALLBACK_LIBRARY_PATH ? { ...process.env, DYLD_FALLBACK_LIBRARY_PATH: "/opt/homebrew/opt/sdl2/lib" } : process.env;
}

/** Install hints per tool, macOS and Linux. */
export function hint(tool: string): string {
  const H: Record<string, [string, string]> = {
    python: ["brew install python (or Xcode's command line tools)", "sudo apt install python3"],
    pio: ["pip install platformio, or the PlatformIO IDE extension (~/.platformio/penv/bin/pio)", "pip install platformio"],
    java: ["brew install openjdk@17", "sudo apt install openjdk-17-jdk-headless"],
    sdl2: ["brew install sdl2", "sudo apt install libsdl2-dev"],
    make: ["xcode-select --install", "sudo apt install build-essential"],
    zip: ["(built into macOS)", "sudo apt install zip"],
    qemu: [
      "Espressif QEMU (esp-develop-9.2.2) unpacked in ../tools-ext/qemu next to s3desk, or QEMU=/path/to/qemu-system-xtensa",
      "Espressif QEMU from github.com/espressif/qemu/releases (qemu-xtensa-softmmu, x86_64/aarch64 linux), then QEMU=/path/to/qemu-system-xtensa",
    ],
    serialport: ["cd sdk && npm install (the optional 'serialport' package)", "cd sdk && npm install (needs a C++ toolchain if no prebuilt binary fits)"],
    magick: ["brew install imagemagick", "sudo apt install imagemagick"],
  };
  const h = H[tool];
  if (!h) return "";
  return `install: macOS: ${h[0]}; Linux: ${h[1]}`;
}

// ---------------------------------------------------------------- running things

export interface RunResult {
  code: number;
  stdout: string;
  stderr: string;
}

/** Runs a command and waits; inherits stdio unless capture. */
export function run(cmd: string, argv: string[], o: SpawnOptions & { capture?: boolean; echo?: boolean } = {}): Promise<RunResult> {
  if (o.echo) note(`$ ${[cmd, ...argv].map(shq).join(" ")}`);
  return new Promise((resolve, reject) => {
    const p = spawn(cmd, argv, { stdio: o.capture ? ["ignore", "pipe", "pipe"] : "inherit", ...o });
    let stdout = "",
      stderr = "";
    p.stdout?.on("data", (d) => (stdout += d));
    p.stderr?.on("data", (d) => (stderr += d));
    p.on("error", (e: NodeJS.ErrnoException) =>
      reject(e.code === "ENOENT" ? new CliError(`${cmd}: not found`, EXIT.MISSING) : e));
    p.on("close", (code, sig) => resolve({ code: code ?? (sig ? 128 + (os.constants.signals[sig] ?? 1) : 1), stdout, stderr }));
  });
}

export function runSync(cmd: string, argv: string[], cwd?: string, env?: NodeJS.ProcessEnv): RunResult {
  const r = spawnSync(cmd, argv, { cwd, encoding: "utf8", env: env ?? process.env });
  return { code: r.status ?? 1, stdout: r.stdout ?? "", stderr: r.stderr ?? (r.error ? String(r.error) : "") };
}

export function shq(s: string): string {
  return /^[A-Za-z0-9_./:=@%+,-]+$/.test(s) ? s : `'${s.replace(/'/g, `'\\''`)}'`;
}

/** "~/x" and relative paths to absolute. */
export function abs(p: string, base = process.cwd()): string {
  if (p.startsWith("~/")) return path.join(os.homedir(), p.slice(2));
  return path.resolve(base, p);
}

export function configDir(): string {
  return process.env.ESPOS_CONFIG_DIR || path.join(process.env.XDG_CONFIG_HOME || path.join(os.homedir(), ".config"), "espos");
}
