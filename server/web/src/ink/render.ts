/**
 * A tiny copy of the X4 firmware's renderer (lib/GfxRenderer) that draws into SVG, so the web previews
 * are laid out exactly as the device draws them: the same fonts (metrics extracted from the firmware's
 * font headers into inkfonts.json by firmware/scripts/… see fontmetrics.py), the same text measuring,
 * the same shapes. Coordinates are device pixels.
 */
import FONTS from "../inkfonts.json";

type FontJson = { f: string; w: number; s: string; em: number; lh: number; asc: number; c: string; a: number[] };
const RAW = FONTS as unknown as Record<string, FontJson>;

interface Face {
  family: string;
  weight: number;
  style: string;
  em: number;
  lh: number;
  asc: number;
  glyphs: Map<string, [number, number, number]>; // advance (12.4 fixed point), left, width
}
const faces = new Map<string, Face>();
for (const [k, v] of Object.entries(RAW)) {
  const glyphs = new Map<string, [number, number, number]>();
  const chars = Array.from(v.c);
  chars.forEach((ch, i) => glyphs.set(ch, [v.a[i * 3], v.a[i * 3 + 1], v.a[i * 3 + 2]]));
  faces.set(k, { family: v.f, weight: v.w, style: v.s, em: v.em, lh: v.lh, asc: v.asc, glyphs });
}

/** Font ids (the firmware's NOTOSANS_14_FONT_ID …). */
export const N12 = "n12", N14 = "n14", N16 = "n16", N18 = "n18", U10 = "u10", U12 = "u12", U8 = "u8";
export const I24 = "i24", I30 = "i30", I44 = "i44", I60 = "i60", S36 = "s36";
export type FontId = string;
export const CAPS_FONT = U10, SMALL_FONT = N12, BODY_FONT = N14, LIST_FONT = N12, SMALL_FONT_ID = U8;
export enum St { Reg = 0, Bold = 1, Italic = 2 }
export const kReg = St.Reg, kBold = St.Bold;

function face(font: FontId, style: St): Face {
  const suffix = style === St.Bold ? "b" : style === St.Italic ? "i" : "";
  return faces.get(font + suffix) ?? faces.get(font) ?? faces.get(N12)!;
}

const toPixel = (fp: number) => (fp + 8) >> 4;

/** Glyph pen positions of `text` (relative to x) and its width, as EpdFont::getTextBounds. */
function layout(font: FontId, text: string, style: St) {
  const f = face(font, style);
  const xs: number[] = [];
  const chars: string[] = [];
  let pen = 0, prevAdv = 0, started = false, minX = 0, maxX = 0;
  for (const ch of text.normalize("NFC")) {
    const g = f.glyphs.get(ch);
    if (!g) continue;
    if (started) pen += toPixel(prevAdv);
    started = true;
    xs.push(pen);
    chars.push(ch);
    minX = Math.min(minX, pen + g[1]);
    maxX = Math.max(maxX, pen + g[1] + g[2]);
    prevAdv = g[0];
  }
  return { xs, chars, width: maxX - minX, f };
}

export const textWidth = (font: FontId, text: string, style: St = St.Reg) => (text ? layout(font, text, style).width : 0);
export const lineHeight = (font: FontId) => face(font, St.Reg).lh;
export const ascender = (font: FontId) => face(font, St.Reg).asc;

const esc = (s: string) => s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
export const INK = "#141414", PAPER = "#eeede8";

/** Collects SVG for one screen or widget. */
export class Renderer {
  parts: string[] = [];
  private col = (black: boolean) => (black ? INK : PAPER);

