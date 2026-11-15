/**
 * The X4's widget drawing (firmware src/inkdesk/Widgets.cpp), line for line, onto the SVG renderer: the
 * web previews are the device's screens. Keep the two in step: a change to a widget there is made here too.
 */
import { daysToTet, fromSolar, julianDay } from "../lunar";
import {
  BODY_FONT, CAPS_FONT, I24, I30, I44, I60, N12, N14, N16, N18, Renderer, S36, SMALL_FONT, SMALL_FONT_ID, St,
  ascender, bar, digitHeight, drawCentered, drawDigits, drawFit, drawFitCentered, drawIcon, drawRight, fitFont,
  lineHeight, minutesOfDay, smallerFont, strokeCircle, textWidth, wrap, wrapCentered, wrapIn, wrapInFont, type FontId,
} from "./render";

const kReg = St.Reg, kBold = St.Bold;
const t = Math.trunc;
const RADIUS = 12;
const P = 12;
export const SHADOW = 5;
const H_LOW = 140, H_BIG = 180, H_HUGE = 280;

export type Skin = "card" | "line" | "bare" | "bold" | "shadow";
export interface Box { x: number; y: number; w: number; h: number }
export interface Item { kind: string; style: string; opt: string; inverted: boolean; x: number; y: number }

export interface InkData {
  timeValid: boolean;
  showTime: boolean;
  now: { hour: number; min: number; mday: number; mon: number; year: number; wday: number };
  h12: boolean;
  lunar: string;
  lunarYear: string;
  hasWeather: boolean;
  city: string;
  desc: string;
  icon: string;
  temp: number;
  tmin: number;
  tmax: number;
  rain: number;
  hours: { h: string; icon: string; t: number }[];
  days: { d: string; icon: string; min: number; max: number }[];
  events: { time: string; title: string }[];
  eventCount: number;
  eventsOld: boolean;
  notes: { title: string; time: string }[];
  hasBook: boolean;
  bookPath: string;
  bookTitle: string;
  bookAuthor: string;
  bookPercent: number;
  cover?: string;
  stats: { dated: boolean; todaySec: number; goalMin: number; streak: number; last7: number[]; todayWday: number };
  library: { path: string; title: string; author: string; percent: number; cover?: string }[];
  newsPath: string;
  newsDay: string;
  newsTop: string[];
  newsCount: number;
  gold: { code: string; name: string; buy: number; sell: number; change: number }[];
  goldAt: string;
  goldSrc: string;
  stocks: { sym: string; price: number; change: number; pct: number }[];
  stocksAt: string;
  fx: { code: string; buy: number; sell: number }[];
  crypto: { sym: string; usd: number; vnd: number; pct: number }[];
  cryptoAt: string;
  battery: number;
  lastSync: string; // "06:20", empty when never
  photos: Record<number, string>;
  bookFocus: number;
}

// ---- strings (lib/I18n/translations/vietnamese.yaml) ----------------------------------------
const WEEKDAY_CAPS = ["CHỦ NHẬT", "THỨ HAI", "THỨ BA", "THỨ TƯ", "THỨ NĂM", "THỨ SÁU", "THỨ BẢY"];
const WDAYS_SHORT = ["CN", "T2", "T3", "T4", "T5", "T6", "T7"];
const fmt = (f: string, ...a: (string | number)[]) => {
  let i = 0;
  return f.replace(/%(%|d|s)/g, (m) => (m === "%%" ? "%" : String(a[i++])));
};
const S = {
  EVENTS_CAPS: "LỊCH HÔM NAY", NOTES_CAPS: "THÔNG BÁO", READING: "ĐANG ĐỌC", NO_BOOK: "Chưa có sách đang đọc",
  PROGRESS: "%d%% đã đọc", NO_WEATHER: "Đồng bộ để có thời tiết", NO_EVENTS: "Hôm nay không có lịch.",
  EVENTS_OLD: "Chưa có lịch của hôm nay. Máy lấy ở lần đồng bộ tới.", NEWS_NONE: "Chưa có số báo nào trên máy. Bật Báo sáng trên web InkDesk.",
  STATS_NOCLOCK: "Đồng bộ một lần để bắt đầu đếm", STATS_TODAY_CAPS: "HÔM NAY", STATS_OF_GOAL: "/ %d phút", STATS_WEEK_CAPS: "7 NGÀY",
  DUR_HM: "%d giờ %d phút", DUR_M: "%d phút", TODAY_READ: "Hôm nay %d/%d phút", STREAK_SUFFIX: " · chuỗi %d ngày",
  DASH_STREAK: "CHUỖI %d", MONTH: "THÁNG %d", WEATHER_RANGE: "Thấp %d° · cao %d° · mưa %d%%",
  LUNAR: "ÂM LỊCH %s · %s", LUNAR_SHORT_CAPS: "ÂM %s", SHEET_LUNAR: "Âm %s · %s", MONTH_YEAR: "THÁNG %d · %d",
  TO_TET: "NGÀY TỚI TẾT", LEFT_CAPS: "CÒN", STATS_UNIT: "/%d PHÚT", NEWS_CAPS_FMT: "BÁO SÁNG · %s", NEWS_COUNT: "%d TIN",
  NEWS_READY: "Số mới đã về máy. Bấm OK để đọc.", NO_NOTES: "Chưa có thông báo.", SYNC_AT: "Đồng bộ %s", NO_DATA: "Chưa có số liệu. Máy lấy ở lần đồng bộ tới.",
  MORE_EVENTS: "+%d sự kiện khác", MORE_NOTES: "+%d thông báo khác", BOOKS_CAPS: "THƯ VIỆN", BOOKS_RECENT: "đọc gần đây",
  BOOKS_EMPTY: "Sách bạn mở gần đây sẽ hiện ở đây.", MORE_BOOKS: "+%d cuốn khác", BOOK_DONE: "xong", BOOK_NEW: "mới",
  PHOTO_EMPTY: "Thêm ảnh cho ô này trên web InkDesk (Giao diện máy).", PHOTO_WAIT: "Ảnh sẽ tải về máy ở lần đồng bộ tới.",
  GOLD_CAPS: "GIÁ VÀNG · %s", BUY: "Mua", SELL: "Bán", GOLD_UNIT: "triệu/lượng", STOCKS_CAPS: "CHỨNG KHOÁN",
  FX_CAPS: "TỶ GIÁ · VIETCOMBANK", FX_SHORT: "TỶ GIÁ", NEWS_CAPS: "BÁO SÁNG", CRYPTO_CAPS: "TIỀN SỐ", BILLION: "tỷ", MILLION: "tr",
  LUNAR_WORD: "Âm", DAYS_WORD: "ngày", DAYS_MORE: "ngày nữa", IS_TET: "LÀ TẾT", LOW_CAPS: "THẤP", HIGH_CAPS: "CAO", RAIN_CAPS: "MƯA", MIN_CAPS: "%d PHÚT",
  THIS_WEEK_CAPS: "TUẦN NÀY", NOTES_HINT: "Gửi từ CI hay Home Assistant qua InkDesk", NO_MORE_EVENTS: "Hôm nay không còn lịch",
};
const APPS: Record<string, { label: string; icon: string }> = {
  library: { label: "Thư viện", icon: "M5 4h3v16H5zM10 4h3v16h-3zM15.5 4.6l2.9-.8 3.9 15.3-2.9.8z" },
  news: { label: "Báo sáng", icon: "M4 5h13v14a2 2 0 0 0 2 2H6a2 2 0 0 1-2-2zM17 8h3v11a2 2 0 0 1-2 2M8 9h5M8 13h5M8 17h3" },
  stats: { label: "Thống kê", icon: "M4 20V10M10 20V4M16 20v-7M22 20H2" },
  sync: { label: "Đồng bộ", icon: "M20 11a8 8 0 0 0-14.3-4.5L4 8M4 4v4h4M4 13a8 8 0 0 0 14.3 4.5L20 16M20 20v-4h-4" },
  transfer: { label: "Truyền file", icon: "M7 7h13l-3-3M17 17H4l3 3" },
  apps: { label: "Ứng dụng", icon: "M4 4h7v7H4zM13 4h7v7h-7zM4 13h7v7H4zM13 13h7v7h-7z" },
  settings: { label: "Cài đặt", icon: "M4 6h9M17 6h3M4 12h3M11 12h9M4 18h11M19 18h1M15 4v4M9 10v4M17 16v4" },
  update: { label: "Cập nhật", icon: "M12 4v11M7.5 10.5 12 15l4.5-4.5M5 20h14" },
};

// ---- helpers -------------------------------------------------------------------------------
let gSkin: Skin = "card";
const rounded = (s: Skin) => s === "card" || s === "shadow";

function num(v: number, dec: number): string {
  if (!Number.isFinite(v)) return "—";
  const s = Math.abs(v).toFixed(dec);
  const [ip, fp] = s.split(".");
  let g = "";
  for (let i = 0; i < ip.length; i++) {
    if (i > 0 && (ip.length - i) % 3 === 0) g += ".";
    g += ip[i];
  }
  if (fp) g += "," + fp;
  return (v < 0 ? "-" : "") + g;
}
function codes(opt: string, def: string): string[] {
  return (opt || def).split(",").map((c) => c.trim().toUpperCase()).filter(Boolean);
}
interface Pick<T> { code: string; v: T | null }
function chosen<T>(all: T[], want: string[], key: (x: T) => string): Pick<T>[] {
  const out: Pick<T>[] = [];
  let any = false;
  for (const c of want) {
    const hit = all.find((x) => key(x) === c) ?? null;
    any = any || hit !== null;
    out.push({ code: c, v: hit });
  }
  if (!any) return all.filter((x) => !key(x)).map((x) => ({ code: "", v: x }));
  return out;
}
const anyData = <T,>(l: Pick<T>[]) => l.some((p) => p.v);
const signOf = (v: number) => (v > 0 ? 1 : v < 0 ? -1 : 0);
const kDash = "—";

