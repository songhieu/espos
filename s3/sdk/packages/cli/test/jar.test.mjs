// J2ME jars wrapped into game packages (the web admin's logic).
import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { test } from "node:test";
import zlib from "node:zlib";
import { jpegInfo } from "../dist/image.js";
import { readEspk } from "../dist/espk.js";
import { cleanVersion, parseManifestMf, readJar, slug, versionCode } from "../dist/jar.js";
import { checkPackage } from "../dist/validate.js";
import { espos, tmp } from "./helpers.mjs";

// The MegaBlock jar (Apache-2.0) downloaded for the J2ME work; read only. ESPOS_TEST_JAR points elsewhere.
const JAR = process.env.ESPOS_TEST_JAR ?? "MegaBlock-1.0.0.jar";
const hasJar = fs.existsSync(JAR);

/** A tiny jar (stored zip) with a manifest and no icon. */
function miniJar(mf) {
  const entries = [["META-INF/MANIFEST.MF", Buffer.from(mf)], ["a/B.class", Buffer.from([0xca, 0xfe, 0xba, 0xbe])]];
  const parts = [], central = [];
  let off = 0;
  for (const [name, data] of entries) {
    const n = Buffer.from(name);
    const crc = zlib.crc32 ? zlib.crc32(data) : 0;
    const local = Buffer.alloc(30);
    local.writeUInt32LE(0x04034b50, 0); local.writeUInt16LE(10, 4); local.writeUInt16LE(0, 8);
    local.writeUInt32LE(crc, 14); local.writeUInt32LE(data.length, 18); local.writeUInt32LE(data.length, 22); local.writeUInt16LE(n.length, 26);
    const c = Buffer.alloc(46);
    c.writeUInt32LE(0x02014b50, 0); c.writeUInt16LE(10, 6); c.writeUInt16LE(0, 10); c.writeUInt32LE(crc, 16);
    c.writeUInt32LE(data.length, 20); c.writeUInt32LE(data.length, 24); c.writeUInt16LE(n.length, 28); c.writeUInt32LE(off, 42);
    parts.push(local, n, data);
    central.push(c, n);
    off += 30 + n.length + data.length;
  }
  const cd = Buffer.concat(central);
  const end = Buffer.alloc(22);
  end.writeUInt32LE(0x06054b50, 0); end.writeUInt16LE(entries.length, 8); end.writeUInt16LE(entries.length, 10);
  end.writeUInt32LE(cd.length, 12); end.writeUInt32LE(off, 16);
  return Buffer.concat([...parts, cd, end]);
}

test("slug, version code and clean version as the web admin", () => {
  assert.equal(slug("Rừng Pixel"), "rung-pixel");
  assert.equal(slug("Đua Xe 3D!"), "dua-xe-3d");
  assert.equal(slug("!!!"), "game");
  assert.equal(versionCode("1.2.3"), 10203);
  assert.equal(versionCode("2.0"), 20000);
  assert.equal(versionCode("x"), 1);
  assert.equal(cleanVersion("1.0 Beta"), "1.0beta");
  assert.equal(cleanVersion(""), "1.0");
});

test("MANIFEST.MF with continuation lines", () => {
  const m = parseManifestMf("MIDlet-Name: Long\r\n Name\r\nMIDlet-1: Game, /i.png, a.b.C\r\n");
  assert.equal(m["MIDlet-Name"], "LongName");
  assert.equal(m["MIDlet-1"], "Game, /i.png, a.b.C");
});

test("a jar without an icon: the colour tile, MIDlet from MIDlet-1", () => {
  const info = readJar(miniJar("MIDlet-Name: Mini\nMIDlet-Vendor: Me\nMIDlet-Version: 1.2\nMIDlet-1: Mini, , a.B\n"), "mini.jar");
  assert.equal(info.name, "Mini");
  assert.equal(info.midlet, "a.B");
  assert.equal(info.icon, null);
  assert.throws(() => readJar(miniJar("Manifest-Version: 1.0\n"), "x.jar"), /MIDlet-1/);
  assert.throws(() => readJar(Buffer.from("not a zip at all"), "x.jar"), /zip/);
});

test("espos jar requires --license", () => {
  const t = tmp("jarlic");
  const f = path.join(t, "mini.jar");
  fs.writeFileSync(f, miniJar("MIDlet-Name: Mini\nMIDlet-1: Mini, , a.B\n"));
  const r = espos(["jar", f]);
  assert.equal(r.code, 2);
  assert.match(r.stderr, /--license/);
});

test("MegaBlock: a checked game package with a 120x120 baseline icon", { skip: !hasJar && `no ${JAR}` }, () => {
  const t = tmp("jar");
  const r = espos(["jar", JAR, "--license", "Apache-2.0, xylophones.net", "--desc", "Xếp gạch", "--out-dir", t, "--json"]);
  assert.equal(r.code, 0, r.stdout + r.stderr);
  const out = r.json.out;
  assert.equal(path.basename(out), "jar.mega-block-1.0.0-core.espk");
  const p = readEspk(fs.readFileSync(out), out);
  assert.equal(p.plain.engine, "j2me");
  assert.equal(p.plain.kind, "native");
  assert.equal(p.plain.min_fw, 400);
  assert.equal(p.plain.part, "core");
  assert.equal(p.plain.license, "Apache-2.0, xylophones.net");
  assert.equal(p.plain.j2me.midlet, "net.xylophones.micro.game.mb.Midlet");
  assert.deepEqual(p.files.map((f) => f.name), ["game.jar", "icon.jpg"]);
  assert.deepEqual(p.files[0].data, fs.readFileSync(JAR));
  const j = jpegInfo(p.files[1].data);
  assert.deepEqual([j.width, j.height, j.baseline], [120, 120, true]);
  assert.deepEqual(checkPackage([p]).filter((x) => x.level !== "info"), []);
  // as a project folder, which then builds to the same bytes
  const proj = path.join(t, "proj");
  const r2 = espos(["jar", JAR, "--license", "Apache-2.0, xylophones.net", "--desc", "Xếp gạch", "--project", proj]);
  assert.equal(r2.code, 0, r2.stdout + r2.stderr);
  const r3 = espos(["build", proj, "--out", path.join(t, "again.espk"), "-q"]);
  assert.equal(r3.code, 0, r3.stdout + r3.stderr);
  const again = readEspk(fs.readFileSync(path.join(t, "again.espk")));
  assert.deepEqual(again.files.map((f) => [f.name, f.crc]), p.files.map((f) => [f.name, f.crc]));
});
