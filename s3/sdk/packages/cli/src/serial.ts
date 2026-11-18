// The EspOS S3 dev console over USB serial (src/devtools.cpp; tools/s3cli.py, scen.py, qa.py), with safety rules:
//   - ports are listed from the OS's USB data without opening them;
//   - a port is opened only to ask "ESPOS:HELLO" and is used only when it answers "ESPOS:READY" with kind "s3";
//   - ports on the deny list (ESPOS_DENY_PORTS + ~/.config/espos/config.json "deny_ports") are never
//     opened, not even with --port;
//   - without --port, only an S3 remembered by its USB serial number, ESPOS_PORT or the default
//     /dev/cu.usbmodem1101 is opened; other Espressif ports are opened only by `espos device list --probe`
//     (opening an ESP32's USB port can reset it; the EspOS X4 reader shares the S3's USB ids 303a:1001).
import fs from "node:fs";
import path from "node:path";
import { CliError, dim, EXIT, note } from "./out.js";
import { configDir, hint } from "./repo.js";

/** Never opened: none built in. Add ports with ESPOS_DENY_PORTS or "deny_ports" in config.json. */
const BUILTIN_DENY: string[] = [];
export const DEFAULT_PORT = "/dev/cu.usbmodem1101";
const ESPRESSIF_VID = "303a";

export interface PortInfo {
  path: string;
  vendorId?: string;
  productId?: string;
  serialNumber?: string;
  manufacturer?: string;
  locationId?: string;
}

export interface Ready {
  fw?: string;
  kind?: string;
  board?: string;
  linked?: boolean;
  raw: string;
}

// eslint-disable-next-line @typescript-eslint/no-explicit-any
let SP: any = null;
async function serialport(): Promise<any> {
  if (SP) return SP;
  try {
    SP = (await import("serialport")).SerialPort;
  } catch {
    throw new CliError("device commands need the optional npm package 'serialport', which is not installed", EXIT.MISSING, hint("serialport"));
  }
  return SP;
}

/** The port's short name: /dev/cu.usbmodem1101 and /dev/tty.usbmodem1101 are both "usbmodem1101". */
export const portKey = (p: string) => path.basename(p).replace(/^(cu|tty)\./, "");

interface Config {
  deny_ports?: string[];
}
function readConfig(): Config {
  try {
    return JSON.parse(fs.readFileSync(path.join(configDir(), "config.json"), "utf8"));
  } catch {
    return {};
  }
}

export function denyList(): string[] {
  const env = (process.env.ESPOS_DENY_PORTS ?? "").split(/[,\s]+/).filter(Boolean);
  return [...BUILTIN_DENY, ...env, ...(readConfig().deny_ports ?? [])].map(portKey);
}
export const isDenied = (p: string) => denyList().includes(portKey(p));

// ---------------------------------------------------------------- remembered devices (by USB serial number)

interface Known {
  s3: Record<string, { path: string; fw?: string; board?: string; seen: string }>;
  other: Record<string, { path: string; answer: string; seen: string }>;
}
const knownFile = () => path.join(configDir(), "devices.json");
export function known(): Known {
  try {
    const k = JSON.parse(fs.readFileSync(knownFile(), "utf8"));
    return { s3: k.s3 ?? {}, other: k.other ?? {} };
  } catch {
    return { s3: {}, other: {} };
  }
}
function remember(info: PortInfo, ready: Ready | null): void {
  if (!info.serialNumber) return;
  const k = known();
  const seen = new Date().toISOString();
  if (ready?.kind === "s3") {
    k.s3[info.serialNumber] = { path: info.path, fw: ready.fw, board: ready.board, seen };
    delete k.other[info.serialNumber];
  } else if (ready) {
    k.other[info.serialNumber] = { path: info.path, answer: ready.raw.slice(0, 120), seen };
  } else return;
  try {
    fs.mkdirSync(configDir(), { recursive: true });
    fs.writeFileSync(knownFile(), JSON.stringify(k, null, 2) + "\n");
  } catch {
    /* not remembered */
  }
}

/** USB serial ports from the OS (nothing is opened). macOS paths are given as /dev/cu.*. */
export async function listPorts(): Promise<PortInfo[]> {
  const S = await serialport();
  const ps: PortInfo[] = await S.list();
  return ps.map((p) => ({ ...p, path: p.path.replace(/^\/dev\/tty\./, "/dev/cu."), vendorId: p.vendorId?.toLowerCase(), productId: p.productId?.toLowerCase() }));
}
export const isEspressif = (p: PortInfo) => p.vendorId === ESPRESSIF_VID;

