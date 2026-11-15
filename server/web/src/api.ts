/**
 * The InkDesk server's API, from the browser.
 *
 *  - The API is `config.api`: the server this page came from.
 *  - Signing in returns a session token; this file keeps it in localStorage and sends
 *    it as `Authorization: Bearer`.
 *  - Reads and writes go through the server's /v1 routes (`call("/v1/books")`).
 */
import type { AppConfig } from "./config";

export interface AuthProvider {
  provider: string;
  label: string;
  web: boolean;
  native: boolean;
  /** Present only when the WEB flow is configured: render a link to it. */
  start?: string;
  native_endpoint?: string;
}

export interface AuthConfig {
  /** false = email + password is switched off: draw no password form. */
  password: boolean;
  providers: AuthProvider[];
}

export interface Violation {
  column: string;
  rule: string;
  detail: string;
}

/**
 * A refusal, as the backend said it. What each status means for the screen
 * (shop-backend-frontend §5):
 *   401 no session or it expired → back to sign-in (the session is dropped)
 *   402 the merchant's plan does not cover this → the OWNER's problem; no retry
 *   403 `password_sign_in_disabled` → hide the password form
 *   404 no such route, or no such record → read `message`
 *   422 the write was refused per column → show `violations` beside the fields
 *   429 too many requests → back off
 */
export class ApiError extends Error {
  constructor(
    readonly status: number,
    message: string,
    readonly code?: string,
    readonly violations: Violation[] = [],
  ) {
    super(message);
  }
}

export interface Api {
  /** Whether the website has a backend to talk to at all. */
  readonly available: boolean;
  session(): string | null;
  signOut(): void;
  authConfig(): Promise<AuthConfig>;
  signIn(email: string, password: string): Promise<unknown>;
  register(email: string, password: string): Promise<unknown>;
  /** The link for a provider's `start` (§3): an <a href>, not fetch(). */
  providerLink(p: AuthProvider, returnTo?: string): string | null;
  /** Read a session (or a sign-in error) the provider flow left in the URL
   *  fragment, and take it out of the address bar. */
  takeSessionFromURL(): { error?: string; description?: string } | null;
  me(): Promise<unknown>;
  /** A workflow route of the merchant's backend, with the session. */
  call<T = unknown>(path: string, init?: { method?: string; body?: unknown }): Promise<T>;
  /** Listen on a channel a workflow publishes to; returns a stop function. */
  listen(channel: string, onMessage: (data: unknown, event: string) => void): () => void;
}

export function createApi(config: AppConfig): Api {
  const base = (config.api ?? "").replace(/\/+$/, "");
  const key = `wm_session:${config.store.slug}`;

  const session = () => {
    try {
      return localStorage.getItem(key);
    } catch {
      return null;
    }
  };
  const save = (token: string | null) => {
    try {
      if (token) localStorage.setItem(key, token);
      else localStorage.removeItem(key);
    } catch {
      /* storage blocked: the session lives as long as the page */
    }
  };

  async function request<T>(path: string, init: { method?: string; body?: unknown; auth?: boolean } = {}): Promise<T> {
    if (!base) throw new ApiError(0, "this website has no backend API yet");
    const headers: Record<string, string> = {};
    if (init.body !== undefined) headers["Content-Type"] = "application/json";
    const token = session();
    if (init.auth !== false && token) headers.Authorization = `Bearer ${token}`;
    const res = await fetch(base + path, {
      method: init.method ?? (init.body === undefined ? "GET" : "POST"),
      headers,
      body: init.body === undefined ? undefined : JSON.stringify(init.body),
    });
    const text = await res.text();
    let data: unknown = null;
    try {
      data = text ? JSON.parse(text) : null;
    } catch {
      data = text;
    }
    if (!res.ok) {
      const d = (data ?? {}) as { error?: string; message?: string; code?: string; violations?: Violation[] };
      if (res.status === 401) save(null);
      throw new ApiError(res.status, d.error ?? d.message ?? res.statusText, d.code, d.violations ?? []);
    }
    return data as T;
  }

  return {
    available: Boolean(base),
    session,
    signOut() {
      const token = session();
      save(null);
      if (base && token) {
        void fetch(`${base}/auth/logout`, { method: "POST", headers: { Authorization: `Bearer ${token}` } }).catch(() => {});
      }
    },
    // §1 — render the sign-in screen FROM this: never hard-code a provider.
    authConfig: () => request<AuthConfig>("/auth/config", { auth: false }),
    // §2 — sign-in returns the session; register does NOT (the confirmation
    // goes to the mailbox).
    async signIn(email, password) {
      const out = await request<{ token: string; user: unknown }>("/auth/login", {
        method: "POST",
        body: { email, password },
        auth: false,
      });
      save(out.token);
      return out.user;
    },
    register: (email, password) => request("/auth/register", { method: "POST", body: { email, password }, auth: false }),
    providerLink(p, returnTo = location.href.split("#")[0]) {
      if (!base || !p.start) return null;
      return `${base}${p.start}?return_to=${encodeURIComponent(returnTo)}`;
    },
    // §3 — the session arrives in the FRAGMENT (never sent to a server).
    takeSessionFromURL() {
      const hash = new URLSearchParams(location.hash.slice(1));
      const token = hash.get("token");
      const error = hash.get("error");
      if (!token && !error) return null;
      if (token) save(token);
      history.replaceState(null, "", location.pathname + location.search);
      return error ? { error, description: hash.get("error_description") ?? undefined } : {};
    },
    me: () => request("/auth/me"),
    call: (path, init) => request(path, init ?? {}),
    // §4b — public: channels reconnect on their own; a user: channel trades
    // the session (in a header, once) for a single-use ticket and opens again
    // after an error, because the ticket is spent.
    listen(channel, onMessage) {
      let es: EventSource | null = null;
      let stopped = false;
      const open = async () => {
        if (stopped || !base) return;
        let url = `${base}/_bb/stream/${channel}`;
        if (!channel.startsWith("public:")) {
          try {
            const { ticket } = await request<{ ticket: string }>("/_bb/stream-ticket", { method: "POST", body: { channel } });
            url += `?ticket=${encodeURIComponent(ticket)}`;
          } catch {
            setTimeout(open, 5000);
            return;
          }
        }
        es = new EventSource(url);
        es.addEventListener("message", (e) => onMessage(parse((e as MessageEvent).data), "message"));
        es.addEventListener("error", () => {
          if (!channel.startsWith("public:")) {
            es?.close();
            setTimeout(open, 2000);
          }
        });
      };
      void open();
      return () => {
        stopped = true;
        es?.close();
      };
    },
  };
}

function parse(s: string): unknown {
  try {
    return JSON.parse(s);
  } catch {
    return s;
  }
}
