// EspOS JavaScript API, level 1, as firmware 0.5.0 gives it (the first firmware that installs .espk packages).
//
// Exactly what the runtime gives an app today: the prelude and the native functions in s3/src/js/jsapp.cpp,
// documented in s3/docs/apps.md. An app is one classic script (QuickJS-ng, ES2023) that sets app.render; the
// screen is a tree of plain objects built again after every tap, timer or app.refresh().
//
// Not in level 1 (do not use): console, setTimeout/setInterval (use timer), fetch/network, modules at runtime
// (espos build bundles them), Promise jobs (async/await and .then() callbacks never run), app lifecycle hooks.
// Planned modules (docs/platform.md: app lifecycle, storage quotas, net, data, theme, sensors...) are not declared
// here because they do not exist yet.
//
// Limits: 1.5 MB heap (PSRAM); 1.5 s per call and 3 s to start, then the call is stopped (InternalError:
// interrupted); QuickJS stack about 11 KB (a deeper script throws "Maximum call stack size exceeded"). A throw never
// stops the OS: from a tap or a timer the device shows the toast "App error: <message>", from app.render the screen
// says "App error while drawing: <message>", and while main.js starts it says "The app cannot run: <message>"
// (these texts are translated when the device shows another language). Apps run only while open.

/** A screen node made by the ui.* functions (a plain object; build it only with ui.*). */
interface EspOSNode {
  readonly t: "col" | "row" | "card" | "title" | "text" | "big" | "button" | "chips" | "list" | "progress" | "spacer";
}

/** A colour: "#rrggbb" only (anything else falls back to the system colour). */
type EspOSColor = `#${string}`;

interface EspOSBoxOpts {
  /** space between children in px (default 8) */
  gap?: number;
  /** centre the children */
  center?: boolean;
}

interface EspOSCardOpts extends EspOSBoxOpts {
  /** the card's background (default: the system card colour) */
  bg?: EspOSColor;
  /** Half the line in landscape (two half cards side by side in a ui.row), the whole line in portrait. */
  half?: boolean;
}

interface EspOSTextOpts {
  /** 12 to 24 px (ui.text default 15, ui.title default 20); clamped */
  size?: number;
  /** bold (ui.title is always bold) */
  bold?: boolean;
  /** the dim system colour (ui.text only) */
  dim?: boolean;
  color?: EspOSColor;
  center?: boolean;
}

interface EspOSBigOpts {
  color?: EspOSColor;
  /** 64 (default), 56 when 50-59, 44 below 50: the digit font comes in those three sizes */
  size?: number;
}

interface EspOSButtonOpts {
  /** background (default #0a84ff, the system blue) */
  bg?: EspOSColor;
  /** text colour (default white) */
  color?: EspOSColor;
  /** full width */
  wide?: boolean;
}

interface EspOSListItem {
  title: string;
  /** a second, smaller line */
  sub?: string;
  /** text on the right */
  right?: string;
}

interface EspOSUi {
  /** Children one under the other. At most 60 children are drawn. */
  column(children?: EspOSNode[], opts?: EspOSBoxOpts): EspOSNode;
  /** Children side by side, wrapping to the next line when full. At most 60 children are drawn. */
  row(children?: EspOSNode[], opts?: EspOSBoxOpts): EspOSNode;
  /** A rounded card around its children (a column). {half: true}: half the line in landscape (see EspOSCardOpts). */
  card(children?: EspOSNode[], opts?: EspOSCardOpts): EspOSNode;
  /** Bold heading (20 px by default). Wraps to as many lines as it needs (3 px line spacing). */
  title(text: string | number, opts?: EspOSTextOpts): EspOSNode;
  /** Text (15 px by default). Wraps to as many lines as it needs (3 px line spacing). */
  text(text: string | number, opts?: EspOSTextOpts): EspOSNode;
  /** Big digits, centred (64 px by default): clocks, counters. */
  big(text: string | number, opts?: EspOSBigOpts): EspOSNode;
  /** A button, 44 px high. onTap gets no arguments; the screen is drawn again after it. */
  button(text: string | number, onTap: () => void, opts?: EspOSButtonOpts): EspOSNode;
  /**
   * A row of choices (at most 8). onPick gets the index; draw `selected` from your own state. Each bar on a screen
   * calls its own onPick.
   */
  chips(names: string[], selected: number, onPick: (index: number) => void): EspOSNode;
  /** List rows (at most 40). Without onTap the rows are not tappable. */
  list(items: EspOSListItem[], onTap?: (index: number) => void): EspOSNode;
  /** A bar from 0 to 1. */
  progress(value: number, opts?: { /** default #34c759 (green) */ color?: EspOSColor }): EspOSNode;
  /** Empty space, 0 to 200 px high (default 8). */
  spacer(height?: number): EspOSNode;
}

