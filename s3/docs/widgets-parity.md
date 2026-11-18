# S3 widgets next to the X4's (2026-11-17)

Owner: "widget của S3 ít hơn cái máy đọc sách nhiều lắm, làm ơn xem đầy đủ và handle cho đủ". This is every X4 widget
kind and option (firmware/src/espos/Widgets.{h,cpp}, web/app/src/widgets.tsx and ink/) against the S3's
(src/wid.cpp kinds(), src/wid/*.cpp, web/app/src/os/wid*.ts), what was added, and what is left on purpose.

S3 sizes are the iOS ones: S 2×2 (144×138), M 4×2 (300×138), L 4×4 (300×286). The X4 places widgets on a free grid
(2×1 … 4×5); its one-row sizes (2×1, 4×1) have no S3 size, the S3 draws the same content at S/M.

## 1. Kind by kind

| X4 kind (styles · option · sizes) | S3 equivalent | Data the S3 needs | Status |
|---|---|---|---|
| clock (big, flip, analog, analog_min, words, lunar · 12 h setting · 2×1…4×4) | clock (digital, big, flip, analog, roman, minimal, station, words, lcd, lunar · flags 12, sec · S M L) | device clock | had it (more styles) |
| date (sheet, compact · 2×2, 2×1) | date (sheet, compact · S M L) | lunar maths | had it |
| month (lunar, solar · 4×3, 4×4, 3×3) | cal style month (solar grid; L adds the lunar days and holidays) + lunar style month (lunar grid) | lunar maths | had it |
| events (one look · 4×2, 4×3, 2×2) | cal styles today, events, week | `agenda` | had it |
| countdown (tet, date · opt `YYYY-MM-DD|label`) | count (tet, date, holiday · same option) | lunar maths | had it |
| weather (now, hourly, days) | weather (now, hourly, days, detail, sun) | `weather` | had it (more styles) |
| **reading** (cover, text) | **reading — Đang đọc** (cover, text · S M L) | `reading.books[0]`, `reading.today/goal/streak` | **new** |
| **stats** (ring, week) | **stats — Thống kê đọc** (ring, week · S M L) | `reading.today/goal/streak/best/week` | **new** |
| news "Báo sáng" (headlines of the morning issue) | news (list, photo, one · option section) | `news` | had it |
| **books** (covers, list · opt `s` skip the book being read) | **books — Thư viện sách** (covers, list · flag `s` Bỏ cuốn đang đọc · S M L) | `reading.books` | **new** |
| photo (one or many pictures, random / in turn / fixed, each opening / hour / day; e-ink fit and ink options) | photo (cover, fit, frame · one picture + caption) | `s3.photos` | partly: see §3 |
| **gold** (codes · 2×2, 4×1, 4×2, 4×3: buy, sell, change) | markets mixed every market in one list; **gold — Giá vàng** (table, tiles · codes `gold:…` · S M L) | `gold` items c, n, b, s, d | **new** |
| **stocks** (codes: index and tickers, price and %) | **stocks — Chứng khoán** (table, tiles · codes `stocks:…`) | `stocks` items s, p, pc | **new** |
| **fx** (codes: buy and sell, Vietcombank) | **fx — Tỷ giá** (table, tiles · codes `fx:…`) | `fx` items c, b, s | **new** |
| **crypto** (coins: USD, đồng, 24 h) | **crypto — Tiền số** (table, tiles · codes `crypto:…`) | `crypto` items s, p, v, pc | **new** |
| notify (the server's notes) | notify (the device's Notification Center) | device | had it |
| battery (battery and last sync · 2×1) | system (battery, WiFi, sync, version · S M) | device | had it |
| app (8 app shortcuts, Home only) | Home app icons and the dock (every app) | device | had it (native) |
| **overview** (time · date and lunar · weather; tall: next event, battery · 4×1, 4×2) | **overview — Tổng quan** (one look · M L; L adds the next hours and three events, the sync time) | `weather`, `agenda`, device | **new** |
| pkg (a declarative widget package, X4 Lite) | none | the Decl renderer | not this round: see §3 |
| setup (link prompt) | the S3 has its own pairing screens | — | not a widget there |

S3-only kinds the X4 does not have: lunar (can chi, detail, lunar month), quote (ca dao), world clocks, alarm,
reminders, notes, my widgets, PawBoop.

Frame looks: the X4's screen skins (card, line, bare, bold, shadow) and per-widget invert match the S3's per-widget
skins (theme, dark, light, glass, none, 12 colours); every new kind takes them.

## 2. What was built

- Device: `src/wid/read.cpp` (reading, stats, books, overview), `src/wid/market.cpp` wQuotes (gold, stocks, fx,
  crypto), registry entries in `src/wid.cpp` (with `wid::marketDefault`), the one-market code picker in
  `src/widopts.cpp` (only that market's codes, its defaults picked), the gallery tab "Đọc sách" in `src/home.cpp`.
  `src/wid/kit.h` helpers take C strings now (the String forms forward), which also shrank the other families.
  The overview is redrawn every minute (`live`); the others when a new dashboard arrives (os.cpp rebuilds Home).
- Web: `web/app/src/os/wid/read.ts`, `os/wid/market.ts` (line by line with the C++), `os/wid.ts` and `os/model.ts`
  registry, `pages/OsMarketCodes.tsx` (`only`), `pages/OsWidgetOptions.tsx`, `pages/OsHome.tsx` (tab Đọc sách).
- Server: `build_dashboard` adds `reading` to a colour desk's dashboard only (X4 answers unchanged, checked), and
  `s3MarketOpts` reads the new kinds' codes. `reading` = `{today, goal, streak, best, week[7], books:[{t, a?, p?, d}]}`:
  seconds today (the S3's time zone), `read.goal_min` of the person's readers (30), streaks as the web's Stats page
  counts them, the last 7 days' seconds oldest first, the 8 books read lately (newest first; `p` only for EspOS books,
  which report progress; `a` from the books table). No table or column changed. Deploy: `web/scripts/deploy-s3widgets.sh`.
- Covers: the server has no pictures of the reader's books (the X4 makes its thumbnails from the EPUB on its card), so
  the S3 draws a cover: a colour picked by the title (8 colours), a hinge line, the title in white or its first letter.

## 3. Left out on purpose

- **Widget packages (`pkg`)**: the X4 draws them with its Decl renderer (src/espos/Decl*, +60 KB on the X4). The S3
  has no port of it and about 14 KB of the app slot left; it belongs with the S3 package runtime work
  (docs/profiles.md says faces and widgets come to the S3 later).
- **Photo in turn**: the X4's photo widget can change picture every opening, hour or day. The S3 shows full-screen
  pictures pinned by Home (≈300 KB of PSRAM each, released only when Home is rebuilt), so changing them every hour
  would pile pinned pictures up; it needs a photo cache change first (photo.cpp).
- **One-row sizes**: the S3 grid has S, M and L only; a 4×1 size would touch Home's layout, both editors and the saver.
