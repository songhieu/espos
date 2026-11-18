// espos device: the EspOS S3's dev console over USB serial (src/devtools.cpp).
import fs from "node:fs";
import path from "node:path";
import readline from "node:readline";
import { show } from "../common.js";
import type { CmdSpec, Ctx, OptSpec } from "../args.js";
import { encodePng } from "../image.js";
import { CliError, dim, emitJson, EXIT, note, say, state, warn, yellow } from "../out.js";
import { connect, DEFAULT_PORT, denyList, identify, isEspressif, known, listPorts, portKey, shot, type Device } from "../serial.js";

const PORT_OPT: OptSpec = { name: "port", short: "p", type: "string", arg: "PATH", desc: "the S3's serial port (it must answer ESPOS:READY with kind s3)", env: "ESPOS_PORT" };

async function withDevice<T>(c: Ctx, fn: (d: Device) => Promise<T>): Promise<T> {
  const d = await connect({ port: c.opts.port });
  note(dim(`${d.path}: ${d.ready?.raw ?? ""}`));
  try {
    return await fn(d);
  } finally {
    await d.close();
  }
}

/** Prints what the device says for ms (stops early after quiet ms once something came). */
async function echo(d: Device, ms: number, quiet = 0, filter?: (l: string) => boolean): Promise<string[]> {
  const out: string[] = [];
  const end = Date.now() + ms;
  let got = false;
  while (Date.now() < end) {
    const ln = await d.readLine(got && quiet ? Math.min(quiet, end - Date.now()) : end - Date.now());
    if (ln === null) {
      if (got && quiet) break;
      continue;
    }
    got = true;
    if (filter && !filter(ln)) continue;
    out.push(ln);
    if (!state.json) say(ln);
  }
  return out;
}

async function listCmd(c: Ctx): Promise<number> {
  const ports = await listPorts();
  const k = known();
  const deny = denyList();
  const rows = [];
  for (const p of ports) {
    const row: Record<string, unknown> = { path: p.path, usb: p.vendorId ? `${p.vendorId}:${p.productId}` : null, serial: p.serialNumber ?? null, manufacturer: p.manufacturer ?? null };
    if (deny.includes(portKey(p.path))) row.status = "denied (never opened)";
    else if (!isEspressif(p)) row.status = "not an Espressif chip (not opened)";
    else if (p.serialNumber && k.other[p.serialNumber]) row.status = `not an S3 (answered earlier: ${k.other[p.serialNumber].answer})`;
    else if (c.opts.probe) {
      try {
        const { dev, ready } = await identify(p.path, p);
        if (dev) await dev.close();
        row.status = ready?.kind === "s3" ? "EspOS S3" : ready ? "not an S3" : "no answer";
        if (ready) Object.assign(row, { fw: ready.fw ?? null, kind: ready.kind ?? null, board: ready.board ?? null, linked: ready.linked ?? null });
      } catch (e) {
        row.status = `could not open: ${(e as Error).message}`;
      }
    } else if (p.serialNumber && k.s3[p.serialNumber]) row.status = `EspOS S3 (remembered: ${k.s3[p.serialNumber].fw ?? ""} ${k.s3[p.serialNumber].board ?? ""})`.trim();
    else row.status = portKey(p.path) === portKey(DEFAULT_PORT) ? "Espressif: the default port, opened by device commands" : "Espressif: not probed (--probe asks it)";
    rows.push(row);
  }
  if (state.json) {
    emitJson({ ports: rows, deny_list: deny });
    return EXIT.OK;
  }
  if (!rows.length) say("no serial ports");
  for (const r of rows) say(`${String(r.path).padEnd(34)} ${String(r.usb ?? "-").padEnd(10)} ${String(r.serial ?? "-").padEnd(20)} ${r.status}`);
  if (!c.opts.probe && rows.some((r) => String(r.status).includes("not probed"))) note(dim("--probe opens the Espressif ports above to ask ESPOS:HELLO (it can reset an ESP32; denied ports are never opened)"));
  return EXIT.OK;
}

async function infoCmd(c: Ctx): Promise<number> {
  return withDevice(c, async (d) => {
    if (state.json) emitJson({ port: d.path, ...d.ready });
    else say(`${d.path}: ${d.ready?.raw}`);
    return EXIT.OK;
  });
}

