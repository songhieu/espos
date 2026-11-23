// Product presets (docs/app-slots.md §17): the starting point of one product, in the open format
// sdk/schemas/preset-v1.schema.json. Two jobs here:
//   checkPreset:   is a preset file well formed (rules P001-P014, `espos preset check`);
//   compilePreset: the device's file /sys/preset.json for one board (`espos preset compile`, tools/make_bundle.py
//                  --preset): the board's parts picked, the saver template, the orientation and a fixed locale
//                  turned into settings, so the firmware only merges keys (src/preset.cpp).
// A preset never locks anything. Its entitlements are names (@aquarium-full) that only the seller's server resolves.
import fs from "node:fs";
import path from "node:path";
import type { Problem } from "./espk.js";
import { checkDef } from "./jsonschema.js";
import { sdkDir } from "./repo.js";

export const PRESET_RULES = [
  { code: "P001", level: "error" as const, text: "the preset's shape (sdk/schemas/preset-v1.schema.json): preset 1, id, version, name, boards, and only the fields it lists" },
  { code: "P002", level: "error" as const, text: "settings: a key a preset may set (s3.*, sys.lang, sys.tz, dash.city/lat/lon/quiet_from/quiet_to, news.*, dev.fw_auto, app.<id>.<key>); not the device's own lists (s3.pkgs, s3.onboard), personal ones (s3.call_name, dash.ics_url, dev.name), the slots (use \"slots\") or sys.tz_min (from locale.timezone)" },
  { code: "P003", level: "error" as const, text: "settings: a value the device accepts for that key (s3.rot 0-3, s3.theme, s3.saver minutes, JSON for s3.home / s3.home_l / s3.saver_pages, a dog key for s3.pet_dog, a language the device has)" },
  { code: "P004", level: "error" as const, text: "settings app.<id>.<key>: <id> must be a package the preset installs" },
  { code: "P005", level: "error" as const, text: "packages: each id once" },
  { code: "P006", level: "warning" as const, text: "packages: not in the store's catalog, or a part the catalog does not have (checked when a catalog is given)" },
  { code: "P007", level: "error" as const, text: "slots: the assistant (unless none) and the saver (unless builtin) must be packages the preset installs" },
  { code: "P008", level: "warning" as const, text: "slots.assistant is not an assistant app in the catalog (\"provides\": [\"assistant\"], or PawBoop's engine)" },
  { code: "P009", level: "error" as const, text: "saver.template: a template of the saver templates file (when one is given)" },
  { code: "P010", level: "warning" as const, text: "home.template: only \"default\" (the device's own Home) is applied by the tools offline; others need --home-templates" },
  { code: "P011", level: "error" as const, text: "locale: a fixed city needs lat and lon; \"ask\" takes none" },
  { code: "P012", level: "error" as const, text: "a key set twice: settings and saver.template (s3.saver_pages), orientation (s3.rot), locale (sys.lang, sys.tz, dash.city)" },
  { code: "P013", level: "warning" as const, text: "a part made for a board the preset does not name" },
  { code: "P014", level: "error" as const, text: "compile: the board must be one of the preset's boards" },
];

const LANGS = ["en", "vi", "zh-Hans", "es", "fr", "de", "ja", "ko", "pt-BR", "ru", "id"];
const DOGS = ["bao", "momo", "boop", "leaf", "volt", "byte", "shiba", "corgi", "husky", "golden"];
// What a preset may set besides app.<id>.<key>: the device's own settings (device_sync deviceDefaults for kind s3, the
// web's shared keys). Not here: lists the device keeps itself (s3.pkgs, s3.onboard), personal values (s3.call_name,
// dash.ics_url, dev.name), the slots (preset "slots") and sys.tz_min (worked out from locale.timezone).
const OS_KEYS = new Set([
  "s3.p1", "s3.p2", "s3.l1", "s3.l2", "s3.bright", "s3.volume", "s3.rot", "s3.saver", "s3.click", "s3.dnd", "s3.saver_mode",
  "s3.home", "s3.home_l", "s3.mywidgets", "s3.notes", "s3.alarms", "s3.todo", "s3.theme", "s3.theme_auto", "s3.wall",
  "s3.cities", "s3.photos", "s3.notif", "s3.notif_preview", "s3.pkg_auto", "s3.saver_pages", "s3.markets", "s3.pet_dog",
  "s3.assist", "sys.lang", "sys.tz", "sys.clock_fmt", "dash.city", "dash.lat", "dash.lon", "dash.quiet_from", "dash.quiet_to",
  "news.sections", "news.per_section", "dev.fw_auto",
]);
const ENUMS: Record<string, string[]> = {
  "s3.rot": ["0", "1", "2", "3"], "s3.saver": ["0", "60", "120", "300", "600"], "s3.click": ["0", "1"], "s3.dnd": ["0", "1"],
  "s3.saver_mode": ["clock", "desk", "photo"], "s3.theme": ["", "color", "light", "dark", "photo"], "s3.theme_auto": ["", "0", "1"],
  "s3.notif_preview": ["", "0", "1"], "s3.pkg_auto": ["", "0", "1"], "s3.pet_dog": ["", ...DOGS], "sys.lang": LANGS,
  "sys.clock_fmt": ["24", "12"], "news.per_section": ["3", "5", "10"], "dev.fw_auto": ["0", "1"],
};
const JSON_KEYS = new Set(["s3.home", "s3.home_l", "s3.mywidgets", "s3.notes", "s3.alarms", "s3.todo", "s3.saver_pages", "s3.assist", "s3.notif"]);
const NUMBER_KEYS: Record<string, [number, number]> = { "s3.bright": [5, 100], "s3.volume": [0, 100] };
const APP_KEY = /^app\.([a-z0-9][a-z0-9._-]{1,63})\.([a-z][a-z0-9_]{0,23})$/;
const HHMM = /^([01][0-9]|2[0-3]):[0-5][0-9]$/;

