// The .espk package file (s3/docs/ecosystem.md §2), byte-identical to tools/espk.py.
//
//   0   "ESPK"   4 u16 format = 1   6 u16 flags = 0   8 u32 manifest_len   12 u32 nfiles
//   16  manifest JSON (UTF-8, Python's compact json.dumps), zero padding to a multiple of 16
//       file table: nfiles x 64 bytes { char name[52] (zero padded); u32 off; u32 size; u32 crc32 }
//       file data, each file at a 16-byte aligned offset, in table order
import crypto from "node:crypto";
import fs from "node:fs";
import path from "node:path";
import zlib from "node:zlib";
import { dumps, floatPaths, parse, PyInt, PyObj, toPlain, type PyVal } from "./pyjson.js";

export const pad16 = (n: number) => (n + 15) & ~15;

const CRC_TABLE = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    t[n] = c >>> 0;
  }
  return t;
})();
export function crc32(b: Uint8Array): number {
  const z = (zlib as unknown as { crc32?: (d: Uint8Array) => number }).crc32;
  if (z) return z(b) >>> 0;
  let c = 0xffffffff;
  for (let i = 0; i < b.length; i++) c = CRC_TABLE[(c ^ b[i]) & 255] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}

export const sha256 = (b: Uint8Array) => crypto.createHash("sha256").update(b).digest("hex");

/** Code point order (Python compares str by code points; JS's < by UTF-16 units). */
function cmpCodePoints(a: string, b: string): number {
  const A = [...a],
    B = [...b];
  for (let i = 0; i < Math.min(A.length, B.length); i++) {
    const x = A[i].codePointAt(0)!,
      y = B[i].codePointAt(0)!;
    if (x !== y) return x - y;
  }
  return A.length - B.length;
}

export interface FolderFile {
  name: string; // relative, "/" separated
  abs: string;
  /** always false since tools/espk.py skips hidden folders (kept for callers) */
  hiddenDir: boolean;
}

/**
 * The files tools/espk.py packs from a folder: sorted(p for p in folder.rglob("*") if p.is_file() and
 * p.name != "manifest.json" and not any(part.startswith(".") for part in p.relative_to(folder).parts)): no dotfiles,
 * nothing inside a hidden folder; sorted as Python sorts paths (part by part).
 */
export function folderFiles(folder: string): FolderFile[] {
  const out: { parts: string[]; abs: string; hiddenDir: boolean }[] = [];
  const seen = new Set<string>();
  const walk = (dir: string, parts: string[], hidden: boolean) => {
    let real: string;
    try {
      real = fs.realpathSync(dir);
    } catch {
      return;
    }
    if (seen.has(real)) return;
    seen.add(real);
    for (const name of fs.readdirSync(dir)) {
      const p = path.join(dir, name);
      let st: fs.Stats;
      try {
        st = fs.statSync(p); // follows links, as is_file()
      } catch {
        continue;
      }
      if (name.startsWith(".")) continue; // dotfiles and hidden folders (.git/, .espos/) stay out
      if (st.isDirectory()) walk(p, [...parts, name], hidden);
      else if (st.isFile() && name !== "manifest.json") out.push({ parts: [...parts, name], abs: p, hiddenDir: hidden });
    }
  };
  walk(folder, [], false);
  out.sort((a, b) => {
    for (let i = 0; i < Math.min(a.parts.length, b.parts.length); i++) {
      const c = cmpCodePoints(a.parts[i], b.parts[i]);
      if (c) return c;
    }
    return a.parts.length - b.parts.length;
  });
  return out.map((f) => ({ name: f.parts.join("/"), abs: f.abs, hiddenDir: f.hiddenDir }));
}

export interface PkgFile {
  name: string;
  data: Uint8Array;
}

