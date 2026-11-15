/**
 * What the page tells the app. The server writes it into index.html as
 * <script id="app-inkdesk-config">; `npm run dev` has none and uses this origin.
 */
export interface AppConfig {
  app: string;
  /** The live version's number (null for a dev build). */
  version: number | null;
  /** The version's URL segment: files live under `base`. */
  ref: string;
  entry: string;
  /** Where this version's files are served: /_app/inkdesk/<ref>/ */
  base: string;
  /** The id of the element the app renders into. */
  root: string;
  store: { id: string; slug: string };
  /** The page's language, and the website's default one. */
  locale: string;
  locale_default: string;
  /** The server's address (this page's origin). */
  api: string | null;
  /** Where a client-side router is rooted on this page: the app route's
   *  prefix, else the page's own path. Use it as the
   *  router's basename. */
  base_path: string;
  /** True in a preview build. */
  preview: boolean;
}

export function readConfig(root: HTMLElement): AppConfig {
  const el = document.getElementById(`${root.id}-config`);
  if (el?.textContent) return JSON.parse(el.textContent) as AppConfig;
  return {
    app: "inkdesk", version: null, ref: "dev", entry: "", base: "/", root: root.id,
    store: { id: "1", slug: "inkdesk" }, locale: "vi", locale_default: "vi",
    api: location.origin, base_path: "/", preview: true,
  };
}
