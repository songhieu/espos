// The morning paper: VnExpress sections made into an EPUB each morning and synced to the X4.
import { useEffect, useRef, useState } from "react";

import { ago, bytes, type Ink, type NewsIssue, type NewsPrefs } from "../inkdesk";
import { Icon, Seg, Toggle } from "../ui";

const SECTIONS: { id: string; label: string }[] = [
  { id: "tin-moi-nhat", label: "Tin mới nhất" },
  { id: "tin-noi-bat", label: "Tin nổi bật" },
  { id: "thoi-su", label: "Thời sự" },
  { id: "the-gioi", label: "Thế giới" },
  { id: "kinh-doanh", label: "Kinh doanh" },
  { id: "bat-dong-san", label: "Bất động sản" },
  { id: "khoa-hoc-cong-nghe", label: "Khoa học công nghệ" },
  { id: "suc-khoe", label: "Sức khỏe" },
  { id: "giao-duc", label: "Giáo dục" },
  { id: "phap-luat", label: "Pháp luật" },
  { id: "gia-dinh", label: "Đời sống" },
  { id: "du-lich", label: "Du lịch" },
  { id: "giai-tri", label: "Giải trí" },
  { id: "the-thao", label: "Thể thao" },
  { id: "oto-xe-may", label: "Xe" },
  { id: "y-kien", label: "Ý kiến" },
];

