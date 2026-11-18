// Helpers shared by the commands: turning paths into packages, printing problems.
import fs from "node:fs";
import path from "node:path";
import { readEspk, type Problem, type ReadPkg } from "./espk.js";
import { CliError, dim, EXIT, red, say, state, yellow } from "./out.js";
import { buildProject, loadProject, pkgFileName, type Project, type Staged } from "./project.js";
import { checkPackage, countLevels } from "./validate.js";

export function fmtProblem(p: Problem): string {
  const lv = p.level === "error" ? red("error  ") : p.level === "warning" ? yellow("warning") : dim("info   ");
  return `${lv} ${p.code.padEnd(6)} ${p.where}: ${p.msg}`;
}

/** Errors and warnings always print (also with --quiet); notes only without it. */
export function printProblems(ps: Problem[], showInfo = true): void {
  for (const p of ps) {
    if (p.level === "info") {
      if (showInfo) say("  " + fmtProblem(p));
    } else (state.json ? process.stderr : process.stdout).write("  " + fmtProblem(p) + "\n");
  }
}

export function summary(ps: Problem[]): string {
  const c = countLevels(ps);
  return `${c.errors} error${c.errors === 1 ? "" : "s"}, ${c.warnings} warning${c.warnings === 1 ? "" : "s"}${c.infos ? `, ${c.infos} note${c.infos === 1 ? "" : "s"}` : ""}`;
}

export const hasErrors = (ps: Problem[]) => ps.some((p) => p.level === "error");

export function readEspkFile(f: string): ReadPkg {
  if (!fs.existsSync(f)) throw new CliError(`${f}: not found`, EXIT.USAGE);
  return readEspk(fs.readFileSync(f), f);
}

export interface Built {
  project: Project;
  staged: Staged;
  bytes: Buffer;
  pkg: ReadPkg;
  problems: Problem[];
}

/** Builds a project or package folder in memory and checks it. */
export async function buildChecked(dir: string, o: { typecheck?: boolean; minify?: boolean } = {}): Promise<Built> {
  const project = loadProject(dir);
  const { staged, bytes, pkg } = await buildProject(project, o);
  // a folder's problems name its files, not the package that is not written yet
  const problems = [...staged.problems, ...checkPackage([pkg]).map((p) => ({ ...p, where: p.where === pkg.name ? "manifest.json" : p.where.replace(`${pkg.name}: `, "") }))];
  if (project.mode === "folder") {
    for (const f of staged.files)
      if (/\.espk$/.test(f.name) || f.name.startsWith("dist/") || f.name.startsWith("node_modules/"))
        problems.push({ level: "warning", code: "F008", where: f.name, msg: "packed into the package (a folder without espos.json packs every file, as tools/espk.py does); add an espos.json to choose the files" });
  }
  return { project, staged, bytes, pkg, problems };
}

/** Where `build` writes a project's package. */
export function outputPath(b: Built, o: { out?: string; outDir?: string }): string {
  if (o.out) return path.resolve(o.out);
  const name = pkgFileName(b.staged.plain);
  if (o.outDir) return path.resolve(o.outDir, name);
  if (b.project.mode === "project") return path.resolve(b.project.dir, b.project.config.out ?? "dist", name);
  // a plain folder packs every file in it: the package goes next to the folder, never inside
  return path.resolve(path.dirname(b.project.dir), name);
}

/** Packages from paths: .espk files as they are, folders built (and written to their output place). */
export async function packagesFrom(targets: string[], o: { write?: boolean; typecheck?: boolean } = {}): Promise<{ pkgs: ReadPkg[]; problems: Problem[]; built: { dir: string; out?: string; pkg: ReadPkg }[] }> {
  const pkgs: ReadPkg[] = [];
  const problems: Problem[] = [];
  const built: { dir: string; out?: string; pkg: ReadPkg }[] = [];
  const files: ReadPkg[] = [];
  for (const t of targets) {
    if (fs.existsSync(t) && fs.statSync(t).isDirectory()) {
      const b = await buildChecked(t, { typecheck: o.typecheck });
      problems.push(...b.problems);
      let out: string | undefined;
      if (o.write && !hasErrors(b.problems)) {
        out = outputPath(b, {});
        fs.mkdirSync(path.dirname(out), { recursive: true });
        fs.writeFileSync(out, b.bytes);
        b.pkg.path = out;
      }
      pkgs.push(b.pkg);
      built.push({ dir: b.project.dir, out, pkg: b.pkg });
    } else {
      const p = readEspkFile(t);
      files.push(p);
      pkgs.push(p);
    }
  }
  for (const group of groupById(files)) problems.push(...checkPackage(group));
  return { pkgs, problems, built };
}

export function groupById(ps: ReadPkg[]): ReadPkg[][] {
  const m = new Map<string, ReadPkg[]>();
  for (const p of ps) {
    // the parts of one release: the same id and version (two versions of a package are two packages)
    const id = `${String(p.plain.id ?? p.name)}@${String(p.plain.version ?? "")}`;
    if (!m.has(id)) m.set(id, []);
    m.get(id)!.push(p);
  }
  return [...m.values()];
}

/** A path for messages: relative to the current folder when it is inside it, else absolute. */
export function show(p: string): string {
  const r = path.relative(process.cwd(), p);
  return !r ? "." : r.startsWith("..") || path.isAbsolute(r) ? p : r;
}

export const kb = (n: number) => (n >= 1048576 ? `${(n / 1048576).toFixed(1)} MB` : n >= 1024 ? `${(n / 1024).toFixed(1)} KB` : `${n} B`);
