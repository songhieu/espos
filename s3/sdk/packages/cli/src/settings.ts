// The manifest's "settings" schema (manifest v2, docs/manifest-driven.md §1): one list of fields that the device and
// the web each draw as a form, whose values sync as the device settings "app.<id>.<key>". Two jobs here:
//   checkSettings: is a manifest's schema well formed (rules S001-S012, `espos check`);
//   checkValue:    is one stored value valid for its field. The server (put_device_settings, device_sync), the web
//                  form and the device apply the same rules; sdk/schemas/settings-vectors.json holds the cases every
//                  implementation's tests run, so they cannot drift apart.
// Values are always strings: a bool is "1"/"0", a list or a city is JSON.
import type { Problem } from "./espk.js";

export const SETTING_TYPES = ["string", "text", "number", "bool", "enum", "list", "time", "date", "color", "url", "city", "photo", "secret", "section"];
/** The types a list's items may have. */
export const LIST_ITEM_TYPES = ["string", "enum"];
export const SECRET_WHERE = ["device", "server"];
export const SECRET_AS = /^(bearer|basic_user|basic_pass|header:[A-Za-z0-9-]{1,40}|query:[A-Za-z0-9_.-]{1,40})$/;
export const KEY_RE = /^[a-z][a-z0-9_]{0,23}$/;
export const OPTION_RE = /^[A-Za-z0-9_.+-]{1,40}$/;
export const PHOTO_RE = /^[a-z0-9_-]{1,40}$/;
export const MAX_FIELDS = 40;
export const MAX_VALUE = 2000; // characters of one stored value
export const MAX_APP_BYTES = 8192; // all of one package's values on one device
export const STRING_MAX = 200;
export const STRING_DEFAULT_MAX = 80;
export const TEXT_MAX = 2000;
export const TEXT_DEFAULT_MAX = 500;
export const URL_MAX = 300;
export const LIST_MAX_ITEMS = 50;
/** The languages a label may be translated into (the S3's languages, validate.ts LANG_TAGS). */
const LANGS = ["en", "vi", "zh-Hans", "es", "fr", "de", "ja", "ko", "pt-BR", "ru", "id"];

export const SETTINGS_RULES = [
  { code: "S001", level: "error" as const, text: `settings: a list of at most ${MAX_FIELDS} fields` },
  { code: "S002", level: "error" as const, text: `a field: an object whose "type" is one of ${SETTING_TYPES.join(", ")}` },
  { code: "S003", level: "error" as const, text: 'key: 1-24 of a-z 0-9 _, starting with a letter, unique in the list ("section" has none)' },
  { code: "S004", level: "error" as const, text: "label: text of 1-40 characters; help: at most 120" },
  { code: "S005", level: "error" as const, text: "string max 1-200, text max 1-2000; number min < max, step > 0; enum: 2-20 options {v, label} with different v" },
  { code: "S006", level: "error" as const, text: 'list: "of" is a string or enum item, max_items 1-50' },
  { code: "S007", level: "error" as const, text: 'secret: "where" device (kept on the device only) or server (kept by the server); no default; it reaches requests only through an "auth" binding' },
  { code: "S008", level: "error" as const, text: "default: a valid value of the field's type (none for city, photo, secret, section)" },
  { code: "S009", level: "error" as const, text: 'i18n: {"<language>": {"label", "help", "options": {"<v>": "<label>"}}} in the device\'s languages' },
  { code: "S010", level: "warning" as const, text: "an auth binding's send_to host is not covered by perms net:<host>, net:* or net:setting:<key>: the device will not send it" },
  { code: "S011", level: "error" as const, text: 'a {placeholder} in an auth binding\'s send_to names a "url" or "string" field of the settings, or is {espos} (the device\'s own server, perm net:espos)' },
  { code: "S012", level: "warning" as const, text: 'store_as: only the OS\'s own packages (dev.espos.*) may point a field at an existing setting; the server ignores it for others' },
  { code: "S013", level: "error" as const, text: 'auth: {"<name>": {"basic": [user, pass]} | {"bearer": key} | {"header": {"<Name>": key}} | {"device": true} (the device\'s own credential, {espos}/v1/device/app/<id>/* only), "query": {"<param>": key} (optional), "send_to": [1-4 patterns https://host/path or {url_field}/path]}, at most 8 bindings' },
  { code: "S014", level: "error" as const, text: "auth: every key a binding uses is a secret field of the settings" },
];

