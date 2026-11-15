import { useEffect, useMemo, useState } from "react";

import { DashPreview, normLayout } from "../DashPreview";
import { WifiPanel } from "./SleepScreen";
import { CityPicker, TzSelect } from "../PlacePickers";
import { FontPicker, PagePreview } from "../ReadingPrefs";
import { ago, randomHex, sha256Hex, type Dash, type Device, type Ink, type SettingCell } from "../inkdesk";
import { Icon, Seg, Toggle } from "../ui";

type Tab = "read" | "sys" | "dash" | "wifi" | "notify";
const TABS: { id: Tab; label: string }[] = [
  { id: "read", label: "Đọc sách" },
  { id: "sys", label: "Máy" },
  { id: "dash", label: "Thời tiết & lịch" },
  { id: "wifi", label: "WiFi" },
  { id: "notify", label: "Thông báo" },
];

export function Settings({ ink }: { ink: Ink }) {
  const [devices, setDevices] = useState<Device[] | null>(null);
  const [sel, setSel] = useState<number | null>(null);
  const [cells, setCells] = useState<Record<string, SettingCell> | null>(null);
  const [dev, setDev] = useState<Device | null>(null);
  const [draft, setDraft] = useState<Record<string, string>>({});
  const [tab, setTab] = useState<Tab>("read");
  const [msg, setMsg] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  const [dash, setDash] = useState<Dash | null>(null);

  useEffect(() => {
    ink.devices().then((d) => {
      setDevices(d);
      if (d.length) setSel(d[0].id);
    }, (e: Error) => setMsg(e.message));
  }, [ink]);

  const load = (id: number) => {
    setDash(null);
    ink.deviceDashboard(id).then(setDash, () => setDash(null));
    return ink.settings(id).then((r) => {
      setCells(r.settings);
      setDev(r.device);
      setDraft({});
    }, (e: Error) => setMsg(e.message));
  };
  useEffect(() => {
    if (sel != null) void load(sel);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [sel]);

  const v = (k: string) => draft[k] ?? cells?.[k]?.v ?? "";
  const set = (k: string) => (val: string) => setDraft((d) => ({ ...d, [k]: val }));
  const fromDevice = useMemo(
    () => Object.entries(cells ?? {}).filter(([, c]) => c.source === "device").sort((a, b) => b[1].ms - a[1].ms),
    [cells],
  );
  const badge = (k: string) => {
    const c = cells?.[k];
    if (!c || c.source !== "device" || draft[k] !== undefined) return null;
    return <span className="fromdev">sửa trên máy {new Date(c.ms).toLocaleTimeString("vi-VN", { hour: "2-digit", minute: "2-digit" })}</span>;
  };

  async function save() {
    if (sel == null || Object.keys(draft).length === 0) return;
    setBusy(true);
    setMsg(null);
    try {
      await ink.saveSettings(sel, draft);
      setMsg("✓ Đã lưu. Máy nhận ở lần thức dậy hoặc đồng bộ tới.");
      await load(sel);
    } catch (e) {
      setMsg((e as Error).message);
    } finally {
      setBusy(false);
    }
  }

  if (devices && devices.length === 0) {
    return (
      <section className="ik-page">
        <header className="ik-head">
          <div>
            <h2>Cài đặt máy</h2>
            <p>Chưa có máy nào. Vào "Thiết bị" để cài InkDesk lên Xteink X4.</p>
          </div>
        </header>
      </section>
    );
  }

  const dirty = Object.keys(draft).length > 0;
  return (
    <section className="ik-page">
      <header className="ik-head">
        <div>
          <h2>Cài đặt máy {dev ? `· ${dev.name}` : ""}</h2>
          <p>Sửa ở đây hoặc trên máy đều được. Hai bên khớp lại ở lần đồng bộ kế tiếp; mục nào sửa sau cùng thì giữ.</p>
        </div>
        <div className="ik-headact">
          {devices && devices.length > 1 && (
            <select aria-label="Chọn máy" value={sel ?? ""} onChange={(e) => setSel(Number(e.target.value))}>
              {devices.map((d) => (
                <option key={d.id} value={d.id}>
                  {d.name}
                </option>
              ))}
            </select>
          )}
          <button type="button" className="ik-btn" disabled={!dirty || busy} onClick={() => void save()}>
            {busy ? "Đang lưu…" : "Lưu"}
          </button>
        </div>
      </header>
      {dev && (
        <div className="ik-syncbar">
          <Icon name="sync" />
          <span>
            Máy đồng bộ lần cuối {ago(dev.last_sync)}
            {fromDevice.length > 0 ? `; đã gửi lên ${fromDevice.length} thay đổi từ máy.` : "."} Thay đổi bạn lưu sẽ về máy ở lần thức dậy tới.
          </span>
        </div>
      )}
      {msg && <p className={msg.startsWith("✓") ? "ik-ok" : "ik-err"}>{msg}</p>}
      <div className="ik-wtabs" role="tablist">
        {TABS.map((t) => (
          <button key={t.id} type="button" role="tab" aria-selected={tab === t.id} className={tab === t.id ? "on" : ""} onClick={() => setTab(t.id)}>
            {t.label}
          </button>
        ))}
      </div>
      <div className="ik-setgrid">
        <div>
      {!cells ? (
          <p className="ik-muted">Đang tải…</p>
        ) : (
          <div className="ik-panel ik-sets">
            {tab === "read" && (
              <>
                <div className="ik-setblock">
                  <p className="ik-label">
                    Font chữ khi đọc {badge("read.font")}
                  </p>
                  <FontPicker value={v("read.font") || "notosans"} installed={dev?.fonts ?? []} synced={cells?.["read.font"]?.v ?? "notosans"} onChange={set("read.font")} />
                </div>
                <Row title="Cỡ chữ" badge={badge("read.size")}>
                  <Seg label="Cỡ chữ" value={v("read.size")} onChange={set("read.size")} options={[{ v: "small", label: "12" }, { v: "medium", label: "14" }, { v: "large", label: "16" }, { v: "xlarge", label: "18" }]} />
                </Row>
                <Row title="Giãn dòng" badge={badge("read.spacing")}>
                  <Seg label="Giãn dòng" value={v("read.spacing")} onChange={set("read.spacing")} options={[{ v: "1.0", label: "100%" }, { v: "1.1", label: "110%" }, { v: "1.2", label: "120%" }, { v: "1.3", label: "130%" }, { v: "1.4", label: "145%" }, { v: "1.6", label: "160%" }]} />
                </Row>
                <Row title="Căn lề chữ" badge={badge("read.align")}>
                  <Seg label="Căn lề" value={v("read.align")} onChange={set("read.align")} options={[{ v: "justify", label: "Đều hai bên" }, { v: "left", label: "Trái" }]} />
                </Row>
                <Row title="Lề trang" badge={badge("read.margin")}>
                  <Seg label="Lề trang" value={v("read.margin")} onChange={set("read.margin")} options={[{ v: "narrow", label: "Hẹp" }, { v: "medium", label: "Vừa" }, { v: "wide", label: "Rộng" }]} />
                </Row>
                <Row title="Thụt đầu dòng" badge={badge("read.indent")}>
                  <Seg label="Thụt đầu dòng" value={v("read.indent")} onChange={set("read.indent")} options={[{ v: "0", label: "Không" }, { v: "1", label: "1 ký tự" }, { v: "2", label: "2 ký tự" }]} />
                </Row>
                <Row title="Cách đoạn" desc="Thêm khoảng trống giữa các đoạn" badge={badge("read.para")}>
                  <Toggle label="Cách đoạn" on={v("read.para") !== "0"} onChange={(b) => set("read.para")(b ? "1" : "0")} />
                </Row>
                <Row title="Ngắt từ cuối dòng" desc="Hợp với sách tiếng Anh; sách tiếng Việt nên tắt" badge={badge("read.hyphen")}>
                  <Toggle label="Ngắt từ" on={v("read.hyphen") === "1"} onChange={(b) => set("read.hyphen")(b ? "1" : "0")} />
                </Row>
                <Row title="Chữ mịn (khử răng cưa)" desc="Đẹp hơn, lật trang chậm hơn một chút" badge={badge("read.aa")}>
                  <Toggle label="Chữ mịn" on={v("read.aa") !== "0"} onChange={(b) => set("read.aa")(b ? "1" : "0")} />
                </Row>
                <Row title="Làm sạch màn hình sau" desc="Chớp toàn màn để xoá bóng mờ" badge={badge("read.refresh")}>
                  <Seg label="Làm sạch màn" value={v("read.refresh")} onChange={set("read.refresh")} options={[{ v: "1", label: "1" }, { v: "5", label: "5" }, { v: "10", label: "10" }, { v: "15", label: "15" }, { v: "30", label: "30 trang" }]} />
                </Row>
                <Row title="Ảnh trong sách" badge={badge("read.images")}>
                  <Seg label="Ảnh trong sách" value={v("read.images")} onChange={set("read.images")} options={[{ v: "show", label: "Hiện (xám)" }, { v: "bw", label: "Đen trắng" }, { v: "hide", label: "Ẩn" }]} />
                </Row>
                <Row title="Thanh trạng thái" badge={badge("read.status")}>
                  <div className="ik-chips">
                    {[
                      { id: "chapter", label: "Tên chương" },
                      { id: "page", label: "Số trang" },
                      { id: "percent", label: "% đã đọc" },
                      { id: "clock", label: "Giờ" },
                      { id: "battery", label: "Pin" },
                    ].map((c) => {
                      const cur = (v("read.status") || "").split(",").filter(Boolean);
                      const on = cur.includes(c.id);
                      return (
                        <button
                          key={c.id}
                          type="button"
                          aria-pressed={on}
                          className={on ? "on" : ""}
                          onClick={() => set("read.status")((on ? cur.filter((x) => x !== c.id) : [...cur, c.id]).join(","))}
                        >
                          {c.label}
                        </button>
                      );
                    })}
                  </div>
                </Row>
              </>
            )}
            {tab === "sys" && (
              <>
                <Row title="Ngôn ngữ giao diện" badge={badge("sys.lang")}>
                  <Seg label="Ngôn ngữ" value={v("sys.lang")} onChange={set("sys.lang")} options={[{ v: "vi", label: "Tiếng Việt" }, { v: "en", label: "English" }]} />
                </Row>
                <Row title="Múi giờ" desc="Giờ trên máy, ngày âm lịch và thời tiết theo giờ" badge={badge("sys.tz_min")}>
                  <TzSelect value={v("sys.tz_min")} onChange={set("sys.tz_min")} />
                </Row>
                <Row title="Tự ngủ sau" desc="Không bấm nút trong khoảng này thì máy ngủ" badge={badge("sys.sleep_min")}>
                  <Seg label="Tự ngủ sau" value={v("sys.sleep_min")} onChange={set("sys.sleep_min")} options={[{ v: "5", label: "5" }, { v: "10", label: "10" }, { v: "15", label: "15" }, { v: "30", label: "30 phút" }]} />
                </Row>
                <Row title="Bấm nhanh nút nguồn" badge={badge("sys.pwr_short")}>
                  <Seg label="Nút nguồn" value={v("sys.pwr_short")} onChange={set("sys.pwr_short")} options={[{ v: "sleep", label: "Ngủ" }, { v: "page", label: "Lật trang" }, { v: "refresh", label: "Làm sạch màn" }, { v: "ignore", label: "Không làm gì" }]} />
                </Row>
                <Row title="Nút bên" desc="Nút trên cùng là" badge={badge("sys.side")}>
                  <Seg label="Nút bên" value={v("sys.side")} onChange={set("sys.side")} options={[{ v: "prev_next", label: "Trang trước" }, { v: "next_prev", label: "Trang sau" }]} />
                </Row>
                <Row title="Nút trước" desc="Thứ tự 4 nút dưới màn hình" badge={badge("sys.front")}>
                  <select className="ik-selectin" aria-label="Thứ tự nút trước" value={v("sys.front") || "bclr"} onChange={(e) => set("sys.front")(e.target.value)}>
                    <option value="bclr">Quay lại · Chọn · Trái · Phải</option>
                    <option value="lrbc">Trái · Phải · Quay lại · Chọn</option>
                    <option value="lbcr">Trái · Quay lại · Chọn · Phải</option>
                    <option value="bcrl">Quay lại · Chọn · Phải · Trái</option>
                  </select>
                </Row>
                <Row title="Chiều màn hình khi đọc" badge={badge("sys.orient")}>
                  <Seg label="Chiều màn hình" value={v("sys.orient")} onChange={set("sys.orient")} options={[{ v: "portrait", label: "Dọc" }, { v: "landscape", label: "Ngang" }, { v: "inverted", label: "Dọc ngược" }]} />
                </Row>
                <Row title="Giờ trên thanh trạng thái" badge={badge("sys.clock_fmt")}>
                  <Seg label="Định dạng giờ" value={v("sys.clock_fmt")} onChange={set("sys.clock_fmt")} options={[{ v: "24", label: "24 giờ" }, { v: "12", label: "12 giờ" }]} />
                </Row>
              </>
            )}
            {tab === "dash" && (
              <>
                <div className="ik-movednote">
                  <b>Kiểu màn chờ đã chuyển sang “Giao diện máy”.</b>
                  <span>Ở đó bạn chọn màn chờ hiện widget, mẫu cổ điển, ảnh của bạn hay bìa sách, và chỉnh giờ cập nhật.</span>
                </div>
                <Row title="Thành phố" desc="Cho khối thời tiết" badge={badge("dash.city")}>
                  <CityPicker
                    value={v("dash.city")}
                    onChange={(c) => setDraft((d) => ({ ...d, "dash.city": c.name, "dash.lat": String(c.lat), "dash.lon": String(c.lon) }))}
                  />
                </Row>
                <div className="ik-setblock">
                  <p className="ik-label">
                    Lịch (Google, iCloud, Outlook… tối đa 5 lịch) {badge("dash.ics_url")}
                  </p>
                  <textarea
                    className="ik-inp ik-ics"
                    rows={3}
                    placeholder={"Mỗi dòng một link lịch, bắt đầu bằng https:// hoặc webcal://"}
                    defaultValue={v("dash.ics_url")}
                    key={"ics" + (cells["dash.ics_url"]?.ms ?? 0)}
                    onBlur={(e) => {
                      const val = e.target.value
                        .split(/\s+/)
                        .filter(Boolean)
                        .join("\n");
                      if (val !== (cells["dash.ics_url"]?.v ?? "")) set("dash.ics_url")(val);
                    }}
                  />
                  <details className="ik-icshelp">
                    <summary>Lấy link lịch ở đâu?</summary>
                    <ul>
                      <li>
                        <b>Google Calendar</b> (máy tính): Cài đặt → chọn lịch → Tích hợp lịch → “Địa chỉ bí mật ở định dạng iCal”.
                      </li>
                      <li>
                        <b>iCloud / Lịch trên iPhone</b>: app Lịch → Lịch → bấm (i) cạnh lịch → bật “Lịch công khai” → Chia sẻ liên kết (link webcal://).
                      </li>
                      <li>
                        <b>Outlook / Microsoft 365</b>: outlook.com → Cài đặt → Lịch → Lịch được chia sẻ → Phát hành lịch → chép link ICS.
                      </li>
                      <li>
                        <b>Lịch khác</b> (Proton, Zoho, Fastmail, Nextcloud, lịch ngày lễ…): tìm mục “Đăng ký”, “Xuất”, “ICS” hoặc “iCal” và chép link.
                      </li>
                    </ul>
                    <p>Link bí mật cho phép ai có nó xem lịch của bạn: đừng chia sẻ nó ở nơi khác.</p>
                  </details>
                </div>
              </>
            )}
            {tab === "wifi" && sel != null && <WifiPanel ink={ink} dev={sel} />}
            {tab === "notify" && <NotifyKey ink={ink} />}
          </div>
        )}
        </div>
        <aside className="ik-preview">
          {tab === "read" ? <PagePreview v={v} /> : <DashPreview d={dash} layout={normLayout(v("dash.layout"))} />}
          <p className="cap">
            {tab === "read" ? "Xem trước trang sách trên X4, đổi ngay khi bạn chọn." : "Màn hình máy lúc ngủ, vẽ từ dữ liệu thật. Lưu thay đổi xong, máy nhận ở lần thức dậy tới."}
          </p>
        </aside>
      </div>
    </section>
  );
}

function Row({ title, desc, badge, children }: { title: string; desc?: string; badge?: React.ReactNode; children: React.ReactNode }) {
  return (
    <div className="ik-setrow">
      <div>
        <b>
          {title} {badge}
        </b>
        {desc && <span className="d">{desc}</span>}
      </div>
      <div className="ik-setctl">{children}</div>
    </div>
  );
}

function NotifyKey({ ink }: { ink: Ink }) {
  const [key, setKey] = useState<string | null>(null);
  const [err, setErr] = useState<string | null>(null);
  return (
    <div className="ik-notify">
      <p>
        Gửi một dòng thông báo lên dashboard của máy từ CI, Home Assistant hay hook của Claude Code. Tạo khoá rồi gọi <code>POST {location.origin}/v1/notify</code>.
      </p>
      <button
        type="button"
        className="ik-btn"
        onClick={async () => {
          setErr(null);
          const k = randomHex(24);
          try {
            await ink.saveNotifyKey(await sha256Hex(k));
            setKey(k);
          } catch (e) {
            setErr((e as Error).message);
          }
        }}
      >
        {key ? "Tạo khoá mới (khoá cũ hết hiệu lực)" : "Tạo khoá thông báo"}
      </button>
      {err && <p className="ik-err">{err}</p>}
      {key && (
        <>
          <p className="ik-ok">Khoá chỉ hiện một lần. Hãy lưu lại:</p>
          <pre className="ik-log">{`curl -X POST ${location.origin}/v1/notify \\
  -H "Authorization: Bearer ${key}" \\
  -H "Content-Type: application/json" \\
  -d '{"source":"CI","title":"firmware build thành công"}'`}</pre>
        </>
      )}
    </div>
  );
}