interface EspOSApp {
  /** The title in the navigation bar (default: the package's name). */
  title: string;
  /** Builds the screen. Called when the app opens and after every tap, timer and refresh(). */
  render: () => EspOSNode;
  /** Draws the screen again soon (once, however often it is called). */
  refresh(): void;
  /**
   * The number on the app's icon; 0 hides it; no argument goes back to counting unread notifications.
   * Needs the "notify" permission (throws TypeError without it).
   */
  badge(n?: number): void;
  /**
   * What the app was opened with: the `arg` of the notification that was tapped (a string; an object given as arg
   * arrives as JSON text). undefined when opened from Home.
   */
  readonly arg: string | undefined;
  /**
   * The room the app draws in: the screen in px (320 x 480 in portrait, 480 x 320 in landscape, on both boards) and
   * whether it is landscape. Set before the script runs; when the screen turns, the OS opens the app again, so it is
   * never stale while the app runs.
   */
  readonly screen: EspOSScreen;
  /**
   * The language the device shows, as a tag ("en" unless the user picked another language and its pack is in).
   * Set before the script runs; it does not change while the app is open (the OS switches language only when no app
   * is open). Pick the app's own texts with it and fall back to English; the manifest's "i18n" names the package
   * in other languages (docs/i18n.md).
   */
  readonly lang: EspOSLang;
}

/** The languages the S3 shows (src/i18n.cpp LANGS). */
type EspOSLang = "en" | "vi" | "zh-Hans" | "es" | "fr" | "de" | "ja" | "ko" | "pt-BR" | "ru" | "id";

interface EspOSScreen {
  readonly w: number;
  readonly h: number;
  readonly landscape: boolean;
}

interface EspOSTimer {
  /** Calls fn every ms (at least 50) while the app is open. At most 8 timers (RangeError "too many timers"). */
  every(ms: number, fn: () => void): number;
  /** Calls fn once after ms (at least 50). Counts towards the 8 timers until it fires. */
  after(ms: number, fn: () => void): number;
  cancel(id: number): void;
}

/** Needs the "sound" permission: without it every call throws TypeError('app has no "sound" permission'). */
interface EspOSSound {
  chime(): void;
  beep(): void;
  click(): void;
}

interface EspOSNotifyOpts {
  /** given back as app.arg when the notification opens the app (non-strings are sent as JSON text) */
  arg?: unknown;
  /** a newer notification with the same tag replaces the older one */
  tag?: string | number;
  /** play the notification sound (default true) */
  sound?: boolean;
  /** count it on the app's icon (default true) */
  badge?: boolean;
}

/** Needs the "notify" permission: without it every call throws TypeError('app has no "notify" permission'). */
interface EspOSNotify {
  /**
   * A notification now: banner, Notification Center, count on the icon. false when the user turned this app's
   * notifications off.
   */
  (title: string, body?: string, opts?: EspOSNotifyOpts): boolean;
  /**
   * A notification at a time, even when the app is closed; kept across restarts; at most 32 waiting.
   * at: a Date or ms since 1970; a number below 10^9 counts as ms from now.
   */
  schedule(at: Date | number, title: string, body?: string, opts?: EspOSNotifyOpts): boolean;
  /** Removes the waiting and the shown notification with that tag; no tag: every waiting one of this app. */
  cancel(tag?: string | number): void;
}

interface EspOSStore {
  /** The value saved under key (JSON), or def when there is none. Keys are 1-14 characters. */
  get<T = unknown>(key: string, def?: T): T;
  /** Saves a JSON value for this app. Keys 1-14 characters, the JSON at most 3000 characters (RangeError). */
  set(key: string, value: unknown): void;
}