  drawText(font: FontId, x: number, y: number, text: string, black = true, style: St = St.Reg) {
    if (!text) return;
    const { xs, chars, f } = layout(font, text, style);
    if (!chars.length) return;
    const base = y + f.asc;
    this.parts.push(
      `<text x="${xs.map((v) => v + x).join(" ")}" y="${base}" font-family="${f.family === "Ubuntu" ? "'Ubuntu','EspOSUbuntuVi'" : `'${f.family}'`}" font-size="${f.em}" font-weight="${f.weight}"` +
        `${f.style === "italic" ? ' font-style="italic"' : ""} fill="${this.col(black)}">${esc(chars.join(""))}</text>`,
    );
  }
  fillRect(x: number, y: number, w: number, h: number, black = true) {
    if (w <= 0 || h <= 0) return;
    this.parts.push(`<rect x="${x}" y="${y}" width="${w}" height="${h}" fill="${this.col(black)}"/>`);
  }
  drawRect(x: number, y: number, w: number, h: number, lw = 1, black = true) {
    if (w <= 0 || h <= 0) return;
    const s = Math.min(lw, w / 2, h / 2);
    this.parts.push(`<rect x="${x + s / 2}" y="${y + s / 2}" width="${w - s}" height="${h - s}" fill="none" stroke="${this.col(black)}" stroke-width="${s}"/>`);
  }
  drawRoundedRect(x: number, y: number, w: number, h: number, lw: number, rad: number, black = true) {
    if (w <= 0 || h <= 0 || lw <= 0) return;
    const r = Math.max(0, Math.min(rad, w / 2, h / 2));
    const s = Math.min(lw, Math.max(1, r || lw));
    this.parts.push(
      `<rect x="${x + s / 2}" y="${y + s / 2}" width="${w - s}" height="${h - s}" rx="${Math.max(0, r - s / 2)}" fill="none" stroke="${this.col(black)}" stroke-width="${s}"/>`,
    );
  }
  fillRoundedRect(x: number, y: number, w: number, h: number, rad: number, black = true) {
    if (w <= 0 || h <= 0) return;
    const r = Math.max(0, Math.min(rad, w / 2, h / 2));
    this.parts.push(`<rect x="${x}" y="${y}" width="${w}" height="${h}" rx="${r}" fill="${this.col(black)}"/>`);
  }
  /** A line `lw` px thick (the firmware stacks lw one-pixel lines downwards). */
  drawLine(x1: number, y1: number, x2: number, y2: number, lw = 1, black = true) {
    const o = (lw - 1) / 2;
    this.parts.push(
      `<line x1="${x1 + 0.5}" y1="${y1 + 0.5 + o}" x2="${x2 + 0.5}" y2="${y2 + 0.5 + o}" stroke="${this.col(black)}" stroke-width="${lw}" stroke-linecap="round"/>`,
    );
  }
  /** A line `lw` px thick, centred on its path (the widgets' thickLine). */
  thickLine(x1: number, y1: number, x2: number, y2: number, lw: number, black = true) {
    this.parts.push(`<line x1="${x1 + 0.5}" y1="${y1 + 0.5}" x2="${x2 + 0.5}" y2="${y2 + 0.5}" stroke="${this.col(black)}" stroke-width="${lw}"/>`);
  }
  fillPolygon(xs: number[], ys: number[], black = true) {
    this.parts.push(`<polygon points="${xs.map((x, i) => `${x},${ys[i]}`).join(" ")}" fill="${this.col(black)}"/>`);
  }
  fillCircle(cx: number, cy: number, rad: number, black = true) {
    if (rad <= 0) return;
    this.parts.push(`<circle cx="${cx}" cy="${cy}" r="${rad}" fill="${this.col(black)}"/>`);
  }
  /** A picture (cover, photo) filling the box, cropped. */
  image(x: number, y: number, w: number, h: number, href: string, contain = false) {
    this.parts.push(
      `<image href="${esc(href)}" x="${x}" y="${y}" width="${w}" height="${h}" preserveAspectRatio="${contain ? "xMidYMid meet" : "xMidYMid slice"}" style="filter:grayscale(1) contrast(1.15)"/>`,
    );
  }
  /** A 24x24 stroke icon path scaled to `size` at (x, y). */
  icon(path: string, x: number, y: number, size: number, black = true) {
    this.parts.push(
      `<g transform="translate(${x} ${y}) scale(${size / 24})"><path d="${path}" fill="none" stroke="${this.col(black)}" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"/></g>`,
    );
  }
  raw(svg: string) {
    this.parts.push(svg);
  }
  group(fn: () => void, attrs: string) {
    this.parts.push(`<g ${attrs}>`);
    fn();
    this.parts.push("</g>");
  }
  svg(w: number, h: number, bg = true) {
    return (
      `<svg xmlns="http://www.w3.org/2000/svg" width="${w}" height="${h}" viewBox="0 0 ${w} ${h}" text-rendering="geometricPrecision">` +
      (bg ? `<rect width="${w}" height="${h}" fill="${PAPER}"/>` : "") +
      this.parts.join("") +
      "</svg>"
    );
  }
}