function triangle(r: Renderer, x: number, y: number, s: number, up: boolean, black = true) {
  const xs = [x, x + s, x + t(s / 2)];
  const ys = [up ? y + s : y, up ? y + s : y, up ? y : y + s];
  if (up) r.fillPolygon(xs, ys, black);
  else {
    r.drawLine(xs[0], ys[0], xs[1], ys[1], 2, black);
    r.drawLine(xs[1], ys[1], xs[2], ys[2], 2, black);
    r.drawLine(xs[2], ys[2], xs[0], ys[0], 2, black);
  }
}
function caps(r: Renderer, x: number, y: number, text: string, black = true, maxW = 0) {
  if (maxW > 0) drawFit(r, CAPS_FONT, x, y, text, maxW, kBold, black);
  else r.drawText(CAPS_FONT, x, y, text, black, kBold);
}
function frame(r: Renderer, b: Box, focused: boolean) {
  switch (gSkin) {
    case "card":
    case "shadow":
      r.drawRoundedRect(b.x, b.y, b.w, b.h, focused ? 4 : 2, RADIUS);
      break;
    case "bold":
      r.drawRect(b.x, b.y, b.w, b.h, focused ? 5 : 3);
      break;
    case "line":
      r.fillRect(b.x, b.y, b.w, 3);
      break;
    case "bare":
      break;
  }
  if (focused && b.h > 50) r.fillRoundedRect(b.x, b.y + 14, 6, b.h - 28, 3, true);
}
function digitsFont(text: string, w: number, h: number): FontId {
  for (const f of [I60, I44, I30]) if (textWidth(f, text, kBold) <= w && digitHeight(f) <= h) return f;
  return I30;
}
function digitsFontOr0(text: string, w: number, h: number): FontId | 0 {
  for (const f of [I60, I44, I30]) if (textWidth(f, text, kBold) <= w && digitHeight(f) <= h) return f;
  return 0;
}
function heroNumber(r: Renderer, text: string, x: number, top: number, w: number, maxH: number) {
  const f = digitsFont(text, w, maxH);
  if (textWidth(f, text, kBold) <= w) {
    drawDigits(r, f, x, top, text);
    return digitHeight(f);
  }
  const tf = fitFont(I24, text, w, kBold);
  r.drawText(tf, x, top, text, true, kBold);
  return lineHeight(tf);
}
function timeText(d: InkData) {
  if (!d.timeValid) return "--:--";
  if (!d.showTime) return `${d.now.mday}/${d.now.mon + 1}`;
  let h = d.now.hour;
  if (d.h12) h = h % 12 === 0 ? 12 : h % 12;
  return `${String(h).padStart(2, "0")}:${String(d.now.min).padStart(2, "0")}`;
}
function viNumber(n: number): string {
  const u = ["", "một", "hai", "ba", "bốn", "năm", "sáu", "bảy", "tám", "chín"];
  if (n < 10) return u[n];
  const tt = t(n / 10), o = n % 10;
  const s = tt === 1 ? "mười" : `${u[tt]} mươi`;
  if (o === 0) return s;
  if (o === 1 && tt > 1) return `${s} mốt`;
  if (o === 5) return `${s} lăm`;
  if (o === 4 && tt > 1) return `${s} tư`;
  return `${s} ${u[o]}`;
}
function analog(r: Renderer, cx: number, cy: number, rad: number, hour: number, minute: number, classic: boolean, hands: boolean) {
  if (classic) strokeCircle(r, cx, cy, rad, 4);
  const inset = classic ? 8 : 2;
  for (let i = 0; i < 60; i++) {
    const big = i % 5 === 0;
    if (!classic && !big) continue;
    const a = (i * Math.PI) / 30;
    const r2 = rad - inset;
    const r1 = r2 - (big ? Math.max(8, t(rad / 7)) : Math.max(4, t(rad / 18)));
    r.thickLine(cx + t(r1 * Math.sin(a)), cy - t(r1 * Math.cos(a)), cx + t(r2 * Math.sin(a)), cy - t(r2 * Math.cos(a)), big ? 4 : 2);
  }
  if (classic && rad >= 60) {
    const font = rad >= 110 ? N16 : SMALL_FONT;
    for (let h = 1; h <= 12; h++) {
      if (rad < 110 && h % 3 !== 0) continue;
      const a = (h * Math.PI) / 6;
      const rr = rad - inset - Math.max(8, t(rad / 7)) - t(rad / 9);
      const n = String(h);
      const tw = textWidth(font, n, kBold);
      const th = ascender(font);
      r.drawText(font, cx + t(rr * Math.sin(a)) - t(tw / 2), cy - t(rr * Math.cos(a)) - t(lineHeight(font) / 2) + t((lineHeight(font) - th) / 2), n, true, kBold);
    }
  }
  if (!hands) return;
  const hand = (turns: number, len: number, width: number) => {
    const a = turns * 2 * Math.PI;
    r.thickLine(cx, cy, cx + t(len * Math.sin(a)), cy - t(len * Math.cos(a)), width);
  };
  hand(((hour % 12) + minute / 60) / 12, rad * 0.5, Math.max(5, t(rad / 14)));
  hand(minute / 60, rad * 0.78, Math.max(3, t(rad / 22)));
  r.fillCircle(cx, cy, Math.max(4, t(rad / 16)), true);
}
function drawFirstFit(r: Renderer, font: FontId, x: number, y: number, texts: string[], w: number, style: St = kBold) {
  for (const s of texts) {
    if (textWidth(font, s, style) <= w) {
      r.drawText(font, x, y, s, true, style);
      return;
    }
  }
  if (texts.length) drawFit(r, font, x, y, texts[texts.length - 1], w, style);
}
function lunarLine(r: Renderer, d: InkData, x: number, y: number, w: number) {
  const full = fmt(S.LUNAR, d.lunar, d.lunarYear);
  const tiny = fmt(S.LUNAR_SHORT_CAPS, d.lunar);
  drawFirstFit(r, CAPS_FONT, x, y, [full, `${tiny} · ${d.lunarYear}`, tiny], w);
}
function dateColumn(r: Renderer, d: InkData, x: number, y: number, w: number, h: number) {
  if (!d.timeValid || w < 60) return;
  const capsH = lineHeight(CAPS_FONT);
  const date = `${d.now.mday}/${d.now.mon + 1}`;
  const digits = textWidth(I30, date, kBold) <= w;
  const dateH = digits ? digitHeight(I30) : lineHeight(N16);
  if (d.lunar && h >= capsH * 2 + dateH + 10) {
    const year = !!d.lunarYear && h >= capsH * 3 + dateH + 10;
    const total = capsH + 4 + dateH + 6 + capsH + (year ? capsH : 0);
    let cy = y + t((h - total) / 2);
    drawFit(r, CAPS_FONT, x, cy, WEEKDAY_CAPS[d.now.wday], w, kBold);
    cy += capsH + 4;
    if (digits) drawDigits(r, I30, x, cy, date);
    else drawFit(r, N16, x, cy, date, w, kBold);
    cy += dateH + 6;
    drawFit(r, CAPS_FONT, x, cy, fmt(S.LUNAR_SHORT_CAPS, d.lunar), w, kBold);
    if (year) drawFit(r, CAPS_FONT, x, cy + capsH, d.lunarYear, w);
    return;
  }
  const f = N14;
  let line = date;
  if (d.lunar) {
    const add = ` · ${S.LUNAR_WORD} ${d.lunar}`;
    if (textWidth(f, line + add, kBold) <= w) line += add;
  }
  const total = capsH + lineHeight(f);
  const cy = y + t((h - total) / 2);
  drawFit(r, CAPS_FONT, x, cy, WEEKDAY_CAPS[d.now.wday], w, kBold);
  drawFit(r, f, x, cy + capsH, line, w, kBold);
}
const centerText = (r: Renderer, font: FontId, x: number, y: number, w: number, h: number, text: string, style: St = kBold) =>
  wrapCentered(r, font, x, y, w, h, text, true, style);

// ---- clock ---------------------------------------------------------------------------------
function drawClock(r: Renderer, it: Item, b: Box, d: InkData) {
  const tm = timeText(d);
  const st = it.style;
  if (st === "analog" || st === "analog_min") {
    const hands = d.timeValid && d.showTime;
    if (b.w * 5 >= b.h * 8) {
      const rad = t(b.h / 2) - 6;
      analog(r, b.x + P + rad, b.y + t(b.h / 2), rad, d.now.hour, d.now.min, st === "analog", hands);
      const x = b.x + P + rad * 2 + 22;
      dateColumn(r, d, x, b.y + P, b.x + b.w - P - x, b.h - 2 * P);
      return;
    }
    const capsH = lineHeight(CAPS_FONT);
    if (b.h - b.w >= capsH * 2 + 16 && d.timeValid) {
      const rad = t(b.w / 2) - 6;
      const below = capsH * 2 + 4;
      const cy = b.y + t((b.h - below - 8) / 2);
      analog(r, b.x + t(b.w / 2), cy, rad, d.now.hour, d.now.min, st === "analog", hands);
      const ty = Math.min(cy + rad + 10, b.y + b.h - P - below);
      drawFitCentered(r, CAPS_FONT, b.x + t(b.w / 2), ty, `${WEEKDAY_CAPS[d.now.wday]} · ${d.now.mday}/${d.now.mon + 1}`, b.w - 2 * P, kBold);
      if (d.lunar) drawFitCentered(r, CAPS_FONT, b.x + t(b.w / 2), ty + capsH + 4, fmt(S.SHEET_LUNAR, d.lunar, d.lunarYear), b.w - 2 * P);
      return;
    }
    const rad = t(Math.min(b.w, b.h) / 2) - 6;
    analog(r, b.x + t(b.w / 2), b.y + t(b.h / 2), rad, d.now.hour, d.now.min, st === "analog", hands);
    return;
  }
  if (st === "flip") {
    if (!d.timeValid || !d.showTime) {
      const f = digitsFont(tm, b.w - 2 * P, b.h - 2 * P);
      drawDigits(r, f, b.x + t((b.w - textWidth(f, tm, kBold)) / 2), b.y + t((b.h - digitHeight(f)) / 2), tm);
      return;
    }
    const hh = tm.slice(0, 2), mm = tm.slice(3, 5);
    let f: FontId = I30;
    let vertical = false;
    for (const cand of [I60, I44, I30]) {
      const cw = textWidth(cand, "88", kBold) + 24;
      const ch = digitHeight(cand) + 28;
      if (cw * 2 + 12 <= b.w - 2 * P && ch <= b.h - 2 * P) { f = cand; break; }
      if (cw <= b.w - 2 * P && ch * 2 + 12 <= b.h - 2 * P) { f = cand; vertical = true; break; }
    }
    const cw = textWidth(f, "88", kBold) + 24;
    const ch = digitHeight(f) + 28;
    const x0 = b.x + t((b.w - (vertical ? cw : cw * 2 + 12)) / 2);
    const y0 = b.y + t((b.h - (vertical ? ch * 2 + 12 : ch)) / 2);
    for (let i = 0; i < 2; i++) {
      const cx = vertical ? x0 : x0 + i * (cw + 12);
      const cy = vertical ? y0 + i * (ch + 12) : y0;
      const s = i === 0 ? hh : mm;
      r.fillRoundedRect(cx, cy, cw, ch, 12, true);
      drawDigits(r, f, cx + t((cw - textWidth(f, s, kBold)) / 2), cy + 14, s, false);
      r.fillRect(cx, cy + t(ch / 2) - 1, cw, 3, false);
    }
    return;
  }
  if (st === "words") {
    const kDays = ["chủ nhật", "thứ hai", "thứ ba", "thứ tư", "thứ năm", "thứ sáu", "thứ bảy"];
    if (!d.timeValid) return;
    const h = d.now.hour, m = d.now.min;
    const part = h < 4 ? "đêm" : h < 11 ? "sáng" : h < 13 ? "trưa" : h < 18 ? "chiều" : h < 22 ? "tối" : "đêm";
    const l1 = viNumber(h % 12 === 0 ? 12 : h % 12) + " giờ";
    const l2 = !d.showTime ? "" : m === 0 ? "đúng" : viNumber(m);
    const l3 = (m === 0 || !d.showTime ? "" : "phút ") + part + " · " + kDays[d.now.wday];
    const w = b.w - 2 * P - 4;
    const joined = l2 ? l1 + " " + l2 : l1;
    const small = b.h >= H_LOW ? N14 : SMALL_FONT;
    const subH = lineHeight(small);
    if (b.h >= H_LOW) {
      const text = l2 ? l1 + "\n" + l2 : l1;
      let { font: f, used } = wrapInFont(S36, w, b.h - P - subH, text, kBold);
      const sub = used <= b.h - P - subH;
      if (!sub) ({ font: f, used } = wrapInFont(S36, w, b.h - P, text, kBold));
      let y = b.y + t((b.h - used - (sub ? subH : 0)) / 2);
      y += wrapIn(r, f, b.x + P + 4, y, w, used, text, true, kBold);
      if (sub) drawFit(r, small, b.x + P + 4, y, l3, w, St.Italic);
      return;
    }
    let big: FontId | 0 = 0;
    let sub = true;
    for (const withSub of [true, false]) {
      for (let f: FontId | 0 = S36; f && !big; f = smallerFont(f)) {
        if (textWidth(f, joined, kBold) <= w && lineHeight(f) + (withSub ? subH : 0) <= b.h - P) big = f;
      }
      if (big) { sub = withSub; break; }
    }
    if (!big) { big = SMALL_FONT_ID; sub = false; }
    let y = b.y + t((b.h - lineHeight(big) - (sub ? subH : 0)) / 2);
    drawFit(r, big, b.x + P + 4, y, joined, w, kBold);
    y += lineHeight(big);
    if (sub) drawFit(r, small, b.x + P + 4, y, l3, w, St.Italic);
    return;
  }
  if (st === "lunar" && b.w >= 300 && b.h >= H_LOW && b.w >= b.h * 2) {
    let f = digitsFontOr0(tm, t(((b.w - 2 * P) * 68) / 100), b.h - 2 * P) || I30;
    const tw = textWidth(f, tm, kBold);
    drawDigits(r, f, b.x + P, b.y + t((b.h - digitHeight(f)) / 2), tm);
    const rx = b.x + P + tw + 16;
    r.fillRect(rx, b.y + P + 4, 2, b.h - 2 * P - 8);
    dateColumn(r, d, rx + 16, b.y + P, b.x + b.w - P - rx - 16, b.h - 2 * P);
    f = f;
    return;
  }
  if (st === "lunar") {
    const capsH = lineHeight(CAPS_FONT);
    const dateLine = d.timeValid && b.h - 2 * P - capsH * 2 - 8 >= digitHeight(I30);
    const room = b.h - 2 * P - capsH * (dateLine ? 2 : 1) - (dateLine ? 8 : 4);
    const centre = b.w >= 300;
    const head = `${WEEKDAY_CAPS[d.now.wday]} · ${String(d.now.mday).padStart(2, "0")}/${String(d.now.mon + 1).padStart(2, "0")}`;
    if (dateLine) {
      if (centre) drawFitCentered(r, CAPS_FONT, b.x + t(b.w / 2), b.y + P, head, b.w - 2 * P, kBold);
      else drawFit(r, CAPS_FONT, b.x + P, b.y + P, head, b.w - 2 * P, kBold);
    }
    const top = b.y + P + (dateLine ? capsH + 4 : 0);
    if (room >= digitHeight(I30)) {
      const f = digitsFont(tm, b.w - 2 * P, room);
      const x = centre ? b.x + t((b.w - textWidth(f, tm, kBold)) / 2) : b.x + P - 2;
      drawDigits(r, f, x, top + t((room - digitHeight(f)) / 2), tm);
      if (centre) {
        if (d.lunar) drawFitCentered(r, CAPS_FONT, b.x + t(b.w / 2), b.y + b.h - P - capsH, fmt(S.LUNAR, d.lunar, d.lunarYear), b.w - 2 * P, kBold);
        return;
      }
    } else {
      const f = fitFont(I24, tm, b.w - 2 * P, kBold);
      r.drawText(f, b.x + P, top + t((room - lineHeight(f)) / 2), tm, true, kBold);
    }
    if (d.lunar) lunarLine(r, d, b.x + P, b.y + b.h - P - capsH, b.w - 2 * P);
    return;
  }
  const f1 = digitsFontOr0(tm, b.w - 2 * P, b.h - 2 * P);
  if (d.timeValid && d.showTime && tm.length === 5) {
    const hh = tm.slice(0, 2), mm = tm.slice(3, 5);
    const f2 = digitsFontOr0("88", b.w - 2 * P, t((b.h - 2 * P - 16) / 2));
    if (f2 && (!f1 || digitHeight(f2) > digitHeight(f1))) {
      const dh = digitHeight(f2);
      const y0 = b.y + t((b.h - dh * 2 - 16) / 2);
      drawDigits(r, f2, b.x + t((b.w - textWidth(f2, hh, kBold)) / 2), y0, hh);
      drawDigits(r, f2, b.x + t((b.w - textWidth(f2, mm, kBold)) / 2), y0 + dh + 16, mm);
      return;
    }
  }
  const f = f1 || I30;
  drawDigits(r, f, b.x + t((b.w - textWidth(f, tm, kBold)) / 2), b.y + t((b.h - digitHeight(f)) / 2), tm);
}