/** The package bytes: manifest as Python's compact json.dumps, files in the order given. */
export function buildEspk(manifest: PyVal, files: PkgFile[]): Buffer {
  const man = Buffer.from(dumps(manifest, { compact: true }), "utf8");
  for (const f of files) if (Buffer.byteLength(f.name) > 51) throw new Error(`${f.name}: file names (with folders) are at most 51 bytes`);
  let off = 16 + pad16(man.length) + 64 * files.length;
  const offs = files.map((f) => {
    off = pad16(off);
    const o = off;
    off += f.data.length;
    return o;
  });
  const out = Buffer.alloc(off);
  out.write("ESPK", 0, "latin1");
  out.writeUInt16LE(1, 4);
  out.writeUInt16LE(0, 6);
  out.writeUInt32LE(man.length, 8);
  out.writeUInt32LE(files.length, 12);
  man.copy(out, 16);
  let t = 16 + pad16(man.length);
  files.forEach((f, i) => {
    out.write(f.name, t, "utf8");
    out.writeUInt32LE(offs[i], t + 52);
    out.writeUInt32LE(f.data.length, t + 56);
    out.writeUInt32LE(crc32(f.data), t + 60);
    out.set(f.data, offs[i]);
    t += 64;
  });
  return out;
}

export interface Problem {
  level: "error" | "warning" | "info";
  code: string;
  where: string;
  msg: string;
}

export interface ReadFile {
  name: string;
  off: number;
  size: number;
  crc: number;
  data: Buffer;
}

export interface ReadPkg {
  path: string;
  name: string; // file name
  bytes: Buffer;
  sha256: string;
  manifestText: string;
  manifest: PyVal; // as parsed (key order, int/float kept)
  plain: Record<string, any>; // the same as plain JS
  floats: Set<string>;
  format: number;
  flags: number;
  files: ReadFile[];
  problems: Problem[]; // structure problems (the file is usable only when none is an error)
}

/** Reads and checks the structure of a .espk: everything tools/espk.py, publish_pkg.py, the web admin and the device check. */
export function readEspk(bytes: Buffer, file = "package.espk"): ReadPkg {
  const name = path.basename(file);
  const problems: Problem[] = [];
  const P = (code: string, msg: string, level: Problem["level"] = "error") => problems.push({ level, code, where: name, msg });
  const r: ReadPkg = {
    path: file, name, bytes, sha256: sha256(bytes), manifestText: "", manifest: new PyObj(), plain: {}, floats: new Set(),
    format: 0, flags: 0, files: [], problems,
  };
  if (bytes.length < 16 || bytes.toString("latin1", 0, 4) !== "ESPK") {
    P("P001", "not an .espk file (no ESPK header)");
    return r;
  }
  r.format = bytes.readUInt16LE(4);
  r.flags = bytes.readUInt16LE(6);
  const mlen = bytes.readUInt32LE(8),
    nf = bytes.readUInt32LE(12);
  if (r.format !== 1) {
    P("P002", `format ${r.format} (format 1 is the only one devices read)`);
    return r;
  }
  if (r.flags !== 0) P("P003", `flags ${r.flags}, expected 0 (publish_pkg.py refuses others)`);
  if (mlen === 0 || mlen > 32768) {
    P("P004", `manifest length ${mlen} (1 to 32768 bytes; the device refuses longer)`);
    return r;
  }
  if (nf > 512) {
    P("P005", `${nf} files (at most 512; the device refuses more)`);
    return r;
  }
  if (16 + mlen > bytes.length) {
    P("P006", "the manifest runs past the end of the file");
    return r;
  }
  const mb = bytes.subarray(16, 16 + mlen);
  const dec = new TextDecoder("utf-8", { fatal: true });
  try {
    r.manifestText = dec.decode(mb);
  } catch {
    P("P007", "the manifest is not UTF-8");
    return r;
  }
  try {
    r.manifest = parse(r.manifestText);
  } catch (e) {
    P("P008", `the manifest is not JSON: ${(e as Error).message}`);
    return r;
  }
  if (!(r.manifest instanceof PyObj)) {
    P("P009", "the manifest is not a JSON object");
    return r;
  }
  r.plain = toPlain(r.manifest) as Record<string, any>;
  r.floats = floatPaths(r.manifest);
  const table = 16 + pad16(mlen);
  if (bytes.subarray(16 + mlen, Math.min(table, bytes.length)).some((x) => x !== 0)) P("P010", "the manifest's padding is not zero");
  const endTable = table + 64 * nf;
  if (endTable > bytes.length) {
    P("P011", "the file table runs past the end of the file");
    return r;
  }
  let last = endTable;
  const seen = new Set<string>();
  for (let k = 0; k < nf; k++) {
    const e = bytes.subarray(table + 64 * k, table + 64 * (k + 1));
    const raw = e.subarray(0, 52);
    const z = raw.indexOf(0);
    if (z < 0) {
      P("P012", `file ${k}: the name has no terminating zero (names are at most 51 bytes)`);
      continue;
    }
    if (raw.subarray(z).some((x) => x !== 0)) P("P013", `file ${k}: the name's padding is not zero`);
    let fname: string;
    try {
      fname = dec.decode(raw.subarray(0, z));
    } catch {
      P("P014", `file ${k}: the name is not UTF-8`);
      continue;
    }
    const off = e.readUInt32LE(52),
      size = e.readUInt32LE(56),
      crc = e.readUInt32LE(60);
    const bad = nameProblem(fname);
    if (bad) P("P015", `${JSON.stringify(fname)}: ${bad}`);
    if (seen.has(fname)) P("P016", `${fname} is in the table twice`);
    seen.add(fname);
    if (off % 16) P("P017", `${fname} starts at ${off}, not 16-byte aligned`);
    if (off < last) P("P018", `${fname} overlaps the table or the file before it`);
    if (off + size > bytes.length) {
      P("P019", `${fname} runs past the end of the file`);
      continue;
    }
    if (size > 64 << 20) P("P020", `${fname} is ${size} bytes (the device takes at most 64 MB per file)`);
    const data = bytes.subarray(off, off + size);
    if (crc32(data) !== crc) P("P021", `${fname}: CRC-32 does not match`);
    last = Math.max(last, off + size);
    r.files.push({ name: fname, off, size, crc, data });
  }
  if (last < bytes.length) P("P022", `${bytes.length - last} bytes after the last file (ignored by the device)`, "warning");
  return r;
}