// ---- EspOSUi text helpers (src/espos/EspOSUi.cpp) -------------------------------------------------

export function smallerFont(font: FontId): FontId | 0 {
  switch (font) {
    case S36: return I24;
    case I24: return N18;
    case N18: return N16;
    case N16: return N14;
    case N14: return N12;
    case N12: case U12: return U10;
    case U10: return U8;
    default: return 0;
  }
}

export function fitFont(font: FontId, text: string, maxW: number, style: St = St.Reg): FontId {
  if (!text) return font;
  let f = font;
  while (textWidth(f, text, style) > maxW) {
    const next = smallerFont(f);
    if (!next) break;
    f = next;
  }
  return f;
}

function words(text: string): string[] {
  const out: string[] = [];
  let cur = "";
  for (const ch of text) {
    if (ch === " " || ch === "\n") {
      if (cur) out.push(cur);
      cur = "";
      if (ch === "\n") out.push("\n");
    } else cur += ch;
  }
  if (cur) out.push(cur);
  return out;
}

export function layoutLines(font: FontId, w: number, ws: string[], st: St): string[] {
  const lines: string[] = [];
  let line = "";
  for (const word of ws) {
    if (word === "\n") {
      lines.push(line);
      line = "";
      continue;
    }
    const trial = line ? line + " " + word : word;
    if (textWidth(font, trial, st) <= w) {
      line = trial;
      continue;
    }
    if (line) lines.push(line);
    line = "";
    if (textWidth(font, word, st) <= w) {
      line = word;
      continue;
    }
    let part = "";
    for (const ch of word) {
      const next = part + ch;
      if (part && textWidth(font, next, st) > w) {
        lines.push(part);
        part = ch;
      } else part = next;
    }
    line = part;
  }
  if (line) lines.push(line);
  return lines;
}

const wordsFit = (font: FontId, w: number, ws: string[], st: St) => ws.every((word) => word === "\n" || textWidth(font, word, st) <= w);

export function wrap(r: Renderer, font: FontId, x: number, y: number, w: number, text: string, maxLines: number, black = true, style: St = St.Reg, lineGap = 2) {
  if (!text || w <= 0) return 0;
  const ws = words(text);
  let f = font;
  let lines = layoutLines(f, w, ws, style);
  while ((lines.length > maxLines || !wordsFit(f, w, ws, style)) && smallerFont(f)) {
    f = smallerFont(f) as FontId;
    lines = layoutLines(f, w, ws, style);
  }
  const lh = lineHeight(f) + lineGap;
  let cy = y;
  for (const l of lines) {
    r.drawText(f, x, cy, l, black, style);
    cy += lh;
  }
  return cy - y;
}

export function wrapInFont(font: FontId, w: number, h: number, text: string, style: St): { font: FontId; used: number } {
  const ws = words(text || "");
  let f = font;
  for (;;) {
    const need = layoutLines(f, w, ws, style).length * lineHeight(f);
    const next = smallerFont(f);
    if ((need <= h && wordsFit(f, w, ws, style)) || !next) return { font: f, used: need };
    f = next;
  }
}

export function wrapIn(r: Renderer, font: FontId, x: number, y: number, w: number, h: number, text: string, black = true, style: St = St.Reg) {
  if (!text || w <= 0) return 0;
  const { font: f } = wrapInFont(font, w, h, text, style);
  const lh = lineHeight(f);
  let cy = y;
  for (const l of layoutLines(f, w, words(text), style)) {
    r.drawText(f, x, cy, l, black, style);
    cy += lh;
  }
  return cy - y;
}

