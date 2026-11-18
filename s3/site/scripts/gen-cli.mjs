#!/usr/bin/env node
// Builds src/content/docs/reference/cli.md from the CLI itself:
//   espos help --json            every command, argument and option
//   espos check --rules --json   the checker's rules
// Usage (from site/):
//   node scripts/gen-cli.mjs                       runs ../sdk/packages/cli/bin/espos.js (or $ESPOS_CLI)
//   node scripts/gen-cli.mjs --from help.json [--rules rules.json]   from saved output instead
// The page says it is generated: edit the CLI's command table (sdk/packages/cli/src), not the page.
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const site = path.resolve(here, '..');
const out = path.join(site, 'src/content/docs/reference/cli.md');

const argv = process.argv.slice(2);
const opt = (name) => {
  const i = argv.indexOf(name);
  return i >= 0 ? argv[i + 1] : undefined;
};

function runCli(args) {
  const cli = process.env.ESPOS_CLI ?? path.resolve(site, '../sdk/packages/cli/bin/espos.js');
  if (!fs.existsSync(cli)) {
    console.error(`gen-cli: ${cli} not found. Build the CLI (cd sdk && npm install) or pass --from help.json.`);
    process.exit(3);
  }
  return execFileSync(process.execPath, [cli, ...args], { encoding: 'utf8', maxBuffer: 16 << 20 });
}

const help = JSON.parse(opt('--from') ? fs.readFileSync(opt('--from'), 'utf8') : runCli(['help', '--json']));
let rules = [];
try {
  rules = JSON.parse(opt('--rules') ? fs.readFileSync(opt('--rules'), 'utf8') : runCli(['check', '--rules', '--json']));
} catch (e) {
  console.error(`gen-cli: no rules (${e.message.split('\n')[0]}); the rules section is left out`);
}

// ---- text helpers. The page is plain Markdown: escape what Markdown or HTML would eat.
const ELLIPSIS = /…/g;
const clean = (s) => String(s ?? '').replace(/"…"/g, 'the ellipsis character').replace(ELLIPSIS, '...');
const text = (s) => clean(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
const cell = (s) => text(s).replace(/\|/g, '\\|').replace(/\n/g, ' ');
const code = (s) => {
  const t = clean(s).replace(/\|/g, '\\|');
  return t.includes('`') ? `\`\` ${t} \`\`` : `\`${t}\``;
};
const anchor = (p) => p.replace(/\s+/g, '-').toLowerCase();

function usage(c) {
  if (c.usage) return clean(c.usage);
  const args = (c.args ?? []).map((a) => (a.variadic ? `[${a.name}...]` : a.required ? `<${a.name}>` : `[${a.name}]`));
  const hasOpts = (c.options ?? []).length > 0;
  return ['espos', c.path, ...args, hasOpts ? '[options]' : ''].filter(Boolean).join(' ');
}

function optName(o) {
  const n = o.type === 'bool' || !o.placeholder ? o.name : `${o.name} ${o.placeholder}`;
  return o.short ? `${o.short}, ${n}` : n;
}

function optInfo(o) {
  const bits = [text(o.description)];
  if (o.values) bits.push(`One of ${o.values.map((v) => code(v)).join(', ')}.`);
  if (o.default !== undefined) bits.push(`Default: ${code(JSON.stringify(o.default))}.`);
  if (o.env) bits.push(`Environment: ${code(o.env)}.`);
  if (o.repeatable) bits.push('Can be repeated.');
  return bits.join(' ').replace(/\|/g, '\\|');
}

function command(c, depth) {
  const h = '#'.repeat(depth);
  const lines = [`${h} espos ${c.path}`, '', text(c.summary), ''];
  if (c.description) lines.push(text(c.description), '');
  if (c.platform) lines.push(`**Platform:** ${text(c.platform)}`, '');
  if (!c.commands || (c.args ?? []).length || (c.options ?? []).length) {
    lines.push('```sh', usage(c).replace(ELLIPSIS, '...'), '```', '');
  }
  if ((c.args ?? []).length) {
    lines.push('| Argument | Required | Description |', '|---|---|---|');
    for (const a of c.args) lines.push(`| ${code(a.name + (a.variadic ? '...' : ''))} | ${a.required ? 'yes' : 'no'} | ${cell(a.description)} |`);
    lines.push('');
  }
  if ((c.options ?? []).length) {
    lines.push('| Option | Description |', '|---|---|');
    for (const o of c.options) lines.push(`| ${code(optName(o))} | ${optInfo(o)} |`);
    lines.push('');
  }
  if (c.passthrough) lines.push(`Arguments after \`--\` go to ${text(c.passthrough)}.`, '');
  if (c.json) lines.push('Supports `--json`.', '');
  for (const n of c.notes ?? []) lines.push(`> ${text(n)}`, '');
  if ((c.examples ?? []).length) lines.push('```sh', ...c.examples.map(clean), '```', '');
  for (const s of c.commands ?? []) lines.push(...command(s, Math.min(depth + 1, 4)));
  return lines;
}

const all = [];
const walk = (c) => {
  all.push(c);
  (c.commands ?? []).forEach(walk);
};
help.commands.forEach(walk);

const page = [
  '---',
  'title: CLI',
  `description: 'Every espos command, argument and option, generated from the CLI (version ${help.version}).'`,
  '---',
  '',
  ':::note[Generated page]',
  `This reference is generated from \`espos help --json\` (espos ${help.version}) by \`site/scripts/gen-cli.mjs\`. To change it, change the CLI's command table and run \`npm run gen:cli\` in \`site/\`.`,
  ':::',
  '',
  text(help.summary),
  '',
  '```sh',
  help.usage,
  '```',
  '',
  'The CLI is **Preview**: commands and options may still change before 1.0. It works inside a checkout of the firmware repository, and runs the repository\'s simulator, QEMU, PlatformIO and Python tools for you. Setup: [Set up your tools](/getting-started/install/).',
  '',
  '## Commands',
  '',
  '| Command | What it does |',
  '|---|---|',
  ...all.map((c) => `| [${code('espos ' + c.path)}](#espos-${anchor(c.path)}) | ${cell(c.summary)} |`),
  '',
  '## Global options',
  '',
  '| Option | Description |',
  '|---|---|',
  ...help.global_options.map((o) => `| ${code(o.short ? `${o.short}, ${o.name}` : o.name)} | ${optInfo(o)} |`),
  '',
  '## Output and exit codes',
  '',
  text(help.output),
  '',
  '| Code | Meaning |',
  '|---|---|',
  ...Object.entries(help.exit_codes).map(([k, v]) => `| ${code(k)} | ${cell(v)} |`),
  '',
  '## Environment',
  '',
  '| Variable | Meaning |',
  '|---|---|',
  ...Object.entries(help.environment).map(([k, v]) => `| ${code(k)} | ${cell(v)} |`),
  '',
  ...help.commands.flatMap((c) => command(c, 2)),
];

if (rules.length) {
  page.push(
    '## Check rules',
    '',
    '`espos check` and `espos build` apply these rules. Errors stop a build; warnings stop it only with `--strict`. `espos check --rules` prints this list.',
    '',
    '| Code | Level | Rule | Source |',
    '|---|---|---|---|',
    ...rules.map((r) => `| ${code(r.code)} | ${r.level} | ${cell(r.text)} | ${cell(r.from)} |`),
    '',
  );
}

fs.writeFileSync(out, page.join('\n'));
console.log(`gen-cli: wrote ${path.relative(site, out)} (${all.length} commands, ${rules.length} rules, espos ${help.version})`);
