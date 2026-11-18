// Wrappers of the repo's own tools: firmware (PlatformIO), j2me (class library, runtime package, VM tests), doctor.
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import type { CmdSpec, Ctx } from "../args.js";
import { kb, show } from "../common.js";
import { bold, CliError, dim, emitJson, EXIT, green, red, say, state, yellow } from "../out.js";
import { findRoot, hint, isMac, needRoot, pio, qemuBinary, qemuEnv, run, runSync, sdl2Config, shq, which } from "../repo.js";

export const SLOT = 3_932_160; // app0/app1 in partitions_pawboop.csv (0x3C0000)

/** The app slot from the partition table platformio.ini names (app0's size), else the known 3,932,160. */
function appSlot(root: string): number {
  try {
    const ini = fs.readFileSync(path.join(root, "platformio.ini"), "utf8");
    const csv = /board_build\.partitions\s*=\s*(\S+)/.exec(ini)?.[1];
    if (csv) {
      const row = fs.readFileSync(path.join(root, csv), "utf8").split("\n").find((l) => /^\s*app0\s*,/.test(l));
      const size = row?.split(",")[4]?.trim();
      if (size) return Number(size);
    }
  } catch {
    /* the default */
  }
  return SLOT;
}

function firmwareBin(root: string, env: string, buildDir?: string): string {
  const base = buildDir ? path.resolve(buildDir) : process.env.PLATFORMIO_BUILD_DIR ? path.resolve(process.env.PLATFORMIO_BUILD_DIR) : path.join(root, ".pio/build");
  return path.join(base, env, "firmware.bin");
}

function sizeReport(root: string, env: string, file: string): { ok: boolean; info: Record<string, unknown> } {
  if (!fs.existsSync(file)) throw new CliError(`${file}: not built`, EXIT.MISSING, `espos firmware build --env ${env}`);
  const size = fs.statSync(file).size;
  const slot = appSlot(root);
  const ok = size <= slot;
  const info = { env, file, size, slot, free: slot - size, used_pct: Math.round((size / slot) * 1000) / 10, ok, built: fs.statSync(file).mtime.toISOString() };
  if (state.json) emitJson(info);
  else {
    const line = `${env}: firmware.bin ${size.toLocaleString("en")} bytes of the ${slot.toLocaleString("en")}-byte app slot (${info.used_pct}%), ${ok ? `${kb(slot - size)} free` : `${kb(size - slot)} OVER`}`;
    say(ok ? (slot - size < 64 * 1024 ? yellow(line) : line) : red(line));
    say(dim(`  ${show(file)}, built ${info.built}`));
  }
  return { ok, info };
}

async function firmwareBuild(c: Ctx): Promise<number> {
  const root = needRoot();
  const p = pio();
  if (!p) throw new CliError("PlatformIO (pio) not found", EXIT.MISSING, hint("pio"));
  const env = c.opts.env as string;
  const envVars = { ...process.env, ...(c.opts.buildDir ? { PLATFORMIO_BUILD_DIR: path.resolve(c.opts.buildDir) } : {}) };
  const argv = ["run", "-e", env, ...c.rest];
  if (c.opts.dryRun) {
    say(`${c.opts.buildDir ? `PLATFORMIO_BUILD_DIR=${shq(path.resolve(c.opts.buildDir))} ` : ""}${shq(p)} ${argv.map(shq).join(" ")}   (in ${root})`);
    return EXIT.OK;
  }
  const r = await run(p, argv, { cwd: root, env: envVars, echo: true });
  if (r.code !== 0) return EXIT.FAIL;
  return sizeReport(root, env, firmwareBin(root, env, c.opts.buildDir)).ok ? EXIT.OK : EXIT.FAIL;
}

async function firmwareSize(c: Ctx): Promise<number> {
  const root = needRoot();
  const env = c.opts.env as string;
  const file = c.opts.file ? path.resolve(c.opts.file) : firmwareBin(root, env, c.opts.buildDir);
  return sizeReport(root, env, file).ok ? EXIT.OK : EXIT.FAIL;
}

const ENV_OPT = { name: "env", short: "e", type: "string" as const, arg: "ENV", desc: "PlatformIO environment (platformio.ini): s3desk, s3desk-qemu, s3desk-28-qemu, ...", default: "s3desk" };
const BUILD_DIR_OPT = { name: "build-dir", type: "string" as const, arg: "DIR", desc: "PLATFORMIO_BUILD_DIR (e.g. .pio/build-you, so parallel builds do not collide)" };
const DRY = { name: "dry-run", type: "bool" as const, desc: "print the command, run nothing" };