export function wrapCentered(r: Renderer, font: FontId, x: number, y: number, w: number, h: number, text: string, black = true, style: St = St.Reg) {
  if (!text || w <= 0) return 0;
  const { font: f, used } = wrapInFont(font, w, h, text, style);
  const lh = lineHeight(f);
  let cy = y + Math.max(0, Math.trunc((h - used) / 2));
  for (const l of layoutLines(f, w, words(text), style)) {
    r.drawText(f, x + Math.trunc((w - textWidth(f, l, style)) / 2), cy, l, black, style);
    cy += lh;
  }
  return used;
}

export function fitText(r: Renderer, font: FontId, x: number, y: number, text: string, maxW: number, black = true, style: St = St.Reg) {
  if (!text || maxW <= 0) return 0;
  const f = fitFont(font, text, maxW, style);
  const tw = textWidth(f, text, style);
  if (tw <= maxW) {
    r.drawText(f, x, y + Math.trunc((lineHeight(font) - lineHeight(f)) / 2), text, black, style);
    return tw;
  }
  wrap(r, f, x, y, maxW, text, 4, black, style, 0);
  return maxW;
}

// ---- DashDraw helpers (src/espos/DashDraw.cpp) ------------------------------------------------

export const drawFit = (r: Renderer, font: FontId, x: number, y: number, text: string, maxW: number, style: St = St.Reg, black = true) =>
  fitText(r, font, x, y, text, maxW, black, style);

