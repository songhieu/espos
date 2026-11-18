// The one package validator. It holds every rule of the four places that check packages today, and where they
// differ it takes the strictest:
//   tools/espk.py (build/--check), scripts/publish_pkg.py (the store upload), the web admin (web/app/src/n2/
//   AdminPage.tsx + admin/espk.ts) and the device itself (src/pkg/espk.cpp safeName, limits; src/js/jsapp.cpp).
// Each rule has a code (M = manifest, F = files, C = code, P = .espk structure, in espk.ts) listed by
// `espos check --rules`.
// Declarative packages (type face, widget or theme, "runtime": "decl", docs/profiles.md §3-4) follow espk.py's
// check_decl_manifest and check_decl_file (M040-M047, F020-F030) instead of the app rules.
import esbuild from "esbuild";
import { jpegInfo } from "./image.js";
import { nameProblem, type Problem, type ReadPkg } from "./espk.js";
import { floatPaths, parse, PyObj, toPlain } from "./pyjson.js";

export const ENGINES = ["js", "pet", "run", "j2me", "lang", "font"]; // lang, font: the OS's language and font packs (docs/i18n.md)
export const KINDS = ["js", "native"];
export const CATEGORIES = ["game", "tool", "study", "health", "widget"];
export const PERMS = ["sound", "notify", "net", "bg"];
export const PERMS_GRANTED = ["sound", "notify"]; // what firmware 0.5.0 grants (src/js/jsapp.cpp)
export const GLYPHS = ["timer", "alarm", "bell", "list", "drop", "flag", "globe", "sun", "moon", "photo", "edit", "font", "home", "search", "speaker", "play", "lap", "pin", "leaf", "power", "grid"];
export const TYPES = ["app", "pack"];
export const KEYPADS = ["full", "compact", "touch"];
export const ID_RE = /^[a-z0-9][a-z0-9._-]{0,47}$/; // web admin (espk.py: <= 48, publish_pkg.py: <= 64)
export const PART_RE = /^[a-z0-9_-]{1,24}$/;
export const VERSION_RE = /^[0-9a-z.+-]{1,20}$/; // publish_pkg.py allows A-Z; the web admin lowercases versions in URLs
export const HEX6 = /^#[0-9a-fA-F]{6}$/;
export const MAX_CODE = 1_999_999_999;
export const APP_AREA = 7_528_448; // the internal app area (docs/ecosystem.md §3)
export const MAIN_JS_MAX = 512 * 1024; // jsapp::open reads main.js only below this
export const ICON_MAX = 200_000; // apps.cpp jpgIcon ignores bigger icons
export const FW_PACKAGES = 300; // 0.3.0: the first firmware with packages
export const FW_J2ME = 400; // 0.4.0: the first with the j2me engine
export const FW_LANG = 500; // 0.5.0: the first with languages (engines lang and font) and .espk files
/** The languages the S3 shows (src/i18n.cpp LANGS, tools/espk.py LANG_TAGS): the "i18n" keys the device reads. */
export const LANG_TAGS = ["en", "vi", "zh-Hans", "es", "fr", "de", "ja", "ko", "pt-BR", "ru", "id"];
/** Each language's font pack (src/i18n.cpp LANGS); the others draw with the built-in fonts. */
/** The language packs' ids keep the tag as it is (dev.espos.lang.pt-BR): the one exception to lowercase ids (M001). */
export const LANG_PACK_IDS = LANG_TAGS.slice(1).map((t) => `dev.espos.lang.${t}`);
export const LANG_FONTS: Record<string, string> = { vi: "dev.espos.font.vi", "zh-Hans": "dev.espos.font.sc", ja: "dev.espos.font.jp", ko: "dev.espos.font.kr", ru: "dev.espos.font.cyr" };
// The letters a language can show (tools/i18n.py BUILTIN, VI_LETTERS, VI_MARKED, CYR). Chinese, Japanese and Korean
// font packs hold a chosen set of characters, so they are not checked.
const BUILTIN = (c: number) => (c >= 0x20 && c < 0x7f) || (c >= 0xa0 && c < 0x100) || [0x2013, 0x2014, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2212].includes(c);
const VI_LETTERS = [0x102, 0x103, 0x110, 0x111, 0x128, 0x129, 0x168, 0x169, 0x1a0, 0x1a1, 0x1af, 0x1b0];
const LETTERS: Record<string, (c: number) => boolean> = {
  en: BUILTIN, es: BUILTIN, fr: BUILTIN, de: BUILTIN, "pt-BR": BUILTIN, id: BUILTIN,
  vi: (c) => BUILTIN(c) || VI_LETTERS.includes(c) || (c >= 0x1ea0 && c <= 0x1ef9),
  ru: (c) => BUILTIN(c) || c === 0x401 || c === 0x451 || (c >= 0x410 && c < 0x450),
};

// Declarative packages (docs/profiles.md; tools/espk.py DECL_*, CAPS): data only, drawn by the firmware.
export const DECL_TYPES = ["face", "widget", "theme"];
export const DECL_LEVEL = 1;
export const DECL_PERMS = ["data:weather", "data:calendar", "data:reading", "data:news", "data:markets"];
export const DECL_REFRESH = ["minute", "hour", "data", "day"];
export const DECL_ITEMS = ["box", "line", "circle", "text", "icon", "image", "bar", "list", "group"];
export const CAPS = ["screen.color", "screen.gray", "screen.eink", "screen.fast", "input.touch", "input.buttons", "mem.psram",
  "audio.out", "audio.in", "store.sd", "net.wifi", "net.ble", "power.battery", "led.rgb"];
export const DECL_FILE_MAX = 12 * 1024; // the X4 reads the layout file in one go
export const DECL_DEPTH = 3; // list/group inside list/group

/** Manifest fields the firmware, the store or the tools read. */
const KNOWN = new Set(["id", "name", "version", "code", "kind", "engine", "min_fw", "category", "author", "desc", "icon", "perms",
  "part", "type", "addon_of", "j2me", "parts", "group", "label", "default", "req", "license", "permissions", "glyph", "bg",
  "i18n", "lang", "font"]);
/** Manifest v2 (docs/platform.md): planned, not read by firmware 0.5.0. */
const V2 = new Set(["runtime", "api", "screens", "theme", "entry", "settings", "web", "match", "provides", "requires", "refresh"]);
/** The v2 fields a declarative package uses (espk.py entry() puts them into the catalog). */
const DECL_FIELDS = new Set(["runtime", "api", "requires", "screens", "refresh", "entry"]);
/** Manifest v1 app fields: espk.py ignores them in a face, widget or theme. */
const APP_ONLY = ["kind", "engine", "min_fw", "category", "j2me", "permissions"];

