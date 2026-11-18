# Settings parity: everything the web sets for an S3 is settable on the device

Owner (2026-11-16): "các mục như thời tiết này nó trên app cũng phải config được như trên web nhé, còn nhiều lắm à".

Rules:
- A device edit goes up with `desk::setSetting` and a sync right away (`desk::syncNow`). The newest change wins.
- The built-in lists are the web's, value for value: cities (`places.ts`), time zones (`TIME_ZONES`), news sections
  (`n2/NewsPage.tsx`), market quick sets (`os/model.ts`). a parity check outside this repository checks this (since
  2026-11-18 the device's names are English and compared through the Vietnamese language pack, i18n/vi.json).
- The settings pages that belong to an app live in that app and open from it. Settings has a shortcut to each
  (`app_settings::open` args `time`, `city`, `calendars`, `news`, `markets`, `photos`). They are declared in `src/appopts.h`.

## The table

"Server" = `device_sync` accepts the key from an S3 and sends it back in every answer's `settings` (live since
2026-11-16: device_sync v27, save_device_settings v23, save_news v2, update_device v2, device_firmware v3,
build_dashboard v16, market_tick v4; an X4 neither stores nor gets these keys).

| key | what it is | web place | device place |
|---|---|---|---|
| `s3.bright` | brightness 5–100 | Thiết bị › Cài đặt › Màn hình và âm thanh | Cài đặt › Màn hình; Trung tâm điều khiển |
| `s3.volume` | volume 0–100 | same group | Cài đặt › Âm thanh; Trung tâm điều khiển |
| `s3.rot` | orientation 0/2/1/3 | same group | Cài đặt › Màn hình |
| `s3.saver` | saver after 0/60/120/300/600 s | same group | Cài đặt › Màn chờ |
| `s3.saver_pages` (`s3.saver_mode` legacy) | the custom saver | Giao diện máy › Màn chờ | long press on the saver; Cài đặt › Màn chờ › Sửa màn chờ |
| `dash.quiet_from` / `dash.quiet_to` | night hours (night saver page, quiet notifications) | Màn hình và âm thanh › Ban đêm | **new:** Cài đặt › Thời gian và dữ liệu › Ban đêm Từ / Đến |
| `s3.click` | touch sound | Màn hình và âm thanh | Cài đặt › Âm thanh |
| `s3.dnd` | Do Not Disturb | Màn hình và âm thanh | Cài đặt › Âm thanh; Trung tâm điều khiển |
| `dash.interval_min` | data refresh 5/15/30/60 min | Màn hình và âm thanh › Lấy dữ liệu mới mỗi | **new:** Cài đặt › Thời gian và dữ liệu |
| `dash.city`, `dash.lat`, `dash.lon` | weather place (also sun times) | Thời tiết và lịch › Thành phố | **new:** Thời tiết › Đổi (or tap the city); Cài đặt › Thời tiết. **Server** |
| `dash.ics_url` | calendar links (≤ 5 shown) | Thời tiết và lịch › Lịch | **new:** Lịch › Lịch đã nối (add, show/hide, remove; the screen shows source and account, the link only after "Hiện link"); Cài đặt › Lịch đã nối. **Server** |
| `sys.tz_min` | time zone | Máy này › Múi giờ | **new:** Cài đặt › Thời gian và dữ liệu › Múi giờ (applied at once). **Server** |
| `sys.lang` | language (docs/i18n.md) | (the web's language setting) | **new 2026-11-18:** Settings › Language (downloads the language's packages first, then switches and sends `sys.lang`). **Server: to allow** |
| devices.name | device name | Máy này › Tên | Cài đặt › Tên máy: **new** sends `dev.name`. **Server** |
| devices.fw_auto | update by itself while charging | Máy này › Tự cập nhật lúc sạc | **new:** Cài đặt › Cập nhật › Tự cập nhật lúc sạc (`dev.fw_auto`). **Server** |
| devices.revoked | unlink | Máy này › Gỡ máy | Cài đặt › Tài khoản EspOS › Huỷ liên kết (forgets the token on the device; the web's row stays until removed there) |
| WiFi (`/v1/devices/:id/wifi` ops) | networks | Mạng | Cài đặt › WiFi (the device joins and forgets networks itself) |
| news_prefs.sections | followed news sections (S3 News app and widget, morning paper) | Tin tức › Chuyên mục | **new:** Tin tức › Sửa; Cài đặt › Tin tức (`news.sections`). **Server** (news_prefs is per user) |
| news_prefs.per_section | stories per section 3/5/10 | Tin tức › Số bài mỗi chuyên mục | **new:** same page (`news.per_section`); the S3 lists that many stories of a section. **Server** |
| news_prefs enabled, hour, images, keep | the X4's morning paper (a book) | Tin tức › Báo sáng | not on the S3 (it does not read the paper) |
| markets widget option (in `s3.home`, `s3.saver_pages`) | codes a markets widget lists | Giao diện máy › widget › Mã (MarketCodes) | widget sheet (Home edit, saver editor): **new** full picker: quick sets or codes one by one (gold, indices, typed stocks, rates, coins), as the web |
| `s3.markets` | **new:** codes the Markets app lists | none yet (web could reuse MarketCodes) | **new:** Thị trường › Sửa; Cài đặt › Thị trường. **Server** (new key, add its codes to the dashboard picks) |
| `s3.cities` | world clock cities | Giao diện máy › Giờ thế giới | Đồng hồ › Giờ thế giới (and the world widget's option) |
| `s3.home` | Home pages, widgets and their options | Giao diện máy › Màn chính | Home edit mode (long press), option sheets; Cài đặt › Màn chính (reset) |
| `s3.mywidgets` | my widgets | Giao diện máy › Widget của tôi | Home edit › a my-widget › Sửa nội dung |
| `s3.notes` | notes | Giao diện máy › Ghi chú | Ghi chú |
| `s3.todo` | reminders | Giao diện máy › Nhắc việc | Ghi chú › Nhắc việc |
| `s3.alarms` | alarms | Giao diện máy › Báo thức | Đồng hồ › Báo thức |
| `s3.theme`, `s3.theme_auto`, `s3.wall` | look, auto dark, wallpaper (the same 5 + light/dark walls) | Giao diện máy › Giao diện | Cài đặt › Giao diện |
| `s3.photos` | the device's pictures | Ảnh | **new:** Cài đặt › Ảnh trên máy: see and remove. Adding stays on the web: the browser renders the screen-size copies before it saves the list |
| `s3.pkgs` | apps wanted | Kho ứng dụng | Kho ứng dụng on the device |
| `s3.notif`, `s3.notif_preview` | notification switches, preview | Kho ứng dụng › Thông báo | Cài đặt › Thông báo |
| `s3.p1`, `s3.p2`, `s3.l1`, `s3.l2` | old widget pages (before `s3.home`) | none (legacy) | read only (pictures for old layouts) |
| `s3.apps` | old JS apps list | none | converted at boot (device only) |
| X4 keys (`sys.lang`, `sys.sleep_min`, `read.*`, `sleep.*`, `dash.layout`, `dash.blocks`, …) | the reader | X4 pages | not for the S3 |

Search: no place-search route on the server and no IP location, so the city page offers the web's built-in list:
69 Vietnamese cities plus the 13 world-clock cities, searched without accents. The web's "Nơi khác" searches
Open-Meteo straight from the browser. A city picked that way shows on the device as the current one; it is not in the
device's list.

## What the server does (live)

Accepts from an S3 in `POST /v1/device/sync` `settings` with the web's checks (a bad value is skipped and the stored
one comes back; newest write wins against each key's own timestamp), and always sends back in the answer's `settings`
(defaults when unset: Hà Nội 21.0245 105.8412, "", 420, the 6 default sections, 5):

- `dash.city` (≤ 60 chars), `dash.lat` (−90..90), `dash.lon` (−180..180): the device writes all three together.
- `dash.ics_url`: tokens separated by new lines, each `https://…`, `webcal://…`, or the same with `#` in front
  (hidden: kept, not fetched; `icsLinks` already skips them). Device limits: 5 shown, 10 in all, 4000 bytes. Drop
  the device's `ics_cache` row when it changes.
- `sys.tz_min`: −720..840, the web's values.
- `sys.lang`: "en" (default, also for empty or unknown), "vi", "zh-Hans", "es", "fr", "de", "ja", "ko", "pt-BR", "ru",
  "id" (`i18n/languages.json`). A device sends it only once the language is installed and shown.
- `news.sections` (comma list of the 16 slugs, at least one) and `news.per_section` (3|5|10): map them to `news_prefs`.
- `dev.name` (1–40 chars) → `devices.name`. `dev.fw_auto` (0|1) → `devices.fw_auto`; echo the current values.
- `s3.markets`: the widget-option format, ≤ 20 tokens, "" = built-in mix; add its codes to the dashboard picks.

