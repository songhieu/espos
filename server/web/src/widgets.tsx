// Widget catalogue and e-ink previews for the layout editor. The previews follow the X4's own
// drawing (firmware src/inkdesk/Widgets.cpp) closely enough to arrange a screen; the device
// redraws everything natively in 1-bit.
import { useMemo } from "react";

import { Renderer } from "./ink/render";
import { drawWidget, type InkData } from "./ink/widgets";
import { fromSolar, yearName } from "./lunar";
import type { Dash, LayoutItem, Markets, NewsIssue, Stats } from "./inkdesk";

export type Kind =
  | "clock" | "date" | "month" | "events" | "countdown" | "weather" | "reading" | "stats" | "news" | "notify"
  | "battery" | "app" | "gold" | "stocks" | "fx" | "crypto" | "books" | "photo" | "overview";

/** What each market widget shows when nothing is chosen (as on the device). */
export const DEFAULT_CODES: Record<string, string> = { gold: "SJC,N24K", fx: "USD,EUR,JPY,CNY", stocks: "VNINDEX,VN30,HNX", crypto: "BTC,ETH" };
export const COINS = ["BTC", "ETH", "BNB", "SOL", "XRP", "DOGE", "TON"];

export interface KindInfo {
  name: string;
  cat: string;
  sizes: [number, number][];
  styles: { id: string; label: string }[];
  opt?: "tickers" | "countdown" | "gold" | "fx" | "coins" | "photo" | "books";
  hint?: string;
  /** Opens something when pressed: only on the Home screen (the sleep screens are only looked at). */
  homeOnly?: boolean;
}

export const CATS = ["Đồng hồ", "Lịch", "Thời tiết", "Đọc sách", "Ảnh", "Thị trường", "Tiện ích"];