export function drawRight(r: Renderer, font: FontId, rightX: number, y: number, text: string, style: St = St.Reg, black = true) {
  if (!text) return;
  r.drawText(font, rightX - textWidth(font, text, style), y, text, black, style);
}
export function drawFitCentered(r: Renderer, font: FontId, cx: number, y: number, text: string, maxW: number, style: St = St.Reg, black = true) {
  if (!text) return;
  const f = fitFont(font, text, maxW, style);
  const tw = Math.min(textWidth(f, text, style), maxW);
  fitText(r, font, cx - Math.trunc(tw / 2), y, text, maxW, black, style);
}
export function drawCentered(r: Renderer, font: FontId, cx: number, y: number, text: string, style: St = St.Reg, black = true) {
  r.drawText(font, cx - Math.trunc(textWidth(font, text, style) / 2), y, text, black, style);
}
export const digitTop = (font: FontId) => (font === I60 ? 134 - 90 : font === I44 ? 98 - 66 : font === I30 ? 67 - 45 : 0);
export const digitHeight = (font: FontId) => (font === I60 ? 90 : font === I44 ? 66 : font === I30 ? 45 : 0);
export function drawDigits(r: Renderer, font: FontId, x: number, top: number, text: string, black = true) {
  r.drawText(font, x, top - digitTop(font), text, black, St.Bold);
}
export function bar(r: Renderer, x: number, y: number, w: number, h: number, pct: number, black = true) {
  pct = Math.max(0, Math.min(100, pct));
  r.drawRoundedRect(x, y, w, h, 2, Math.trunc(h / 2), black);
  const fill = Math.trunc(((w - 4) * pct) / 100);
  if (fill > 0) r.fillRoundedRect(x + 2, y + 2, fill, h - 4, Math.trunc((h - 4) / 2), black);
}
export function strokeCircle(r: Renderer, cx: number, cy: number, rad: number, lw: number) {
  r.fillCircle(cx, cy, rad, true);
  r.fillCircle(cx, cy, rad - lw, false);
}
function cloudShape(r: Renderer, cx: number, cy: number, s: number, inset: number, black: boolean) {
  const t = Math.trunc;
  const baseW = t((s * 80) / 100) - inset * 2;
  const baseH = t((s * 34) / 100) - inset * 2;
  const baseX = cx - t((s * 40) / 100) + inset;
  const baseY = cy + inset;
  const radius = baseH / 2 > 0 ? t(baseH / 2) : 1;
  if (baseW > 0 && baseH > 0) r.fillRoundedRect(baseX, baseY, baseW, baseH, radius, black);
  r.fillCircle(cx - t((s * 10) / 100), cy + t((s * 2) / 100), t((s * 22) / 100) - inset, black);
  r.fillCircle(cx + t((s * 16) / 100), cy + t((s * 8) / 100), t((s * 15) / 100) - inset, black);
}
function cloud(r: Renderer, cx: number, cy: number, s: number) {
  const lw = s >= 40 ? 3 : 2;
  cloudShape(r, cx, cy, s, 0, true);
  cloudShape(r, cx, cy, s, lw, false);
}
function sun(r: Renderer, cx: number, cy: number, s: number) {
  const t = Math.trunc;
  const rad = t((s * 20) / 100);
  const lw = s >= 40 ? 3 : 2;
  strokeCircle(r, cx, cy, rad, lw);
  const dx = [0, 7, 10, 7, 0, -7, -10, -7], dy = [-10, -7, 0, 7, 10, 7, 0, -7];
  for (let i = 0; i < 8; i++) {
    r.drawLine(cx + t((dx[i] * rad * 14) / 100), cy + t((dy[i] * rad * 14) / 100), cx + t((dx[i] * rad * 20) / 100), cy + t((dy[i] * rad * 20) / 100), lw);
  }
}
export function drawIcon(r: Renderer, icon: string, cx: number, cy: number, s: number) {
  const t = Math.trunc;
  icon = icon || "cloud";
  const lw = s >= 40 ? 3 : 2;
  if (icon === "sun") sun(r, cx, cy, s);
  else if (icon === "cloudsun") {
    sun(r, cx - t((s * 18) / 100), cy - t((s * 16) / 100), t((s * 70) / 100));
    cloud(r, cx + t((s * 6) / 100), cy - t((s * 4) / 100), t((s * 85) / 100));
  } else if (icon === "moon") {
    const rad = t((s * 28) / 100);
    r.fillCircle(cx, cy, rad, true);
    r.fillCircle(cx + t((rad * 55) / 100), cy - t((rad * 35) / 100), rad, false);
  } else if (icon === "fog") {
    for (let i = -1; i <= 1; i++) r.drawLine(cx - t((s * 35) / 100), cy + t((i * s) / 6), cx + t((s * 35) / 100), cy + t((i * s) / 6), lw);
  } else {
    const precip = icon === "rain" || icon === "storm" || icon === "snow";
    const cloudY = precip ? cy - t((s * 18) / 100) : cy - t((s * 8) / 100);
    cloud(r, cx, cloudY, s);
    const top = cloudY + t((s * 34) / 100) + t(s / 14);
    if (icon === "rain") {
      for (let i = -1; i <= 1; i++) {
        const x = cx + t((i * s) / 5);
        r.drawLine(x, top, x - t(s / 12), top + t(s / 6), lw);
      }
    } else if (icon === "storm") {
      r.drawLine(cx + t(s / 12), top, cx - t(s / 20), top + t(s / 10), lw);
      r.drawLine(cx - t(s / 20), top + t(s / 10), cx + t(s / 16), top + t(s / 10), lw);
      r.drawLine(cx + t(s / 16), top + t(s / 10), cx - t(s / 12), top + t(s / 4), lw);
    } else if (icon === "snow") {
      for (let i = -1; i <= 1; i++) r.fillCircle(cx + t((i * s) / 5), top + t(s / 12), lw + 1, true);
    }
  }
}
export function minutesOfDay(hhmm: string, fallback: number) {
  if (!hhmm || hhmm.length < 4 || !/^\d/.test(hhmm)) return fallback;
  const h = parseInt(hhmm, 10);
  const c = hhmm.indexOf(":");
  const m = c >= 0 ? parseInt(hhmm.slice(c + 1), 10) : 0;
  if (!(h >= 0 && h <= 23 && m >= 0 && m <= 59)) return fallback;
  return h * 60 + m;
}