export const firmwareCommand: CmdSpec = {
  name: "firmware",
  summary: "Build the firmware (PlatformIO) and check firmware.bin against the 3,932,160-byte app slot.",
  description: "No flashing here: the board is flashed by its owner. The size check fails (exit 1) when firmware.bin is bigger than the app slot (app0 in the partition table, 0x3C0000).",
  sub: [
    { name: "build", summary: "pio run -e ENV, then the size check.", opts: [ENV_OPT, BUILD_DIR_OPT, DRY], passthrough: "more arguments for pio run", json: true, examples: ["espos firmware build", "espos firmware build --env s3desk-qemu", "espos firmware build --build-dir .pio/build-me"], run: firmwareBuild },
    { name: "size", summary: "firmware.bin's size vs the app slot (exit 1 when over).", opts: [ENV_OPT, BUILD_DIR_OPT, { name: "file", type: "string", arg: "BIN", desc: "a firmware.bin to check instead" }], json: true, examples: ["espos firmware size", "espos firmware size --json"], run: firmwareSize },
  ],
};

// ---------------------------------------------------------------- j2me

async function sh(root: string, script: string, args: string[], dry: boolean): Promise<number> {
  const p = path.join(root, script);
  if (!fs.existsSync(p)) throw new CliError(`${script} not found`, EXIT.MISSING);
  if (dry) {
    say(`sh ${shq(script)} ${args.map(shq).join(" ")}   (in ${root})`);
    return EXIT.OK;
  }
  const r = await run("sh", [p, ...args], { cwd: root, echo: true });
  return r.code === 0 ? EXIT.OK : EXIT.FAIL;
}

function needJava(): void {
  if (!which("javac")) throw new CliError("javac (JDK 9+, 17 tested) not found", EXIT.MISSING, hint("java"));
}

export const j2meCommand: CmdSpec = {
  name: "j2me",
  summary: "Java (J2ME) engine tools: the runtime package, the MIDP layer's tests, the VM tests.",
  sub: [
    {
      name: "runtime",
      summary: "Build the runtime package dev.espos.j2me (class library) into build-pkgs/ (tools/j2me/make_runtime.sh).",
      args: [{ name: "version", desc: "default 1.0.0", optional: true }, { name: "code", desc: "default 10000", optional: true }],
      opts: [DRY],
      notes: ["Needs javac (JDK 9+) and zip."],
      run: async (c) => {
        const root = needRoot();
        if (!c.opts.dryRun) {
          needJava();
          if (!which("zip")) throw new CliError("zip not found", EXIT.MISSING, hint("zip"));
        }
        return sh(root, "tools/j2me/make_runtime.sh", c.args, !!c.opts.dryRun);
      },
    },
    {
      name: "test",
      summary: "Compile the MIDP layer and its logic tests (j2me/build-midp.sh tests); --run also runs them on the VM.",
      opts: [DRY, { name: "run", type: "bool", desc: "run each MIDP test class on tools/j2me/build/j2me-run (boot class path with the stand-in natives)" }],
      notes: ["Needs the class library's java.* in j2me/build/classlib (j2me/build.sh or espos j2me vm-test builds it)."],
      run: async (c) => {
        const root = needRoot();
        if (!c.opts.dryRun) needJava();
        const code = await sh(root, "j2me/build-midp.sh", ["tests"], !!c.opts.dryRun);
        if (code || !c.opts.run) return code;
        const vm = path.join(root, "tools/j2me/build/j2me-run");
        if (c.opts.dryRun) return EXIT.OK;
        if (!fs.existsSync(vm)) {
          const m = await run("make", ["-s", "-C", path.join(root, "tools/j2me"), `-j${os.cpus().length}`], { echo: true });
          if (m.code) return EXIT.FAIL;
        }
        const t = path.join(root, "j2me/build/test-midp");
        const classes = (function walk(d: string, pre = ""): string[] {
          return fs.readdirSync(d, { withFileTypes: true }).flatMap((e) => (e.isDirectory() ? walk(path.join(d, e.name), pre + e.name + ".") : /Test\.class$/.test(e.name) ? [pre + e.name.replace(/\.class$/, "")] : []));
        })(t);
        let fail = 0;
        for (const cls of classes) {
          const r = await run(vm, ["-boot", path.join(root, "j2me/build/test-midp-boot"), "-cp", t, cls], { capture: true, cwd: root });
          const ok = r.code === 0 && /^ALL OK$/m.test(r.stdout);
          say(`${ok ? green("PASS") : red("FAIL")} ${cls}`);
          if (!ok) {
            fail++;
            process.stderr.write((r.stdout + r.stderr).split("\n").slice(-15).join("\n") + "\n");
          }
        }
        say(`${classes.length - fail} passed, ${fail} failed`);
        return fail ? EXIT.FAIL : EXIT.OK;
      },
    },
    {
      name: "vm-test",
      summary: "Build the VM runner and run every VM test, compared with the JDK (j2me/test/run.sh).",
      opts: [DRY, { name: "asan", type: "bool", desc: "AddressSanitizer + UBSan build" }, { name: "gcstress", type: "bool", desc: "a full GC at every allocation" }, { name: "bench", type: "bool", desc: "also the benchmark" }],
      run: async (c) => {
        const root = needRoot();
        if (!c.opts.dryRun) needJava();
        return sh(root, "j2me/test/run.sh", [...(c.opts.asan ? ["asan"] : []), ...(c.opts.gcstress ? ["gcstress"] : []), ...(c.opts.bench ? ["-bench"] : [])], !!c.opts.dryRun);
      },
    },
  ],
};