async function shotCmd(c: Ctx): Promise<number> {
  const out = path.resolve(c.args[0] ?? `shot-${new Date().toISOString().replace(/[-:]/g, "").replace(/\..*/, "")}.png`);
  return withDevice(c, async (d) => {
    const s = await shot(d);
    fs.mkdirSync(path.dirname(out), { recursive: true });
    fs.writeFileSync(out, encodePng(s.width, s.height, s.rgba));
    if (state.json) emitJson({ file: out, width: s.width, height: s.height });
    else say(`${show(out)} ${s.width}x${s.height}`);
    return EXIT.OK;
  });
}

async function logsCmd(c: Ctx): Promise<number> {
  const re = c.opts.grep ? new RegExp(c.opts.grep) : null;
  return withDevice(c, async (d) => {
    const secs = c.opts.seconds ?? Infinity;
    note(dim(Number.isFinite(secs) ? `reading for ${secs} s` : "reading until Ctrl-C"));
    let stop = false;
    process.once("SIGINT", () => (stop = true));
    const end = Date.now() + secs * 1000;
    while (!stop && Date.now() < end) {
      const ln = await d.readLine(Math.min(250, end - Date.now()));
      if (ln === null) continue;
      if (!c.opts.all && /^(sysmon|heap)/.test(ln)) continue;
      if (re && !re.test(ln)) continue;
      process.stdout.write(ln + "\n");
    }
    return EXIT.OK;
  });
}

const simple = (line: (c: Ctx) => string, wait: number, quiet = 0) => async (c: Ctx) =>
  withDevice(c, async (d) => {
    const l = line(c);
    note(dim(`> ${l}`));
    await d.write(l + "\n");
    const lines = await echo(d, c.opts.wait !== undefined ? c.opts.wait * 1000 : wait, quiet);
    if (state.json) emitJson({ sent: l, lines });
    return EXIT.OK;
  });

const intArg = (s: string, what: string) => {
  if (!/^-?\d+$/.test(s)) throw new CliError(`${what}: a whole number, got ${JSON.stringify(s)}`, EXIT.USAGE);
  return s;
};

async function consoleCmd(c: Ctx): Promise<number> {
  return withDevice(c, async (d) => {
    note(`${d.path}: dev console (APP id, TAP x y, DRAG x0 y0 x1 y1 ms, HOME, SHOT is binary: use espos device shot, MEM, TRACE, JS code, ...); Ctrl-D or Ctrl-C quits`);
    d.onBytes = (b) => process.stdout.write(b);
    const rl = readline.createInterface({ input: process.stdin, terminal: false });
    await new Promise<void>((res) => {
      rl.on("line", (l) => {
        if (/^\s*SHOT\s*$/i.test(l)) {
          note(yellow("SHOT prints a binary picture; use `espos device shot` in another terminal"));
          return;
        }
        void d.write(l + "\n");
      });
      rl.on("close", () => res());
      process.once("SIGINT", () => rl.close());
    });
    return EXIT.OK;
  });
}

const WAIT_OPT: OptSpec = { name: "wait", type: "number", arg: "S", desc: "seconds to print the device's answer" };

