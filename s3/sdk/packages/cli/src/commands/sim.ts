// espos sim: the package(s) installed into a simulator data folder, the SDL simulator built when needed and run
// with the app opened. Headless runs save screenshots and fail on a script error; --watch rebuilds and restarts.
import { spawn, type ChildProcess } from "node:child_process";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import type { CmdSpec, Ctx } from "../args.js";
import { hasErrors, packagesFrom, printProblems, show, summary } from "../common.js";
import { decodePpm, encodePng } from "../image.js";
import { installToFolder, readEspk, type ReadPkg } from "../espk.js";
import { CliError, dim, EXIT, green, note, red, say, warn, yellow } from "../out.js";
import { hint, isMac, needRoot, run, runSync, sdl2Config } from "../repo.js";

const BASES = ["data-live", "data-run", "data"];

/** The simulator binary, built with make when needed. */
export async function ensureSim(root: string, live: boolean, o: { build?: string; noBuild?: boolean }): Promise<string> {
  const build = o.build || process.env.ESPOS_SIM_BUILD || (live ? "build-live" : "build");
  const bin = path.join(root, "sim", build, live ? "espos-live" : "espos-sim");
  if (o.noBuild) {
    if (!fs.existsSync(bin)) throw new CliError(`${path.relative(root, bin)} is not built (drop --no-build)`, EXIT.MISSING);
    return bin;
  }
  if (!isMac) warn("the simulator builds on macOS only today: sim/main.cpp calls malloc_zone_statistics and the live build links -framework ApplicationServices (sim/Makefile); expect build errors here");
  if (!fs.existsSync(path.join(root, ".pio/libdeps/s3desk/lvgl")))
    throw new CliError("the simulator needs LVGL and ArduinoJson from PlatformIO's libdeps (.pio/libdeps/s3desk)", EXIT.MISSING, "run `espos firmware build` (or pio run -e s3desk) once to fetch them");
  if (live && !sdl2Config()) throw new CliError("the live simulator needs SDL2 (sdl2-config)", EXIT.MISSING, hint("sdl2"));
  const jobs = String(Math.max(2, os.cpus().length));
  note(dim(`building the simulator (make -C sim BUILD=${build}${live ? " live" : ""})`));
  const r = await run("make", ["-C", path.join(root, "sim"), `-j${jobs}`, `BUILD=${build}`, ...(live ? ["live"] : [])], { capture: true });
  if (r.code !== 0) {
    process.stderr.write(r.stdout.split("\n").slice(-30).join("\n") + r.stderr.split("\n").slice(-30).join("\n") + "\n");
    throw new CliError(`the simulator did not build (make exit ${r.code})`, EXIT.FAIL, "if a file under src/ is being edited by someone else, try again in a minute, or use --no-build with the last binary");
  }
  return bin;
}

/** A simulator data folder: --data DIR, or <project>/.espos/sim-<base>, copied from sim/<base> when new or --fresh. */
export function dataFolder(root: string, o: { data?: string; base?: string; fresh?: boolean; projectDir?: string }): string {
  const base = o.base ?? "data-live";
  const src = BASES.includes(base) ? path.join(root, "sim", base) : path.resolve(base);
  const dir = o.data ? path.resolve(o.data) : path.join(o.projectDir ?? process.cwd(), ".espos", `sim-${path.basename(src)}`);
  if (path.resolve(dir) === path.resolve(src)) throw new CliError(`--data ${dir} is the base folder itself; use a copy`, EXIT.USAGE);
  if (o.fresh && fs.existsSync(dir)) fs.rmSync(dir, { recursive: true, force: true });
  if (!fs.existsSync(dir)) {
    if (!fs.existsSync(src)) throw new CliError(`${src}: no such simulator data folder (--base ${BASES.join("|")}|DIR)`, EXIT.MISSING);
    note(dim(`new simulator data ${dir} (a copy of ${path.relative(root, src) || src})`));
    fs.mkdirSync(path.dirname(dir), { recursive: true });
    copyTree(src, dir);
  }
  return dir;
}

/**
 * A copy of a folder that shares the data blocks when the file system can (APFS clones with cp -c, reflinks on
 * Btrfs/XFS): sim/data-live is about 28 MB and a copy per project would add up. Node's cpSync does not clone.
 */
