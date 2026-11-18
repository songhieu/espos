// A package's source: a project (espos.json + manifest.json + sources) or a plain folder packed as tools/espk.py
// packs it (every file except dotfiles, hidden folders and manifest.json).
//
// espos.json:
//   {"main": "src/main.ts",          JS/TS entry, becomes main.js (bundled with esbuild when .ts or "bundle": true)
//    "bundle": false,                bundle a .js entry and its imports too
//    "files": ["icon.jpg", "data"],  copied into the package as they are (folders keep their structure)
//    "out": "dist",                  where `espos build` writes <id>-<version>-<part>.espk
//    "minify": false}
import esbuild from "esbuild";
import fs from "node:fs";
import path from "node:path";
import ts from "typescript";
import { buildEspk, folderFiles, readEspk, type PkgFile, type Problem, type ReadPkg } from "./espk.js";
import { CliError, EXIT } from "./out.js";
import { JsonError, parse, PyObj, toPlain, type PyVal } from "./pyjson.js";
import { sdkDir } from "./repo.js";

export interface ProjectConfig {
  main?: string;
  bundle?: boolean;
  files?: string[];
  out?: string;
  minify?: boolean;
}

export interface Project {
  dir: string;
  mode: "project" | "folder";
  config: ProjectConfig;
}

const CONFIG_KEYS = new Set(["$schema", "main", "bundle", "files", "out", "minify"]);

export function loadProject(dir: string): Project {
  const d = path.resolve(dir);
  if (!fs.existsSync(d) || !fs.statSync(d).isDirectory()) throw new CliError(`${dir}: not a folder`, EXIT.USAGE);
  const cfgPath = path.join(d, "espos.json");
  if (fs.existsSync(cfgPath)) {
    let cfg: any;
    try {
      cfg = JSON.parse(fs.readFileSync(cfgPath, "utf8"));
    } catch (e) {
      throw new CliError(`${cfgPath}: not JSON (${(e as Error).message})`);
    }
    if (!cfg || typeof cfg !== "object" || Array.isArray(cfg)) throw new CliError(`${cfgPath}: a JSON object is needed`);
    for (const k of Object.keys(cfg)) if (!CONFIG_KEYS.has(k)) throw new CliError(`${cfgPath}: unknown key "${k}" (known: ${[...CONFIG_KEYS].join(", ")})`);
    if (cfg.files !== undefined && (!Array.isArray(cfg.files) || cfg.files.some((f: unknown) => typeof f !== "string")))
      throw new CliError(`${cfgPath}: "files" is a list of paths`);
    if (!fs.existsSync(path.join(d, "manifest.json"))) throw new CliError(`${d}: no manifest.json next to espos.json`);
    return { dir: d, mode: "project", config: cfg };
  }
  if (fs.existsSync(path.join(d, "manifest.json"))) return { dir: d, mode: "folder", config: {} };
  throw new CliError(`${d}: no manifest.json (and no espos.json): not a package folder`, EXIT.USAGE, "espos create NAME makes a new project");
}

export function readManifestFile(file: string): { text: string; value: PyVal; plain: Record<string, any> } {
  const raw = fs.readFileSync(file);
  let text: string;
  try {
    text = new TextDecoder("utf-8", { fatal: true }).decode(raw);
  } catch {
    throw new CliError(`${file}: not UTF-8`);
  }
  let value: PyVal;
  try {
    value = parse(text);
  } catch (e) {
    throw new CliError(`${file}: ${e instanceof JsonError ? e.message : String(e)}`);
  }
  if (!(value instanceof PyObj)) throw new CliError(`${file}: the manifest must be a JSON object`);
  return { text, value, plain: toPlain(value) as Record<string, any> };
}

export interface Staged {
  manifest: PyVal;
  plain: Record<string, any>;
  files: PkgFile[];
  /** problems found while staging (type errors) */
  problems: Problem[];
  /** where each package file came from */
  sources: Map<string, string>;
}

export interface StageOpts {
  typecheck?: boolean;
  minify?: boolean;
}

/** Collects what goes into the package (in tools/espk.py's order). */
export async function stage(p: Project, o: StageOpts = {}): Promise<Staged> {
  const man = readManifestFile(path.join(p.dir, "manifest.json"));
  const problems: Problem[] = [];
  const sources = new Map<string, string>();
  const entries: { name: string; abs?: string; data?: Uint8Array; hiddenDir?: boolean }[] = [];
  if (p.mode === "folder") {
    for (const f of folderFiles(p.dir)) entries.push(f);
  } else {
    const cfg = p.config;
    if (cfg.main) {
      const mainAbs = path.resolve(p.dir, cfg.main);
      if (!fs.existsSync(mainAbs)) throw new CliError(`${cfg.main}: not found (espos.json "main")`);
      const isTs = /\.(ts|tsx|mts|cts)$/.test(mainAbs);
      if (isTs && o.typecheck !== false) problems.push(...typecheck(p.dir, mainAbs));
      if (isTs || cfg.bundle) {
        entries.push({ name: "main.js", data: await bundle(mainAbs, { minify: o.minify ?? cfg.minify }) });
        sources.set("main.js", `${cfg.main} (bundled)`);
      } else entries.push({ name: "main.js", abs: mainAbs });
    }
    for (const f of cfg.files ?? []) {
      const a = path.resolve(p.dir, f);
      const rel = path.relative(p.dir, a).split(path.sep).join("/");
      if (rel.startsWith("..")) throw new CliError(`espos.json files: ${f} is outside the project`);
      if (!fs.existsSync(a)) throw new CliError(`espos.json files: ${f} not found`);
      if (fs.statSync(a).isDirectory()) for (const x of folderFiles(a)) entries.push({ name: `${rel}/${x.name}`, abs: x.abs, hiddenDir: x.hiddenDir });
      else if (path.basename(a) !== "manifest.json") entries.push({ name: rel, abs: a });
    }
    // the same order tools/espk.py would give the staged folder
    entries.sort((a, b) => cmpParts(a.name, b.name));
  }
  const files: PkgFile[] = [];
  const seen = new Set<string>();
  for (const e of entries) {
    if (seen.has(e.name)) throw new CliError(`${e.name} would be in the package twice (espos.json "main" and "files")`);
    seen.add(e.name);
    files.push({ name: e.name, data: e.data ?? fs.readFileSync(e.abs!) });
    if (!sources.has(e.name) && e.abs) sources.set(e.name, path.relative(p.dir, e.abs) || e.abs);
  }
  return { manifest: man.value, plain: man.plain, files, problems, sources };
}

