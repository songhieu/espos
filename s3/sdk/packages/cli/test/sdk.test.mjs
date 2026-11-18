// @espos/sdk: the typings follow the runtime's prelude (src/js/jsapp.cpp), the schemas follow the validator.
import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { test } from "node:test";
import ts from "typescript";
import { CATEGORIES, ENGINES, GLYPHS, KEYPADS, PERMS } from "../dist/validate.js";
import { hasRoot, here, ROOT } from "./helpers.mjs";

const SDK = path.resolve(here, "../../sdk");
const DTS = path.join(SDK, "espos.d.ts");

test("the typings declare exactly the prelude's globals and ui.* functions", { skip: !hasRoot && "no repo" }, () => {
  const cpp = fs.readFileSync(path.join(ROOT, "src/js/jsapp.cpp"), "utf8");
  const prelude = cpp.slice(cpp.indexOf('const char* PRELUDE = R"JS('), cpp.indexOf(')JS";'));
  const globals = [...prelude.matchAll(/^var (\w+) =/gm)].map((m) => m[1]).filter((n) => !n.startsWith("__"));
  const dts = fs.readFileSync(DTS, "utf8");
  const declared = [...dts.matchAll(/^declare (?:const|function) (\w+)/gm)].map((m) => m[1]);
  assert.deepEqual(declared.sort(), globals.sort());
  const uiFns = [...prelude.slice(prelude.indexOf("var ui = {"), prelude.indexOf("};")).matchAll(/^\s+(\w+): /gm)].map((m) => m[1]);
  const uiDecl = [...dts.slice(dts.indexOf("interface EspOSUi {"), dts.indexOf("interface EspOSApp")).matchAll(/^\s+(\w+)\(/gm)].map((m) => m[1]);
  assert.deepEqual(uiDecl.sort(), uiFns.sort());
  for (const k of ["badge", "refresh", "title", "render", "arg"]) assert.match(dts.slice(dts.indexOf("interface EspOSApp")), new RegExp(`\\b${k}\\b`));
  for (const k of ["every", "after", "cancel"]) assert.match(prelude, new RegExp(`${k}: `));
  for (const k of ["chime", "beep", "click"]) assert.match(dts, new RegExp(`${k}\\(\\): void`));
});

test("the JavaScript template type-checks against the typings (checkJs)", () => {
  const main = path.resolve(here, "../templates/js-app/main.js");
  const prog = ts.createProgram([main, DTS], { allowJs: true, checkJs: true, noEmit: true, strict: false, lib: ["lib.es2023.d.ts"], types: [], target: ts.ScriptTarget.ES2022 });
  const d = ts.getPreEmitDiagnostics(prog).map((x) => ts.flattenDiagnosticMessageText(x.messageText, "\n"));
  assert.deepEqual(d, []);
});

test("no console, setTimeout or fetch in the typings (not in API level 1)", () => {
  const prog = ts.createProgram([DTS], { noEmit: true, lib: ["lib.es2023.d.ts"], types: [] });
  const checker = prog.getTypeChecker();
  const sf = prog.getSourceFile(DTS);
  const names = checker.getSymbolsInScope(sf, ts.SymbolFlags.Value).map((s) => s.name);
  for (const n of ["console", "setTimeout", "setInterval", "fetch", "require"]) assert.ok(!names.includes(n), n);
  for (const n of ["ui", "app", "timer", "sound", "notify", "toast", "store"]) assert.ok(names.includes(n), n);
});

test("manifest schema enums match the validator", () => {
  const s = JSON.parse(fs.readFileSync(path.join(SDK, "schemas/manifest.schema.json"), "utf8"));
  assert.deepEqual(s.properties.category.enum, CATEGORIES);
  assert.deepEqual(s.properties.engine.enum, ENGINES);
  assert.deepEqual(s.properties.icon.properties.glyph.enum, GLYPHS);
  assert.deepEqual(s.properties.perms.items.enum, PERMS);
  assert.deepEqual(s.properties.j2me.properties.keypad.enum, KEYPADS);
  for (const f of ["manifest-v2.schema.json", "project.schema.json"]) assert.ok(JSON.parse(fs.readFileSync(path.join(SDK, "schemas", f), "utf8")).title);
  const v2 = JSON.parse(fs.readFileSync(path.join(SDK, "schemas/manifest-v2.schema.json"), "utf8"));
  assert.match(v2.title, /DRAFT/);
});
