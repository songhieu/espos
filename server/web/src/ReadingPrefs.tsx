// Settings → Reading: the reader font (CrossPoint's e-ink font catalogue, only families with
// their own Vietnamese glyphs) and a live page preview of the reading settings.
import { useEffect, useMemo, useState } from "react";

export type FontKind = "serif" | "sans" | "legible" | "mono";
export interface ReadFont {
  id: string; // value of read.font, the family name on the device
  label: string;
  kind: FontKind;
  desc: string;
  mb: number; // download size; 0 = built in
  google?: string; // Google Fonts family for the web preview
}

export const READ_FONTS: ReadFont[] = [
  { id: "notosans", label: "Noto Sans", kind: "sans", desc: "Có sẵn trong máy", mb: 0, google: "Noto Sans" },
  { id: "Literata", label: "Literata", kind: "serif", desc: "Làm riêng cho đọc trên màn hình", mb: 4.7, google: "Literata" },
  { id: "Merriweather", label: "Merriweather", kind: "serif", desc: "Ấm, nét dày, đọc lâu không mỏi", mb: 3.7, google: "Merriweather" },
  { id: "Bitter", label: "Bitter", kind: "serif", desc: "Chân vuông, thiết kế cho e‑ink", mb: 3.4, google: "Bitter" },
  { id: "Lora", label: "Lora", kind: "serif", desc: "Mềm mại, hợp văn học", mb: 3.2, google: "Lora" },
  { id: "SourceSerif4", label: "Source Serif 4", kind: "serif", desc: "Gọn, sáng sủa", mb: 4.1, google: "Source Serif 4" },
  { id: "Vollkorn", label: "Vollkorn", kind: "serif", desc: "Cổ điển, đậm đà", mb: 4.3, google: "Vollkorn" },
  { id: "NotoSerif", label: "Noto Serif", kind: "serif", desc: "Chân truyền thống, nhẹ", mb: 2.3, google: "Noto Serif" },
  { id: "GentiumBookPlus", label: "Gentium Book Plus", kind: "serif", desc: "Kiểu sách học thuật", mb: 3.6, google: "Gentium Book Plus" },
  { id: "IBMPlexSerif", label: "IBM Plex Serif", kind: "serif", desc: "Rõ nét, hiện đại", mb: 4.1, google: "IBM Plex Serif" },
  { id: "Alegreya", label: "Alegreya", kind: "serif", desc: "Nét bút, có hồn", mb: 3.7, google: "Alegreya" },
  { id: "Libron", label: "Libron", kind: "serif", desc: "Làm cho máy đọc sách", mb: 2.8 },
  { id: "NotoSerifExtended", label: "Noto Serif (mở rộng)", kind: "serif", desc: "Nhiều ngôn ngữ", mb: 4.3, google: "Noto Serif" },
  { id: "Inter", label: "Inter", kind: "sans", desc: "Hiện đại, gọn", mb: 4.5, google: "Inter" },
  { id: "SourceSans3", label: "Source Sans 3", kind: "sans", desc: "Thân thiện, dễ đọc", mb: 3.9, google: "Source Sans 3" },
  { id: "IBMPlexSans", label: "IBM Plex Sans", kind: "sans", desc: "Kỹ thuật, rõ ràng", mb: 4.3, google: "IBM Plex Sans" },
  { id: "NotoSansExtended", label: "Noto Sans (mở rộng)", kind: "sans", desc: "Nhiều ngôn ngữ", mb: 4.1, google: "Noto Sans" },
  { id: "LexicaUltralegible", label: "Lexica Ultralegible", kind: "legible", desc: "Cho mắt kém, chữ to rõ", mb: 2.3 },
  { id: "OpenDyslexic", label: "OpenDyslexic", kind: "legible", desc: "Cho người khó đọc", mb: 2.0 },
  { id: "IBMPlexMono", label: "IBM Plex Mono", kind: "mono", desc: "Đơn cách, hợp sách lập trình", mb: 3.6, google: "IBM Plex Mono" },
  { id: "SourceCodePro", label: "Source Code Pro", kind: "mono", desc: "Đơn cách, nét mảnh", mb: 2.2, google: "Source Code Pro" },
];

const KIND_LABEL: Record<FontKind, string> = { serif: "có chân", sans: "không chân", legible: "dễ đọc", mono: "đơn cách" };
const FILTERS: { id: "all" | FontKind | "installed"; label: string }[] = [
  { id: "all", label: "Tất cả" },
  { id: "serif", label: "Có chân" },
  { id: "sans", label: "Không chân" },
  { id: "legible", label: "Dễ đọc" },
  { id: "mono", label: "Đơn cách" },
  { id: "installed", label: "Đã cài trên máy" },
];
const SAMPLE = "Mùa thu, lá vàng rơi đầy ngõ nhỏ.";

/** Loads the Google Fonts used by the previews, once. */
function useGoogleFonts() {
  useEffect(() => {
    if (document.getElementById("ik-read-fonts")) return;
    const fams = Array.from(new Set(READ_FONTS.map((f) => f.google).filter(Boolean))) as string[];
    const link = document.createElement("link");
    link.id = "ik-read-fonts";
    link.rel = "stylesheet";
    link.href = "https://fonts.googleapis.com/css2?" + fams.map((f) => "family=" + f.replace(/ /g, "+") + ":wght@400;700").join("&") + "&display=swap";
    document.head.appendChild(link);
  }, []);
}

const cssFamily = (f: ReadFont | undefined) => (f?.google ? `'${f.google}', serif` : f?.kind === "sans" || f?.kind === "legible" ? "system-ui, sans-serif" : "Georgia, serif");

