# S3 widgets, full set (2026-11-16) — the shared plan

Owner's words: "trên firmware của máy S3 thiếu các loại đồng hồ cổ điển và rất nhiều thứ khác, làm ơn handle hết
dùm… cả 1 cái lịch ngày lịch âm rồi đủ thứ, handle đi handle đi"; earlier: "custom sâu… như bên máy đọc sách, rà kỹ
và làm custom được cái nhỏ nhất luôn". Goal: every widget of the X4 (in colour) and more, every option editable on
the web (Màn chính and Màn chờ) and on the device (Home edit mode and the saver editor), web preview = device pixels.

## 2. The item

Home (`s3.home`): `["w", kind, x, y, size, style, opt, open, skin]` (index 8 new, optional).
Saver (`s3.saver_pages`): `{"t":"w","k":kind,"s":size,"st":style,"op":opt,"sk":skin}` (`sk` new, optional).
`wid::Item` gains `String skin`. Unknown style or option = the kind's default, never an error.

`skin` (card look, every kind): `""` theme default, `dark`, `light`, `glass` (see-through over the wallpaper; costly on
Home pages, fine on the saver), `c:rrggbb` a solid colour with automatic text colour, `none` (no card, text straight on
the background; for the saver).

`wid.h` exposes the lists the editors show (web and device read the same names):
`struct Style {const char* id; const char* name;}`; `struct Kind` gains `std::vector<Style> styles` and an option
descriptor (`opt`: none | codes | note | photo | mine | cities | section | date | todo) so the device can build its
option sheets from the registry.

## 3. The set (style ids; first one is the default)