declare const ui: EspOSUi;
declare const app: EspOSApp;
declare const timer: EspOSTimer;
declare const sound: EspOSSound;
declare const notify: EspOSNotify;
/** A short message at the top of the screen. */
declare function toast(text: string): void;
/** A line in the device's trace ("app <id>: <text>", the first 200 characters): what `espos device log` and the
 *  serial console show. For debugging; nothing appears on the screen. */
declare function log(text: unknown): void;
declare const store: EspOSStore;

// ---------------------------------------------------------------- API level 2 (planned, docs/app-slots.md)
// Not in firmware 0.5.x: `espos check` warns when a package uses them before its min_fw grants them.

/** A secret setting as the app sees it: whether it is set, and its last 4 characters ("" when not set). */
interface EspOSSecretState {
  set: boolean;
  hint: string;
}

/** A setting's value as the form stored it: text, number, bool, a list of texts, a city; a secret reads as its state. */
type EspOSSettingValue = string | number | boolean | string[] | { name: string; lat: number; lon: number } | EspOSSecretState | null;

interface EspOSSettings {
  /**
   * The value of one of this package's settings (manifest "settings", docs/manifest-driven.md §1), or its default,
   * or null. Secrets are never readable: they read as {set, hint}; the OS adds them to requests through the
   * manifest's "auth" bindings (docs/app-slots.md §15).
   * @since 2
   */
  get(key: string): EspOSSettingValue;
  /** Opens this app's own settings form on the device (the OS draws it from the manifest's schema). @since 2 */
  open(): void;
}

interface EspOSApp {
  /** Called when the person changed settings on the device or on the web; `changed` lists their keys. @since 2 */
  onSettings?: (changed: string[]) => void;
}

/** This package's settings (manifest "settings"). @since 2 */
declare const settings: EspOSSettings;

// ---------------------------------------------------------------- API level 2: the terminal view and dictation
// (src/js/jsterm.cpp, src/js/jsvoice.cpp; first used by apps/sessions, the Claude Code sessions app)

interface EspOSTermOpts {
  /** Required: the view and its lines survive re-renders under this key; term.apply(key, ...) feeds it. */
  key: string;
  /** Text size in px: the package's fonts/m<px>.bin (mono, ESF) when there is one, else the OS font in cells. */
  font?: number;
  /** "fit" (default): lines wrap to the view, boxes are drawn again at its width; "wide": the sender's columns, panned. */
  wrap?: "fit" | "wide";
  /** Lines kept (default 2000, 50 to 5000), in PSRAM outside the JS heap. */
  lines?: number;
  /** Background and text colour (default: the OS theme's terminal colours). */
  bg?: EspOSColor;
  fg?: EspOSColor;
  /** Height in px when not {fill: true}. */
  h?: number;
  /** Take the room the parent has left. */
  fill?: boolean;
  /** Shown at the bottom when the person scrolled up and lines came; {n} = how many. A tap goes back to the end. */
  pill?: string;
  onTap?: () => void;
  /** 1: following the end again, 0: the person scrolled back. */
  onScroll?: (following: number) => void;
}

interface EspOSTermInfo {
  follow: boolean; fresh: number; lines: number; first: number; last: number; seq: number; epoch: string; cols: number;
}

interface EspOSTermApplied {
  epoch: string; seq: number; full: boolean; starting: boolean; lines: number;
  /** the answer's "at": when the source read that screen (unix ms), for the age of what is drawn */
  at?: number;
}

interface EspOSUi {
  /** A terminal view (level 2). Feed it with term.apply(); it redraws itself without a re-render. */
  term(opts: EspOSTermOpts): EspOSNode;
}

interface EspOSCardOpts {
  /** Level 2: the whole card is a button. */
  onTap?: () => void;
}

interface EspOSBoxOpts {
  /** Level 2, column: it scrolls up and down (give it a height: fill or h). */
  scroll?: boolean;
  /** Level 2, column: its height in px. */
  h?: number;
  /** Level 2, row: children align their tops instead of their middles. */
  top?: boolean;
  /** Level 2: room inside the column or row, 0 to 24 px. */
  pad?: number;
  /** Level 2: take the room the parent has left. */
  fill?: boolean;
}

