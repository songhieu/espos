// Product presets (docs/app-slots.md §17, src/preset.ts): `espos preset check` (the open format's validator) and
// `espos preset compile` (the device's /sys/preset.json for one board, which tools/make_bundle.py --preset puts in a
// delivery bundle). Presets of your own products can stay private: the format and these tools are open.
import fs from "node:fs";
import path from "node:path";
import type { CmdSpec, Ctx } from "../args.js";
import { hasErrors, printProblems, summary } from "../common.js";
import type { Problem } from "../espk.js";
import { checkDef } from "../jsonschema.js";
import { bold, CliError, emitJson, EXIT, green, red, say, state } from "../out.js";
import { checkPreset, compilePreset, PRESET_RULES, presetSchema, readInputs } from "../preset.js";
import { countLevels } from "../validate.js";

const INPUT_OPTS = [
  { name: "catalog", type: "string" as const, arg: "FILE", desc: "the store's catalog.json: packages and parts are checked against it, and compile picks each board's parts" },
  { name: "saver-templates", type: "string" as const, arg: "FILE", desc: "the saver templates (web's os/saver-templates.json): saver.template is checked and compiled from it" },
  { name: "home-templates", type: "string" as const, arg: "FILE", desc: "Home templates as JSON {id: s3.home}: home.template other than default" },
];

function readPreset(f: string): unknown {
  if (!fs.existsSync(f)) throw new CliError(`${f}: not found`, EXIT.USAGE);
  try {
    return JSON.parse(fs.readFileSync(f, "utf8"));
  } catch (e) {
    throw new CliError(`${f}: not JSON (${(e as Error).message})`, EXIT.USAGE);
  }
}

async function checkCmd(c: Ctx): Promise<number> {
  if (c.opts.rules) {
    if (state.json) emitJson(PRESET_RULES);
    else for (const r of PRESET_RULES) say(`${r.code}  ${r.level.padEnd(7)}  ${r.text}`);
    return EXIT.OK;
  }
  if (!c.args.length) throw new CliError("give one or more preset files", EXIT.USAGE, "espos preset check presets/*.json");
  const inputs = readInputs({ catalog: c.opts.catalog, saverTemplates: c.opts.saverTemplates, homeTemplates: c.opts.homeTemplates });
  const results = c.args.map((f) => ({ target: f, problems: checkPreset(readPreset(f), path.basename(f), inputs) }));
  const all: Problem[] = results.flatMap((r) => r.problems);
  const failed = hasErrors(all) || (c.opts.strict && all.some((p) => p.level === "warning"));
  if (state.json) {
    emitJson({ ok: !failed, results: results.map((r) => ({ ...r, ...countLevels(r.problems) })) });
    return failed ? EXIT.FAIL : EXIT.OK;
  }
  for (const r of results) {
    say(`${countLevels(r.problems).errors ? red("FAIL") : green("OK  ")}  ${r.target}: ${summary(r.problems)}`);
    printProblems(r.problems);
  }
  return failed ? EXIT.FAIL : EXIT.OK;
}

async function compileCmd(c: Ctx): Promise<number> {
  const f = c.args[0];
  if (!f) throw new CliError("give the preset file", EXIT.USAGE);
  const board = c.opts.board;
  if (!board) throw new CliError("give --board es3c35p or es3c28p", EXIT.USAGE);
  const p = readPreset(f) as Record<string, any>;
  const inputs = readInputs({ catalog: c.opts.catalog, saverTemplates: c.opts.saverTemplates, homeTemplates: c.opts.homeTemplates });
  const problems = checkPreset(p, path.basename(f), inputs);
  if (hasErrors(problems)) {
    printProblems(problems);
    throw new CliError(`${f}: fix the errors first (espos preset check)`, EXIT.FAIL);
  }
  let dev: Record<string, unknown>;
  try {
    dev = compilePreset(p, board, inputs);
  } catch (e) {
    throw new CliError((e as Error).message, EXIT.FAIL);
  }
  if (p.saver?.template && !inputs.saverTemplates) {
    problems.push({ level: "warning", code: "P009", where: path.basename(f), msg: `saver.template "${p.saver.template}" not compiled: give --saver-templates (the device keeps its own saver)` });
  }
  const bad = checkDef(presetSchema(), "device", dev);
  if (bad.length) throw new CliError(`the compiled file does not match the schema's "device": ${bad.join("; ")}`, EXIT.FAIL);
  const text = JSON.stringify(dev, null, 1) + "\n";
  if (c.opts.out) {
    fs.mkdirSync(path.dirname(path.resolve(c.opts.out)), { recursive: true });
    fs.writeFileSync(c.opts.out, text);
    if (!state.json) say(`${bold(String(dev.id))} v${dev.version} for ${board} -> ${c.opts.out}`);
  }
  if (state.json) emitJson({ ok: true, problems, device: dev });
  else {
    if (!c.opts.out) process.stdout.write(text);
    printProblems(problems);
  }
  return EXIT.OK;
}

export const presetCommand: CmdSpec = {
  name: "preset",
  summary: "Product presets: check a preset file, or make the device's file for one board.",
  description:
    "A preset is the starting point of one product: the packages it comes with, the assistant and saver, settings, the first-boot questions and the names of what the buyer is entitled to (resolved only by the seller's server). It never locks anything: the customer can change every part, and a newer version changes only what the customer never changed (the device's three-way merge). The format is sdk/schemas/preset-v1.schema.json; tools/make_bundle.py --preset builds a delivery bundle from it.",
  sub: [
    {
      name: "check",
      summary: "Check preset files against the format and its rules (P001-P014).",
      args: [{ name: "files", desc: "preset .json files", variadic: true }],
      opts: [...INPUT_OPTS, { name: "strict", type: "bool", desc: "warnings fail too" }, { name: "rules", type: "bool", desc: "list the rules" }],
      json: true,
      examples: ["espos preset check sdk/presets/clock-desk.json", "espos preset check my-product.json --catalog build-pkgs/espos/s3/catalog.json"],
      run: checkCmd,
    },
    {
      name: "compile",
      summary: "Write the device's /sys/preset.json for one board (parts, saver template, orientation and locale as settings).",
      args: [{ name: "file", desc: "the preset" }],
      opts: [{ name: "board", type: "string", arg: "B", desc: "es3c35p (3.5\") or es3c28p (2.8\")" }, { name: "out", type: "string", arg: "FILE", desc: "write it there (default: print it)" }, ...INPUT_OPTS],
      json: true,
      examples: ["espos preset compile sdk/presets/clock-desk.json --board es3c35p", "espos preset compile p.json --board es3c28p --catalog catalog.json --saver-templates saver-templates.json --out preset.json"],
      run: compileCmd,
    },
  ],
};
