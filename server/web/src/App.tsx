import { useCallback, useEffect, useMemo, useRef, useState } from "react";

import { ApiError, createApi, type Api } from "./api";
import type { AppConfig } from "./config";
import { ago, ink, type Device } from "./espos";
import { Devices } from "./pages/Devices";
import { Library } from "./pages/Library";
import { News } from "./pages/News";
import { Settings } from "./pages/Settings";
import { Stats } from "./pages/Stats";
import { LayoutPage } from "./pages/Layout";
import { Icon } from "./ui";
import { BrandLogo } from "./brand";

type Route = "library" | "news" | "stats" | "layout" | "devices" | "settings";
interface Me {
  id: number;
  email: string;
}

/** /auth/me answers {user:{id,email,…}}; sign-in answers the user itself. Accept both. */
function toMe(r: unknown): Me {
  const o = (r ?? {}) as { user?: Me } & Partial<Me>;
  const u = o.user ?? o;
  return { id: Number(u.id ?? 0), email: String(u.email ?? "") };
}

function readRoute(): { route: Route; query: URLSearchParams } {
  const h = location.hash.replace(/^#\/?/, "");
  const [path, q] = h.split("?");
  const route: Route = path === "settings" || path === "news" || path === "stats" || path === "layout" ? path : path === "devices" || path === "pair" ? "devices" : "library";
  const query = new URLSearchParams(q ?? "");
  if (path === "pair" && query.get("code")) query.set("pair", query.get("code")!);
  return { route, query };
}

export function App({ config }: { config: AppConfig }) {
  const api = useMemo(() => createApi(config), [config]);
  const [me, setMe] = useState<Me | null>(null);
  const [checked, setChecked] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [loc, setLoc] = useState(readRoute);

  useEffect(() => {
    const onHash = () => setLoc(readRoute());
    window.addEventListener("hashchange", onHash);
    return () => window.removeEventListener("hashchange", onHash);
  }, []);

  useEffect(() => {
    // A pairing link opened before signing in: keep the code across the sign-in round trip.
    const code = loc.query.get("pair");
    if (code) sessionStorage.setItem("espos_pair", code);
  }, [loc]);

  useEffect(() => {
    const back = api.takeSessionFromURL();
    if (back?.error) setError(back.description ?? back.error);
    if (!api.session()) {
      setChecked(true);
      return;
    }
    api.me().then(
      (u) => {
        setMe(toMe(u));
        setChecked(true);
        const code = sessionStorage.getItem("espos_pair");
        if (code) location.hash = `#/devices?pair=${code}`;
      },
      () => setChecked(true),
    );
  }, [api]);

  const signOut = useCallback(() => {
    api.signOut();
    setMe(null);
  }, [api]);

  if (!api.available) return <div className="ik ik-center">Website chưa có backend.</div>;
  if (!checked) return <div className="ik ik-center ik-muted">Đang tải…</div>;
  if (!me) return <Login api={api} error={error} onIn={setMe} />;

  const go = (r: Route) => () => (location.hash = `#/${r}`);
  const nav: { id: Route; label: string; icon: string }[] = [
    { id: "library", label: "Thư viện", icon: "book" },
    { id: "news", label: "Báo sáng", icon: "news" },
    { id: "stats", label: "Thống kê đọc", icon: "chart" },
    { id: "layout", label: "Giao diện máy", icon: "grid" },
    { id: "devices", label: "Thiết bị", icon: "device" },
    { id: "settings", label: "Cài đặt máy", icon: "sliders" },
  ];
  const k = ink(api);
  return <Shell k={k} me={me} route={loc.route} query={loc.query} apiBase={config.api ?? ""} go={go} nav={nav} signOut={signOut} />;
}

function Shell({
  k,
  me,
  route,
  query,
  apiBase,
  go,
  nav,
  signOut,
}: {
  k: ReturnType<typeof ink>;
  me: Me;
  route: Route;
  query: URLSearchParams;
  apiBase: string;
  go: (r: Route) => () => void;
  nav: { id: Route; label: string; icon: string }[];
  signOut: () => void;
}) {
  const [dev, setDev] = useState<Device | null | undefined>(undefined);
  const firstLoad = useRef(true);
  useEffect(() => {
    k.devices().then(
      (d) => {
        setDev(d[0] ?? null);
        // An account with no X4 yet starts on the page that installs and links one.
        if (firstLoad.current && d.length === 0 && route !== "devices") location.hash = "#/devices";
        firstLoad.current = false;
      },
      () => setDev(null),
    );
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [route]);
  const loc = { route, query };
  return (
    <div className="ik ik-app">
      <aside className="ik-side">
        <div className="ik-brand">
          <BrandLogo height={30} />
        </div>
        <nav className="ik-nav">
          {nav.map((n) => (
            <button key={n.id} type="button" className={loc.route === n.id ? "on" : ""} onClick={go(n.id)}>
              <Icon name={n.icon} />
              {n.label}
            </button>
          ))}
        </nav>
        {dev !== undefined && (
          <div className="ik-devcard">
            <span className="k">MÁY CỦA BẠN</span>
            {dev ? (
              <>
                <b>{dev.name}</b>
                <div className="row">
                  <span>Đồng bộ</span>
                  <span>{ago(dev.last_sync)}</span>
                </div>
                <div className="row">
                  <span>Pin</span>
                  <span>{dev.battery != null ? `${dev.battery}%` : "—"}</span>
                </div>
              </>
            ) : (
              <>
                <span>Chưa có máy nào.</span>
                <button type="button" className="ik-btn sm" onClick={go("devices")}>
                  Cài lên X4
                </button>
              </>
            )}
          </div>
        )}
        <div className="ik-me">
          <span className="ik-avatar">{(me.email || "?").slice(0, 1).toUpperCase()}</span>
          <div title={me.email}>
            <b>{me.email.split("@")[0]}</b>
            <span>Tài khoản EspOS</span>
          </div>
          <button type="button" className="ik-link" onClick={signOut}>
            Thoát
          </button>
        </div>
      </aside>
      <main className="ik-main">
        {loc.route === "library" && <Library ink={k} />}
        {loc.route === "news" && <News ink={k} />}
        {loc.route === "stats" && <Stats ink={k} />}
        {loc.route === "layout" && <LayoutPage ink={k} />}
        {loc.route === "devices" && <Devices ink={k} apiBase={apiBase} me={me} pairCode={loc.query.get("pair")} />}
        {loc.route === "settings" && <Settings ink={k} />}
      </main>
    </div>
  );
}

function Login({ api, error, onIn }: { api: Api; error: string | null; onIn: (m: Me) => void }) {
  const [msg, setMsg] = useState<string | null>(error);
  const [busy, setBusy] = useState(false);

  async function submit(e: React.FormEvent<HTMLFormElement>) {
    e.preventDefault();
    const f = new FormData(e.currentTarget);
    setBusy(true);
    setMsg(null);
    try {
      await api.signIn(String(f.get("user")).trim(), String(f.get("password")));
      onIn(toMe(await api.me()));
    } catch (err) {
      setMsg(err instanceof ApiError ? err.message : String(err));
    } finally {
      setBusy(false);
    }
  }

  return (
    <div className="ik ik-login">
      <div className="ik-art">
        <div className="ik-brand light">
          <BrandLogo height={34} />
        </div>
        <div>
          <h1>
            Sách, lịch và tin tức của bạn trên máy đọc <span className="nowrap">e‑ink</span>.
          </h1>
          <p>Gửi sách lên, chọn những gì hiện trên màn hình lúc máy nghỉ. Xteink X4 tự đồng bộ với máy chủ này của bạn.</p>
        </div>
        <ul className="ik-feat">
          <li>
            <Icon name="book" />
            Gửi sách xuống máy, như Send to Kindle
          </li>
          <li>
            <Icon name="grid" />
            Màn chính và màn chờ ghép từ widget
          </li>
          <li>
            <Icon name="usb" />
            Cài firmware ngay trên trình duyệt
          </li>
        </ul>
      </div>
      <div className="ik-formside">
        <div className="ik-card2">
          <h2>Đăng nhập</h2>
          <form onSubmit={submit} className="ik-form1">
            <label>
              Tên đăng nhập
              <input name="user" type="text" required autoComplete="username" autoCapitalize="none" spellCheck={false} />
            </label>
            <label>
              Mật khẩu
              <input name="password" type="password" required autoComplete="current-password" />
            </label>
            <button className="ik-btn" disabled={busy} type="submit">
              {busy ? "Đang đăng nhập…" : "Đăng nhập"}
            </button>
          </form>
          {msg && <p className="ik-err">{msg}</p>}
          <p className="ik-fine">Máy chủ tự host: một tài khoản duy nhất, đặt bằng ESPOS_USER và ESPOS_PASSWORD.</p>
        </div>
      </div>
    </div>
  );
}