export const deviceCommand: CmdSpec = {
  name: "device",
  summary: "Talk to an EspOS S3 over USB: list, info, shot, logs, app, tap, drag, home, trace, mem, js, send, console.",
  description:
    "Port safety: ports are listed from the OS without opening them; a port is opened only to ask ESPOS:HELLO and used only if it answers ESPOS:READY with kind s3; ports on the deny list are never opened. Without --port, device commands open only an S3 remembered by its USB serial number or the default " +
    DEFAULT_PORT +
    "; `device list --probe` identifies other Espressif ports (and remembers the S3). No flashing here.",
  platform: "macOS and Linux; needs the optional npm package serialport",
  sub: [
    { name: "list", summary: "Serial ports and which one is an EspOS S3 (nothing is opened without --probe).", opts: [{ name: "probe", type: "bool", desc: "open Espressif ports (not denied ones) to ask ESPOS:HELLO" }], json: true, run: listCmd },
    { name: "info", summary: "The S3's answer to HELLO: firmware, board, linked (also sets its clock).", opts: [PORT_OPT], json: true, run: infoCmd },
    { name: "shot", summary: "Screenshot to PNG (screen + top layer).", args: [{ name: "out", desc: "the PNG (default shot-<time>.png)", optional: true }], opts: [PORT_OPT], json: true, run: shotCmd },
    {
      name: "logs",
      summary: "Stream the S3's serial log.",
      opts: [PORT_OPT, { name: "seconds", type: "number", arg: "S", desc: "stop after S seconds (default: Ctrl-C)" }, { name: "grep", type: "string", arg: "RE", desc: "only lines matching" }, { name: "all", type: "bool", desc: "include sysmon/heap lines" }],
      run: logsCmd,
    },
    {
      name: "app",
      summary: "Open an app or package (APP id [arg]).",
      args: [{ name: "id", desc: "app or package id (firmware before 0.4.0 reads only 23 characters of it)" }, { name: "arg", desc: "what the app gets as app.arg", optional: true }],
      opts: [PORT_OPT, WAIT_OPT],
      json: true,
      run: async (c) => {
        const id = c.args[0];
        if (id.length > 51) throw new CliError(`the dev console's APP reads at most 51 characters of the id (src/devtools.cpp); ${id} has ${id.length}`, EXIT.USAGE);
        if (Buffer.byteLength(c.args[1] ?? "") > 63) throw new CliError("the dev console's APP reads at most 63 bytes of the argument", EXIT.USAGE);
        return withDevice(c, async (d) => {
          // before 0.4.0 the console read 23 characters of the id (char id[24])
          const v = /(\d+)\.(\d+)\.(\d+)/.exec(d.ready?.fw ?? "");
          const code = v ? +v[1] * 10000 + +v[2] * 100 + +v[3] : 0;
          if (id.length > 23 && code < 400) warn(`firmware ${d.ready?.fw ?? "?"} reads only 23 characters of APP's id: it gets "${id.slice(0, 23)}" (fixed in 0.4.0)`);
          const l = `APP ${id}${c.args[1] ? " " + c.args[1] : ""}`;
          note(dim(`> ${l}`));
          await d.write(l + "\n");
          const lines = await echo(d, c.opts.wait !== undefined ? c.opts.wait * 1000 : 1500);
          if (state.json) emitJson({ sent: l, lines });
          return EXIT.OK;
        });
      },
    },
    { name: "tap", summary: "Tap at x y (screen coordinates of the current rotation).", args: [{ name: "x", desc: "0-319 (portrait)" }, { name: "y", desc: "0-479 (portrait)" }], opts: [PORT_OPT, WAIT_OPT], json: true, run: simple((c) => `TAP ${intArg(c.args[0], "x")} ${intArg(c.args[1], "y")}`, 600) },
    {
      name: "drag",
      summary: "Drag from x0 y0 to x1 y1 in ms.",
      args: [{ name: "x0", desc: "start x" }, { name: "y0", desc: "start y" }, { name: "x1", desc: "end x" }, { name: "y1", desc: "end y" }, { name: "ms", desc: "duration (default 250)", optional: true }],
      opts: [PORT_OPT, WAIT_OPT],
      json: true,
      run: simple((c) => `DRAG ${c.args.slice(0, 4).map((v, i) => intArg(v, ["x0", "y0", "x1", "y1"][i])).join(" ")} ${intArg(c.args[4] ?? "250", "ms")}`, 800),
    },
    { name: "home", summary: "Go to Home.", opts: [PORT_OPT, WAIT_OPT], json: true, run: simple(() => "HOME", 600) },
    { name: "trace", summary: "The device's event history (TRACE: touches, screens, slow loops, heap).", opts: [PORT_OPT, WAIT_OPT], json: true, run: simple(() => "TRACE", 4000, 700) },
    { name: "mem", summary: "Internal RAM, PSRAM and LVGL memory (MEM).", opts: [PORT_OPT, WAIT_OPT], json: true, run: simple(() => "MEM", 1500, 400) },
    { name: "js", summary: "Evaluate JavaScript in a scratch QuickJS context (JS code).", args: [{ name: "code", desc: "one line of JavaScript", variadic: true }], opts: [PORT_OPT, WAIT_OPT], json: true, run: simple((c) => `JS ${c.args.join(" ")}`, 2000, 400) },
    {
      name: "send",
      summary: "Send one dev console line and print the answer (not SHOT: use device shot).",
      args: [{ name: "line", desc: "the command, e.g. ROT 1, SAVER, REMOTE", variadic: true }],
      opts: [PORT_OPT, WAIT_OPT],
      json: true,
      run: async (c) => {
        const l = c.args.join(" ");
        if (!l) throw new CliError("send what?", EXIT.USAGE);
        if (/^\s*SHOT\s*$/i.test(l)) throw new CliError("SHOT answers with a binary picture: espos device shot", EXIT.USAGE);
        if (/^\s*(ESPOS:PROVISION|CMD:WIFI)/i.test(l)) throw new CliError("provisioning and WiFi lines are not sent from here (the web installer does that)", EXIT.USAGE);
        return simple(() => l, 1500, 500)(c);
      },
    },
    { name: "console", summary: "Interactive dev console: your lines to the S3, its output here.", opts: [PORT_OPT], run: consoleCmd },
  ],
};

