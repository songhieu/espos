// The X4 sleep-screen dashboard drawn in HTML, e-ink style (black on paper), from the same
// JSON the device downloads. Used as a live preview on the web.
import { useEffect, useState } from "react";

import type { Dash } from "./espos";

const ICON: Record<string, string> = {
  sun: "M12 4v2M12 18v2M4 12h2M18 12h2M6.3 6.3l1.4 1.4M16.3 16.3l1.4 1.4M6.3 17.7l1.4-1.4M16.3 7.7l1.4-1.4M12 8a4 4 0 1 0 0 8 4 4 0 0 0 0-8z",
  cloudsun: "M8 2.5v1.6M3.2 7.6H1.8M4.5 4.1l1.1 1.1M11.6 4.6l-1.1 1.1M5.2 10.6A3.6 3.6 0 0 1 11.4 7M7.5 20h9.3a3.7 3.7 0 0 0 .3-7.4 5 5 0 0 0-9.5 1.3A3.1 3.1 0 0 0 7.5 20z",
  cloud: "M7 19h10a4 4 0 0 0 .4-8 5.5 5.5 0 0 0-10.6 1.6A3.2 3.2 0 0 0 7 19z",
  rain: "M7 15h10a4 4 0 0 0 .4-8 5.5 5.5 0 0 0-10.6 1.6A3.2 3.2 0 0 0 7 15zM8.5 18l-1 2.5M12.5 18l-1 2.5M16.5 18l-1 2.5",
  storm: "M7 14h10a4 4 0 0 0 .4-8 5.5 5.5 0 0 0-10.6 1.6A3.2 3.2 0 0 0 7 14zM12 15l-2 4h3l-2 4",
  fog: "M4 10h16M6 14h12M4 18h16M7 6h10",
  snow: "M12 3v18M4.2 7.5l15.6 9M4.2 16.5l15.6-9",
  moon: "M19 14.5A7.5 7.5 0 0 1 9.5 5a7.5 7.5 0 1 0 9.5 9.5z",
};

function WIcon({ name, size }: { name: string; size: number }) {
  return (
    <svg width={size} height={size} viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth={1.7} strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d={ICON[name] ?? ICON.cloud} />
    </svg>
  );
}

export type DashLayout = "desk" | "calendar" | "portrait";

/** dash.layout as stored ("landscape" is the old name of "desk"; empty means the default). */
export function normLayout(v: string | undefined): DashLayout {
  return v === "portrait" || v === "calendar" ? v : "desk";
}

function useClock() {
  const [now, setNow] = useState(() => new Date());
  useEffect(() => {
    const t = setInterval(() => setNow(new Date()), 15000);
    return () => clearInterval(t);
  }, []);
  return now;
}

const pad2 = (n: number) => String(n).padStart(2, "0");

function nextEvent(d: Dash, now: Date) {
  const m = now.getHours() * 60 + now.getMinutes();
  return (d.events ?? []).findIndex((e) => {
    const x = /^(\d{1,2}):(\d{2})/.exec(e.time);
    return x != null && Number(x[1]) * 60 + Number(x[2]) >= m;
  });
}

function bottomItem(d: Dash): { tag: string; big: string; small: string; note: boolean } | null {
  if (d.notes && d.notes.length > 0) return { tag: "THÔNG BÁO", big: d.notes[0].title, small: d.notes[0].time, note: true };
  if (d.word?.w) return { tag: "TỪ CỦA NGÀY", big: d.word.w, small: d.word.m, note: false };
  return null;
}

function Events({ d, now, max, cls }: { d: Dash; now: Date; max: number; cls?: string }) {
  const next = nextEvent(d, now);
  const ev = (d.events ?? []).slice(0, max);
  if (ev.length === 0) return <div className="la-empty">Hôm nay không có lịch.</div>;
  return (
    <>
      {ev.map((e, i) => (
        <div key={i} className={"la-ev" + (i === next ? " next" : "") + (cls ? " " + cls : "")}>
          <b>{e.time}</b>
          <span className="tr">{e.title}</span>
          {i === next && <em>SẮP TỚI</em>}
        </div>
      ))}
    </>
  );
}

