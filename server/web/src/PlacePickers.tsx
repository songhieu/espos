// Dropdowns for the weather city (with coordinates) and the time zone.
import { useState } from "react";

import { ALL_CITIES, CITY_GROUPS, searchPlaces, TIME_ZONES, type Place } from "./places";

const OTHER = "__other__";

/** City select; "Nơi khác…" opens a search over the whole world. */
export function CityPicker({ value, onChange, label = "Thành phố" }: { value: string; onChange: (p: Place) => void; label?: string }) {
  const [other, setOther] = useState(false);
  const [q, setQ] = useState("");
  const [hits, setHits] = useState<(Place & { where: string })[] | null>(null);
  const [err, setErr] = useState<string | null>(null);
  const known = ALL_CITIES.some((c) => c.name === value);

  async function search() {
    if (!q.trim()) return;
    setErr(null);
    try {
      const r = await searchPlaces(q);
      setHits(r);
      if (!r.length) setErr("Không thấy nơi nào tên như vậy.");
    } catch (e) {
      setErr((e as Error).message);
    }
  }

  return (
    <div className="ik-city">
      <select
        aria-label={label}
        value={other ? OTHER : value}
        onChange={(e) => {
          if (e.target.value === OTHER) {
            setOther(true);
            return;
          }
          setOther(false);
          const c = ALL_CITIES.find((x) => x.name === e.target.value);
          if (c) onChange(c);
        }}
      >
        {!known && value && <option value={value}>{value}</option>}
        {CITY_GROUPS.map((g) => (
          <optgroup key={g.label} label={g.label}>
            {g.cities.map((c) => (
              <option key={c.name} value={c.name}>
                {c.name}
              </option>
            ))}
          </optgroup>
        ))}
        <option value={OTHER}>Nơi khác…</option>
      </select>
      {other && (
        <div className="ik-citysearch">
          <div className="row">
            <input
              value={q}
              autoFocus
              placeholder="Gõ tên nơi bạn ở, ví dụ: Bangkok"
              aria-label="Tìm nơi khác"
              onChange={(e) => setQ(e.target.value)}
              onKeyDown={(e) => {
                if (e.key === "Enter") {
                  e.preventDefault();
                  void search();
                }
              }}
            />
            <button type="button" className="ik-btn sm" onClick={() => void search()}>
              Tìm
            </button>
          </div>
          {err && <p className="ik-err">{err}</p>}
          {hits && hits.length > 0 && (
            <ul>
              {hits.map((h, i) => (
                <li key={i}>
                  <button
                    type="button"
                    onClick={() => {
                      onChange({ name: h.name, lat: h.lat, lon: h.lon });
                      setOther(false);
                      setHits(null);
                      setQ("");
                    }}
                  >
                    <b>{h.name}</b>
                    <span>{h.where}</span>
                  </button>
                </li>
              ))}
            </ul>
          )}
        </div>
      )}
    </div>
  );
}

export function TzSelect({ value, onChange }: { value: string; onChange: (v: string) => void }) {
  return (
    <select aria-label="Múi giờ" value={value || "420"} onChange={(e) => onChange(e.target.value)}>
      {TIME_ZONES.map((t) => (
        <option key={t.min} value={String(t.min)}>
          {t.label}
        </option>
      ))}
    </select>
  );
}
