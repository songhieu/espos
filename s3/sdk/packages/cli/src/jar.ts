// A J2ME game's .jar made into a package (port of web/app/src/admin/jar.ts + zip.ts and the admin page's
// JarDialog): META-INF/MANIFEST.MF (MIDlet-Name, -Vendor, -Version, MIDlet-1 = "name, icon, class", MIDlet-Icon,
// MicroEdition-Profile/-Configuration), the icon on a 120x120 tile as a baseline JPEG, id jar.<slug>.
import zlib from "node:zlib";
import { Canvas, decodePng, fitDown, rgbHex } from "./image.js";

export interface ZipEntry {
  name: string;
  method: number;
  csize: number;
  usize: number;
  off: number;
}

const u16 = (b: Uint8Array, o: number) => b[o] | (b[o + 1] << 8);
const u32 = (b: Uint8Array, o: number) => (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0;

export function zipEntries(b: Uint8Array): Map<string, ZipEntry> {
  let e = -1;
  for (let i = b.length - 22; i >= Math.max(0, b.length - 65557); i--)
    if (u32(b, i) === 0x06054b50) {
      e = i;
      break;
    }
  if (e < 0) throw new Error("not a .jar (zip) file");
  const n = u16(b, e + 10);
  let p = u32(b, e + 16);
  const out = new Map<string, ZipEntry>();
  const dec = new TextDecoder();
  for (let k = 0; k < n; k++) {
    if (p + 46 > b.length || u32(b, p) !== 0x02014b50) throw new Error("broken .jar (central directory)");
    const nl = u16(b, p + 28), xl = u16(b, p + 30), cl = u16(b, p + 32);
    const name = dec.decode(b.subarray(p + 46, p + 46 + nl));
    out.set(name, { name, method: u16(b, p + 10), csize: u32(b, p + 20), usize: u32(b, p + 24), off: u32(b, p + 42) });
    p += 46 + nl + xl + cl;
  }
  return out;
}

export function zipRead(b: Uint8Array, e: ZipEntry): Uint8Array {
  if (u32(b, e.off) !== 0x04034b50) throw new Error(`broken .jar (${e.name})`);
  const at = e.off + 30 + u16(b, e.off + 26) + u16(b, e.off + 28);
  const raw = b.subarray(at, at + e.csize);
  if (e.method === 0) return raw;
  if (e.method !== 8) throw new Error(`${e.name}: compression method ${e.method} is not supported`);
  return zlib.inflateRawSync(raw, { maxOutputLength: 64 << 20 });
}

/** MANIFEST.MF as a map (continuation lines joined). */
export function parseManifestMf(text: string): Record<string, string> {
  const out: Record<string, string> = {};
  let last = "";
  for (const line of text.replace(/\r/g, "").split("\n")) {
    if (line.startsWith(" ") && last) out[last] += line.slice(1);
    else {
      const i = line.indexOf(":");
      if (i > 0) out[(last = line.slice(0, i).trim())] = line.slice(i + 1).trim();
    }
  }
  return out;
}

export interface JarInfo {
  name: string;
  vendor: string;
  version: string;
  midlet: string;
  profile: string;
  config: string;
  size: number;
  icon: Buffer | null; // 120x120 baseline JPEG
  iconFrom: string; // the icon's path in the jar
  iconNote?: string;
  bg: [string, string];
  props: Record<string, string>;
}

export function readJar(b: Uint8Array, fileName: string): JarInfo {
  const zip = zipEntries(b);
  const mf = [...zip.values()].find((e) => e.name.toUpperCase() === "META-INF/MANIFEST.MF");
  if (!mf) throw new Error("not a J2ME game: no META-INF/MANIFEST.MF");
  const m = parseManifestMf(new TextDecoder().decode(zipRead(b, mf)));
  const first = (m["MIDlet-1"] ?? "").split(",").map((s) => s.trim());
  if (!first[2] && !m["MIDlet-Name"]) throw new Error("not a J2ME game: MANIFEST.MF has no MIDlet-1");
  let icon: Buffer | null = null;
  let bg: [string, string] = ["#5b6270", "#2b2f3a"];
  let iconNote: string | undefined;
  const iconPath = (m["MIDlet-Icon"] || first[1] || "").replace(/^\//, "");
  const ie = iconPath ? zip.get(iconPath) : undefined;
  if (ie) {
    try {
      ({ icon, bg } = storeIcon(zipRead(b, ie)));
    } catch (e) {
      icon = null; // an unreadable icon: the glyph tile instead
      iconNote = `${iconPath}: ${(e as Error).message}`;
    }
  } else if (iconPath) iconNote = `${iconPath} is not in the jar`;
  return {
    name: m["MIDlet-Name"] || first[0] || fileName.replace(/\.jar$/i, ""),
    vendor: m["MIDlet-Vendor"] || "",
    version: m["MIDlet-Version"] || "1.0",
    midlet: first[2] || "",
    profile: m["MicroEdition-Profile"] || "",
    config: m["MicroEdition-Configuration"] || "",
    size: b.length,
    icon,
    iconFrom: ie ? iconPath : "",
    iconNote,
    bg,
    props: m,
  };
}

/**
 * The game's small icon (often 16-48 px) on a 120x120 tile, pixels kept square, as a baseline JPEG; the tile is the
 * icon's average colour, darker below (as the web admin draws it). Icons over 96 px are scaled down to fit (the
 * web admin draws them at their size, cut at the tile's edges).
 */
export function storeIcon(png: Uint8Array): { icon: Buffer; bg: [string, string] } {
  let img = decodePng(png);
  let r = 0, g = 0, bl = 0, n = 0;
  for (let i = 0; i < img.data.length; i += 4)
    if (img.data[i + 3] > 128) (r += img.data[i]), (g += img.data[i + 1]), (bl += img.data[i + 2]), n++;
  const avg = n ? [r / n, g / n, bl / n] : [91, 98, 112];
  const bg: [string, string] = [rgbHex(avg.map((v) => v * 0.55)), rgbHex(avg.map((v) => v * 0.3))];
  const c = new Canvas(120, 120);
  const top = avg.map((v) => v * 0.55) as [number, number, number];
  const bottom = avg.map((v) => v * 0.3) as [number, number, number];
  c.gradient(top, bottom);
  if (Math.max(img.width, img.height) > 96) img = fitDown(img, 96);
  const s = Math.max(1, Math.floor(96 / Math.max(img.width, img.height)));
  c.drawScaled(img, Math.floor((120 - img.width * s) / 2), Math.floor((120 - img.height * s) / 2), s);
  return { icon: c.jpeg(92), bg };
}

/** "Rừng Pixel" -> "rung-pixel": a package id's last part. */
export function slug(s: string): string {
  return (
    s
      .normalize("NFD")
      .replace(/[̀-ͯ]/g, "")
      .replace(/đ/g, "d")
      .replace(/Đ/g, "d")
      .toLowerCase()
      .replace(/[^a-z0-9]+/g, "-")
      .replace(/^-+|-+$/g, "")
      .slice(0, 40) || "game"
  );
}

/** As the web admin: 1.2.3 -> 10203 (at least 1). */
export function versionCode(v: string): number {
  const p = v.split(/[.-]/).map((x) => parseInt(x, 10) || 0);
  return Math.max(1, (p[0] ?? 0) * 10000 + (p[1] ?? 0) * 100 + (p[2] ?? 0));
}

/** As the web admin: lowercase, only 0-9 a-z . + -, at most 20 characters. */
export function cleanVersion(v: string): string {
  return v.toLowerCase().replace(/[^0-9a-z.+-]/g, "").slice(0, 20) || "1.0";
}

export interface JarPkgOpts {
  id?: string;
  name?: string;
  desc?: string;
  category?: string;
  keypad?: string;
  license: string;
  version?: string;
  code?: number;
  author?: string;
}

/** The package manifest for a jar (the web admin's, plus "part" and "license"). Plain object in field order. */
export function jarManifest(info: JarInfo, o: JarPkgOpts): Record<string, unknown> {
  const ver = cleanVersion(o.version ?? info.version);
  const name = (o.name ?? info.name).trim();
  return {
    id: o.id ?? "jar." + slug(name),
    name,
    version: ver,
    code: o.code ?? versionCode(ver),
    kind: "native",
    engine: "j2me",
    min_fw: 400,
    category: o.category ?? "game",
    author: o.author ?? info.vendor,
    desc: (o.desc ?? "").trim(),
    icon: { bg: info.bg, glyph: "play" },
    perms: ["sound"],
    j2me: { midlet: info.midlet, keypad: o.keypad ?? "full", profile: info.profile, config: info.config },
    license: o.license.trim(),
    part: "core",
  };
}