/** The lines of terminal views (level 2): a screen answer as desktop-protocol.md §9.2 shapes it, parsed in C. */
interface EspOSTerm {
  /**
   * Puts a screen answer ({epoch, seq, full, cols, rows, top, lines: [{n, t, r}]}, or the older lines of a `before`
   * answer) into the view's lines: a line with the same n replaces the old one, another epoch starts over.
   * Throws TypeError when the text is not JSON.
   */
  apply(key: string, text: string): EspOSTermApplied;
  clear(key: string): void;
  /** Follows the end again. */
  end(key: string): void;
  /** Wide views: pans by `cells` columns. */
  pan(key: string, cells: number): void;
  /** Drops the key's lines. */
  forget(key: string): void;
  info(key: string): EspOSTermInfo;
  /** The last `lines` lines as plain text ("\n" between), at most 8 KB. */
  tail(key: string, lines?: number): string;
}

/**
 * Speech to text through the OS (permission "dictate"): the OS records with its own card and mic light, sends the
 * audio to the account's server and gives the app the text only. Errors carry `code`: denied, no_mic, busy, no_room,
 * offline, locked (the OS's lock sheet shows), failed, cancelled.
 */
interface EspOSVoice {
  /**
   * expect "yesno": a spoken answer about `about` (what the app read back); the result's `yesno` is confirm, cancel,
   * unclear or other, decided by the server from the words. until_quiet: the recording ends by itself once the person
   * has spoken and gone quiet.
   */
  dictate(opts?: { lang?: string; max_ms?: number; expect?: "yesno"; about?: string; until_quiet?: boolean }):
    Promise<{ text: string; yesno?: "confirm" | "cancel" | "unclear" | "other" }>;
  /** Ends the recording now (hold to talk: the button's onUp); the text still comes. cancel: nothing is sent. */
  stop(cancel?: boolean): void;
}

declare const term: EspOSTerm;
declare const voice: EspOSVoice;

// ---------------------------------------------------------------- API level 2: net, files, crypto, UI nodes
// (src/js/jsnet.cpp, jsfs.cpp, jsui.cpp).
// Limits as the firmware has them: docs/app-slots.md §15.4 and reference/js-api-2.mdx.
// EspOS JavaScript API level 2 (docs/app-slots.md §15.4), as firmware 0.6.0 gives it: the calls this app uses on top
// of level 1 (types/espos.d.ts). @espos/sdk will carry these; until then this file declares them.

/** A colour: "#rrggbb", or a system colour that follows the light/dark theme. */
type EspOSColor2 = `#${string}` | '@bg' | '@card' | '@card2' | '@fg' | '@dim' | '@faint' | '@line' | '@chip' | '@accent' | '@ok' | '@warn' | '@bad';

interface EspOSNetRequest {
  method?: 'GET' | 'POST' | 'PUT' | 'PATCH' | 'DELETE' | 'HEAD';
  /** "https://host/path", "http://<local address>/..." (net:lan), or "{<url setting>}/path" filled in by the OS. */
  url: string;
  headers?: Record<string, string>;
  /** A string, or an object sent as JSON. 32 KB at most. */
  body?: unknown;
  /** Response header names to return (4 at most). */
  keep?: string[];
  /** Milliseconds, 20000 by default, 1000 to 60000. */
  timeout?: number;
  /** One of the manifest's `auth` bindings: the OS adds its secrets. */
  auth?: string;
  /** "cache/<name>": a 2xx body goes into that file; the answer then has `size` and no body. */
  save?: string;
}
interface EspOSNetResponse {
  status: number;
  ok: boolean;
  headers: Record<string, string>;
  body?: string;
  size?: number;
}
/** net.request rejects only without an answer. */
interface EspOSNetError extends Error {
  code: 'offline' | 'timeout' | 'dns' | 'tls' | 'too_big' | 'denied' | 'busy';
  /** The request may have reached the server. */
  sent: boolean;
}
interface EspOSNet {
  request(o: EspOSNetRequest): Promise<EspOSNetResponse>;
  readonly online: boolean;
}
interface EspOSFs {
  /** "data/<path>" (kept) or "cache/<path>" (the OS may delete old files); a bare name is data/. */
  read(name: string): string | undefined;
  write(name: string, text: string): true;
  remove(name: string): boolean;
  list(dir?: string): {name: string; size: number}[];
  free(area?: 'data' | 'cache'): number;
}
interface EspOSCrypto {
  sha256(text: string): string;
  /** n random bytes (1 to 64) as hex. */
  random(n?: number): string;
}
declare const net: EspOSNet;
declare const fs: EspOSFs;
declare const crypto: EspOSCrypto;

