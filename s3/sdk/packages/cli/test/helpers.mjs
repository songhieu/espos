// Shared test helpers: run the CLI, temp folders, the repo.
import { spawnSync } from "node:child_process";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

export const here = path.dirname(fileURLToPath(import.meta.url));
export const BIN = path.resolve(here, "../bin/espos.js");
export const ROOT = path.resolve(here, "../../../.."); // s3desk
export const hasRoot = fs.existsSync(path.join(ROOT, "platformio.ini")) && fs.existsSync(path.join(ROOT, "src"));

const made = [];
process.on("exit", () => {
  if (!process.env.ESPOS_TEST_KEEP) for (const d of made) fs.rmSync(d, { recursive: true, force: true });
});

/** A new temp folder, removed when the test file ends (ESPOS_TEST_KEEP=1 keeps them). */
export function tmp(name = "t") {
  const d = fs.mkdtempSync(path.join(os.tmpdir(), `espos-${name}-`));
  made.push(d);
  return d;
}

/** Runs espos; returns {code, stdout, stderr, json?}. */
export function espos(args, o = {}) {
  const r = spawnSync(process.execPath, [BIN, ...args], {
    cwd: o.cwd ?? process.cwd(),
    encoding: "utf8",
    env: { ...process.env, NO_COLOR: "1", ESPOS_CONFIG_DIR: o.configDir ?? path.join(os.tmpdir(), "espos-test-config"), ...(o.env ?? {}) },
    timeout: o.timeout ?? 180000,
    input: o.input,
  });
  let json;
  if (args.includes("--json")) {
    try {
      json = JSON.parse(r.stdout);
    } catch {
      /* not JSON */
    }
  }
  return { code: r.status, stdout: r.stdout, stderr: r.stderr, json };
}

export function which(cmd) {
  const r = spawnSync("sh", ["-c", `command -v ${cmd}`], { encoding: "utf8" });
  return r.status === 0 ? r.stdout.trim() : null;
}

export const python = which("python3");

/** A package folder from a manifest object and files {name: string|Buffer}. */
export function writePkg(dir, manifest, files = {}) {
  fs.mkdirSync(dir, { recursive: true });
  fs.writeFileSync(path.join(dir, "manifest.json"), typeof manifest === "string" ? manifest : JSON.stringify(manifest));
  for (const [n, d] of Object.entries(files)) {
    const p = path.join(dir, n);
    fs.mkdirSync(path.dirname(p), { recursive: true });
    fs.writeFileSync(p, d);
  }
  return dir;
}

export const goodJs = (extra = {}) => ({
  id: "vn.test.app", name: "Thử", version: "1.0.0", code: 10000, kind: "js", engine: "js", min_fw: 300, category: "tool",
  author: "Test", desc: "Ứng dụng thử", icon: { bg: ["#60a5fa", "#2563eb"], glyph: "timer" }, perms: ["sound"], part: "core", ...extra,
});

export const goodMain = "app.title = 'x';\napp.render = () => ui.column([ui.text('a'), ui.button('b', () => sound.click())]);\n";