// ---- calendar ------------------------------------------------------------------------------
function drawDate(r: Renderer, it: Item, b: Box, d: InkData) {
  if (!d.timeValid) return;
  const capsH = lineHeight(CAPS_FONT);
  if (it.style === "compact") {
    const w = b.w - 2 * P;
    const low = b.h < H_LOW;
    caps(r, b.x + P, b.y + P, WEEKDAY_CAPS[d.now.wday]);
    if (low && d.lunar) {
      const l = fmt(S.LUNAR_SHORT_CAPS, d.lunar);
      if (textWidth(CAPS_FONT, WEEKDAY_CAPS[d.now.wday], kBold) + 10 + textWidth(CAPS_FONT, l, kBold) <= w) drawRight(r, CAPS_FONT, b.x + b.w - P, b.y + P, l, kBold);
    }
    const buf = `${d.now.mday}/${d.now.mon + 1}`;
    const top = b.y + P + capsH + 2;
    const bottom = b.y + b.h - (low ? t(P / 2) : P + capsH + 4);
    const f = digitsFont(buf, w, bottom - top);
    drawDigits(r, f, b.x + P, top + Math.max(0, t((bottom - top - digitHeight(f)) / 2)), buf);
    if (!low && d.lunar) lunarLine(r, d, b.x + P, b.y + b.h - P - capsH, w);
    return;
  }
  if (b.h < H_LOW) {
    const tileW = Math.min(t((b.w * 45) / 100), b.h + 24);
    if (rounded(gSkin) || gSkin === "bare") r.fillRoundedRect(b.x, b.y, tileW, b.h, RADIUS, true);
    else r.fillRect(b.x, b.y, tileW, b.h, true);
    drawFitCentered(r, CAPS_FONT, b.x + t(tileW / 2), b.y + 6, fmt(S.MONTH, d.now.mon + 1), tileW - 8, kBold, false);
    const day = String(d.now.mday);
    const room = b.h - capsH - 12;
    const f = digitsFont(day, tileW - 12, room);
    drawDigits(r, f, b.x + t((tileW - textWidth(f, day, kBold)) / 2), b.y + capsH + 6 + Math.max(0, t((room - digitHeight(f)) / 2)), day, false);
    const x = b.x + tileW + 12;
    const w = b.x + b.w - P - x;
    const lf = N14;
    const lun = d.lunar ? `${S.LUNAR_WORD} ${d.lunar}` : "";
    const year = !!d.lunarYear && b.h - 12 >= capsH * 2 + lineHeight(lf);
    const total = capsH + (lun ? lineHeight(lf) : 0) + (year ? capsH : 0);
    let y = b.y + t((b.h - total) / 2);
    drawFit(r, CAPS_FONT, x, y, WEEKDAY_CAPS[d.now.wday], w, kBold);
    y += capsH;
    if (lun) {
      drawFit(r, lf, x, y, lun, w, kBold);
      y += lineHeight(lf);
    }
    if (year) drawFit(r, CAPS_FONT, x, y, d.lunarYear, w);
    return;
  }
  const bandH = capsH + 8;
  if (gSkin === "bold" || gSkin === "line") {
    r.fillRect(b.x, b.y, b.w, bandH, true);
    if (gSkin === "bold") r.drawRect(b.x, b.y, b.w, b.h, 3, true);
  } else {
    r.fillRoundedRect(b.x, b.y, b.w, bandH + RADIUS, RADIUS, true);
    r.fillRect(b.x, b.y + bandH, b.w, RADIUS, false);
    if (gSkin !== "bare") r.drawRoundedRect(b.x, b.y, b.w, b.h, 2, RADIUS, true);
  }
  drawCentered(r, CAPS_FONT, b.x + t(b.w / 2), b.y + 4, fmt(S.MONTH, d.now.mon + 1), kBold, false);
  const lunarH = d.lunar ? lineHeight(SMALL_FONT) + 8 : 0;
  const day = String(d.now.mday);
  const room = b.h - bandH - capsH - lunarH - 20;
  const f = digitsFont(day, b.w - 2 * P, room);
  let y = b.y + bandH + 6 + Math.max(0, t((room - digitHeight(f)) / 2));
  drawDigits(r, f, b.x + t((b.w - textWidth(f, day, kBold)) / 2), y, day);
  y += digitHeight(f) + 6;
  drawCentered(r, CAPS_FONT, b.x + t(b.w / 2), y, WEEKDAY_CAPS[d.now.wday], kBold);
  if (lunarH) {
    y = b.y + b.h - lunarH - 2;
    r.fillRect(b.x + P, y, b.w - 2 * P, 2, true);
    drawFitCentered(r, SMALL_FONT, b.x + t(b.w / 2), y + 4, fmt(S.SHEET_LUNAR, d.lunar, d.lunarYear), b.w - 2 * P, kBold);
  }
}

function drawMonth(r: Renderer, it: Item, b: Box, d: InkData) {
  if (!d.timeValid) return;
  const year = d.now.year, month = d.now.mon + 1, today = d.now.mday;
  caps(r, b.x + P, b.y + P, fmt(S.MONTH_YEAR, month, year));
  if (d.lunar) drawRight(r, CAPS_FONT, b.x + b.w - P, b.y + P, fmt(S.LUNAR_SHORT_CAPS, d.lunar), kBold);
  const colW = t((b.w - 2 * P) / 7);
  let y = b.y + P + lineHeight(CAPS_FONT) + 6;
  for (let c = 0; c < 7; c++) drawCentered(r, CAPS_FONT, b.x + P + c * colW + t(colW / 2), y, WDAYS_SHORT[(c + 1) % 7], kBold);
  y += lineHeight(CAPS_FONT) + 2;
  r.fillRect(b.x + P, y, b.w - 2 * P, 2, true);
  y += 4;
  const jd1 = julianDay(1, month, year);
  const firstCol = jd1 % 7;
  const nextMonthJd = month === 12 ? julianDay(1, 1, year + 1) : julianDay(1, month + 1, year);
  const days = nextMonthJd - jd1;
  const weeks = t((firstCol + days + 6) / 7);
  const rowH = t((b.y + b.h - P - y) / weeks);
  const showLunar = it.style !== "solar" && rowH >= 30;
  const beside = showLunar && rowH < 40;
  const lunarGap = rowH >= 40 ? 18 : 16;
  const solarDy = t((rowH - (lunarGap + 10)) / 2) - 6;
  const numFont = showLunar || rowH < 30 ? CAPS_FONT : SMALL_FONT;
  for (let day = 1; day <= days; day++) {
    const ld = fromSolar(day, month, year);
    const idx = firstCol + day - 1;
    const cx = b.x + P + (idx % 7) * colW + t(colW / 2);
    const cy = y + t(idx / 7) * rowH;
    const isToday = day === today;
    const nh = lineHeight(numFont);
    if (isToday) r.fillRoundedRect(cx - t(colW / 2) + 3, cy + 1, colW - 6, rowH - 2, 8, true);
    if (beside) {
      const ny = cy + t((rowH - nh) / 2);
      drawRight(r, numFont, cx + 3, ny, String(day), kBold, !isToday);
      r.drawText(SMALL_FONT_ID, cx + 6, ny + 6, String(ld.day), !isToday, ld.day === 1 ? kBold : kReg);
      continue;
    }
    drawCentered(r, numFont, cx, cy + (showLunar ? solarDy : t((rowH - nh) / 2)), String(day), kBold, !isToday);
    if (showLunar) {
      const s = ld.day === 1 ? `${ld.day}/${ld.month}` : String(ld.day);
      drawCentered(r, SMALL_FONT_ID, cx, cy + solarDy + lunarGap, s, ld.day === 1 ? kBold : kReg, !isToday);
    }
  }
}

function linesOf(font: FontId, titleW: number, title: string) {
  let lines = 1, cur = 0;
  const space = textWidth(font, " ");
  for (const word of title.split(" ")) {
    const ww = textWidth(font, word);
    if (cur > 0 && cur + space + ww > titleW) {
      lines++;
      cur = ww;
    } else cur += (cur > 0 ? space : 0) + ww;
  }
  return lines;
}

function drawEvents(r: Renderer, _it: Item, b: Box, d: InkData) {
  caps(r, b.x + P + 6, b.y + P, d.eventCount > 0 ? `${S.EVENTS_CAPS} · ${d.eventCount}` : S.EVENTS_CAPS);
  let y = b.y + P + lineHeight(CAPS_FONT) + 6;
  const w = b.w - 2 * P - 12;
  const x = b.x + P + 6;
  if (!d.events.length) {
    centerText(r, b.h < H_LOW ? BODY_FONT : N16, x, y, w, b.y + b.h - P - y, d.eventsOld ? S.EVENTS_OLD : S.NO_EVENTS);
    return;
  }
  let next = -1;
  if (d.timeValid) {
    const nowMin = d.now.hour * 60 + d.now.min;
    next = d.events.findIndex((e) => minutesOfDay(e.time, -1) >= nowMin);
  }
  const font = b.w >= 300 ? BODY_FONT : N12;
  const lineH = lineHeight(font);
  const timeW = textWidth(font, "00:00", kBold) + 12;
  const titleW = w - timeW;
  const bottom = b.y + b.h - t(P / 2);
  const moreH = lineHeight(CAPS_FONT) + 2;
  const n = d.events.length;
  const start = next >= 0 ? next : Math.max(0, n - 1);
  const left = n - start;
  const agendaTimeF = N16;
  if (left > 0 && left <= 4 && t((bottom - y) / left) >= lineHeight(agendaTimeF) + lineH + 10) {
    const rowH = t((bottom - y) / left);
    for (let i = start; i < n; i++) {
      const inv = i === next;
      if (inv) r.fillRoundedRect(x - 8, y + 1, w + 16, rowH - 2, 8, true);
      else if (i > start) r.fillRect(x - 8, y, w + 16, 2, true);
      const titleRoom = rowH - lineHeight(agendaTimeF) - 8;
      const { used } = wrapInFont(font, w, titleRoom, d.events[i].title, kReg);
      const top = y + t((rowH - lineHeight(agendaTimeF) - used) / 2);
      r.drawText(agendaTimeF, x, top, d.events[i].time, !inv, kBold);
      wrapIn(r, font, x, top + lineHeight(agendaTimeF), w, titleRoom, d.events[i].title, !inv, kReg);
      y += rowH;
    }
    return;
  }
  let shown = 0;
  for (let i = start; i < n; i++) {
    const lines = Math.min(3, linesOf(font, titleW, d.events[i].title));
    const rowH = lines * lineH + 6;
    const reserve = i < n - 1 ? moreH : 0;
    if (shown > 0 && y + rowH + reserve > bottom) break;
    const inv = i === next;
    if (inv) r.fillRoundedRect(x - 8, y, w + 16, rowH, 8, true);
    r.drawText(font, x, y + 3, d.events[i].time, !inv, kBold);
    wrap(r, font, x + timeW, y + 3, titleW, d.events[i].title, 3, !inv, kReg, 0);
    y += rowH;
    shown++;
  }
  const hidden = n - shown;
  if (hidden > 0) drawFit(r, CAPS_FONT, x, Math.min(y + 2, bottom - moreH), fmt(S.MORE_EVENTS, hidden), w, kBold);
}