/** A core manifest of a face, widget or theme: checked by the decl rules (espk.py check_decl_manifest). */
export const isDecl = (m: unknown): boolean =>
  isObj(m) && (m.part ?? "core") === "core" && typeof m.type === "string" && DECL_TYPES.includes(m.type);

export interface RuleDoc {
  code: string;
  level: Problem["level"];
  text: string;
  from: string; // which of the old validators had it
}

export const RULES: RuleDoc[] = [
  { code: "M001", level: "error", text: "id: lowercase letters, digits, '.', '_', '-', 1-48 characters, starting with a letter or digit (the language packs dev.espos.lang.<tag> keep their tag's capitals)", from: "web admin (espk.py: <=48, no '/'; publish: <=64)" },
  { code: "M002", level: "warning", text: "id should be a reverse-domain name (vn.you.app)", from: "docs" },
  { code: "M003", level: "warning", text: "id longer than 23 characters and min_fw below 400: the dev console's APP command before firmware 0.4.0 cuts it", from: "src/devtools.cpp" },
  { code: "M010", level: "error", text: "name: text of 1-40 characters", from: "publish_pkg.py (espk.py: non-empty)" },
  { code: "M011", level: "error", text: "version: 1-20 characters of 0-9 a-z . + -", from: "publish_pkg.py + web admin cleanVersion" },
  { code: "M012", level: "error", text: "code: a whole number 1..1999999999 (not 1.0, not true)", from: "publish_pkg.py + espk.py" },
  { code: "M013", level: "error", text: 'kind: "js" or "native"', from: "all" },
  { code: "M014", level: "error", text: "engine: js, pet, run, j2me, lang or font; kind js goes with engine js; native with another", from: "espk.py + publish_pkg.py" },
  { code: "M015", level: "error", text: "min_fw: a whole number >= 0 (300 = 0.3.0), required", from: "publish_pkg.py (web admin assumes 300, publish 0)" },
  { code: "M016", level: "warning", text: "min_fw below what the package needs (300 for packages, 400 for j2me, 500 for lang and font)", from: "web admin (J2ME_MIN_FW) + tools/i18n.py" },
  { code: "M017", level: "error", text: "category: game, tool, study, health or widget (required)", from: "publish_pkg.py" },
  { code: "M018", level: "error", text: "author: text of at most 40 characters; desc: at most 300", from: "publish_pkg.py" },
  { code: "M019", level: "error", text: "perms: a list of sound, notify, net, bg", from: "espk.py + publish_pkg.py" },
  { code: "M020", level: "warning", text: "perms net and bg are reserved: no firmware grants them yet", from: "src/js/jsapp.cpp" },
  { code: "M021", level: "error", text: '"permissions" (the 0.2.3 field) without "perms", or with a different list: the device adds both up, the store reads only perms', from: "pkg.cpp vs publish_pkg.py" },
  { code: "M022", level: "error", text: 'icon: {"bg": ["#rrggbb", "#rrggbb"], "glyph": one of the glyphs}', from: "espk.py + publish_pkg.py" },
  { code: "M023", level: "warning", text: "no icon.jpg and no icon colours: the device draws a grey square", from: "publish_pkg.py" },
  { code: "M024", level: "error", text: 'type: "app" (default), "pack", or "face"/"widget"/"theme" (declarative, M040-M047); service/driver need manifest v2 (planned)', from: "espk.py" },
  { code: "M025", level: "error", text: "addon_of: the id of another package", from: "espk.py + publish_pkg.py" },
  { code: "M026", level: "error", text: 'no "…" anywhere in the manifest (the device shows text in full)', from: "espk.py" },
  { code: "M027", level: "error", text: 'part: "core" or a short lowercase name, required (publish_pkg.py finds the core by it)', from: "publish_pkg.py" },
  { code: "M028", level: "error", text: "core \"parts\": [{id, group, label, default, req}] naming parts of this package", from: "publish_pkg.py" },
  { code: "M029", level: "warning", text: "j2me: {midlet, keypad (full|compact|touch), profile, config} for engine j2me", from: "web admin + docs/j2me-games.md" },
  { code: "M030", level: "warning", text: "field not read by the firmware, the store or the tools", from: "-" },
  { code: "M031", level: "warning", text: "manifest v2 field (docs/platform.md): kept, but firmware 0.5.0 does not read it", from: "-" },
  { code: "M032", level: "error", text: "the manifest is not a JSON object / not JSON", from: "all" },
  { code: "M033", level: "error", text: "parts of one package share id, version and code; exactly one core; no part twice", from: "publish_pkg.py" },
  { code: "M034", level: "error", text: "group parts: a label of 1-40 characters, at most one default per group; core is never a choice", from: "publish_pkg.py" },
  { code: "M035", level: "error", text: 'i18n: {"<language tag>": {"name": text of 1-40 characters, "desc": text of at most 300}}', from: "espk.py check_i18n + src/pkg/pkg.cpp, store.cpp" },
  { code: "M036", level: "warning", text: `i18n: a key the S3 never shows (it reads exactly ${LANG_TAGS.join(", ")}), or a field other than name and desc`, from: "src/i18n.cpp LANGS + src/pkg/store.cpp" },
  { code: "M037", level: "warning", text: "i18n: letters the device cannot draw in that language (Latin-1 for en, es, fr, de, pt-BR, id; plus the Vietnamese letters for vi, Cyrillic for ru)", from: "tools/i18n.py BUILTIN + font packs" },
  { code: "M038", level: "warning", text: 'engine lang/font: the OS\'s own packs (tools/i18n.py): type "pack", "lang" a language tag; the device uses a language only as dev.espos.lang.<lang> and the font its language table names', from: "src/i18n.cpp" },
  { code: "M040", level: "error", text: 'face/widget/theme: "runtime": "decl"', from: "espk.py check_decl_manifest" },
  { code: "M041", level: "error", text: 'face/widget/theme: api {"min": 1..1, "target": n} (optional; min defaults to 1, the declarative level)', from: "espk.py check_decl_manifest" },
  { code: "M042", level: "error", text: "face/widget/theme: requires: a list of capability flags (screen.color, screen.gray, screen.eink, screen.fast, input.touch, input.buttons, mem.psram, audio.out, audio.in, store.sd, net.wifi, net.ble, power.battery, led.rgb)", from: "espk.py check_decl_manifest (CAPS)" },
  { code: "M043", level: "error", text: 'face/widget/theme: screens: a list of sizes like "800x480"', from: "espk.py check_decl_manifest" },
  { code: "M044", level: "error", text: "face/widget/theme: refresh: minute, hour, data (the default) or day", from: "espk.py check_decl_manifest" },
  { code: "M045", level: "error", text: "face/widget/theme: perms: a list of data:weather, data:calendar, data:reading, data:news, data:markets", from: "espk.py check_decl_manifest" },
  { code: "M046", level: "error", text: 'face/widget/theme: entry {"<type>": "<file>.json"} naming the layout (or theme) file', from: "espk.py check_decl_manifest" },
  { code: "M047", level: "warning", text: "face/widget/theme: kind, engine, min_fw, category, j2me, permissions are app fields; nothing reads them in a declarative package", from: "docs/profiles.md §3" },
  { code: "F001", level: "error", text: "file names: 1-51 bytes, no '..', '//', leading or trailing '/', \\ : * ? \" < > |, control characters, not manifest.json", from: "device safeName + espk.py + publish_pkg.py" },
  { code: "F002", level: "error", text: "kind js needs main.js in the core part", from: "all" },
  { code: "F003", level: "error", text: "engine j2me (not a pack) needs game.jar in the core part", from: "docs/j2me-games.md" },
  { code: "F004", level: "error", text: "icon.jpg: a square baseline JPEG (the device cannot draw progressive ones), at most 200,000 bytes", from: "espk.py + publish_pkg.py + apps.cpp" },
  { code: "F005", level: "warning", text: "icon.jpg is not 120x120", from: "espk.py + publish_pkg.py" },
  { code: "F006", level: "error", text: "main.js below 512 KB (the device does not open bigger ones)", from: "src/js/jsapp.cpp" },
  { code: "F007", level: "error", text: "a part with no files", from: "publish_pkg.py" },
  { code: "F008", level: "warning", text: "a folder without espos.json packs every file: build outputs (*.espk, dist/, node_modules/) end up in the package", from: "tools/espk.py" },
  { code: "F009", level: "error", text: "at most 512 files per part; at most 64 MB per file", from: "device (src/pkg/espk.cpp)" },
  { code: "F010", level: "warning", text: "the package is bigger than the internal app area (7.5 MB): it fits only on a memory card", from: "docs/ecosystem.md" },
  { code: "F011", level: "warning", text: "engine lang needs strings.bin; engine font needs r<px>.bin (or b<px>.bin) files", from: "src/i18n.cpp + src/fonts.cpp" },
  { code: "F020", level: "error", text: "face/widget: the layout file named by entry is in the package (themes: not checked)", from: "espk.py check_decl_file" },
  { code: "F021", level: "error", text: "face/widget: the layout file is at most 12288 bytes (the X4 reads it in one go)", from: "espk.py check_decl_file" },
  { code: "F022", level: "error", text: "face/widget: the layout file is a JSON object (UTF-8, no BOM)", from: "espk.py check_decl_file" },
  { code: "F023", level: "error", text: 'face/widget: the layout file has "decl": 1 (the format level)', from: "espk.py check_decl_file" },
  { code: "F024", level: "error", text: 'face/widget: "layouts": a non-empty list', from: "espk.py check_decl_file" },
  { code: "F025", level: "error", text: 'face/widget: each layout\'s "size": [w, h], whole numbers above 0', from: "espk.py check_decl_file" },
  { code: "F026", level: "error", text: '"items" are lists; each item\'s "t": box, line, circle, text, icon, image, bar, list or group', from: "espk.py check_decl_file" },
  { code: "F027", level: "error", text: 'an image\'s "src" (without a {binding}) is a file of the package', from: "espk.py check_decl_file" },
  { code: "F028", level: "error", text: "lists and groups go 3 deep at most", from: "espk.py check_decl_file + docs/profiles.md §4.3" },
  { code: "F029", level: "error", text: 'no "…" in an item (text always fits on the device)', from: "espk.py check_decl_file" },
  { code: "F030", level: "error", text: 'the manifest\'s "screens" (when given) are the layout sizes', from: "espk.py check_decl_file" },
  { code: "C001", level: "error", text: "main.js does not parse (QuickJS runs it as a classic script, ES2023)", from: "-" },
  { code: "C002", level: "error", text: "import/export: main.js is one classic script (bundle TypeScript or modules with espos build)", from: "src/js/jsapp.cpp JS_EVAL_TYPE_GLOBAL" },
  { code: "C003", level: "warning", text: "code calls notify / notify.schedule / notify.cancel / app.badge without the notify permission (it throws)", from: "src/js/jsapp.cpp" },
  { code: "C004", level: "warning", text: "code calls sound.* without the sound permission (it throws)", from: "src/js/jsapp.cpp" },
  { code: "C005", level: "info", text: "a permission is declared but the code never uses it", from: "-" },
  { code: "C006", level: "warning", text: "uses something API level 1 does not have (console, setTimeout, setInterval, fetch, require, XMLHttpRequest, localStorage)", from: "src/js/jsapp.cpp prelude" },
  { code: "C007", level: "warning", text: "async/await or .then(): API level 1 never runs Promise jobs, so the callbacks never run", from: "src/js/jsapp.cpp" },
  { code: "C008", level: "warning", text: 'a "…" in main.js (screens show text in full: shrink or wrap)', from: "docs" },
  { code: "C009", level: "warning", text: "main.js is not valid UTF-8", from: "-" },
];