export function copyTree(src: string, dst: string): void {
  const r = isMac ? runSync("cp", ["-Rc", src, dst]) : process.platform === "linux" ? runSync("cp", ["-R", "--reflink=auto", src, dst]) : { code: 1, stdout: "", stderr: "" };
  if (r.code === 0 && fs.existsSync(dst)) return;
  fs.rmSync(dst, { recursive: true, force: true });
  fs.cpSync(src, dst, { recursive: true });
}

/** The J2ME runtime (dev.espos.j2me) in the data folder, installed from build-pkgs/ when a game needs it. */
function ensureJ2meRuntime(root: string, data: string): void {
  for (const d of ["sd/espos/pkgs", "fs/pkgs"]) if (fs.existsSync(path.join(data, d, "dev.espos.j2me/manifest.json"))) return;
  const dir = path.join(root, "build-pkgs");
  const cands = fs.existsSync(dir) ? fs.readdirSync(dir).filter((f) => /^dev\.espos\.j2me-.*\.espk$/.test(f)).sort() : [];
  if (!cands.length) throw new CliError("a Java game needs the runtime package dev.espos.j2me, and build-pkgs/ has none", EXIT.MISSING, "build it with `espos j2me runtime`");
  const f = path.join(dir, cands[cands.length - 1]);
  installToFolder(data, [readEspk(fs.readFileSync(f), f)], "sd");
  note(dim(`installed the J2ME runtime ${cands[cands.length - 1]} on the simulator's card`));
}

export interface SimRun {
  bin: string;
  live: boolean;
  data: string;
  script: string;
  out: string;
  ids: string[];
  args: string[];
}

/** The script: boot, open the (first) app, a first screenshot, then the user's commands. */
function writeScript(o: { dir: string; open?: string; arg?: string; user?: string; live: boolean }): string {
  const lines = ["# written by espos sim", "wait 600"];
  if (o.open) lines.push(`app ${o.open}${o.arg ? " " + o.arg : ""}`, "wait 1500");
  if (!o.live && o.open) lines.push("shot open");
  if (o.user) lines.push(`# ${o.user}`, ...fs.readFileSync(o.user, "utf8").split(/\r?\n/));
  fs.mkdirSync(o.dir, { recursive: true });
  const f = path.join(o.dir, "run-script.txt");
  fs.writeFileSync(f, lines.join("\n") + "\n");
  return f;
}

/** A JS error of one of our apps in a trace line ("app <id> start (main.js): ...", "app <id> render: ...", "app <id>: ...";
 *  a saver app's: "saver app <id> start (saver.js): ...", "saver app <id> error: ..."). */
export function appError(line: string, ids: string[]): string | null {
  const m = /\]\s+app (\S+)(?: start \((?:main\.js|prelude)\)| render)?: (.*)$/.exec(line) ??
    /\]\s+saver app (\S+) (?:start \([^)]*\)|error): (.*)$/.exec(line);
  if (!m || !ids.includes(m[1])) return null;
  return `${m[1]}: ${m[2]}`;
}

function ppmToPng(dir: string): string[] {
  const out: string[] = [];
  if (!fs.existsSync(dir)) return out;
  for (const f of fs.readdirSync(dir).filter((x) => x.endsWith(".ppm")).sort()) {
    const p = path.join(dir, f);
    try {
      const img = decodePpm(fs.readFileSync(p));
      const png = p.replace(/\.ppm$/, ".png");
      fs.writeFileSync(png, encodePng(img.width, img.height, img.data));
      fs.unlinkSync(p);
      out.push(png);
    } catch {
      out.push(p);
    }
  }
  return out;
}