function drawCountdown(r: Renderer, it: Item, b: Box, d: InkData) {
  if (!d.timeValid) return;
  const y0 = d.now.year, m0 = d.now.mon + 1, d0 = d.now.mday;
  let days = -1;
  let label = S.TO_TET;
  if (it.style === "date" && it.opt.length >= 10) {
    const m = /^(\d{4})-(\d{1,2})-(\d{1,2})/.exec(it.opt);
    if (m) days = julianDay(+m[3], +m[2], +m[1]) - julianDay(d0, m0, y0);
    const bar = it.opt.indexOf("|");
    label = bar < 0 ? "" : it.opt.slice(bar + 1);
  } else days = daysToTet(d0, m0, y0);
  if (days < 0) return;
  const buf = String(days);
  if (b.h < H_LOW) {
    let f: FontId | 0 = digitsFontOr0(buf, t((b.w - 2 * P) / 2), b.h - 16);
    let nw: number;
    if (f) {
      nw = textWidth(f, buf, kBold);
      drawDigits(r, f, b.x + P, b.y + t((b.h - digitHeight(f)) / 2), buf);
    } else {
      f = N18;
      nw = textWidth(f, buf, kBold);
      r.drawText(f, b.x + P, b.y + t((b.h - lineHeight(f)) / 2), buf, true, kBold);
    }
    const x = b.x + P + nw + 14;
    const w = b.x + b.w - P - x;
    const wf = N16;
    const capsH = lineHeight(CAPS_FONT);
    const ty = b.y + t((b.h - lineHeight(wf) - capsH) / 2);
    drawFit(r, wf, x, ty, S.DAYS_MORE, w, kBold);
    drawFit(r, CAPS_FONT, x, ty + lineHeight(wf), it.style === "date" ? label : S.IS_TET, w, kBold);
    return;
  }
  const capsH = lineHeight(CAPS_FONT);
  const f = digitsFont(buf, b.w - 2 * P, b.h - 2 * P - capsH * 2 - 8);
  const total = capsH * 2 + digitHeight(f) + 8;
  let y = b.y + t((b.h - total) / 2);
  drawCentered(r, CAPS_FONT, b.x + t(b.w / 2), y, S.LEFT_CAPS, kBold);
  y += capsH + 2;
  drawDigits(r, f, b.x + t((b.w - textWidth(f, buf, kBold)) / 2), y, buf);
  y += digitHeight(f) + 6;
  drawFitCentered(r, CAPS_FONT, b.x + t(b.w / 2), y, label, b.w - 2 * P, kBold);
}

// ---- weather -------------------------------------------------------------------------------
function weatherCells(r: Renderer, d: InkData, days: boolean, x: number, y: number, w: number, h: number) {
  const n = days ? Math.min(d.days.length, 3) : Math.min(d.hours.length, 4);
  if (n === 0 || h < 30) return;
  const cw = t(w / n);
  const capsH = lineHeight(CAPS_FONT);
  const valF = h >= 110 ? SMALL_FONT : CAPS_FONT;
  const valH = lineHeight(valF);
  for (let i = 0; i < n; i++) {
    const cx = x + i * cw;
    if (i > 0) r.fillRect(cx, y + 4, 2, h - 8, true);
    const label = days ? d.days[i].d : d.hours[i].h;
    const value = days ? `${d.days[i].min}–${d.days[i].max}°` : `${d.hours[i].t}°`;
    const icon = days ? d.days[i].icon : d.hours[i].icon;
    const is = Math.min(Math.min(h - capsH - valH - 8, cw - 16), 64);
    if (is >= 18) {
      drawFitCentered(r, CAPS_FONT, cx + t(cw / 2), y, label, cw - 8, kReg);
      drawIcon(r, icon, cx + t(cw / 2), y + capsH + t((h - capsH - valH) / 2), is);
      drawFitCentered(r, valF, cx + t(cw / 2), y + h - valH, value, cw - 8, kBold);
    } else {
      const top = y + t((h - capsH - valH) / 2);
      drawFitCentered(r, CAPS_FONT, cx + t(cw / 2), top, label, cw - 8, kReg);
      drawFitCentered(r, valF, cx + t(cw / 2), top + capsH, value, cw - 8, kBold);
    }
  }
}
function weatherNow(r: Renderer, d: InkData, x: number, y: number, h: number, maxW: number, maxIcon = 64) {
  const temp = `${d.temp}°`;
  const s = Math.min(h, maxIcon);
  const f = digitsFontOr0(temp, maxW - s - 8, h) || I30;
  drawIcon(r, d.icon, x + t(s / 2), y + t(h / 2), s);
  drawDigits(r, f, x + s + 8, y + t((h - digitHeight(f)) / 2), temp);
  return x + s + 8 + textWidth(f, temp, kBold);
}
function drawWeather(r: Renderer, it: Item, b: Box, d: InkData) {
  if (!d.hasWeather) {
    centerText(r, SMALL_FONT, b.x + P, b.y + P, b.w - 2 * P, b.h - 2 * P, S.NO_WEATHER);
    return;
  }
  const range = `${d.tmin}–${d.tmax}° · ${d.rain}%`;
  const days = it.style === "days" && d.days.length > 0;
  const cells = days || (it.style === "hourly" && d.hours.length > 0);
  const capsH = lineHeight(CAPS_FONT);
  const right = b.x + b.w - P;
  if (b.h < H_LOW) {
    const top = b.y + 8, ih = b.h - 16;
    if (cells && b.w < 300) {
      weatherCells(r, d, days, b.x + P, top, b.w - 2 * P, ih);
      return;
    }
    const x = weatherNow(r, d, b.x + P, top, ih, b.w < 300 ? b.w - 2 * P : t(((b.w - 2 * P) * 40) / 100), 58) + 12;
    if (cells) {
      r.fillRect(x - 2, top + 4, 2, ih - 8, true);
      weatherCells(r, d, days, x + 6, top, right - x - 6, ih);
      return;
    }
    let rr = right;
    if (b.w >= 300 && d.lunar) {
      const l = fmt(S.LUNAR_SHORT_CAPS, d.lunar);
      const lw = Math.max(textWidth(CAPS_FONT, l, kBold), textWidth(CAPS_FONT, d.lunarYear)) + 16;
      const lh = capsH * 2 + 8;
      const ly = b.y + t((b.h - lh) / 2);
      r.drawRoundedRect(rr - lw, ly, lw, lh, 2, 8, true);
      drawCentered(r, CAPS_FONT, rr - t(lw / 2), ly + 4, l, kBold);
      drawCentered(r, CAPS_FONT, rr - t(lw / 2), ly + 4 + capsH, d.lunarYear);
      rr -= lw + 10;
    }
    const w = rr - x;
    if (w >= 50) {
      const mid = b.y + t(b.h / 2);
      drawFit(r, SMALL_FONT, x, mid - lineHeight(SMALL_FONT), d.desc || d.city, w, kBold);
      drawFit(r, CAPS_FONT, x, mid + 2, range, w);
    }
    return;
  }
  if (b.w < 300) {
    if (cells) {
      const hh = digitHeight(I30);
      const x = weatherNow(r, d, b.x + P, b.y + P, hh, b.w - 2 * P, 44) + 10;
      if (right - x >= 40) drawFit(r, SMALL_FONT, x, b.y + P + t((hh - lineHeight(SMALL_FONT)) / 2), d.desc || d.city, right - x, kBold);
      const top = b.y + P + hh + 8;
      r.fillRect(b.x + P, top, b.w - 2 * P, 2, true);
      weatherCells(r, d, days, b.x + P, top + 6, b.w - 2 * P, b.y + b.h - P - top - 6);
      return;
    }
    caps(r, b.x + P, b.y + P, d.city, true, b.w - 2 * P);
    const bottomH = lineHeight(SMALL_FONT) + capsH;
    const midTop = b.y + P + capsH + 4;
    const midH = b.y + b.h - P - bottomH - 6 - midTop;
    const hh = Math.min(midH, digitHeight(I60));
    weatherNow(r, d, b.x + P, midTop + t((midH - hh) / 2), hh, b.w - 2 * P, 72);
    const by = b.y + b.h - P - bottomH;
    drawFit(r, SMALL_FONT, b.x + P, by, d.desc, b.w - 2 * P, kBold);
    drawFit(r, CAPS_FONT, b.x + P, by + lineHeight(SMALL_FONT), range, b.w - 2 * P);
    return;
  }
  if (cells) {
    const hh = Math.max(digitHeight(I30), lineHeight(SMALL_FONT) + capsH);
    const x = weatherNow(r, d, b.x + P, b.y + P, hh, t((b.w - 2 * P) / 2), 52) + 16;
    const tw = right - x;
    const ty = b.y + P + t((hh - lineHeight(SMALL_FONT) - capsH) / 2);
    const place = !d.desc ? d.city : !d.city ? d.desc : `${d.city} · ${d.desc}`;
    drawFit(r, SMALL_FONT, x, ty, place, tw, kBold);
    drawFit(r, CAPS_FONT, x, ty + lineHeight(SMALL_FONT), range, tw);
    const top = b.y + P + hh + 8;
    r.fillRect(b.x + P, top, b.w - 2 * P, 2, true);
    weatherCells(r, d, days, b.x + P, top + 6, b.w - 2 * P, b.y + b.h - P - top - 6);
    return;
  }
  const statsH = b.h >= H_HUGE ? capsH + digitHeight(I30) + 18 : 0;
  const topH = b.h - 2 * P - (statsH ? statsH + 10 : 0);
  const hh = Math.min(topH, digitHeight(I60));
  const x = weatherNow(r, d, b.x + P, b.y + P + t((topH - hh) / 2), hh, t(((b.w - 2 * P) * 58) / 100), 96) + 18;
  const w = right - x;
  const line = fmt(S.WEATHER_RANGE, d.tmin, d.tmax, d.rain);
  const { font: rangeF, used } = wrapInFont(CAPS_FONT, w, capsH * 2, line, kReg);
  const textH = lineHeight(BODY_FONT) + (d.desc ? lineHeight(SMALL_FONT) : 0) + (statsH ? 0 : used + 4);
  let ty = b.y + P + Math.max(0, t((topH - textH) / 2));
  drawFit(r, BODY_FONT, x, ty, d.city, w, kBold);
  ty += lineHeight(BODY_FONT);
  if (d.desc) {
    drawFit(r, SMALL_FONT, x, ty, d.desc, w);
    ty += lineHeight(SMALL_FONT);
  }
  if (!statsH) {
    wrapIn(r, rangeF, x, ty + 4, w, capsH * 2, line, true, kReg);
    return;
  }
  const sy = b.y + b.h - P - statsH;
  r.fillRect(b.x + P, sy, b.w - 2 * P, 2, true);
  const cw = t((b.w - 2 * P) / 3);
  const labels = [S.LOW_CAPS, S.HIGH_CAPS, S.RAIN_CAPS];
  const vals = [`${d.tmin}°`, `${d.tmax}°`, `${d.rain}%`];
  for (let i = 0; i < 3; i++) {
    const cx = b.x + P + i * cw;
    if (i > 0) r.fillRect(cx, sy + 8, 2, statsH - 10, true);
    drawCentered(r, CAPS_FONT, cx + t(cw / 2), sy + 8, labels[i], kBold);
    const f = digitsFont(vals[i], cw - 12, digitHeight(I30));
    drawDigits(r, f, cx + t((cw - textWidth(f, vals[i], kBold)) / 2), sy + 10 + capsH, vals[i]);
  }
}