export function News({ ink }: { ink: Ink }) {
  const [prefs, setPrefs] = useState<NewsPrefs | null>(null);
  const [issues, setIssues] = useState<NewsIssue[]>([]);
  const [dirty, setDirty] = useState(false);
  const [msg, setMsg] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  const [making, setMaking] = useState(false);
  const poll = useRef<number | null>(null);

  const load = () =>
    ink.news().then(
      (r) => {
        setPrefs((p) => (dirty && p ? p : r.prefs));
        setIssues(r.issues);
        return r;
      },
      (e: Error) => {
        setMsg(e.message);
        return null;
      },
    );
  useEffect(() => {
    void load();
    return () => {
      if (poll.current) window.clearInterval(poll.current);
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  if (!prefs) return <section className="ik-page">{msg ? <p className="ik-err">{msg}</p> : <p className="ik-muted">Đang tải…</p>}</section>;

  const picked = prefs.sections.split(",").filter(Boolean);
  const edit = (p: Partial<NewsPrefs>) => {
    setPrefs({ ...prefs, ...p });
    setDirty(true);
  };

  async function save() {
    if (!prefs) return;
    setBusy(true);
    setMsg(null);
    try {
      await ink.saveNews(prefs);
      setDirty(false);
      setMsg("✓ Đã lưu. Số tới làm lúc " + prefs.hour + ":00.");
    } catch (e) {
      setMsg((e as Error).message);
    } finally {
      setBusy(false);
    }
  }

  async function makeNow() {
    setMaking(true);
    setMsg(null);
    try {
      if (dirty) await save();
      const before = issues[0]?.id ?? 0;
      await ink.makeNews(String(Date.now()));
      let tries = 0;
      poll.current = window.setInterval(async () => {
        tries++;
        const r = await load();
        if ((r && r.issues[0] && r.issues[0].id !== before) || tries > 40) {
          if (poll.current) window.clearInterval(poll.current);
          setMaking(false);
          if (r && r.issues[0]?.id !== before) setMsg("✓ Xong số mới. Máy nhận ở lần đồng bộ tới.");
          else setMsg("Chưa xong, có thể lâu hơn bình thường. Tải lại trang sau ít phút.");
        }
      }, 6000);
    } catch (e) {
      setMsg((e as Error).message);
      setMaking(false);
    }
  }

  return (
    <section className="ik-page">
      <header className="ik-head">
        <div>
          <h2>Báo sáng</h2>
          <p>Tin VnExpress đóng thành sách, có ảnh, đọc trên máy như một cuốn sách. Mỗi sáng làm số mới, máy tự tải về.</p>
        </div>
        <div className="ik-headact">
          <button type="button" className="ik-btn ghost" disabled={making} onClick={() => void makeNow()}>
            <Icon name="sync" />
            {making ? "Đang làm số mới…" : "Làm số mới ngay"}
          </button>
          <button type="button" className="ik-btn" disabled={!dirty || busy} onClick={() => void save()}>
            {busy ? "Đang lưu…" : "Lưu"}
          </button>
        </div>
      </header>
      {msg && <p className={msg.startsWith("✓") ? "ik-ok" : "ik-err"}>{msg}</p>}
      <div className="ik-newsgrid">
        <div className="ik-panel ik-sets">
          <div className="ik-setblock">
            <p className="ik-label">Chuyên mục</p>
            <div className="ik-chips">
              {SECTIONS.map((s) => {
                const on = picked.includes(s.id);
                return (
                  <button
                    key={s.id}
                    type="button"
                    aria-pressed={on}
                    className={on ? "on" : ""}
                    onClick={() => {
                      const next = on ? picked.filter((x) => x !== s.id) : [...picked, s.id];
                      if (next.length) edit({ sections: SECTIONS.map((x) => x.id).filter((x) => next.includes(x)).join(",") });
                    }}
                  >
                    {s.label}
                  </button>
                );
              })}
            </div>
          </div>
          <Row title="Tự làm số mới mỗi ngày">
            <Toggle label="Tự làm số mới" on={prefs.enabled} onChange={(b) => edit({ enabled: b })} />
          </Row>
          <Row title="Giờ làm số mới" desc="Giờ Việt Nam. Máy nhận ở lần đồng bộ ngay sau đó">
            <Seg label="Giờ" value={String(prefs.hour)} onChange={(v) => edit({ hour: Number(v) })} options={[{ v: "5", label: "5:00" }, { v: "6", label: "6:00" }, { v: "7", label: "7:00" }, { v: "12", label: "12:00" }]} />
          </Row>
          <Row title="Số bài mỗi chuyên mục">
            <Seg label="Số bài" value={String(prefs.per_section)} onChange={(v) => edit({ per_section: Number(v) })} options={[{ v: "3", label: "3" }, { v: "5", label: "5" }, { v: "10", label: "10" }]} />
          </Row>
          <Row title="Kèm ảnh" desc="Ảnh thu nhỏ đen trắng, mỗi số khoảng 1–3 MB">
            <Toggle label="Kèm ảnh" on={prefs.images} onChange={(b) => edit({ images: b })} />
          </Row>
          <Row title="Giữ trên máy">
            <Seg label="Giữ" value={String(prefs.keep)} onChange={(v) => edit({ keep: Number(v) })} options={[{ v: "3", label: "3 số" }, { v: "7", label: "7 số" }, { v: "30", label: "30 số" }]} />
          </Row>
        </div>
        <aside className="ik-panel ik-issues">
          <p className="ik-label">Các số gần đây</p>
          {issues.length === 0 ? (
            <p className="ik-muted">Chưa có số nào. Bấm "Làm số mới ngay" để thử.</p>
          ) : (
            <ul>
              {issues.map((i) => (
                <li key={i.id}>
                  <div>
                    <b>{i.title}</b>
                    <span>
                      {bytes(i.size)} · {ago(i.created_at)}
                    </span>
                  </div>
                  <span className={"pill" + (i.on_device ? " ok" : "")}>
                    {i.progress ? `Đọc ${i.progress}%` : i.on_device ? "Đã về máy" : "Chờ máy tải"}
                  </span>
                </li>
              ))}
            </ul>
          )}
          {prefs.last_note && <p className="ik-muted ik-small">Lần làm gần nhất: {prefs.last_note}</p>}
          <p className="ik-muted ik-small">Bản tin chỉ để bạn đọc riêng trên máy của mình. Nội dung thuộc VnExpress.</p>
        </aside>
      </div>
    </section>
  );
}

function Row({ title, desc, children }: { title: string; desc?: string; children: React.ReactNode }) {
  return (
    <div className="ik-setrow">
      <div>
        <b>{title}</b>
        {desc && <span className="d">{desc}</span>}
      </div>
      <div className="ik-setctl">{children}</div>
    </div>
  );
}