// ---------------------------------------------------------------- a session

export class Device {
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  private port: any;
  private buf = Buffer.alloc(0);
  private wake: (() => void) | null = null;
  private closed = false;
  ready: Ready | null = null;
  onBytes: ((b: Buffer) => void) | null = null;

  private constructor(readonly path: string) {}

  static async open(p: string): Promise<Device> {
    if (isDenied(p)) throw new CliError(`${p} is on the deny list: never opened`, EXIT.DEVICE);
    const S = await serialport();
    const d = new Device(p);
    await new Promise<void>((res, rej) => {
      d.port = new S({ path: p, baudRate: 115200, autoOpen: false });
      d.port.open((e: Error | null) => (e ? rej(new CliError(`${p}: ${e.message}`, EXIT.DEVICE)) : res()));
    });
    d.port.on("data", (b: Buffer) => {
      if (d.onBytes) d.onBytes(b);
      else d.buf = Buffer.concat([d.buf, b]);
      d.wake?.();
    });
    d.port.on("close", () => {
      d.closed = true;
      d.wake?.();
    });
    d.port.on("error", () => {
      d.closed = true;
      d.wake?.();
    });
    return d;
  }

  private waitData(ms: number): Promise<void> {
    return new Promise((res) => {
      const t = setTimeout(() => {
        this.wake = null;
        res();
      }, ms);
      this.wake = () => {
        clearTimeout(t);
        this.wake = null;
        res();
      };
    });
  }

  write(s: string | Buffer): Promise<void> {
    return new Promise((res, rej) => this.port.write(s, (e: Error | null) => (e ? rej(e) : this.port.drain(() => res()))));
  }

  /** One line (without \r\n), or null after ms. */
  async readLine(ms: number): Promise<string | null> {
    const end = Date.now() + ms;
    for (;;) {
      const i = this.buf.indexOf(10);
      if (i >= 0) {
        const line = this.buf.subarray(0, i).toString("utf8").replace(/\r$/, "");
        this.buf = this.buf.subarray(i + 1);
        return line;
      }
      const left = end - Date.now();
      if (left <= 0 || this.closed) return null;
      await this.waitData(left);
    }
  }

  async readBytes(n: number, ms: number): Promise<Buffer> {
    const end = Date.now() + ms;
    while (this.buf.length < n) {
      const left = end - Date.now();
      if (left <= 0 || this.closed) throw new CliError(`the device sent ${this.buf.length} of ${n} bytes`, EXIT.FAIL);
      await this.waitData(left);
    }
    const out = this.buf.subarray(0, n);
    this.buf = this.buf.subarray(n);
    return Buffer.from(out);
  }

  drop(): void {
    this.buf = Buffer.alloc(0);
  }

  /** ESPOS:HELLO <ms> (sets the board's clock) -> ESPOS:READY {"fw", "kind", "board", "linked"}. */
  async hello(ms = 3000): Promise<Ready | null> {
    await new Promise((r) => setTimeout(r, 200));
    this.drop();
    for (let k = 0; k < 2 && !this.ready; k++) {
      await this.write(`ESPOS:HELLO ${Date.now()}\n`);
      const end = Date.now() + ms / 2;
      while (Date.now() < end) {
        const ln = await this.readLine(end - Date.now());
        if (ln === null) break;
        const i = ln.indexOf("ESPOS:READY");
        if (i >= 0) {
          const raw = ln.slice(i + "ESPOS:READY".length).trim();
          let j: Record<string, unknown> = {};
          try {
            j = JSON.parse(raw);
          } catch {
            /* an answer without JSON */
          }
          this.ready = { fw: j.fw as string, kind: j.kind as string, board: j.board as string, linked: j.linked as boolean, raw: ln.slice(i) };
          break;
        }
      }
    }
    return this.ready;
  }

  close(): Promise<void> {
    if (this.closed) return Promise.resolve();
    return new Promise((res) => this.port.close(() => res()));
  }
}

/** Opens a port only to ask who it is; closes it unless it is an EspOS S3. */
export async function identify(p: string, info?: PortInfo): Promise<{ dev: Device | null; ready: Ready | null }> {
  const dev = await Device.open(p);
  const ready = await dev.hello();
  if (info) remember(info, ready);
  if (ready?.kind === "s3") return { dev, ready };
  await dev.close();
  return { dev: null, ready };
}

