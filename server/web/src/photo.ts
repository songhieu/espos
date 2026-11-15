// Photo widget: the option string, rendering a picture for one widget box (1-bit BMP the device draws
// as it is) and the uploads. The original is kept on the server so a resized widget can be rendered again.
import type { Ink } from "./inkdesk";
import { toBmp } from "./wallpaper";

export type PhotoMode = "r" | "s" | "f"; // random, in turn, keep one
export type PhotoEvery = "v" | "h" | "d"; // each time the screen is drawn, every hour, every day
export type PhotoFit = "c" | "n"; // fill the box (crop), fit inside (white bars)
export type PhotoInk = "a" | "t"; // fine dots (Atkinson), bold (threshold)

export interface PhotoOpt {
  mode: PhotoMode;
  every: PhotoEvery;
  fit: PhotoFit;
  ink: PhotoInk;
  /** 0..99, 50 = as is */
  bright: number;
  ids: number[];
}

export const PHOTO_MAX = 12;
export const DEFAULT_PHOTO: PhotoOpt = { mode: "r", every: "h", fit: "c", ink: "a", bright: 50, ids: [] };

/** "rhca50|12,15" <-> PhotoOpt (the same text the server checks and the device reads). */
export function parsePhotoOpt(opt: string): PhotoOpt {
  const [head, list = ""] = (opt || "").split("|");
  if (!head || head.length !== 6) return { ...DEFAULT_PHOTO };
  const b = parseInt(head.slice(4), 10);
  return {
    mode: ("rsf".includes(head[0]) ? head[0] : "r") as PhotoMode,
    every: ("vhd".includes(head[1]) ? head[1] : "h") as PhotoEvery,
    fit: ("cn".includes(head[2]) ? head[2] : "c") as PhotoFit,
    ink: ("at".includes(head[3]) ? head[3] : "a") as PhotoInk,
    bright: Number.isFinite(b) ? Math.min(99, Math.max(0, b)) : 50,
    ids: list.split(",").map((x) => parseInt(x, 10)).filter((n) => n > 0).slice(0, PHOTO_MAX),
  };
}
export function formatPhotoOpt(o: PhotoOpt): string {
  return `${o.mode}${o.every}${o.fit}${o.ink}${String(o.bright).padStart(2, "0")}|${o.ids.join(",")}`;
}
/** The part of the option a render depends on: fit, ink, brightness ("ca50"). */
export const renderParams = (o: PhotoOpt) => `${o.fit}${o.ink}${String(o.bright).padStart(2, "0")}`;

type Src = CanvasImageSource & { width: number; height: number };

/** The picture in a w×h box as the device will show it: 1 = white, 0 = black per pixel. */
export function renderBits(img: Src, w: number, h: number, o: Pick<PhotoOpt, "fit" | "ink" | "bright">): { bits: Uint8Array; w: number; h: number } {
  const c = document.createElement("canvas");
  c.width = w;
  c.height = h;
  const g = c.getContext("2d", { willReadFrequently: true })!;
  g.fillStyle = "#fff";
  g.fillRect(0, 0, w, h);
  const s = o.fit === "c" ? Math.max(w / img.width, h / img.height) : Math.min(w / img.width, h / img.height);
  const dw = img.width * s, dh = img.height * s;
  g.imageSmoothingQuality = "high";
  g.drawImage(img, (w - dw) / 2, (h - dh) / 2, dw, dh);
  const data = g.getImageData(0, 0, w, h).data;
  const lum = new Float32Array(w * h);
  const hist = new Uint32Array(256);
  for (let i = 0, p = 0; i < lum.length; i++, p += 4) {
    const y = 0.2126 * data[p] + 0.7152 * data[p + 1] + 0.0722 * data[p + 2];
    lum[i] = y;
    hist[Math.min(255, Math.round(y))]++;
  }
  // Light auto-levels (photos look flat on e-ink), then the brightness the user chose.
  const pick = (q: number) => {
    let n = 0;
    for (let v = 0; v < 256; v++) {
      n += hist[v];
      if (n >= lum.length * q) return v;
    }
    return 255;
  };
  const lo = pick(0.01);
  const hi = Math.max(lo + 1, pick(0.99));
  const shift = (o.bright - 50) * 2.4;
  for (let i = 0; i < lum.length; i++) lum[i] = Math.min(255, Math.max(0, ((lum[i] - lo) * 255) / (hi - lo) + shift));
  const bits = new Uint8Array(w * h);
  if (o.ink === "t") {
    for (let i = 0; i < lum.length; i++) bits[i] = lum[i] >= 128 ? 1 : 0;
    return { bits, w, h };
  }
  const spread = [[1, 0], [2, 0], [-1, 1], [0, 1], [1, 1], [0, 2]];
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      const i = y * w + x;
      const old = lum[i];
      const v = old < 128 ? 0 : 255;
      bits[i] = v ? 1 : 0;
      const err = (old - v) / 8;
      for (const [dx, dy] of spread) {
        const nx = x + dx, ny = y + dy;
        if (nx >= 0 && nx < w && ny < h) lum[ny * w + nx] += err;
      }
    }
  }
  return { bits, w, h };
}