const len = (s: string) => [...s].length;
const isStr = (v: unknown): v is string => typeof v === "string";
const isObj = (v: unknown): v is Record<string, any> => !!v && typeof v === "object" && !Array.isArray(v);

/** Checks one manifest. where: the file name for messages; floats: JSON paths written as floats. */
export function checkManifest(m: unknown, where: string, floats: Set<string> = new Set()): Problem[] {
  const out: Problem[] = [];
  const P = (code: string, msg: string, level?: Problem["level"]) =>
    out.push({ level: level ?? RULES.find((r) => r.code === code)?.level ?? "error", code, where, msg });
  if (!isObj(m)) {
    P("M032", "the manifest is not a JSON object");
    return out;
  }
  const isInt = (k: string, v: unknown) => typeof v === "number" && Number.isInteger(v) && !floats.has(k);
  const part = m.part;
  if (part === undefined) P("M027", 'part: missing; write "part": "core" (publish_pkg.py does not find the core part without it)');
  else if (!isStr(part) || !PART_RE.test(part)) P("M027", "part: a short lowercase name (core, roads10, ...)");
  const core = (part ?? "core") === "core";
  const decl = isDecl(m);
  // id, version, code: every part
  if (!isStr(m.id) || !(ID_RE.test(m.id) || LANG_PACK_IDS.includes(m.id))) P("M001", `id: ${JSON.stringify(m.id)} is not a package id like vn.you.app (a-z 0-9 . _ -, 1-48 characters, starting with a letter or digit)`);
  else {
    if (!m.id.includes(".")) P("M002", `id: ${m.id} is not a reverse-domain name like vn.you.app`);
    // faces and widgets are not opened with APP
    if (!decl && m.id.length > 23 && !(typeof m.min_fw === "number" && m.min_fw >= FW_J2ME))
      P("M003", `id: ${m.id.length} characters; before firmware 0.4.0 the dev console's APP command reads only 23 (src/devtools.cpp), so "APP ${m.id}" does not open it there`);
  }
  if (!isStr(m.version) || !VERSION_RE.test(m.version)) P("M011", `version: ${JSON.stringify(m.version)}; text like 1.0.0 (1-20 characters of 0-9 a-z . + -)`);
  if (!isInt("code", m.code) || m.code < 1 || m.code > MAX_CODE) P("M012", `code: ${JSON.stringify(m.code)}${floats.has("code") ? " (written as a float)" : ""}; a whole number from 1 to ${MAX_CODE}, higher for every release`);
  if (!core) {
    for (const k of Object.keys(m)) if (!["id", "version", "code", "part", "group", "label", "default", "req"].includes(k) && !KNOWN.has(k) && !V2.has(k)) P("M030", `${k}: not read in a non-core part`, "warning");
    if (JSON.stringify(m).includes("…")) P("M026", 'a "…" in the manifest; the device shows text in full (shrink or wrap)');
    return out;
  }
  // core: the full manifest
  if (!isStr(m.name) || len(m.name.trim()) < 1 || len(m.name) > 40) P("M010", `name: ${JSON.stringify(m.name)}; text of 1 to 40 characters`);
  if (decl) checkDeclManifest(m, P, isInt);
  else {
    if (!KINDS.includes(m.kind)) P("M013", `kind: ${JSON.stringify(m.kind)}; "js" or "native"`);
    if (m.engine === undefined) P("M014", `engine: missing; ${m.kind === "js" ? '"js"' : "the engine that runs the package (pet, run, j2me, lang, font)"}`);
    else if (!ENGINES.includes(m.engine)) P("M014", `engine: ${JSON.stringify(m.engine)}; one of ${ENGINES.join(", ")}`);
    else if (m.kind === "js" && m.engine !== "js") P("M014", 'kind "js" goes with engine "js"');
    else if (m.kind === "native" && m.engine === "js") P("M014", 'engine "js" goes with kind "js"');
    const need = m.engine === "j2me" ? FW_J2ME : m.engine === "lang" || m.engine === "font" ? FW_LANG : FW_PACKAGES;
    const needText = `${need} = 0.${need / 100}.0`;
    if (m.min_fw === undefined) P("M015", `min_fw: missing; the firmware code the package needs (${needText})`);
    else if (!isInt("min_fw", m.min_fw) || m.min_fw < 0) P("M015", `min_fw: ${JSON.stringify(m.min_fw)}; a whole number (300 = 0.3.0)`);
    else if (m.min_fw < need)
      P("M016", `min_fw ${m.min_fw}: ${m.engine === "j2me" ? "the j2me engine came with 0.4.0 (400)" : need === FW_LANG ? `the ${m.engine} engine came with 0.5.0 (500)` : "packages need 0.3.0 (300)"}`);
    if (m.engine === "lang" || m.engine === "font") checkLangPack(m, P);
    if (!CATEGORIES.includes(m.category)) P("M017", `category: ${JSON.stringify(m.category)}; one of ${CATEGORIES.join(", ")}`);
  }
  if (m.author !== undefined && (!isStr(m.author) || len(m.author) > 40)) P("M018", "author: text of at most 40 characters");
  if (m.desc !== undefined && (!isStr(m.desc) || len(m.desc) > 300)) P("M018", "desc: text of at most 300 characters");
  if (!decl) checkAppPerms(m, P);
  checkI18n(m, P);
  if (m.icon !== undefined) {
    const ic = m.icon;
    const bgOk = isObj(ic) && Array.isArray(ic.bg) && ic.bg.length === 2 && ic.bg.every((c: unknown) => isStr(c) && HEX6.test(c));
    if (!bgOk) P("M022", 'icon: {"bg": ["#rrggbb", "#rrggbb"], "glyph": "..."} (two colours of the gradient)');
    if (isObj(ic) && ic.glyph !== undefined && !GLYPHS.includes(ic.glyph)) P("M022", `icon.glyph: ${JSON.stringify(ic.glyph)}; one of ${GLYPHS.join(", ")}`);
    if (isObj(ic)) for (const k of Object.keys(ic)) if (k !== "bg" && k !== "glyph") P("M030", `icon.${k}: not read by the firmware`, "warning");
  }
  if (!decl && m.type !== undefined && !TYPES.includes(m.type))
    P("M024", ["service", "driver"].includes(m.type) ? `type "${m.type}": needs manifest v2 (planned, docs/platform.md); the types packages can have today are "app", "pack" and the declarative "face", "widget" and "theme" (runtime decl)` : `type: ${JSON.stringify(m.type)}; "app" (the default), "pack" (data for an engine, no icon), or a declarative "face", "widget" or "theme" (runtime decl, docs/profiles.md)`);
  if (m.addon_of !== undefined && (!isStr(m.addon_of) || !ID_RE.test(m.addon_of) || m.addon_of === m.id)) P("M025", "addon_of: the id of the package this one adds to (e.g. vn.pawboop.pet)");
  if (!decl && m.engine === "j2me" && m.type !== "pack") {
    const j = m.j2me;
    if (!isObj(j)) P("M029", 'j2me: {"midlet": "<class>", "keypad": "full", "profile": "MIDP-2.0", "config": "CLDC-1.1"} (what the store shows; the device reads MIDlet-1 from the jar)');
    else {
      if (!isStr(j.midlet) || !j.midlet) P("M029", "j2me.midlet: the MIDlet class (MIDlet-1 in the jar's MANIFEST.MF)");
      if (j.keypad !== undefined && !KEYPADS.includes(j.keypad)) P("M029", `j2me.keypad: one of ${KEYPADS.join(", ")}`);
    }
  }
  if (m.parts !== undefined && !Array.isArray(m.parts)) P("M028", 'parts: a list of {"id", "group", "label", "default", "req"}');
  if (m.license !== undefined && (!isStr(m.license) || !m.license.trim())) P("M030", "license: text (where the package comes from and under what licence)", "warning");
  for (const k of Object.keys(m)) {
    if (decl && DECL_FIELDS.has(k)) continue;
    if (decl && APP_ONLY.includes(k)) P("M047", `${k}: an app field (manifest v1); a ${m.type} does not use it`);
    else if (V2.has(k)) P("M031", `${k}: a manifest v2 field (docs/platform.md); firmware 0.5.0 does not read it`);
    else if (!KNOWN.has(k)) P("M030", `${k}: not read by the firmware, the store or the tools`);
  }
  if (JSON.stringify(m).includes("…")) P("M026", 'a "…" in the manifest; the device shows text in full (shrink or wrap)');
  return out;
}