const isObj = (v: unknown): v is Record<string, any> => !!v && typeof v === "object" && !Array.isArray(v);
const isStr = (v: unknown): v is string => typeof v === "string";
const len = (s: string) => [...s].length;
const isNum = (v: unknown): v is number => typeof v === "number" && Number.isFinite(v);
// eslint-disable-next-line no-control-regex
const CONTROL = /[\u0000-\u0008\u000b\u000c\u000e-\u001f\u007f]/;

type Put = (code: string, msg: string, level?: Problem["level"]) => void;

/** One field's rules, written so the server, the web and the device can port them line for line. */
export function checkValue(f: Record<string, any>, raw: unknown): string {
  if (!isStr(raw)) return "a value is text";
  if (len(raw) > MAX_VALUE) return `at most ${MAX_VALUE} characters`;
  if (CONTROL.test(raw) || (f.type !== "text" && /[\r\n]/.test(raw))) return "no control characters";
  switch (f.type) {
    case "string": {
      const max = isNum(f.max) ? f.max : STRING_DEFAULT_MAX;
      return len(raw) <= max ? "" : `at most ${max} characters`;
    }
    case "text": {
      const max = isNum(f.max) ? f.max : TEXT_DEFAULT_MAX;
      return len(raw) <= max ? "" : `at most ${max} characters`;
    }
    case "number": {
      if (!/^-?(\d+(\.\d+)?|\.\d+)$/.test(raw)) return "a number like 4 or 2.5";
      const v = Number(raw);
      if (isNum(f.min) && v < f.min) return `at least ${f.min}`;
      if (isNum(f.max) && v > f.max) return `at most ${f.max}`;
      if (f.int && !Number.isInteger(v)) return "a whole number";
      if (isNum(f.step) && f.step > 0) {
        const n = (v - (isNum(f.min) ? f.min : 0)) / f.step;
        if (Math.abs(n - Math.round(n)) > 1e-9) return `in steps of ${f.step}`;
      }
      return "";
    }
    case "bool":
      return raw === "1" || raw === "0" ? "" : '"1" or "0"';
    case "enum":
      return Array.isArray(f.options) && f.options.some((o: any) => isObj(o) && o.v === raw) ? "" : "one of the options";
    case "list": {
      let a: unknown;
      try {
        a = JSON.parse(raw);
      } catch {
        return "a JSON list";
      }
      if (!Array.isArray(a)) return "a JSON list";
      const max = isNum(f.max_items) ? f.max_items : LIST_MAX_ITEMS;
      if (a.length > max) return `at most ${max} items`;
      const of = isObj(f.of) ? f.of : { type: "string" };
      for (const x of a) {
        if (!isStr(x)) return "each item is text";
        const e = checkValue(of, x);
        if (e) return `an item: ${e}`;
      }
      return "";
    }
    case "time": {
      const m = /^(\d\d):(\d\d)$/.exec(raw);
      return m && +m[1] < 24 && +m[2] < 60 ? "" : "HH:MM (00:00 to 23:59)";
    }
    case "date": {
      const m = /^(\d{4})-(\d\d)-(\d\d)$/.exec(raw);
      if (!m) return "YYYY-MM-DD";
      const d = new Date(Date.UTC(+m[1], +m[2] - 1, +m[3]));
      return d.getUTCFullYear() === +m[1] && d.getUTCMonth() === +m[2] - 1 && d.getUTCDate() === +m[3] ? "" : "a real date";
    }
    case "color":
      return /^#[0-9a-fA-F]{6}$/.test(raw) ? "" : "#rrggbb";
    case "url":
      return len(raw) <= URL_MAX && /^https:\/\/[^\s/?#]+\.[^\s/?#]+([/?#]\S*)?$/.test(raw) ? "" : `an https:// address of at most ${URL_MAX} characters`;
    case "city": {
      let c: any;
      try {
        c = JSON.parse(raw);
      } catch {
        return 'JSON {"name", "lat", "lon"}';
      }
      if (!isObj(c) || !isStr(c.name) || len(c.name.trim()) < 1 || len(c.name) > 80) return 'JSON {"name": 1-80 characters, "lat", "lon"}';
      if (!isNum(c.lat) || c.lat < -90 || c.lat > 90 || !isNum(c.lon) || c.lon < -180 || c.lon > 180) return "lat -90..90 and lon -180..180";
      return "";
    }
    case "photo":
      return PHOTO_RE.test(raw) ? "" : "a photo id";
    case "secret":
      return "a secret is never a setting value (the OS keeps it apart)";
    case "section":
      return "a section has no value";
  }
  return "unknown type";
}

/** The fields of a schema by key (sections skipped), for the server and the tests. */
export function fieldsByKey(settings: unknown): Map<string, Record<string, any>> {
  const out = new Map<string, Record<string, any>>();
  if (Array.isArray(settings)) for (const f of settings) if (isObj(f) && isStr(f.key) && f.type !== "section") out.set(f.key, f);
  return out;
}

/** A send_to pattern: a fixed https:// address, or one that starts with a url field's value ("{shop_url}/wp-json/*"). */
const SEND_TO_RE = /^(https:\/\/[^\s/?#]+|\{[a-z][a-z0-9_]{0,23}\})\S*$/;

function hostOf(pattern: string): string {
  const m = /^https:\/\/([^/?#]+)/.exec(pattern);
  return m ? m[1].toLowerCase() : "";
}

/** A manifest's "settings" (S001-S012). `perms` decide whether secrets' hosts are covered; `id` whether store_as counts. */
export function checkSettings(settings: unknown, perms: unknown, id: unknown, P: Put): void {
  if (!Array.isArray(settings) || settings.length > MAX_FIELDS) {
    P("S001", `settings: a list of at most ${MAX_FIELDS} fields`);
    return;
  }
  const permList = Array.isArray(perms) ? perms.filter(isStr) : [];
  const keys = new Set<string>();
  const urlish = new Set<string>();
  for (const f of settings) if (isObj(f) && isStr(f.key) && (f.type === "url" || f.type === "string")) urlish.add(f.key);
  settings.forEach((f: unknown, i: number) => {
    const at = `settings[${i}]`;
    if (!isObj(f) || !SETTING_TYPES.includes(f.type)) {
      P("S002", `${at}: an object whose "type" is one of ${SETTING_TYPES.join(", ")}`);
      return;
    }
    const name = f.type === "section" ? at : `${at} (${String(f.key)})`;
    if (f.type !== "section") {
      if (!isStr(f.key) || !KEY_RE.test(f.key)) P("S003", `${at}: key ${JSON.stringify(f.key)}; 1-24 of a-z 0-9 _, starting with a letter`);
      else if (keys.has(f.key)) P("S003", `${at}: key "${f.key}" twice`);
      else keys.add(f.key);
    } else if (f.key !== undefined) P("S003", `${at}: a section has no key`);
    if (!isStr(f.label) || len(f.label.trim()) < 1 || len(f.label) > 40) P("S004", `${name}: label of 1-40 characters`);
    if (f.help !== undefined && (!isStr(f.help) || len(f.help) > 120)) P("S004", `${name}: help of at most 120 characters`);
    switch (f.type) {
      case "string":
        if (f.max !== undefined && (!Number.isInteger(f.max) || f.max < 1 || f.max > STRING_MAX)) P("S005", `${name}: max 1-${STRING_MAX}`);
        break;
      case "text":
        if (f.max !== undefined && (!Number.isInteger(f.max) || f.max < 1 || f.max > TEXT_MAX)) P("S005", `${name}: max 1-${TEXT_MAX}`);
        break;
      case "number":
        if ((f.min !== undefined && !isNum(f.min)) || (f.max !== undefined && !isNum(f.max)) || (isNum(f.min) && isNum(f.max) && f.min >= f.max))
          P("S005", `${name}: min and max are numbers, min < max`);
        if (f.step !== undefined && (!isNum(f.step) || f.step <= 0)) P("S005", `${name}: step > 0`);
        if (f.int !== undefined && typeof f.int !== "boolean") P("S005", `${name}: int is true or false`);
        break;
      case "enum": {
        const o = f.options;
        const vs = Array.isArray(o) ? o.map((x: any) => (isObj(x) ? x.v : undefined)) : [];
        if (!Array.isArray(o) || o.length < 2 || o.length > 20) P("S005", `${name}: options, a list of 2-20 {v, label}`);
        else if (o.some((x: any) => !isObj(x) || !isStr(x.v) || !OPTION_RE.test(x.v) || !isStr(x.label) || len(x.label.trim()) < 1 || len(x.label) > 40))
          P("S005", `${name}: each option {"v": 1-40 of A-Z a-z 0-9 _ . + -, "label": 1-40 characters}`);
        else if (new Set(vs).size !== vs.length) P("S005", `${name}: two options with the same v`);
        break;
      }
      case "list": {
        const of = f.of;
        if (!isObj(of) || !LIST_ITEM_TYPES.includes(of.type)) P("S006", `${name}: of, a {"type": "string"} or {"type": "enum", "options": [...]} item`);
        else if (of.type === "string" && of.max !== undefined && (!Number.isInteger(of.max) || of.max < 1 || of.max > STRING_MAX)) P("S006", `${name}: of.max 1-${STRING_MAX}`);
        else if (of.type === "enum") checkSettings([{ ...of, key: "x", label: "x" }], [], id, (c, m) => P(c === "S005" ? "S006" : c, `${name}: of: ${m.replace(/^settings\[0\] \(x\): /, "")}`));
        if (f.max_items !== undefined && (!Number.isInteger(f.max_items) || f.max_items < 1 || f.max_items > LIST_MAX_ITEMS)) P("S006", `${name}: max_items 1-${LIST_MAX_ITEMS}`);
        break;
      }
      case "secret": {
        if (!SECRET_WHERE.includes(f.where)) P("S007", `${name}: where "device" (kept on the device only) or "server" (kept by the server)`);
        if (f.default !== undefined) P("S007", `${name}: a secret has no default`);
        if (f.send_to !== undefined || f.as !== undefined) P("S007", `${name}: send_to and as moved to the manifest's "auth" bindings (docs/app-slots.md §15.1)`);
        break;
      }
    }
    if (f.default !== undefined && f.type !== "secret") {
      if (["city", "photo", "section"].includes(f.type)) P("S008", `${name}: a ${f.type} has no default`);
      else {
        const raw = f.type === "bool" ? (f.default === true ? "1" : f.default === false ? "0" : String(f.default))
          : f.type === "number" ? String(f.default)
          : f.type === "list" ? JSON.stringify(f.default)
          : f.default;
        const e = f.type === "bool" && typeof f.default !== "boolean" ? "true or false" : checkValue(f, raw);
        if (e) P("S008", `${name}: default ${JSON.stringify(f.default)}: ${e}`);
      }
    }
    if (f.i18n !== undefined) {
      if (!isObj(f.i18n)) P("S009", `${name}: i18n, {"<language>": {"label", "help", "options"}}`);
      else for (const [lang, t] of Object.entries(f.i18n)) {
        if (!LANGS.includes(lang)) P("S009", `${name}: i18n key ${lang}; the device's languages are ${LANGS.join(", ")}`);
        if (!isObj(t)) { P("S009", `${name}: i18n.${lang} is an object`); continue; }
        if (t.label !== undefined && (!isStr(t.label) || len(t.label) < 1 || len(t.label) > 40)) P("S009", `${name}: i18n.${lang}.label of 1-40 characters`);
        if (t.help !== undefined && (!isStr(t.help) || len(t.help) > 120)) P("S009", `${name}: i18n.${lang}.help of at most 120 characters`);
        if (t.options !== undefined && (!isObj(t.options) || Object.values(t.options).some((x) => !isStr(x) || len(x) < 1 || len(x) > 40)))
          P("S009", `${name}: i18n.${lang}.options, {"<v>": label of 1-40 characters}`);
        for (const k of Object.keys(t)) if (!["label", "help", "options"].includes(k)) P("S009", `${name}: i18n.${lang}.${k} is not read`);
      }
    }
    if (f.store_as !== undefined && !(isStr(id) && id.startsWith("dev.espos."))) P("S012", `${name}: store_as is for the OS's own packages only`);
  });
}

/** A manifest's "auth": named bindings of secret fields to requests (S010, S011, S013, S014; docs/app-slots.md §15.1). */
export function checkAuth(auth: unknown, settings: unknown, perms: unknown, P: Put): void {
  if (!isObj(auth) || Object.keys(auth).length > 8) {
    P("S013", "auth: an object of at most 8 named bindings");
    return;
  }
  const permList = Array.isArray(perms) ? perms.filter(isStr) : [];
  const fields = fieldsByKey(settings);
  const secret = (k: unknown) => isStr(k) && fields.get(k)?.type === "secret";
  for (const [name, b] of Object.entries(auth)) {
    const at = `auth.${name}`;
    if (!KEY_RE.test(name) || !isObj(b)) { P("S013", `${at}: a binding named 1-24 of a-z 0-9 _, an object`); continue; }
    const forms = ["basic", "bearer", "header", "device"].filter((k) => b[k] !== undefined);
    if (forms.length !== 1) P("S013", `${at}: exactly one of basic, bearer, header, device`);
    // {"device": true}: the device's own credential, only to the app's own {espos}/v1/device/app/<id>/* (net:espos)
    if (b.device !== undefined && (b.device !== true || !Array.isArray(b.send_to) ||
        b.send_to.some((x: unknown) => !isStr(x) || !/^\{espos\}\/v1\/device\/app\//.test(x))))
      P("S013", `${at}.device: true, with send_to only {espos}/v1/device/app/<this app's id>/...`);
    const used: unknown[] = [];
    if (b.basic !== undefined) {
      if (!Array.isArray(b.basic) || b.basic.length !== 2) P("S013", `${at}.basic: [user key, password key]`);
      else used.push(...b.basic);
    }
    if (b.bearer !== undefined) used.push(b.bearer);
    if (b.header !== undefined) {
      if (!isObj(b.header) || Object.keys(b.header).length < 1 || Object.keys(b.header).length > 2 || Object.keys(b.header).some((h) => !/^[A-Za-z0-9-]{1,40}$/.test(h) || /^(authorization|cookie|host)$/i.test(h)))
        P("S013", `${at}.header: {"<Header-Name>": key}, 1-2 headers, not Authorization, Cookie or Host`);
      else used.push(...Object.values(b.header));
    }
    if (b.query !== undefined) {
      if (!isObj(b.query) || Object.keys(b.query).length < 1 || Object.keys(b.query).length > 4 || Object.keys(b.query).some((q) => !/^[A-Za-z0-9_.-]{1,40}$/.test(q)))
        P("S013", `${at}.query: {"<param>": key}, 1-4 parameters`);
      else used.push(...Object.values(b.query));
    }
    for (const k of used) if (!secret(k)) P("S014", `${at}: ${JSON.stringify(k)} is not a secret field of the settings`);
    if (!Array.isArray(b.send_to) || b.send_to.length < 1 || b.send_to.length > 4 || b.send_to.some((x: unknown) => !isStr(x) || !SEND_TO_RE.test(x) || len(x) > URL_MAX)) {
      P("S013", `${at}.send_to: 1-4 patterns, https://host/path or {url_field}/path`);
      continue;
    }
    for (const x of b.send_to as string[]) {
      const ph = /^\{([a-z0-9_]+)\}/.exec(x);
      // {espos}: the device's own EspOS server (src/js/jsnet.cpp), with perm net:espos; any other placeholder is a field
      const own = !!ph && ph[1] === "espos";
      if (ph && !own) {
        const f = fields.get(ph[1]);
        if (!f || (f.type !== "url" && f.type !== "string")) P("S011", `${at}.send_to ${x}: {${ph[1]}} is not a url or string field`);
      }
      const host = hostOf(x);
      const need = own ? "net:espos" : ph ? `net:setting:${ph[1]}` : `net:${host}`;
      const wild = !ph && permList.some((q) => q.startsWith("net:*.") && host.endsWith("." + q.slice(6))); // net:*.example.com
      const covered = permList.includes("net:*") || permList.includes("net") || permList.includes(need) || wild;
      if (!covered) P("S010", `${at}.send_to ${x}: add "${need}" to perms, or the device will not send it`);
    }
  }
}