type Obj = Record<string, any>;
const isObj = (v: unknown): v is Obj => typeof v === "object" && v !== null && !Array.isArray(v);

export interface CatalogPart {
  id: string;
  group?: string;
  board?: string;
  req?: boolean;
  default?: boolean;
}
export interface CatalogEntry {
  id: string;
  engine?: string;
  addon_of?: string;
  provides?: string[];
  parts?: CatalogPart[];
}
export interface PresetInputs {
  catalog?: CatalogEntry[];
  saverTemplates?: Obj; // web/app/src/os/saver-templates.json: {"templates": [{id, p, l}]}
  homeTemplates?: Record<string, unknown>; // {id: s3.home value}
}

let schemaCache: Obj | null = null;
export function presetSchema(): Obj {
  if (!schemaCache) schemaCache = JSON.parse(fs.readFileSync(path.join(sdkDir(), "schemas", "preset-v1.schema.json"), "utf8"));
  return schemaCache!;
}

/** The value problems of one OS setting ("" when fine). */
export function settingValueProblem(k: string, v: string): string {
  if (ENUMS[k] && !ENUMS[k].includes(v)) return `${k}: "${v}" is not one of ${ENUMS[k].map((x) => JSON.stringify(x)).join(", ")}`;
  if (NUMBER_KEYS[k]) {
    const n = Number(v);
    const [lo, hi] = NUMBER_KEYS[k];
    if (!/^[0-9]{1,3}$/.test(v) || n < lo || n > hi) return `${k}: a whole number ${lo}-${hi}`;
  }
  if (JSON_KEYS.has(k) && v !== "") {
    try {
      JSON.parse(v);
    } catch {
      return `${k}: not JSON`;
    }
  }
  if ((k === "dash.quiet_from" || k === "dash.quiet_to") && !HHMM.test(v)) return `${k}: HH:MM`;
  if ((k === "dash.lat" && !/^-?[0-9]{1,2}(\.[0-9]{1,6})?$/.test(v)) || (k === "dash.lon" && !/^-?[0-9]{1,3}(\.[0-9]{1,6})?$/.test(v))) return `${k}: a coordinate`;
  if (k === "sys.tz" && !/^([A-Za-z_]+(\/[A-Za-z0-9_+-]+){0,2}|UTC|)$/.test(v)) return `${k}: an IANA time zone`;
  return "";
}

/** The board-specific parts of a package for `board`, from the catalog (a group whose part names that board). */
function partsFor(entry: CatalogEntry | undefined, given: string[] | undefined, board: string): string[] {
  const all = entry?.parts ?? [];
  let parts = given ? [...given] : all.filter((p) => p.req || p.default).map((p) => p.id);
  if (!entry) return parts;
  // a part made for another board goes; its group's part for this board comes in its place
  const groups = new Set<string>();
  parts = parts.filter((id) => {
    const p = all.find((x) => x.id === id);
    if (p?.board && p.board !== board) {
      if (p.group) groups.add(p.group);
      return false;
    }
    return true;
  });
  for (const p of all) {
    if (p.board === board && (groups.has(p.group ?? "") || (!given && p.group && all.some((x) => x.group === p.group && x.default)))) {
      if (!parts.includes(p.id)) parts.push(p.id);
    }
  }
  if (!parts.length && all.length) parts = all.filter((p) => p.req).map((p) => p.id);
  return parts;
}