async function headless(r: SimRun, o: { log?: boolean; timeout: number }): Promise<{ code: number; errors: string[]; shots: string[] }> {
  fs.rmSync(r.out, { recursive: true, force: true });
  fs.mkdirSync(r.out, { recursive: true });
  const errors: string[] = [];
  const p = spawn(r.bin, [r.script, r.out], { env: { ...process.env, SIM_DATA: r.data }, stdio: ["ignore", "pipe", "pipe"] });
  let buf = "";
  const onLine = (line: string) => {
    if (o.log) process.stderr.write(line + "\n");
    const e = appError(line, r.ids);
    if (e) {
      errors.push(e);
      if (!o.log) process.stderr.write(red("  app error: ") + e + "\n");
    } else if (!o.log && r.ids.some((id) => line.includes(`app ${id} started`) || line.includes(`pkg: run ${id}`))) note(dim("  " + line.trim()));
  };
  p.stderr.on("data", (d) => {
    buf += d;
    let i;
    while ((i = buf.indexOf("\n")) >= 0) {
      onLine(buf.slice(0, i));
      buf = buf.slice(i + 1);
    }
  });
  p.stdout.on("data", (d) => o.log && process.stdout.write(d));
  const timer = setTimeout(() => {
    errors.push(`the simulator ran longer than ${o.timeout} s and was stopped`);
    p.kill("SIGKILL");
  }, o.timeout * 1000);
  const code = await new Promise<number>((res) => p.on("close", (c, sig) => res(c ?? (sig ? 128 : 1))));
  clearTimeout(timer);
  if (buf) onLine(buf);
  if (code !== 0) errors.push(`the simulator exited with ${code}`);
  return { code, errors, shots: ppmToPng(r.out) };
}

function startLive(r: SimRun): ChildProcess {
  return spawn(r.bin, [...r.args, "--out", r.out, r.script], { env: { ...process.env, SIM_DATA: r.data }, stdio: "inherit" });
}