export const KINDS: Record<Kind, KindInfo> = {
  overview: { name: "Tổng quan", cat: "Đồng hồ", sizes: [[4, 1], [4, 2]], styles: [{ id: "", label: "Giờ · ngày · thời tiết · lịch" }] },
  clock: {
    name: "Đồng hồ", cat: "Đồng hồ",
    sizes: [[2, 1], [2, 2], [4, 2], [4, 3], [2, 3], [2, 4], [3, 3], [4, 4]],
    styles: [
      { id: "big", label: "Số lớn" }, { id: "flip", label: "Lật" }, { id: "analog", label: "Kim cổ điển" },
      { id: "analog_min", label: "Kim tối giản" }, { id: "words", label: "Chữ" }, { id: "lunar", label: "Số + âm lịch" },
    ],
  },
  date: { name: "Ngày âm dương", cat: "Lịch", sizes: [[2, 2], [2, 1]], styles: [{ id: "sheet", label: "Tờ lịch" }, { id: "compact", label: "Gọn" }] },
  month: { name: "Lịch tháng", cat: "Lịch", sizes: [[4, 3], [4, 4], [3, 3]], styles: [{ id: "lunar", label: "Có âm lịch" }, { id: "solar", label: "Chỉ dương lịch" }] },
  events: { name: "Lịch hôm nay", cat: "Lịch", sizes: [[4, 2], [4, 3], [2, 2]], styles: [{ id: "", label: "Danh sách" }] },
  countdown: {
    name: "Đếm ngược", cat: "Lịch", sizes: [[2, 2], [2, 1], [4, 1]],
    styles: [{ id: "tet", label: "Tết" }, { id: "date", label: "Ngày tự chọn" }], opt: "countdown",
  },
  weather: {
    name: "Thời tiết", cat: "Thời tiết", sizes: [[2, 2], [2, 1], [4, 1], [4, 2]],
    styles: [{ id: "now", label: "Hiện tại" }, { id: "hourly", label: "Theo giờ" }, { id: "days", label: "3 ngày" }],
  },
  reading: { name: "Đang đọc", cat: "Đọc sách", sizes: [[4, 2], [2, 2], [4, 3], [4, 4]], styles: [{ id: "cover", label: "Có bìa" }, { id: "text", label: "Chỉ chữ" }], hint: "Đọc tiếp" },
  stats: { name: "Thống kê đọc", cat: "Đọc sách", sizes: [[2, 2], [4, 2]], styles: [{ id: "ring", label: "Vòng mục tiêu" }, { id: "week", label: "7 ngày" }], hint: "Thống kê đọc" },
  news: { name: "Báo sáng", cat: "Đọc sách", sizes: [[4, 2], [4, 3]], styles: [{ id: "", label: "Tin đầu" }], hint: "Đọc số mới nhất" },
  books: { name: "Thư viện sách", cat: "Đọc sách", sizes: [[4, 2], [4, 3], [2, 2], [2, 4]], styles: [{ id: "covers", label: "Có bìa" }, { id: "list", label: "Danh sách" }], hint: "Mở cuốn đang chọn", opt: "books" },
  photo: { name: "Hình ảnh", cat: "Ảnh", sizes: [[4, 3], [4, 2], [2, 2], [2, 3], [2, 4], [4, 4], [4, 5]], styles: [{ id: "", label: "Ảnh của bạn" }], opt: "photo" },
  gold: { name: "Giá vàng", cat: "Thị trường", sizes: [[2, 2], [4, 1], [4, 2], [4, 3]], styles: [{ id: "", label: "PNJ" }], opt: "gold" },
  stocks: { name: "Chứng khoán", cat: "Thị trường", sizes: [[2, 2], [4, 2], [4, 3]], styles: [{ id: "", label: "Danh sách mã" }], opt: "tickers" },
  fx: { name: "Tỷ giá", cat: "Thị trường", sizes: [[2, 2], [4, 2], [4, 3]], styles: [{ id: "", label: "Vietcombank" }], opt: "fx" },
  crypto: { name: "Tiền số", cat: "Thị trường", sizes: [[2, 2], [4, 2], [4, 3]], styles: [{ id: "", label: "CoinGecko" }], opt: "coins" },
  notify: { name: "Thông báo", cat: "Tiện ích", sizes: [[4, 2], [4, 1]], styles: [{ id: "", label: "Mới nhất" }] },
  battery: { name: "Pin và đồng bộ", cat: "Tiện ích", sizes: [[2, 1]], styles: [{ id: "", label: "Gọn" }] },
  app: {
    name: "Lối tắt ứng dụng", cat: "Tiện ích", sizes: [[1, 1]], homeOnly: true,
    styles: [
      { id: "library", label: "Thư viện" }, { id: "news", label: "Báo sáng" },
      { id: "stats", label: "Thống kê" }, { id: "sync", label: "Đồng bộ" }, { id: "transfer", label: "Truyền file" },
      { id: "apps", label: "Ứng dụng" }, { id: "settings", label: "Cài đặt" }, { id: "update", label: "Cập nhật" },
    ],
  },
};

export type Screen = "home" | "wl" | "wp";
export const SCREENS: Record<Screen, { label: string; key: string; cols: number; rows: number; w: number; h: number }> = {
  home: { label: "Màn chính", key: "home.w", cols: 4, rows: 7, w: 480, h: 800 },
  wl: { label: "Màn chờ · ngang", key: "sleep.wl", cols: 6, rows: 4, w: 800, h: 480 },
  wp: { label: "Màn chờ · dọc", key: "sleep.wp", cols: 4, rows: 7, w: 480, h: 800 },
};

/** Frame styles: one per screen, drawn the same by the device (layout key "k"). */
export type Skin = "card" | "line" | "bare" | "bold" | "shadow";
export const SKINS: { id: Skin; name: string; desc: string }[] = [
  { id: "card", name: "Thẻ bo góc", desc: "Viền mảnh, góc bo" },
  { id: "line", name: "Giấy kẻ", desc: "Không khung, chỉ đường kẻ ngang" },
  { id: "bare", name: "Trơn", desc: "Không viền, chỉ khoảng trắng" },
  { id: "bold", name: "Viền đậm", desc: "Viền dày, góc vuông" },
  { id: "shadow", name: "Khối nổi", desc: "Viền kèm bóng đổ" },
];
/** The shadow skin draws each widget this much smaller, the shadow in the space left. */
export const SHADOW = 5;

