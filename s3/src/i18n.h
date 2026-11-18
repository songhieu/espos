#pragma once
// Languages (docs/i18n.md). English is built in: the text in the sources is English. Mark every piece the user reads:
//   T_("Settings")            translated where it is used (a call: the current language's text, or the English);
//   N_("Settings")            a mark only, for tables and for text handed straight to the helpers that translate
//                             text lying in the firmware image themselves (kit::label/setText/listRow/caption/hint/
//                             button/chips/fitText/wrapText/textW, the wid:: helpers, os::screen/navAction/toast,
//                             i18n::fmt/plural/date): never text made at run time (names, SSIDs, server text);
//   NP_("{n} app|{n} apps")   the plural forms for i18n::plural();
//   TC_("month", "May")       the same English with another meaning (a context); NC_(ctx, s) marks one for a
//                             translating helper (never print an NC_ string any other way: it carries its context).
// tools/i18n.py collects the marks into i18n/en.json; i18n/<tag>.json hold the translations. Every language but
// English is the package dev.espos.lang.<tag> (strings.bin, ~30 KB, kept in PSRAM or read in place from the
// flash), plus a font package when the built-in fonts lack its letters. The setting sys.lang picks one.
#include <Arduino.h>

#define N_(s) s
#define NP_(s) s
#define T_(s) i18n::tr(s)
#define TC_(ctx, s) i18n::tr(ctx "\x04" s)
#define NC_(ctx, s) ctx "\x04" s  // a mark with a context: only for text handed to the translating helpers

namespace i18n {

struct Lang {
  const char* tag;   // BCP 47: "en", "vi", "zh-Hans", "pt-BR"...
  const char* name;     // in its own language ("Tiếng Việt", "日本語")
  const char* english;  // in English (marked: T_ shows it in the language shown)
  const char* font;     // its font package, nullptr when the built-in fonts have its letters
};
extern const Lang LANGS[];
extern const int LANG_COUNT;
const Lang* find(const char* tag);
String packId(const char* tag);  // "dev.espos.lang.vi"

void begin();                     // after desk::begin(): the language of sys.lang, when its packages are in
void tick();                      // UI loop: follows sys.lang set from the web (installs the packages first)
bool installed(const char* tag);  // its packages are in (always for "en")
bool use(const char* tag);        // shows that language from now on (the caller rebuilds the screens)
const char* lang();               // the language shown: "en" until a pack is loaded
const char* wanted();             // sys.lang ("en" when unset)
bool switchReady();               // sys.lang changed and is loaded now: os rebuilds the screens when calm
void switchDone();
// Downloads for `tag` (font package first, then the language): false with a reason in `why`.
bool download(const char* tag, String& why);
// The download in progress for `tag`: 0..100, -1 when none (done or failed: installed() tells which).
int progress(const char* tag);

const char* tr(const char* en);  // the current language's text for `en`; the English when it has none
const char* trl(const char* s);  // tr() only for text in the firmware image (literals, tables); others as they are
bool inImage(const void* p);

// A list with English built in and the pack's "@<key>" (items separated by '|' in i18n/<tag>.json).
struct List {
  const char* key;
  const char* const* en;
  uint8_t n;
  const char* operator[](int i) const;
  int size() const;
};
extern const List WEEKDAY, WD_SHORT, MONTH, MON_SHORT;

// "{0}" "{1}" "{2}" of a pattern (translated when it is a literal) replaced by a, b, c.
String fmt(const char* pattern, const String& a, const String& b = String(), const String& c = String());
// The plural form of `forms` ("{n} app|{n} apps", English one|other; a pack gives its language's forms) for n,
// with {n} replaced.
String plural(int n, const char* forms);
// A date from a pattern (translated): {d} day, {m} month number, {M} short month name, {MM} month name, {y} year,
// {w} short weekday, {W} weekday. wd < 0: no weekday.
String date(const char* pattern, int d, int m, int y = 0, int wd = -1);
// Number separators of the language: "," and "." in English.
const char* groupSep();
const char* decSep();
// Upper case (ASCII, Latin-1, Vietnamese, Latin Extended-A, Cyrillic).
String upper(const String& s);

}  // namespace i18n
