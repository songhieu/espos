# Translating EspOS (S3 firmware)

EspOS is the system of a small desk device: a 3.5" colour touch screen, 320x480 portrait (also shrunk to a 2.8"
screen), with a Home screen of widgets (clock, weather, calendar, lunar calendar, news, markets), apps (Settings,
Store, Weather, Calendar, Clock, Notes, Reminders, Photos...), a screen saver, a store of apps, and a pet game
(PawBoop: Biscuit is the dog, "she"). The texts were written in Vietnamese first, then in English, which is the
source now. docs/i18n.md has the technical side.

## Files

- Source: `i18n/en.json`. Each key is the English text. Its value tells:
  - `kind`: `text`, `plural` (forms, see below), `list` (items in `en`, `count` of them), `meta`;
  - `where`: the source lines that show it (`src/file.cpp:123`; read the line when the meaning is unclear);
  - `notes`: notes for translators.
  - A key `ctx\u0004text` has a context before `\u0004`: translate only the text after it (the context tells
    which meaning; the same English elsewhere may need another word).
- Yours: `i18n/<tag>.json`, `{ "<key exactly as in en.json>": "<translation>", ... }`, every key of en.json
  except the `meta` ones (you may set `"@num"` and `"@plural"` only if the defaults below are wrong).
- Check: `python3 tools/i18n.py status` must show your language with
  0 errors, 0 missing, 0 unused.

## Rules

1. **Short.** The screen is small and many texts sit in fixed boxes (buttons, tabs, list rows, the names under
   Home icons ~11 characters). Be as short as the English or shorter, natural and idiomatic like the phone UI
   in your language (iOS conventions). Never use "…" (U+2026) anywhere; the device never cuts text.
2. **Placeholders stay**: `{0}` `{1}` `{2}` `{n}` may move within the sentence; printf parts (`%d` `%s` `%u`
   `%02d` `%.1f` `%%` ...) must stay in the same order. Keep `\n` line breaks (same number).
3. **Dates**: `{d}` day number, `{m}` month number, `{y}` year, `{M}` short month name, `{MM}` month name, `{w}`
   short weekday, `{W}` weekday. Write the pattern your language uses; you may change `{M}` to `{m}`, `{W}` to `{w}`
   and the order (e.g. "{W}, {M} {d}" -> de "{W}, {d}. {M}", ja "{m}月{d}日({w})", vi "{W} {d}/{m}").
4. **Plurals** (`kind: plural`, English "one|other"): give your language's forms joined by `|`:
   - one form: vi, zh-Hans, ja, ko, id (e.g. "{n}个应用");
   - one|other: de, es (n = 1 / else);
   - one|other where 0 and 1 are "one": fr, pt-BR;
   - one|few|many: ru (1, 21.. / 2-4, 22-24.. / else).
5. **Lists** (`kind: list`): the items joined by `|`, exactly `count` of them, in order:
   - `@weekdays` from Sunday; `@weekdays_short` (3 letters or your usual short form); `@weekdays_min` (1-2
     letters, month grid heads); `@months`, `@months_short`, `@months_spelled` (the month names as written out in a
     long date);
   - `@stems` (10 heavenly stems) and `@branches` (12 earthly branches): zh-Hans/ja 甲乙丙... and 子丑寅...,
     ko 갑을병... and 자축인...; other languages: the English approach (element: Wood Wood Fire Fire Earth Earth
     Metal Metal Water Water; branch: the zodiac animal) in your language; `canchi\u0004{0} {1}` is how one stem and
     one branch combine ("Fire Horse", zh "{0}{1}" for 丙午);
   - `@solar_terms`: the 24 solar terms from the spring equinox, by their usual names in your language (zh 春分 清明…,
     ja 春分 清明…, ko 춘분 청명…);
   - `@number_words`: 0-59 written out as words, as a "time in words" clock reads them;
   - `@quotes`: NOT a translation: 40-60 well-known, short sayings or proverbs of your language (any count).
6. **Names stay**: EspOS, PawBoop, Biscuit, Telex, VN-Index, SJC, Bitcoin, city names, app ids.
7. **Letters**: Latin-script languages may use ASCII and Latin-1 only (U+0020-U+007E, U+00A0-U+00FF): no œ, no „,
   no special spaces but U+00A0; use « » or "..." for quotes. zh-Hans, ja, ko, ru: your script, plus ASCII and
   Latin-1; full-width punctuation is fine for CJK. The check refuses letters the device's fonts lack.
8. Vietnamese holidays and lunar-calendar words: translate naturally (Tết = Lunar New Year; zh 春节, ja 旧正月,
   ko 설날).
9. Defaults (only override with a reason): `@num` group+decimal separators: en ",." vi/es/de/pt-BR/id ".,",
   fr/ru " ,", zh/ja/ko ",."; `@plural`: as in rule 4.

## Core terms

Decide your terms for these first and use them everywhere (iOS wording of your language where it exists):
Settings, Home (screen), App Library, Store, app, widget, Install, Remove (uninstall), Move, Update, Device (where
apps are stored, vs the) Memory card, Storage, Add-ons, Notifications, Notification Center, Control Center, Sync,
Link to EspOS (the web account), Screen saver, Wallpaper, Appearance, Brightness, Volume, Do Not Disturb, Quiet
hours, Weather, Calendar, Lunar (calendar), News, Markets, Notes, Reminders, Clock, Alarm, Calculator, Photos,
Done, Cancel, Save, Edit, Delete, Add, Close, Try again, Today, Tomorrow, Downloading, Loading.