/** A data URL of the rendered bits (paper and ink colours), for the preview. */
export function bitsDataUrl(d: { bits: Uint8Array; w: number; h: number }): string {
  const c = document.createElement("canvas");
  c.width = d.w;
  c.height = d.h;
  const g = c.getContext("2d")!;
  const im = g.createImageData(d.w, d.h);
  for (let i = 0, p = 0; i < d.bits.length; i++, p += 4) {
    const v = d.bits[i] ? 238 : 17;
    im.data[p] = v;
    im.data[p + 1] = v - (d.bits[i] ? 1 : 0);
    im.data[p + 2] = v - (d.bits[i] ? 6 : 0);
    im.data[p + 3] = 255;
  }
  g.putImageData(im, 0, 0);
  return c.toDataURL("image/png");
}

function loadUrl(url: string): Promise<HTMLImageElement> {
  return new Promise((resolve, reject) => {
    const img = new Image();
    img.crossOrigin = "anonymous";
    img.onload = () => resolve(img);
    img.onerror = () => reject(new Error("Không tải được ảnh gốc"));
    img.src = url;
  });
}

function put(url: string, blob: Blob, type: string): Promise<void> {
  return fetch(url, { method: "PUT", headers: { "Content-Type": type }, body: blob }).then((r) => {
    if (!r.ok) throw new Error(`Tải lên lỗi ${r.status}`);
  });
}

/** Uploads a new picture: shrunk to at most 1600 px (JPEG) with a small thumbnail for the editor. */
export async function uploadPhoto(ink: Ink, file: File): Promise<void> {
  const url = URL.createObjectURL(file);
  try {
    const img = await loadUrl(url);
    const s = Math.min(1, 1600 / Math.max(img.width, img.height));
    const c = document.createElement("canvas");
    c.width = Math.round(img.width * s);
    c.height = Math.round(img.height * s);
    const g = c.getContext("2d")!;
    g.imageSmoothingQuality = "high";
    g.drawImage(img, 0, 0, c.width, c.height);
    const blob = await new Promise<Blob>((res, rej) => c.toBlob((b) => (b ? res(b) : rej(new Error("Không chuyển được ảnh"))), "image/jpeg", 0.85));
    const t = document.createElement("canvas");
    const ts = 120 / Math.max(c.width, c.height);
    t.width = Math.max(1, Math.round(c.width * ts));
    t.height = Math.max(1, Math.round(c.height * ts));
    const tg = t.getContext("2d")!;
    tg.imageSmoothingQuality = "high";
    tg.drawImage(c, 0, 0, t.width, t.height);
    const thumb = t.toDataURL("image/jpeg", 0.8);
    const start = await ink.startPhotoUpload({ size: blob.size, w: c.width, h: c.height, thumb });
    await put(start.url, blob, "image/jpeg");
    await ink.confirmPhoto(start.id);
  } finally {
    URL.revokeObjectURL(url);
  }
}

const originals = new Map<number, Promise<HTMLImageElement>>();
/** The original of a picture (cached for this page). */
export function original(ink: Ink, id: number): Promise<HTMLImageElement> {
  let p = originals.get(id);
  if (!p) {
    p = ink.photoSource(id).then(loadUrl);
    p.catch(() => originals.delete(id));
    originals.set(id, p);
  }
  return p;
}

/** Makes sure the device can download every picture of a photo widget at this box size. */
export async function ensureRenders(ink: Ink, o: PhotoOpt, w: number, h: number, progress?: () => void): Promise<void> {
  const dim = `${w}x${h}`;
  const p = renderParams(o);
  for (const id of o.ids) {
    const img = await original(ink, id);
    const blob = toBmp(renderBits(img, w, h, o));
    const r = await ink.startPhotoRender({ id, dim, p, size: blob.size });
    if (!r.exists && r.url) await put(r.url, blob, "image/bmp");
    progress?.();
  }
}
