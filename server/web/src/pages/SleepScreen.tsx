// Settings → Sleep screen (mode cards, my pictures with the in-browser converter) and Settings → WiFi.
import { useEffect, useRef, useState } from "react";

import type { Ink, Wallpaper, WifiOp, WifiSaved } from "../inkdesk";
import { Icon, Seg } from "../ui";
import { dither, drawBits, loadImage, panelSize, thumbDataUrl, toBmp, type Crop } from "../wallpaper";

export type SleepMode = "dashboard" | "images" | "cover" | "auto";
export const normMode = (v: string): SleepMode => (v === "images" || v === "cover" || v === "auto" ? v : "dashboard");

const MODES: { id: SleepMode; label: string; desc: string; thumb: React.ReactNode }[] = [
  {
    id: "dashboard",
    label: "Dashboard",
    desc: "Giờ, thời tiết, lịch, việc",
    thumb: (
      <span className="ik-mthumb" aria-hidden="true">
        <i style={{ left: 8, top: 9, width: 44, height: 16 }} />
        <i style={{ left: 8, top: 31, width: 46, height: 3 }} />
        <i style={{ left: 8, top: 39, width: 34, height: 3 }} />
        <i style={{ left: 62, top: 7, width: 2, height: 42 }} />
        <i style={{ left: 70, top: 10, width: 44, height: 3 }} />
        <i style={{ left: 70, top: 18, width: 44, height: 6 }} />
        <i style={{ left: 70, top: 30, width: 36, height: 3 }} />
        <i style={{ left: 0, bottom: 0, width: "100%", height: 7 }} />
      </span>
    ),
  },
  {
    id: "images",
    label: "Ảnh của tôi",
    desc: "Ảnh bạn tải lên, tự đổi",
    thumb: (
      <span className="ik-mthumb pics" aria-hidden="true">
        <b />
        <b className="b2" />
        <b className="b3" />
      </span>
    ),
  },
  {
    id: "cover",
    label: "Bìa sách đang đọc",
    desc: "Bìa cuốn đang đọc dở",
    thumb: (
      <span className="ik-mthumb center" aria-hidden="true">
        <em>SÁCH</em>
      </span>
    ),
  },
  {
    id: "auto",
    label: "Tự động",
    desc: "Đang đọc dở thì hiện bìa, không thì dashboard",
    thumb: (
      <span className="ik-mthumb split" aria-hidden="true">
        <em>S</em>
        <i style={{ left: 70, top: 10, width: 36, height: 12 }} />
        <i style={{ left: 70, top: 28, width: 40, height: 3 }} />
        <i style={{ left: 70, top: 36, width: 28, height: 3 }} />
      </span>
    ),
  },
];

export function ModePicker({ value, onChange }: { value: SleepMode; onChange: (v: string) => void }) {
  return (
    <div className="ik-modes" role="group" aria-label="Kiểu màn chờ">
      {MODES.map((m) => (
        <button key={m.id} type="button" aria-pressed={value === m.id} onClick={() => onChange(m.id)}>
          {m.thumb}
          <b>{m.label}</b>
          <span>{m.desc}</span>
        </button>
      ))}
    </div>
  );
}

function put(url: string, blob: Blob): Promise<void> {
  return fetch(url, { method: "PUT", headers: { "Content-Type": "image/bmp" }, body: blob }).then((r) => {
    if (!r.ok) throw new Error(`Tải lên lỗi ${r.status}`);
  });
}

