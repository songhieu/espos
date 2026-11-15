// Thống kê đọc: reading time from the X4 (active reading only: from a page appearing to the
// next page turn), per day, against a daily goal that the device also shows.
import { useEffect, useMemo, useState } from "react";

import { type Device, type Ink, type Stats as StatsData } from "../inkdesk";
import { Seg } from "../ui";

const GOALS = ["10", "15", "20", "30", "45", "60"] as const;
type Range = "7" | "30" | "12m";
const WD = ["CN", "T2", "T3", "T4", "T5", "T6", "T7"];

/** "2026-11-14" shifted by n days (calendar arithmetic, no time zone involved). */
function addDays(day: string, n: number): string {
  const [y, m, d] = day.split("-").map(Number);
  const t = new Date(Date.UTC(y, m - 1, d + n));
  return t.toISOString().slice(0, 10);
}
const wdayOf = (day: string) => {
  const [y, m, d] = day.split("-").map(Number);
  return new Date(Date.UTC(y, m - 1, d)).getUTCDay();
};
const ddmm = (day: string) => `${day.slice(8, 10)}/${day.slice(5, 7)}`;

function dur(sec: number): { big: string; unit: string } {
  // Whole minutes, rounded down like the reader shows them.
  const m = Math.floor(sec / 60);
  if (m < 60) return { big: String(m), unit: "phút" };
  return { big: `${Math.floor(m / 60)} giờ ${String(m % 60).padStart(2, "0")}`, unit: "phút" };
}
const durText = (sec: number) => {
  const d = dur(sec);
  return `${d.big} ${d.unit}`;
};