type Put = (code: string, msg: string, level?: Problem["level"]) => void;

/** "i18n": the name and description in other languages (docs/i18n.md). The device reads i18n[<the language it
 * shows>].name (src/pkg/pkg.cpp) and the store .name and .desc (src/pkg/store.cpp); tools/espk.py check_i18n has
 * the same errors. */
function checkI18n(m: Record<string, any>, P: Put): void {
  const t = m.i18n;
  if (t === undefined) return;
  if (!isObj(t)) {
    P("M035", 'i18n: {"<language tag>": {"name": ..., "desc": ...}} (the name and description in other languages)');
    return;
  }
  for (const [tag, v] of Object.entries(t)) {
    if (!isObj(v)) {
      P("M035", `i18n.${tag}: {"name": ..., "desc": ...}`);
      continue;
    }
    if (v.name !== undefined && (!isStr(v.name) || !v.name.trim() || len(v.name) > 40)) P("M035", `i18n.${tag}.name: text of 1 to 40 characters`);
    if (v.desc !== undefined && (!isStr(v.desc) || len(v.desc) > 300)) P("M035", `i18n.${tag}.desc: text of at most 300 characters`);
    if (!LANG_TAGS.includes(tag)) {
      const near = LANG_TAGS.find((l) => l.toLowerCase() === tag.toLowerCase() || l.split("-")[0] === tag.split(/[-_]/)[0].toLowerCase());
      P("M036", `i18n.${tag}: the S3 never shows this key (it reads exactly ${LANG_TAGS.join(", ")})${near ? `; did you mean "${near}"?` : ""}`);
    }
    for (const k of Object.keys(v)) if (k !== "name" && k !== "desc") P("M036", `i18n.${tag}.${k}: not read (only name and desc are)`);
    const can = LETTERS[tag];
    if (can)
      for (const f of ["name", "desc"]) {
        const s = v[f];
        if (!isStr(s)) continue;
        const bad = [...new Set([...s].filter((ch) => !can(ch.codePointAt(0)!)))];
        if (bad.length) P("M037", `i18n.${tag}.${f}: ${bad.slice(0, 8).map((c) => JSON.stringify(c)).join(" ")} cannot be drawn in ${tag} (the device leaves them out)`);
      }
  }
}

