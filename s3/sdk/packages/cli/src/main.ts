// espos: the EspOS developer CLI.
import { GLOBAL_OPTS, helpText, parseArgs, setCmdPath, usageLine, type CmdSpec } from "./args.js";
import { deviceCommand } from "./commands/device.js";
import { pkgCommands } from "./commands/pkg.js";
import { qemuCommand } from "./commands/qemu.js";
import { simCommand } from "./commands/sim.js";
import { doctorCommand, firmwareCommand, j2meCommand } from "./commands/tools.js";
import { CliError, emitJson, EXIT, red, state } from "./out.js";
import { cliVersion, setRootOverride } from "./repo.js";

const helpCmd: CmdSpec = {
  name: "help",
  summary: "Help for a command; --json prints every command, option and argument (the docs site's reference).",
  args: [{ name: "command", desc: "a command (and sub-command)", variadic: true }],
  json: true,
  examples: ["espos help sim", "espos help device shot", "espos help --json > commands.json"],
};

const versionCmd: CmdSpec = { name: "version", summary: "Print the CLI's version.", json: true };

export const COMMANDS: CmdSpec[] = [...pkgCommands, simCommand, qemuCommand, deviceCommand, firmwareCommand, j2meCommand, doctorCommand, helpCmd, versionCmd];

const ROOT: CmdSpec = {
  name: "espos",
  summary: "EspOS developer tools: make, check and run packages (.espk) in the simulator, QEMU or on a board.",
  sub: COMMANDS,
};

/** The whole command table as data (espos help --json). */
export function reference(): unknown {
  const conv = (c: CmdSpec, path: string[]): Record<string, unknown> => ({
    name: c.name,
    path: [...path, c.name].join(" "),
    usage: usageLine(c, [...path, c.name]),
    summary: c.summary,
    ...(c.description ? { description: c.description } : {}),
    ...(c.platform ? { platform: c.platform } : {}),
    json: !!c.json,
    args: (c.args ?? []).map((a) => ({ name: a.name, description: a.desc, required: !a.optional && !a.variadic, variadic: !!a.variadic })),
    options: (c.opts ?? []).map((o) => ({
      name: `--${o.name}`, ...(o.short ? { short: `-${o.short}` } : {}), type: o.type, ...(o.arg ? { placeholder: o.arg } : {}),
      description: o.desc, ...(o.values ? { values: o.values } : {}), ...(o.default !== undefined ? { default: o.default } : {}),
      ...(o.env ? { env: o.env } : {}), ...(o.repeat ? { repeatable: true } : {}),
    })),
    ...(c.passthrough ? { passthrough: c.passthrough } : {}),
    ...(c.notes ? { notes: c.notes } : {}),
    ...(c.examples ? { examples: c.examples } : {}),
    ...(c.sub ? { commands: c.sub.map((s) => conv(s, [...path, c.name])) } : {}),
  });
  return {
    name: "espos",
    version: cliVersion(),
    summary: ROOT.summary,
    usage: "espos <command> [arguments] [options]",
    exit_codes: { "0": "success", "1": "the command ran and found problems or a step failed", "2": "bad arguments", "3": "a tool or file the command needs is missing", "4": "no EspOS S3 found, or a serial port refused for safety" },
    output: "Human text on stdout, warnings and progress on stderr; with --json (commands marked json) stdout carries exactly one JSON document.",
    environment: {
      ESPOS_ROOT: "the s3desk firmware repo (default: found by walking up for platformio.ini + src/)",
      ESPOS_PYTHON: "python3 to run the repo's tools",
      ESPOS_PIO: "PlatformIO's pio (default: PATH, then ~/.platformio/penv/bin/pio)",
      QEMU: "Espressif's qemu-system-xtensa (default: ../tools-ext/qemu next to s3desk)",
      ESPOS_PORT: "the serial port of the EspOS S3 (it must still answer as kind s3)",
      ESPOS_DENY_PORTS: "serial ports never opened, comma separated (also deny_ports in config.json; none by default)",
      ESPOS_SIM_BUILD: "the simulator's build folder name under sim/ (default build, build-live for --live)",
      ESPOS_BBK: "the store upload key, read only by scripts/publish_pkg.py (store publish --yes)",
      ESPOS_PUBLISH_SCRIPT: "scripts/publish_pkg.py if it is not next to s3desk",
      ESPOS_CONFIG_DIR: "where remembered devices are kept (default ~/.config/espos)",
      NO_COLOR: "no colours",
    },
    global_options: GLOBAL_OPTS.map((o) => ({ name: `--${o.name}`, ...(o.short ? { short: `-${o.short}` } : {}), type: o.type, description: o.desc, ...(o.env ? { env: o.env } : {}) })),
    commands: COMMANDS.map((c) => conv(c, [])),
  };
}

