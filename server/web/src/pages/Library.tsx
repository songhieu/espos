import { useEffect, useRef, useState } from "react";

import { bytes, type Book, type Ink } from "../inkdesk";
import { Icon } from "../ui";

interface Upload {
  name: string;
  pct: number;
  err?: string;
}

function put(url: string, file: File, onPct: (p: number) => void): Promise<void> {
  return new Promise((resolve, reject) => {
    const x = new XMLHttpRequest();
    x.open("PUT", url);
    x.setRequestHeader("Content-Type", file.type || "application/octet-stream");
    x.upload.onprogress = (e) => e.lengthComputable && onPct(e.loaded / e.total);
    x.onload = () => (x.status >= 200 && x.status < 300 ? resolve() : reject(new Error(`Tải lên lỗi ${x.status}`)));
    x.onerror = () => reject(new Error("Mất kết nối khi tải lên"));
    x.send(file);
  });
}

export function Library({ ink }: { ink: Ink }) {
  const [books, setBooks] = useState<Book[] | null>(null);
  const [used, setUsed] = useState(0);
  const [ups, setUps] = useState<Upload[]>([]);
  const [err, setErr] = useState<string | null>(null);
  const [over, setOver] = useState(false);
  const input = useRef<HTMLInputElement>(null);

  const load = () =>
    ink.books().then(
      (r) => {
        setBooks(r.books);
        setUsed(r.bytes);
      },
      (e: Error) => setErr(e.message),
    );
  useEffect(() => {
    void load();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  async function uploadAll(files: FileList | File[]) {
    for (const file of Array.from(files)) {
      const name = file.name;
      const set = (u: Partial<Upload>) => setUps((list) => list.map((x) => (x.name === name ? { ...x, ...u } : x)));
      setUps((list) => [...list.filter((x) => x.name !== name), { name, pct: 0 }]);
      try {
        if (!/\.(epub|txt)$/i.test(name)) throw new Error("Chỉ nhận .epub hoặc .txt");
        const start = await ink.startUpload({ file_name: name, size: file.size, title: name.replace(/\.(epub|txt)$/i, "") });
        await put(start.url, file, (p) => set({ pct: p }));
        await ink.confirmBook(start.id);
        setUps((list) => list.filter((x) => x.name !== name));
      } catch (e) {
        set({ err: (e as Error).message });
      }
    }
    void load();
  }

  return (
    <section className="ik-page">
      <header className="ik-head">
        <div>
          <h2>Thư viện</h2>
          <p>Gửi sách lên đây, máy tải về thư mục InkDesk trên thẻ nhớ ở lần đồng bộ sau. Máy tải đủ rồi thì bản gốc trên server tự xoá, ở đây chỉ giữ lịch sử gửi.</p>
        </div>
        <div className="ik-usage">{bytes(used)} đang chờ trên server</div>
      </header>
      <div
        className={"ik-drop" + (over ? " over" : "")}
        onDragOver={(e) => {
          e.preventDefault();
          setOver(true);
        }}
        onDragLeave={() => setOver(false)}
        onDrop={(e) => {
          e.preventDefault();
          setOver(false);
          void uploadAll(e.dataTransfer.files);
        }}
      >
        <Icon name="upload" size={40} />
        <div>
          <b>Kéo thả file EPUB vào đây, hoặc bấm để chọn</b>
          <span>EPUB, TXT · tối đa 100 MB mỗi file · chỉ bạn thấy được sách của mình</span>
        </div>
        <button type="button" className="ik-btn ghost" onClick={() => input.current?.click()}>
          Chọn file
        </button>
        <input
          ref={input}
          type="file"
          accept=".epub,.txt"
          multiple
          hidden
          onChange={(e) => {
            if (e.target.files) void uploadAll(e.target.files);
            e.target.value = "";
          }}
        />
      </div>
      {ups.map((u) => (
        <div key={u.name} className="ik-up">
          <span>{u.name}</span>
          {u.err ? <span className="ik-err">{u.err}</span> : <progress max={1} value={u.pct} aria-label={`Đang tải ${u.name}`} />}
        </div>
      ))}
      {err && <p className="ik-err">{err}</p>}
      <div className="ik-tbl ik-sent">
        <div className="ik-tr h">
          <span />
          <span>Sách</span>
          <span>Dung lượng</span>
          <span>Gửi lúc</span>
          <span>Trạng thái</span>
          <span />
        </div>
        {books?.length === 0 && <div className="ik-empty">Chưa gửi cuốn nào. Kéo một file EPUB vào khung phía trên.</div>}
        {books?.map((b) => {
          const st = sentState(b);
          return (
            <div key={b.id} className="ik-tr">
              <span className="ik-thumb" />
              <span className="ttl">
                <b>{b.title}</b>
                <span>{b.author || b.file_name}</span>
              </span>
              <span>{bytes(b.size)}</span>
              <span className="ik-when">{when(b.created_at)}</span>
              <span className="ik-st">
                <span className={"dot " + st.dot} />
                <span>
                  {st.text}
                  {st.note && <small>{st.note}</small>}
                </span>
              </span>
              <button
                type="button"
                className="ik-iconbtn"
                aria-label={`Xoá ${b.title}`}
                onClick={async () => {
                  if (!confirm(st.ask(b.title))) return;
                  await ink.deleteBook(b.id);
                  void load();
                }}
              >
                <Icon name="trash" />
              </button>
            </div>
          );
        })}
      </div>
    </section>
  );
}

/** "15/11 09:12" in the reader's own time. */
function when(iso?: string): string {
  const t = iso ? Date.parse(iso) : NaN;
  if (Number.isNaN(t)) return "";
  const d = new Date(t);
  const p = (n: number) => String(n).padStart(2, "0");
  return `${p(d.getDate())}/${p(d.getMonth() + 1)} ${p(d.getHours())}:${p(d.getMinutes())}`;
}

/** Where a sent book is: still uploading, waiting for the device, or delivered (original deleted). */
function sentState(b: Book): { dot: string; text: string; note?: string; ask: (title: string) => string } {
  const onDevice = (t: string) => `Xoá “${t}”? Lần đồng bộ sau máy cũng xoá cuốn này.`;
  if (b.status !== "ready") return { dot: "wait", text: "Đang tải lên", ask: (t) => `Huỷ gửi “${t}”?` };
  if (b.delivered_at) {
    if (!b.on_device) return { dot: "gone", text: "Máy đã xoá", note: `Về máy lúc ${when(b.delivered_at)}`, ask: (t) => `Xoá “${t}” khỏi lịch sử?` };
    return {
      dot: "ok",
      text: `Đã về máy ${when(b.delivered_at)}`,
      note: "Bản gốc trên server đã xoá",
      ask: (t) => `${onDevice(t)} Bản gốc không còn trên server nên muốn đọc lại phải gửi lại file.`,
    };
  }
  if (b.on_device) return { dot: "ok", text: "Đã về máy", note: "Bản gốc xoá khi máy báo đã tải đủ", ask: onDevice };
  return { dot: "wait", text: "Chờ máy tải", note: "Ở lần đồng bộ tới", ask: (t) => `Huỷ gửi “${t}”? Máy sẽ không tải cuốn này nữa.` };
}