interface EspOSGridItem {
  title: string; sub?: string; image?: string; initials?: string; bg?: EspOSColor2; bar?: EspOSColor2;
  badge?: string; badgeBg?: EspOSColor2; tag?: string; tagBg?: EspOSColor2; dim?: boolean; span?: boolean; subColor?: EspOSColor2; fg?: EspOSColor2;
}
interface EspOSRowItem {
  title: string; sub?: string; sub2?: string; subColor?: EspOSColor2; right?: string; rsub?: string; tone?: 'ok' | 'warn' | 'bad';
  image?: string; initials?: string; bg?: EspOSColor2; fg?: EspOSColor2; badge?: string; sel?: boolean; step?: number;
}
interface EspOSNavTab { label: string; icon: string; }
interface EspOSUi {
  grid(items: EspOSGridItem[], onTap?: (i: number) => void, o?: {cols?: number; h?: number; gap?: number; onLong?: (i: number) => void; fill?: boolean}): EspOSNode;
  rows(items: EspOSRowItem[], onTap?: (i: number) => void, o?: {thumb?: number; card?: boolean; gap?: number; onStep?: (code: number) => void}): EspOSNode;
  image(src: string, o?: {w?: number; h?: number; radius?: number; band?: boolean; fallback?: {text?: string; bg?: EspOSColor2; fg?: EspOSColor2}}): EspOSNode;
  split(a: EspOSNode, b: EspOSNode, o?: {w?: number; pad?: number; footL?: EspOSNode; footR?: EspOSNode; bgL?: EspOSColor2; bgR?: EspOSColor2; fill?: boolean;
    /** each pane stays where it was scrolled when the screen is drawn again */ key?: string}): EspOSNode;
  nav(tabs: EspOSNavTab[], sel: number, onPick: (i: number) => void, o?: {badges?: string[]; foot?: EspOSNode}): EspOSNode;
  input(value: string, o?: {key?: string; hint?: string; mode?: 'text' | 'chat' | 'number' | 'decimal' | 'password' | 'search'; lines?: number; max?: number; w?: number;
    onChange?: (text: string) => void; onSubmit?: (text: string) => void;
    /** the keyboard's OK key (default "Done"); the keyboard opens when the field first shows */
    ok?: string; focus?: boolean;
    /** the keyboard opened on this field */
    onFocus?: () => void;
    /** a node (a ui.row of buttons) shown on top of the keyboard while this field has it, keysH px tall (default 40):
     *  a terminal's Esc, Tab, arrows */
    keys?: EspOSNode; keysH?: number;
    /** typing calls onChange without drawing the screen again */
    quiet?: boolean;
    /** the app's value replaces the text even while someone types (a chat field emptied after Send) */
    force?: boolean;
    bg?: EspOSColor2; border?: EspOSColor2}): EspOSNode;
  qr(text: string, o?: {size?: number}): EspOSNode;
  line(left: string, right?: string, o?: {bold?: boolean; size?: number; color?: EspOSColor2; rcolor?: EspOSColor2; dim?: boolean}): EspOSNode;
  sheet(children: (EspOSNode | null | false | undefined)[], o?: {onClose?: () => void; w?: number}): EspOSNode;
}
interface EspOSApp {
  /** No nav bar: the body is the app's, edge to edge. */
  full?: boolean;
  /** The back gesture asks the app first; return true when it went back inside itself. */
  onBack?: () => boolean;
  /** The system theme is dark now. */
  readonly dark: boolean;
  /** true: network and voice answers do not draw the screen again by themselves (call refresh()); taps still do. */
  selfDraw?: boolean;
  /** The keyboard's height (with its key row) while it types into one of the app's fields, else 0. A full-screen
   *  app's body is that much shorter while it is up; the app is drawn again when it changes. */
  readonly kb: number;
  /** Puts the keyboard away (when it types into one of the app's fields). The back gesture does it first, too. */
  closeKeyboard(): void;
}
interface EspOSButtonOpts { w?: number; h?: number; onDown?: () => void; onUp?: () => void; fill?: boolean;
  /** the tap does not draw the screen again by itself (a terminal's keys); call app.refresh() when it changes it */
  quiet?: boolean; }