async function simCmd(c: Ctx): Promise<number> {
  if (c.opts.board && String(c.opts.board) !== "35")
    throw new CliError(`the SDL simulator draws only the 3.5" 320x480 board; the 2.8" board runs in QEMU: espos qemu --board 28 --install PKG.espk`, EXIT.USAGE);
  if (c.opts.live && c.opts.headless) throw new CliError("--live or --headless, not both", EXIT.USAGE);
  const live = !c.opts.headless; // the window by default, as a phone simulator
  const root = needRoot();
  const targets = c.args.length ? c.args : fs.existsSync("manifest.json") ? ["."] : [];
  const projectDirs = targets.filter((t) => fs.existsSync(t) && fs.statSync(t).isDirectory()).map((t) => path.resolve(t));
  const projectDir = projectDirs[0];
  const userScript = c.opts.script ? path.resolve(c.opts.script) : projectDir && fs.existsSync(path.join(projectDir, "sim/smoke.txt")) ? path.join(projectDir, "sim/smoke.txt") : undefined;
  if (c.opts.script && !fs.existsSync(userScript!)) throw new CliError(`${c.opts.script}: not found`, EXIT.USAGE);
  const data = dataFolder(root, { data: c.opts.data, base: c.opts.base, fresh: c.opts.fresh, projectDir });
  const work = path.join(projectDir ?? process.cwd(), ".espos");
  const out = path.resolve(c.opts.out ?? path.join(work, "shots"));

  const prepare = async (): Promise<string[] | null> => {
    if (!targets.length) return [];
    const { pkgs, problems } = await packagesFrom(targets, { write: true });
    const shown = problems.filter((p) => p.level !== "info");
    if (shown.length) printProblems(shown);
    if (hasErrors(problems) && !c.opts.force) {
      say(red("not installed") + `: ${summary(problems)} (--force installs anyway)`);
      return null;
    }
    install(pkgs);
    return [...new Set(pkgs.map((p) => String(p.plain.id)))];
  };
  const install = (pkgs: ReadPkg[]) => {
    for (const p of pkgs) if (p.plain.engine === "j2me" && p.plain.type !== "pack") ensureJ2meRuntime(root, data);
    const groups = new Map<string, ReadPkg[]>();
    for (const p of pkgs) groups.set(String(p.plain.id), [...(groups.get(String(p.plain.id)) ?? []), p]);
    for (const [id, parts] of groups) {
      const core = parts.find((p) => (p.plain.part ?? "core") === "core");
      const vol = (c.opts.vol as "sd" | "flash" | undefined) ?? (core?.plain.kind === "js" ? "flash" : "sd");
      // the other volume's copy would shadow or duplicate it
      for (const d of ["sd/espos/pkgs", "fs/pkgs"]) fs.rmSync(path.join(data, d, id), { recursive: true, force: true });
      for (const r of installToFolder(data, parts, vol)) note(`${green("installed")} ${r.id} (${r.files} files) on ${vol === "sd" ? "the card" : "the device's flash"}: ${show(r.dir)}`);
    }
  };

  let ids = await prepare();
  if (!ids) return EXIT.FAIL;
  const open = c.opts.open === false ? undefined : (c.opts.app as string) ?? ids[0];
  const bin = await ensureSim(root, live, { build: c.opts.simBuild, noBuild: c.opts.build === false });
  const script = writeScript({ dir: work, open, arg: c.opts.arg, user: userScript, live });
  const args: string[] = [];
  if (c.opts.fast) args.push("--fast");
  if (c.opts.zoom) args.push("--zoom", String(c.opts.zoom));
  const r: SimRun = { bin, live, data, script, out, ids, args };

  if (!live) {
    const once = async () => {
      note(dim(`running ${path.relative(root, bin)} headless${userScript ? ` with ${show(userScript)}` : ""}`));
      const res = await headless(r, { log: c.opts.log, timeout: c.opts.timeout });
      for (const s of res.shots) say(`  shot ${show(s)}`);
      if (res.errors.length) {
        say(red("FAIL") + `: ${res.errors.length} error${res.errors.length > 1 ? "s" : ""}`);
        for (const e of res.errors) say("  " + e);
        return EXIT.FAIL;
      }
      say(green("OK") + `: ${open ? `${open} opened` : "ran"} without script errors${res.shots.length ? `, ${res.shots.length} shot${res.shots.length > 1 ? "s" : ""}` : ""}`);
      return EXIT.OK;
    };
    const code = await once();
    if (!c.opts.watch) return code;
    await watch(projectDirs, async () => {
      ids = await prepare();
      if (ids) await once();
    });
    return EXIT.OK;
  }

  if (!isMac) warn("the live simulator's window sizing (sim/realsize.cpp) is macOS-only; elsewhere it falls back to a plain zoom");
  note(`live simulator: mouse = finger; arrows swipe, H home, B back, S shot (${show(out) || out}), M memory, Q quit; commands typed here too (help)`);
  if (!c.opts.watch) {
    const p = startLive(r);
    for (const sig of ["SIGTERM", "SIGHUP"] as const) process.once(sig, () => p.kill("SIGTERM"));
    return await new Promise<number>((res) => p.on("close", (code) => res(code === 0 ? EXIT.OK : EXIT.FAIL)));
  }
  note(yellow("--watch: a change rebuilds the package and restarts the simulator (it cannot reload an app inside a running simulator yet)"));
  let child = startLive(r);
  let restarting = false;
  // the simulator goes with us (a kill of espos alone would leave its window open)
  for (const sig of ["SIGINT", "SIGTERM", "SIGHUP"] as const)
    process.once(sig, () => {
      restarting = true;
      child.kill("SIGTERM");
      process.exit(130);
    });
  child.on("close", function onClose() {
    if (!restarting) process.exit(0);
  });
  await watch(projectDirs, async () => {
    const next = await prepare();
    if (!next) return; // keep the old one running
    restarting = true;
    const old = child;
    await new Promise<void>((res) => {
      old.once("close", () => res());
      old.kill("SIGTERM");
      setTimeout(() => old.kill("SIGKILL"), 2000);
    });
    restarting = false;
    child = startLive(r);
    child.on("close", () => {
      if (!restarting) process.exit(0);
    });
    note(green("restarted") + " with the new build");
  });
  return EXIT.OK;
}

/** Calls fn after files change in the folders (outputs and dotfolders ignored), one run at a time. */
function watch(dirs: string[], fn: () => Promise<void>): Promise<never> {
  if (!dirs.length) throw new CliError("--watch needs a project folder", EXIT.USAGE);
  let t: NodeJS.Timeout | null = null;
  let busy = false,
    again = false;
  const fire = async () => {
    if (busy) {
      again = true;
      return;
    }
    busy = true;
    try {
      await fn();
    } catch (e) {
      say(red("error: ") + (e as Error).message);
    }
    busy = false;
    if (again) {
      again = false;
      void fire();
    }
  };
  for (const d of dirs)
    fs.watch(d, { recursive: true }, (_ev, f) => {
      const n = String(f ?? "");
      if (!n || n.split(/[\\/]/).some((x) => x.startsWith(".") || x === "dist" || x === "node_modules")) return;
      if (t) clearTimeout(t);
      t = setTimeout(() => {
        note(dim(`changed: ${n}`));
        void fire();
      }, 300);
    });
  note(dim(`watching ${dirs.map((d) => show(d) || ".").join(", ")} (Ctrl-C stops)`));
  return new Promise<never>(() => {});
}