/** A language or font pack: the OS's own (tools/i18n.py), found by fixed ids (src/i18n.cpp). */
function checkLangPack(m: Record<string, any>, P: Put): void {
  if (m.type !== "pack") P("M038", `type: "pack" for engine ${m.engine} (never listed in the store, never opened)`);
  if (!LANG_TAGS.includes(m.lang) || m.lang === "en") P("M038", `lang: ${JSON.stringify(m.lang)}; the language tag the pack is for (${LANG_TAGS.slice(1).join(", ")})`);
  else if (m.engine === "lang" && m.id !== `dev.espos.lang.${m.lang}`) P("M038", `id: the device looks for the language ${m.lang} as dev.espos.lang.${m.lang}, never as ${m.id}`);
  else if (m.engine === "font" && m.id !== LANG_FONTS[m.lang]) P("M038", `id: the device loads ${LANG_FONTS[m.lang] ?? `no font pack for ${m.lang}`}, never ${m.id}`);
  if (m.engine === "lang" && m.font !== undefined && m.font !== LANG_FONTS[m.lang]) P("M038", `font: ${JSON.stringify(m.font)}; the device loads ${LANG_FONTS[m.lang] ?? "no font pack"} for ${m.lang}`);
}

/** perms and the 0.2.3 "permissions" of an app or pack. */
function checkAppPerms(m: Record<string, any>, P: Put): void {
  if (m.permissions !== undefined) {
    const same = (a: unknown, b: unknown) => Array.isArray(a) && Array.isArray(b) && [...new Set(a)].sort().join() === [...new Set(b)].sort().join();
    if (m.perms === undefined) P("M021", 'use "perms" instead of "permissions" (the 0.2.3 field: the device still reads it, the store and the tools do not)');
    else if (!same(m.perms, m.permissions)) P("M021", '"permissions" and "perms" differ: the device grants both lists, the store shows only perms; make them the same');
    else P("M021", '"permissions" kept next to "perms" for firmware 0.2.3 (apps/pack.py\'s .espk)', "info");
  }
  if (m.perms !== undefined) {
    if (!Array.isArray(m.perms)) P("M019", `perms: a list of ${PERMS.join(", ")}`);
    else {
      const seen = new Set();
      for (const p of m.perms) {
        if (!PERMS.includes(p)) P("M019", `perms: unknown ${JSON.stringify(p)} (known: ${PERMS.join(", ")})`);
        else if (!PERMS_GRANTED.includes(p)) P("M020", `perms: "${p}" is reserved; no firmware grants it yet`);
        if (seen.has(p)) P("M019", `perms: ${JSON.stringify(p)} twice`, "warning");
        seen.add(p);
      }
    }
  }
}