function find(argv: string[]): { cmd: CmdSpec; path: string[]; rest: string[] } {
  let cmd = ROOT;
  const path: string[] = [];
  let i = 0;
  while (cmd.sub && i < argv.length && !argv[i].startsWith("-")) {
    const next = cmd.sub.find((s) => s.name === argv[i]);
    if (!next) {
      if (cmd === ROOT) throw new CliError(`unknown command "${argv[i]}" (espos help)`, EXIT.USAGE);
      throw new CliError(`unknown command "${[...path, argv[i]].join(" ")}" (espos help ${path.join(" ")})`, EXIT.USAGE);
    }
    cmd = next;
    path.push(next.name);
    i++;
  }
  return { cmd, path, rest: argv.slice(i) };
}

export async function main(argv: string[]): Promise<number> {
  if (argv.includes("--no-color")) state.color = false;
  if (!argv.length || argv[0] === "--help" || argv[0] === "-h") {
    process.stdout.write(rootHelp() + "\n");
    return argv.length ? EXIT.OK : EXIT.USAGE;
  }
  if (argv[0] === "--version" || argv[0] === "-V") {
    process.stdout.write(cliVersion() + "\n");
    return EXIT.OK;
  }
  const { cmd, path, rest } = find(argv);
  setCmdPath(path.join(" "));
  const parsed = parseArgs(rest, cmd);
  state.json = !!parsed.opts.json;
  state.quiet = !!parsed.opts.quiet;
  if (parsed.opts.color === false) state.color = false;
  setRootOverride(parsed.opts.root);
  if (parsed.opts.help) {
    process.stdout.write(helpText(cmd, path) + "\n");
    return EXIT.OK;
  }
  if (cmd === helpCmd) {
    if (state.json) {
      emitJson(reference());
      return EXIT.OK;
    }
    if (!parsed.args.length) {
      process.stdout.write(rootHelp() + "\n");
      return EXIT.OK;
    }
    const f = find(parsed.args);
    process.stdout.write(helpText(f.cmd, f.path) + "\n");
    return EXIT.OK;
  }
  if (cmd === versionCmd) {
    if (state.json) emitJson({ version: cliVersion(), node: process.version, platform: process.platform });
    else process.stdout.write(cliVersion() + "\n");
    return EXIT.OK;
  }
  if (!cmd.run) {
    process.stdout.write(helpText(cmd, path) + "\n");
    return EXIT.USAGE;
  }
  const r = await cmd.run({ args: parsed.args, opts: parsed.opts, rest: parsed.rest, cmd, path });
  return typeof r === "number" ? r : EXIT.OK;
}

function rootHelp(): string {
  const w = Math.max(...COMMANDS.map((c) => c.name.length));
  return [
    `espos ${cliVersion()}: ${ROOT.summary}`,
    "",
    "Usage: espos <command> [arguments] [options]",
    "",
    "Commands:",
    ...COMMANDS.map((c) => `  ${c.name.padEnd(w)}  ${c.summary}`),
    "",
    "espos <command> --help for its options; espos help --json for all of them as data.",
    "Exit codes: 0 ok, 1 problems found or a step failed, 2 bad arguments, 3 a tool is missing, 4 no device / port refused.",
  ].join("\n");
}

export async function cli(argv: string[]): Promise<void> {
  let code: number;
  try {
    code = await main(argv);
  } catch (e) {
    if (e instanceof CliError) {
      if (state.json) emitJson({ ok: false, error: e.message, hint: e.hint ?? null, exit: e.code });
      process.stderr.write(`${red("espos:")} ${e.message}\n${e.hint ? `  ${e.hint}\n` : ""}`);
      code = e.code;
    } else {
      process.stderr.write(`${red("espos: internal error:")} ${(e as Error)?.stack ?? e}\n`);
      code = EXIT.FAIL;
    }
  }
  process.exitCode = code;
}