function Foot({ d }: { d: Dash }) {
  return (
    <div className="la-foot">
      <span>
        <i className="batt" />
        82%
      </span>
      <span>Dữ liệu lúc {d.date ? "vừa xong" : "—"}</span>
    </div>
  );
}

function Desk({ d, now }: { d: Dash; now: Date }) {
  const b = bottomItem(d);
  return (
    <div className="la la-desk">
      <div className="la-left">
        <div className="la-caps">
          {d.weekday} · {d.date}
        </div>
        <div className="la-clock">
          {pad2(now.getHours())}:{pad2(now.getMinutes())}
        </div>
        {d.lunar && (
          <div className="la-lunar">
            <span className="la-tag">ÂM LỊCH</span>
            {d.lunar} · {d.lunar_year}
          </div>
        )}
        <div className="la-rule" />
        {d.weather && (
          <>
            <div className="la-wx">
              <WIcon name={d.weather.icon} size={60} />
              <span className="t">{d.weather.temp}°</span>
              <div className="m">
                <b>{d.weather.city}</b>
                <span>{d.weather.desc}</span>
                <span>
                  {d.weather.min ?? ""}–{d.weather.max ?? ""}° · mưa {d.weather.rain ?? 0}%
                </span>
              </div>
            </div>
            {d.weather.hours && d.weather.hours.length > 0 && (
              <div className="la-hours">
                {d.weather.hours.slice(0, 4).map((h) => (
                  <div key={h.h}>
                    {h.h} {h.t}°
                  </div>
                ))}
              </div>
            )}
          </>
        )}
        <div className="la-book">
          <div className="la-caps sm">ĐANG ĐỌC</div>
          <div className="t tr">Sách bạn đang đọc trên máy</div>
          <div className="la-prog">
            <div>
              <b style={{ width: "40%" }} />
            </div>
            40%
          </div>
        </div>
      </div>
      <div className="la-vline" />
      <div className="la-right">
        <div className="la-sec">
          <span>LỊCH HÔM NAY</span>
          <small>{d.events?.length ? `${d.events.length} sự kiện` : ""}</small>
        </div>
        <Events d={d} now={now} max={7} />
        {b && (
          <div className="la-word">
            <span className="la-tag">{b.tag}</span>
            <b className={b.note ? "n" : ""}>{b.big}</b>
            <span className="tr">{b.small}</span>
          </div>
        )}
      </div>
      <Foot d={d} />
    </div>
  );
}

function Calendar({ d, now }: { d: Dash; now: Date }) {
  const b = bottomItem(d);
  const lm = /^(\d+)\/(\d+)(N?)/.exec(d.lunar ?? "");
  return (
    <div className="la la-cal">
      <div className="lc-sheet">
        <div className="mo">THÁNG {now.getMonth() + 1}</div>
        <div className="day">{now.getDate()}</div>
        <div className="wd">{d.weekday}</div>
        {lm && (
          <div className="lun">
            <div className="k">ÂM LỊCH</div>
            <div className="v">
              {lm[1]} tháng {lm[2]}
              {lm[3] ? " nhuận" : ""}
            </div>
            <div className="y">năm {d.lunar_year}</div>
          </div>
        )}
        <div className="bk">
          <div className="k">ĐANG ĐỌC</div>
          <div className="t tr">Sách trên máy</div>
          <div className="la-prog inv">
            <div>
              <b style={{ width: "40%" }} />
            </div>
            40%
          </div>
        </div>
      </div>
      <div className="lc-main">
        <div className="lc-top">
          <div className="la-clock">
            {pad2(now.getHours())}:{pad2(now.getMinutes())}
          </div>
          {d.weather && (
            <div className="lc-wx">
              <div className="r1">
                <WIcon name={d.weather.icon} size={50} />
                <span className="t">{d.weather.temp}°</span>
              </div>
              <span className="tr">
                <b>{d.weather.city}</b> · {d.weather.desc}
              </span>
              <span>
                {d.weather.min ?? ""}–{d.weather.max ?? ""}° · mưa {d.weather.rain ?? 0}%
              </span>
            </div>
          )}
        </div>
        {d.weather?.hours && d.weather.hours.length > 0 && (
          <div className="la-hours">
            {d.weather.hours.slice(0, 4).map((h) => (
              <div key={h.h}>
                <WIcon name={h.icon} size={18} />
                {h.h} {h.t}°
              </div>
            ))}
          </div>
        )}
        <div className="lc-events">
          <div className="la-sec">
            <span>LỊCH HÔM NAY</span>
            <small>{d.events?.length ? `${d.events.length} sự kiện` : ""}</small>
          </div>
          <Events d={d} now={now} max={6} cls="sm" />
        </div>
        {b && (
          <div className="lc-bottom">
            <span className="k">{b.tag}</span>
            <b>{b.big}</b>
            <span className="tr">{b.small}</span>
          </div>
        )}
      </div>
      <Foot d={d} />
    </div>
  );
}