/** The manifest of a face, widget or theme (docs/profiles.md §3): espk.py check_decl_manifest, rule for rule. */
function checkDeclManifest(m: Record<string, any>, P: Put, isInt: (k: string, v: unknown) => boolean): void {
  const t = m.type as string;
  if (m.runtime !== "decl") P("M040", `runtime: ${m.runtime === undefined ? "missing" : JSON.stringify(m.runtime)}; "decl" for a face, widget or theme`);
  const api = m.api === undefined ? {} : m.api;
  if (!isObj(api) || (api.min !== undefined && (!isInt("api.min", api.min) || api.min < 1 || api.min > DECL_LEVEL)))
    P("M041", `api: ${JSON.stringify(m.api)}; {"min": 1..${DECL_LEVEL}, "target": n} (min: the lowest declarative level the package needs)`);
  const req = m.requires === undefined ? [] : m.requires;
  if (!Array.isArray(req)) P("M042", `requires: a list of ${CAPS.join(", ")}`);
  else for (const r of req) if (!CAPS.includes(r)) P("M042", `requires: unknown ${JSON.stringify(r)} (known: ${CAPS.join(", ")})`);
  const screens = m.screens === undefined ? [] : m.screens;
  if (!Array.isArray(screens)) P("M043", 'screens: a list of sizes like "800x480"');
  else for (const sc of screens) if (!isStr(sc) || !/^[0-9]+x[0-9]+$/.test(sc)) P("M043", `screens: sizes like 800x480, not ${JSON.stringify(sc)}`);
  if (!DECL_REFRESH.includes(m.refresh === undefined ? "data" : m.refresh)) P("M044", `refresh: ${JSON.stringify(m.refresh)}; one of ${DECL_REFRESH.join(", ")}`);
  const perms = m.perms === undefined ? [] : m.perms;
  if (!Array.isArray(perms)) P("M045", `perms: a list of ${DECL_PERMS.join(", ")}`);
  else for (const p of perms) if (!DECL_PERMS.includes(p)) P("M045", `perms: unknown ${JSON.stringify(p)} (known: ${DECL_PERMS.join(", ")}; time, date, lunar date and battery need none)`);
  const entry = m.entry === undefined ? {} : m.entry;
  if (!isObj(entry) || !isStr(entry[t])) P("M046", `entry: ${m.entry === undefined ? "missing" : JSON.stringify(m.entry)}; {"${t}": "${t}.json"} (the ${t}'s file in the package)`);
}

export interface CheckOpts {
  /** the icon may come from next to the files (publish_pkg.py: <id>-<version>.jpg) */
  sideIcon?: boolean;
}

/** Checks the parts of one package (structure problems from readEspk included). */
export function checkPackage(parts: ReadPkg[]): Problem[] {
  const out: Problem[] = [];
  for (const p of parts) out.push(...p.problems);
  if (parts.some((p) => p.problems.some((x) => x.level === "error" && ["P001", "P002", "P004", "P005", "P006", "P007", "P008", "P009", "P011"].includes(x.code))))
    return out;
  for (const p of parts) out.push(...checkPart(p));
  // across parts (publish_pkg.py)
  const cores = parts.filter((p) => (p.plain.part ?? "core") === "core");
  const id = parts[0]?.plain.id;
  const E = (code: string, where: string, msg: string) => out.push({ level: "error", code, where, msg });
  if (parts.length > 1 || cores.length !== 1) {
    if (cores.length !== 1) E("M033", String(id), `needs exactly one core part (found ${cores.length})`);
    const names = new Set<string>();
    for (const p of parts) {
      if (p.plain.id !== id) E("M033", p.name, `id ${JSON.stringify(p.plain.id)} differs from ${JSON.stringify(id)}`);
      const c = cores[0]?.plain;
      if (c && (p.plain.version !== c.version || p.plain.code !== c.code)) E("M033", p.name, `version/code ${p.plain.version}/${p.plain.code} differ from the core's ${c.version}/${c.code}`);
      const pn = String(p.plain.part ?? "core");
      if (names.has(pn)) E("M033", p.name, `part ${pn} given twice`);
      names.add(pn);
    }
    const files = new Map<string, string>();
    for (const p of parts)
      for (const f of p.files) {
        if (files.has(f.name)) E("M033", p.name, `${f.name} is also in ${files.get(f.name)} (two parts must not hold the same file)`);
        files.set(f.name, p.name);
      }
  }
  // part choices (group/label/default)
  const core = cores[0]?.plain;
  const names = new Set(parts.map((p) => String(p.plain.part ?? "core")));
  if (core && Array.isArray(core.parts))
    for (const e of core.parts) if (!isObj(e) || !names.has(e.id)) E("M028", "manifest.json", `parts names a part that is not here: ${JSON.stringify(isObj(e) ? e.id : e)}`);
  const meta = new Map<string, Record<string, any>>();
  for (const p of parts) meta.set(String(p.plain.part ?? "core"), { ...pick(p.plain) });
  if (core && Array.isArray(core.parts)) for (const e of core.parts) if (isObj(e) && meta.has(e.id)) Object.assign(meta.get(e.id)!, pick(e));
  const defaults = new Map<string, number>();
  for (const [n, e] of meta) {
    if (n === "core" && e.group) E("M034", String(id), "the core part cannot be a choice (group)");
    if (e.group !== undefined && (!isStr(e.group) || !PART_RE.test(e.group))) E("M034", String(id), `part ${n}: group must be a short lowercase name`);
    if (e.label !== undefined && (!isStr(e.label) || len(e.label) < 1 || len(e.label) > 40)) E("M034", String(id), `part ${n}: label: text of 1 to 40 characters`);
    if (e.group && e.default === true) defaults.set(e.group, (defaults.get(e.group) ?? 0) + 1);
    if (e.group && !e.label) out.push({ level: "warning", code: "M034", where: String(id), msg: `part ${n} of group ${e.group} has no label; the store shows "${n}"` });
  }
  for (const [g, n] of defaults) if (n > 1) E("M034", String(id), `group ${g} has ${n} defaults`);
  // what one install takes: every required part and the biggest choice of each group
  const groupMax = new Map<string, number>();
  let total = 0;
  for (const p of parts) {
    const g = meta.get(String(p.plain.part ?? "core"))?.group;
    if (g) groupMax.set(g, Math.max(groupMax.get(g) ?? 0, p.bytes.length));
    else total += p.bytes.length;
  }
  for (const v of groupMax.values()) total += v;
  if (total > APP_AREA) out.push({ level: "warning", code: "F010", where: String(id), msg: `${(total / 1048576).toFixed(1)} MB installed: bigger than the internal app area (7.2 MB), it fits only on a memory card` });
  return out;
}

const pick = (o: Record<string, any>) => {
  const r: Record<string, any> = {};
  for (const k of ["group", "label", "default", "req"]) if (k in o) r[k] = o[k];
  return r;
};