// ---- reading -------------------------------------------------------------------------------
function drawCoverAt(r: Renderer, href: string | undefined, x: number, y: number, w: number, h: number) {
  if (href) r.image(x, y, w, h, href);
  else {
    // GUI.drawCoverPlaceholder: white top third, black below, an outline.
    const top = t(h / 3);
    r.fillRect(x, y, w, h, false);
    r.fillRect(x, y + top, w, h - top, true);
  }
  r.drawRect(x, y, w, h, 2, true);
}
export function readingCoverH(it: Item, b: Box) {
  if (it.style === "text") return 0;
  if (b.w < 300) return b.h - 2 * P - 22;
  const maxW = t(((b.w - 2 * P - 10) * (b.h >= H_HUGE ? 50 : 36)) / 100);
  return Math.min(b.h - 2 * P, t((maxW * 3) / 2));
}
function drawReading(r: Renderer, it: Item, b: Box, d: InkData) {
  const cover = it.style !== "text";
  if (b.w < 300 && cover) {
    const ch = readingCoverH(it, b);
    const cw = t((ch * 2) / 3);
    drawCoverAt(r, d.cover, b.x + t((b.w - cw) / 2), b.y + P, cw, ch);
    if (d.bookPercent >= 0) {
      bar(r, b.x + P, b.y + b.h - P - 12, b.w - 2 * P - 46, 12, d.bookPercent, true);
      drawRight(r, CAPS_FONT, b.x + b.w - P, b.y + b.h - P - 16, `${d.bookPercent}%`, kBold);
    }
    return;
  }
  const big = b.h >= H_BIG, huge = b.h >= H_HUGE;
  let tx = b.x + P + 10;
  if (cover) {
    const ch = readingCoverH(it, b);
    const cw = t((ch * 2) / 3);
    drawCoverAt(r, d.cover, tx, b.y + t((b.h - ch) / 2), cw, ch);
    tx += cw + 16;
  }
  const tw = b.x + b.w - P - tx;
  let ty = b.y + P + 2;
  caps(r, tx, ty, S.READING);
  ty += lineHeight(CAPS_FONT) + 2;
  if (!d.hasBook) {
    wrap(r, N14, tx, ty, tw, S.NO_BOOK, 2, true, kBold, 0);
    return;
  }
  const pct = `${Math.max(0, d.bookPercent)}%`;
  const pctF = huge ? I44 : I30;
  const showPct = big && d.bookPercent >= 0;
  const statsLine = d.stats.dated;
  const lineH = lineHeight(CAPS_FONT);
  const blockH = (showPct ? digitHeight(pctF) + 8 : 0) + (d.bookPercent >= 0 ? 18 : 0) + (statsLine ? lineH : 0) + (!showPct && d.bookPercent >= 0 ? lineH : 0);
  const by = b.y + b.h - P - blockH;
  const titleF = huge ? (cover ? I24 : S36) : big ? N18 : N16;
  const authorF = big ? BODY_FONT : SMALL_FONT;
  const room = by - 6 - ty;
  let authorH = d.bookAuthor ? lineHeight(authorF) + 2 : 0;
  const { used } = wrapInFont(titleF, tw, room - authorH, d.bookTitle, kBold);
  if (used > room - authorH) authorH = 0;
  if (big) ty += Math.max(0, t((room - authorH - used) / 2));
  ty += wrapIn(r, titleF, tx, ty, tw, room - authorH, d.bookTitle, true, kBold);
  if (authorH > 0) drawFit(r, authorF, tx, ty, d.bookAuthor, tw);
  let y = by;
  if (showPct) {
    drawDigits(r, pctF, tx, y, pct);
    y += digitHeight(pctF) + 8;
  }
  if (d.bookPercent >= 0) {
    bar(r, tx, y, tw, 12, d.bookPercent, true);
    y += 18;
    if (!showPct) {
      r.drawText(CAPS_FONT, tx, y - 2, fmt(S.PROGRESS, d.bookPercent), true, kBold);
      y += lineH;
    }
  }
  if (statsLine) {
    let line = fmt(S.TODAY_READ, t(d.stats.todaySec / 60), d.stats.goalMin);
    if (d.stats.streak > 0) line += fmt(S.STREAK_SUFFIX, d.stats.streak);
    drawFit(r, CAPS_FONT, tx, y - 2, line, tw, kBold);
  }
}

function ring(r: Renderer, cx: number, cy: number, rad: number, thick: number, pct: number) {
  strokeCircle(r, cx, cy, rad, 2);
  const p = Math.max(0, Math.min(100, pct)) / 100;
  if (p > 0) {
    const rm = rad - thick / 2;
    const a = p * 2 * Math.PI;
    const x1 = cx + rm * Math.sin(a), y1 = cy - rm * Math.cos(a);
    const path = p >= 1 ? `M${cx} ${cy - rm}A${rm} ${rm} 0 1 1 ${cx - 0.01} ${cy - rm}` : `M${cx} ${cy - rm}A${rm} ${rm} 0 ${a > Math.PI ? 1 : 0} 1 ${x1} ${y1}`;
    r.raw(`<path d="${path}" fill="none" stroke="#141414" stroke-width="${thick}"/>`);
  }
  strokeCircle(r, cx, cy, rad - thick, 2);
}

function drawStats(r: Renderer, it: Item, b: Box, d: InkData) {
  const s = d.stats;
  if (!s.dated) {
    centerText(r, SMALL_FONT, b.x + P, b.y + P, b.w - 2 * P, b.h - 2 * P, S.STATS_NOCLOCK);
    return;
  }
  const mins = t(s.todaySec / 60);
  const capsH = lineHeight(CAPS_FONT);
  const weekSec = s.last7.reduce((a, v) => a + v, 0);
  if (it.style === "week") {
    caps(r, b.x + P + 6, b.y + P, S.STATS_WEEK_CAPS);
    drawRight(r, CAPS_FONT, b.x + b.w - P, b.y + P, fmt(S.MIN_CAPS, t(weekSec / 60)), kBold);
    const goal = s.goalMin * 60;
    let top = goal;
    for (const v of s.last7) top = Math.max(top, v);
    top += t(top / 8);
    const base = b.y + b.h - P - capsH - 4;
    const barsH = base - (b.y + P + capsH + 8);
    const gap = b.w >= 300 ? 8 : 5;
    const bw = t((b.w - 2 * P - 12 - gap * 6) / 7);
    const gy = base - t((goal * barsH) / Math.max(1, top));
    for (let gx = b.x + P + 6; gx < b.x + b.w - P - 6; gx += 10) r.fillRect(gx, gy, 5, 1, true);
    const labF = WDAYS_SHORT.some((l) => textWidth(CAPS_FONT, l, kBold) > bw + gap - 2) ? SMALL_FONT_ID : CAPS_FONT;
    for (let k = 0; k < 7; k++) {
      const bx = b.x + P + 6 + k * (bw + gap);
      const v = s.last7[k];
      const h = v > 0 ? Math.max(3, t((v * barsH) / top)) : 0;
      if (h > 0) {
        if (v >= goal) r.fillRect(bx, base - h, bw, h, true);
        else r.drawRect(bx, base - h, bw, h, 2, true);
      }
      r.fillRect(bx, base, bw, 2, true);
      const wday = (s.todayWday - 6 + k + 14) % 7;
      drawCentered(r, labF, bx + t(bw / 2), base + 3 + t((lineHeight(CAPS_FONT) - lineHeight(labF)) / 2), WDAYS_SHORT[wday], k === 6 ? kBold : kReg);
    }
    return;
  }
  const pct = s.goalMin > 0 ? t((mins * 100) / s.goalMin) : 0;
  if (b.w >= 300) {
    const rad = t((b.h - 2 * P) / 2);
    const cx = b.x + P + 6 + rad, cy = b.y + t(b.h / 2);
    ring(r, cx, cy, rad, Math.max(8, t(rad / 4)), pct);
    const pf = rad >= 60 ? N18 : SMALL_FONT;
    drawCentered(r, pf, cx, cy - t(lineHeight(pf) / 2), `${Math.min(999, pct)}%`, kBold);
    const x = cx + rad + 22;
    const w = b.x + b.w - P - x;
    const vf = N16;
    const lineH = capsH + lineHeight(vf) + 6;
    const streak = s.streak > 0 && b.h - 2 * P >= lineH * 2 + capsH;
    const total = lineH * 2 + (streak ? capsH : 0);
    let y = b.y + t((b.h - total) / 2);
    caps(r, x, y, S.STATS_TODAY_CAPS);
    drawFit(r, vf, x, y + capsH, `${mins} ${fmt(S.STATS_OF_GOAL, s.goalMin)}`, w, kBold);
    y += lineH;
    caps(r, x, y, S.THIS_WEEK_CAPS);
    const wm = t(weekSec / 60);
    drawFit(r, vf, x, y + capsH, wm >= 60 ? fmt(S.DUR_HM, t(wm / 60), wm % 60) : fmt(S.DUR_M, wm), w, kBold);
    y += lineH;
    if (streak) drawFit(r, CAPS_FONT, x, y, fmt(S.DASH_STREAK, s.streak), w, kBold);
    return;
  }
  const rad = t(Math.min(b.w - 2 * P, b.h - 2 * P - capsH - 6) / 2);
  const cx = b.x + t(b.w / 2);
  const cy = b.y + P + rad;
  ring(r, cx, cy, rad, Math.max(8, t(rad / 4)), pct);
  const buf = String(mins);
  if (rad >= 60) drawDigits(r, I30, cx - t(textWidth(I30, buf, kBold) / 2), cy - t(digitHeight(I30) / 2), buf);
  else drawCentered(r, N18, cx, cy - t(lineHeight(N18) / 2), buf, kBold);
  let line = fmt(S.STATS_UNIT, s.goalMin);
  if (s.streak > 0) line += " · " + fmt(S.DASH_STREAK, s.streak);
  drawFitCentered(r, CAPS_FONT, cx, b.y + b.h - P - capsH, line, b.w - 2 * P, kBold);
}

function drawNews(r: Renderer, _it: Item, b: Box, d: InkData) {
  caps(r, b.x + P + 6, b.y + P, d.newsDay ? fmt(S.NEWS_CAPS_FMT, d.newsDay) : S.NEWS_CAPS);
  if (d.newsCount > 0) drawRight(r, CAPS_FONT, b.x + b.w - P, b.y + P, fmt(S.NEWS_COUNT, d.newsCount), kBold);
  const x = b.x + P + 6;
  const w = b.w - 2 * P - 12;
  let y = b.y + P + lineHeight(CAPS_FONT) + 6;
  if (!d.newsTop.length && !d.newsPath) {
    centerText(r, BODY_FONT, x, y, w, b.y + b.h - P - y, S.NEWS_NONE, kReg);
    return;
  }
  if (!d.newsTop.length) {
    centerText(r, b.h >= H_BIG ? N18 : N16, x, y, w, b.y + b.h - P - y, S.NEWS_READY);
    return;
  }
  const maxY = b.y + b.h - t(P / 2);
  const headF = b.h >= H_HUGE ? I24 : b.h >= H_BIG ? N18 : N16;
  const subF = b.h >= H_HUGE ? BODY_FONT : SMALL_FONT;
  const others = d.newsTop.length - 1;
  const headRoom = Math.min(lineHeight(headF) * 3, others > 0 ? t(((maxY - y) * 3) / 5) : maxY - y);
  y += wrapIn(r, headF, x, y, w, headRoom, d.newsTop[0], true, kBold) + 6;
  for (let i = 1; i < d.newsTop.length; i++) {
    const lh = lineHeight(subF);
    if (y + 6 + lh > maxY) break;
    r.fillRect(x, y, w, 2, true);
    y += 6;
    y += wrapIn(r, subF, x, y, w, Math.min(lh * 2, maxY - y), d.newsTop[i], true, kReg) + 4;
  }
}

function drawNotify(r: Renderer, _it: Item, b: Box, d: InkData) {
  caps(r, b.x + P + 6, b.y + P, d.notes.length ? `${S.NOTES_CAPS} · ${d.notes.length}` : S.NOTES_CAPS);
  let y = b.y + P + lineHeight(CAPS_FONT) + 4;
  const x = b.x + P + 6;
  const w = b.w - 2 * P - 12;
  if (!d.notes.length) {
    const h = b.y + b.h - t(P / 2) - y;
    const hintH = lineHeight(CAPS_FONT);
    if (h >= lineHeight(N16) + hintH + 8) {
      const used = lineHeight(N16);
      const top = y + t((h - used - hintH - 4) / 2);
      drawFitCentered(r, N16, b.x + t(b.w / 2), top, S.NO_NOTES, w, kBold);
      drawFitCentered(r, CAPS_FONT, b.x + t(b.w / 2), top + used + 4, S.NOTES_HINT, w);
    } else centerText(r, SMALL_FONT, x, y, w, h, S.NO_NOTES);
    return;
  }
  const noteF = t((b.y + b.h - t(P / 2) - y) / d.notes.length) >= lineHeight(BODY_FONT) + 8 ? BODY_FONT : SMALL_FONT;
  const lh = lineHeight(noteF) + 4;
  const moreH = lineHeight(CAPS_FONT) + 2;
  const bottom = b.y + b.h - t(P / 2);
  const n = d.notes.length;
  let shown = 0;
  for (let i = 0; i < n; i++) {
    const reserve = i < n - 1 ? moreH : 0;
    if (shown > 0 && y + lh + reserve > bottom) break;
    const note = d.notes[i];
    const tw = note.time ? textWidth(CAPS_FONT, note.time, kBold) + 10 : 0;
    drawFit(r, noteF, x, y, note.title, w - tw, kBold);
    if (tw) drawRight(r, CAPS_FONT, x + w, y + t((lineHeight(noteF) - lineHeight(CAPS_FONT)) / 2), note.time, kBold);
    y += lh;
    shown++;
  }
  if (shown < n) drawFit(r, CAPS_FONT, x, Math.min(y, bottom - moreH), fmt(S.MORE_NOTES, n - shown), w, kBold);
}

