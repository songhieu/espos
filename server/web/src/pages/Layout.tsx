// Giao diện máy: arrange the X4's Home and sleep screens with widgets, by drag and drop, like a
// phone's home screen. Saves the layouts as settings (home.w, sleep.wl, sleep.wp); the device
// picks them up at its next sync and draws them itself.
import { useCallback, useEffect, useMemo, useRef, useState, type PointerEvent as RPointerEvent } from "react";

import type { Device, Ink, LayoutDoc, LayoutItem, Photo, Wallpaper } from "../inkdesk";
import { bitsDataUrl, ensureRenders, formatPhotoOpt, original, parsePhotoOpt, PHOTO_MAX, renderBits, uploadPhoto, type PhotoOpt } from "../photo";
import { CATS, COINS, DEFAULT_CODES, DEFAULT_LAYOUT, gridArea, inkDataOf, KINDS, SCREENS, SHADOW, SKINS, TEMPLATES, WidgetView, placeItems, type Kind, type PreviewData, type Screen, type Skin, type Template } from "../widgets";
import { Renderer } from "../ink/render";
import { drawHomeHints, drawSleepFooter, drawStatusBar } from "../ink/widgets";
import { DashPreview, type DashLayout } from "../DashPreview";
import { Seg, Toggle } from "../ui";
import { PicturePreview, Wallpapers, normMode, type SleepMode } from "./SleepScreen";

type Layouts = Record<Screen, LayoutItem[]>;
type Skins = Record<Screen, Skin>;
type SleepSet = Record<string, string>;

/** Sleep-screen settings edited here (with the server's defaults). */
const SLEEP_KEYS: SleepSet = {
  "sleep.mode": "dashboard", "dash.interval_min": "30", "dash.clock": "1", "dash.charging_min": "1", "dash.save_pct": "20",
  "dash.quiet_from": "23:00", "dash.quiet_to": "06:00", "sleep.rotate": "sleep", "sleep.order": "seq",
  "sleep.clock": "1", "dash.enabled": "1", "dash.blocks": "weather,lunar,calendar,notes", "sys.clock_fmt": "24",
};

/** What the sleep screen shows: widget layouts, the classic layouts, my pictures, the book cover, or automatic. */
type SleepPick = "wl" | "wp" | "desk" | "calendar" | "portrait" | "images" | "cover" | "auto" | "off";
const DASH_LAYOUTS = ["wl", "wp", "desk", "calendar", "portrait"];
const SLEEP_PICKS: { id: SleepPick; name: string; desc: string }[] = [
  { id: "wl", name: "Widget ngang", desc: "Tự sắp widget, máy nằm ngang" },
  { id: "wp", name: "Widget dọc", desc: "Tự sắp widget, máy dựng đứng" },
  { id: "images", name: "Ảnh của tôi", desc: "Ảnh bạn tải lên, tự đổi" },
  { id: "desk", name: "Bàn làm việc", desc: "Mẫu vẽ sẵn: giờ, lịch, thời tiết" },
  { id: "calendar", name: "Tờ lịch", desc: "Mẫu vẽ sẵn: tờ lịch to" },
  { id: "portrait", name: "Dọc cổ điển", desc: "Mẫu vẽ sẵn, máy dựng đứng" },
  { id: "cover", name: "Bìa sách", desc: "Bìa cuốn đang đọc dở" },
  { id: "auto", name: "Tự động", desc: "Đang đọc dở: bìa; không: màn chờ" },
  { id: "off", name: "Màn ngủ gốc", desc: "Của máy, không cập nhật, đỡ pin nhất" },
];
const BLOCKS = [
  { id: "weather", label: "Thời tiết" }, { id: "lunar", label: "Âm lịch" },
  { id: "calendar", label: "Lịch hôm nay" }, { id: "notes", label: "Thông báo" },
];

function PickIcon({ id }: { id: SleepPick }) {
  const land = id === "wl" || id === "desk" || id === "calendar";
  const W = land ? 44 : 28, H = land ? 28 : 44;
  const box = (x: number, y: number, w: number, h: number, fill = false) => <rect x={x} y={y} width={w} height={h} rx={1.5} fill={fill ? "currentColor" : "none"} stroke="currentColor" strokeWidth={1.4} />;
  let inner: React.ReactNode = null;
  if (id === "wl") inner = <>{box(4, 4, 22, 14)}{box(28, 4, 12, 9)}{box(28, 15, 12, 9)}{box(4, 20, 22, 4)}</>;
  else if (id === "wp") inner = <>{box(4, 4, 20, 12)}{box(4, 18, 9, 10)}{box(15, 18, 9, 10)}{box(4, 30, 20, 10)}</>;
  else if (id === "desk") inner = <><rect x={5} y={6} width={16} height={7} fill="currentColor" /><path d="M5 17h14M5 21h10M26 5v18M29 8h10M29 12h8M29 16h10" stroke="currentColor" strokeWidth={1.4} /></>;
  else if (id === "calendar") inner = <><rect x={8} y={4} width={28} height={5} fill="currentColor" /><path d="M14 13h16M14 17h16M14 21h10" stroke="currentColor" strokeWidth={1.6} /></>;
  else if (id === "portrait") inner = <><rect x={5} y={5} width={18} height={8} fill="currentColor" /><path d="M5 17h18M5 21h14M5 27h18M5 31h12M5 36h16" stroke="currentColor" strokeWidth={1.4} /></>;
  else if (id === "images") inner = <><path d="M4 36l7-9 5 6 4-4 4 7z" fill="currentColor" /><circle cx={19} cy={12} r={3.2} fill="currentColor" /></>;
  else if (id === "cover") inner = <><rect x={7} y={6} width={14} height={22} rx={1} fill="currentColor" /><path d="M7 33h14M7 37h9" stroke="currentColor" strokeWidth={1.4} /></>;
  else if (id === "off") inner = <><circle cx={14} cy={20} r={6} fill="none" stroke="currentColor" strokeWidth={1.4} /><path d="M14 11v6" stroke="currentColor" strokeWidth={1.6} strokeLinecap="round" /></>;
  else inner = <><rect x={4} y={6} width={9} height={14} rx={1} fill="currentColor" /><path d="M16 8h8M16 12h8M16 16h6M4 26h20M4 31h16M4 36h20" stroke="currentColor" strokeWidth={1.4} /></>;
  return (
    <svg width={W} height={H} viewBox={`0 0 ${W} ${H}`} aria-hidden="true" className="lay-pickico">
      <rect x={0.7} y={0.7} width={W - 1.4} height={H - 1.4} rx={4} fill="none" stroke="currentColor" strokeWidth={1.4} />
      {inner}
    </svg>
  );
}
interface Drag {
  from: number | null; // index in the layout, or null from the gallery
  kind: string;
  w: number;
  h: number;
  style: string;
  opt: string;
  sx: number;
  sy: number;
  moved: boolean;
}

const SCALE: Record<Screen, number> = { home: 0.75, wl: 0.78, wp: 0.75 };
const sleepLayoutValue: Record<Screen, string> = { home: "", wl: "wl", wp: "wp" };

function parse(v: string | undefined, screen: Screen): { items: LayoutItem[]; footer: boolean; skin: Skin } | null {
  if (!v) return null;
  try {
    const d = JSON.parse(v) as LayoutDoc;
    if (!Array.isArray(d.i) || d.i.length === 0) return null;
    const items = d.i.map((x) => withFlags([String(x[0]), +x[1], +x[2], +x[3], +x[4], String(x[5] ?? ""), String(x[6] ?? "")], x[7] === "i"));
    // Kinds no longer offered (word of the day) and, on the sleep screens, button widgets are dropped.
    const ok = (x: LayoutItem) => x[0] in KINDS && !(screen !== "home" && KINDS[x[0] as Kind].homeOnly) && !(x[0] === "app" && x[5] === "flashcard");
    const skin = (SKINS.some((k) => k.id === d.k) ? d.k : "card") as Skin;
    return { items: items.filter((x) => ok(x) && x[1] + x[3] <= SCREENS[screen].cols && x[2] + x[4] <= SCREENS[screen].rows), footer: d.f !== 0, skin };
  } catch {
    return null;
  }
}