// ---------------------------------------------------------------- doctor

interface Item {
  name: string;
  ok: boolean;
  detail: string;
  needed_for: string;
  required?: boolean;
  hint?: string;
  note?: string;
}

function ver(cmd: string, args: string[], env?: NodeJS.ProcessEnv): string | null {
  const r = runSync(cmd, args, undefined, env);
  if (r.code !== 0 && !r.stdout && !r.stderr) return null;
  return (r.stdout + r.stderr).split("\n").find((l) => l.trim())?.trim() ?? "";
}

async function doctorCmd(): Promise<number> {
  const items: Item[] = [];
  const add = (i: Item) => items.push(i);
  const nodeMajor = Number(process.versions.node.split(".")[0]);
  add({ name: "node", ok: nodeMajor >= 20, detail: process.version, needed_for: "everything", required: true, hint: "Node.js 20 or newer (nodejs.org, brew install node, or nvm)" });
  for (const [mod, why] of [["esbuild", "build (TypeScript, bundles, syntax checks)"], ["typescript", "build (type checks)"], ["jpeg-js", "create, jar (icons)"], ["pngjs", "jar, sim, device shot"]] as const) {
    let ok = true, detail = "";
    try {
      const m = await import(mod);
      detail = (m.version ?? m.default?.version ?? "loaded") as string;
    } catch (e) {
      ok = false;
      detail = (e as Error).message.split("\n")[0];
    }
    add({ name: `npm:${mod}`, ok, detail, needed_for: why, required: true, hint: "cd sdk && npm install" });
  }
  let sp = true, spd = "";
  try {
    await import("serialport");
    spd = "loaded";
  } catch (e) {
    sp = false;
    spd = (e as Error).message.split("\n")[0];
  }
  add({ name: "npm:serialport", ok: sp, detail: spd, needed_for: "device", hint: hint("serialport") });
  let root: string | null = null;
  try {
    root = findRoot();
  } catch (e) {
    add({ name: "s3desk repo", ok: false, detail: (e as Error).message, needed_for: "sim, qemu, firmware, j2me, store publish" });
  }
  if (root !== undefined && !items.some((i) => i.name === "s3desk repo"))
    add({ name: "s3desk repo", ok: !!root, detail: root ?? "not found", needed_for: "sim, qemu, firmware, j2me, store publish", hint: "run inside s3desk, --root DIR or ESPOS_ROOT" });
  const py = process.env.ESPOS_PYTHON || which("python3");
  add({ name: "python3", ok: !!py, detail: py ? `${py} ${ver(py, ["--version"]) ?? ""}` : "not found", needed_for: "store publish, qemu (mkimage.py), the repo's tools", hint: hint("python") });
  const javac = which("javac");
  add({ name: "javac (JDK)", ok: !!javac, detail: javac ? ver(javac, ["-version"]) ?? javac : "not found", needed_for: "j2me runtime/test/vm-test", hint: hint("java") });
  add({ name: "zip", ok: !!which("zip"), detail: which("zip") ?? "not found", needed_for: "j2me runtime", hint: hint("zip") });
  const cc = which("c++") || which("clang++") || which("g++");
  add({ name: "make + C++ compiler", ok: !!which("make") && !!cc, detail: `${which("make") ?? "no make"}, ${cc ?? "no c++"}`, needed_for: "sim, j2me vm-test", hint: hint("make") });
  const sdl = sdl2Config();
  add({ name: "SDL2", ok: !!sdl, detail: sdl ? `${sdl} ${ver(sdl, ["--version"]) ?? ""}` : "sdl2-config not found", needed_for: "sim --live, qemu viewer", hint: hint("sdl2") });
  const p = pio();
  add({ name: "PlatformIO (pio)", ok: !!p, detail: p ?? "not found", needed_for: "firmware, qemu; the simulator needs its libdeps (.pio/libdeps/s3desk)", hint: hint("pio") });
  if (root) {
    const libdeps = fs.existsSync(path.join(root, ".pio/libdeps/s3desk/lvgl"));
    add({ name: "LVGL/ArduinoJson libdeps", ok: libdeps, detail: libdeps ? ".pio/libdeps/s3desk" : "missing", needed_for: "sim", hint: "espos firmware build (pio fetches them once)" });
    for (const [b, bin, why] of [["build", "espos-sim", "sim --headless"], ["build-live", "espos-live", "sim --live"]] as const) {
      const f = path.join(root, "sim", b, bin);
      let detail = fs.existsSync(f) ? `sim/${b}/${bin}` : "not built (espos sim builds it)";
      if (fs.existsSync(f) && libdeps) {
        const q = runSync("make", ["-q", "-C", path.join(root, "sim"), `BUILD=${b}`, ...(b === "build-live" ? ["live"] : [])]);
        detail += q.code === 0 ? ", up to date" : ", out of date (rebuilt by espos sim)";
      }
      add({ name: `simulator (${b})`, ok: fs.existsSync(f), detail, needed_for: why, note: !isMac ? "builds on macOS only today (malloc_zone_statistics, -framework ApplicationServices)" : undefined });
    }
    const q = qemuBinary(root);
    const qv = q ? runSync(q, ["--version"], undefined, qemuEnv()) : null;
    const qok = !!qv && qv.code === 0;
    add({ name: "Espressif QEMU", ok: qok, detail: q ? `${q} ${qok ? qv!.stdout.split("\n")[0] : `does not start: ${(qv!.stderr || qv!.stdout).split("\n")[0]}`}` : "not found", needed_for: "qemu", hint: q ? "it needs SDL2's library (brew install sdl2; run.sh sets DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/opt/sdl2/lib)" : hint("qemu") });
    const fw = path.join(root, ".pio/build/s3desk/firmware.bin");
    if (fs.existsSync(fw)) {
      const n = fs.statSync(fw).size;
      add({ name: "firmware.bin (s3desk)", ok: n <= appSlot(root), detail: `${n.toLocaleString("en")} of ${appSlot(root).toLocaleString("en")} bytes`, needed_for: "release size budget" });
    }
  }
  const mt = which("mcopy");
  add({ name: isMac ? "hdiutil / mtools" : "mtools", ok: isMac || !!mt, detail: mt ? `mtools ${mt}` : isMac ? "hdiutil (built in)" : "not found", needed_for: "qemu --install (the card image)", hint: "Linux: sudo apt install mtools; macOS: brew install mtools (optional)" });
  if (!isMac) add({ name: "macOS-only parts", ok: false, detail: "the simulator (malloc_zone_statistics, ApplicationServices), tools/qemu/run.sh's viewer and mkimage.py's card (hdiutil)", needed_for: "sim, qemu", note: "known gaps; device, build, check, jar, store work here" });
  if (state.json) {
    emitJson({ ok: items.every((i) => i.ok || !i.required), platform: `${process.platform}-${process.arch}`, items });
  } else {
    say(bold(`espos doctor (${process.platform}-${process.arch})`));
    const w = Math.max(...items.map((i) => i.name.length));
    for (const i of items) {
      say(`${i.ok ? green("ok  ") : i.required ? red("MISS") : yellow("miss")}  ${i.name.padEnd(w)}  ${i.detail}${dim(`  [${i.needed_for}]`)}`);
      if (!i.ok && i.hint) say(dim(`        ${i.hint}`));
      if (i.note) say(dim(`        note: ${i.note}`));
    }
  }
  return items.some((i) => i.required && !i.ok) ? EXIT.MISSING : EXIT.OK;
}

export const doctorCommand: CmdSpec = {
  name: "doctor",
  summary: "Check the toolchain (node, npm packages, python, JDK, make, SDL2, PlatformIO, QEMU, simulator builds) with install hints.",
  json: true,
  run: doctorCmd,
};

