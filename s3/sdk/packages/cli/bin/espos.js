#!/usr/bin/env node
// The espos command (packages/cli/src/main.ts, built into dist/ by `npm install` / `npm run build` in sdk/).
import { existsSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const dist = join(dirname(fileURLToPath(import.meta.url)), "../dist/main.js");
if (!existsSync(dist)) {
  process.stderr.write("espos: not built yet; run `npm install` (or `npm run build`) in the sdk folder\n");
  process.exit(3);
}
const { cli } = await import(dist);
await cli(process.argv.slice(2));