| kind | name | sizes | styles | opt |
|---|---|---|---|---|
| clock | Đồng hồ | S M L | `digital` Số, `big` Số lớn, `center` Số giữa, `stack` Hai tầng, `flip` Lật; 17 analog faces (§5): `analog` Kim cổ điển, `roman` La Mã, `minimal` Kim tối giản, `station` Đồng hồ ga, `arabic` Số Ả Rập, `sticks` Vạch mảnh, `gold` Vàng sang trọng, `wood` Gỗ ấm, `bauhaus` Bauhaus, `chrono` Bấm giờ, `outline` Trong suốt, `pilot` Phi công, `kids` Trẻ em, `night` Đêm đỏ, `datewin` Có ô ngày, `wxface` Kèm thời tiết, `diver` Thợ lặn; `words` Chữ, `lcd` Điện tử 7 đoạn, `lunar` Số và âm lịch | `12` = 12 giờ; `sec` = kim/số giây |
| world | Giờ thế giới | S M | `list` Danh sách, `analog` Mặt kim nhỏ, `big` Giờ lớn | cities (from s3.cities or its own list) |
| alarm | Báo thức | S M | `next` Báo thức tới, `list` Tất cả | — |
| date | Lịch ngày | S M L | `sheet` Tờ lịch (tháng đỏ, ngày to, thứ, âm lịch, ngày/tháng/năm can chi; L: giờ hoàng đạo, tiết khí, câu ca dao), `compact` Gọn, `block` Ngày lớn | — |
| cal | Lịch | S M L | `today` Hôm nay (as now), `events` Sự kiện, `week` Tuần, `month` Tháng (có số âm lịch nhỏ) | — |
| lunar | Âm lịch | S M L | `canchi` Can chi (as now), `detail` Chi tiết (tiết khí, giờ hoàng đạo), `month` Lịch âm tháng, `big` Ngày âm lớn | — |
| count | Đếm ngày | S M | `tet` Tết, `date` Ngày tự chọn, `holiday` Ngày lễ tới | date: `YYYY-MM-DD|nhãn` |
| quote | Ca dao, tục ngữ | M L | `day` Mỗi ngày một câu, `big` Chữ lớn | — (built-in list, ~120 câu) |
| weather | Thời tiết | S M L | `now` Hiện tại, `hourly` Theo giờ, `days` Các ngày, `detail` Chi tiết (ẩm, cảm giác, gió, UV, AQI), `sun` Mặt trời, `big` Nhiệt độ lớn | city (from the device's cities) |
| markets | Thị trường | S M L | `list` Danh sách, `big` Một mã lớn | codes (Phase A picker) |
| news | Tin mới | S M L | `list` Tin đầu, `photo` Có ảnh, `one` Một tin lớn | section |
| remind | Nhắc việc | S M L | `open` Việc chưa xong, `all` Cả việc xong | — |
| notes | Ghi chú | S M | `note` | note id |
| photo | Ảnh | S M L | `cover` Lấp đầy, `fit` Vừa khung, `frame` Khung ảnh có chú thích | photo id |
| my | Widget của tôi | S M L | — | mine id |
| pet | PawBoop | S M L | as now | — |
| notify | Thông báo | M L | `latest` Mới nhất | — |
| system | Máy | S M | `status` Pin, WiFi, đồng bộ | — |
| gold, stocks, fx, crypto | Giá vàng, Chứng khoán, Tỷ giá, Tiền số (the X4's four, 2026-11-17) | S M L | `table` Bảng giá, `tiles` Ô lớn | codes of that market only (`gold:SJC,…`; empty = `wid::marketDefault`) |
| reading | Đang đọc | S M L | `cover` Có bìa, `text` Chỉ chữ | — (dashboard `reading`, colour desk only) |
| stats | Thống kê đọc | S M L | `ring` Vòng mục tiêu, `week` 7 ngày | — |
| books | Thư viện sách | S M L | `covers` Có bìa, `list` Danh sách | flag `s` Bỏ cuốn đang đọc |
| overview | Tổng quan | M L | one look (time, date, weather, events, battery) | — |

The last five rows: docs/widgets-parity.md (X4 parity, 2026-11-17).

Design: each widget looks like a small piece of the matching S3 app (app_weather, app_calendar, app_clock, app_news,
app_markets), in colour, on the theme's cards; the classic clocks look like real clock faces (anti-aliased hands via
lv_line with round ends, ticks, numerals). Live parts (seconds, flip) tick through `wid::tick()` only where the
setting asks for seconds; Home pages stay snapshot-friendly.

Weather's city heading (2026-11-18, a long name such as "Ho Chi Minh City" wrapped over the temperature): `cap()`
takes the room it may use and `capFits()` says whether it stays on one line (letters closing up to -1 px). Rows with a
value on the right (hourly/detail S, hourly/days/detail M) keep the first value that leaves the city one line
("28° · Nhiều mây", then "28°"), else the city alone, else the city wrapped beside the last value; "now" S puts the
city beside the icon, else on the whole row with the icon down beside the temperature, else wrapped with the
temperature under it at 44/34/28 px; the looks with the 56 px icon move the icon down the same way. Whatever sits
under a wrapped heading moves down and leaves out what then no longer fits (rows, a min/rain line, tiles); "now" S
gives too long conditions two lines in place of the high/low. Web: `os/wid/weather.ts` and `kit.ts` (`cap`,
`capFits`), the same steps.

## 4. Per-page backgrounds (2026-11-16, owner: "chọn hình nền cho từng màn ngay trong trình sửa của màn đó")

`s3.home` v2 may carry `"bg"`, an array aligned with `"pages"`: each entry is null (the look's wallpaper) or the
saver's page bg object (`docs/saver.md` §2: black / color / grad / wall with optional `w` / photo, plus `dim`).
Trailing nulls are dropped; the key is left out when all are null. The device keeps it through edits (add, delete,
move, trim pages) and draws each page's own background at rest, in its swipe picture and behind the dots and dock.
Web: v57. Device: notif. PSRAM: identical backgrounds share one picture; at most the shown page and its
neighbours are kept painted.

## 4b. Landscape Home pages (2026-11-18, owner: "make landscape firmware for everything") In landscape Home has its own pages, the setting `s3.home_l`:
`{"v":1,"pages":[[entries as in s3.home]],"bg":[page backgrounds as in s3.home §4]}`. The dock and the App Library's
order stay in `s3.home` (shared). Grid 5 x 4 cells of 78 x 72 from (4, 24), so the 20 cells and every widget's box
(S 144x138, M 300x138, L 300x286) are portrait's; a widget sits at its cell + (6, 3); in landscape a widget may start
at any column it fits in (portrait snaps them to the halves). The dock stands at (404, 27), 68 x 284; the dots under
the grid at y 313. Left of page 1 is StandBy (src/standby.cpp), where Today is in portrait; the App Library after the
last page, 5 columns.

While `s3.home_l` is empty or not readable (or after Settings › Home Screen › "Lay out landscape from portrait
again"), both the device (`home.cpp seed()`) and the web (`os/model.ts`) make the landscape pages from the portrait
ones by the same rule, so both write the same value; the first edit saves it:

```
for each portrait page P, in page order (stop once there are 8 landscape pages):
  append a new empty landscape page (its background: P's own, or none); first = its index
  items = P's widgets ordered by (y, then x), then P's apps ordered by (y, then x)
  for each item e (every field kept but x, y):
    for k = first .. the last page: if FIND(page k, e): append e to page k, next item
    else, with fewer than 8 pages: append a new empty page (P's background), FIND on it (always fits), append e
    else: drop e (an app stays in the App Library)
trailing empty pages go; none at all: one empty page
FIND(page, e): y = 0 .. 4 - rows(e), x = 0 .. 5 - cols(e) in steps of 1 (an app) or 2 (a widget): the first (x, y)
that overlaps nothing on the page (an app 1 x 1, S 2 x 2, M 4 x 2, L 4 x 4)
written as {"v":1,"pages":[...],"bg":[...]}: entries in the order they were appended, each as s3.home writes it; "bg"
up to the last page with its own background (null before it), left out when none
```

Today's defaults come out as two full landscape pages: clock M (0,0), calendar S (0,2), markets S (2,2), News,
Weather, Calendar, Photos down column 4; news M (0,0), note S (0,2), countdown S (2,2), Store, Lunar, Reminders,
Calculator down column 4. Photo backgrounds of landscape pages are fetched at 480x320 (photo.cpp wanted() through home::bgPhotos(): s3.home_l's when it has pages, else the portrait pages' ones, which the seeded landscape pages show; sim command `bgphotos` prints both lists).

## 5. Faces and posters (2026-11-17, owner: "nhiều kiểu đồng hồ kim vào… các cái khác cũng thế… đảm bảo ngon nhất")

- **Analog faces are data.** `tools/faces.py` holds one row of numbers per face (colours, rim, ticks, numerals, hands,
  extras: date window at 3, weather at 6, sub-dials at 9 and 3, diver's bezel, station disc; colour mode: as given,
  light/dark, or the card's text colours) and writes `src/wid/faces.h` and `web/app/src/os/wid/faces.ts`. One renderer
  draws them all: `face()` in `src/wid/clock.cpp` and in `clock.ts`. A new face is a new row (about 95 B of flash), not
  code. Edit faces.py and run it; never the generated files.
- **Posters** read across a room and suit the saver: `kit::poster()` (kit.ts `Card.poster`) centres lines, each as big
  as its size and the width allow (numbers in the big digit fonts, letters at most 24 px), stepping the biggest line
  down until the block fits; a line that fits at no size is left out. Styles built on it: clock `center`, `stack`,
  date `block`, lunar `big`, weather `big`, world `big` (quote `big` uses midFit).
- The server takes any style id (save_device_settings checks the JSON of `s3.home` and the shape of saver elements);
  `web/backend/fixtures/inputs/save_device_settings_s3styles.json` saves every new face and poster (200).

