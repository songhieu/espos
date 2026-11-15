// Typed calls to the InkDesk backend routes (backend/routes.json).
import type { Api } from "./api";

export interface Book {
  id: number;
  title: string;
  author: string;
  file_name: string;
  size: number;
  status: "uploading" | "ready";
  created_at: string;
  on_device?: boolean;
  /** When the device had a full copy and the original was deleted from the server; "" before. */
  delivered_at?: string;
}

export interface Device {
  id: number;
  name: string;
  last_sync: string;
  battery: number | null;
  fw: string;
  fonts?: string[] | null;
  fw_code?: number;
  fw_req?: number; // version code the web asked it to install, 0 = none
  fw_auto?: boolean; // install new versions by itself while charging
  ota_state?: OtaState | null;
}

/** The last firmware update request or result for a device. */
export interface OtaState {
  state: "" | "requested" | "ok" | "failed";
  version?: string;
  why?: string;
  at?: string;
}

/** The newest firmware on the server. */
export interface FirmwareInfo {
  version: string;
  code: number;
  notes: string;
  released: string;
  size: number;
}

export interface StatsDay {
  d: string; // YYYY-MM-DD, the reader's local day
  s: number; // seconds read
  p: number; // pages turned
}
export interface StatsBook {
  key: string;
  title: string;
  seconds: number;
  pages: number;
  last: string;
  percent?: number;
}
export interface Stats {
  goal: number; // minutes per day
  today: string;
  days: StatsDay[];
  books: StatsBook[];
  streak: number;
  best: number;
  has_device: boolean;
}

/** "inkdesk-0.2.0" -> "0.2.0". */
export const shortVersion = (v: string) => (v.includes("-") ? v.slice(v.lastIndexOf("-") + 1) : v);

export interface SettingCell {
  v: string;
  source: "default" | "web" | "device";
  ms: number;
}

export interface Dash {
  date?: string;
  weekday?: string;
  lunar?: string;
  lunar_year?: string;
  weather?: {
    city: string;
    temp: number;
    desc: string;
    min?: number;
    max?: number;
    rain?: number;
    icon: string;
    hours?: { h: string; t: number; icon: string }[];
    days?: { d: string; min: number; max: number; icon: string }[];
  } | null;
  events?: { time: string; title: string }[];
  notes?: { title: string; time: string }[];
  word?: { w: string; m: string };
  layout?: string;
  gold?: MarketGold;
  stocks?: { at: string; items: Quote[] };
  fx?: { at: string; items: Rate[] };
  crypto?: { at: string; items: Coin[] };
  news?: { top?: string[] };
}
export interface Coin {
  s: string;
  p: number; // USD
  v: number; // VND
  pc: number; // 24 h change, %
}

export interface GoldQuote {
  c?: string; // PNJ code: SJC, N24K, KB…
  n: string;
  b: number;
  s: number;
  d?: number;
}
export interface MarketGold {
  src: string;
  at: string;
  items: GoldQuote[];
}
export interface Quote {
  s: string;
  p: number;
  c: number;
  pc: number;
}
export interface Rate {
  c: string;
  b: number;
  s: number;
}
/** Cached market data (GET /v1/markets), for the widget previews. */
export interface Markets {
  gold?: MarketGold;
  index?: { at: string; items: Record<string, Quote> };
  stk?: { at: string; items: Record<string, Quote> };
  fx?: { at: string; items: Rate[] };
  crypto?: { at: string; items: Record<string, Coin> };
}

/** One widget: [kind, x, y, w, h, style, option, flags] (settings home.w, sleep.wl, sleep.wp); flags "i" = inverted. */
export type LayoutItem = [string, number, number, number, number, string, string, string?];
export interface LayoutDoc {
  v: 1;
  f?: 0 | 1;
  /** Frame style: card (default), line, bare, bold, shadow. */
  k?: string;
  i: LayoutItem[];
}

export interface NewsPrefs {
  enabled: boolean;
  sections: string;
  hour: number;
  per_section: number;
  images: boolean;
  keep: number;
  last_date: string;
  last_note: string;
  saved: boolean;
}
export interface NewsIssue {
  id: number;
  title: string;
  size: number;
  progress: number | null;
  created_at: string;
  on_device?: boolean;
}

/** A picture for photo widgets (the original; renders per widget box are made on save). */
export interface Photo {
  id: number;
  thumb: string;
  w: number;
  h: number;
}
export interface Wallpaper {
  id: number;
  orient: "p" | "l";
  thumb: string;
  size: number;
}
export interface WifiSaved {
  ssid: string;
  ok: boolean;
  code: number;
  age_s: number;
}
export interface WifiOp {
  id: number;
  op: "add" | "remove";
  ssid: string;
  status: "pending" | "failed";
  result: string;
}

