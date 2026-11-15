// A line-based session with InkDesk firmware over Web Serial: hello, WiFi scan/test, provisioning.
// Opening the port can reset the ESP32-C3, so every session starts by waiting for INKDESK:READY.
import { ESPRESSIF_VID } from "esptool-js";

const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

export interface WifiNet {
  ssid: string;
  rssi: number;
}

export class DeviceSession {
  private reader: ReadableStreamDefaultReader<Uint8Array> | null = null;
  private writer: WritableStreamDefaultWriter<Uint8Array> | null = null;
  private lines: string[] = [];
  private buf = "";
  private stop = false;
  /** A write never completed: the device does not read the port (stock firmware). */
  private stalled = false;
  private pump: Promise<void> | null = null;
  private constructor(private port: SerialPort) {}

  /** Finds the granted Espressif port again (it re-enumerates after a reset) and opens it. */
  static async open(hint: SerialPort | null): Promise<DeviceSession> {
    const until = Date.now() + 25000;
    let port: SerialPort | null = null;
    while (!port && Date.now() < until) {
      const ports = await navigator.serial.getPorts();
      port = (hint && ports.includes(hint) ? hint : null) ?? ports.find((p) => p.getInfo().usbVendorId === ESPRESSIF_VID) ?? null;
      if (!port) await sleep(500);
    }
    if (!port) throw new Error("Không thấy máy. Rút cáp, cắm lại rồi thử lại.");
    try {
      await port.open({ baudRate: 115200 });
    } catch (e) {
      if (!/already open/i.test((e as Error).message)) throw e;
    }
    await port.setSignals({ dataTerminalReady: false, requestToSend: false }).catch(() => {});
    const s = new DeviceSession(port);
    s.writer = port.writable!.getWriter();
    s.reader = port.readable!.getReader();
    const dec = new TextDecoder();
    s.pump = (async () => {
      while (!s.stop) {
        const { value, done } = await s.reader!.read().catch(() => ({ value: undefined, done: true }));
        if (done) break;
        if (!value) continue;
        s.buf += dec.decode(value, { stream: true });
        let i: number;
        while ((i = s.buf.indexOf("\n")) >= 0) {
          s.lines.push(s.buf.slice(0, i).replace(/\r$/, ""));
          s.buf = s.buf.slice(i + 1);
          if (s.lines.length > 400) s.lines.splice(0, 200);
        }
      }
    })();
    return s;
  }

  /** Writes one line. Stock firmware never reads the port, so a write can hang: give up after `ms`. */
  async send(line: string, ms = 2500) {
    if (this.stalled) throw new Error("Máy không nhận lệnh qua cáp.");
    const done = this.writer!.write(new TextEncoder().encode(line + "\n")).then(
      () => true,
      () => false,
    );
    const ok = await Promise.race([done, sleep(ms).then(() => false)]);
    if (!ok) {
      this.stalled = true;
      throw new Error("Máy không nhận lệnh qua cáp.");
    }
  }

  /** Waits for a line starting with one of `prefixes`; optionally re-sends `resend()` every 1.5 s. */
  async waitFor(prefixes: string[], ms: number, resend?: () => string): Promise<string | null> {
    const until = Date.now() + ms;
    let next = 0;
    while (Date.now() < until) {
      const idx = this.lines.findIndex((l) => prefixes.some((p) => l.startsWith(p)));
      if (idx >= 0) return this.lines.splice(0, idx + 1).pop() ?? null;
      if (resend && Date.now() >= next) {
        try {
          await this.send(resend());
        } catch {
          return null; // the device does not read: it will not answer either
        }
        next = Date.now() + 1500;
      }
      await sleep(120);
    }
    return null;
  }

  /** Waits until the InkDesk firmware answers (it may be booting after the port opened). */
  async hello(ms = 40000): Promise<string> {
    // Every HELLO carries our clock (the X4 has no RTC).
    const ready = await this.waitFor(["INKDESK:READY"], ms, () => "INKDESK:HELLO " + Date.now());
    if (!ready) throw new Error("Máy không trả lời. Bật màn hình máy (bấm nút nguồn) rồi thử lại.");
    this.lines.length = 0;
    return ready;
  }

  async scanWifi(): Promise<WifiNet[]> {
    this.lines.length = 0;
    await this.send("CMD:WIFISCAN");
    const until = Date.now() + 25000;
    const found = new Map<string, number>();
    while (Date.now() < until) {
      const end = this.lines.findIndex((l) => l.startsWith("WIFISCAN_END"));
      for (const l of this.lines.splice(0, end >= 0 ? end + 1 : this.lines.length)) {
        const m = /^WIFI (-?\d+) (.+)$/.exec(l);
        if (m && m[2].trim()) {
          const rssi = Number(m[1]);
          const ssid = m[2];
          if (!found.has(ssid) || (found.get(ssid) ?? -999) < rssi) found.set(ssid, rssi);
        }
      }
      if (end >= 0) break;
      await sleep(150);
    }
    return [...found.entries()].map(([ssid, rssi]) => ({ ssid, rssi })).sort((a, b) => b.rssi - a.rssi);
  }

  /** Tries the network on the device. Returns null on success, else a human reason. */
  async testWifi(ssid: string, pass: string): Promise<string | null> {
    this.lines.length = 0;
    await this.send(`CMD:WIFITEST ${ssid}\t${pass}`);
    const ans = await this.waitFor(["WIFITEST_OK", "WIFITEST_FAIL"], 32000);
    if (!ans) return "Máy không trả lời khi thử WiFi.";
    if (ans.startsWith("WIFITEST_OK")) return null;
    const st = /status=(\d+)/.exec(ans)?.[1];
    if (st === "1") return "Máy không thấy mạng này (có thể là mạng 5 GHz hoặc ở quá xa).";
    if (st === "4") return "Sai mật khẩu WiFi.";
    return "Không vào được mạng: sai mật khẩu, hoặc mạng cần đăng nhập qua trang web (WiFi quán).";
  }

  /** Saves a network on the device (added to its list, never replacing it). */
  async saveWifi(ssid: string, pass: string): Promise<boolean> {
    this.lines.length = 0;
    await this.send(`CMD:WIFIADD ${ssid}\t${pass}`);
    const ans = await this.waitFor(["WIFIADD_OK", "WIFIADD_ERR"], 8000);
    return ans?.startsWith("WIFIADD_OK") ?? false;
  }

  async provision(payload: unknown): Promise<void> {
    this.lines.length = 0;
    await this.send("INKDESK:PROVISION " + JSON.stringify({ ...(payload as object), now_ms: Date.now() }));
    const ans = await this.waitFor(["INKDESK:OK", "INKDESK:ERR"], 20000);
    if (!ans) throw new Error("Máy không xác nhận đã nhận cài đặt.");
    if (ans.startsWith("INKDESK:ERR")) throw new Error("Máy báo lỗi: " + ans.slice(11).trim());
  }

  /** Releases the port even when a write is stuck, so the flasher can open it next. */
  async close() {
    this.stop = true;
    const quick = <T>(p: Promise<T> | undefined, ms = 800) => Promise.race([p?.catch(() => undefined), sleep(ms)]);
    await quick(this.reader?.cancel());
    try {
      this.reader?.releaseLock();
    } catch {
      /* already released */
    }
    if (this.stalled) await quick(this.writer?.abort());
    try {
      this.writer?.releaseLock();
    } catch {
      /* a pending write keeps the lock until abort settles */
    }
    await quick(this.pump ?? undefined);
    await quick(this.port.close(), 2000);
  }
}