export function Stats({ ink }: { ink: Ink }) {
  const [data, setData] = useState<StatsData | null>(null);
  const [devices, setDevices] = useState<Device[]>([]);
  const [err, setErr] = useState<string | null>(null);
  const [range, setRange] = useState<Range>("30");
  const [saving, setSaving] = useState(false);
  useEffect(() => {
    ink.stats().then(setData, (e: Error) => setErr(e.message));
    ink.devices().then(setDevices, () => undefined);
  }, [ink]);

  const byDay = useMemo(() => new Map((data?.days ?? []).map((d) => [d.d, d])), [data]);
  if (err) return <section className="ik-page"><p className="ik-err">{err}</p></section>;
  if (!data) return <section className="ik-page"><p className="ik-muted">Đang tải…</p></section>;

  const goalSec = data.goal * 60;
  const sec = (day: string) => byDay.get(day)?.s ?? 0;
  const today = data.today;
  const todaySec = sec(today);
  const todayPages = byDay.get(today)?.p ?? 0;
  const sinceMonday = (wdayOf(today) + 6) % 7;
  let week = 0;
  let lastWeek = 0;
  for (let i = 0; i <= sinceMonday; i++) week += sec(addDays(today, -i));
  for (let i = 0; i <= sinceMonday; i++) lastWeek += sec(addDays(today, -i - 7));
  const month = today.slice(0, 7);
  const monthDays = data.days.filter((d) => d.d.startsWith(month));
  const monthSec = monthDays.reduce((n, d) => n + d.s, 0);
  const monthPages = monthDays.reduce((n, d) => n + d.p, 0);
  const pct = Math.min(1, goalSec ? todaySec / goalSec : 0);
  const left = Math.max(0, Math.ceil((goalSec - todaySec) / 60));
  const empty = data.days.length === 0;

  const setGoal = async (v: string) => {
    setSaving(true);
    try {
      await Promise.all(devices.map((d) => ink.saveSettings(d.id, { "read.goal_min": v })));
      setData({ ...data, goal: Number(v) });
    } catch (x) {
      setErr((x as Error).message);
    } finally {
      setSaving(false);
    }
  };

  return (
    <section className="ik-page ik-stats">
      <header className="ik-head">
        <div>
          <h2>Thống kê đọc</h2>
          <p>Chỉ tính lúc đọc thật: từ lúc một trang hiện ra đến lúc lật sang trang sau. Máy gửi số liệu ở mỗi lần đồng bộ.</p>
        </div>
        <div className="ik-goal">
          <span>Mục tiêu mỗi ngày</span>
          <Seg label="Mục tiêu mỗi ngày (phút)" value={String(data.goal) as (typeof GOALS)[number]} options={GOALS.map((g) => ({ v: g, label: g }))} onChange={(v) => !saving && void setGoal(v)} />
          <span>phút</span>
        </div>
      </header>

      {empty && (
        <div className="ik-panel ik-muted">
          {data.has_device ? "Chưa có số liệu. Đọc sách trên máy; lần đồng bộ sau số phút đọc sẽ hiện ở đây." : "Chưa có máy nào liên kết. Cài máy ở trang Thiết bị trước."}
        </div>
      )}

      <div className="ik-kpis">
        <div className="ik-kpi">
          <div className="k">Hôm nay</div>
          <div className="ik-ring">
            <svg width="58" height="58" viewBox="0 0 36 36" aria-hidden="true">
              <circle cx="18" cy="18" r="15" fill="none" stroke="var(--ring-bg)" strokeWidth="4" />
              <circle cx="18" cy="18" r="15" fill="none" stroke="currentColor" strokeWidth="4" strokeDasharray={`${pct * 94.25} 94.25`} transform="rotate(-90 18 18)" strokeLinecap="round" />
            </svg>
            <div>
              <div className="v">
                {Math.floor(todaySec / 60)} <small>/ {data.goal} phút</small>
              </div>
              <div className="s">{left > 0 ? `còn ${left} phút · ${todayPages} trang` : `đạt mục tiêu · ${todayPages} trang`}</div>
            </div>
          </div>
        </div>
        <div className="ik-kpi">
          <div className="k">Chuỗi ngày</div>
          <div className="v">
            {data.streak} <small>ngày</small>
          </div>
          <div className="s">dài nhất {data.best} ngày</div>
        </div>
        <div className="ik-kpi">
          <div className="k">Tuần này</div>
          <div className="v">
            {dur(week).big} <small>{dur(week).unit}</small>
          </div>
          <div className="s">
            {lastWeek > 0 ? (
              <>
                <b className={week >= lastWeek ? "up" : "down"}>
                  {week >= lastWeek ? "+" : ""}
                  {Math.round(((week - lastWeek) / lastWeek) * 100)}%
                </b>{" "}
                so với cùng kỳ tuần trước
              </>
            ) : (
              "tính từ thứ Hai"
            )}
          </div>
        </div>
        <div className="ik-kpi">
          <div className="k">Tháng {Number(today.slice(5, 7))}</div>
          <div className="v">
            {dur(monthSec).big} <small>{dur(monthSec).unit}</small>
          </div>
          <div className="s">
            {monthPages} trang · {data.books.length} cuốn
          </div>
        </div>
      </div>

      <div className="ik-stats-grid">
        <div className="ik-panel">
          <div className="ik-stats-bar">
            <span className="ik-label">{range === "12m" ? "12 tháng qua" : `${range} ngày qua`}</span>
            <Seg
              label="Khoảng thời gian"
              value={range}
              options={[
                { v: "7", label: "7 ngày" },
                { v: "30", label: "30 ngày" },
                { v: "12m", label: "12 tháng" },
              ]}
              onChange={setRange}
            />
          </div>
          {range === "12m" ? <Months data={data} /> : <Bars days={Number(range)} today={today} sec={sec} goalSec={goalSec} goal={data.goal} />}
        </div>
        <div className="ik-panel">
          <span className="ik-label">12 tuần</span>
          <Heat today={today} sec={sec} goalSec={goalSec} />
        </div>
      </div>

      <div className="ik-panel">
        <span className="ik-label">Sách đã đọc trong tháng</span>
        {data.books.length === 0 ? (
          <p className="ik-muted">Chưa đọc cuốn nào trong tháng này.</p>
        ) : (
          <table className="ik-booktable">
            <thead>
              <tr>
                <th>Sách</th>
                <th>Tiến độ</th>
                <th className="num">Thời gian</th>
                <th className="num">Trang</th>
                <th className="num">Đọc gần nhất</th>
              </tr>
            </thead>
            <tbody>
              {data.books.map((b) => (
                <tr key={b.key}>
                  <td className="t">{b.title}</td>
                  <td>
                    {b.percent != null ? (
                      <span className="ik-mini">
                        <span className="tr">
                          <i style={{ width: `${b.percent}%` }} />
                        </span>
                        {b.percent >= 98 ? "Xong" : `${b.percent}%`}
                      </span>
                    ) : (
                      "—"
                    )}
                  </td>
                  <td className="num">{durText(b.seconds)}</td>
                  <td className="num">{b.pages}</td>
                  <td className="num">{b.last === today ? "hôm nay" : b.last === addDays(today, -1) ? "hôm qua" : ddmm(b.last)}</td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
      </div>
    </section>
  );
}

function Bars({ days, today, sec, goalSec, goal }: { days: number; today: string; sec: (d: string) => number; goalSec: number; goal: number }) {
  const list = Array.from({ length: days }, (_, i) => addDays(today, i - days + 1));
  const top = Math.max(goalSec * 1.5, ...list.map(sec)) || 1;
  const every = days <= 7 ? 1 : 5;
  return (
    <>
      <div className="ik-bars" role="img" aria-label={`Số phút đọc ${days} ngày qua, mục tiêu ${goal} phút`}>
        <div className="goal" style={{ bottom: `${(goalSec / top) * 100}%` }}>
          <span>mục tiêu {goal} phút</span>
        </div>
        {list.map((d, i) => {
          const s = sec(d);
          const label = d === today ? "nay" : days <= 7 ? WD[wdayOf(d)] : (days - 1 - i) % every === 0 ? ddmm(d) : "";
          return (
            <div key={d} className={"b" + (s >= goalSec && s > 0 ? " hit" : "") + (d === today ? " today" : "")} style={{ height: `${(s / top) * 100}%` }} title={`${ddmm(d)}: ${Math.round(s / 60)} phút`}>
              {label && <em>{label}</em>}
            </div>
          );
        })}
      </div>
      <div className="ik-legend">
        <span>
          <i className="hit" />
          Đạt mục tiêu
        </span>
        <span>
          <i />
          Chưa đạt
        </span>
        <span>
          <i className="today" />
          Hôm nay
        </span>
      </div>
    </>
  );
}

function Months({ data }: { data: StatsData }) {
  const months: { key: string; sec: number }[] = [];
  const [y, m] = data.today.split("-").map(Number);
  for (let i = 11; i >= 0; i--) {
    const d = new Date(Date.UTC(y, m - 1 - i, 1));
    const key = d.toISOString().slice(0, 7);
    months.push({ key, sec: data.days.filter((x) => x.d.startsWith(key)).reduce((n, x) => n + x.s, 0) });
  }
  const top = Math.max(...months.map((x) => x.sec)) || 1;
  return (
    <div className="ik-bars months" role="img" aria-label="Số giờ đọc mỗi tháng">
      {months.map((x) => (
        <div key={x.key} className="b hit" style={{ height: `${(x.sec / top) * 100}%` }} title={`${x.key}: ${durText(x.sec)}`}>
          <em>T{Number(x.key.slice(5))}</em>
        </div>
      ))}
    </div>
  );
}

/** The last 12 weeks, Monday rows first, darker = more reading (relative to the goal). */
function Heat({ today, sec, goalSec }: { today: string; sec: (d: string) => number; goalSec: number }) {
  const sinceMonday = (wdayOf(today) + 6) % 7;
  const start = addDays(today, -sinceMonday - 7 * 11);
  const rows = ["T2", "T3", "T4", "T5", "T6", "T7", "CN"];
  const level = (s: number) => (s <= 0 ? 0 : s < goalSec * 0.5 ? 1 : s < goalSec ? 2 : s < goalSec * 2 ? 3 : 4);
  return (
    <div className="ik-heat">
      {rows.map((r, ri) => (
        <div className="row" key={r}>
          <span>{ri % 2 === 0 ? r : ""}</span>
          {Array.from({ length: 12 }, (_, c) => {
            const d = addDays(start, c * 7 + ri);
            if (d > today) return <i key={c} className="future" />;
            return <i key={c} className={"l" + level(sec(d))} title={`${ddmm(d)}: ${Math.round(sec(d) / 60)} phút`} />;
          })}
        </div>
      ))}
      <div className="ik-legend end">
        <span>ít</span>
        {[0, 1, 2, 3, 4].map((l) => (
          <i key={l} className={"l" + l} />
        ))}
        <span>nhiều</span>
      </div>
    </div>
  );
}