/** The EspOS S3 to talk to (see the rules at the top), opened and identified. */
export async function connect(o: { port?: string }): Promise<Device> {
  const explicit = o.port || process.env.ESPOS_PORT;
  if (explicit) {
    const p = explicit;
    if (isDenied(p)) throw new CliError(`${p} is on the deny list: never opened`, EXIT.DEVICE);
    const infos = await listPorts().catch(() => [] as PortInfo[]);
    const info = infos.find((x) => portKey(x.path) === portKey(p));
    const { dev, ready } = await identify(p, info);
    if (!dev) throw new CliError(`${p} is not an EspOS S3 (${ready ? `it answered: ${ready.raw}` : "no ESPOS:READY"}); closed without sending anything else`, EXIT.DEVICE);
    return dev;
  }
  const ports = (await listPorts()).filter((p) => !isDenied(p.path));
  const k = known();
  const tried: string[] = [];
  for (const p of ports.filter((x) => isEspressif(x) && x.serialNumber && k.s3[x.serialNumber])) {
    const { dev } = await identify(p.path, p);
    if (dev) return dev;
    tried.push(p.path);
  }
  const def = ports.find((x) => portKey(x.path) === portKey(DEFAULT_PORT) && !(x.serialNumber && k.other[x.serialNumber]));
  if (def && !tried.includes(def.path)) {
    const { dev, ready } = await identify(def.path, def);
    if (dev) return dev;
    note(dim(`${def.path}: ${ready ? `answered as ${ready.kind ?? "not an S3"}` : "no answer"}`));
    tried.push(def.path);
  }
  const unknown = ports.filter((x) => isEspressif(x) && !tried.includes(x.path) && !(x.serialNumber && k.other[x.serialNumber]));
  throw new CliError(
    tried.length || unknown.length ? "no EspOS S3 identified" : "no EspOS S3 connected (no Espressif USB serial port)",
    EXIT.DEVICE,
    unknown.length
      ? `not opened automatically: ${unknown.map((x) => x.path).join(", ")}; \`espos device list --probe\` asks them who they are (an S3 is then remembered), or pass --port PATH`
      : "plug the board in with a data cable; then espos device list",
  );
}

/** A screenshot: SHOT -> "SHOT w h 2" + RGB565, "TOP w h 4" + ARGB8888 (the top layer), composited (as s3cli.py). */
export async function shot(dev: Device): Promise<{ width: number; height: number; rgba: Uint8Array }> {
  dev.drop();
  await dev.write("SHOT\n");
  const header = async (tag: string) => {
    for (;;) {
      const ln = await dev.readLine(10000);
      if (ln === null) throw new CliError(`no ${tag} answer from the device`, EXIT.FAIL);
      const i = ln.indexOf(tag + " ");
      if (i >= 0) return ln.slice(i).split(/\s+/).slice(1).map(Number);
    }
  };
  const [w, h, bpp = 0] = await header("SHOT");
  if (!w || bpp !== 2) throw new CliError(`unexpected screenshot format ${w}x${h}x${bpp}`, EXIT.FAIL);
  const scr = await dev.readBytes(w * h * bpp, 20000);
  await dev.readBytes(1, 2000);
  const [tw, th, tb = 0] = await header("TOP");
  const top = tw ? await dev.readBytes(tw * th * tb, 20000) : null;
  if (tw) await dev.readBytes(1, 2000);
  const rgba = new Uint8Array(w * h * 4);
  for (let i = 0; i < w * h; i++) {
    const v = scr[2 * i] | (scr[2 * i + 1] << 8);
    rgba[4 * i] = Math.floor((((v >> 11) & 31) * 255) / 31);
    rgba[4 * i + 1] = Math.floor((((v >> 5) & 63) * 255) / 63);
    rgba[4 * i + 2] = Math.floor(((v & 31) * 255) / 31);
    rgba[4 * i + 3] = 255;
  }
  if (top && tb === 4)
    for (let i = 0; i < Math.min(w * h, tw * th); i++) {
      const b = top[4 * i], g = top[4 * i + 1], r = top[4 * i + 2], a = top[4 * i + 3];
      if (a) for (const [k, c] of [r, g, b].entries()) rgba[4 * i + k] = Math.floor((c * a + rgba[4 * i + k] * (255 - a)) / 255);
    }
  return { width: w, height: h, rgba };
}