const A = (y: number, ...apps: string[]): LayoutItem[] => apps.map((s, i) => ["app", i, y, 1, 1, s, ""] as LayoutItem);

/** What each screen shows before anything is set (the device's built-in layouts). */
export const DEFAULT_LAYOUT: Record<Screen, LayoutItem[]> = {
  home: [["weather", 0, 0, 4, 1, "now", ""], ["reading", 0, 1, 4, 2, "cover", ""], ["events", 0, 3, 4, 2, "", ""], ...A(5, "library", "news", "stats", "sync"), ...A(6, "transfer", "update", "apps", "settings")],
  wl: [["clock", 0, 0, 4, 3, "flip", ""], ["date", 4, 0, 2, 2, "sheet", ""], ["weather", 4, 2, 2, 2, "now", ""], ["countdown", 0, 3, 4, 1, "tet", ""]],
  wp: [["clock", 0, 0, 4, 2, "lunar", ""], ["weather", 0, 2, 4, 2, "hourly", ""], ["events", 0, 4, 4, 3, "", ""]],
};

export interface Template { screen: Screen; name: string; desc: string; skin: Skin; items: LayoutItem[] }
/** Ready-made layouts: practical ones for Home (the status bar shows the time), look-only ones for the sleep screens. */
export const TEMPLATES: Template[] = [
  { screen: "home", name: "Kệ sách", desc: "Cuốn đang đọc và kệ bìa sách ngay dưới.", skin: "card",
    items: [["reading", 0, 0, 4, 2, "cover", ""], ["books", 0, 2, 4, 3, "covers", "s"], ["weather", 0, 5, 4, 1, "now", ""], ...A(6, "library", "news", "stats", "apps")] },
  { screen: "home", name: "Tối giản", desc: "Một cuốn sách thật to và danh sách sách.", skin: "bare",
    items: [["reading", 0, 0, 4, 4, "cover", ""], ["books", 0, 4, 4, 2, "list", "s"], ...A(6, "library", "news", "apps", "settings")] },
  { screen: "home", name: "Tạp chí", desc: "Tin đầu trang như trang báo, kẻ ngang.", skin: "line",
    items: [["news", 0, 0, 4, 3, "", ""], ["reading", 0, 3, 4, 2, "text", ""], ["weather", 0, 5, 4, 1, "now", ""], ...A(6, "news", "library", "stats", "apps")] },
  { screen: "home", name: "Điểm nhấn đen", desc: "Sách đang đọc trên nền đen nổi bật.", skin: "card",
    items: [["reading", 0, 0, 4, 2, "cover", "", "i"], ["books", 0, 2, 4, 2, "list", "s"], ["weather", 0, 4, 2, 2, "now", ""], ["stats", 2, 4, 2, 2, "ring", ""], ...A(6, "library", "news", "stats", "apps")] },
  { screen: "home", name: "Ô vuông", desc: "Lưới ô vuông viền đậm, rõ ràng.", skin: "bold",
    items: [["reading", 0, 0, 4, 2, "cover", ""], ["stats", 0, 2, 2, 2, "ring", ""], ["weather", 2, 2, 2, 2, "now", ""], ["news", 0, 4, 4, 2, "", ""], ...A(6, "library", "news", "stats", "apps")] },
  { screen: "home", name: "Khối nổi", desc: "Các khối có bóng đổ, kệ bìa sách ở giữa.", skin: "shadow",
    items: [["reading", 0, 0, 2, 2, "cover", ""], ["stats", 2, 0, 2, 2, "ring", ""], ["books", 0, 2, 4, 2, "covers", "s"], ["events", 0, 4, 4, 2, "", ""], ...A(6, "library", "sync", "apps", "settings")] },
  { screen: "home", name: "Ảnh kỷ niệm", desc: "Ảnh bạn thích ở đầu màn, sách đang đọc bên dưới.", skin: "card",
    items: [["photo", 0, 0, 4, 3, "", "rhca50|"], ["reading", 0, 3, 4, 2, "cover", ""], ["weather", 0, 5, 4, 1, "now", ""], ...A(6, "library", "news", "stats", "apps")] },
  { screen: "home", name: "Nhà đầu tư", desc: "Đọc sách xen giá vàng và chứng khoán.", skin: "line",
    items: [["reading", 0, 0, 4, 2, "text", ""], ["gold", 0, 2, 4, 2, "", "SJC,N24K,KB"], ["stocks", 0, 4, 4, 2, "", "VNINDEX,VN30,FPT"], ...A(6, "library", "news", "stats", "apps")] },
  { screen: "home", name: "Văn phòng", desc: "Lịch họp trên nền đen, thông báo, sách giờ nghỉ.", skin: "bold",
    items: [["events", 0, 0, 4, 3, "", "", "i"], ["reading", 0, 3, 4, 2, "text", ""], ["notify", 0, 5, 4, 1, "", ""], ...A(6, "library", "sync", "apps", "settings")] },
  { screen: "home", name: "Lối tắt", desc: "Sách, tin và đủ 8 ứng dụng trong tầm tay.", skin: "card",
    items: [["reading", 0, 0, 4, 2, "cover", ""], ["news", 0, 2, 4, 2, "", ""], ["weather", 0, 4, 4, 1, "now", ""], ...A(5, "library", "news", "stats", "sync"), ...A(6, "transfer", "update", "apps", "settings")] },
  { screen: "wp", name: "Kim cổ điển", desc: "Mặt kim cổ điển, tờ lịch xé và thời tiết.", skin: "line",
    items: [["clock", 0, 0, 4, 4, "analog", ""], ["date", 0, 4, 2, 2, "sheet", ""], ["weather", 2, 4, 2, 2, "now", ""], ["countdown", 0, 6, 4, 1, "tet", ""]] },
  { screen: "wp", name: "Âm lịch", desc: "Giờ âm lịch trên nền đen, lịch tháng âm dương.", skin: "card",
    items: [["clock", 0, 0, 4, 2, "lunar", "", "i"], ["month", 0, 2, 4, 4, "lunar", ""], ["countdown", 0, 6, 4, 1, "tet", ""]] },
  { screen: "wp", name: "Ảnh dọc", desc: "Ảnh hoa, lá hay người thân, kèm giờ và ngày.", skin: "card",
    items: [["photo", 0, 0, 4, 5, "", "rhca50|"], ["clock", 0, 5, 2, 2, "big", ""], ["date", 2, 5, 2, 2, "sheet", ""]] },
  { screen: "wp", name: "Ngày của tôi", desc: "Giờ, thời tiết theo giờ, lịch hôm nay.", skin: "shadow",
    items: [["clock", 0, 0, 4, 2, "lunar", ""], ["weather", 0, 2, 4, 2, "hourly", ""], ["events", 0, 4, 4, 3, "", ""]] },
  { screen: "wp", name: "Đọc tiếp", desc: "Cuốn đang đọc thật to để nhớ đọc tiếp.", skin: "card",
    items: [["clock", 0, 0, 4, 2, "big", "", "i"], ["reading", 0, 2, 4, 3, "cover", ""], ["stats", 0, 5, 4, 2, "week", ""]] },
  { screen: "wl", name: "Đồng hồ lật", desc: "Đồng hồ lật cỡ lớn, tờ lịch, thời tiết.", skin: "bare",
    items: [["clock", 0, 0, 4, 3, "flip", ""], ["date", 4, 0, 2, 2, "sheet", ""], ["weather", 4, 2, 2, 2, "now", ""], ["countdown", 0, 3, 4, 1, "tet", ""]] },
  { screen: "wl", name: "Khung ảnh", desc: "Ảnh bạn chọn chiếm gần hết màn, giờ ở góc.", skin: "bare",
    items: [["photo", 0, 0, 4, 4, "", "rhca50|"], ["clock", 4, 0, 2, 2, "big", "", "i"], ["date", 4, 2, 2, 2, "sheet", ""]] },
  { screen: "wl", name: "Giờ bằng chữ", desc: "Giờ viết bằng chữ, nhiều khoảng trắng.", skin: "bare",
    items: [["clock", 0, 0, 4, 4, "words", ""], ["date", 4, 0, 2, 2, "sheet", ""], ["weather", 4, 2, 2, 2, "now", ""]] },
  { screen: "wl", name: "Lịch để bàn", desc: "Tờ lịch, lịch tháng, đếm ngày tới Tết.", skin: "bold",
    items: [["date", 0, 0, 2, 2, "sheet", ""], ["month", 2, 0, 4, 3, "lunar", ""], ["clock", 0, 2, 2, 2, "lunar", ""], ["countdown", 2, 3, 4, 1, "tet", ""]] },
  { screen: "wl", name: "Thị trường", desc: "Vàng, chứng khoán, tỷ giá, tiền số.", skin: "line",
    items: [["clock", 0, 0, 2, 2, "big", ""], ["gold", 2, 0, 2, 2, "", "SJC"], ["crypto", 4, 0, 2, 2, "", "BTC,ETH"], ["stocks", 0, 2, 4, 2, "", "VNINDEX,VN30,HNX"], ["fx", 4, 2, 2, 2, "", "USD,EUR,JPY"]] },
];