function drawBattery(r: Renderer, _it: Item, b: Box, d: InkData) {
  const buf = `${d.battery}%`;
  const capsH = lineHeight(CAPS_FONT);
  const sync = !!d.lastSync;
  const bw = 52, bh = 26;
  const room = b.h - 16 - (sync ? capsH + 2 : 0);
  const f = digitsFontOr0(buf, b.w - 2 * P - bw - 20, room);
  const rowH = f ? digitHeight(f) : lineHeight(N18);
  const top = b.y + t((b.h - rowH - (sync ? capsH + 2 : 0)) / 2);
  const x = b.x + P;
  const by = top + t((rowH - bh) / 2);
  r.drawRect(x, by, bw, bh, 2, true);
  r.fillRect(x + bw, by + 8, 4, bh - 16, true);
  const fill = t(((bw - 6) * Math.min(100, Math.max(0, d.battery))) / 100);
  if (fill > 0) r.fillRect(x + 3, by + 3, fill, bh - 6, true);
  if (f) drawDigits(r, f, x + bw + 14, top, buf);
  else r.drawText(N18, x + bw + 14, top, buf, true, kBold);
  if (sync) drawFit(r, CAPS_FONT, x, top + rowH + 2, fmt(S.SYNC_AT, d.lastSync), b.w - 2 * P);
}

function drawApp(r: Renderer, it: Item, b: Box, focused: boolean) {
  const a = APPS[it.style] ?? APPS.apps;
  if (focused) {
    if (gSkin === "bold" || gSkin === "line") r.fillRect(b.x, b.y, b.w, b.h, true);
    else r.fillRoundedRect(b.x, b.y, b.w, b.h, RADIUS, true);
    r.fillRoundedRect(b.x + t((b.w - 40) / 2), b.y + 10, 40, 40, 8, false);
  } else if (gSkin === "bold") r.drawRect(b.x, b.y, b.w, b.h, 3, true);
  else if (gSkin === "card" || gSkin === "shadow") r.drawRoundedRect(b.x, b.y, b.w, b.h, 2, RADIUS, true);
  r.icon(a.icon, b.x + t((b.w - 32) / 2), b.y + 14, 32);
  drawFitCentered(r, CAPS_FONT, b.x + t(b.w / 2), b.y + b.h - lineHeight(CAPS_FONT) - 8, a.label, b.w - 8, kBold, !focused);
}

// ---- markets -------------------------------------------------------------------------------
function changeText(r: Renderer, x: number, y: number, change: number, text: string, font: FontId = CAPS_FONT) {
  const s = t(lineHeight(font) / 2);
  triangle(r, x, y + t((lineHeight(font) - s) / 2), s, change >= 0);
  r.drawText(font, x + s + 5, y, text, true, kBold);
}
function rowsFor(n: number, avail: number) {
  for (const f of [N18, BODY_FONT, N12, CAPS_FONT, SMALL_FONT_ID]) {
    const h = lineHeight(f) + 4;
    if (n * h <= avail) return { font: f, h: Math.min(h * 2, t(avail / Math.max(1, n))) };
  }
  return { font: SMALL_FONT_ID, h: Math.max(1, t(avail / Math.max(1, n))) };
}
function marketHeader(r: Renderer, b: Box, title: string, at: string) {
  const x = b.x + P + 6;
  const w = b.w - 2 * P - 6;
  let aw = at ? textWidth(CAPS_FONT, at) + 10 : 0;
  if (aw > 0 && textWidth(CAPS_FONT, title, kBold) + aw > w) aw = 0;
  if (aw > 0) drawRight(r, CAPS_FONT, b.x + b.w - P, b.y + P, at);
  drawFit(r, CAPS_FONT, x, b.y + P, title, w - aw, kBold);
}
function pairList(r: Renderer, x: number, y: number, w: number, avail: number, rows: { left: string; right: string; sign: number }[]) {
  const n = rows.length;
  let font: FontId = SMALL_FONT_ID;
  for (const f of [N18, BODY_FONT, N12, CAPS_FONT, SMALL_FONT_ID]) {
    let widest = 0;
    for (const row of rows) {
      const tri = row.sign !== 0 ? Math.max(8, t(lineHeight(f) / 3)) + 6 : 0;
      widest = Math.max(widest, tri + textWidth(f, row.left, kBold) + 10 + textWidth(f, row.right, kBold));
    }
    font = f;
    if (widest <= w && n * (lineHeight(f) + 4) <= avail) break;
  }
  const lh = lineHeight(font);
  const step = Math.min(lh * 2, Math.max(lh + 4, t(avail / Math.max(1, n))));
  y += Math.max(0, t((step - lh - 4) / 2));
  for (const row of rows) {
    let lx = x;
    if (row.sign !== 0) {
      const ts = Math.max(8, t(lh / 3));
      triangle(r, x, y + t((ascender(font) * 2) / 3) - t(ts / 2), ts, row.sign > 0);
      lx += ts + 6;
    }
    r.drawText(font, lx, y, row.left, true, kBold);
    drawRight(r, font, x + w, y, row.right, kBold);
    y += step;
  }
}
const noData = (r: Renderer, b: Box, y: number) => wrap(r, SMALL_FONT, b.x + P + 6, y, b.w - 2 * P - 12, S.NO_DATA, 2);

function drawGold(r: Renderer, it: Item, b: Box, d: InkData) {
  const list = chosen(d.gold, codes(it.opt, "SJC,N24K"), (x) => x.code);
  const title = fmt(S.GOLD_CAPS, d.goldSrc || "PNJ");
  if (b.w >= 300 && b.h < H_LOW) {
    if (!anyData(list)) {
      marketHeader(r, b, title, d.goldAt);
      noData(r, b, b.y + P + lineHeight(CAPS_FONT) + 4);
      return;
    }
    const x = b.x + P + 6;
    const w = b.w - 2 * P - 12;
    const n = list.length;
    const cw = t(w / n);
    const capsH = lineHeight(CAPS_FONT);
    let vf: FontId = N12;
    for (const f of [N18, N16, N14, N12]) {
      if (capsH + lineHeight(f) > b.h - 10) continue;
      if (list.every((p) => textWidth(f, p.v ? num(p.v.sell, 1) : kDash, kBold) + 22 <= cw - 12)) { vf = f; break; }
    }
    const top = b.y + t((b.h - capsH - lineHeight(vf)) / 2);
    list.forEach((p, i) => {
      const cx = x + i * cw;
      if (i > 0) r.fillRect(cx - 8, b.y + 10, 2, b.h - 20, true);
      drawFit(r, CAPS_FONT, cx, top, p.v ? p.v.name : p.code, cw - 12);
      const sell = p.v ? num(p.v.sell, 1) : kDash;
      r.drawText(vf, cx, top + capsH, sell, true, kBold);
      if (p.v && p.v.change !== 0) {
        const ts = Math.max(8, t(lineHeight(vf) / 3));
        triangle(r, cx + textWidth(vf, sell, kBold) + 8, top + capsH + t((ascender(vf) * 2) / 3) - t(ts / 2), ts, p.v.change >= 0);
      }
    });
    return;
  }
  marketHeader(r, b, title, d.goldAt);
  const y0 = b.y + P + lineHeight(CAPS_FONT) + 4;
  if (!anyData(list)) return void noData(r, b, y0);
  const x = b.x + P + 6;
  const w = b.w - 2 * P - 12;
  const nameOf = (p: Pick<InkData["gold"][number]>) => (p.v ? p.v.name : p.code);
  if (b.w < 300 && list.length === 1) {
    const q = list[0].v!;
    drawFit(r, SMALL_FONT, x, y0, q.name, w, kBold);
    const sell = num(q.sell, 1);
    const dy = y0 + lineHeight(SMALL_FONT) + 2;
    let y = dy + heroNumber(r, sell, x, dy, w, t(b.h / 3)) + 6;
    drawFit(r, CAPS_FONT, x, y, `${S.BUY} ${num(q.buy, 1)} · ${S.GOLD_UNIT}`, w);
    y += lineHeight(CAPS_FONT);
    if (q.change !== 0 && y + lineHeight(CAPS_FONT) <= b.y + b.h - t(P / 2)) changeText(r, x, y, q.change, num(q.change, 1));
    return;
  }
  if (b.w < 300) {
    pairList(r, x, y0 + 2, w, b.y + b.h - t(P / 2) - y0, list.map((p) => ({ left: nameOf(p), right: p.v ? num(p.v.sell, 1) : kDash, sign: p.v ? signOf(p.v.change) : 0 })));
    return;
  }
  let y = y0 + lineHeight(CAPS_FONT);
  const rows = rowsFor(list.length, b.y + b.h - t(P / 2) - y);
  const nameW = Math.max(0, ...list.map((p) => textWidth(rows.font, nameOf(p), kBold)));
  const buyW = Math.max(0, ...list.map((p) => (p.v ? textWidth(rows.font, num(p.v.buy, 1)) : 0)));
  const sellW = Math.max(0, ...list.map((p) => (p.v ? textWidth(rows.font, num(p.v.sell, 1), kBold) : 0)));
  const colBuy = Math.max(x + t((w * 42) / 100), x + nameW + 14);
  const colSell = Math.max(x + t((w * 64) / 100), colBuy + buyW + 18);
  const colChg = Math.max(x + t((w * 86) / 100), colSell + sellW + 14);
  r.drawText(CAPS_FONT, colBuy, y0, S.BUY, true, kBold);
  r.drawText(CAPS_FONT, colSell, y0, S.SELL, true, kBold);
  for (const p of list) {
    drawFit(r, rows.font, x, y, nameOf(p), colBuy - x - 8, kBold);
    if (p.v) {
      r.drawText(rows.font, colBuy, y, num(p.v.buy, 1));
      r.drawText(rows.font, colSell, y, num(p.v.sell, 1), true, kBold);
      if (p.v.change !== 0) changeText(r, colChg, y + t((lineHeight(rows.font) - lineHeight(CAPS_FONT)) / 2), p.v.change, num(p.v.change, 1));
    } else {
      r.drawText(rows.font, colBuy, y, kDash);
      r.drawText(rows.font, colSell, y, kDash, true, kBold);
    }
    y += rows.h;
  }
}

function drawStocks(r: Renderer, it: Item, b: Box, d: InkData) {
  marketHeader(r, b, S.STOCKS_CAPS, d.stocksAt);
  const y0 = b.y + P + lineHeight(CAPS_FONT) + 4;
  const list = chosen(d.stocks, codes(it.opt, "VNINDEX,VN30,HNX"), (x) => x.sym);
  if (!anyData(list)) return void noData(r, b, y0);
  const x = b.x + P + 6;
  const w = b.w - 2 * P - 12;
  if (b.w < 300 && list.length === 1) {
    const tk = list[0].v!;
    drawFit(r, SMALL_FONT, x, y0, tk.sym, w, kBold);
    const price = num(tk.price, 2);
    const dy = y0 + lineHeight(SMALL_FONT) + 2;
    const hh = heroNumber(r, price, x, dy, w, t(b.h / 3));
    changeText(r, x, dy + hh + 8, tk.change, `${num(tk.change, 2)} (${num(tk.pct, 2)}%)`);
    return;
  }
  if (b.w < 300) {
    pairList(r, x, y0 + 2, w, b.y + b.h - t(P / 2) - y0, list.map((p) => ({ left: p.code || p.v!.sym, right: p.v ? num(p.v.price, 2) : kDash, sign: p.v ? signOf(p.v.change) : 0 })));
    return;
  }
  const colPrice = x + t((w * 34) / 100);
  let y = y0;
  const rows = rowsFor(list.length, b.y + b.h - t(P / 2) - y);
  const priceW = Math.max(0, ...list.map((p) => (p.v ? textWidth(rows.font, num(p.v.price, 2), kBold) : 0)));
  const colChg = Math.max(x + t((w * 66) / 100), colPrice + priceW + 14);
  for (const p of list) {
    drawFit(r, rows.font, x, y, p.code || p.v!.sym, colPrice - x - 8, kBold);
    if (p.v) {
      r.drawText(rows.font, colPrice, y, num(p.v.price, 2), true, kBold);
      changeText(r, colChg, y + t((lineHeight(rows.font) - lineHeight(CAPS_FONT)) / 2), p.v.change, `${num(p.v.pct, 2)}%`, CAPS_FONT);
    } else r.drawText(rows.font, colPrice, y, kDash, true, kBold);
    y += rows.h;
  }
}

