// Package commands: create, build, check, inspect, jar, types, store.
import fs from "node:fs";
import path from "node:path";
import type { CmdSpec, Ctx } from "../args.js";
import { buildChecked, fmtProblem, groupById, hasErrors, kb, outputPath, packagesFrom, printProblems, readEspkFile, show, summary } from "../common.js";
import { placeholderIcon } from "../image.js";
import { buildEspk, catalogEntry, readEspk, type Problem } from "../espk.js";
import { jarManifest, readJar, slug } from "../jar.js";
import { bold, CliError, emitJson, EXIT, green, note, red, say, state, warn } from "../out.js";
import { fromPlain } from "../pyjson.js";
import { findRoot, python, run, sdkDir, TEMPLATES_DIR } from "../repo.js";
import { CATEGORIES, checkPackage, countLevels, KEYPADS, RULES } from "../validate.js";

// ---------------------------------------------------------------- create

const TEMPLATES: Record<string, { summary: string }> = {
  "js-app": { summary: "JavaScript app (main.js, API level 1): a counter that keeps its number" },
  "js-app-ts": { summary: "TypeScript app (src/*.ts bundled into main.js, typed by @espos/sdk): a countdown with a reminder" },
  "j2me-game": { summary: "a J2ME (Java phone) game wrapped as a package: game.jar + icon, engine j2me (firmware 0.4.0)" },
};

const PALETTE: [string, string][] = [
  ["#60a5fa", "#2563eb"], ["#a78bfa", "#6d28d9"], ["#34d399", "#059669"], ["#fbbf24", "#d97706"],
  ["#f472b6", "#db2777"], ["#38bdf8", "#0284c7"], ["#fb7185", "#e11d48"], ["#4ade80", "#16a34a"],
];

function titleCase(s: string): string {
  return s.replace(/[-_]+/g, " ").replace(/\s+/g, " ").trim().replace(/\b\w/g, (c) => c.toUpperCase());
}

function hashPick<T>(s: string, arr: T[]): T {
  let h = 2166136261;
  for (const ch of s) h = Math.imul(h ^ ch.codePointAt(0)!, 16777619);
  return arr[(h >>> 0) % arr.length];
}

function readme(t: string, m: Record<string, any>, dirName: string): string {
  const common = `
## Commands

\`\`\`
espos check                 # the manifest, files, icon and permissions vs the APIs the code calls
espos build                 # dist/${m.id}-${m.version}-core.espk
espos sim --live            # the simulator in a window, the app opened (mouse = finger)
espos sim --headless        # a headless run: screenshots in .espos/shots, fails on a script error
espos sim --live --watch    # rebuilds and restarts the simulator when a file changes
espos qemu --install dist/${m.id}-${m.version}-core.espk   # the real firmware in QEMU, the package on its card
espos device app ${m.id}    # open it on a board (after installing it from the store)
\`\`\`

## Files

| file | what |
|---|---|
| \`manifest.json\` | the package's manifest (s3/docs/packages.md): id, name, version, code (raise it every release), perms, ... |
| \`espos.json\` | what \`espos build\` puts into the package |
| \`icon.jpg\` | 120x120 baseline JPEG (a placeholder: replace it; the device rounds the corners) |
| \`sim/smoke.txt\` | simulator commands run after the app opens (tap X Y, wait MS, shot NAME, ...) |
`;
  if (t === "js-app")
    return `# ${m.name}

An EspOS app in JavaScript (API level 1: \`ui\`, \`app\`, \`timer\`, \`sound\`, \`notify\`, \`toast\`, \`store\`).
The whole app is \`main.js\`: it sets \`app.render\`, which returns the screen as a tree of \`ui.*\` nodes, built again
after every tap, timer and \`app.refresh()\`.
${common}| \`main.js\` | the app |

Limits: 1.5 MB heap, 1.5 s per call (3 s to start), about 11 KB of stack, 8 timers; no console, no network, no
Promise callbacks yet. Types for your editor: \`espos types\` writes \`types/espos.d.ts\`.
`;
  if (t === "js-app-ts")
    return `# ${m.name}

An EspOS app in TypeScript. \`espos build\` type-checks \`src/\` against API level 1 (\`types/espos.d.ts\`,
from @espos/sdk) and bundles \`src/main.ts\` with its imports into the one \`main.js\` the device runs.
${common}| \`src/main.ts\` | the app (entry, \`espos.json\` "main") |
| \`src/countdown.ts\` | a module it imports |
| \`tsconfig.json\`, \`types/espos.d.ts\` | for your editor (\`espos types\` refreshes the typings) |

Limits: 1.5 MB heap, 1.5 s per call (3 s to start), about 11 KB of stack, 8 timers; no console, no network, and
Promise callbacks never run in API level 1 (no async/await).
`;
  return `# ${m.name}

A J2ME (Java phone) game as an EspOS package: \`game.jar\` runs on the \`j2me\` engine (firmware 0.4.0, the
runtime package \`dev.espos.j2me\` installs with the first game). The store shows the licence: say where the game
comes from and under what licence in \`manifest.json\` "license".
${common}| \`game.jar\` | the game (MIDP 2.0 / CLDC 1.1); \`espos jar game.jar --license ...\` fills the manifest from it |

The device reads the MIDlet class from the jar's \`META-INF/MANIFEST.MF\` (MIDlet-1); \`j2me.midlet\` is what the store
shows. Keys: s3/docs/j2me-games.md. In the simulator the runtime comes from \`build-pkgs/dev.espos.j2me-*.espk\`
(\`espos j2me runtime\` builds it).

Created by \`espos create ${dirName} --template j2me-game\`.
`;
}