/** What a preview needs: the device's dashboard copy, cached markets, reading stats, the newest issue. */
export interface PreviewData {
  dash: Dash | null;
  markets: Markets | null;
  stats: Stats | null;
  news: NewsIssue | null;
  battery: number | null;
  now: Date;
  /** Cover picture and author of the book being read, when known. */
  cover?: string;
  author?: string;
  /** Books for the library widget, most recently read first. */
  library?: { t: string; a?: string; p?: number; cover?: string }[];
  /** Picture thumbnails by id, for photo widgets. */
  photos?: Record<number, string>;
  /** The device shows 12-hour clocks (setting sys.clock_fmt). */
  h12?: boolean;
  /** The device's last sync, "06:20", and its name (the sleep screen's footer). */
  lastSync?: string;
  deviceName?: string;
}

/** The preview data in the device's terms (what loadData/readDashboard fill on the X4). */
export function inkDataOf(data: PreviewData): InkData {
  const { now, dash } = data;
  const l = fromSolar(now.getDate(), now.getMonth() + 1, now.getFullYear());
  const wx = dash?.weather;
  const st = data.stats;
  const key = (d: Date) => `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, "0")}-${String(d.getDate()).padStart(2, "0")}`;
  const last7 = Array.from({ length: 7 }, (_, i) => {
    const d = new Date(now);
    d.setDate(d.getDate() - 6 + i);
    return st?.days.find((x) => x.d === key(d))?.s ?? 0;
  });
  const book = st?.books?.length ? [...st.books].sort((a, b) => (b.last || "").localeCompare(a.last || ""))[0] : null;
  const mk = data.markets;
  const stocks = [
    ...Object.values(mk?.index?.items ?? {}), ...Object.values(mk?.stk?.items ?? {}), ...(dash?.stocks?.items ?? []),
  ].map((q) => ({ sym: q.s, price: q.p, change: q.c, pct: q.pc }));
  const crypto = [...Object.values(mk?.crypto?.items ?? {}), ...(dash?.crypto?.items ?? [])].map((c) => ({ sym: c.s, usd: c.p, vnd: c.v, pct: c.pc }));
  const g = mk?.gold ?? dash?.gold;
  const issue = data.news;
  return {
    timeValid: true,
    showTime: true,
    now: { hour: now.getHours(), min: now.getMinutes(), mday: now.getDate(), mon: now.getMonth(), year: now.getFullYear(), wday: now.getDay() },
    h12: !!data.h12,
    lunar: `${l.day}/${l.month}${l.leap ? "N" : ""}`,
    lunarYear: yearName(l.year),
    hasWeather: !!wx,
    city: wx?.city ?? "",
    desc: wx?.desc ?? "",
    icon: wx?.icon ?? "cloud",
    temp: wx?.temp ?? 0,
    tmin: wx?.min ?? 0,
    tmax: wx?.max ?? 0,
    rain: wx?.rain ?? 0,
    hours: (wx?.hours ?? []).slice(0, 4).map((h) => ({ h: h.h, icon: h.icon, t: h.t })),
    days: (wx?.days ?? []).slice(0, 3).map((d) => ({ d: d.d, icon: d.icon, min: d.min, max: d.max })),
    events: (dash?.events ?? []).slice(0, 10),
    eventCount: dash?.events?.length ?? 0,
    eventsOld: false,
    notes: (dash?.notes ?? []).slice(0, 4),
    hasBook: !!book,
    bookPath: book?.title ?? "",
    bookTitle: book?.title ?? "",
    bookAuthor: data.author ?? "",
    bookPercent: book?.percent ?? -1,
    cover: data.cover,
    stats: { dated: !!st, todaySec: last7[6], goalMin: st?.goal ?? 30, streak: st?.streak ?? 0, last7, todayWday: now.getDay() },
    library: (data.library ?? []).slice(0, 8).map((b) => ({ path: b.t, title: b.t, author: b.a ?? "", percent: b.p ?? -1, cover: b.cover })),
    newsPath: issue ? "news" : "",
    newsDay: issue ? `${new Date(issue.created_at).getDate()}/${new Date(issue.created_at).getMonth() + 1}` : "",
    newsTop: (dash?.news?.top ?? []).slice(0, 3),
    newsCount: 0,
    gold: (g?.items ?? []).map((q) => ({ code: q.c ?? "", name: q.n, buy: q.b, sell: q.s, change: q.d ?? 0 })),
    goldAt: g?.at ?? "",
    goldSrc: g?.src ?? "",
    stocks,
    stocksAt: mk?.index?.at ?? dash?.stocks?.at ?? "",
    fx: (mk?.fx?.items ?? dash?.fx?.items ?? []).map((r) => ({ code: r.c, buy: r.b, sell: r.s })),
    crypto,
    cryptoAt: mk?.crypto?.at ?? dash?.crypto?.at ?? "",
    battery: data.battery ?? 80,
    lastSync: data.lastSync ?? "",
    photos: data.photos ?? {},
    bookFocus: -1,
  };
}