/** Manifest + files of one part. */
export function checkPart(p: ReadPkg): Problem[] {
  const out = checkManifest(p.plain, p.name, p.floats);
  const m = p.plain;
  const W = (level: Problem["level"], code: string, msg: string) => out.push({ level, code, where: p.name, msg });
  const core = (m.part ?? "core") === "core";
  const decl = isDecl(m);
  const names = new Map(p.files.map((f) => [f.name, f]));
  for (const f of p.files) {
    const bad = nameProblem(f.name);
    if (bad && !p.problems.some((x) => x.code === "P015")) W("error", "F001", `${JSON.stringify(f.name)}: ${bad}`);
    if (f.size > 64 << 20) W("error", "F009", `${f.name}: ${f.size} bytes (at most 64 MB per file)`);
  }
  if (p.files.length > 512) W("error", "F009", `${p.files.length} files (at most 512 per part)`);
  if (!p.files.length) W("error", "F007", "no files");
  if (core && m.kind === "js" && !names.has("main.js")) W("error", "F002", 'kind "js" needs main.js');
  if (core && !decl && m.engine === "j2me" && m.type !== "pack" && !names.has("game.jar")) W("error", "F003", 'engine "j2me" needs game.jar (the game)');
  if (core && !decl && m.engine === "lang" && !names.has("strings.bin")) W("warning", "F011", 'engine "lang" needs strings.bin (tools/i18n.py build)');
  if (core && !decl && m.engine === "font" && ![...names.keys()].some((n) => /^[rb]\d+\.bin$/.test(n))) W("warning", "F011", 'engine "font" needs r<px>.bin or b<px>.bin files (tools/i18n.py font)');
  if (decl && m.type !== "theme") out.push(...checkDeclFile(m, names, p.name));
  const icon = names.get("icon.jpg");
  if (core && icon) {
    try {
      const j = jpegInfo(icon.data);
      if (!j.baseline) W("error", "F004", `icon.jpg: ${j.sof === 0xc2 ? "progressive" : "not baseline"} JPEG; the device draws baseline JPEGs only`);
      else if (j.width !== j.height) W("error", "F004", `icon.jpg: must be square (120x120), is ${j.width}x${j.height}`);
      else if (j.width !== 120) W("warning", "F005", `icon.jpg is ${j.width}x${j.height}; the device expects 120x120`);
      if (icon.size > ICON_MAX) W("error", "F004", `icon.jpg: ${icon.size} bytes; the device ignores icons over ${ICON_MAX} bytes`);
    } catch (e) {
      W("error", "F004", `icon.jpg: ${(e as Error).message}`);
    }
  } else if (core && !decl && m.icon === undefined && m.type !== "pack") W("warning", "M023", "no icon.jpg and no icon colours: the device draws a grey square");
  if (icon && !core) W("warning", "M030", "icon.jpg in a non-core part is not used");
  const main = names.get("main.js");
  if (core && !decl && main) {
    if (main.size >= MAIN_JS_MAX) W("error", "F006", `main.js is ${main.size} bytes; the device opens main.js only below ${MAIN_JS_MAX} bytes`);
    let text = "";
    try {
      text = new TextDecoder("utf-8", { fatal: true }).decode(main.data);
    } catch {
      W("warning", "C009", "main.js is not valid UTF-8");
      text = main.data.toString("utf8");
    }
    // the device grants perms and the old permissions together (src/pkg/pkg.cpp)
    const granted = [...(Array.isArray(m.perms) ? m.perms : []), ...(Array.isArray(m.permissions) ? m.permissions : [])];
    out.push(...checkCode(text, granted, p.name));
  }
  return out;
}

// ---------------------------------------------------------------- the layout file of a face or widget

/** The entry file of a face or widget (level 1, docs/profiles.md §4): espk.py check_decl_file, rule for rule. */
export function checkDeclFile(m: Record<string, any>, names: Map<string, { size: number; data: Buffer }>, where: string): Problem[] {
  const out: Problem[] = [];
  const entry = isObj(m.entry) && isStr(m.entry[m.type]) ? (m.entry[m.type] as string) : "";
  const f = entry ? names.get(entry) : undefined;
  if (!f) return [{ level: "error", code: "F020", where, msg: `${entry || "entry"}: the layout file is missing (entry.${m.type} names a file of the package)` }];
  const E = (code: string, msg: string) => out.push({ level: "error", code, where: `${where}: ${entry}`, msg });
  if (f.size > DECL_FILE_MAX) {
    E("F021", `${f.size} bytes, at most ${DECL_FILE_MAX} (the X4 reads it in one go)`);
    return out;
  }
  let text: string;
  try {
    // the BOM is kept so parse() refuses it: espk.py's json.loads(bytes) skips one, the X4's ArduinoJson does not
    text = new TextDecoder("utf-8", { fatal: true, ignoreBOM: true }).decode(f.data);
  } catch {
    E("F022", "not JSON (not UTF-8)");
    return out;
  }
  let v;
  try {
    v = parse(text);
  } catch (e) {
    E("F022", `not JSON (${(e as Error).message})`);
    return out;
  }
  if (!(v instanceof PyObj)) {
    E("F022", 'not a JSON object ({"decl": 1, "layouts": [...]})');
    return out;
  }
  const doc = toPlain(v) as Record<string, any>;
  const floats = floatPaths(v);
  const int = (path: string, v: unknown) => typeof v === "number" && Number.isInteger(v) && !floats.has(path);
  if (doc.decl !== DECL_LEVEL || !int("decl", doc.decl)) E("F023", `"decl": ${DECL_LEVEL} is missing${doc.decl === undefined ? "" : ` (found ${JSON.stringify(doc.decl)}${floats.has("decl") ? ", a float" : ""})`}`);
  const layouts = doc.layouts;
  if (!Array.isArray(layouts) || !layouts.length) {
    E("F024", 'needs a list of layouts ("layouts": [{"size": [800, 480], "items": [...]}])');
    return out;
  }
  const walk = (items: unknown, at: string, depth: number) => {
    if (!Array.isArray(items)) {
      E("F026", `${at}: items must be a list`);
      return;
    }
    items.forEach((it, i) => {
      const t = isObj(it) ? it.t : undefined;
      if (!DECL_ITEMS.includes(t)) {
        E("F026", `${at}[${i}]: t ${JSON.stringify(t)}; one of ${DECL_ITEMS.join(", ")}`);
        return;
      }
      // a src with a {binding} is resolved on the device; a plain one must be a file of the package
      if (t === "image" && it.src !== undefined && !(isStr(it.src) && (it.src.includes("{") || names.has(it.src))))
        E("F027", `${at}[${i}]: picture ${JSON.stringify(it.src)} is not in the package`);
      if (JSON.stringify(it).includes("…")) E("F029", `${at}[${i}]: no "…" in texts (the device fits text in full)`);
      if (t === "list" || t === "group") {
        if (depth >= DECL_DEPTH) E("F028", `${at}[${i}]: lists and groups go ${DECL_DEPTH} deep at most`);
        else walk(it.items === undefined ? [] : it.items, `${at}[${i}].items`, depth + 1);
      }
    });
  };
  const sizes: string[] = [];
  layouts.forEach((lay, k) => {
    const size = isObj(lay) ? lay.size : undefined;
    if (!(Array.isArray(size) && size.length === 2 && size.every((v, i) => int(`layouts[${k}].size[${i}]`, v) && v > 0))) {
      E("F025", `layouts[${k}].size must be [w, h] (whole numbers above 0), not ${JSON.stringify(size)}`);
      return;
    }
    sizes.push(`${size[0]}x${size[1]}`);
    walk(lay.items === undefined ? [] : lay.items, `layouts[${k}].items`, 0);
  });
  // espk.py: sorted(m["screens"]) != sorted(sizes), a list of the same sizes (each as often)
  const sc = m.screens;
  if (Array.isArray(sc) && sc.length) {
    const a = sc.map((x) => JSON.stringify(x)).sort(), b = sizes.map((x) => JSON.stringify(x)).sort();
    if (a.length !== b.length || a.some((x, i) => x !== b[i])) E("F030", `manifest screens ${JSON.stringify(sc)} do not match the layouts ${JSON.stringify(sizes)}`);
  }
  return out;
}