export function checkPreset(p: unknown, where: string, inputs: PresetInputs = {}): Problem[] {
  const out: Problem[] = [];
  const add = (code: string, msg: string, level?: Problem["level"]) =>
    out.push({ level: level ?? PRESET_RULES.find((r) => r.code === code)?.level ?? "error", code, where, msg });
  for (const e of checkDef(presetSchema(), "preset", p)) add("P001", e);
  if (!isObj(p)) return out;
  const pkgs: Obj[] = Array.isArray(p.packages) ? p.packages.filter(isObj) : [];
  const ids = new Set<string>();
  for (const k of pkgs) {
    if (ids.has(k.id)) add("P005", `packages: ${k.id} twice`);
    ids.add(k.id);
  }
  const cat = inputs.catalog;
  if (cat) {
    for (const k of pkgs) {
      const e = cat.find((x) => x.id === k.id);
      if (!e) {
        add("P006", `packages: ${k.id} is not in the catalog`);
        continue;
      }
      for (const part of k.parts ?? []) {
        const cp = (e.parts ?? []).find((x) => x.id === part);
        if (!cp) add("P006", `packages: ${k.id} has no part ${part}`);
        else if (cp.board && Array.isArray(p.boards) && !p.boards.includes(cp.board)) add("P013", `packages: ${k.id} part ${part} is made for ${cp.board}`);
      }
    }
  }
  const settings: Obj = isObj(p.settings) ? p.settings : {};
  for (const [k, v] of Object.entries(settings)) {
    if (typeof v !== "string") continue; // P001 says so
    const m = APP_KEY.exec(k);
    if (m) {
      if (!ids.has(m[1])) add("P004", `settings ${k}: ${m[1]} is not among the preset's packages`);
      continue;
    }
    if (!OS_KEYS.has(k)) {
      add("P002", `settings: ${k} is not a key a preset sets`);
      continue;
    }
    const why = settingValueProblem(k, v);
    if (why) add("P003", `settings ${why}`);
  }
  const slots: Obj = isObj(p.slots) ? p.slots : {};
  if (typeof slots.assistant === "string" && slots.assistant !== "none") {
    if (!ids.has(slots.assistant)) add("P007", `slots.assistant: ${slots.assistant} is not among the preset's packages`);
    else if (cat) {
      const e = cat.find((x) => x.id === slots.assistant);
      if (e && !(e.provides ?? []).includes("assistant") && !(e.engine === "pet" && !e.addon_of)) add("P008", `slots.assistant: ${slots.assistant} is not an assistant app`);
    }
  }
  if (typeof slots.saver === "string" && slots.saver !== "builtin" && !ids.has(slots.saver)) add("P007", `slots.saver: ${slots.saver} is not among the preset's packages`);
  const saverT = isObj(p.saver) ? p.saver.template : undefined;
  if (typeof saverT === "string") {
    if ("s3.saver_pages" in settings) add("P012", "s3.saver_pages is set in settings and by saver.template");
    const list = inputs.saverTemplates?.templates ?? inputs.saverTemplates?.t;
    if (Array.isArray(list) && !list.some((t: Obj) => t.id === saverT)) add("P009", `saver.template: no template "${saverT}"`);
  }
  const homeT = isObj(p.home) ? p.home.template : undefined;
  if (typeof homeT === "string") {
    if ("s3.home" in settings) add("P012", "s3.home is set in settings and by home.template");
    if (homeT !== "default" && !(inputs.homeTemplates && homeT in inputs.homeTemplates)) add("P010", `home.template "${homeT}": applied as the device's own Home unless --home-templates has it`);
  }
  if (typeof p.orientation === "string" && p.orientation !== "ask" && "s3.rot" in settings) add("P012", "s3.rot is set in settings and by orientation");
  const loc: Obj = isObj(p.locale) ? p.locale : {};
  if (typeof loc.lang === "string" && loc.lang !== "ask") {
    if (!LANGS.includes(loc.lang)) add("P003", `locale.lang: "${loc.lang}" is not a language the device has (${LANGS.join(", ")})`);
    if ("sys.lang" in settings) add("P012", "sys.lang is set in settings and by locale.lang");
  }
  if (typeof loc.timezone === "string" && loc.timezone !== "ask" && "sys.tz" in settings) add("P012", "sys.tz is set in settings and by locale.timezone");
  if (typeof loc.city === "string") {
    if (loc.city === "ask" ? loc.lat !== undefined || loc.lon !== undefined : loc.lat === undefined || loc.lon === undefined) {
      add("P011", loc.city === "ask" ? "locale: city \"ask\" takes no lat or lon" : `locale: the city "${loc.city}" needs lat and lon`);
    }
    if (loc.city !== "ask" && ("dash.city" in settings || "dash.lat" in settings || "dash.lon" in settings)) add("P012", "dash.city is set in settings and by locale.city");
  }
  return out;
}