/** One widget exactly as the X4 draws it (the firmware's drawing, ported in ink/widgets.ts), at device pixels. */
export function WidgetView({ it, w, h, data, focused, skin = "card" }: { it: LayoutItem; w: number; h: number; data: PreviewData; focused?: boolean; skin?: Skin }) {
  const ink = useMemo(() => inkDataOf(data), [data]);
  const svg = useMemo(() => {
    const r = new Renderer();
    const item = { kind: it[0], style: it[5] === "strip" && it[0] === "weather" ? "now" : it[5], opt: it[6] ?? "", inverted: it[7] === "i", x: it[1], y: it[2] };
    drawWidget(r, item, { x: 0, y: 0, w, h }, ink, !!focused, skin);
    return r.svg(w + SHADOW, h + SHADOW, false);
  }, [it, w, h, ink, focused, skin]);
  return <div className="w-ink" style={{ width: w, height: h }} dangerouslySetInnerHTML={{ __html: svg }} />;
}

/** Pixel boxes of a layout's items, exactly as the device places them (src/inkdesk/Widgets.cpp place()). */
export function placeItems(items: LayoutItem[], cols: number, rows: number, left: number, top: number, right: number, bottom: number, gap: number) {
  const cw = (right - left - gap * (cols - 1)) / cols;
  const rh = (bottom - top - gap * (rows - 1)) / rows;
  return items.map(([, x, y, w, h]) => {
    const x0 = left + Math.trunc(x * (cw + gap));
    const y0 = top + Math.trunc(y * (rh + gap));
    const x1 = left + Math.trunc((x + w) * (cw + gap) - gap);
    const y1 = top + Math.trunc((y + h) * (rh + gap) - gap);
    return { x: x0, y: y0, w: x1 - x0, h: y1 - y0 };
  });
}

/** The grid area of a screen (device pixels), matching the firmware. */
export function gridArea(screen: Screen, footer: boolean) {
  const s = SCREENS[screen];
  if (screen === "home") return { left: 12, top: 44, right: s.w - 12, bottom: s.h - 48, gap: 8 };
  return { left: 12, top: footer ? 44 : 12, right: s.w - 12, bottom: footer ? s.h - 50 : s.h - 12, gap: 10 };
}
