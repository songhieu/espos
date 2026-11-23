# Custom saver (S3) — the shared plan

Owner's words (2026-11-16): the saver is a widget board like the e-reader's, "user được kéo thả widget cả trên mạch cả
trên web", "mục tiêu cho user custom mạnh nhất có thể", approved with "làm đẹp và nhẹ nhất có thể" (games will live on
the memory card later).

## 2. The setting `s3.saver_pages`

A JSON string, at most 8000 bytes, edited on the web (save_device_settings) and on the device
(`desk::setSetting`, synced up like any setting). The newest write wins. `s3.saver` (minutes before the saver)
and `dash.quiet_from` / `dash.quiet_to` (night) stay as they are.

```
{"v":1,
 "cycle":0,            seconds between the pages of one orientation: 0 (no change), 60, 300, 900
 "light":30,           saver backlight in percent by day, 5..100
 "night":"page",       in quiet hours: "page" = the night page, "off" = backlight off (a touch wakes), "day" = as by day
 "p":[PAGE, ...],      portrait pages, 1..3 (320 x 480)
 "l":[PAGE, ...],      landscape pages, 1..3 (480 x 320)
 "np":PAGE, "nl":PAGE  night pages (absent: the built-in red clock below)
}
PAGE = {"bg":BG, "e":[EL, ...]}          at most 24 elements, drawn in order (later ones on top)
BG   = {"t":"black"}
     | {"t":"color","c":"rrggbb"}
     | {"t":"grad","c":"rrggbb","c2":"rrggbb"}      top to bottom
     | {"t":"wall"}                                 the theme's wallpaper (theme::wallpaper)
     | {"t":"wall","w":"bien"}                      a built-in wallpaper by id (theme::wallpaperOf; added 2026-11-16)
     | {"t":"photo","id":N}                         a full-screen picture; id 0 = the s3.photos slideshow, one a minute
     | {"t":"tank"}                                 the user's aquarium, alive (Hồ cá cảnh, docs/tank.md; added 2026-11-19);
                                                    without the package a deep-water gradient
     plus "dim":0..90 on any of them: percent of black over it
EL   common keys: "t" type; "x","y" top left and "w","h" the box, in the page's pixels; "c":"rrggbb" colour
     (text, or the fill of a box; default e8ecf2); "o":10..100 opacity (default 100); "a":"l"|"c"|"r" (default c)
     "b":0|1 bold; "px": font size (0 or absent = the largest that fits the box)
```

| `t` | extra keys | shows |
|---|---|---|
| `time` | `f`: `24` 07:21, `24h1` 7:21, `12` 7:21 SA | the time |
| `date` | `f`: `wdm` Thứ Hai 16/11, `wdml` Thứ Hai 16/11 · 7/10 âm lịch, `wdmy` Thứ Hai 16/11/2026, `dm` 16/11, `wd` Thứ Hai, `full` Thứ Hai, 16 tháng 11, `wdu` THỨ HAI, `fullu` THỨ HAI, 16 THÁNG 11 (`wdu`, `fullu` added 2026-11-17: `wd`, `full` in capitals), `day` 16, `month` THÁNG MƯỜI MỘT | the date |
| `lunar` | `f`: `dm` 7/10 âm lịch, `full` Ngày 7 tháng 10 năm Ất Tỵ, `cc` Ất Tỵ | the lunar date |
| `text` | `s`: up to 80 characters | the user's words, wrapped inside the box, top aligned |
| `next` | — | Tiếp theo 08:30 · Khám răng (the next calendar event today; empty when none) |
| `alarm` | — | Báo thức 06:30 (empty when no alarm) |
| `temp` | `f`: `t` 29°, `td` 29° Nắng nhẹ, `d` Nắng nhẹ | the weather now |
| `box` | `r` corner radius | a filled rounded rectangle (`c` fill, `o` opacity); a line is a thin box |
| `photo` | `id`, `r`; slideshow (2026-11-17): `all`, `ids`, `iv`, `ord`, `tr`, `of` (below) | the picture `id`, rendered by the server at exactly `w`x`h` (`photo::wanted()` adds it, p `cj50`); or a slideshow |
| `w` | `k` kind, `s` S/M/L, `st` style, `op` option | a widget of `wid.cpp`; `w`,`h` follow its size: S 144x138, M 300x138, L 300x286 |