function drawFx(r: Renderer, it: Item, b: Box, d: InkData) {
  marketHeader(r, b, b.w < 300 ? S.FX_SHORT : S.FX_CAPS, "");
  const y0 = b.y + P + lineHeight(CAPS_FONT) + 4;
  const list = chosen(d.fx, codes(it.opt, "USD,EUR,JPY,CNY"), (x) => x.code);
  if (!anyData(list)) return void noData(r, b, y0);
  const x = b.x + P + 6;
  const w = b.w - 2 * P - 12;
  const codeOf = (p: Pick<InkData["fx"][number]>) => p.code || p.v!.code;
  const f2 = (v: number) => num(v, v < 1000 ? 2 : 0);
  if (b.w < 300) {
    pairList(r, x, y0 + 2, w, b.y + b.h - t(P / 2) - y0, list.map((p) => ({ left: codeOf(p), right: p.v ? f2(p.v.sell) : kDash, sign: 0 })));
    return;
  }
  let y = y0;
  const colBuy = x + t((w * 30) / 100), colSell = x + t((w * 66) / 100);
  if (b.h >= 140) {
    r.drawText(CAPS_FONT, colBuy, y, S.BUY, true, kBold);
    r.drawText(CAPS_FONT, colSell, y, S.SELL, true, kBold);
    y += lineHeight(CAPS_FONT);
  }
  const rows = rowsFor(list.length, b.y + b.h - t(P / 2) - y);
  for (const p of list) {
    r.drawText(rows.font, x, y, codeOf(p), true, kBold);
    r.drawText(rows.font, colBuy, y, p.v ? f2(p.v.buy) : kDash);
    r.drawText(rows.font, colSell, y, p.v ? f2(p.v.sell) : kDash, true, kBold);
    y += rows.h;
  }
}

const usd = (v: number) => num(v, v >= 1000 ? 0 : v >= 1 ? 2 : 4);
const vnd = (v: number) => (v >= 1e9 ? `${num(v / 1e9, 2)} ${S.BILLION}` : v >= 1e6 ? `${num(v / 1e6, 1)} ${S.MILLION}` : num(v, 0));

function drawCrypto(r: Renderer, it: Item, b: Box, d: InkData) {
  marketHeader(r, b, S.CRYPTO_CAPS, d.cryptoAt);
  const y0 = b.y + P + lineHeight(CAPS_FONT) + 4;
  const list = chosen(d.crypto, codes(it.opt, "BTC,ETH"), (x) => x.sym);
  if (!anyData(list)) return void noData(r, b, y0);
  const x = b.x + P + 6;
  const w = b.w - 2 * P - 12;
  const symOf = (p: Pick<InkData["crypto"][number]>) => p.code || p.v!.sym;
  if (b.w < 300 && list.length === 1) {
    const c = list[0].v!;
    drawFit(r, SMALL_FONT, x, y0, c.sym, w, kBold);
    const p = usd(c.usd);
    const dy = y0 + lineHeight(SMALL_FONT) + 2;
    let y = dy + heroNumber(r, p, x, dy, w, t(b.h / 3)) + 6;
    drawFit(r, CAPS_FONT, x, y, `USD · ~${vnd(c.vnd)} đ`, w);
    y += lineHeight(CAPS_FONT);
    if (y + lineHeight(CAPS_FONT) <= b.y + b.h - t(P / 2)) changeText(r, x, y, c.pct, `${num(Math.abs(c.pct), 2)}% · 24h`);
    return;
  }
  if (b.w < 300) {
    pairList(r, x, y0 + 2, w, b.y + b.h - t(P / 2) - y0, list.map((p) => ({ left: symOf(p), right: p.v ? "$" + usd(p.v.usd) : kDash, sign: p.v ? signOf(p.v.pct) : 0 })));
    return;
  }
  const colUsd = x + t((w * 20) / 100), colVnd = x + t((w * 52) / 100), colChg = x + t((w * 80) / 100);
  let y = y0;
  const rows = rowsFor(list.length, b.y + b.h - t(P / 2) - y);
  for (const p of list) {
    r.drawText(rows.font, x, y, symOf(p), true, kBold);
    if (p.v) {
      drawFit(r, rows.font, colUsd, y, "$" + usd(p.v.usd), colVnd - colUsd - 8, kBold);
      drawFit(r, rows.font, colVnd, y, vnd(p.v.vnd), colChg - colVnd - 8);
      changeText(r, colChg, y + t((lineHeight(rows.font) - lineHeight(CAPS_FONT)) / 2), p.v.pct, `${num(Math.abs(p.v.pct), 2)}%`);
    } else r.drawText(rows.font, colUsd, y, kDash, true, kBold);
    y += rows.h;
  }
}

// ---- library and photos --------------------------------------------------------------------
interface Shelf { ch: number; titles: boolean; feature: boolean; card?: boolean }
export function shelfOf(b: Box, count: number): Shelf {
  const CAPS_H = 24, SMALL_H = 19;
  const base = b.h - P - CAPS_H - 6 - 12 - t(P / 2);
  const withTitles = Math.min(150, base - CAPS_H * 2 - (b.h >= H_HUGE ? SMALL_H * 2 + 4 : 0));
  const s: Shelf = t((withTitles * 2) / 3) >= 80 ? { ch: withTitles, titles: true, feature: false } : { ch: Math.max(40, Math.min(150, base)), titles: false, feature: false };
  const w = b.w - 2 * P - 12;
  const per = Math.max(1, t((w + 10) / (t((s.ch * 2) / 3) + 10)));
  if (count > 0 && count <= 2 && count < per) {
    const ch = b.h - P - CAPS_H - 6 - P;
    const colW = t((w - (count - 1) * 16) / count);
    if (colW - t((ch * 2) / 3) - 12 >= 80) return { ch, titles: true, feature: true };
    if (count === 1 && b.h >= H_BIG) {
      const TEXT_H = 45 * 2 + CAPS_H + 16;
      const cw = Math.min(t((w * 70) / 100), t(((ch - TEXT_H) * 2) / 3));
      if (cw >= 60) return { ch: t((cw * 3) / 2), titles: true, feature: false, card: true };
    }
  }
  return s;
}
const bookState = (p: number) => (p >= 100 ? S.BOOK_DONE : p <= 0 ? S.BOOK_NEW : `${p}%`);
export function booksOf(it: Item, d: InkData) {
  const skip = it.opt.includes("s");
  return d.library.filter((bk, i) => !(skip && i === 0 && d.hasBook && bk.path === d.bookPath));
}
function drawBooks(r: Renderer, it: Item, b: Box, d: InkData) {
  const x = b.x + P + 6;
  const w = b.w - 2 * P - 12;
  const list = booksOf(it, d);
  const count = list.length;
  caps(r, x, b.y + P, S.BOOKS_CAPS);
  if (b.w >= 300) drawRight(r, CAPS_FONT, b.x + b.w - P, b.y + P, S.BOOKS_RECENT);
  let y = b.y + P + lineHeight(CAPS_FONT) + 6;
  if (!count) {
    centerText(r, SMALL_FONT, x, y, w, b.y + b.h - P - y, S.BOOKS_EMPTY, kReg);
    return;
  }
  const mark = (i: number, mx: number, my: number, mw: number, mh: number) => {
    if (i === d.bookFocus) r.drawRoundedRect(mx - 5, my - 5, mw + 10, mh + 10, 3, 8, true);
  };
  if (it.style !== "list") {
    const sh = shelfOf(b, count);
    const ch = sh.ch;
    const cw = t((ch * 2) / 3);
    if (sh.card) {
      const bk = list[0];
      const cx = b.x + t((b.w - cw) / 2);
      drawCoverAt(r, bk.cover, cx, y, cw, ch);
      mark(0, cx, y, cw, ch);
      let ty = y + ch + 8;
      const bottom = b.y + b.h - P;
      const barY = bottom - 12;
      const stY = barY - lineHeight(CAPS_FONT) - 2;
      ty += wrapCentered(r, N16, x, ty, w, stY - 4 - ty, bk.title, true, kBold);
      drawFitCentered(r, CAPS_FONT, b.x + t(b.w / 2), stY, bookState(bk.percent), w, kBold);
      if (bk.percent >= 0) bar(r, x, barY, w, 12, Math.min(100, bk.percent), true);
      return;
    }
    if (sh.feature) {
      const colW = t((w - (count - 1) * 16) / count);
      list.forEach((bk, i) => {
        const cx = x + i * (colW + 16);
        drawCoverAt(r, bk.cover, cx, y, cw, ch);
        mark(i, cx, y, cw, ch);
        const tx = cx + cw + 12;
        const tw = colW - cw - 12;
        const barY = y + ch - 12;
        const stY = barY - lineHeight(CAPS_FONT) - 2;
        if (bk.percent >= 0) bar(r, tx, barY, tw, 12, Math.min(100, bk.percent), true);
        drawFit(r, CAPS_FONT, tx, stY, bookState(bk.percent), tw, kBold);
        const authorH = !bk.author || textWidth(CAPS_FONT, bk.author) > tw ? 0 : lineHeight(CAPS_FONT);
        const room = stY - 6 - y - authorH;
        const used = wrapIn(r, N16, tx, y, tw, room, bk.title, true, kBold);
        if (authorH) r.drawText(CAPS_FONT, tx, y + used + 2, bk.author, true);
      });
      return;
    }
    const per = Math.max(1, t((w + 10) / (cw + 10)));
    const n = Math.min(per, count);
    for (let i = 0; i < n; i++) {
      const bk = list[i];
      const cx = x + i * (cw + 10);
      drawCoverAt(r, bk.cover, cx, y, cw, ch);
      mark(i, cx, y, cw, ch);
      let ty = y + ch + 3;
      if (bk.percent >= 0) bar(r, cx, ty, cw, 8, Math.min(100, bk.percent), true);
      ty += 12;
      if (!sh.titles) continue;
      const titleH = lineHeight(CAPS_FONT) * 2;
      const { used } = wrapInFont(CAPS_FONT, cw, titleH, bk.title, kBold);
      if (used <= titleH) ty += wrapIn(r, CAPS_FONT, cx, ty, cw, titleH, bk.title, true, kBold);
      if (b.h >= H_HUGE) {
        if (bk.author && textWidth(SMALL_FONT_ID, bk.author) <= cw) {
          r.drawText(SMALL_FONT_ID, cx, ty, bk.author, true);
          ty += lineHeight(SMALL_FONT_ID);
        }
        drawFit(r, SMALL_FONT_ID, cx, ty, bookState(bk.percent), cw);
      }
    }
    return;
  }
  const bottom = b.y + b.h - t(P / 2);
  const moreH = lineHeight(CAPS_FONT) + 2;
  let stateW = 0;
  for (const bk of list) stateW = Math.max(stateW, textWidth(CAPS_FONT, bookState(bk.percent), kBold));
  const titleW = w - stateW - 10;
  const first = b.w >= 300 ? (count <= 3 ? N16 : BODY_FONT) : count <= 2 ? BODY_FONT : SMALL_FONT;
  let font: FontId | 0 = 0;
  for (const lines of [2, 3]) {
    for (let cand: FontId | 0 = first; cand && !font; cand = smallerFont(cand)) {
      let ok = true;
      for (let i = 0; i < count && i < 8 && ok; i++) ok = wrapInFont(cand, titleW, lineHeight(cand) * lines, list[i].title, kBold).font === cand;
      if (ok) font = cand;
    }
    if (font) break;
  }
  const f: FontId = font || SMALL_FONT_ID;
  const authorH = lineHeight(CAPS_FONT);
  const authorLine = b.h >= H_BIG && count * (lineHeight(f) + authorH + 8) <= bottom - y;
  let shown = 0;
  for (let i = 0; i < count; i++) {
    const bk = list[i];
    const { used } = wrapInFont(f, titleW, lineHeight(f) * 3, bk.title, kBold);
    const authorBelow = authorLine && !!bk.author;
    const rowH = Math.max(used, lineHeight(f)) + (authorBelow ? authorH : 0) + 6;
    const reserve = i < count - 1 ? moreH : 0;
    if (shown > 0 && y + rowH + reserve > bottom) break;
    const oneLine = used <= lineHeight(f);
    const titleAuthor = `${bk.title} · ${bk.author}`;
    if (!authorLine && b.w >= 300 && oneLine && bk.author && textWidth(f, titleAuthor, kBold) <= titleW) r.drawText(f, x, y, titleAuthor, true, kBold);
    else {
      const th = wrapIn(r, f, x, y, titleW, lineHeight(f) * 3, bk.title, true, kBold);
      if (authorBelow) drawFit(r, CAPS_FONT, x, y + th, bk.author, titleW);
    }
    drawRight(r, CAPS_FONT, x + w, y + t((lineHeight(f) - lineHeight(CAPS_FONT)) / 2), bookState(bk.percent), kBold);
    r.fillRect(x, y + rowH - 3, w, 1, true);
    mark(i, x, y, w, rowH - 6);
    y += rowH;
    shown++;
  }
  if (shown < count) drawFit(r, CAPS_FONT, x, Math.min(y, bottom - moreH), fmt(S.MORE_BOOKS, count - shown), w, kBold);
}

