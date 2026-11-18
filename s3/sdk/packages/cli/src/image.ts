// Pictures: reading a JPEG's frame header (the device draws baseline JPEGs only), writing baseline JPEGs (jpeg-js),
// reading PNGs (pngjs) and a few drawing helpers for icons.
import jpeg from "jpeg-js";
import { PNG } from "pngjs";

export interface JpegInfo {
  width: number;
  height: number;
  baseline: boolean;
  sof: number;
}

/** The frame header of a JPEG (publish_pkg.py's reader: skips standalone markers, knows every SOF). Throws. */
export function jpegInfo(b: Uint8Array): JpegInfo {
  if (b[0] !== 0xff || b[1] !== 0xd8) throw new Error("not a JPEG");
  let i = 2;
  while (i + 4 <= b.length) {
    if (b[i] !== 0xff) throw new Error("broken JPEG markers");
    const m = b[i + 1];
    if (m === 0xd8 || m === 0x01 || (m >= 0xd0 && m <= 0xd7) || m === 0xff) {
      i += m === 0xff ? 1 : 2; // fill bytes and standalone markers
      continue;
    }
    const len = (b[i + 2] << 8) | b[i + 3];
    if (m >= 0xc0 && m <= 0xcf && m !== 0xc4 && m !== 0xc8 && m !== 0xcc) {
      if (i + 9 > b.length) break;
      const h = (b[i + 5] << 8) | b[i + 6],
        w = (b[i + 7] << 8) | b[i + 8];
      return { width: w, height: h, baseline: m === 0xc0, sof: m };
    }
    i += 2 + len;
  }
  throw new Error("JPEG without a frame header");
}

export class Canvas {
  readonly data: Uint8Array; // RGBA
  constructor(readonly w: number, readonly h: number) {
    this.data = new Uint8Array(w * h * 4);
  }
  /** Source-over blend of one pixel. */
  blend(x: number, y: number, r: number, g: number, b: number, a = 255): void {
    if (x < 0 || y < 0 || x >= this.w || y >= this.h || a <= 0) return;
    const i = (y * this.w + x) * 4;
    const d = this.data;
    const k = a / 255;
    d[i] = Math.round(r * k + d[i] * (1 - k));
    d[i + 1] = Math.round(g * k + d[i + 1] * (1 - k));
    d[i + 2] = Math.round(b * k + d[i + 2] * (1 - k));
    d[i + 3] = 255;
  }
  /** Vertical gradient from top to bottom colour. */
  gradient(top: [number, number, number], bottom: [number, number, number], diagonal = false): void {
    for (let y = 0; y < this.h; y++)
      for (let x = 0; x < this.w; x++) {
        const t = diagonal ? (x + y) / (this.w + this.h - 2) : y / (this.h - 1);
        this.blend(x, y, top[0] + (bottom[0] - top[0]) * t, top[1] + (bottom[1] - top[1]) * t, top[2] + (bottom[2] - top[2]) * t);
      }
  }
  /** Draws an RGBA picture scaled by an integer factor (nearest neighbour), with alpha. */
  drawScaled(src: { width: number; height: number; data: Uint8Array }, x0: number, y0: number, s: number): void {
    for (let y = 0; y < src.height * s; y++)
      for (let x = 0; x < src.width * s; x++) {
        const i = (Math.floor(y / s) * src.width + Math.floor(x / s)) * 4;
        this.blend(x0 + x, y0 + y, src.data[i], src.data[i + 1], src.data[i + 2], src.data[i + 3]);
      }
  }
  /** A filled shape given by an inside test on pixel centres, 4x4 supersampled. */
  fill(inside: (x: number, y: number) => boolean, rgb: [number, number, number], alpha = 255): void {
    for (let y = 0; y < this.h; y++)
      for (let x = 0; x < this.w; x++) {
        let n = 0;
        for (let sy = 0; sy < 4; sy++) for (let sx = 0; sx < 4; sx++) if (inside(x + (sx + 0.5) / 4, y + (sy + 0.5) / 4)) n++;
        if (n) this.blend(x, y, rgb[0], rgb[1], rgb[2], (alpha * n) / 16);
      }
  }
  jpeg(quality = 92): Buffer {
    return jpeg.encode({ data: this.data, width: this.w, height: this.h }, quality).data;
  }
}