export function DashPreview({ d, layout = "desk", scale }: { d: Dash | null; layout?: DashLayout; scale?: number }) {
  const now = useClock();
  if (layout !== "portrait") {
    const k = scale ?? 0.5;
    return (
      <div className="ik-eink" style={{ width: 800 * k, height: 480 * k }} aria-label="Xem trước màn hình máy lúc ngủ">
        <div className="ik-eink-in land" style={{ transform: `scale(${k})` }}>
          {!d ? <div className="ik-eink-empty">Đang tải dữ liệu…</div> : layout === "calendar" ? <Calendar d={d} now={now} /> : <Desk d={d} now={now} />}
        </div>
      </div>
    );
  }
  return <Portrait d={d} scale={scale ?? 0.62} />;
}

function Portrait({ d, scale }: { d: Dash | null; scale: number }) {
  return (
    <div className="ik-eink" style={{ width: 480 * scale, height: 800 * scale }} aria-label="Xem trước màn hình máy lúc ngủ">
      <div className="ik-eink-in" style={{ transform: `scale(${scale})` }}>
        {!d ? (
          <div className="ik-eink-empty">Đang tải dữ liệu…</div>
        ) : (
          <>
            <div className="e-top">
              <div>
                <div className="e-dow">{d.weekday}</div>
                <div className="e-date">{d.date}</div>
              </div>
              {d.lunar && (
                <div className="e-lunar">
                  <div className="l">ÂM LỊCH</div>
                  <div className="b">{d.lunar}</div>
                  <div className="s">{d.lunar_year}</div>
                </div>
              )}
            </div>
            <div className="e-rule" />
            {d.weather && (
              <>
                <div className="e-wx">
                  <WIcon name={d.weather.icon} size={72} />
                  <span className="t">{d.weather.temp}°</span>
                  <div className="desc">
                    <b>{d.weather.city}</b>
                    {d.weather.desc}
                    <br />
                    {d.weather.min ?? ""}° – {d.weather.max ?? ""}° · mưa {d.weather.rain ?? 0}%
                  </div>
                </div>
                {d.weather.hours && d.weather.hours.length > 0 && (
                  <div className="e-fc">
                    {d.weather.hours.slice(0, 4).map((h) => (
                      <div key={h.h}>
                        <WIcon name={h.icon} size={16} />
                        {h.h} <b>{h.t}°</b>
                      </div>
                    ))}
                  </div>
                )}
              </>
            )}
            {d.events && d.events.length > 0 && (
              <div className="e-sec">
                <h4>LỊCH HÔM NAY</h4>
                {d.events.map((e, i) => (
                  <div className="e-ev" key={i}>
                    <b>{e.time}</b>
                    <span>{e.title}</span>
                  </div>
                ))}
              </div>
            )}
            {d.notes && d.notes.length > 0 && (
              <div className="e-sec">
                <h4>THÔNG BÁO</h4>
                {d.notes.map((n, i) => (
                  <div className="e-nt" key={i}>
                    <b>{n.title}</b>
                    <span>{n.time}</span>
                  </div>
                ))}
              </div>
            )}
            {d.word && (
              <div className="e-word">
                <div className="l">TỪ CỦA NGÀY</div>
                <div className="w">{d.word.w}</div>
                <div className="m">{d.word.m}</div>
              </div>
            )}
            <div className="e-foot">
              <span>Pin</span>
              <span>Dữ liệu vừa cập nhật</span>
            </div>
          </>
        )}
      </div>
    </div>
  );
}