interface EspOSBoxOpts {
  fill?: boolean;
  /** room inside, px */
  pad?: number;
  /** a column that scrolls (give it a height: fill or h) */
  scroll?: boolean;
  /** with scroll: it stays where it was scrolled when the screen is drawn again */
  key?: string;
  /** a column of a set height */
  h?: number;
}

interface EspOSButtonOpts {
  /** An OS glyph ("search", "plus"...) or a package .a8 icon, drawn instead of (or before) the text. */
  icon?: string;
}

// ---------------------------------------------------------------- API level 2: screen saver apps (firmware 0.6.1)
// A package with "provides": ["saver"] (docs/app-slots.md §4, src/js/jssaver.cpp): picked in Settings, Default apps,
// Screen saver, its saver file (manifest "entry": {"saver": "saver.js"}, else saver.js, else main.js) runs while the
// saver shows, in a runtime of its own: app.onFrame draws each frame into g. Only these names exist there (no ui,
// store, net, timer or sound); a touch leaves the saver. Guide: espos.dev/docs/guides/saver-apps.

/** "#rrggbb", "#rgb" or a number 0xrrggbb. */
type EspOSSaverColor = string | number;

interface EspOSFrame {
  /** The frame's size in pixels: the whole screen (320 x 480 portrait on the 3.5" board, 480 x 320 turned). */
  readonly w: number;
  readonly h: number;
  /** The screen's size class ("medium" on the S3 boards) and depth (false: colour). */
  readonly cls: string;
  readonly mono: boolean;
  /** Fills the frame and forgets what was drawn before in it. */
  clear(c?: EspOSSaverColor): void;
  /** r: the corners' radius; a: opacity 0..1. */
  rect(x: number, y: number, w: number, h: number, c: EspOSSaverColor, o?: {r?: number; a?: number}): void;
  circle(x: number, y: number, r: number, c: EspOSSaverColor, o?: {a?: number}): void;
  /** w: the width in pixels (1..64, default 2), round ends. */
  line(x1: number, y1: number, x2: number, y2: number, c: EspOSSaverColor, o?: {w?: number; a?: number}): void;
  /** From c1 to c2, top to bottom ("v", the default) or left to right ("h"). */
  gradient(x: number, y: number, w: number, h: number, c1: EspOSSaverColor, c2: EspOSSaverColor, o?: {dir?: "v" | "h"; a?: number}): void;
  /** y is the text's top; x is where its left edge, middle or right edge goes (align). size 10..96 px (the
   *  sizes over 24 have digits and : . , + - % ° only); "\n" starts a new line. */
  text(s: unknown, x: number, y: number, o?: {size?: number; color?: EspOSSaverColor; align?: "left" | "center" | "right"; bold?: boolean; a?: number}): void;
  /** The size text would take. */
  measure(s: unknown, o?: {size?: number; bold?: boolean}): {w: number; h: number};
}

interface EspOSSaverInfo {
  as: "saver";
  w: number;
  h: number;
  cls: string;
  mono: boolean;
  scale: number;
  landscape: boolean;
}

interface EspOSApp {
  /** Saver apps: draw one frame into g; dt is the time since the last one in ms (0 the first time). A frame that
   *  draws nothing keeps the last one on the screen. Up to 200 ms a call, 600 shapes and 4 KB of text. @since 2 */
  onFrame?: (g: EspOSFrame, dt: number) => void;
  /** Saver apps: the saver starts (once, before the first frame). @since 2 */
  onStart?: (info: EspOSSaverInfo) => void;
  /** Saver apps: the saver goes (a touch, the night, the screen turned). @since 2 */
  onStop?: () => void;
  /** Saver apps: the assistant's pop-up came over the saver (no frames until onResume). @since 2 */
  onPause?: () => void;
  onResume?: () => void;
  /** Saver apps: frames a second wanted, 1..20 (10 when not asked); returns the rate given. @since 2 */
  fps?: (n: number) => number;
  /** Saver apps: the device's time now, formatted: HH H hh h mm ss a dd d MM M yyyy yy ("HH:mm" when left out). @since 2 */
  time?: (fmt?: string) => string;
}