/** Why a name in the file table is refused (the strictest of espk.py, publish_pkg.py and the device's safeName). */
export function nameProblem(n: string): string | null {
  const len = Buffer.byteLength(n);
  if (!n) return "empty name";
  if (len > 51) return `${len} bytes; names (with folders) are at most 51 bytes`;
  if (n === "manifest.json") return "manifest.json cannot be a file of the package";
  if (n.startsWith("/")) return "starts with /";
  if (n.endsWith("/")) return "ends with /";
  if (n.includes("..")) return 'contains ".."';
  if (n.includes("//")) return "has an empty folder name (//)";
  // eslint-disable-next-line no-control-regex
  if (/[\x00-\x1f\\:*?"<>|]/.test(n)) return 'contains one of \\ : * ? " < > | or a control character';
  return null;
}

// ---------------------------------------------------------------- catalog entry

export interface EntryOpts {
  base: string;
  iconUrl?: string;
}

/** A catalog "packages" entry for the parts of one package (tools/espk.py --entry, with publish_pkg.py's part rules). */
export function catalogEntry(parts: ReadPkg[], o: EntryOpts): Record<string, unknown> {
  const core = parts.find((p) => (p.plain.part ?? "core") === "core");
  if (!core) throw new Error("the core part is needed for a catalog entry");
  const m = core.plain;
  const out: Record<string, unknown> = {};
  for (const k of ["id", "name", "version", "code", "kind", "engine", "min_fw", "category", "author", "desc", "icon", "perms", "addon_of",
    "type", "runtime", "api", "requires", "screens", "refresh", "i18n", "lang", "font"])
    if (k in m) out[k] = m[k];
  // A face, widget or theme (runtime "decl") carries no engine or firmware fields: the catalog keeps its v2 fields
  // as they are, like espk.py entry().
  const decl = ["face", "widget", "theme"].includes(String(m.type));
  if (!decl) {
    if (!("engine" in out)) out.engine = m.kind === "js" ? "js" : "";
    if (!("min_fw" in out)) out.min_fw = 0;
    if (!("perms" in out)) out.perms = [];
  }
  // what the web admin also puts into the catalog (publish_pkg.py leaves them out): the store shows them
  if (m.j2me) out.j2me = m.j2me;
  if (m.license) out.license = m.license;
  if (o.iconUrl) out.icon_url = o.iconUrl;
  // what each part is: its own manifest, then the core manifest's "parts" (which wins), as publish_pkg.py
  const meta = new Map<string, Record<string, unknown>>();
  for (const p of parts) {
    const pm = p.plain;
    const e: Record<string, unknown> = {};
    for (const k of ["group", "label", "default", "req"]) if (k in pm) e[k] = pm[k];
    meta.set(String(pm.part ?? "core"), e);
  }
  if (Array.isArray(m.parts)) for (const e of m.parts) if (e && typeof e === "object" && meta.has(e.id)) for (const k of ["group", "label", "default", "req"]) if (k in e) meta.get(e.id)![k] = e[k];
  const order = [core, ...parts.filter((p) => p !== core).sort((a, b) => cmpCodePoints(String(a.plain.part), String(b.plain.part)))];
  const groups = new Map<string, Record<string, unknown>[]>();
  const list = order.map((p) => {
    const id = String(p.plain.part ?? "core");
    const e = meta.get(id) ?? {};
    const c: Record<string, unknown> = { id };
    if (e.group && id !== "core") {
      c.group = e.group;
      c.label = e.label || id;
      if (e.default === true) c.default = true;
      if (!groups.has(String(e.group))) groups.set(String(e.group), []);
      groups.get(String(e.group))!.push(c);
    } else {
      c.req = true;
      if (e.label) c.label = e.label;
    }
    c.url = `${o.base.replace(/\/+$/, "")}/${p.name}`;
    c.size = p.bytes.length;
    c.sha256 = p.sha256;
    return c;
  });
  for (const cs of groups.values()) if (!cs.some((c) => c.default)) cs[0].default = true;
  out.parts = list;
  return out;
}

// ---------------------------------------------------------------- simulator install (tools/espk.py --install-sim)

/**
 * Unpacks the parts of each package into a simulator data folder: $SIM_DATA/sd/espos/pkgs/<id>/ (the card) or
 * $SIM_DATA/fs/pkgs/<id>/ (the device's flash, JavaScript apps only), with the installed manifest.json the device writes.
 */
export function installToFolder(simData: string, pkgs: ReadPkg[], vol: "sd" | "flash"): { id: string; dir: string; files: number }[] {
  const byId = new Map<string, ReadPkg[]>();
  for (const p of pkgs) {
    const id = String(p.plain.id);
    if (!byId.has(id)) byId.set(id, []);
    byId.get(id)!.push(p);
  }
  const res: { id: string; dir: string; files: number }[] = [];
  for (const [id, parts] of byId) {
    const core = parts.find((p) => (p.plain.part ?? "core") === "core");
    if (!core) throw new Error(`${id}: the core part is needed`);
    if (vol === "flash" && core.plain.kind !== "js") throw new Error(`${id}: only kind js goes to the device's flash (fs/pkgs); use the card`);
    res.push({ id, ...installTree(path.join(simData, vol === "sd" ? "sd/espos/pkgs" : "fs/pkgs", id), core, parts) });
  }
  return res;
}

/** Writes one package's folder (files + installed manifest.json) at dir, replacing what is there. */
export function installTree(dir: string, core: ReadPkg, parts: ReadPkg[]): { dir: string; files: number } {
  fs.rmSync(dir, { recursive: true, force: true });
  fs.mkdirSync(dir, { recursive: true });
  const all = new PyObjList();
  for (const p of parts)
    for (const f of p.files) {
      const dst = path.join(dir, f.name);
      fs.mkdirSync(path.dirname(dst), { recursive: true });
      fs.writeFileSync(dst, f.data); // plain writes: no extended attributes (no ._ files on a FAT card)
      all.add(f.name, f.size, f.crc);
    }
  fs.writeFileSync(path.join(dir, "manifest.json"), installedManifest(core, parts, all));
  return { dir, files: all.n };
}

class PyObjList {
  items: PyVal[] = [];
  n = 0;
  add(name: string, size: number, crc: number) {
    const o = new PyObj();
    o.set("n", name);
    o.set("s", new PyInt(String(size)));
    o.set("c", new PyInt(String(crc)));
    this.items.push(o);
    this.n++;
  }
}

/** json.dumps(man, ensure_ascii=False) of the core manifest minus "part", plus "parts" and "files" (espk.py). */
function installedManifest(core: ReadPkg, parts: ReadPkg[], files: PyObjList): string {
  const m = new PyObj();
  for (const [k, v] of (core.manifest as PyObj).entries()) m.set(k, v);
  m.delete("part");
  m.set("parts", parts.map((p) => String(p.plain.part ?? "core")));
  m.set("files", files.items);
  return dumps(m);
}