async function createCmd(c: Ctx): Promise<number> {
  const dirArg = c.args[0];
  const t = c.opts.template as string;
  if (!TEMPLATES[t]) throw new CliError(`unknown template ${t} (known: ${Object.keys(TEMPLATES).join(", ")})`, EXIT.USAGE);
  const dir = path.resolve(dirArg);
  if (fs.existsSync(dir) && fs.readdirSync(dir).length && !c.opts.force) throw new CliError(`${dirArg} exists and is not empty (--force writes into it)`, EXIT.USAGE);
  const base = path.basename(dir);
  let jarInfo: ReturnType<typeof readJar> | null = null;
  let jarBytes: Buffer | null = null;
  if (t === "j2me-game" && c.opts.jar) {
    if (!c.opts.license) throw new CliError("--license TEXT is required with --jar: where the game comes from and under what licence (the store shows it)", EXIT.USAGE);
    jarBytes = fs.readFileSync(c.opts.jar);
    jarInfo = readJar(jarBytes, path.basename(c.opts.jar));
  } else if (t !== "j2me-game" && (c.opts.jar || c.opts.license)) throw new CliError("--jar and --license are for --template j2me-game", EXIT.USAGE);
  const name = (c.opts.name as string) ?? jarInfo?.name ?? titleCase(base);
  const id = (c.opts.id as string) ?? (t === "j2me-game" ? "jar." + slug(name) : "com.example." + slug(base).replace(/-/g, ""));
  const bg = hashPick(id, PALETTE);
  let m: Record<string, unknown>;
  if (t === "j2me-game") {
    if (jarInfo) m = jarManifest(jarInfo, { id, name, desc: c.opts.desc, license: c.opts.license, author: c.opts.author, keypad: c.opts.keypad });
    else
      m = {
        id, name, version: "1.0.0", code: 10000, kind: "native", engine: "j2me", min_fw: 400, category: "game",
        author: c.opts.author ?? "", desc: c.opts.desc ?? "", icon: { bg, glyph: "play" }, perms: ["sound"],
        j2me: { midlet: "", keypad: c.opts.keypad ?? "full", profile: "MIDP-2.0", config: "CLDC-1.1" },
        license: c.opts.license ?? "", part: "core",
      };
  } else {
    m = {
      id, name, version: "1.0.0", code: 10000, kind: "js", engine: "js", min_fw: 300, category: c.opts.category ?? "tool",
      author: c.opts.author ?? "", desc: c.opts.desc ?? (t === "js-app" ? "Counts, and remembers the count" : "A timer with a chime, even when the app is closed"),
      icon: { bg, glyph: t === "js-app" ? "grid" : "timer" }, perms: t === "js-app" ? ["sound"] : ["sound", "notify"], part: "core",
    };
  }
  if (!m.author) delete m.author;
  fs.mkdirSync(dir, { recursive: true });
  const w = (rel: string, data: string | Uint8Array) => {
    const p = path.join(dir, rel);
    fs.mkdirSync(path.dirname(p), { recursive: true });
    fs.writeFileSync(p, data);
  };
  const tpl = path.join(TEMPLATES_DIR, t);
  const sub = (s: string) => s.replace(/\{\{name\}\}/g, String(m.name).replace(/'/g, "\\'")).replace(/\{\{id\}\}/g, String(m.id));
  const copyTree = (from: string, rel = "") => {
    for (const e of fs.readdirSync(from, { withFileTypes: true })) {
      const r = rel ? `${rel}/${e.name}` : e.name;
      if (e.isDirectory()) copyTree(path.join(from, e.name), r);
      else w(r === "gitignore" ? ".gitignore" : r, sub(fs.readFileSync(path.join(from, e.name), "utf8")));
    }
  };
  copyTree(tpl);
  w("manifest.json", JSON.stringify(m, null, 2) + "\n");
  const files = t === "j2me-game" ? ["game.jar", "icon.jpg"] : ["icon.jpg"];
  const cfg: Record<string, unknown> = { $schema: "https://espos.dev/schemas/project.schema.json" };
  if (t === "js-app") cfg.main = "main.js";
  if (t === "js-app-ts") cfg.main = "src/main.ts";
  cfg.files = files;
  cfg.out = "dist";
  w("espos.json", JSON.stringify(cfg, null, 2) + "\n");
  w("icon.jpg", jarInfo?.icon ?? placeholderIcon(bg, t === "j2me-game" ? "play" : t === "js-app" ? "grid" : "circle"));
  if (jarBytes) w("game.jar", jarBytes);
  if (t === "js-app-ts") w("types/espos.d.ts", fs.readFileSync(path.join(sdkDir(), "espos.d.ts")));
  w("README.md", readme(t, m, path.basename(dirArg)));
  say(`${green("created")} ${show(dir) || "."} (${t}): ${m.id} "${m.name}"`);
  // the template must pass the checks it ships with
  if (t !== "j2me-game" || jarBytes) {
    const b = await buildChecked(dir);
    const ps = b.problems.filter((p) => p.level !== "info");
    if (ps.length) {
      printProblems(ps);
      if (hasErrors(ps)) return EXIT.FAIL;
    }
  } else {
    say(`  next: put the game in ${path.join(show(dir) || ".", "game.jar")} (or: espos create ${dirArg} --template j2me-game --jar GAME.jar --license TEXT --force)`);
    say(`  and fill manifest.json "j2me.midlet" (MIDlet-1's class) and "license"`);
  }
  if (String(m.id).startsWith("com.example.")) note(`  the id ${m.id} is a placeholder: set your own (--id vn.you.app); it never changes once published`);
  say(`  cd ${show(dir) || "."} && espos build && espos sim --live`);
  return EXIT.OK;
}

// ---------------------------------------------------------------- build

async function buildCmd(c: Ctx): Promise<number> {
  const dir = c.args[0] ?? ".";
  const b = await buildChecked(dir, { typecheck: c.opts.typecheck !== false, minify: c.opts.minify });
  const out = outputPath(b, { out: c.opts.out, outDir: c.opts.outDir });
  const failed = hasErrors(b.problems) || (c.opts.strict && b.problems.some((p) => p.level === "warning"));
  if (state.json) {
    if (!failed) {
      fs.mkdirSync(path.dirname(out), { recursive: true });
      fs.writeFileSync(out, b.bytes);
    }
    emitJson({ ok: !failed, out: failed ? null : out, id: b.pkg.plain.id, version: b.pkg.plain.version, part: b.pkg.plain.part ?? "core", size: b.bytes.length, sha256: b.pkg.sha256, files: b.pkg.files.map((f) => ({ name: f.name, size: f.size, crc32: f.crc.toString(16).padStart(8, "0") })), problems: b.problems });
    return failed ? EXIT.FAIL : EXIT.OK;
  }
  printProblems(b.problems.filter((p) => p.level !== "info" || !state.quiet));
  if (failed) {
    say(red(`build failed`) + `: ${summary(b.problems)}; nothing written`);
    return EXIT.FAIL;
  }
  fs.mkdirSync(path.dirname(out), { recursive: true });
  fs.writeFileSync(out, b.bytes);
  const m = b.pkg.plain;
  say(`${show(out)}: ${m.id} ${m.version} part ${m.part ?? "core"}, ${b.pkg.files.length} files, ${b.bytes.length} bytes`);
  if (!state.quiet) for (const f of b.pkg.files) say(`    ${f.name.padEnd(40)} ${String(f.size).padStart(9)} bytes${b.staged.sources.get(f.name)?.endsWith("(bundled)") ? "  (bundled)" : ""}`);
  say(`  sha256 ${b.pkg.sha256}`);
  return EXIT.OK;
}

// ---------------------------------------------------------------- check

async function checkCmd(c: Ctx): Promise<number> {
  if (c.opts.rules) {
    if (state.json) emitJson(RULES);
    else for (const r of RULES) say(`${r.code}  ${r.level.padEnd(7)}  ${r.text}  ${"[" + r.from + "]"}`);
    return EXIT.OK;
  }
  const targets = c.args.length ? c.args : ["."];
  const results: { target: string; id?: string; problems: Problem[] }[] = [];
  const espks: string[] = [];
  for (const t of targets) {
    if (fs.existsSync(t) && fs.statSync(t).isDirectory()) {
      const b = await buildChecked(t, { typecheck: c.opts.typecheck !== false });
      results.push({ target: t, id: b.pkg.plain.id, problems: b.problems });
    } else espks.push(t);
  }
  for (const g of groupById(espks.map(readEspkFile))) results.push({ target: g.map((p) => p.path).join(" + "), id: g[0].plain.id, problems: checkPackage(g) });
  const all = results.flatMap((r) => r.problems);
  const failed = hasErrors(all) || (c.opts.strict && all.some((p) => p.level === "warning"));
  if (state.json) {
    emitJson({ ok: !failed, results: results.map((r) => ({ ...r, ...countLevels(r.problems) })) });
    return failed ? EXIT.FAIL : EXIT.OK;
  }
  for (const r of results) {
    const n = countLevels(r.problems);
    say(`${n.errors ? red("FAIL") : green("OK  ")}  ${r.target}${r.id ? ` (${r.id})` : ""}: ${summary(r.problems)}`);
    printProblems(r.problems);
  }
  return failed ? EXIT.FAIL : EXIT.OK;
}

// ---------------------------------------------------------------- inspect

async function inspectCmd(c: Ctx): Promise<number> {
  const p = readEspkFile(c.args[0]);
  const problems = checkPackage([p]);
  const entry = (() => {
    try {
      return catalogEntry([p], { base: c.opts.base ?? "BASE_URL" });
    } catch {
      return null;
    }
  })();
  if (c.opts.extract) {
    const dir = path.resolve(c.opts.extract);
    for (const f of p.files) {
      const dst = path.join(dir, f.name);
      if (!dst.startsWith(dir + path.sep)) throw new CliError(`${f.name}: outside the folder, not extracted`);
      fs.mkdirSync(path.dirname(dst), { recursive: true });
      fs.writeFileSync(dst, f.data);
    }
    fs.writeFileSync(path.join(dir, "manifest.json"), p.manifestText);
    note(`extracted ${p.files.length} files + manifest.json into ${dir}`);
  }
  if (state.json) {
    emitJson({
      file: p.path, size: p.bytes.length, sha256: p.sha256, format: p.format, flags: p.flags, manifest: p.plain,
      files: p.files.map((f) => ({ name: f.name, off: f.off, size: f.size, crc32: f.crc.toString(16).padStart(8, "0") })),
      entry, problems,
    });
    return hasErrors(problems) ? EXIT.FAIL : EXIT.OK;
  }
  say(bold(p.name), `${p.bytes.length} bytes, format ${p.format}, flags ${p.flags}`);
  say(`sha256 ${p.sha256}`);
  say("\n" + bold("manifest"));
  say(JSON.stringify(p.plain, null, 2).replace(/^/gm, "  "));
  say("\n" + bold(`files (${p.files.length})`));
  say(`  ${"name".padEnd(40)} ${"offset".padStart(9)} ${"size".padStart(9)}  crc32`);
  for (const f of p.files) say(`  ${f.name.padEnd(40)} ${String(f.off).padStart(9)} ${String(f.size).padStart(9)}  ${f.crc.toString(16).padStart(8, "0")}`);
  if (entry) {
    say("\n" + bold(`catalog entry`) + (c.opts.base ? "" : " (--base URL for the real URLs)"));
    say(JSON.stringify(entry, null, 1).replace(/^/gm, "  "));
  }
  say("\n" + bold("checks") + `: ${summary(problems)}`);
  printProblems(problems);
  return hasErrors(problems) ? EXIT.FAIL : EXIT.OK;
}

// ---------------------------------------------------------------- jar

async function jarCmd(c: Ctx): Promise<number> {
  const file = c.args[0];
  if (!c.opts.license || !String(c.opts.license).trim())
    throw new CliError("--license TEXT is required: where the game comes from and under what licence (e.g. \"Apache-2.0, github.com/author/game\"); the store shows it", EXIT.USAGE);
  if (!fs.existsSync(file)) throw new CliError(`${file}: not found`, EXIT.USAGE);
  const bytes = fs.readFileSync(file);
  let info;
  try {
    info = readJar(bytes, path.basename(file));
  } catch (e) {
    throw new CliError(`${file}: ${(e as Error).message}`);
  }
  if (info.iconNote) warn(`icon: ${info.iconNote}; the store shows the colour tile`);
  const m = jarManifest(info, {
    id: c.opts.id, name: c.opts.name, desc: c.opts.desc, category: c.opts.category, keypad: c.opts.keypad, license: c.opts.license,
    version: c.opts.version, code: c.opts.code, author: c.opts.author,
  });
  const files = [{ name: "game.jar", data: bytes as Uint8Array }];
  if (info.icon) files.push({ name: "icon.jpg", data: info.icon });
  files.sort((a, b) => (a.name < b.name ? -1 : 1));
  const pkgBytes = buildEspk(fromPlain(m), files);
  const pkg = readEspk(pkgBytes, `${m.id}-${m.version}-core.espk`);
  const problems = checkPackage([pkg]);
  const failed = hasErrors(problems);
  let out: string | null = null;
  if (!failed) {
    if (c.opts.project) {
      const dir = path.resolve(c.opts.project);
      if (fs.existsSync(dir) && fs.readdirSync(dir).length && !c.opts.force) throw new CliError(`${c.opts.project} exists and is not empty (--force)`, EXIT.USAGE);
      fs.mkdirSync(dir, { recursive: true });
      fs.writeFileSync(path.join(dir, "manifest.json"), JSON.stringify(m, null, 2) + "\n");
      fs.writeFileSync(path.join(dir, "espos.json"), JSON.stringify({ $schema: "https://espos.dev/schemas/project.schema.json", files: files.map((f) => f.name), out: "dist" }, null, 2) + "\n");
      for (const f of files) fs.writeFileSync(path.join(dir, f.name), f.data);
      fs.mkdirSync(path.join(dir, "sim"), { recursive: true });
      fs.copyFileSync(path.join(TEMPLATES_DIR, "j2me-game/sim/smoke.txt"), path.join(dir, "sim/smoke.txt"));
      fs.writeFileSync(path.join(dir, ".gitignore"), ".espos/\ndist/\n");
      out = dir;
    } else {
      out = c.opts.out ? path.resolve(c.opts.out) : path.resolve(c.opts.outDir ?? ".", pkg.name);
      fs.mkdirSync(path.dirname(out), { recursive: true });
      fs.writeFileSync(out, pkgBytes);
    }
  }
  if (state.json) {
    emitJson({ ok: !failed, out, manifest: m, jar: { name: info.name, vendor: info.vendor, version: info.version, midlet: info.midlet, profile: info.profile, config: info.config, size: info.size, icon: info.iconFrom || null }, size: pkgBytes.length, sha256: pkg.sha256, problems });
    return failed ? EXIT.FAIL : EXIT.OK;
  }
  say(`${path.basename(file)}: "${info.name}" by ${info.vendor || "?"}, ${info.version}, ${[info.profile, info.config].filter(Boolean).join(" / ") || "no profile"}, MIDlet ${info.midlet || "?"}, icon ${info.iconFrom || "none"}`);
  printProblems(problems);
  if (failed) {
    say(red("not written") + `: ${summary(problems)} (fix with --name/--author/--id/... )`);
    return EXIT.FAIL;
  }
  say(`${c.opts.project ? "project" : "package"} ${show(out!) || out}: ${m.id} ${m.version} (code ${m.code}), ${pkgBytes.length} bytes, sha256 ${pkg.sha256}`);
  return EXIT.OK;
}

// ---------------------------------------------------------------- types

async function typesCmd(c: Ctx): Promise<number> {
  const dir = path.resolve(c.args[0] ?? ".");
  const dst = path.join(dir, "types/espos.d.ts");
  fs.mkdirSync(path.dirname(dst), { recursive: true });
  fs.copyFileSync(path.join(sdkDir(), "espos.d.ts"), dst);
  say(`${show(dst)}: API level 1 typings (@espos/sdk)`);
  return EXIT.OK;
}

// ---------------------------------------------------------------- store

async function storeEntryCmd(c: Ctx): Promise<number> {
  const { pkgs, problems } = await packagesFrom(c.args, { write: true });
  printProblems(problems.filter((p) => p.level !== "info"));
  if (hasErrors(problems)) {
    note(red("refused") + `: ${summary(problems)}`);
    return EXIT.FAIL;
  }
  const entries = groupById(pkgs).map((g) => catalogEntry(g, { base: c.opts.base, iconUrl: c.opts.iconUrl }));
  const text = JSON.stringify(entries.length === 1 ? entries[0] : entries, null, 1);
  if (c.opts.out) fs.writeFileSync(c.opts.out, text + "\n");
  process.stdout.write(text + "\n");
  return EXIT.OK;
}

function publishScript(): string {
  const p = process.env.ESPOS_PUBLISH_SCRIPT || (() => {
    const r = findRoot();
    return r ? path.resolve(r, "../scripts/publish_pkg.py") : "";
  })();
  if (!p || !fs.existsSync(p)) throw new CliError("scripts/publish_pkg.py not found next to s3desk", EXIT.MISSING, "set ESPOS_PUBLISH_SCRIPT=/path/to/publish_pkg.py");
  return p;
}

async function storePublishCmd(c: Ctx): Promise<number> {
  const { pkgs, problems } = await packagesFrom(c.args, { write: true });
  printProblems(problems.filter((p) => p.level !== "info"));
  if (hasErrors(problems)) {
    note(red("refused") + `: ${summary(problems)}; nothing uploaded`);
    return EXIT.FAIL;
  }
  const argv = [publishScript()];
  const dry = !c.opts.yes || c.opts.dryRun;
  if (dry) argv.push("--dry-run");
  if (c.opts.catalog) argv.push("--catalog", c.opts.catalog);
  if (c.opts.out) argv.push("--out", c.opts.out);
  if (c.opts.replace) argv.push("--replace");
  for (const s of c.opts.shot ?? []) argv.push("--shot", s);
  for (const s of c.opts.meta ?? []) argv.push("--meta", s);
  argv.push(...pkgs.map((p) => p.path));
  const env = { ...process.env };
  if (dry) {
    delete env.ESPOS_BBK; // a dry run never needs the key
    note(`dry run (nothing is uploaded; --yes uploads with the key in ESPOS_BBK)`);
  } else {
    const k = (process.env.ESPOS_BBK ?? "").trim();
    if (!k.startsWith("bbk_")) throw new CliError("--yes needs ESPOS_BBK set to a temporary bbk_ key in the environment (it is never printed or stored)", EXIT.USAGE);
    note(`uploading with the key from ESPOS_BBK (${k.length} characters)`);
  }
  const r = await run(python(), argv, { env });
  return r.code === 0 ? EXIT.OK : EXIT.FAIL;
}

// ---------------------------------------------------------------- specs

export const pkgCommands: CmdSpec[] = [
  {
    name: "create",
    summary: "Make a new package project from a template.",
    args: [{ name: "dir", desc: "the folder to create (its name is the default display name)" }],
    opts: [
      { name: "template", short: "t", type: "string", arg: "NAME", desc: "the kind of project", values: Object.keys(TEMPLATES), default: "js-app" },
      { name: "id", type: "string", arg: "ID", desc: "package id, reverse domain (default com.example.<dir>, jar.<name> for games); never changes once published" },
      { name: "name", type: "string", arg: "TEXT", desc: "display name (at most 40 characters, no \"…\")" },
      { name: "desc", type: "string", arg: "TEXT", desc: "one line for the store (at most 300 characters)" },
      { name: "author", type: "string", arg: "TEXT", desc: "author (at most 40 characters)" },
      { name: "category", type: "string", arg: "NAME", desc: "store category", values: CATEGORIES },
      { name: "jar", type: "string", arg: "FILE", desc: "j2me-game: the game's .jar (fills name, version, MIDlet, icon)" },
      { name: "license", type: "string", arg: "TEXT", desc: "j2me-game: where the game comes from and its licence (required with --jar)" },
      { name: "keypad", type: "string", arg: "NAME", desc: "j2me-game: the suggested keypad", values: KEYPADS },
      { name: "force", type: "bool", desc: "write into a folder that is not empty" },
    ],
    notes: [
      ...Object.entries(TEMPLATES).map(([k, v]) => `${k}: ${v.summary}`),
      "No widget, face, theme, service or driver templates: those package types need manifest v2 (planned, docs/platform.md), which no firmware reads yet.",
    ],
    examples: ["espos create my-counter", "espos create focus-timer --template js-app-ts --id vn.you.focus --name \"Focus timer\"", "espos create megablock --template j2me-game --jar MegaBlock.jar --license \"Apache-2.0, xylophones.net\""],
    run: createCmd,
  },
  {
    name: "build",
    summary: "Bundle (TypeScript), check and write the .espk.",
    description:
      "A project (espos.json) packs its main (bundled with esbuild when it is TypeScript) and its files; a plain folder with manifest.json packs every file but dotfiles and hidden folders, exactly as tools/espk.py does (byte-identical output). Nothing is written when a check fails.",
    args: [{ name: "dir", desc: "the project or package folder (default .)", optional: true }],
    opts: [
      { name: "out", short: "o", type: "string", arg: "FILE", desc: "the .espk to write (default: <out>/<id>-<version>-<part>.espk; for a plain folder, next to it)" },
      { name: "out-dir", type: "string", arg: "DIR", desc: "the folder to write it to" },
      { name: "typecheck", type: "bool", desc: "type-check TypeScript (--no-typecheck to skip)", default: true },
      { name: "minify", type: "bool", desc: "minify the bundled main.js" },
      { name: "strict", type: "bool", desc: "warnings fail the build too" },
    ],
    json: true,
    examples: ["espos build", "espos build ../apps/dice --out /tmp/dice.espk", "espos build --json | jq .sha256"],
    run: buildCmd,
  },
  {
    name: "check",
    summary: "Check packages: manifest, files, icon, sizes, permissions vs the APIs the code calls.",
    description:
      "The one validator: every rule of tools/espk.py, scripts/publish_pkg.py, the web admin and the device (src/pkg/espk.cpp), the strictest where they differ, plus checks of main.js (syntax, permissions, API level 1). Folders are built in memory first; .espk files with the same id are checked together as the parts of one package.",
    args: [{ name: "targets", desc: "folders or .espk files (default .)", variadic: true }],
    opts: [
      { name: "strict", type: "bool", desc: "warnings fail too" },
      { name: "typecheck", type: "bool", desc: "type-check TypeScript projects (--no-typecheck to skip)", default: true },
      { name: "rules", type: "bool", desc: "list the rules (code, level, what, where it comes from)" },
    ],
    json: true,
    examples: ["espos check", "espos check dist/*.espk", "espos check --rules"],
    run: checkCmd,
  },
  {
    name: "inspect",
    summary: "Show what is inside an .espk: manifest, files, offsets, CRCs, sha256, its catalog entry.",
    args: [{ name: "file", desc: "the .espk" }],
    opts: [
      { name: "base", type: "string", arg: "URL", desc: "base URL for the catalog entry's part URLs" },
      { name: "extract", type: "string", arg: "DIR", desc: "also unpack the files and manifest.json into DIR" },
    ],
    json: true,
    examples: ["espos inspect dist/vn.you.timer-1.0.0-core.espk", "espos inspect game.espk --extract /tmp/game"],
    run: inspectCmd,
  },
  {
    name: "jar",
    summary: "Wrap a J2ME game (.jar) into a game package (engine j2me), as the web admin does.",
    description:
      "Reads META-INF/MANIFEST.MF (MIDlet-Name, -Vendor, -Version, MIDlet-1, MIDlet-Icon, profile, configuration), makes the icon a 120x120 baseline JPEG on a tile of its average colour, and writes jar.<slug>-<version>-core.espk (kind native, engine j2me, min_fw 400, perms sound). The licence text is required.",
    args: [{ name: "file", desc: "the game's .jar" }],
    opts: [
      { name: "license", type: "string", arg: "TEXT", desc: "required: where the game comes from and its licence (\"GPL-3.0, github.com/a/b\", \"author's permission by email 2026-11-17\")" },
      { name: "id", type: "string", arg: "ID", desc: "package id (default jar.<slug of the name>)" },
      { name: "name", type: "string", arg: "TEXT", desc: "store name (default MIDlet-Name)" },
      { name: "desc", type: "string", arg: "TEXT", desc: "one line: how to play" },
      { name: "author", type: "string", arg: "TEXT", desc: "author (default MIDlet-Vendor)" },
      { name: "category", type: "string", arg: "NAME", desc: "store category", values: CATEGORIES, default: "game" },
      { name: "keypad", type: "string", arg: "NAME", desc: "suggested keypad", values: KEYPADS, default: "full" },
      { name: "version", type: "string", arg: "V", desc: "version (default MIDlet-Version, cleaned)" },
      { name: "code", type: "number", arg: "N", desc: "version code (default from the version: 1.2.3 = 10203)" },
      { name: "out", short: "o", type: "string", arg: "FILE", desc: "the .espk to write" },
      { name: "out-dir", type: "string", arg: "DIR", desc: "the folder for <id>-<version>-core.espk (default .)" },
      { name: "project", type: "string", arg: "DIR", desc: "write a project folder (manifest.json, espos.json, game.jar, icon.jpg) instead of an .espk" },
      { name: "force", type: "bool", desc: "with --project: write into a folder that is not empty" },
    ],
    json: true,
    examples: ["espos jar MegaBlock-1.0.0.jar --license \"Apache-2.0, xylophones.net\" --desc \"Falling blocks\"", "espos jar game.jar --license \"public domain\" --project games/space"],
    run: jarCmd,
  },
  {
    name: "types",
    summary: "Write the API level 1 typings (types/espos.d.ts) into a project, for the editor.",
    args: [{ name: "dir", desc: "the project (default .)", optional: true }],
    run: typesCmd,
  },
  {
    name: "store",
    summary: "Catalog entries and publishing to the store (wraps scripts/publish_pkg.py).",
    sub: [
      {
        name: "entry",
        summary: "Print the catalog \"packages\" entry for the parts of a package (tools/espk.py --entry with publish_pkg.py's part rules).",
        args: [{ name: "files", desc: ".espk parts (or project folders, built first)", variadic: true }],
        opts: [
          { name: "base", type: "string", arg: "URL", desc: "required: where the .espk files will be (URLs are BASE/<file name>)" },
          { name: "icon-url", type: "string", arg: "URL", desc: "the store picture's URL (icon_url)" },
          { name: "out", short: "o", type: "string", arg: "FILE", desc: "also write the entry to FILE" },
        ],
        json: true,
        examples: ["espos store entry dist/vn.you.timer-1.0.0-core.espk --base https://example.com/apps-s3"],
        run: (c) => {
          if (!c.opts.base) throw new CliError("--base URL is required", EXIT.USAGE);
          return storeEntryCmd(c);
        },
      },
      {
        name: "publish",
        summary: "Check, then publish packages with scripts/publish_pkg.py. Without --yes it is a dry run.",
        description:
          "Runs the validator first (refuses on errors), then publish_pkg.py. The upload key is read only from the environment variable ESPOS_BBK by publish_pkg.py itself; espos never takes it as an argument, prints it or stores it. Without --yes, publish_pkg.py --dry-run prints the plan and the merged catalog.",
        args: [{ name: "files", desc: ".espk parts (or project folders, built first)", variadic: true }],
        opts: [
          { name: "yes", type: "bool", desc: "really upload (needs ESPOS_BBK in the environment)" },
          { name: "dry-run", type: "bool", desc: "only print the plan (the default without --yes)" },
          { name: "catalog", type: "string", arg: "FILE", desc: "merge into this local catalog.json instead of the live one (offline check)" },
          { name: "out", type: "string", arg: "FILE", desc: "also write the merged catalog to FILE" },
          { name: "replace", type: "bool", desc: "allow the same version again with other bytes" },
          { name: "shot", type: "string", arg: "FILE.jpg", desc: "a store screenshot (baseline JPEG)", repeat: true },
          { name: "meta", type: "string", arg: "FILE.json", desc: "part choices from a build tool's catalog entry", repeat: true },
        ],
        notes: ["Needs python3 and ../scripts/publish_pkg.py next to s3desk (or ESPOS_PUBLISH_SCRIPT)."],
        examples: ["espos store publish dist/*.espk --catalog catalog.json", "ESPOS_BBK=bbk_... espos store publish dist/*.espk --yes"],
        run: storePublishCmd,
      },
    ],
  },
];