export const ink = (api: Api) => ({
  news: () => api.call<{ prefs: NewsPrefs; issues: NewsIssue[] }>("/v1/news"),
  saveNews: (p: NewsPrefs) =>
    api.call("/v1/news", { body: { enabled: p.enabled, sections: p.sections, hour: p.hour, per_section: p.per_section, images: p.images, keep: p.keep } }),
  makeNews: (nonce: string) => api.call<{ job_id: number }>("/v1/news/now", { body: { nonce } }),
  wallpapers: () => api.call<{ wallpapers: Wallpaper[] }>("/v1/wallpapers").then((r) => r.wallpapers),
  startWallUpload: (b: { orient: "p" | "l"; size: number; thumb: string }) => api.call<{ id: number; url: string }>("/v1/wallpapers/upload", { body: b }),
  confirmWall: (id: number) => api.call("/v1/wallpapers/confirm", { body: { id } }),
  deleteWall: (id: number) => api.call("/v1/wallpapers/delete", { body: { id } }),
  photos: () => api.call<{ photos: Photo[] }>("/v1/photos").then((r) => r.photos),
  startPhotoUpload: (b: { size: number; w: number; h: number; thumb: string }) => api.call<{ id: number; url: string }>("/v1/photos/upload", { body: b }),
  confirmPhoto: (id: number) => api.call("/v1/photos/confirm", { body: { id } }),
  deletePhoto: (id: number) => api.call("/v1/photos/delete", { body: { id } }),
  photoSource: (id: number) => api.call<{ url: string }>(`/v1/photos/${id}/source`).then((r) => r.url),
  startPhotoRender: (b: { id: number; dim: string; p: string; size: number }) => api.call<{ exists: boolean; url?: string }>("/v1/photos/render", { body: b }),
  wifi: (dev: number) => api.call<{ saved: WifiSaved[]; ops: WifiOp[] }>(`/v1/devices/${dev}/wifi`),
  saveWifi: (dev: number, b: { op: "add" | "remove" | "cancel"; ssid?: string; pass?: string; id?: number }) => api.call(`/v1/devices/${dev}/wifi`, { body: b }),
  books: () => api.call<{ books: Book[]; bytes: number }>("/v1/books"),
  startUpload: (f: { file_name: string; size: number; title?: string }) =>
    api.call<{ id: number; url: string }>("/v1/books/upload", { body: f }),
  confirmBook: (id: number) => api.call("/v1/books/confirm", { body: { id } }),
  deleteBook: (id: number) => api.call("/v1/books/delete", { body: { id } }),
  devices: () => api.call<{ devices: Device[] }>("/v1/devices").then((r) => r.devices),
  devicesWithFirmware: () => api.call<{ devices: Device[]; firmware: FirmwareInfo | null }>("/v1/devices"),
  firmware: (dev: number, b: { action: "update" | "cancel" } | { auto: boolean }) =>
    api.call<{ ok: boolean; ota_state?: OtaState }>(`/v1/devices/${dev}/firmware`, { body: b }),
  stats: () => api.call<Stats>("/v1/stats"),
  markets: () => api.call<Markets>("/v1/markets"),
  createDevice: (b: { name: string; token_hash: string; settings: Record<string, string> }) =>
    api.call<{ device: { id: number; name: string; user: string }; settings: Record<string, string>; settings_rev: number }>(
      "/v1/devices",
      { body: b },
    ),
  updateDevice: (b: { id: number; name?: string; revoked?: boolean }) => api.call("/v1/devices/update", { body: b }),
  settings: (id: number) =>
    api.call<{ device: Device; settings: Record<string, SettingCell> }>(`/v1/devices/${id}/settings`),
  saveSettings: (id: number, changes: Record<string, string>) =>
    api.call<{ ok: boolean; saved: Record<string, string> }>(`/v1/devices/${id}/settings`, { body: { changes } }),
  deviceDashboard: (id: number) => api.call<Dash>(`/v1/devices/${id}/dashboard`),
  confirmPair: (code: string, name: string) =>
    api.call<{ device: { id: number; name: string } }>("/v1/pair/confirm", { body: { code, name } }),
  saveNotifyKey: (key_hash: string) => api.call("/v1/notify-key", { body: { key_hash } }),
});
export type Ink = ReturnType<typeof ink>;

export function randomHex(bytes: number): string {
  const a = new Uint8Array(bytes);
  crypto.getRandomValues(a);
  return Array.from(a, (b) => b.toString(16).padStart(2, "0")).join("");
}

export async function sha256Hex(s: string): Promise<string> {
  const d = await crypto.subtle.digest("SHA-256", new TextEncoder().encode(s));
  return Array.from(new Uint8Array(d), (b) => b.toString(16).padStart(2, "0")).join("");
}

export function ago(iso: string): string {
  if (!iso) return "chưa đồng bộ";
  const t = Date.parse(iso);
  if (Number.isNaN(t)) return iso;
  const s = Math.round((Date.now() - t) / 1000);
  if (s < 60) return "vừa xong";
  if (s < 3600) return `${Math.round(s / 60)} phút trước`;
  if (s < 86400) return `${Math.round(s / 3600)} giờ trước`;
  return new Date(t).toLocaleDateString("vi-VN");
}

export function bytes(n: number): string {
  if (n < 1024) return `${n} B`;
  if (n < 1024 * 1024) return `${(n / 1024).toFixed(0)} KB`;
  return `${(n / 1024 / 1024).toFixed(1).replace(".", ",")} MB`;
}

export function dueLabel(d: string): string {
  if (!d || d.length < 10) return "";
  return `Hạn ${d.slice(8, 10)}/${d.slice(5, 7)}`;
}