/** The UTC offset of an IANA zone now, in minutes (sys.tz_min, what the device's clock uses). */
export function tzOffsetMin(zone: string, at = new Date()): number {
  const parts = new Intl.DateTimeFormat("en-US", { timeZone: zone, hourCycle: "h23", year: "numeric", month: "2-digit", day: "2-digit", hour: "2-digit", minute: "2-digit" }).formatToParts(at);
  const g = (t: string) => Number(parts.find((x) => x.type === t)?.value);
  const local = Date.UTC(g("year"), g("month") - 1, g("day"), g("hour"), g("minute"));
  return Math.round((local - Math.floor(at.getTime() / 60000) * 60000) / 60000);
}

/** The device's /sys/preset.json for one board. Throws on P014 (board) and on a template it cannot find. */
export function compilePreset(p: Obj, board: string, inputs: PresetInputs = {}, now = new Date()): Obj {
  if (!Array.isArray(p.boards) || !p.boards.includes(board)) throw new Error(`P014: ${board} is not one of the preset's boards (${(p.boards ?? []).join(", ")})`);
  const settings: Record<string, string> = { ...(isObj(p.settings) ? p.settings : {}) };
  const ask: string[] = [];
  const slots: Obj = isObj(p.slots) ? p.slots : {};
  if (typeof slots.assistant === "string") settings["slot.assistant"] = slots.assistant;
  if (typeof slots.saver === "string") settings["slot.saver"] = slots.saver;
  const saverT = isObj(p.saver) ? p.saver.template : undefined;
  if (typeof saverT === "string" && inputs.saverTemplates) {  // without the templates file: the device's own saver
    const list: Obj[] = inputs.saverTemplates.templates ?? inputs.saverTemplates.t ?? [];
    const t = list.find((x) => x.id === saverT);
    if (!t) throw new Error(`P009: no saver template "${saverT}"`);
    const pages = (x: unknown) => (Array.isArray(x) ? x : x ? [x] : []);
    settings["s3.saver_pages"] = JSON.stringify({ v: 1, p: pages(t.p), l: pages(t.l) });
  }
  const homeT = isObj(p.home) ? p.home.template : undefined;
  if (typeof homeT === "string" && homeT !== "default" && inputs.homeTemplates?.[homeT] !== undefined) {
    const h = inputs.homeTemplates[homeT];
    settings["s3.home"] = typeof h === "string" ? h : JSON.stringify(h);
  }
  if (p.orientation === "ask") ask.push("orientation");
  else if (p.orientation === "portrait") settings["s3.rot"] = "0";
  else if (p.orientation === "landscape") settings["s3.rot"] = board === "es3c28p" ? "3" : "1"; // as the web installer's ROT (the 2.8" case stands on its other side)
  const loc: Obj = isObj(p.locale) ? p.locale : {};
  if (loc.lang === "ask") ask.push("lang");
  else if (typeof loc.lang === "string") settings["sys.lang"] = loc.lang;
  if (loc.timezone === "ask") ask.push("timezone");
  else if (typeof loc.timezone === "string") {
    settings["sys.tz"] = loc.timezone;
    settings["sys.tz_min"] = String(tzOffsetMin(loc.timezone, now));
  }
  if (loc.city === "ask") ask.push("city");
  else if (typeof loc.city === "string") Object.assign(settings, { "dash.city": loc.city, "dash.lat": String(loc.lat), "dash.lon": String(loc.lon) });
  const dev: Obj = { preset: 1, id: p.id, version: p.version, name: p.name, board };
  dev.packages = (Array.isArray(p.packages) ? p.packages : []).map((k: Obj) => ({ id: k.id, parts: partsFor(inputs.catalog?.find((x) => x.id === k.id), k.parts, board) }));
  dev.settings = Object.fromEntries(Object.entries(settings).sort(([a], [b]) => (a < b ? -1 : a > b ? 1 : 0)));
  if (ask.length) dev.ask = ask;
  const skip = isObj(p.onboarding) && Array.isArray(p.onboarding.skip) ? p.onboarding.skip : [];
  if (skip.length) dev.skip = skip;
  if (isObj(p.branding)) dev.branding = p.branding;
  return dev;
}

/** Reads the optional inputs from files (the commands' --catalog, --saver-templates, --home-templates). */
export function readInputs(o: { catalog?: string; saverTemplates?: string; homeTemplates?: string }): PresetInputs {
  const read = (f?: string) => (f ? JSON.parse(fs.readFileSync(f, "utf8")) : undefined);
  const cat = read(o.catalog);
  return { catalog: cat ? (Array.isArray(cat) ? cat : cat.packages) : undefined, saverTemplates: read(o.saverTemplates), homeTemplates: read(o.homeTemplates) };
}