function fits(items: LayoutItem[], it: { x: number; y: number; w: number; h: number }, screen: Screen, skip: number | null) {
  const S = SCREENS[screen];
  if (it.x < 0 || it.y < 0 || it.x + it.w > S.cols || it.y + it.h > S.rows) return false;
  return items.every((o, i) => i === skip || o[1] >= it.x + it.w || o[1] + o[3] <= it.x || o[2] >= it.y + it.h || o[2] + o[4] <= it.y);
}

/** An item with or without the invert flag (the flag is left out when off). */
function withFlags(x: LayoutItem, inv: boolean): LayoutItem {
  const base: LayoutItem = [x[0], x[1], x[2], x[3], x[4], x[5], x[6]];
  return inv ? [x[0], x[1], x[2], x[3], x[4], x[5], x[6], "i"] : base;
}
const inverted = (x: LayoutItem) => x[7] === "i";
const copyItems = (items: LayoutItem[]) => items.map((x) => [...x] as LayoutItem);

/** Pixel box of every item as the device draws it (the shadow style draws widgets smaller). */
function boxesOf(items: LayoutItem[], screen: Screen, footer: boolean, skin: Skin) {
  const S = SCREENS[screen];
  const a = gridArea(screen, footer);
  const sh = skin === "shadow" ? SHADOW : 0;
  return placeItems(items, S.cols, S.rows, a.left, a.top, a.right, a.bottom, a.gap).map((b) => ({ ...b, w: b.w - sh, h: b.h - sh }));
}

/** The pixel size a w x h widget gets on a screen (its top-left cell), as the device places it. */
function cellBox(screen: Screen, w: number, h: number, skin: Skin, footer = true) {
  const b = boxesOf([["x", 0, 0, w, h, "", ""]], screen, footer, skin)[0];
  return { w: b.w, h: b.h };
}

/** The screen's own lines drawn by the device: Home's status bar and button hints, the sleep screens' status bar and footer. */
function Chrome({ screen, footer, data, interval }: { screen: Screen; footer: boolean; data: PreviewData; interval?: number }) {
  const S = SCREENS[screen];
  const svg = useMemo(() => {
    const r = new Renderer();
    const d = inkDataOf(data);
    if (screen === "home") {
      drawStatusBar(r, S.w, d, true, { battery: data.battery ?? 80 });
      drawHomeHints(r, S.w, S.h, ["Ứng dụng", "Mở", "Lên", "Xuống"]);
    } else if (footer) {
      drawStatusBar(r, S.w, d, true, { battery: data.battery ?? 80 });
      const hhmm = (t: Date) => `${String(t.getHours()).padStart(2, "0")}:${String(t.getMinutes()).padStart(2, "0")}`;
      const next = new Date(data.now.getTime() + (interval ?? 30) * 60000);
      drawSleepFooter(r, S.w, S.h, screen === "wl" ? data.deviceName ?? "" : "", data.lastSync || hhmm(data.now), hhmm(next));
    }
    return r.svg(S.w, S.h, false);
  }, [screen, footer, data, interval, S.w, S.h]);
  return <div className="lay-chrome" dangerouslySetInnerHTML={{ __html: svg }} />;
}

const appsTiles = (items: LayoutItem[]) => items.filter((x) => x[0] === "app" && x[5] === "apps").length;

