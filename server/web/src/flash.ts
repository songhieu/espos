// Install InkDesk on an Xteink X4 from the browser (Web Serial), then hand the
// device its token, WiFi and settings over the same USB cable
// (docs/inkdesk-api.md, "USB provisioning").
import { ESPLoader, ESPRESSIF_VID, Transport } from "esptool-js";

// The bundle this server offers (`inkdesk firmware add`, see README).
export const FW_MANIFEST_URL = "/firmware/latest.json";
const FLASH_16MB = 16 * 1024 * 1024;

export interface FwPart {
  file: string;
  offset: number;
  url: string;
  size?: number;
}
export interface FwManifest {
  version: string;
  chip: string;
  parts: FwPart[];
}

export interface ProvisionPayload {
  api: string;
  token: string;
  device: { id: number; name: string; user: string };
  wifi: { ssid: string; pass: string }[];
  settings: Record<string, string>;
  settings_rev: number;
}

export type Log = (line: string) => void;
export type Progress = (stage: "backup" | "flash" | "provision", fraction: number) => void;

export const serialSupported = () => typeof navigator !== "undefined" && "serial" in navigator;

export async function pickPort(anyPort = false): Promise<SerialPort> {
  return navigator.serial.requestPort(anyPort ? {} : { filters: [{ usbVendorId: ESPRESSIF_VID }] });
}

export async function loadManifest(): Promise<FwManifest> {
  const res = await fetch(FW_MANIFEST_URL, { cache: "no-store" });
  if (!res.ok) throw new Error("Chưa có bản firmware để cài (manifest " + res.status + ")");
  return (await res.json()) as FwManifest;
}

async function fetchBytes(url: string): Promise<Uint8Array> {
  const res = await fetch(url, { cache: "no-store" });
  if (!res.ok) throw new Error(`Tải ${url.split("/").pop()} lỗi ${res.status}`);
  return new Uint8Array(await res.arrayBuffer());
}

/** Back up (optional) and write the firmware. Returns the 16 MB backup, if taken. */
export async function flashFirmware(
  port: SerialPort,
  manifest: FwManifest,
  opts: { backup: boolean; log: Log; progress: Progress },
): Promise<Uint8Array | null> {
  const { log, progress } = opts;
  log("Tải firmware " + manifest.version + "…");
  const files = await Promise.all(manifest.parts.map(async (p) => ({ address: p.offset, data: await fetchBytes(p.url) })));
  const total = files.reduce((n, f) => n + f.data.length, 0);

  const transport = new Transport(port, false);
  const terminal = { clean() {}, writeLine: (d: string) => log(d), write: () => {} };
  const loader = new ESPLoader({ transport, baudrate: 921600, romBaudrate: 115200, terminal, debugLogging: false });
  let backup: Uint8Array | null = null;
  try {
    const chip = await loader.main();
    log("Đã kết nối: " + chip);
    if (!/ESP32-C3/i.test(chip)) throw new Error("Đây không phải Xteink X4 (chip " + chip + ")");
    if (opts.backup) {
      log("Sao lưu firmware gốc (16 MB, khoảng 3 phút)…");
      backup = await loader.readFlash(0, FLASH_16MB, (_p, done, size) => progress("backup", done / size));
      log("✓ Đã sao lưu " + Math.round(backup.length / 1024 / 1024) + " MB");
    }
    const before: number[] = [];
    let acc = 0;
    for (const f of files) {
      before.push(acc);
      acc += f.data.length;
    }
    log("Ghi firmware…");
    await loader.writeFlash({
      fileArray: files,
      flashMode: "keep",
      flashFreq: "keep",
      flashSize: "keep",
      eraseAll: false,
      compress: true,
      reportProgress: (i, written) => progress("flash", (before[i] + written) / total),
    });
    log("✓ Đã ghi firmware");
    await loader.after("hard_reset");
  } finally {
    await transport.disconnect().catch(() => {});
  }
  return backup;
}

const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

