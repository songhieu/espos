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
declare const store: EspOSStore;