// ---------------------------------------------------------------- the code (main.js)

/** The code with comments removed and string/template/regex contents blanked (for the API scan). */
export function stripCode(src: string): string {
  let out = "";
  let i = 0;
  let prevSignificant = "";
  const regexAllowed = () => !prevSignificant || /[(,=:[!&|?{};+\-*%<>~^]$/.test(prevSignificant) || /\b(return|typeof|case|do|else|in|of|new|delete|void|throw)$/.test(prevSignificant);
  while (i < src.length) {
    const c = src[i], n = src[i + 1];
    if (c === "/" && n === "/") {
      while (i < src.length && src[i] !== "\n") i++;
      continue;
    }
    if (c === "/" && n === "*") {
      const e = src.indexOf("*/", i + 2);
      i = e < 0 ? src.length : e + 2;
      out += " ";
      continue;
    }
    if (c === '"' || c === "'" || c === "`") {
      const q = c;
      out += q;
      i++;
      let depth = 0;
      while (i < src.length) {
        const ch = src[i];
        if (ch === "\\") {
          i += 2;
          continue;
        }
        if (q === "`" && ch === "$" && src[i + 1] === "{") {
          // a template's ${...}: keep the code inside
          depth = 1;
          i += 2;
          out += "${";
          let inner = "";
          while (i < src.length && depth) {
            if (src[i] === "{") depth++;
            else if (src[i] === "}") depth--;
            if (depth) inner += src[i];
            i++;
          }
          out += stripCode(inner) + "}";
          continue;
        }
        if (ch === q) break;
        i++;
      }
      out += q;
      i++;
      prevSignificant = "a";
      continue;
    }
    if (c === "/" && regexAllowed()) {
      // a regular expression literal
      i++;
      let cls = false;
      while (i < src.length && src[i] !== "\n") {
        if (src[i] === "\\") {
          i += 2;
          continue;
        }
        if (src[i] === "[") cls = true;
        else if (src[i] === "]") cls = false;
        else if (src[i] === "/" && !cls) break;
        i++;
      }
      i++;
      out += "/r/";
      prevSignificant = "a";
      continue;
    }
    out += c;
    if (!/\s/.test(c)) prevSignificant = (prevSignificant + c).slice(-8);
    i++;
  }
  return out;
}

/** Permissions vs the APIs the code calls, things API level 1 lacks, syntax. */
export function checkCode(src: string, perms: string[], where = "main.js"): Problem[] {
  const out: Problem[] = [];
  const W = (level: Problem["level"], code: string, msg: string) => out.push({ level, code, where: where === "main.js" ? "main.js" : `${where}: main.js`, msg });
  try {
    esbuild.transformSync(src, { loader: "js", format: undefined, target: "es2023", logLevel: "silent" });
  } catch (e) {
    const err = (e as { errors?: { text: string; location?: { line: number; column: number } }[] }).errors?.[0];
    W("error", "C001", err ? `${err.text}${err.location ? ` (line ${err.location.line}, column ${err.location.column + 1})` : ""}` : String(e));
    return out;
  }
  const code = stripCode(src);
  if (/(^|[;\n}])\s*(import\s*[\w{*"'`]|export\s+(default|const|let|var|function|class|\{|\*))/.test(code))
    W("error", "C002", "import/export: main.js runs as one classic script; bundle modules with `espos build` (espos.json \"main\" with \"bundle\": true) or write one file");
  const usesNotify = /(^|[^.\w$])notify\s*(\(|\.\s*(schedule|cancel)\s*\()/.test(code) || /(^|[^.\w$])app\s*\.\s*badge\s*\(/.test(code);
  const usesSound = /(^|[^.\w$])sound\s*\.\s*(chime|beep|click)\b/.test(code);
  if (usesNotify && !perms.includes("notify")) W("warning", "C003", 'calls notify/app.badge without "notify" in perms: the call throws "app has no \\"notify\\" permission"');
  if (usesSound && !perms.includes("sound")) W("warning", "C004", 'calls sound.* without "sound" in perms: the call throws "app has no \\"sound\\" permission"');
  if (perms.includes("notify") && !usesNotify) W("info", "C005", 'perms has "notify" but the code never calls notify or app.badge');
  if (perms.includes("sound") && !usesSound) W("info", "C005", 'perms has "sound" but the code never calls sound.*');
  const missing: string[] = [];
  for (const [name, re] of [
    ["console", /(^|[^.\w$])console\s*\./],
    ["setTimeout", /(^|[^.\w$])setTimeout\s*\(/],
    ["setInterval", /(^|[^.\w$])setInterval\s*\(/],
    ["fetch", /(^|[^.\w$])fetch\s*\(/],
    ["require", /(^|[^.\w$])require\s*\(/],
    ["XMLHttpRequest", /(^|[^.\w$])XMLHttpRequest\b/],
    ["localStorage", /(^|[^.\w$])localStorage\b/],
  ] as const)
    if (re.test(code)) missing.push(name);
  if (missing.length) W("warning", "C006", `${missing.join(", ")}: not in API level 1 (use timer.after/every, store, toast; there is no console or network yet)`);
  if (/(^|[^.\w$])(async\s+(function|\(|[\w$]+\s*=>)|await\s)|\.then\s*\(/.test(code)) W("warning", "C007", "async/await or .then(): API level 1 never runs Promise jobs, so these callbacks never run");
  if (src.includes("…")) W("warning", "C008", 'a "…" in main.js: screens show text in full (shrink or wrap instead)');
  return out;
}

export const countLevels = (ps: Problem[]) => ({
  errors: ps.filter((p) => p.level === "error").length,
  warnings: ps.filter((p) => p.level === "warning").length,
  infos: ps.filter((p) => p.level === "info").length,
});