function photoIds(opt: string): number[] {
  const bar = opt.indexOf("|");
  if (bar !== 6) return [];
  return opt.slice(bar + 1).split(",").map((s) => parseInt(s, 10)).filter((n) => n > 0);
}
function drawPhoto(r: Renderer, it: Item, b: Box, d: InkData) {
  const ids = photoIds(it.opt);
  const src = ids.map((id) => d.photos[id]).find(Boolean);
  if (src) {
    r.image(b.x, b.y, b.w, b.h, src, it.opt[2] === "n");
    return;
  }
  wrapIn(r, SMALL_FONT, b.x + P + 6, b.y + P + 4, b.w - 2 * P - 12, b.h - 2 * P, ids.length ? S.PHOTO_WAIT : S.PHOTO_EMPTY, true, kBold);
}

function drawOverview(r: Renderer, _it: Item, b: Box, d: InkData) {
  const tm = timeText(d);
  const capsH = lineHeight(CAPS_FONT);
  const tall = b.h >= H_LOW;
  const top = tall ? b.y + P : b.y + 8;
  const topH = tall ? t(((b.h - 2 * P) * 58) / 100) : b.h - 16;
  const right = b.x + b.w - P;
  let x = b.x + P;
  const f = digitsFontOr0(tm, t(((b.w - 2 * P) * 40) / 100), topH) || I30;
  drawDigits(r, f, x, top + t((topH - digitHeight(f)) / 2), tm);
  x += textWidth(f, tm, kBold) + 14;
  r.fillRect(x - 2, top + 6, 2, topH - 12, true);
  x += 12;
  let wx = right;
  if (d.hasWeather) {
    const temp = `${d.temp}°`;
    const s = Math.min(topH, 52);
    const tf = digitHeight(I30) <= topH ? I30 : 0;
    const tw = tf ? textWidth(tf, temp, kBold) : textWidth(N18, temp, kBold);
    const left = right - s - 6 - tw;
    if (left - x >= 110) {
      drawIcon(r, d.icon, left + t(s / 2), top + t(topH / 2), s);
      if (tf) drawDigits(r, tf, left + s + 6, top + t((topH - digitHeight(tf)) / 2), temp);
      else r.drawText(N18, left + s + 6, top + t((topH - lineHeight(N18)) / 2), temp, true, kBold);
      wx = left - 12;
    }
  }
  dateColumn(r, d, x, top, wx - x, topH);
  if (!tall) return;
  const ry = top + topH + 6;
  r.fillRect(b.x + P, ry, b.w - 2 * P, 2, true);
  const by = ry + 2;
  const bh = b.y + b.h - t(P / 2) - by;
  const pctT = `${d.battery}%`;
  const pw = textWidth(CAPS_FONT, pctT, kBold);
  const iconW = 30, iconH = 15;
  const bx = right - pw - 8 - iconW - 3;
  const bcy = by + t(bh / 2);
  r.drawRect(bx, bcy - t(iconH / 2), iconW, iconH, 2, true);
  r.fillRect(bx + iconW, bcy - t(iconH / 2) + 4, 3, iconH - 8, true);
  const fill = t(((iconW - 6) * Math.min(100, Math.max(0, d.battery))) / 100);
  if (fill > 0) r.fillRect(bx + 3, bcy - t(iconH / 2) + 3, fill, iconH - 6, true);
  r.drawText(CAPS_FONT, right - pw, bcy - t(capsH / 2), pctT, true, kBold);
  const ex = b.x + P;
  const ew = bx - 16 - ex;
  let next = -1;
  if (d.timeValid) {
    const nowMin = d.now.hour * 60 + d.now.min;
    next = d.events.findIndex((e) => minutesOfDay(e.time, -1) >= nowMin);
  }
  const ef = BODY_FONT;
  const ey = by + t((bh - lineHeight(ef)) / 2);
  if (next >= 0) {
    const e = d.events[next];
    const tw = textWidth(ef, e.time, kBold);
    r.drawText(ef, ex, ey, e.time, true, kBold);
    drawFit(r, ef, ex + tw + 12, ey, e.title, ew - tw - 12, kReg);
  } else drawFit(r, SMALL_FONT, ex, by + t((bh - lineHeight(SMALL_FONT)) / 2), S.NO_MORE_EVENTS, ew, kBold);
}

function drawContent(r: Renderer, it: Item, b: Box, d: InkData) {
  switch (it.kind) {
    case "clock": return drawClock(r, it, b, d);
    case "date": return drawDate(r, it, b, d);
    case "month": return drawMonth(r, it, b, d);
    case "events": return drawEvents(r, it, b, d);
    case "countdown": return drawCountdown(r, it, b, d);
    case "weather": return drawWeather(r, it, b, d);
    case "reading": return drawReading(r, it, b, d);
    case "stats": return drawStats(r, it, b, d);
    case "news": return drawNews(r, it, b, d);
    case "notify": return drawNotify(r, it, b, d);
    case "battery": return drawBattery(r, it, b, d);
    case "gold": return drawGold(r, it, b, d);
    case "stocks": return drawStocks(r, it, b, d);
    case "fx": return drawFx(r, it, b, d);
    case "crypto": return drawCrypto(r, it, b, d);
    case "books": return drawBooks(r, it, b, d);
    case "photo": return drawPhoto(r, it, b, d);
    case "overview": return drawOverview(r, it, b, d);
  }
}

let clipSeq = 0;
/** One widget as the device draws it (Widgets.cpp draw()). */
export function drawWidget(r: Renderer, it: Item, b: Box, d: InkData, focused: boolean, skin: Skin) {
  gSkin = skin;
  if (skin === "shadow") {
    r.fillRect(b.x + SHADOW, b.y + SHADOW, b.w, b.h, true);
    r.fillRoundedRect(b.x, b.y, b.w, b.h, RADIUS, false);
  }
  if (it.kind === "app") return drawApp(r, it, b, focused);
  const sheet = it.kind === "date" && it.style !== "compact" && b.h >= H_LOW;
  if (it.inverted) {
    // White on black: drawn plain, then flipped (pictures flipped back), the corners cut as on the device.
    const id = `ic${++clipSeq}`;
    const rad = rounded(skin) || skin === "bare" ? RADIUS : 0;
    r.raw(`<clipPath id="${id}"><rect x="${b.x}" y="${b.y}" width="${b.w}" height="${b.h}" rx="${rad}"/></clipPath>`);
    r.raw(`<g clip-path="url(#${id})"><g style="filter:invert(1)"><rect x="${b.x}" y="${b.y}" width="${b.w}" height="${b.h}" fill="#eeede8"/>`);
    const before = r.parts.length;
    drawContent(r, it, b, d);
    // Pictures keep their tones.
    for (let i = before; i < r.parts.length; i++) {
      if (r.parts[i].startsWith("<image")) r.parts[i] = r.parts[i].replace('style="filter:', 'style="filter:invert(1) ');
    }
    r.raw("</g></g>");
    if (focused) r.drawRoundedRect(b.x - 4, b.y - 4, b.w + 8, b.h + 8, 2, RADIUS + 4, true);
    return;
  }
  if (it.kind === "photo") {
    const id = `pc${++clipSeq}`;
    r.raw(`<clipPath id="${id}"><rect x="${b.x}" y="${b.y}" width="${b.w}" height="${b.h}" rx="${rounded(skin) ? RADIUS : 0}"/></clipPath><g clip-path="url(#${id})">`);
    drawContent(r, it, b, d);
    r.raw("</g>");
    frame(r, b, focused);
    return;
  }
  if (!sheet) frame(r, b, focused);
  drawContent(r, it, b, d);
}

// ---- screen chrome (DashboardView.cpp drawStatusBar / drawFooter, Home's status and hints) --------
export function drawStatusBar(r: Renderer, w: number, d: InkData, clock: boolean, opts: { battery: number; side?: number }) {
  const H = 40;
  const side = opts.side ?? 20;
  const x = side + 4;
  // iu::battery: icon and percentage at the right.
  const pct = `${opts.battery}%`;
  const rightX = w - side - 4;
  const tw = textWidth(U10B, pct, kBold);
  const bodyW = 26, bodyH = 13;
  const bx = rightX - tw - 8 - bodyW - 3;
  const by = t(H / 2) + 2 - t(bodyH / 2);
  r.drawRect(bx, by, bodyW, bodyH, 2, true);
  r.fillRect(bx + bodyW, by + 3, 3, bodyH - 6, true);
  const fill = t(((bodyW - 6) * Math.max(0, Math.min(100, opts.battery))) / 100);
  if (fill > 0) r.fillRect(bx + 3, by + 3, fill, bodyH - 6, true);
  r.drawText(U10B, rightX - tw, t(H / 2) + 2 - t(lineHeight(U10B) / 2), pct, true, kBold);
  if (!d.timeValid) return;
  const date = `${WEEKDAY_CAPS[d.now.wday]} · ${String(d.now.mday).padStart(2, "0")}/${String(d.now.mon + 1).padStart(2, "0")}`;
  const cf = N14;
  const cy = t((H - lineHeight(cf)) / 2) + 2;
  if (!clock) {
    drawFit(r, cf, x, cy, date, bx - 14 - x, kBold);
    return;
  }
  const hour = d.h12 ? (d.now.hour % 12 === 0 ? 12 : d.now.hour % 12) : d.now.hour;
  const tm = `${String(hour).padStart(2, "0")}:${String(d.now.min).padStart(2, "0")}`;
  r.drawText(cf, x, cy, tm, true, kBold);
  const baseline = cy + ascender(cf);
  const dx = x + textWidth(cf, tm, kBold) + 12;
  drawFit(r, U10B, dx, baseline - ascender(U10B), date, bx - 14 - dx, kBold);
}
const U10B = CAPS_FONT;

export function drawSleepFooter(r: Renderer, w: number, h: number, name: string, dataAt: string, next: string) {
  const FOOTER_H = 40;
  const y = h - FOOTER_H;
  r.fillRect(0, y, w, 2, true);
  const lineH = lineHeight(CAPS_FONT);
  const textY = y + t((FOOTER_H - lineH) / 2) + 1;
  const left = 24;
  const text = next ? `Dữ liệu lúc ${dataAt} · lần tới ${next}` : `Dữ liệu lúc ${dataAt}`;
  const tw = textWidth(CAPS_FONT, text, kBold);
  const rightX = w - left;
  r.drawText(CAPS_FONT, rightX - tw, textY, text, true, kBold);
  if (name) drawFit(r, CAPS_FONT, left, textY, name, rightX - tw - 20 - left, kBold);
}

/** Home's button hints (the RoundedRaff theme's two pills: back and open, up and down). */
export function drawHomeHints(r: Renderer, w: number, h: number, labels: string[]) {
  const side = 20, gap = 10, bottom = 10, hh = 30;
  const gw = t((w - side * 2 - gap) / 2);
  const y = h - hh - bottom;
  const ty = y + t((hh - lineHeight(SMALL_FONT_ID)) / 2);
  const pad = 16;
  [0, 1].forEach((g) => {
    const gx = side + g * (gw + gap);
    r.fillRect(gx, y, gw, hh, false);
    r.drawRoundedRect(gx, y, gw, hh, 2, 15, true);
    const a = labels[g * 2] ?? "", b = labels[g * 2 + 1] ?? "";
    if (a) r.drawText(SMALL_FONT_ID, gx + pad, ty, a, true, kReg);
    if (b) r.drawText(SMALL_FONT_ID, gx + gw - pad - textWidth(SMALL_FONT_ID, b, kReg), ty, b, true, kReg);
  });
}

export { I24 as _I24, I44 as _I44, N12 as _N12, N14 as _N14, N18 as _N18, S36 as _S36 };
