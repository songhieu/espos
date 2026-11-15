// Small shared UI pieces: stroke icons and a segmented control.
const PATHS: Record<string, string> = {
  kanban: "M3 4h18v16H3zM9 4v16M15 4v16",
  book: "M4 19.5V5a2 2 0 0 1 2-2h13v16H6.5A2.5 2.5 0 0 0 4 21.5v-2zM8 7h7M8 11h5",
  news: "M4 5h13v14a2 2 0 0 0 2 2H6a2 2 0 0 1-2-2zM17 9h3v10a2 2 0 0 1-2 2M7 9h7M7 13h7M7 17h4",
  device: "M5.5 2.5h13v19h-13zM8.5 6h7M8.5 9h7M8.5 12h5M9 18.5h2M13 18.5h2",
  sliders: "M4 6h9M17 6h3M4 12h3M11 12h9M4 18h11M19 18h1M15 4v4M9 10v4M17 16v4",
  usb: "M12 3v13M9.5 5.5 12 3l2.5 2.5M12 16l-4-3V9.5M12 13.5l4-2.5V8",
  plus: "M12 5v14M5 12h14",
  upload: "M12 15V4M7.5 8.5 12 4l4.5 4.5M4 15v3.5A1.5 1.5 0 0 0 5.5 20h13a1.5 1.5 0 0 0 1.5-1.5V15",
  sync: "M20 11a8 8 0 0 0-14.3-4.5L4 8.5M4 4v4.5h4.5M4 13a8 8 0 0 0 14.3 4.5L20 15.5M20 20v-4.5h-4.5",
  alert: "M12 3.5l9.5 16.5h-19zM12 10v4.5M12 17.3v.2",
  link: "M10 14a4 4 0 0 0 5.7 0l3-3a4 4 0 0 0-5.7-5.7l-1 1M14 10a4 4 0 0 0-5.7 0l-3 3a4 4 0 0 0 5.7 5.7l1-1",
  trash: "M4 7h16M9 7V4h6v3M6 7l1 13h10l1-13",
  x: "M6.5 6.5l11 11M17.5 6.5l-11 11",
  check: "M5 12.5l4.5 4.5L19 7.5",
  chart: "M4 20V10M10 20V4M16 20v-7M22 20H2",
  grid: "M4 4h7v7H4zM13 4h7v7h-7zM4 13h7v7H4zM13 13h7v7h-7z",
  download: "M12 4v11M7.5 10.5 12 15l4.5-4.5M5 20h14",
};

export function Icon({ name, size = 18 }: { name: string; size?: number }) {
  return (
    <svg className="ik-ico" width={size} height={size} viewBox="0 0 24 24" aria-hidden="true">
      <path d={PATHS[name] ?? ""} />
    </svg>
  );
}

export function Seg<T extends string>({
  value,
  options,
  onChange,
  label,
}: {
  value: T;
  options: { v: T; label: string }[];
  onChange: (v: T) => void;
  label: string;
}) {
  return (
    <div className="ik-seg" role="radiogroup" aria-label={label}>
      {options.map((o) => (
        <button key={o.v} type="button" role="radio" aria-checked={value === o.v} className={value === o.v ? "on" : ""} onClick={() => onChange(o.v)}>
          {o.label}
        </button>
      ))}
    </div>
  );
}

export function Toggle({ on, onChange, label }: { on: boolean; onChange: (v: boolean) => void; label: string }) {
  return <button type="button" role="switch" aria-checked={on} aria-label={label} className={on ? "ik-tg" : "ik-tg off"} onClick={() => onChange(!on)} />;
}