/** After a reset the X4 re-enumerates on USB: find a granted Espressif port again. */
async function reacquire(prev: SerialPort | null, log: Log): Promise<SerialPort> {
  const until = Date.now() + 25000;
  while (Date.now() < until) {
    const ports = await navigator.serial.getPorts();
    const esp = ports.filter((p) => p.getInfo().usbVendorId === ESPRESSIF_VID);
    const pick = (prev && ports.includes(prev) ? prev : null) ?? esp[0] ?? (ports.length === 1 ? ports[0] : null);
    if (pick) return pick;
    await sleep(500);
  }
  log("Không thấy máy sau khi khởi động lại.");
  throw new Error("Không thấy máy. Rút cáp, cắm lại rồi bấm “Chỉ liên kết lại”.");
}

/** Send the provisioning line and wait for INKDESK:OK. */
export async function provisionDevice(
  portHint: SerialPort | null,
  payload: ProvisionPayload,
  log: Log,
  progress: Progress,
): Promise<void> {
  const port = await reacquire(portHint, log);
  await port.open({ baudRate: 115200 }).catch(async (e: Error) => {
    if (!/already open/i.test(e.message)) throw e;
  });
  try {
    await port.setSignals({ dataTerminalReady: false, requestToSend: false }).catch(() => {});
  } catch {
    /* some ports refuse signals */
  }
  const writer = port.writable!.getWriter();
  const reader = port.readable!.getReader();
  const enc = new TextEncoder();
  const dec = new TextDecoder();
  let buf = "";
  let stop = false;
  const lines: string[] = [];
  const pump = (async () => {
    while (!stop) {
      const { value, done } = await reader.read().catch(() => ({ value: undefined, done: true }));
      if (done) break;
      if (!value) continue;
      buf += dec.decode(value, { stream: true });
      let i: number;
      while ((i = buf.indexOf("\n")) >= 0) {
        const line = buf.slice(0, i).replace(/\r$/, "");
        buf = buf.slice(i + 1);
        if (line.startsWith("INKDESK:")) lines.push(line);
      }
    }
  })();
  const waitFor = async (prefix: string[], ms: number, resend?: () => string) => {
    const until = Date.now() + ms;
    let next = 0;
    while (Date.now() < until) {
      const hit = lines.find((l) => prefix.some((p) => l.startsWith(p)));
      if (hit) return hit;
      if (resend && Date.now() >= next) {
        await writer.write(enc.encode(resend() + "\n"));
        next = Date.now() + 1500;
      }
      await sleep(150);
    }
    return null;
  };
  try {
    log("… Chờ máy sẵn sàng nhận cài đặt");
    progress("provision", 0.2);
    // The X4 has no RTC: every HELLO carries our clock, so the device knows the time at once.
    const ready = await waitFor(["INKDESK:READY"], 40000, () => "INKDESK:HELLO " + Date.now());
    if (!ready) throw new Error("Máy không trả lời. Kiểm tra máy đã bật và đang chạy firmware InkDesk.");
    let fw = "";
    try {
      fw = String((JSON.parse(ready.slice(ready.indexOf("{"))) as { fw?: string }).fw ?? "");
    } catch {
      /* no details */
    }
    log(`✓ Máy sẵn sàng${fw ? ` (${fw})` : ""}`);
    progress("provision", 0.5);
    lines.length = 0;
    await writer.write(enc.encode("INKDESK:PROVISION " + JSON.stringify({ ...payload, now_ms: Date.now() }) + "\n"));
    const ans = await waitFor(["INKDESK:OK", "INKDESK:ERR"], 20000);
    if (!ans) throw new Error("Máy không xác nhận đã nhận cài đặt.");
    if (ans.startsWith("INKDESK:ERR")) throw new Error("Máy báo lỗi: " + ans.slice("INKDESK:ERR".length).trim());
    progress("provision", 1);
    log("✓ Máy đã nhận mã thiết bị, WiFi và cài đặt");
  } finally {
    stop = true;
    await reader.cancel().catch(() => {});
    reader.releaseLock();
    writer.releaseLock();
    await pump.catch(() => {});
    await port.close().catch(() => {});
  }
}

export function downloadBlob(data: Uint8Array, name: string) {
  const url = URL.createObjectURL(new Blob([data as BlobPart], { type: "application/octet-stream" }));
  const a = document.createElement("a");
  a.href = url;
  a.download = name;
  document.body.appendChild(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 60000);
}