export function LayoutPage({ ink }: { ink: Ink }) {
  const [devices, setDevices] = useState<Device[] | null>(null);
  const [dev, setDev] = useState<number | null>(null);
  const [screen, setScreen] = useState<Screen>("home");
  const [layouts, setLayouts] = useState<Layouts>({ home: [], wl: [], wp: [] });
  const [footer, setFooter] = useState<Record<Screen, boolean>>({ home: true, wl: true, wp: true });
  const [dashLayout, setDashLayout] = useState("desk");
  const [skins, setSkins] = useState<Skins>({ home: "card", wl: "card", wp: "card" });
  const [sset, setSset] = useState<SleepSet>({ ...SLEEP_KEYS });
  const [walls, setWalls] = useState<Wallpaper[]>([]);
  const [photos, setPhotos] = useState<Photo[]>([]);
  const [saved, setSaved] = useState("");
  const [sel, setSel] = useState(-1);
  const [hist, setHist] = useState<string[]>([]);
  const [cat, setCat] = useState("Tất cả");
  const [query, setQuery] = useState("");
  const [msg, setMsg] = useState<{ ok: boolean; text: string } | null>(null);
  const [busy, setBusy] = useState(false);
  const [now, setNow] = useState(() => new Date());
  const [data, setData] = useState<Omit<PreviewData, "now">>({ dash: null, markets: null, stats: null, news: null, battery: null });
  const [ghost, setGhost] = useState<{ x: number; y: number; item: LayoutItem } | null>(null);
  const [target, setTarget] = useState<{ x: number; y: number; w: number; h: number } | null>(null);
  const drag = useRef<Drag | null>(null);
  const targetRef = useRef<{ x: number; y: number; w: number; h: number } | null>(null);
  const screenRef = useRef<HTMLDivElement>(null);

  const snapshot = (l: Layouts, f: Record<Screen, boolean>, dl: string, sk: Skins, ss: SleepSet) => JSON.stringify([l, f, dl, sk, ss]);
  const loadPhotos = useCallback(() => ink.photos().then(setPhotos, () => undefined), [ink]);

  useEffect(() => {
    // The previews use the X4's fonts (Noto Sans, Noto Serif for the word clock).
    if (!document.getElementById("ik-widget-fonts")) {
      const link = document.createElement("link");
      link.id = "ik-widget-fonts";
      link.rel = "stylesheet";
      link.href = "https://fonts.googleapis.com/css2?family=Noto+Sans:wght@400;700;800&family=Noto+Serif:wght@700&display=swap";
      document.head.appendChild(link);
    }
    const t = setInterval(() => setNow(new Date()), 30000);
    return () => clearInterval(t);
  }, []);

  useEffect(() => {
    ink.devices().then(
      (ds) => {
        setDevices(ds);
        if (ds.length) setDev(ds[0].id);
      },
      (e: Error) => setMsg({ ok: false, text: e.message }),
    );
    ink.markets().then((m) => setData((d) => ({ ...d, markets: m })), () => undefined);
    ink.stats().then((s) => setData((d) => ({ ...d, stats: s })), () => undefined);
    ink.news().then((n) => setData((d) => ({ ...d, news: n.issues?.[0] ?? null })), () => undefined);
    // The library widget lists the books read lately first, then the rest of the library.
    Promise.all([ink.stats().catch(() => null), ink.books().then((r) => r.books).catch(() => [])]).then(([st, books]) => {
      const seen = new Set<string>();
      const lib: { t: string; p?: number }[] = [];
      for (const b of [...(st?.books ?? [])].sort((a, c) => (c.last || "").localeCompare(a.last || ""))) {
        seen.add(b.title);
        lib.push({ t: b.title, p: b.percent ?? undefined });
      }
      for (const b of books) if (!seen.has(b.title)) lib.push({ t: b.title });
      setData((d) => ({ ...d, library: lib }));
    });
    void loadPhotos();
  }, [ink, loadPhotos]);

  useEffect(() => {
    if (dev == null) return;
    ink.settings(dev).then(
      (r) => {
        const v = (k: string) => r.settings[k]?.v ?? "";
        const next: Layouts = { home: [], wl: [], wp: [] };
        const f: Record<Screen, boolean> = { home: true, wl: true, wp: true };
        const sk: Skins = { home: "card", wl: "card", wp: "card" };
        (Object.keys(SCREENS) as Screen[]).forEach((s) => {
          const p = parse(v(SCREENS[s].key), s);
          next[s] = p ? p.items : copyItems(DEFAULT_LAYOUT[s]);
          f[s] = p ? p.footer : true;
          sk[s] = p ? p.skin : "card";
        });
        const dl = v("dash.layout") || "desk";
        const ss: SleepSet = {};
        for (const [k, def] of Object.entries(SLEEP_KEYS)) ss[k] = v(k) || def;
        setLayouts(next);
        setFooter(f);
        setDashLayout(dl);
        setSkins(sk);
        setSset(ss);
        setSaved(snapshot(next, f, dl, sk, ss));
        const ls = r.device.last_sync ? new Date(r.device.last_sync) : null;
        setData((d) => ({
          ...d,
          battery: r.device.battery ?? null,
          deviceName: r.device.name,
          lastSync: ls ? `${String(ls.getHours()).padStart(2, "0")}:${String(ls.getMinutes()).padStart(2, "0")}` : "",
        }));
      },
      (e: Error) => setMsg({ ok: false, text: e.message }),
    );
    ink.deviceDashboard(dev).then((dash) => setData((d) => ({ ...d, dash })), () => undefined);
  }, [ink, dev]);

  const items = layouts[screen];
  const S = SCREENS[screen];
  // The device shrinks to the stage's width (a landscape screen is wide).
  const stageRef = useRef<HTMLDivElement>(null);
  const [stageW, setStageW] = useState(0);
  useEffect(() => {
    const el = stageRef.current;
    if (!el) return;
    const ro = new ResizeObserver(() => setStageW(el.clientWidth));
    ro.observe(el);
    setStageW(el.clientWidth);
    return () => ro.disconnect();
  });
  const scale = stageW > 0 ? Math.min(SCALE[screen], (stageW - 40 - 36) / SCREENS[screen].w) : SCALE[screen];
  const area = gridArea(screen, footer[screen]);
  const skin = skins[screen];
  const boxes = useMemo(() => boxesOf(items, screen, footer[screen], skin), [items, screen, footer, skin]);
  const photoThumbs = useMemo(() => Object.fromEntries(photos.map((p) => [p.id, p.thumb])), [photos]);
  const preview: PreviewData = useMemo(() => ({ ...data, now, photos: photoThumbs, h12: sset["sys.clock_fmt"] === "12" }), [data, now, photoThumbs, sset]);
  const dirty = saved !== "" && snapshot(layouts, footer, dashLayout, skins, sset) !== saved;
  const sleepMode = normMode(sset["sleep.mode"]);
  const off = sset["dash.enabled"] === "0";
  const pick: SleepPick = off ? "off" : sleepMode === "dashboard" ? ((DASH_LAYOUTS.includes(dashLayout) ? dashLayout : "desk") as SleepPick) : sleepMode;
  const widgetSleep = !off && (sleepMode === "dashboard" || sleepMode === "auto") && (dashLayout === "wl" || dashLayout === "wp");
  // The widget editor shows for Home, and for the sleep screen when it shows widgets.
  const showEditor = screen === "home" || widgetSleep;

  const change = useCallback(
    (fn: (cur: LayoutItem[]) => LayoutItem[]) => {
      setHist((h) => [...h.slice(-40), JSON.stringify([layouts, footer, dashLayout, skins, sset])]);
      setLayouts((l) => ({ ...l, [screen]: fn(l[screen]) }));
      setMsg(null);
    },
    [layouts, footer, dashLayout, skins, sset, screen],
  );

  // Pictures already placed get renders at this firmware's box sizes too (the sleep screens' boxes sit under
  // the status bar since 0.3.1); made once per visit, quietly, only those missing.
  const rendersChecked = useRef(false);
  useEffect(() => {
    if (!saved || rendersChecked.current) return;
    rendersChecked.current = true;
    void (async () => {
      for (const s of Object.keys(SCREENS) as Screen[]) {
        const bx = boxesOf(layouts[s], s, footer[s], skins[s]);
        for (const [i, it] of layouts[s].entries()) {
          if (it[0] === "photo") await ensureRenders(ink, parsePhotoOpt(it[6]), bx[i].w, bx[i].h).catch(() => undefined);
        }
      }
    })();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [saved]);

  const undo = () => {
    const last = hist[hist.length - 1];
    if (!last) return;
    const [l, f, dl, sk, ss] = JSON.parse(last) as [Layouts, Record<Screen, boolean>, string, Skins, SleepSet];
    setLayouts(l);
    setFooter(f);
    setDashLayout(dl);
    setSkins(sk);
    setSset(ss);
    if (screen !== "home" && (dl === "wl" || dl === "wp")) setScreen(dl);
    setHist((h) => h.slice(0, -1));
    setSel(-1);
  };

  // ---- drag and drop ----
  const cellAt = (px: number, py: number) => {
    const el = screenRef.current;
    if (!el) return null;
    const r = el.getBoundingClientRect();
    const k = r.width / S.w;
    const dx = (px - r.left) / k, dy = (py - r.top) / k;
    const cw = (area.right - area.left + area.gap) / S.cols;
    const rh = (area.bottom - area.top + area.gap) / S.rows;
    const x = Math.floor((dx - area.left) / cw), y = Math.floor((dy - area.top) / rh);
    if (x < 0 || y < 0 || x >= S.cols || y >= S.rows) return null;
    return { x, y };
  };

  const startDrag = (e: RPointerEvent, d: Omit<Drag, "sx" | "sy" | "moved">) => {
    if (e.button !== 0) return;
    e.preventDefault();
    drag.current = { ...d, sx: e.clientX, sy: e.clientY, moved: false };
  };

  useEffect(() => {
    const move = (e: PointerEvent) => {
      const d = drag.current;
      if (!d) return;
      if (!d.moved && Math.abs(e.clientX - d.sx) + Math.abs(e.clientY - d.sy) < 5) return;
      d.moved = true;
      setGhost({ x: e.clientX, y: e.clientY, item: [d.kind, 0, 0, d.w, d.h, d.style, d.opt] });
      const c = cellAt(e.clientX, e.clientY);
      const t = c ? { x: Math.min(c.x, S.cols - d.w), y: Math.min(c.y, S.rows - d.h), w: d.w, h: d.h } : null;
      targetRef.current = t && fits(items, t, screen, d.from) ? t : null;
      setTarget(targetRef.current);
    };
    const up = () => {
      const d = drag.current;
      drag.current = null;
      setGhost(null);
      if (!d) return;
      if (!d.moved) {
        if (d.from != null) setSel(d.from);
        setTarget(null);
        return;
      }
      const t = targetRef.current;
      targetRef.current = null;
      setTarget(null);
      if (!t) return;
      if (d.from != null) {
        const from = d.from;
        change((cur) => cur.map((x, i) => (i === from ? withFlags([x[0], t.x, t.y, x[3], x[4], x[5], x[6]], inverted(x)) : x)));
        setSel(from);
      } else {
        change((cur) => [...cur, [d.kind, t.x, t.y, d.w, d.h, d.style, d.opt]]);
        setSel(items.length);
      }
    };
    window.addEventListener("pointermove", move);
    window.addEventListener("pointerup", up);
    return () => {
      window.removeEventListener("pointermove", move);
      window.removeEventListener("pointerup", up);
    };
  });

  useEffect(() => {
    if (sel < 0) return;
    const key = (e: KeyboardEvent) => {
      if (e.key !== "Escape" || drag.current) return;
      const t = e.target as HTMLElement | null;
      // Esc inside a text box only leaves the box.
      if (t && (t.tagName === "INPUT" || t.tagName === "TEXTAREA") && (t as HTMLInputElement).type !== "checkbox") {
        t.blur();
        return;
      }
      setSel(-1);
    };
    window.addEventListener("keydown", key);
    return () => window.removeEventListener("keydown", key);
  }, [sel]);

  const save = async () => {
    if (dev == null) return;
    if (appsTiles(layouts.home) === 0) {
      setMsg({ ok: false, text: "Màn chính cần có ô Ứng dụng (Lối tắt ứng dụng → Ứng dụng)." });
      return;
    }
    const [l0, f0, dl0, sk0, ss0] = JSON.parse(saved) as [Layouts, Record<Screen, boolean>, string, Skins, SleepSet];
    const changes: Record<string, string> = {};
    const jobs: { o: PhotoOpt; w: number; h: number }[] = [];
    (Object.keys(SCREENS) as Screen[]).forEach((s) => {
      if (JSON.stringify(layouts[s]) !== JSON.stringify(l0[s]) || footer[s] !== f0[s] || skins[s] !== sk0?.[s]) {
        const doc: LayoutDoc = { v: 1, i: layouts[s] };
        if (s !== "home" && !footer[s]) doc.f = 0;
        if (skins[s] !== "card") doc.k = skins[s];
        changes[SCREENS[s].key] = JSON.stringify(doc);
        const bx = boxesOf(layouts[s], s, footer[s], skins[s]);
        layouts[s].forEach((it, i) => {
          if (it[0] === "photo") jobs.push({ o: parsePhotoOpt(it[6]), w: bx[i].w, h: bx[i].h });
        });
      }
    });
    if (dashLayout !== dl0) changes["dash.layout"] = dashLayout;
    for (const k of Object.keys(SLEEP_KEYS)) if (sset[k] !== ss0?.[k]) changes[k] = sset[k];
    if (!Object.keys(changes).length) return;
    setBusy(true);
    try {
      // Each picture is made at the exact size of its box, as the device will draw it.
      const total = jobs.reduce((n, j) => n + j.o.ids.length, 0);
      let done = 0;
      for (const j of jobs) {
        await ensureRenders(ink, j.o, j.w, j.h, () => {
          done++;
          setMsg({ ok: true, text: `Đang chuẩn bị ảnh ${done}/${total}…` });
        });
      }
      await ink.saveSettings(dev, changes);
      setSaved(snapshot(layouts, footer, dashLayout, skins, sset));
      setMsg({ ok: true, text: "Đã lưu. Máy nhận ở lần đồng bộ tới (mở máy về màn chính là đồng bộ)." });
    } catch (x) {
      setMsg({ ok: false, text: (x as Error).message });
    } finally {
      setBusy(false);
    }
  };

  const useTemplate = (t: Template) => {
    setHist((h) => [...h.slice(-40), JSON.stringify([layouts, footer, dashLayout, skins, sset])]);
    setLayouts((l) => ({ ...l, [screen]: copyItems(t.items) }));
    setSkins((k) => ({ ...k, [screen]: t.skin }));
    if (screen !== "home") {
      setDashLayout(sleepLayoutValue[screen]);
      if (sleepMode !== "dashboard" && sleepMode !== "auto") setSset((x) => ({ ...x, "sleep.mode": "dashboard" }));
    }
    setSel(-1);
    setMsg({ ok: true, text: `Đã dùng mẫu “${t.name}”. Chỉnh tiếp nếu muốn, rồi bấm Lưu. Không ưng thì bấm Hoàn tác.` });
  };
  // The sleep screen: what it shows, and its other settings (all go through the history).
  const remember = () => setHist((h) => [...h.slice(-40), JSON.stringify([layouts, footer, dashLayout, skins, sset])]);
  const choose = (p: SleepPick) => {
    if (p === pick) return;
    remember();
    setSel(-1);
    setMsg(null);
    if (p === "off") {
      setSset((x) => ({ ...x, "dash.enabled": "0" }));
    } else if (DASH_LAYOUTS.includes(p)) {
      setSset((x) => ({ ...x, "sleep.mode": "dashboard", "dash.enabled": "1" }));
      setDashLayout(p);
      if (p === "wl" || p === "wp") setScreen(p);
    } else {
      setSset((x) => ({ ...x, "sleep.mode": p, "dash.enabled": "1" }));
    }
  };
  const chooseAutoLayout = (p: string) => {
    remember();
    setDashLayout(p);
    if (p === "wl" || p === "wp") setScreen(p);
  };
  const setSleep = (k: string) => (val: string) => {
    remember();
    setSset((x) => ({ ...x, [k]: val }));
    setMsg(null);
  };
  const setSkin = (k: Skin) => {
    if (k === skin) return;
    setHist((h) => [...h.slice(-40), JSON.stringify([layouts, footer, dashLayout, skins, sset])]);
    setSkins((sk) => ({ ...sk, [screen]: k }));
    setMsg(null);
  };

  const gallery = (Object.keys(KINDS) as Kind[]).filter((k) => {
    const info = KINDS[k];
    if (screen !== "home" && info.homeOnly) return false;
    if (cat !== "Tất cả" && info.cat !== cat) return false;
    if (query && !(info.name + " " + info.cat).toLowerCase().includes(query.toLowerCase())) return false;
    return info.sizes.some(([w, h]) => w <= S.cols && h <= S.rows);
  });

  const selItem = sel >= 0 ? items[sel] : null;

  if (devices && devices.length === 0) {
    return (
      <section className="ik-page">
        <h2>Giao diện máy</h2>
        <p className="ik-muted">Chưa có máy nào liên kết. Cài máy ở trang Thiết bị trước.</p>
      </section>
    );
  }

  return (
    <section className="ik-page lay">
      <header className="ik-head">
        <div>
          <h2>Giao diện máy</h2>
          <p>Chọn một mẫu hoặc kéo widget vào máy như sắp màn hình điện thoại. Lưu xong, máy nhận ở lần đồng bộ tới.</p>
        </div>
        <div className="lay-actions">
          {msg && <span className={"lay-msg " + (msg.ok ? "ok" : "err")}>{msg.text}</span>}
          {!msg && <span className={"lay-state" + (dirty ? " dirty" : "")}>{dirty ? "Chưa lưu" : "Đã lưu"}</span>}
          <button type="button" className="ik-btn ghost" disabled={!hist.length} onClick={undo}>
            ↶ Hoàn tác
          </button>
          <button type="button" className="ik-btn" disabled={!dirty || busy} onClick={save}>
            Lưu và gửi xuống máy
          </button>
        </div>
      </header>

      <div className="lay-toolbar">
        <div className="lay-tabs" role="tablist">
          <button type="button" role="tab" aria-selected={screen === "home"} className={screen === "home" ? "on" : ""} onClick={() => { setScreen("home"); setSel(-1); }}>
            Màn chính
          </button>
          <button type="button" role="tab" aria-selected={screen !== "home"} className={screen !== "home" ? "on" : ""} onClick={() => { setScreen(dashLayout === "wp" ? "wp" : "wl"); setSel(-1); }}>
            Màn chờ <span className="pill">{SLEEP_PICKS.find((p) => p.id === pick)?.name}</span>
          </button>
        </div>
        {showEditor && <div className="lay-skins" role="radiogroup" aria-label="Kiểu khung">
          <span>Kiểu khung</span>
          {SKINS.map((k) => (
            <button key={k.id} type="button" role="radio" aria-checked={skin === k.id} className={skin === k.id ? "on" : ""} title={k.desc} onClick={() => setSkin(k.id)}>
              <i className={"sk-dot " + k.id} aria-hidden="true" />
              {k.name}
            </button>
          ))}
        </div>}
      </div>

      {screen !== "home" && (
        <section className="lay-tplbar lay-sleeppick" aria-label="Màn chờ hiện gì">
          <div className="lay-tplhead">
            <b>Màn chờ hiện gì</b>
            <span>Khi máy ngủ, màn e-ink giữ nguyên hình này mà không tốn pin</span>
          </div>
          <div className="lay-picks" role="radiogroup" aria-label="Màn chờ hiện gì">
            {SLEEP_PICKS.map((p) => (
              <button key={p.id} type="button" role="radio" aria-checked={pick === p.id} className={"lay-pick" + (pick === p.id ? " on" : "")} onClick={() => choose(p.id)}>
                <PickIcon id={p.id} />
                <span>
                  <b>{p.name}</b>
                  <small>{p.desc}</small>
                </span>
              </button>
            ))}
          </div>
          {!off && sleepMode === "auto" && (
            <div className="lay-autorow">
              <span>Khi không đọc dở, hiện</span>
              <Seg label="Khi không đọc dở" value={DASH_LAYOUTS.includes(dashLayout) ? dashLayout : "desk"} onChange={chooseAutoLayout} options={SLEEP_PICKS.filter((p) => DASH_LAYOUTS.includes(p.id)).map((p) => ({ v: p.id as string, label: p.name }))} />
            </div>
          )}
        </section>
      )}

      {showEditor && (<>
      <section className="lay-tplbar" aria-label="Mẫu có sẵn">
        <div className="lay-tplhead">
          <b>Mẫu có sẵn</b>
          <span>{TEMPLATES.filter((t) => t.screen === screen).length} mẫu cho {SCREENS[screen].label.toLowerCase()} · bấm để dùng, sau đó vẫn chỉnh tiếp được</span>
        </div>
        <div className="lay-tpls">
          {TEMPLATES.filter((t) => t.screen === screen).map((t) => (
            <button key={t.name} type="button" className="lay-tpl" title={t.desc} onClick={() => useTemplate(t)}>
              <MiniScreen tpl={t} data={preview} />
              <span className="lay-tplname">
                <b>{t.name}</b>
                <small>{SKINS.find((k) => k.id === t.skin)?.name}</small>
              </span>
            </button>
          ))}
        </div>
      </section>

      <div className="lay-body">
        <aside className="lay-gal">
          <div className="lay-galhead">
            <b>Widget</b>
            <span>Kéo vào máy</span>
          </div>
          <input placeholder="Tìm: đồng hồ, vàng, lịch…" value={query} onChange={(e) => setQuery(e.target.value)} />
          <div className="lay-cats">
            {["Tất cả", ...CATS].map((c) => (
              <button key={c} type="button" className={cat === c ? "on" : ""} onClick={() => setCat(c)}>
                {c}
              </button>
            ))}
          </div>
          <div className="lay-ggrid">
          {gallery.map((k) => {
            const info = KINDS[k];
            const [w, h] = info.sizes.find(([a, b]) => a <= S.cols && b <= S.rows) ?? info.sizes[0];
            const { w: pw, h: ph } = cellBox(screen, w, h, skin);
            const k2 = Math.min(90 / pw, 62 / ph, 1);
            return (
              <div key={k} className="lay-gitem" onPointerDown={(e) => startDrag(e, { from: null, kind: k, w, h, style: info.styles[0].id, opt: "" })}>
                <div className="lay-gprev">
                  <div style={{ width: pw * k2, height: ph * k2 }}>
                    <div style={{ transform: `scale(${k2})`, transformOrigin: "top left" }}>
                      <WidgetView it={[k, 0, 0, w, h, info.styles[0].id, ""]} w={pw} h={ph} data={preview} skin={skin} />
                    </div>
                  </div>
                </div>
                <div className="lay-gname">
                  <b>{info.name}</b>
                  <span>{info.sizes.filter(([a, b]) => a <= S.cols && b <= S.rows).length} cỡ</span>
                </div>
              </div>
            );
          })}
          </div>
        </aside>

        <div
          className="lay-stage"
          ref={stageRef}
          onPointerDown={(e) => {
            // A press anywhere on the desk that is not a widget drops the selection.
            if (!(e.target as Element).closest(".lay-w")) setSel(-1);
          }}
        >
          <div className={"lay-device" + (screen === "wl" ? " land" : "")}>
            <div className="lay-scalebox" style={{ width: S.w * scale, height: S.h * scale }}>
              <div ref={screenRef} className={"lay-screen sk-" + skin} style={{ width: S.w, height: S.h, transform: `scale(${scale})` }}>
                <Chrome screen={screen} footer={footer[screen]} data={preview} interval={+sset["dash.interval_min"] || 30} />
                {placeItems(
                  Array.from({ length: S.cols * S.rows }, (_, i) => ["c", i % S.cols, Math.floor(i / S.cols), 1, 1, "", ""] as LayoutItem),
                  S.cols, S.rows, area.left, area.top, area.right, area.bottom, area.gap,
                ).map((b, i) => {
                  const cx = i % S.cols, cy = Math.floor(i / S.cols);
                  const hot = target && cx >= target.x && cx < target.x + target.w && cy >= target.y && cy < target.y + target.h;
                  // Only the free cells show (a widget without a frame must not show the grid through it).
                  const taken = items.some((it, j) => j !== drag.current?.from && cx >= it[1] && cx < it[1] + it[3] && cy >= it[2] && cy < it[2] + it[4]);
                  if (taken && !hot) return null;
                  return <div key={i} className={"lay-cell" + (hot ? " hot" : "")} style={{ left: b.x, top: b.y, width: b.w, height: b.h }} />;
                })}
                {items.map((it, i) => {
                  const b = boxes[i];
                  const locked = screen === "home" && it[0] === "app" && it[5] === "apps" && appsTiles(items) === 1;
                  return (
                    <div
                      key={i}
                      className={"lay-w" + (i === sel ? " sel" : "") + (inverted(it) ? " inv" : "")}
                      style={{ left: b.x, top: b.y }}
                      onPointerDown={(e) => startDrag(e, { from: i, kind: it[0], w: it[3], h: it[4], style: it[5], opt: it[6] })}
                    >
                      <WidgetView it={it} w={b.w} h={b.h} data={preview} skin={skin} focused={i === sel && it[0] === "app"} />
                      {!locked && (
                        <button
                          type="button"
                          className="lay-x"
                          aria-label="Xoá widget"
                          onPointerDown={(e) => e.stopPropagation()}
                          onClick={() => { change((cur) => cur.filter((_, j) => j !== i)); setSel(-1); }}
                        >
                          ✕
                        </button>
                      )}
                    </div>
                  );
                })}

              </div>
            </div>
          </div>
          <p className="ik-muted ik-small">
            Lưới {S.cols} cột × {S.rows} hàng · kéo widget vào ô trống ·{" "}
            {sel >= 0 ? "bấm chỗ trống hoặc Esc để về cài đặt chung" : "bấm vào widget để chỉnh"}
          </p>
        </div>

        <aside className="lay-insp">
          {!selItem ? (
            <>
              <h4>{SCREENS[screen].label}</h4>
              <p className="ik-muted ik-small">Bấm vào một widget trên máy để đổi kiểu, cỡ và tuỳ chọn.</p>
              {screen !== "home" && (
                <>
                  <div className="lay-row">
                    <div>
                      <b>Thanh trạng thái và chân trang</b>
                      <small>Giờ, ngày, pin ở trên; giờ cập nhật ở dưới, như Màn chính</small>
                    </div>
                    <Toggle label="Dải pin" on={footer[screen]} onChange={(on) => setFooter((f) => ({ ...f, [screen]: on }))} />
                  </div>
                </>
              )}
              {screen !== "home" && <SleepCommon v={(k) => sset[k]} set={setSleep} />}
              {screen === "home" && <p className="ik-muted ik-small">Ô “Ứng dụng” luôn có trên màn chính. Trên máy, nút Quay lại cũng mở Ứng dụng.</p>}
              <button type="button" className="ik-btn ghost sm" onClick={() => change(() => copyItems(DEFAULT_LAYOUT[screen]))}>
                Về bố cục mặc định
              </button>
            </>
          ) : (
            <Inspector
              key={sel + screen}
              item={selItem}
              screen={screen}
              data={preview}
              ink={ink}
              box={boxes[sel]}
              photos={photos}
              onPhotos={loadPhotos}
              skin={skin}
              h12={sset["sys.clock_fmt"] === "12"}
              onH12={(on) => setSleep("sys.clock_fmt")(on ? "12" : "24")}
              onClose={() => setSel(-1)}
              canResize={(w, h) => fits(items, { x: selItem[1], y: selItem[2], w, h }, screen, sel)}
              onChange={(next) => change((cur) => cur.map((x, i) => (i === sel ? next : x)))}
              onDelete={screen === "home" && selItem[0] === "app" && selItem[5] === "apps" && appsTiles(items) === 1 ? null : () => { change((cur) => cur.filter((_, j) => j !== sel)); setSel(-1); }}
            />
          )}
        </aside>
      </div>

      </>)}

      {!showEditor && off && (
        <div className="lay-sleepbody">
          <div className="lay-sleepmain">
            <h4>Màn ngủ gốc của máy</h4>
            <p className="ik-muted">
              Khi ngủ, máy hiện màn ngủ có sẵn của CrossPoint (chỉnh kiểu trên máy: Cài đặt › Màn hình ngủ). InkDesk không đánh thức máy để cập
              nhật nên đỡ pin nhất. Chọn một kiểu khác ở trên để dùng lại màn chờ InkDesk.
            </p>
          </div>
        </div>
      )}
      {!showEditor && !off && (
        <div className="lay-sleepbody">
          <div className="lay-sleepmain">
            {sleepMode === "images" && (
              <>
                <h4>Ảnh của tôi</h4>
                <Wallpapers ink={ink} onList={setWalls} />
                <div className="lay-row">
                  <div><b>Đổi ảnh</b><small>Mỗi lần máy ngủ, hoặc tự đổi khi đang ngủ</small></div>
                  <Seg label="Đổi ảnh" value={sset["sleep.rotate"]} onChange={setSleep("sleep.rotate")} options={[{ v: "sleep", label: "Mỗi lần ngủ" }, { v: "30", label: "30 phút" }, { v: "60", label: "1 giờ" }, { v: "1440", label: "Mỗi ngày" }]} />
                </div>
                <div className="lay-row">
                  <div><b>Thứ tự</b></div>
                  <Seg label="Thứ tự" value={sset["sleep.order"]} onChange={setSleep("sleep.order")} options={[{ v: "seq", label: "Lần lượt" }, { v: "random", label: "Ngẫu nhiên" }]} />
                </div>
                <div className="lay-row">
                  <div><b>Giờ và pin ở cạnh dưới ảnh</b><small>Một dải mỏng, cập nhật mỗi phút</small></div>
                  <Toggle label="Giờ trên ảnh" on={sset["sleep.clock"] !== "0"} onChange={(b) => setSleep("sleep.clock")(b ? "1" : "0")} />
                </div>
              </>
            )}
            {sleepMode === "cover" && (
              <>
                <h4>Bìa sách đang đọc</h4>
                <p className="ik-muted">Máy hiện bìa cuốn đang đọc dở. Chưa đọc cuốn nào thì hiện màn chờ thường.</p>
              </>
            )}
            {(sleepMode === "dashboard" || sleepMode === "auto") && (
              <>
                <h4>{SLEEP_PICKS.find((p) => p.id === dashLayout)?.name ?? "Màn chờ"}</h4>
                <p className="ik-muted">Mẫu cổ điển vẽ sẵn. Muốn tự sắp từng khối, chọn “Widget ngang” hoặc “Widget dọc”.</p>
                <div className="lay-row lay-blocks">
                  <div><b>Khối hiển thị</b></div>
                  <div className="ik-chips">
                    {BLOCKS.map((b) => {
                      const cur = (sset["dash.blocks"] || "").split(",").filter(Boolean);
                      const on = cur.includes(b.id);
                      return (
                        <button key={b.id} type="button" aria-pressed={on} className={on ? "on" : ""} onClick={() => setSleep("dash.blocks")((on ? cur.filter((x) => x !== b.id) : [...cur, b.id]).join(","))}>
                          {b.label}
                        </button>
                      );
                    })}
                  </div>
                </div>
              </>
            )}
            <SleepCommon v={(k) => sset[k]} set={setSleep} />
          </div>
          <aside className="lay-sleepprev">
            {sleepMode === "images" || sleepMode === "cover" ? (
              <PicturePreview mode={sleepMode as SleepMode} walls={walls} clock={sset["sleep.clock"] !== "0"} />
            ) : (
              <DashPreview d={data.dash} layout={(["desk", "calendar", "portrait"].includes(dashLayout) ? dashLayout : "desk") as DashLayout} />
            )}
            <p className="ik-muted ik-small">Màn hình máy lúc ngủ, vẽ từ dữ liệu thật. Lưu xong, máy nhận ở lần thức dậy tới.</p>
          </aside>
        </div>
      )}

      {ghost && (
        <div className="lay-ghost" style={{ left: ghost.x - 30, top: ghost.y - 20 }}>
          {(() => {
            const gb = cellBox(screen, ghost.item[3], ghost.item[4], skin);
            const pw = gb.w * scale, ph = gb.h * scale;
            return (
              <div style={{ width: pw, height: ph }}>
                <div style={{ transform: `scale(${scale})`, transformOrigin: "top left" }}>
                  <WidgetView it={ghost.item} w={gb.w} h={gb.h} data={preview} skin={skin} />
                </div>
              </div>
            );
          })()}
        </div>
      )}
    </section>
  );
}

function Inspector({
  item, screen, data, ink, box, photos, onPhotos, canResize, onChange, onDelete, onClose, skin, h12, onH12,
}: {
  onClose: () => void;
  item: LayoutItem;
  screen: Screen;
  skin: Skin;
  h12: boolean;
  onH12: (on: boolean) => void;
  data: PreviewData;
  ink: Ink;
  box: { w: number; h: number };
  photos: Photo[];
  onPhotos: () => void;
  canResize: (w: number, h: number) => boolean;
  onChange: (it: LayoutItem) => void;
  onDelete: (() => void) | null;
}) {
  const info = KINDS[item[0] as Kind];
  const S = SCREENS[screen];
  const [opt, setOpt] = useState(item[6]);
  const set = (patch: Partial<{ w: number; h: number; style: string; opt: string; inv: boolean }>) =>
    onChange(withFlags([item[0], item[1], item[2], patch.w ?? item[3], patch.h ?? item[4], patch.style ?? item[5], patch.opt ?? item[6]], patch.inv ?? inverted(item)));
  const [cdDate, cdLabel] = item[6].split("|");
  return (
    <>
      <button type="button" className="lay-back" onClick={onClose} title="Bỏ chọn widget (phím Esc)">
        <svg viewBox="0 0 16 16" width="14" height="14" aria-hidden="true">
          <path d="M10 3 5 8l5 5" fill="none" stroke="currentColor" strokeWidth="1.8" strokeLinecap="round" strokeLinejoin="round" />
        </svg>
        Cài đặt {S.label.toLowerCase()}
      </button>
      <h4>{info.name}</h4>
      <p className="ik-muted ik-small">
        Cỡ {item[3]}×{item[4]} ô{info.hint ? ` · OK trên máy: ${info.hint}` : ""}
      </p>
      {info.styles.length > 1 && (
        <>
          <div className="ik-label">Kiểu</div>
          <div className="lay-styles">
            {info.styles.map((s) => {
              const pw = box.w, ph = box.h;
              const k = Math.min(108 / pw, 56 / ph, 1);
              return (
                <button key={s.id} type="button" className={s.id === item[5] ? "on" : ""} onClick={() => set({ style: s.id })}>
                  <div className="lay-sprev">
                    <div style={{ width: pw * k, height: ph * k }}>
                      <div style={{ transform: `scale(${k})`, transformOrigin: "top left" }}>
                        <WidgetView it={[item[0], 0, 0, item[3], item[4], s.id, item[6], item[7]]} w={pw} h={ph} data={data} skin={skin} />
                      </div>
                    </div>
                  </div>
                  {s.label}
                </button>
              );
            })}
          </div>
        </>
      )}
      <div className="ik-label">Cỡ</div>
      <div className="lay-sizes">
        {info.sizes
          .filter(([w, h]) => w <= S.cols && h <= S.rows)
          .map(([w, h]) => {
            const on = w === item[3] && h === item[4];
            const ok = on || canResize(w, h);
            return (
              <button key={`${w}x${h}`} type="button" className={on ? "on" : ""} disabled={!ok} title={ok ? "" : "Không đủ chỗ trống"} onClick={() => set({ w, h })}>
                {w}×{h}
              </button>
            );
          })}
      </div>
      {info.opt === "tickers" && (
        <>
          <div className="ik-label">Mã chứng khoán</div>
          <input
            className="lay-input"
            value={opt}
            placeholder="VNINDEX,VN30,HNX,FPT"
            onChange={(e) => setOpt(e.target.value.toUpperCase().replace(/[^A-Z0-9,]/g, ""))}
            onBlur={() => set({ opt: opt.split(",").filter(Boolean).slice(0, 8).join(",") })}
          />
          <p className="ik-muted ik-small">Cách nhau bằng dấu phẩy, tối đa 8 mã. Chỉ số: VNINDEX, VN30, HNX, UPCOM. Server cập nhật giá 15 phút một lần.</p>
        </>
      )}
      {(info.opt === "gold" || info.opt === "fx" || info.opt === "coins") && (
        <CodePicker
          label={info.opt === "gold" ? "Loại vàng" : info.opt === "fx" ? "Ngoại tệ" : "Đồng tiền"}
          options={
            info.opt === "gold"
              ? (data.markets?.gold?.items ?? []).filter((q) => q.c).map((q) => ({ code: q.c as string, label: q.n }))
              : info.opt === "fx"
                ? (data.markets?.fx?.items ?? []).map((r) => ({ code: r.c, label: r.c }))
                : COINS.map((c) => ({ code: c, label: c }))
          }
          value={item[6] || DEFAULT_CODES[item[0]] || ""}
          max={info.opt === "gold" ? 8 : info.opt === "fx" ? 10 : 6}
          onChange={(v) => set({ opt: v })}
        />
      )}
      {info.opt === "countdown" && item[5] === "date" && (
        <>
          <div className="ik-label">Ngày và nhãn</div>
          <input className="lay-input" type="date" value={cdDate || ""} onChange={(e) => set({ opt: `${e.target.value}|${cdLabel || ""}` })} />
          <input className="lay-input" value={cdLabel || ""} maxLength={40} placeholder="Sinh nhật mẹ" onChange={(e) => set({ opt: `${cdDate || ""}|${e.target.value}` })} />
        </>
      )}
      {(item[0] === "clock" || item[0] === "overview") && (
        <div className="lay-row">
          <div>
            <b>Kiểu giờ</b>
            <small>Áp dụng cho mọi đồng hồ trên máy</small>
          </div>
          <Seg label="Kiểu giờ" value={h12 ? "12" : "24"} onChange={(v) => onH12(v === "12")} options={[{ v: "24", label: "24 giờ" }, { v: "12", label: "12 giờ" }]} />
        </div>
      )}
      {info.opt === "books" && (
        <div className="lay-row">
          <div>
            <b>Bỏ cuốn đang đọc</b>
            <small>Khi đã có widget “Đang đọc”, khỏi hiện trùng cuốn đó</small>
          </div>
          <Toggle label="Bỏ cuốn đang đọc" on={item[6].includes("s")} onChange={(on) => set({ opt: on ? "s" : "" })} />
        </div>
      )}
      {info.opt === "photo" && <PhotoPanel ink={ink} opt={item[6]} box={box} photos={photos} onPhotos={onPhotos} onChange={(v) => set({ opt: v })} />}
      <div className="lay-row">
        <div>
          <b>Đảo màu</b>
          <small>Nền đen, chữ trắng: làm điểm nhấn</small>
        </div>
        <Toggle label="Đảo màu" on={inverted(item)} onChange={(on) => set({ inv: on })} />
      </div>
      {onDelete && (
        <button type="button" className="ik-btn ghost sm danger" onClick={onDelete}>
          Xoá widget
        </button>
      )}
    </>
  );
}

/** Pick which codes a market widget shows, in the order picked (the device lists them that way). */
function CodePicker({ label, options, value, max, onChange }: { label: string; options: { code: string; label: string }[]; value: string; max: number; onChange: (v: string) => void }) {
  const picked = value.split(",").filter(Boolean);
  const toggle = (c: string) => {
    const next = picked.includes(c) ? picked.filter((x) => x !== c) : picked.length < max ? [...picked, c] : picked;
    if (next.length) onChange(next.join(","));
  };
  return (
    <>
      <div className="ik-label">{label}</div>
      {options.length === 0 ? (
        <p className="ik-muted ik-small">Server đang lấy danh sách, thử lại sau ít phút.</p>
      ) : (
        <div className="lay-codes">
          {options.map((o) => {
            const i = picked.indexOf(o.code);
            return (
              <button key={o.code} type="button" className={i >= 0 ? "on" : ""} aria-pressed={i >= 0} onClick={() => toggle(o.code)}>
                {i >= 0 && <span>{i + 1}</span>}
                {o.label}
              </button>
            );
          })}
        </div>
      )}
      <p className="ik-muted ik-small">Chọn tối đa {max}, theo thứ tự muốn hiện. Server cập nhật 15 phút một lần.</p>
    </>
  );
}

/** A template's screen in small, for the gallery. */
function MiniScreen({ tpl, data }: { tpl: Template; data: PreviewData }) {
  const S = SCREENS[tpl.screen];
  const k = tpl.screen === "wl" ? 0.19 : 0.15;
  const bx = boxesOf(tpl.items, tpl.screen, true, tpl.skin);
  return (
    <div className="lay-mini" style={{ width: S.w * k, height: S.h * k }}>
      <div className="lay-screen" style={{ width: S.w, height: S.h, transform: `scale(${k})` }}>
        <Chrome screen={tpl.screen} footer data={data} />
        {tpl.items.map((it, i) => (
          <div key={i} className="lay-w" style={{ left: bx[i].x, top: bx[i].y }}>
            <WidgetView it={it} w={bx[i].w} h={bx[i].h} data={data} skin={tpl.skin} />
          </div>
        ))}
      </div>
    </div>
  );
}

/** Pictures of a photo widget: pick from my pictures (or add new ones), how they change, and how they look. */
function PhotoPanel({ ink, opt, box, photos, onPhotos, onChange }: { ink: Ink; opt: string; box: { w: number; h: number }; photos: Photo[]; onPhotos: () => void; onChange: (v: string) => void }) {
  const o = parsePhotoOpt(opt);
  const [busy, setBusy] = useState("");
  const [err, setErr] = useState("");
  const [shot, setShot] = useState("");
  const [drop, setDrop] = useState(false);
  const input = useRef<HTMLInputElement>(null);
  const set = (p: Partial<PhotoOpt>) => onChange(formatPhotoOpt({ ...o, ...p }));
  const first = o.ids[0];
  const key = `${first}|${box.w}x${box.h}|${o.fit}${o.ink}${o.bright}`;
  useEffect(() => {
    let live = true;
    setShot("");
    if (!first) return;
    original(ink, first).then(
      (img) => live && setShot(bitsDataUrl(renderBits(img, box.w, box.h, o))),
      (e: Error) => live && setErr(e.message),
    );
    return () => {
      live = false;
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [key]);
  const add = async (files: FileList | File[]) => {
    setErr("");
    const list = Array.from(files).filter((f) => f.type.startsWith("image/"));
    let n = 0;
    for (const f of list) {
      setBusy(`Đang tải ảnh ${++n}/${list.length}…`);
      try {
        await uploadPhoto(ink, f);
      } catch (x) {
        setErr((x as Error).message);
      }
    }
    setBusy("");
    const fresh = await ink.photos().catch(() => [] as Photo[]);
    onPhotos();
    // New pictures join this widget (up to the limit).
    const known = new Set(photos.map((p) => p.id));
    const added = fresh.filter((p) => !known.has(p.id)).map((p) => p.id);
    if (added.length) set({ ids: [...o.ids, ...added].slice(0, PHOTO_MAX) });
  };
  const toggle = (id: number) => set({ ids: o.ids.includes(id) ? o.ids.filter((x) => x !== id) : o.ids.length < PHOTO_MAX ? [...o.ids, id] : o.ids });
  const seg = <T extends string>(value: T, opts: [T, string][], on: (v: T) => void) => (
    <div className="lay-seg">
      {opts.map(([v, l]) => (
        <button key={v} type="button" className={v === value ? "on" : ""} onClick={() => on(v)}>
          {l}
        </button>
      ))}
    </div>
  );
  return (
    <>
      <div className="ik-label">Ảnh trong ô · {o.ids.length}/{PHOTO_MAX}</div>
      <div
        className={"lay-photos" + (drop ? " drop" : "")}
        onDragOver={(e) => { e.preventDefault(); setDrop(true); }}
        onDragLeave={() => setDrop(false)}
        onDrop={(e) => { e.preventDefault(); setDrop(false); void add(e.dataTransfer.files); }}
      >
        {photos.map((p) => {
          const i = o.ids.indexOf(p.id);
          return (
            <button key={p.id} type="button" className={"lay-ph" + (i >= 0 ? " on" : "")} style={{ backgroundImage: `url(${p.thumb})` }} aria-pressed={i >= 0} title={i >= 0 ? "Bỏ khỏi ô này" : "Thêm vào ô này"} onClick={() => toggle(p.id)}>
              {i >= 0 && <span>{i + 1}</span>}
            </button>
          );
        })}
        <button type="button" className="lay-ph add" onClick={() => input.current?.click()}>
          <b>＋</b>
          <small>Kéo ảnh vào đây</small>
        </button>
      </div>
      <input ref={input} type="file" accept="image/*" multiple hidden onChange={(e) => { const f = e.target.files; if (f) void add(f); e.target.value = ""; }} />
      {busy && <p className="ik-muted ik-small">{busy}</p>}
      {err && <p className="ik-err ik-small">{err}</p>}
      <div className="ik-label">Đổi ảnh</div>
      {seg(o.mode, [["r", "Ngẫu nhiên"], ["s", "Lần lượt"], ["f", "Giữ 1 ảnh"]], (mode) => set({ mode }))}
      {o.mode !== "f" && (
        <>
          <div className="ik-label">Đổi khi</div>
          {seg(o.every, [["v", "Mỗi lần mở"], ["h", "Mỗi giờ"], ["d", "Mỗi ngày"]], (every) => set({ every }))}
        </>
      )}
      <div className="ik-label">Cách đặt ảnh</div>
      {seg(o.fit, [["c", "Lấp đầy ô"], ["n", "Vừa khung"]], (fit) => set({ fit }))}
      <div className="ik-label">Kiểu đen trắng</div>
      {seg(o.ink, [["a", "Chấm mịn"], ["t", "Nét đậm"]], (v) => set({ ink: v }))}
      <div className="ik-label">Độ sáng</div>
      <input type="range" min={0} max={99} value={o.bright} onChange={(e) => set({ bright: +e.target.value })} className="lay-range" />
      {first ? (
        <>
          <div className="ik-label">Trên máy · {box.w}×{box.h} điểm ảnh</div>
          {shot ? <img className="lay-phshot" src={shot} alt="Ảnh như trên máy" /> : <p className="ik-muted ik-small">Đang chuyển ảnh…</p>}
        </>
      ) : (
        <p className="ik-muted ik-small">Chọn hoặc kéo ảnh vào khung trên. Bấm vào ảnh để thêm hay bỏ khỏi ô này.</p>
      )}
      {photos.length > 0 && (
        <details className="lay-phmanage">
          <summary>Xoá ảnh khỏi kho</summary>
          <div className="lay-photos">
            {photos.map((p) => (
              <button key={p.id} type="button" className="lay-ph del" style={{ backgroundImage: `url(${p.thumb})` }} title="Xoá hẳn ảnh này" onClick={async () => {
                await ink.deletePhoto(p.id).catch((x: Error) => setErr(x.message));
                if (o.ids.includes(p.id)) set({ ids: o.ids.filter((x) => x !== p.id) });
                onPhotos();
              }}>
                <span>✕</span>
              </button>
            ))}
          </div>
        </details>
      )}
    </>
  );
}

/** Settings every sleep screen shares: how often it updates, the minute clock, charging, quiet hours. */
function SleepCommon({ v, set }: { v: (k: string) => string; set: (k: string) => (val: string) => void }) {
  return (
    <div className="lay-common">
      <div className="ik-label">Cập nhật và tiết kiệm pin</div>
      <div className="lay-row">
        <div><b>Cập nhật dữ liệu</b><small>Thời tiết, lịch, giá… cần WiFi</small></div>
        <Seg label="Cập nhật dữ liệu" value={v("dash.interval_min")} onChange={set("dash.interval_min")} options={[{ v: "5", label: "5" }, { v: "15", label: "15" }, { v: "30", label: "30" }, { v: "60", label: "60 phút" }]} />
      </div>
      <div className="lay-row">
        <div><b>Đồng hồ mỗi phút</b><small>Không cần WiFi. Tự tắt ban đêm và khi tiết kiệm pin</small></div>
        <Toggle label="Đồng hồ mỗi phút" on={v("dash.clock") !== "0"} onChange={(b) => set("dash.clock")(b ? "1" : "0")} />
      </div>
      <div className="lay-row">
        <div><b>Cắm sạc thì cập nhật mỗi phút</b></div>
        <Toggle label="Khi cắm sạc" on={v("dash.charging_min") === "1"} onChange={(b) => set("dash.charging_min")(b ? "1" : "0")} />
      </div>
      <div className="lay-row">
        <div>
          <b>Tiết kiệm pin khi pin dưới</b>
          <small>
            {v("dash.save_pct") === "0"
              ? "Máy luôn tự cập nhật và chạy đồng hồ, kể cả khi pin yếu"
              : `Dưới ${v("dash.save_pct")}% máy không tự cập nhật (cả lúc mở máy) và ngừng đồng hồ cho tới khi sạc. Vẫn đồng bộ tay được`}
          </small>
        </div>
        <Seg label="Tiết kiệm pin khi pin dưới" value={v("dash.save_pct")} onChange={set("dash.save_pct")} options={[{ v: "0", label: "Tắt" }, { v: "10", label: "10" }, { v: "20", label: "20" }, { v: "30", label: "30%" }]} />
      </div>
      <div className="lay-row">
        <div><b>Giờ yên lặng</b><small>Ban đêm máy không thức dậy</small></div>
        <span className="ik-time">
          <input type="time" aria-label="Từ" value={v("dash.quiet_from")} onChange={(e) => set("dash.quiet_from")(e.target.value)} />
          –
          <input type="time" aria-label="Đến" value={v("dash.quiet_to")} onChange={(e) => set("dash.quiet_to")(e.target.value)} />
        </span>
      </div>
    </div>
  );
}