/** My pictures: list, delete, and the converter dialog for new ones. */
export function Wallpapers({ ink, onList }: { ink: Ink; onList?: (w: Wallpaper[]) => void }) {
  const [list, setList] = useState<Wallpaper[] | null>(null);
  const [err, setErr] = useState<string | null>(null);
  const [edit, setEdit] = useState<HTMLImageElement | null>(null);
  const input = useRef<HTMLInputElement>(null);
  const load = () =>
    ink.wallpapers().then(
      (w) => {
        setList(w);
        onList?.(w);
      },
      (e: Error) => setErr(e.message),
    );
  useEffect(() => {
    void load();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  return (
    <div className="ik-walls">
      <p className="ik-label">Ảnh của tôi {list ? `· ${list.length}/20` : ""}</p>
      {err && <p className="ik-err">{err}</p>}
      <div className="ik-pics">
        {(list ?? []).map((w) => (
          <figure key={w.id} className={"ik-pic " + w.orient}>
            <img src={w.thumb} alt="" />
            <button
              type="button"
              className="x"
              aria-label="Xoá ảnh"
              onClick={async () => {
                await ink.deleteWall(w.id).catch((e: Error) => setErr(e.message));
                void load();
              }}
            >
              <Icon name="trash" size={14} />
            </button>
          </figure>
        ))}
        {(list?.length ?? 0) < 20 && (
          <button type="button" className="ik-pic add" onClick={() => input.current?.click()}>
            <b>+</b>
            Tải ảnh lên
            <small>JPG, PNG</small>
          </button>
        )}
      </div>
      <input
        ref={input}
        type="file"
        accept="image/*"
        hidden
        onChange={async (e) => {
          const f = e.target.files?.[0];
          e.target.value = "";
          if (!f) return;
          try {
            setEdit(await loadImage(f));
          } catch (x) {
            setErr((x as Error).message);
          }
        }}
      />
      {edit && (
        <WallEditor
          img={edit}
          onClose={() => setEdit(null)}
          onSave={async (crop) => {
            const d = dither(edit, crop);
            const blob = toBmp(d);
            const start = await ink.startWallUpload({ orient: crop.orient, size: blob.size, thumb: thumbDataUrl(d) });
            await put(start.url, blob);
            await ink.confirmWall(start.id);
            setEdit(null);
            void load();
          }}
        />
      )}
    </div>
  );
}

function WallEditor({ img, onClose, onSave }: { img: HTMLImageElement; onClose: () => void; onSave: (c: Crop) => Promise<void> }) {
  const [crop, setCrop] = useState<Crop>({ orient: img.width > img.height ? "l" : "p", fit: "cover", panX: 0, panY: 0 });
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState<string | null>(null);
  const canvas = useRef<HTMLCanvasElement>(null);
  const drag = useRef<{ x: number; y: number; px: number; py: number } | null>(null);

  useEffect(() => {
    if (canvas.current) drawBits(canvas.current, dither(img, crop));
  }, [img, crop]);

  const { w, h } = panelSize(crop.orient);
  const scale = crop.orient === "p" ? 0.5 : 0.5;
  return (
    <div className="ik-modal" role="dialog" aria-modal="true" aria-label="Ảnh màn chờ" onClick={onClose}>
      <div className="ik-dialog ik-walled" onClick={(e) => e.stopPropagation()}>
        <h3>Ảnh màn chờ</h3>
        <p className="ik-muted">Kéo ảnh để chọn phần giữ lại. Đây đúng là thứ máy sẽ hiện: đen trắng có chấm, khổ {w}×{h}.</p>
        <div className="ik-walled-row">
          <canvas
            ref={canvas}
            className="ik-wallcanvas"
            style={{ width: w * scale, height: h * scale, cursor: crop.fit === "cover" ? "grab" : "default" }}
            onPointerDown={(e) => {
              (e.target as HTMLElement).setPointerCapture(e.pointerId);
              drag.current = { x: e.clientX, y: e.clientY, px: crop.panX, py: crop.panY };
            }}
            onPointerMove={(e) => {
              if (!drag.current || crop.fit !== "cover") return;
              const k = 2 / 160;
              const nx = Math.max(-1, Math.min(1, drag.current.px - (e.clientX - drag.current.x) * k));
              const ny = Math.max(-1, Math.min(1, drag.current.py - (e.clientY - drag.current.y) * k));
              setCrop((c) => ({ ...c, panX: nx, panY: ny }));
            }}
            onPointerUp={() => (drag.current = null)}
          />
          <div className="ik-walled-ctl">
            <label>
              Khổ
              <Seg label="Khổ" value={crop.orient} onChange={(o) => setCrop((c) => ({ ...c, orient: o, panX: 0, panY: 0 }))} options={[{ v: "p", label: "Dọc" }, { v: "l", label: "Ngang" }]} />
            </label>
            <label>
              Cách đặt
              <Seg label="Cách đặt" value={crop.fit} onChange={(f) => setCrop((c) => ({ ...c, fit: f, panX: 0, panY: 0 }))} options={[{ v: "cover", label: "Lấp đầy" }, { v: "contain", label: "Vừa khung" }]} />
            </label>
            {err && <p className="ik-err">{err}</p>}
            <div className="ik-dialog-actions">
              <button type="button" className="ik-btn ghost" onClick={onClose}>
                Huỷ
              </button>
              <button
                type="button"
                className="ik-btn"
                disabled={busy}
                onClick={async () => {
                  setBusy(true);
                  setErr(null);
                  try {
                    await onSave(crop);
                  } catch (x) {
                    setErr((x as Error).message);
                    setBusy(false);
                  }
                }}
              >
                {busy ? "Đang lưu…" : "Lưu cho máy"}
              </button>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}

/** Preview of the sleep screen for the picture modes. */
export function PicturePreview({ mode, walls, clock }: { mode: SleepMode; walls: Wallpaper[]; clock: boolean }) {
  const first = walls[0];
  const land = mode === "images" && first?.orient === "l";
  const now = new Date();
  const hhmm = `${String(now.getHours()).padStart(2, "0")}:${String(now.getMinutes()).padStart(2, "0")}`;
  return (
    <div className={"ik-picprev " + (land ? "l" : "p")} aria-label="Xem trước màn chờ">
      {mode === "images" ? first ? <img src={first.thumb} alt="" /> : <span className="empty">Chưa có ảnh nào</span> : <span className="cover">Bìa sách bạn đang đọc trên máy</span>}
      {mode === "images" && clock && (
        <span className="strip">
          <span>{hhmm} · hôm nay</span>
          <span>82%</span>
        </span>
      )}
    </div>
  );
}

function savedLine(s: WifiSaved): { cls: string; text: string } {
  if (s.ok) return { cls: "ok", text: s.age_s < 3600 ? "Đang dùng" : "Vào được lần gần nhất" };
  if (s.code === 1) return { cls: "", text: "Đã lưu · không thấy mạng lúc thử" };
  if (s.code === 4) return { cls: "bad", text: "Máy thử không vào được: sai mật khẩu" };
  if (s.code === 0) return { cls: "", text: "Đã lưu · chưa thử" };
  return { cls: "bad", text: "Máy thử không vào được" };
}

export function WifiPanel({ ink, dev }: { ink: Ink; dev: number }) {
  const [saved, setSaved] = useState<WifiSaved[] | null>(null);
  const [ops, setOps] = useState<WifiOp[]>([]);
  const [err, setErr] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  const load = () =>
    ink.wifi(dev).then(
      (r) => {
        setSaved(r.saved ?? []);
        setOps(r.ops ?? []);
      },
      (e: Error) => setErr(e.message),
    );
  useEffect(() => {
    void load();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [dev]);

  const pendingFor = (ssid: string) => ops.find((o) => o.ssid === ssid);
  return (
    <div className="ik-wifi">
      <p className="ik-label">Mạng máy đã lưu</p>
      {err && <p className="ik-err">{err}</p>}
      {!saved ? (
        <p className="ik-muted">Đang tải…</p>
      ) : (
        <div className="ik-wifilist">
          {saved.length === 0 && ops.length === 0 && <p className="ik-muted">Máy chưa báo mạng nào. Danh sách hiện sau lần đồng bộ tới với firmware mới.</p>}
          {saved.map((s) => {
            const p = pendingFor(s.ssid);
            const line = p?.op === "remove" ? { cls: "wait", text: "Chờ máy xoá ở lần đồng bộ tới" } : savedLine(s);
            return (
              <div key={s.ssid} className="ik-wifirow">
                <b>{s.ssid}</b>
                <span className={"pill " + line.cls}>{line.text}</span>
                {p?.op === "remove" ? (
                  <button type="button" className="ik-link" onClick={() => void ink.saveWifi(dev, { op: "cancel", id: p.id }).then(load)}>
                    Huỷ
                  </button>
                ) : (
                  <button type="button" className="ik-link" onClick={() => void ink.saveWifi(dev, { op: "remove", ssid: s.ssid }).then(load, (e: Error) => setErr(e.message))}>
                    Xoá
                  </button>
                )}
              </div>
            );
          })}
          {ops
            .filter((o) => o.op === "add")
            .map((o) => (
              <div key={o.id} className="ik-wifirow">
                <b>{o.ssid}</b>
                <span className={"pill " + (o.status === "failed" ? "bad" : "wait")}>{o.status === "failed" ? `Máy không lưu được: ${o.result || "lỗi"}` : "Chờ máy nhận ở lần đồng bộ tới"}</span>
                <button type="button" className="ik-link" onClick={() => void ink.saveWifi(dev, { op: "cancel", id: o.id }).then(load)}>
                  Huỷ
                </button>
              </div>
            ))}
        </div>
      )}
      <form
        className="ik-wifiform"
        onSubmit={async (e) => {
          e.preventDefault();
          const f = new FormData(e.currentTarget);
          const form = e.currentTarget;
          setBusy(true);
          setErr(null);
          try {
            await ink.saveWifi(dev, { op: "add", ssid: String(f.get("ssid")).trim(), pass: String(f.get("pass")) });
            form.reset();
            await load();
          } catch (x) {
            setErr((x as Error).message);
          } finally {
            setBusy(false);
          }
        }}
      >
        <label>
          Tên WiFi
          <input name="ssid" required maxLength={32} autoComplete="off" placeholder="Phân biệt chữ hoa, chữ thường" />
        </label>
        <label>
          Mật khẩu
          <input name="pass" type="password" maxLength={63} autoComplete="new-password" />
        </label>
        <button className="ik-btn" type="submit" disabled={busy}>
          {busy ? "Đang thêm…" : "Thêm cho máy"}
        </button>
      </form>
      <p className="ik-note">Mật khẩu chỉ nằm trên server cho tới khi máy nhận xong, sau đó server xoá. Web chỉ giữ tên mạng và kết quả máy báo lại. Máy chỉ dùng được WiFi 2.4 GHz.</p>
    </div>
  );
}