/** Area-average downscale of an RGBA picture to fit in max x max (keeps the aspect). */
export function fitDown(src: { width: number; height: number; data: Uint8Array }, max: number): { width: number; height: number; data: Uint8Array } {
  const k = Math.min(1, max / Math.max(src.width, src.height));
  if (k >= 1) return src;
  const w = Math.max(1, Math.round(src.width * k)),
    h = Math.max(1, Math.round(src.height * k));
  const out = new Uint8Array(w * h * 4);
  for (let y = 0; y < h; y++)
    for (let x = 0; x < w; x++) {
      const x0 = Math.floor((x * src.width) / w), x1 = Math.max(x0 + 1, Math.floor(((x + 1) * src.width) / w));
      const y0 = Math.floor((y * src.height) / h), y1 = Math.max(y0 + 1, Math.floor(((y + 1) * src.height) / h));
      let r = 0, g = 0, b = 0, a = 0, n = 0;
      for (let yy = y0; yy < y1; yy++)
        for (let xx = x0; xx < x1; xx++) {
          const i = (yy * src.width + xx) * 4;
          const al = src.data[i + 3];
          r += src.data[i] * al, g += src.data[i + 1] * al, b += src.data[i + 2] * al, a += al, n++;
        }
      const o = (y * w + x) * 4;
      out[o] = a ? r / a : 0, out[o + 1] = a ? g / a : 0, out[o + 2] = a ? b / a : 0, out[o + 3] = a / n;
    }
  return { width: w, height: h, data: out };
}

export function decodePng(b: Uint8Array): { width: number; height: number; data: Uint8Array } {
  const p = PNG.sync.read(Buffer.from(b));
  return { width: p.width, height: p.height, data: new Uint8Array(p.data) };
}

export function encodePng(w: number, h: number, rgba: Uint8Array): Buffer {
  const p = new PNG({ width: w, height: h });
  p.data = Buffer.from(rgba);
  return PNG.sync.write(p);
}

/** A PPM (P6) file as RGBA. */
export function decodePpm(b: Buffer): { width: number; height: number; data: Uint8Array } {
  const m = /^P6\s+(\d+)\s+(\d+)\s+(\d+)\s/.exec(b.toString("latin1", 0, 64));
  if (!m) throw new Error("not a P6 PPM");
  const w = +m[1], h = +m[2];
  const px = b.subarray(m[0].length);
  const out = new Uint8Array(w * h * 4);
  for (let i = 0; i < w * h; i++) out[i * 4] = px[i * 3], out[i * 4 + 1] = px[i * 3 + 1], out[i * 4 + 2] = px[i * 3 + 2], out[i * 4 + 3] = 255;
  return { width: w, height: h, data: out };
}

export const hexRgb = (h: string): [number, number, number] => [parseInt(h.slice(1, 3), 16), parseInt(h.slice(3, 5), 16), parseInt(h.slice(5, 7), 16)];
export const rgbHex = (c: number[]) => "#" + c.map((v) => Math.round(Math.max(0, Math.min(255, v))).toString(16).padStart(2, "0")).join("");

/** A 120x120 placeholder icon: the manifest's two colours as a gradient and a white mark. */
export function placeholderIcon(bg: [string, string], mark: "circle" | "play" | "grid" = "circle"): Buffer {
  const c = new Canvas(120, 120);
  c.gradient(hexRgb(bg[0]), hexRgb(bg[1]), true);
  const white: [number, number, number] = [255, 255, 255];
  if (mark === "play") c.fill((x, y) => x >= 46 && x - 46 <= 1.6 * Math.min(y - 36, 84 - y) && y >= 36 && y <= 84, white, 235);
  else if (mark === "grid")
    c.fill((x, y) => {
      const gx = (x - 34) % 28, gy = (y - 34) % 28;
      return x >= 34 && y >= 34 && x < 90 && y < 90 && gx < 22 && gy < 22;
    }, white, 235);
  else c.fill((x, y) => (x - 60) ** 2 + (y - 60) ** 2 <= 26 ** 2 && (x - 60) ** 2 + (y - 60) ** 2 >= 16 ** 2, white, 235);
  return c.jpeg(92);
}