Text rules (the web draws them exactly like this, with the device's fonts and sizes from `fonts.h`):
- Fonts are Ubuntu: text sizes regular 12–16, 18, 20 and bold 12–18, 20, 22, 24; digits-only display sizes bold 28,
  34, 44, 56, 64, 96 (96 added 2026-11-16) and regular 40, 64, 84, 96 (letters in those come from the largest text size).
- FINAL (2026-11-16, the device code is the reference; the web copies it): sizes: `time` and `date` `day` choose
  from text and display sizes, the other types from text sizes only. `px` picks the nearest listed size (a tie takes
  the smaller); `px` 0 the largest; then step down to the first size that fits (line height ≤ h, and width ≤ w for
  one-line types; `text`: wrapped lines × line height ≤ h, no line space). If none fits: the smallest size; one-line
  types drop code points from the end until they fit (never "…"); `text` keeps the lines that fit, at least the first.
- Placement, exactly as desksaver.cpp draws it (C integer division; the web copies it):
  - one-line types (all but `text`): y = floor((h − lh) / 2); x = 0 for `a` "l", w − tw for "r",
    w/2 − tw/2 for "c" (each half rounded down);
  - `text`: wraps in `w`, top aligned; each line placed as an LVGL label does: centre (w − lineWidth) / 2,
    right w − lineWidth.
- Words: months THÁNG MỘT … THÁNG MƯỜI HAI; `next` is today's first "HH:MM" event at or after now; `time` `12`
  adds SA/CH; `lunar` is data::lunarText plus " âm lịch".
- `o` is the opacity of the whole element, widgets and photos included; widgets ignore `c`. Elements are clamped
  inside the page. Web v47 and desksaver.cpp were checked against each other on 2026-11-16.
- Colours of widgets are their own (as on Home). The dark/light card colours follow the theme as on Home.
- Text over pictures (2026-11-17), on every text type (`time` `date` `lunar` `text` `next` `alarm` `temp`):
  - `bg` "rrggbb": a band behind the text, a filled rounded rect over the element's whole box, radius `r` (0..h/2),
    opacity `bo` 10..100 (default 50). With a band the text lays out in the box inset 10 px left and right (`text`
    also 6 px top and bottom); every rule above then applies to that inner box. No band when the element shows nothing.
  - `sh` 1: a soft shadow: the same text in 000000 first at (+2,+2) 25 % opacity, then at (+1,+1) 45 %, then the text.
  - `o` still covers it all (band, shadow, text).

Photo slideshows (2026-11-17), on `photo` elements; all keys optional:
- `all` 1: the device's own pictures (`s3.photos`, in their order). Else `ids` [N, …]: 2..12 picture ids. `id` stays
  the first one (old firmware shows it).
- `iv` seconds per picture: 10, 30, 60, 300, 900 (default 60). `ord` "s" in order (default) | "r" shuffled. `tr` "c"
  cut (default) | "f" fade (400 ms, the new picture over the old one).
- The index shown at tick k = floor(epoch seconds / `iv`) (desksaver.cpp pickOf; the web's saver.ts slidePick is the
  same, line by line): in order (k + `of`) mod n; shuffled, a permutation of 0..n−1 per round of n ticks: x =
  (floor(k / n) × 2654435761 + 1) mod 2³², then for i = n−1 … 1: x ^= x << 13, x ^= x >> 17, x ^= x << 5 (32 bit),
  swap perm[i] and perm[x mod (i + 1)]; the index is perm[(k + `of`) mod n]. Either way, when it equals tick k−1's
  index (and n > 1, k > 0) the next one, (index + 1) mod n, is taken: never the same picture twice in a row.
- `of` 0..11: the element shows set[(k + `of`) mod n], k = floor(epoch seconds / `iv`): every frame changes on the
  same tick and a collage with `of` 0, 1, 2 shows consecutive pictures.
- Each picture is rendered at the element's `w`x`h` (p `cj50`, the web renders them when it saves). A missing render:
  that picture's full-screen render cropped to the box (as the photo widget does), else the next picture.
- At most 3 slideshow elements (`all` or 2+ `ids`) per page. Memory: per element at most the
  picture shown and the next one, decoded into PSRAM (the next ahead on its own task, the one before freed right after
  the change), never the whole set; the fade blends the two held pictures (no full-screen double buffer); PSRAM must not
  grow over 30 minutes of changes.
- "+ Chữ lên ảnh" in both editors: a `text` over the bottom of the selected photo: its `x` and `w`; `h` = min(56,
  max(24, photo h / 3)) (integer division); `y` = the photo's bottom − `h`; centred (no `a`); white, bold, `bg` 000000,
  `bo` 45, `r` = the photo's `r`.

Defaults (the setting empty or not valid JSON; also the web template "Bàn làm việc"). These boxes reproduce
today's desk saver under the text rules above (verified pixel against the old saver in the simulator by notif):
- `p`: `[{"bg":{"t":"black"},"e":[{"t":"time","x":0,"y":15,"w":320,"h":100},{"t":"date","f":"wdml","x":0,"y":110,"w":320,"h":24,"px":15,"c":"9aa3ad"},{"t":"w","k":"weather","s":"S","x":8,"y":172},{"t":"w","k":"cal","s":"S","x":168,"y":172},{"t":"w","k":"remind","s":"M","x":10,"y":322}]}]`
- `l`: `[{"bg":{"t":"black"},"e":[{"t":"time","x":0,"y":89,"w":320,"h":100},{"t":"date","f":"wdml","x":0,"y":186,"w":320,"h":24,"px":15,"c":"9aa3ad"},{"t":"w","k":"weather","s":"S","x":328,"y":16},{"t":"w","k":"cal","s":"S","x":328,"y":162}]}]`
- night (`np`, `nl`): black; `time` in `d92d20`, `alarm` px 15 in `8a1a12`. Portrait time 0,171,320,100 and alarm
  0,270,320,24; landscape time 0,111,320,100 and alarm 0,208,320,24 (centred in the left 320, as today);
  backlight 3 %.
- `cycle` 0, `light` 30, `night` "page".
- Old devices' `s3.saver_mode`: when `s3.saver_pages` is empty, "clock" means the "Tối giản" template and "photo"
  the "Khung ảnh" template with `id` 0; "desk" means the defaults. The web writes `s3.saver_mode` "desk" with every save.

Behaviour on the device: the saver shows the current orientation's page (cycling if `cycle` > 0), the night page in
quiet hours. Text updates once a minute, widgets every 10 minutes and when night starts or ends (as today); the whole
page moves 0–3 px each minute against burn-in. A long press (1 s) on the saver opens the editor; a short touch wakes.

## 3. The device editor

Long press 1 s on the saver, or Cài đặt › Màn chờ › Chỉnh màn chờ, opens the editor on the page shown, this orientation
only. The real page full screen, every element outlined. Top: Huỷ (asks first when something changed; nothing is
saved), ‹ Trang 1/2 › (a tap on the name: Thêm trang trống, Nhân đôi trang này, Xoá trang này; the night page follows
the day pages when it is one's own), Xong. Bottom: + Thêm (Home's widget gallery with a first tab 'Cơ bản' for time,
date, lunar, text, next, alarm, temp, box, photo), Mẫu (§5 file, drawn small by the device renderer), Cài đặt (Nền:
Home's page-background sheet plus 'Lần lượt' photos; daytime backlight `light`; the night choices of §5; `cycle` with
2+ pages). Drag to move (4 px steps, snaps to the centre and other elements' edges, always inside the screen); tap to
select, tap again to let go: a corner handle resizes (widgets jump between their sizes), a sheet gives Lên trên /
Xuống dưới / Xoá and the options (free elements: format, size or 'Vừa khung', colour, Mảnh/Đậm, alignment; widgets:
Home's rows). Xong, or 3 minutes untouched, saves `s3.saver_pages`.

Speed (2026-11-23, the owner: "kéo widget Claude rất lag" on an aquarium page). The editor is a child of the top
layer, over the saver, and LVGL draws the active screen and every child of the top layer for each piece it redraws:
Home, then the saver (its aquarium and widgets), then the editor. And the saver's aquarium went on making 5 pictures a
second behind the editor, each one a full-screen redraw. Now, while the editor is open:
- What it covers is hidden: the screen, and the top layer's children under it (`under()`). They are shown again when
  it closes.
- The aquarium holds still (`tank::onSaver`, docs/tank.md).

QEMU at the board's speed (`-icount shift=2`), dragging the Claude widget (M) on an aquarium page:

| | before | after |
|---|---|---|
| editor open, untouched | 7.1 redraws/s, CPU 35 % | 0 redraws/s, CPU 0.1 % |
| dragging | 22 redraws/s, CPU 91 %, render 37 ms each | ~60 redraws/s, CPU 64 %, render 10 ms each |

Moving a picture of the element (an `lv_snapshot` while dragging) was tried and dropped: blending the ARGB picture
cost more (15 ms) than drawing the widget (10 ms).

The same day, two more changes:
- The saver hides the screen under it while it is shown (`os.cpp` `coverUnder`). Every saver redraw (each aquarium
  picture, a widget's change) had drawn Home too. This is checked at every update, because the dog's pop-up shows
  Home again when it closes. `wake()` shows the screen again. Pop-ups, notifications and the saver's widgets are on
  the top layer above it, unchanged.
- The editor shows the page's aquarium as a still copy of the saver's last picture (`tank::saverBg`, `stillOf`),
  not the deep-water gradient. The water under it holds still while the editor is open.

QEMU at the board's speed, the saver with an aquarium page (clock, date, Claude widget), 5 aquarium pictures a second
either way:

| | before | after |
|---|---|---|
| CPU | 29 % | 16 % |
| LVGL render per redraw | 21.7 ms | 5.0 ms |
| the aquarium's own picture | 25.6 ms | 25.6 ms |

## 4. Flash room

The app slot is 3,932,160 B and the image was 3,919,536 B. The built-in art (`src/art.c`: app icons and weather
pictures at several sizes) is ~600 KB of raw pixels. Plan: store it deflated and inflate it once at boot into PSRAM
(the ESP32-S3 ROM has `tinfl`, so no inflate code is added; the host simulator uses zlib), keeping the same
`lv_image_dsc_t` names so no caller changes. Measure: image size, boot time added, PSRAM used. Fonts (~460 KB of
glyph bitmaps) may follow the same way if it measures well.

## 5. Editor v3 and the shared templates (2026-11-17)

The owner found the first editor unclear. In v3 the night is a
setting, the pages sit under the device, the widgets are the Home gallery's, and a template gallery replaces the old 7.
The format of §2 is unchanged. Orientation (changed 2026-11-18, the owner: "muốn setup màn hình dọc mà không phải quay
máy"): the web opens on the device's own orientation (`s3.rot` 0/2 portrait, 1/3 landscape, marked "máy đang dùng") and
a "Sửa trang: Dọc | Ngang" switch edits the other one; a template is applied by default to both orientations (this page
and the other orientation's page at the same place, or both night pages while the night is edited), or to this
orientation only.

Night, as both editors offer it (one orientation at a time; `k` is `np` portrait or `nl` landscape):

| Choice | Stored |
|---|---|
| Đồng hồ đỏ tối (default) | `"night":"page"` and no `k` (the built-in red clock of §2) |
| Tắt màn hình | `"night":"off"` (`k` is kept if it was there, unused) |
| Giống ban ngày | `"night":"day"` (`k` kept, unused) |
| Trang đêm riêng | `"night":"page"` and `k` (starting from the red clock, or the page the user had before) |

Templates: one file, `web/app/src/os/saver-templates.json`, imported by the web and published by
`web/scripts/deploy-saver.sh` as public storage `apps/saver-templates.json`
(`https://api.espos.dev/storage/bb/149/apps/saver-templates.json`):

```
{"v":1,
 "groups":[{"id":"clock","name":"Đồng hồ"}, ...],          clock, calendar, info, reading, photo, night (gallery order)
 "templates":[{"id":"desk","name":"Bàn làm việc","desc":"…","group":"info","look":"dark",
               "p":PAGE,"l":PAGE}, ...]}                    PAGE exactly as §2 (widgets without w/h), one per orientation
```

- Every template has a portrait and a landscape page; using one replaces the page being edited (or adds it as a new
  page, or replaces the night page while that is edited). `desk` is the §2 defaults; `night` is the built-in night page.
- Since 2026-11-17 (the owner: "mẫu xấu", "mấy mẫu desk", "khung ảnh gia đình", "đồng hồ kiểu kim và rất nhiều thứ
  nữa", "đủ kiểu"): 71 templates in 13 groups, in this order: desk 9 (after the S3's own desk and the X4's desk
  dashboard), photo 8 (family frames: full, mat, polaroid, 2 and 3 photo collages, photo + weather, a memory caption on
  a band, a slideshow), analog 16 (one face each, the faces of docs/widgets.md incl. gold, wood, bauhaus, chrono, pilot,
  diver, kids, datewin, wxface, outline), digital 8, calendar 6, weather 3, markets 3, reading 2, news 3, world 2,
  count 2, quote 3, night 6 (the device offers this group on the night page). Names ≤ 16 characters and descs ≤ 34
  (the device's tiles). The photo templates use the slideshow keys of §2. The file is about 62 KB (parse it in PSRAM).
- Widgets in templates set `sk` where the look must not follow the device's theme (a template keeps its colours).
- The web checks each page on every day of a year: inside the screen, no two elements overlapping (boxes under text
  excepted; `desk` and `night` are the device's own defaults), every text whole at its size (no cut letters, never "…").
  Every template was compared with the simulator (`make -C sim`): same drawing apart from anti-aliasing.
- Old devices keep working: the old `s3.saver_mode` looks ("clock", "photo") are not templates any more, the web keeps
  their JSON as it was for the legacy mapping of §2.