function cmpParts(a: string, b: string): number {
  const A = a.split("/"),
    B = b.split("/");
  for (let i = 0; i < Math.min(A.length, B.length); i++) {
    const x = [...A[i]].map((c) => c.codePointAt(0)!),
      y = [...B[i]].map((c) => c.codePointAt(0)!);
    for (let k = 0; k < Math.min(x.length, y.length); k++) if (x[k] !== y[k]) return x[k] - y[k];
    if (x.length !== y.length) return x.length - y.length;
  }
  return A.length - B.length;
}

/** TS/JS modules bundled into one classic script (an IIFE) for QuickJS. */
export async function bundle(entry: string, o: { minify?: boolean } = {}): Promise<Uint8Array> {
  try {
    const r = await esbuild.build({
      entryPoints: [entry],
      bundle: true,
      write: false,
      format: "iife",
      platform: "neutral",
      mainFields: ["module", "main"],
      target: "es2022",
      charset: "utf8",
      legalComments: "none",
      minify: !!o.minify,
      logLevel: "silent",
    });
    return r.outputFiles[0].contents;
  } catch (e) {
    const errs = (e as { errors?: esbuild.Message[] }).errors ?? [];
    const msg = errs.map((x) => `${x.location ? `${x.location.file}:${x.location.line}:${x.location.column + 1}: ` : ""}${x.text}`).join("\n  ");
    throw new CliError(`bundling ${path.basename(entry)} failed:\n  ${msg || String(e)}`);
  }
}

/** Type errors of a TypeScript project against the SDK's typings (API level 1). */
export function typecheck(dir: string, entry: string): Problem[] {
  const dts = path.join(sdkDir(), "espos.d.ts");
  let options: ts.CompilerOptions = {
    strict: true,
    target: ts.ScriptTarget.ES2022,
    module: ts.ModuleKind.ESNext,
    moduleResolution: ts.ModuleResolutionKind.Bundler,
    lib: ["lib.es2023.d.ts"],
    types: [],
    skipLibCheck: true,
  };
  const tsconfig = path.join(dir, "tsconfig.json");
  if (fs.existsSync(tsconfig)) {
    const cfg = ts.readConfigFile(tsconfig, ts.sys.readFile);
    if (cfg.error) return [{ level: "error", code: "TS", where: "tsconfig.json", msg: ts.flattenDiagnosticMessageText(cfg.error.messageText, "\n") }];
    options = { ...ts.parseJsonConfigFileContent(cfg.config, ts.sys, dir).options };
  }
  options.noEmit = true;
  // the CLI's typings are the truth; the project's copy (types/espos.d.ts) is for the editor
  const roots = [entry, dts];
  const prog = ts.createProgram(roots, { ...options, types: [] });
  const seenDts = prog.getSourceFiles().filter((f) => /[\\/]espos\.d\.ts$/.test(f.fileName) && path.resolve(f.fileName) !== path.resolve(dts));
  const diags = ts.getPreEmitDiagnostics(prog).filter((d) => !d.file || !seenDts.includes(d.file));
  return diags.map((d) => {
    let where = "typescript";
    if (d.file && d.start !== undefined) {
      const { line, character } = d.file.getLineAndCharacterOfPosition(d.start);
      where = `${path.relative(dir, d.file.fileName)}:${line + 1}:${character + 1}`;
    }
    return { level: d.category === ts.DiagnosticCategory.Error ? "error" : "warning", code: `TS${d.code}`, where, msg: ts.flattenDiagnosticMessageText(d.messageText, "\n") } as Problem;
  });
}

/** <id>-<version>-<part>.espk (the store's names: scripts/publish_pkg.py, apps/pack.py). */
export function pkgFileName(m: Record<string, any>): string {
  const safe = (s: unknown) => String(s ?? "x").replace(/[^A-Za-z0-9._+-]/g, "_");
  return `${safe(m.id)}-${safe(m.version)}-${safe(m.part ?? "core")}.espk`;
}

/** Builds the package in memory and reads it back (so every check sees exactly the bytes that ship). */
export async function buildProject(p: Project, o: StageOpts = {}): Promise<{ staged: Staged; bytes: Buffer; pkg: ReadPkg }> {
  const staged = await stage(p, o);
  let bytes: Buffer;
  try {
    bytes = buildEspk(staged.manifest, staged.files);
  } catch (e) {
    throw new CliError((e as Error).message);
  }
  const pkg = readEspk(bytes, pkgFileName(staged.plain));
  return { staged, bytes, pkg };
}