export function FontPicker({ value, installed, synced, onChange }: { value: string; installed: string[]; synced: string; onChange: (id: string) => void }) {
  useGoogleFonts();
  const [filter, setFilter] = useState<(typeof FILTERS)[number]["id"]>("all");
  const [more, setMore] = useState(false);
  const matching = READ_FONTS.filter((f) =>
    filter === "all" ? true : filter === "installed" ? f.mb === 0 || installed.includes(f.id) : f.kind === filter,
  );
  // Six at first (the chosen one among them), the rest on request: the settings below stay in reach.
  const FIRST = 6;
  let shown = matching;
  if (!more && matching.length > FIRST) {
    shown = matching.slice(0, FIRST);
    const cur = matching.find((f) => f.id === value);
    if (cur && !shown.includes(cur)) shown = [...shown.slice(0, FIRST - 1), cur];
  }
  return (
    <div className="ik-fontpick">
      <div className="ik-chips">
        {FILTERS.map((f) => (
          <button key={f.id} type="button" aria-pressed={filter === f.id} className={filter === f.id ? "on" : ""} onClick={() => setFilter(f.id)}>
            {f.label}
          </button>
        ))}
      </div>
      <div className="ik-fonts">
        {shown.map((f) => {
          const on = value === f.id;
          const onDevice = f.mb === 0 || installed.includes(f.id);
          const status = on
            ? onDevice
              ? { cls: "dark", text: synced === f.id ? "Đang dùng" : "Chọn, chờ lưu" }
              : { cls: "wait", text: "Máy tải ở lần đồng bộ tới" }
            : onDevice
              ? { cls: "ok", text: "Trên máy" }
              : null;
          return (
            <button key={f.id} type="button" className={"ik-font" + (on ? " on" : "")} aria-pressed={on} onClick={() => onChange(f.id)}>
              <span className="name">
                <b>{f.label}</b>
                <small>{KIND_LABEL[f.kind]}</small>
              </span>
              <span className="sample" style={{ fontFamily: cssFamily(f) }}>
                {SAMPLE}
              </span>
              <span className="foot">
                <span>
                  {f.desc}
                  {f.mb > 0 ? ` · ${f.mb.toLocaleString("vi-VN")} MB` : ""}
                  {!f.google ? " · xem trước gần đúng" : ""}
                </span>
                {status ? <span className={"pill " + status.cls}>{status.text}</span> : <span className="pick">Dùng font này</span>}
              </span>
            </button>
          );
        })}
      </div>
      {matching.length > FIRST && (
        <button type="button" className="ik-btn ghost sm ik-fontmore" onClick={() => setMore((m) => !m)}>
          {more ? "Thu gọn" : `Xem tất cả ${matching.length} font`}
        </button>
      )}
      <p className="ik-muted ik-small">Font dựng sẵn cho màn e‑ink của dự án CrossPoint (giấy phép OFL), đều tự có đủ dấu tiếng Việt. Mỗi font gồm cỡ 12–18, thường, đậm, nghiêng.</p>
    </div>
  );
}

const SIZE_PX: Record<string, number> = { small: 12, medium: 13.5, large: 15, xlarge: 16.5 };
const MARGIN_PX: Record<string, number> = { narrow: 10, medium: 18, wide: 28 };

/** An e-ink page drawn with the reading settings, as the X4 lays it out. */
export function PagePreview({ v }: { v: (k: string) => string }) {
  useGoogleFonts();
  const font = READ_FONTS.find((f) => f.id === (v("read.font") || "notosans"));
  const status = useMemo(() => (v("read.status") || "page,percent,battery,chapter").split(","), [v]);
  const spacing = Number(v("read.spacing") || "1.4");
  const indent = Number(v("read.indent") || "2");
  const style: React.CSSProperties = {
    fontFamily: cssFamily(font),
    fontSize: SIZE_PX[v("read.size") || "medium"] ?? 13.5,
    lineHeight: spacing,
    textAlign: v("read.align") === "left" ? "left" : "justify",
    padding: `16px ${MARGIN_PX[v("read.margin") || "narrow"] ?? 10}px 30px`,
    hyphens: v("read.hyphen") === "1" ? "auto" : "manual",
  };
  const p: React.CSSProperties = { textIndent: indent ? `${indent * 0.6}em` : 0, margin: v("read.para") === "0" ? 0 : "0 0 .55em" };
  return (
    <div className="ik-page-prev" aria-label="Xem trước trang sách">
      <div className="txt" style={style} lang="vi">
        <h4>Chương 1</h4>
        <p style={p}>Mùa thu năm ấy, lá vàng rơi đầy ngõ nhỏ. Tôi ngồi bên cửa sổ, tay cầm cuốn sách cũ, nghe gió lùa qua hàng cây sấu trước nhà.</p>
        <p style={p}>Mẹ bảo, đọc sách là cách đi xa nhất mà không cần rời khỏi chỗ ngồi. Ngày ấy tôi chưa hiểu, chỉ thấy những trang giấy ngả màu thơm mùi thời gian.</p>
        <p style={p}>Bây giờ, mỗi tối trước khi ngủ, tôi vẫn giữ thói quen ấy: một ngọn đèn nhỏ, một chén trà, và vài chục trang sách.</p>
      </div>
      {status.some(Boolean) && (
        <div className="bar">
          <span>
            {status.includes("chapter") ? "Chương 1" : ""}
            {status.includes("chapter") && status.includes("page") ? " · " : ""}
            {status.includes("page") ? "3/24" : ""}
          </span>
          <span>
            {[status.includes("percent") ? "12%" : "", status.includes("clock") ? "21:30" : "", status.includes("battery") ? "82%" : ""].filter(Boolean).join(" · ")}
          </span>
        </div>
      )}
    </div>
  );
}
