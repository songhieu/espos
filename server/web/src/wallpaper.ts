// Turns a photo into an X4 sleep-screen picture in the browser: crop to the panel size,
// dither to pure black and white (what the e-ink panel shows), and encode a 1-bit BMP the
// firmware draws without further processing. Also makes the small PNG thumbnail the web lists.

export type Orient = "p" | "l";
export type Fit = "cover" | "contain";

export interface Crop {
  orient: Orient;
  fit: Fit;
  /** Pan offset as a fraction of the free space (-1..1), used when the photo overflows the frame. */
  panX: number;
  panY: number;
}

export const panelSize = (o: Orient) => (o === "p" ? { w: 480, h: 800 } : { w: 800, h: 480 });

/** Draws the photo into a w×h canvas following the crop, white background. */
function place(img: CanvasImageSource & { width: number; height: number }, crop: Crop, w: number, h: number) {
  const c = document.createElement("canvas");
  c.width = w;
  c.height = h;
  const g = c.getContext("2d", { willReadFrequently: true })!;
  g.fillStyle = "#fff";
  g.fillRect(0, 0, w, h);
  const s = crop.fit === "cover" ? Math.max(w / img.width, h / img.height) : Math.min(w / img.width, h / img.height);
  const dw = img.width * s;
  const dh = img.height * s;
  const x = (w - dw) / 2 + ((w - dw) / 2) * -crop.panX * (crop.fit === "cover" ? 1 : 0);
  const y = (h - dh) / 2 + ((h - dh) / 2) * -crop.panY * (crop.fit === "cover" ? 1 : 0);
  g.imageSmoothingQuality = "high";
  g.drawImage(img, x, y, dw, dh);
  return g.getImageData(0, 0, w, h);
}

/** Grayscale with a light auto-levels, then Atkinson dithering. Returns 1 = white, 0 = black per pixel. */
export function dither(img: CanvasImageSource & { width: number; height: number }, crop: Crop): { bits: Uint8Array; w: number; h: number } {
  const { w, h } = panelSize(crop.orient);
  const data = place(img, crop, w, h).data;
  const lum = new Float32Array(w * h);
  const hist = new Uint32Array(256);
  for (let i = 0, p = 0; i < lum.length; i++, p += 4) {
    const y = 0.2126 * data[p] + 0.7152 * data[p + 1] + 0.0722 * data[p + 2];
    lum[i] = y;
    hist[Math.min(255, Math.round(y))]++;
  }
  // Stretch the 1st..99th percentile to full range: photos look flat on e-ink otherwise.
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
  for (let i = 0; i < lum.length; i++) lum[i] = Math.min(255, Math.max(0, ((lum[i] - lo) * 255) / (hi - lo)));
  const bits = new Uint8Array(w * h);
  const spread = [
    [1, 0],
    [2, 0],
    [-1, 1],
    [0, 1],
    [1, 1],
    [0, 2],
  ];
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      const i = y * w + x;
      const old = lum[i];
      const v = old < 128 ? 0 : 255;
      bits[i] = v ? 1 : 0;
      const err = (old - v) / 8;
      for (const [dx, dy] of spread) {
        const nx = x + dx;
        const ny = y + dy;
        if (nx >= 0 && nx < w && ny < h) lum[ny * w + nx] += err;
      }
    }
  }
  return { bits, w, h };
}

/** 1-bit BMP (bottom-up rows, 2-colour palette: 0 black, 1 white). */
export function toBmp({ bits, w, h }: { bits: Uint8Array; w: number; h: number }): Blob {
  const rowBytes = Math.ceil(w / 32) * 4;
  const pixelBytes = rowBytes * h;
  const offset = 14 + 40 + 8;
  const buf = new ArrayBuffer(offset + pixelBytes);
  const v = new DataView(buf);
  v.setUint8(0, 0x42);
  v.setUint8(1, 0x4d);
  v.setUint32(2, buf.byteLength, true);
  v.setUint32(10, offset, true);
  v.setUint32(14, 40, true);
  v.setInt32(18, w, true);
  v.setInt32(22, h, true);
  v.setUint16(26, 1, true);
  v.setUint16(28, 1, true);
  v.setUint32(30, 0, true);
  v.setUint32(34, pixelBytes, true);
  v.setInt32(38, 5906, true); // 150 dpi
  v.setInt32(42, 5906, true);
  v.setUint32(46, 2, true);
  v.setUint32(50, 2, true);
  v.setUint32(54, 0x00000000, true); // black
  v.setUint32(58, 0x00ffffff, true); // white
  const px = new Uint8Array(buf, offset);
  for (let y = 0; y < h; y++) {
    const row = (h - 1 - y) * rowBytes;
    for (let x = 0; x < w; x++) {
      if (bits[y * w + x]) px[row + (x >> 3)] |= 0x80 >> (x & 7);
    }
  }
  return new Blob([buf], { type: "image/bmp" });
}

/** The dithered picture drawn into a canvas at `scale`, e.g. for the preview. */
export function drawBits(canvas: HTMLCanvasElement, { bits, w, h }: { bits: Uint8Array; w: number; h: number }) {
  canvas.width = w;
  canvas.height = h;
  const g = canvas.getContext("2d")!;
  const im = g.createImageData(w, h);
  for (let i = 0, p = 0; i < bits.length; i++, p += 4) {
    const c = bits[i] ? 238 : 17; // paper and ink, like the panel
    im.data[p] = c;
    im.data[p + 1] = c - (bits[i] ? 1 : 0);
    im.data[p + 2] = c - (bits[i] ? 6 : 0);
    im.data[p + 3] = 255;
  }
  g.putImageData(im, 0, 0);
}

/** Small PNG of the dithered picture for the web list (kept in the table). */
export function thumbDataUrl(d: { bits: Uint8Array; w: number; h: number }): string {
  const full = document.createElement("canvas");
  drawBits(full, d);
  const t = document.createElement("canvas");
  const s = 160 / Math.max(d.w, d.h);
  t.width = Math.round(d.w * s);
  t.height = Math.round(d.h * s);
  const g = t.getContext("2d")!;
  g.imageSmoothingQuality = "high";
  g.drawImage(full, 0, 0, t.width, t.height);
  return t.toDataURL("image/png");
}

export function loadImage(file: File): Promise<HTMLImageElement> {
  return new Promise((resolve, reject) => {
    const url = URL.createObjectURL(file);
    const img = new Image();
    img.onload = () => resolve(img);
    img.onerror = () => {
      URL.revokeObjectURL(url);
      reject(new Error("Không đọc được ảnh này. Thử JPG hoặc PNG."));
    };
    img.src = url;
  });
}