export const simCommand: CmdSpec = {
  name: "sim",
  summary: "Run packages in the SDL simulator: install, build the simulator if needed, open the app.",
  description:
    "Builds project folders (or takes .espk files), installs them into a simulator data folder (by default <project>/.espos/sim-data-live, a copy of s3/sim/data-live), builds the simulator with make when its sources changed, and runs it with a script that opens the first app. Headless runs write PNG screenshots and exit 1 when the app throws (\"app <id>: ...\" in the trace). JavaScript apps go to the device's flash (fs/pkgs), other packages to the card; Java games get the J2ME runtime from build-pkgs/.",
  args: [{ name: "targets", desc: "project folders or .espk files (default: the current folder if it has manifest.json)", variadic: true }],
  opts: [
    { name: "live", type: "bool", desc: "the window (the default; mouse = finger, keys, sound): sim/build-live/espos-live" },
    { name: "headless", type: "bool", desc: "no window, script time, screenshots, exit 1 on an app error (CI): sim/build/espos-sim" },
    { name: "script", short: "s", type: "string", arg: "FILE", desc: "simulator commands run after the app opens (default: <project>/sim/smoke.txt)" },
    { name: "app", type: "string", arg: "ID", desc: "the app to open (default: the first package)" },
    { name: "arg", type: "string", arg: "TEXT", desc: "app.arg for the opened app" },
    { name: "open", type: "bool", desc: "open the app (--no-open: only install and boot)", default: true },
    { name: "data", type: "string", arg: "DIR", desc: "the simulator data folder to use (default <project>/.espos/sim-<base>)" },
    { name: "base", type: "string", arg: "NAME", desc: "what a new data folder is copied from: data-live, data-run, data or a folder", default: "data-live" },
    { name: "fresh", type: "bool", desc: "start from a new copy of the base data" },
    { name: "vol", type: "string", arg: "VOL", desc: "where to install: sd (the card) or flash (the device's flash, JavaScript only)", values: ["sd", "flash"] },
    { name: "out", type: "string", arg: "DIR", desc: "screenshots (default <project>/.espos/shots)" },
    { name: "watch", short: "w", type: "bool", desc: "rebuild, reinstall and restart (live) or run again (headless) when a file changes" },
    { name: "build", type: "bool", desc: "build the simulator when needed (--no-build: use the last binary)", default: true },
    { name: "sim-build", type: "string", arg: "NAME", desc: "the simulator's build folder under sim/", env: "ESPOS_SIM_BUILD" },
    { name: "fast", type: "bool", desc: "live: the computer's speed instead of the board's" },
    { name: "zoom", type: "number", arg: "N", desc: "live: points per screen pixel" },
    { name: "board", type: "string", arg: "N", desc: "35 only: the 2.8\" board (28) runs in QEMU (espos qemu --board 28)" },
    { name: "log", type: "bool", desc: "headless: print the simulator's whole log" },
    { name: "timeout", type: "number", arg: "S", desc: "headless: stop after S seconds", default: 120 },
    { name: "force", type: "bool", desc: "install even when the checks find errors" },
  ],
  notes: [
    "--watch restarts the simulator: an app cannot be reloaded inside a running simulator yet.",
    "Script commands (sim/main.cpp): tap X Y, long X Y, drag X0 Y0 X1 Y1 MS, swipe left|right|up|down, back, home, app ID [ARG], wait MS, shot NAME, set KEY VALUE, rot N, saver, wake, mem, quit.",
    "macOS only today: the simulator uses malloc_zone_statistics (sim/main.cpp) and macOS frameworks (live window).",
  ],
  examples: ["espos sim", "espos sim --watch", "espos sim --headless", "espos sim dist/jar.megablock-1.0.0-core.espk --script play.txt", "espos sim . --headless --fresh --out shots"],
  run: simCmd,
};
