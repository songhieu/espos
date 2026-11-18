import { useEffect, useRef, useState } from "react";

import { downloadBlob, flashFirmware, loadManifest, pickPort, provisionDevice, serialSupported } from "../flash";
import { ago, randomHex, sha256Hex, shortVersion, type Device, type FirmwareInfo, type Ink } from "../espos";
import { DeviceSession, type WifiNet } from "../serialSession";
import { ESPRESSIF_VID } from "esptool-js";
import { CityPicker, TzSelect } from "../PlacePickers";
import { Icon, Toggle } from "../ui";

export function Devices({ ink, apiBase, me, pairCode }: { ink: Ink; apiBase: string; me: { email: string }; pairCode: string | null }) {
  const [devices, setDevices] = useState<Device[] | null>(null);
  const [firmware, setFirmware] = useState<FirmwareInfo | null>(null);
  const [err, setErr] = useState<string | null>(null);
  const load = () =>
    ink.devicesWithFirmware().then(
      (r) => {
        setDevices(r.devices);
        setFirmware(r.firmware ?? null);
      },
      (e: Error) => setErr(e.message),
    );
  useEffect(() => {
    void load();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  return (
    <section className="ik-page">
      <Installer ink={ink} apiBase={apiBase} me={me} onDone={load} />
      {err && <p className="ik-err">{err}</p>}
      <div className="ik-panel">
        <h3>Máy của bạn</h3>
        {devices?.length === 0 && <p className="ik-muted">Chưa có máy nào. Cắm máy vào máy tính, trang sẽ cài và liên kết ở trên.</p>}
        {devices?.map((d) => (
          <div key={d.id} className="ik-devblock">
            <DeviceRow d={d} ink={ink} onChange={load} />
            <FirmwareBox d={d} latest={firmware} ink={ink} onChange={load} />
          </div>
        ))}
      </div>
      <details className="ik-pairfold" open={!!pairCode}>
        <summary>Không có cáp? Liên kết lại bằng mã hiện trên máy</summary>
        <PairBox ink={ink} code={pairCode} onDone={load} />
      </details>
    </section>
  );
}

function DeviceRow({ d, ink, onChange }: { d: Device; ink: Ink; onChange: () => void }) {
  return (
    <div className="ik-devrow">
      <Icon name="device" size={28} />
      <div>
        <b>{d.name}</b>
        <span>
          Đồng bộ {ago(d.last_sync)}
          {d.battery != null ? ` · pin ${d.battery}%` : ""}
          {d.fw ? ` · ${d.fw}` : ""}
        </span>
      </div>
      <button
        type="button"
        className="ik-btn ghost sm"
        onClick={async () => {
          const name = prompt("Tên mới cho máy", d.name);
          if (name && name.trim()) {
            await ink.updateDevice({ id: d.id, name: name.trim() });
            onChange();
          }
        }}
      >
        Đổi tên
      </button>
      <button
        type="button"
        className="ik-btn ghost sm danger"
        onClick={async () => {
          if (!confirm(`Thu hồi “${d.name}”? Máy sẽ mất liên kết ngay và phải cài/ghép lại.`)) return;
          await ink.updateDevice({ id: d.id, revoked: true });
          onChange();
        }}
      >
        Thu hồi
      </button>
    </div>
  );
}

// Failure reasons from the reader; fw 0.3.5+ adds details after a colon ("download: HTTP -1, 0/5890176 B").
const WHY: Record<string, string> = {
  download: "tải firmware không xong",
  wifi: "máy không vào được WiFi",
  checksum: "file tải về bị lỗi",
  rollback: "bản mới khởi động lỗi nên máy tự quay về bản cũ",
  "image check": "file firmware không hợp lệ",
  "wrong chip": "file không dành cho máy này",
};

const hhmm = (iso?: string) => {
  const t = iso ? Date.parse(iso) : NaN;
  if (Number.isNaN(t)) return "";
  const d = new Date(t);
  const today = new Date().toDateString() === d.toDateString();
  return d.toLocaleTimeString("vi-VN", { hour: "2-digit", minute: "2-digit" }) + (today ? "" : " " + d.toLocaleDateString("vi-VN"));
};

/** Firmware on this reader, the newest one on the server, and the update over WiFi. */
function FirmwareBox({ d, latest, ink, onChange }: { d: Device; latest: FirmwareInfo | null; ink: Ink; onChange: () => void }) {
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState<string | null>(null);
  const running = d.fw ? shortVersion(d.fw) : "";
  const newer = !!latest && (d.fw_code ?? 0) > 0 && latest.code > (d.fw_code ?? 0);
  const requested = (d.fw_req ?? 0) > 0;
  const st = d.ota_state;
  const act = async (b: { action: "update" | "cancel" } | { auto: boolean }) => {
    setBusy(true);
    setErr(null);
    try {
      await ink.firmware(d.id, b);
      onChange();
    } catch (x) {
      setErr((x as Error).message);
    } finally {
      setBusy(false);
    }
  };
  const notes = (latest?.notes ?? "").split("\n").map((s) => s.trim()).filter(Boolean);
  const mb = latest ? (latest.size / 1048576).toLocaleString("vi-VN", { maximumFractionDigits: 1 }) : "";
  return (
    <div className="ik-fw">
      <div className="ik-fw-head">
        <span>
          Firmware <b>{running || "—"}</b>
        </span>
        {requested ? (
          <span className="pill wait">Chờ máy</span>
        ) : newer ? (
          <span className="pill wait">Có bản {shortVersion(latest!.version)}</span>
        ) : running ? (
          <span className="pill ok">Bản mới nhất</span>
        ) : null}
      </div>
      {newer && !requested && (
        <>
          {notes.length > 0 && (
            <div className="ik-fw-notes">
              <h5>
                Có gì mới trong {shortVersion(latest!.version)}
                {latest!.released ? ` (${latest!.released})` : ""}
              </h5>
              <ul>
                {notes.map((n, i) => (
                  <li key={i}>{n}</li>
                ))}
              </ul>
            </div>
          )}
          <div className="ik-fw-act">
            <button type="button" className="ik-btn" disabled={busy} onClick={() => act({ action: "update" })}>
              <Icon name="download" />
              Cập nhật qua WiFi
            </button>
            <span className="ik-muted ik-small">{mb} MB · khoảng 5 phút · sách và cài đặt giữ nguyên</span>
          </div>
        </>
      )}
      {requested && (
        <div className="ik-fw-state wait">
          Máy sẽ cài bản {shortVersion(st?.version ?? latest?.version ?? "")} ở lần đồng bộ tới (mở máy về màn chính là đồng bộ). Cần pin từ 30% trở lên hoặc
          đang sạc.
          <button type="button" className="ik-btn ghost sm" disabled={busy} onClick={() => act({ action: "cancel" })}>
            Huỷ
          </button>
        </div>
      )}
      {!requested && st?.state === "ok" && !newer && st.version && (
        <div className="ik-fw-state ok">
          Đã lên bản {shortVersion(st.version)} {hhmm(st.at) && `lúc ${hhmm(st.at)}`}.
        </div>
      )}
      {!requested && st?.state === "failed" && (
        <div className="ik-fw-state bad">
          Lần cài bản {shortVersion(st.version ?? "")} {hhmm(st.at) && `lúc ${hhmm(st.at)} `}không thành công: <span title={st.why}>{WHY[(st.why ?? "").split(":")[0]] ?? "lỗi khi ghi"}</span>. Máy vẫn chạy bản{" "}
          {running}, không mất gì.
          {newer && (
            <button type="button" className="ik-btn ghost sm" disabled={busy} onClick={() => act({ action: "update" })}>
              Thử lại
            </button>
          )}
        </div>
      )}
      <div className="ik-fw-auto">
        <div>
          <b>Tự cập nhật</b>
          <small>Máy tự cài bản mới khi đang cắm sạc</small>
        </div>
        <Toggle label="Tự cập nhật" on={!!d.fw_auto} onChange={(v) => act({ auto: v })} />
      </div>
      {err && <p className="ik-err">{err}</p>}
    </div>
  );
}

function PairBox({ ink, code, onDone }: { ink: Ink; code: string | null; onDone: () => void }) {
  const [msg, setMsg] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  return (
    <form
      className="ik-panel ik-pair"
      onSubmit={async (e) => {
        e.preventDefault();
        const f = new FormData(e.currentTarget);
        setBusy(true);
        setMsg(null);
        try {
          const r = await ink.confirmPair(String(f.get("code")).replace(/\D/g, ""), String(f.get("name") || ""));
          sessionStorage.removeItem("espos_pair");
          setMsg(`✓ Đã liên kết “${r.device.name}”. Máy sẽ tự đồng bộ trong vài giây.`);
          onDone();
        } catch (x) {
          setMsg((x as Error).message);
        } finally {
          setBusy(false);
        }
      }}
    >
      <div>
        <h3>Ghép bằng mã</h3>
        <p className="ik-muted">Trên máy: Ứng dụng → Liên kết tài khoản. Nhập mã 6 số máy đang hiện.</p>
      </div>
      <label>
        Mã trên máy
        <input name="code" inputMode="numeric" pattern="[0-9 ]{6,7}" required defaultValue={code ?? ""} placeholder="482 913" />
      </label>
      <label>
        Tên máy
        <input name="name" maxLength={40} placeholder="X4 phòng ngủ" />
      </label>
      <button className="ik-btn" disabled={busy} type="submit">
        <Icon name="link" />
        Liên kết
      </button>
      {msg && <p className={msg.startsWith("✓") ? "ik-ok" : "ik-err"}>{msg}</p>}
    </form>
  );
}

type DevState =
  | { kind: "none" }
  | { kind: "asking" }
  | { kind: "espos"; fw: string; linked: boolean }
  | { kind: "stock" };
type WifiTry = { ssid: string; status: "testing" | "ok" | "fail"; msg: string };
type Step = "connect" | "firmware" | "wifi" | "link" | "done";

const STEP_INFO: { id: Step; title: string }[] = [
  { id: "connect", title: "Kết nối máy" },
  { id: "firmware", title: "Cài firmware EspOS" },
  { id: "wifi", title: "WiFi" },
  { id: "link", title: "Liên kết" },
];

/**
 * USB setup as a wizard: connect → (stock firmware) install EspOS → the device reboots and
 * the page asks it again → WiFi from the device's own scan → link to the account. Every step
 * starts by itself when the previous one ends; nothing is typed that the device can tell us.
 */
function Installer({ ink, apiBase, me, onDone }: { ink: Ink; apiBase: string; me: { email: string }; onDone: () => void }) {
  const [port, setPort] = useState<SerialPort | null>(null);
  const [dev, setDev] = useState<DevState>({ kind: "none" });
  const [step, setStep] = useState<Step>("connect");
  const [log, setLog] = useState<string[]>([]);
  const [stage, setStage] = useState<{ name: string; pct: number } | null>(null);
  const [busy, setBusy] = useState(false);
  const [backup, setBackup] = useState(true);
  const [nets, setNets] = useState<WifiNet[] | null>(null);
  const [scanning, setScanning] = useState(false);
  const [picked, setPicked] = useState<string | null>(null);
  const [manual, setManual] = useState(false);
  const [wifiTry, setWifiTry] = useState<WifiTry | null>(null);
  const [wifi, setWifi] = useState<{ ssid: string; pass: string } | null>(null);
  const [form, setForm] = useState({ name: "X4 của tôi", lang: "vi", size: "medium", dash: "1", city: "Hà Nội", lat: 21.0285, lon: 105.8542, tz: "420" });
  const serial = useRef(false);
  const logBox = useRef<HTMLDivElement>(null);
  const add = (line: string) => setLog((l) => [...l, line]);

  // Follow the newest line, smoothly.
  useEffect(() => {
    const el = logBox.current;
    if (el) el.scrollTo({ top: el.scrollHeight, behavior: "smooth" });
  }, [log]);

  // A port granted before is opened on its own: plug in, open the page, it starts.
  useEffect(() => {
    if (!serialSupported()) return;
    void navigator.serial.getPorts().then((ps) => {
      const p = ps.find((x) => x.getInfo().usbVendorId === ESPRESSIF_VID);
      if (p) {
        setPort(p);
        add("✓ Đã thấy máy đang cắm");
        void probe(p);
      }
    });
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  if (!serialSupported()) {
    return (
      <div className="ik-panel">
        <h3>Cài EspOS lên Xteink X4</h3>
        <div className="ik-warn">
          <Icon name="alert" />
          <span>Trình duyệt này không cài qua USB được. Hãy mở trang này bằng Chrome hoặc Edge trên máy tính.</span>
        </div>
      </div>
    );
  }

  /** Asks the device what it runs; an EspOS device also lists the WiFi it sees. Picks the next step. */
  async function probe(p: SerialPort, helloMs = 12000) {
    if (serial.current) return;
    serial.current = true;
    setDev({ kind: "asking" });
    let session: DeviceSession | null = null;
    try {
      session = await DeviceSession.open(p);
      let ready: string;
      try {
        ready = await session.hello(helloMs);
      } catch {
        setDev({ kind: "stock" });
        setStep("firmware");
        add("• Máy đang chạy firmware khác (chưa có EspOS). Bước kế tiếp: cài firmware.");
        return;
      }
      let info = { fw: "", linked: false };
      try {
        info = { ...info, ...JSON.parse(ready.slice(ready.indexOf("{"))) };
      } catch {
        /* older firmware */
      }
      setDev({ kind: "espos", fw: info.fw, linked: info.linked });
      add(`✓ Máy chạy ${info.fw || "EspOS"}${info.linked ? ", đã liên kết tài khoản" : ", chưa liên kết"}. Máy đã có giờ.`);
      setStep(info.linked ? "done" : "wifi");
      setScanning(true);
      const found = await session.scanWifi();
      setNets(found);
      add(found.length ? `✓ Máy thấy ${found.length} mạng WiFi.` : "✗ Máy không thấy mạng WiFi nào. Để máy gần bộ phát rồi bấm Quét lại.");
    } catch (e) {
      add("✗ " + (e as Error).message);
      setDev({ kind: "none" });
    } finally {
      setScanning(false);
      await session?.close();
      serial.current = false;
    }
  }

  async function connect(any = false) {
    try {
      const p = await pickPort(any);
      setPort(p);
      add("✓ Đã chọn máy");
      void probe(p);
    } catch (e) {
      if ((e as Error).name !== "NotFoundError") add("✗ " + (e as Error).message);
    }
  }

  async function installFirmware() {
    if (!port || busy) return;
    setBusy(true);
    try {
      const manifest = await loadManifest();
      const saved = await flashFirmware(port, manifest, {
        backup,
        log: add,
        progress: (s, f) => setStage({ name: s === "backup" ? "Sao lưu firmware gốc" : "Ghi EspOS " + manifest.version, pct: f }),
      });
      if (saved) {
        downloadBlob(saved, `x4-firmware-goc-${new Date().toISOString().slice(0, 10)}.bin`);
        add("✓ Đã tải file sao lưu firmware gốc về máy tính. Hãy giữ file này.");
      }
      setStage({ name: "Máy đang khởi động EspOS", pct: 1 });
      add("… Máy đang khởi động lại vào EspOS");
      setBusy(false);
      // Give it time to boot, then ask again: it now answers and scans WiFi.
      await new Promise((r) => setTimeout(r, 4000));
      setStage(null);
      await probe(port, 40000);
    } catch (e) {
      add("✗ " + (e as Error).message);
      setStage(null);
      setBusy(false);
    }
  }

  /** The device joins the network now and saves it; then linking starts by itself. */
  async function joinWifi(ssid: string, pass: string) {
    if (!port || serial.current) return;
    serial.current = true;
    setWifiTry({ ssid, status: "testing", msg: "Máy đang kết nối…" });
    let session: DeviceSession | null = null;
    let ok = false;
    try {
      session = await DeviceSession.open(port);
      await session.hello(30000);
      const why = await session.testWifi(ssid, pass);
      if (why) {
        setWifiTry({ ssid, status: "fail", msg: why });
        add(`✗ “${ssid}”: ${why}`);
      } else {
        await session.saveWifi(ssid, pass);
        setWifiTry({ ssid, status: "ok", msg: "Đã kết nối" });
        setWifi({ ssid, pass });
        add(`✓ Máy vào được “${ssid}” và đã lưu mạng này.`);
        ok = true;
      }
    } catch (e) {
      setWifiTry({ ssid, status: "fail", msg: (e as Error).message });
    } finally {
      await session?.close();
      serial.current = false;
    }
    if (ok && dev.kind === "espos" && !dev.linked) setStep("link");
  }

  /** Links over the cable: a device token, the settings, and the WiFi chosen above. */
  async function link() {
    if (!port || busy) return;
    setBusy(true);
    try {
      setStage({ name: "Liên kết tài khoản", pct: 0.2 });
      const token = randomHex(32);
      const created = await ink.createDevice({
        name: form.name.trim() || "Xteink X4",
        token_hash: await sha256Hex(token),
        settings: {
          "sys.lang": form.lang,
          "read.size": form.size,
          "dash.enabled": form.dash,
          "dash.city": form.city,
          "dash.lat": String(form.lat),
          "dash.lon": String(form.lon),
          "sys.tz_min": form.tz,
        },
      });
      add(`✓ Đã thêm “${created.device.name}” vào tài khoản ${me.email}`);
      await provisionDevice(
        port,
        {
          api: apiBase.replace(/\/+$/, "") || location.origin,
          token,
          device: { id: created.device.id, name: created.device.name, user: created.device.user || me.email },
          wifi: wifi ? [wifi] : [],
          settings: created.settings,
          settings_rev: created.settings_rev,
        },
        add,
        (_s, f) => setStage({ name: "Gửi cài đặt xuống máy", pct: 0.2 + 0.8 * f }),
      );
      setStage(null);
      add("✓ Xong! Máy hiện màn chào mừng rồi tự đồng bộ lần đầu.");
      setDev({ kind: "espos", fw: dev.kind === "espos" ? dev.fw : "", linked: true });
      setStep("done");
      onDone();
    } catch (e) {
      add("✗ " + (e as Error).message);
      setStage(null);
    } finally {
      setBusy(false);
    }
  }

  const order: Step[] = ["connect", "firmware", "wifi", "link", "done"];
  const at = order.indexOf(step);
  const stepState = (id: Step) => {
    if (id === "firmware" && dev.kind === "espos" && at > 0) return "ok";
    const i = order.indexOf(id);
    return i < at ? "ok" : i === at ? "now" : "";
  };
  const pill = (ssid: string) =>
    wifiTry?.ssid === ssid ? <span className={"pill " + (wifiTry.status === "ok" ? "ok" : wifiTry.status === "fail" ? "bad" : "wait")}>{wifiTry.msg}</span> : null;

  return (
    <div className="ik-installer">
      <header className="ik-head">
        <div>
          <h2>Cài EspOS lên Xteink X4</h2>
          <p>Cắm máy vào máy tính bằng cáp USB-C có truyền dữ liệu và bật máy. Trang tự làm từng bước. Dùng Chrome hoặc Edge.</p>
        </div>
      </header>
      <div className="ik-steps">
        {STEP_INFO.map((s, i) => (
          <div key={s.id} className={"ik-step " + stepState(s.id)}>
            <span className="n">{stepState(s.id) === "ok" ? "✓" : i + 1}</span>
            <div>
              <b>{s.title}</b>
              {s.id === "connect" && (dev.kind === "asking" ? "Đang hỏi máy…" : dev.kind === "espos" ? `Máy chạy ${dev.fw || "EspOS"}` : dev.kind === "stock" ? "Máy chạy firmware khác" : "Cắm cáp, bật máy")}
              {s.id === "firmware" && (dev.kind === "espos" ? "Đã có EspOS" : stage ? `${stage.name}…` : "Sao lưu, ghi firmware")}
              {s.id === "wifi" && (wifi ? `Đã vào “${wifi.ssid}”` : "Máy tự quét, bạn chọn mạng")}
              {s.id === "link" && (step === "done" ? "Hoàn tất" : "Thêm vào tài khoản")}
            </div>
          </div>
        ))}
      </div>
      <div className="ik-two">
        <div className="ik-panel ik-wizard">
          {step === "connect" && (
            <>
              <h3>Kết nối máy</h3>
              {dev.kind === "asking" ? (
                <p className="ik-muted">Đang hỏi máy… (máy có thể khởi động lại một lần)</p>
              ) : (
                <>
                  <p className="ik-muted">Lần đầu, Chrome hỏi bạn chọn máy: chọn mục có chữ USB JTAG hoặc ESP32. Từ lần sau trang tự nhận.</p>
                  <div className="ik-actions">
                    <button type="button" className="ik-btn" onClick={() => void connect()}>
                      <Icon name="usb" />
                      Kết nối máy
                    </button>
                    <button type="button" className="ik-link" onClick={() => void connect(true)}>
                      Không thấy máy? Hiện mọi cổng
                    </button>
                  </div>
                </>
              )}
            </>
          )}

          {step === "firmware" && (
            <>
              <h3>Cài firmware EspOS</h3>
              <p className="ik-muted">Máy đang chạy firmware khác. Cài EspOS trước, máy sẽ tự khởi động lại; sau đó trang tự chuyển sang bước WiFi.</p>
              <label className="ik-check">
                <input type="checkbox" checked={backup} onChange={(e) => setBackup(e.target.checked)} disabled={busy} />
                Sao lưu firmware đang có về máy tính trước khi ghi (khuyên dùng, thêm khoảng 3 phút)
              </label>
              {stage && (
                <>
                  <div className="ik-progline">
                    <span>{stage.name}</span>
                    <b>{Math.round(stage.pct * 100)}%</b>
                  </div>
                  <progress className="ik-prog" max={1} value={stage.pct} />
                </>
              )}
              <div className="ik-actions">
                <button type="button" className="ik-btn" disabled={busy || dev.kind === "asking"} onClick={() => void installFirmware()}>
                  {busy ? "Đang cài… đừng rút cáp" : "Cài EspOS"}
                </button>
              </div>
            </>
          )}

          {(step === "wifi" || (step === "done" && dev.kind === "espos")) && (
            <div className="ik-wifibox">
              <div className="ik-labelrow">
                <h3>{step === "done" ? "WiFi của máy" : "Chọn WiFi cho máy"}</h3>
                {port && !scanning && (
                  <button type="button" className="ik-link" onClick={() => void probe(port)}>
                    Quét lại
                  </button>
                )}
              </div>
              {step === "done" && <p className="ik-ok">✓ Máy đã liên kết với tài khoản. Muốn thêm mạng WiFi thì chọn ở dưới.</p>}
              {scanning && <p className="ik-muted">Máy đang quét WiFi…</p>}
              {nets && nets.length > 0 && (
                <ul className="ik-netlist">
                  {nets.map((n) => (
                    <li key={n.ssid} className={picked === n.ssid ? "on" : ""}>
                      <button type="button" className="net" onClick={() => setPicked(picked === n.ssid ? null : n.ssid)}>
                        <Bars rssi={n.rssi} />
                        <b>{n.ssid}</b>
                        {pill(n.ssid)}
                      </button>
                      {picked === n.ssid && (
                        <form
                          className="ik-netform"
                          onSubmit={(e) => {
                            e.preventDefault();
                            void joinWifi(n.ssid, String(new FormData(e.currentTarget).get("pass")));
                          }}
                        >
                          <input name="pass" type="password" autoFocus placeholder="Mật khẩu WiFi" autoComplete="off" aria-label={`Mật khẩu ${n.ssid}`} />
                          <button className="ik-btn sm" type="submit" disabled={wifiTry?.status === "testing"}>
                            {wifiTry?.status === "testing" && wifiTry.ssid === n.ssid ? "Đang kết nối…" : "Kết nối"}
                          </button>
                        </form>
                      )}
                    </li>
                  ))}
                </ul>
              )}
              {(manual || (nets && nets.length === 0)) && (
                <form
                  className="ik-netform manual"
                  onSubmit={(e) => {
                    e.preventDefault();
                    const f = new FormData(e.currentTarget);
                    void joinWifi(String(f.get("ssid")).trim(), String(f.get("pass")));
                  }}
                >
                  <input name="ssid" placeholder="Tên WiFi (phân biệt hoa thường)" autoComplete="off" aria-label="Tên WiFi" />
                  <input name="pass" type="password" placeholder="Mật khẩu" autoComplete="off" aria-label="Mật khẩu WiFi" />
                  <button className="ik-btn sm" type="submit">
                    Kết nối
                  </button>
                </form>
              )}
              <div className="ik-actions">
                {nets && nets.length > 0 && !manual && (
                  <button type="button" className="ik-link" onClick={() => setManual(true)}>
                    Mạng ẩn? Nhập tên mạng
                  </button>
                )}
                {step === "wifi" && (
                  <button type="button" className="ik-link" onClick={() => setStep("link")}>
                    Bỏ qua, máy đã có WiFi
                  </button>
                )}
              </div>
              <span className="ik-hint">Chỉ WiFi 2.4 GHz. Mật khẩu đi thẳng xuống máy qua cáp, không gửi lên server.</span>
            </div>
          )}

          {step === "link" && (
            <>
              <h3>Liên kết vào tài khoản</h3>
              <p className="ik-muted">Máy được thêm vào tài khoản {me.email} ngay qua cáp. Các mục dưới sửa lại được sau, trên web hoặc trên máy.</p>
              <div className="ik-form">
                <label>
                  Tên máy
                  <input value={form.name} onChange={(e) => setForm((f) => ({ ...f, name: e.target.value }))} maxLength={40} />
                </label>
                <label>
                  Ngôn ngữ
                  <select value={form.lang} onChange={(e) => setForm((f) => ({ ...f, lang: e.target.value }))}>
                    <option value="vi">Tiếng Việt</option>
                    <option value="en">English</option>
                  </select>
                </label>
                <label>
                  Thành phố (thời tiết)
                  <CityPicker value={form.city} onChange={(c) => setForm((f) => ({ ...f, city: c.name, lat: c.lat, lon: c.lon }))} />
                </label>
                <label>
                  Múi giờ
                  <TzSelect value={form.tz} onChange={(v) => setForm((f) => ({ ...f, tz: v }))} />
                </label>
                <label>
                  Cỡ chữ
                  <select value={form.size} onChange={(e) => setForm((f) => ({ ...f, size: e.target.value }))}>
                    <option value="small">Nhỏ</option>
                    <option value="medium">Vừa</option>
                    <option value="large">Lớn</option>
                    <option value="xlarge">Rất lớn</option>
                  </select>
                </label>
                <label>
                  Màn hình khi ngủ
                  <select value={form.dash} onChange={(e) => setForm((f) => ({ ...f, dash: e.target.value }))}>
                    <option value="1">EspOS (dashboard, ảnh, bìa sách)</option>
                    <option value="0">Như CrossPoint</option>
                  </select>
                </label>
              </div>
              {stage && (
                <>
                  <div className="ik-progline">
                    <span>{stage.name}</span>
                    <b>{Math.round(stage.pct * 100)}%</b>
                  </div>
                  <progress className="ik-prog" max={1} value={stage.pct} />
                </>
              )}
              <div className="ik-actions">
                <button type="button" className="ik-btn" disabled={busy} onClick={() => void link()}>
                  <Icon name="link" />
                  {busy ? "Đang liên kết…" : "Hoàn tất"}
                </button>
                {!wifi && (
                  <button type="button" className="ik-link" onClick={() => setStep("wifi")}>
                    Quay lại chọn WiFi
                  </button>
                )}
              </div>
            </>
          )}
        </div>
        <div className="ik-panel">
          <h3>Tiến trình</h3>
          <div className="ik-log" aria-live="polite" ref={logBox}>
            {log.length === 0 ? (
              <span className="ik-muted">Chưa bắt đầu.</span>
            ) : (
              log.map((l, i) => (
                <div key={i} className={"ln " + lineKind(l) + (i === log.length - 1 ? " last" : "")}>
                  {l}
                </div>
              ))
            )}
          </div>
          {step === "done" && dev.kind === "espos" && (
            <div className="ik-actions">
              <button type="button" className="ik-link" onClick={() => setStep("firmware")}>
                Cài lại firmware EspOS
              </button>
            </div>
          )}
          <div className="ik-warn">
            <Icon name="alert" />
            <span>Không rút cáp trong lúc cài. Máy bán ngoài Trung Quốc có thể bị khoá nạp qua USB; khi đó cài firmware bằng thẻ SD rồi liên kết bằng mã ở cuối trang.</span>
          </div>
        </div>
      </div>
    </div>
  );
}

/** Colour for a log line: done, failed, waiting, note, or the flasher's own technical output. */
function lineKind(l: string): string {
  if (l.startsWith("✓")) return "ok";
  if (l.startsWith("✗")) return "err";
  if (l.startsWith("…")) return "wait";
  if (l.startsWith("•") || l.startsWith("!")) return "note";
  return "raw";
}

/** Signal strength as four bars. */
function Bars({ rssi }: { rssi: number }) {
  const n = rssi >= -55 ? 4 : rssi >= -67 ? 3 : rssi >= -78 ? 2 : 1;
  return (
    <span className="ik-signal" aria-label={`Sóng ${n}/4`}>
      {[1, 2, 3, 4].map((i) => (
        <i key={i} className={i <= n ? "on" : ""} style={{ height: 3 + i * 3 }} />
      ))}
    </span>
  );
}
