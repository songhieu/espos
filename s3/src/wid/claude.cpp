#pragma GCC optimize("Os")  // built when a page is made (Home shows snapshots), never per frame: small over fast
// The Claude widget (2026-11-21, owner: "ko thấy widget claude để add vào"): the Claude Code usage of the person's
// Mac, which EspOS Desktop (desktop) sends to the server and build_dashboard hands the colour desk as
// `claude` (s3/docs/desktop-protocol.md §2.1 usage): the 5-hour and weekly windows with their reset times (only
// when the Mac's status line helper is installed), tokens today and their API-price estimate, the last 7 days, the
// Claude sessions open and waiting for the person. Three looks: bars, gauge (style id ring), today; S M L.
// web/app/src/os/wid/claude.ts draws the same: change both.
// Later (docs/app-slots.md): a candidate for a declarative widget package fed by the same dashboard key.
#include <ArduinoJson.h>

#include <initializer_list>

#include "../data.h"
#include "../desk.h"
#include "kit.h"

namespace wid {

const std::vector<Style> CLAUDE_STYLES = {{"bars", N_("Bars")}, {"ring", N_("Gauge")}, {"today", N_("Today")}};

namespace {

using namespace kit;

constexpr uint32_t TERRA = 0xd97757, WARN = 0xff9500, FULL = 0xff3b30, WAITC = 0xc2410c;

uint32_t mix(uint32_t a, uint32_t b, uint8_t k) { return lv_color_to_u32(lv_color_mix(C(a), C(b), k)) & 0xffffff; }
uint32_t sep() { return mix(GREY, PAPER, 64); }
uint32_t acc() { return SKIN.own ? TERRA : GREY; }
// The fill of a usage bar: Claude's terracotta, orange from 75 %, red from 90 %.
uint32_t level(int pct) { return pct >= 90 ? FULL : pct >= 75 ? WARN : TERRA; }

void rect(lv_obj_t* p, int x, int y, int w, int h, uint32_t color, int r = 0) {
  lv_obj_t* o = box(p, color, r);
  lv_obj_set_size(o, w, h);
  lv_obj_set_pos(o, x, y);
}
void mid(lv_obj_t* p, const char* text, int x, int y, int w, int px, bool bold, uint32_t color) {
  at(p, text, x + (w - textW(text, px, bold)) / 2, y, px, bold, color);
}
void bar(lv_obj_t* p, int x, int y, int w, int h, int pct, uint32_t color) {
  rect(p, x, y, w, h, sep(), h / 2);
  if (pct > 0) rect(p, x, y, max(h, w * min(100, pct) / 100), h, color, h / 2);
}
// An arc from the top, clockwise, over an empty track (the reading ring's twin).
void ring(lv_obj_t* p, int x, int y, int d, int wd, int pct, uint32_t color) {
  lv_obj_t* a = lv_arc_create(p);
  lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(a, d, d);
  lv_obj_set_pos(a, x, y);
  lv_arc_set_rotation(a, 270);
  lv_arc_set_bg_angles(a, 0, 360);
  const int end = 360 * min(100, max(0, pct)) / 100;
  lv_arc_set_angles(a, 0, end);
  lv_obj_set_style_arc_width(a, wd, LV_PART_MAIN);
  lv_obj_set_style_arc_width(a, wd, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(a, C(sep()), LV_PART_MAIN);
  lv_obj_set_style_arc_color(a, C(color), LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(a, end < 360, LV_PART_INDICATOR);
  if (!end) lv_obj_set_style_arc_opa(a, LV_OPA_TRANSP, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, LV_PART_KNOB);
  lv_obj_set_style_pad_all(a, 0, LV_PART_KNOB);
  lv_obj_set_style_pad_all(a, 0, 0);
  lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, 0);
}

struct Win {
  bool ok = false;
  int pct = 0;
  String at;   // "16:20" today, "Mon 09:00" later
  String day;  // "16:20" today, "Mon" later
};
struct U {
  bool has = false, online = false;
  String name, seen;  // seen: "14:05" today, else "20/11"
  Win five, week;
  long long tokens = -1;
  double usd = -1;
  long long days[7] = {0, 0, 0, 0, 0, 0, 0};
  int nd = 0;
  int waiting = 0, open = 0;
};
String when(JsonObjectConst w) {
  const char* hm = w["hm"] | "";
  const int dd = w["dd"] | 0, wd = w["wd"] | 0;
  if (!*hm) return "";
  if (dd == 0) return hm;
  return String(WD_SHORT[(wd % 7 + 7) % 7]) + " " + hm;
}
Win winOf(JsonObjectConst o) {
  Win w;
  if (o.isNull() || o["pct"].isNull()) return w;
  w.ok = true;
  w.pct = max(0, min(100, (int)lround(o["pct"].as<double>())));
  JsonObjectConst a = o["at"];
  w.at = when(a);
  w.day = (a["dd"] | 0) == 0 ? w.at : String(WD_SHORT[((a["wd"] | 0) % 7 + 7) % 7]);
  return w;
}
U usageOf() {
  U u;
  JsonObjectConst o = desk::dash()["claude"];
  u.has = !o.isNull();
  if (!u.has) return u;
  u.online = o["online"] | false;
  u.name = o["name"] | "";
  JsonObjectConst s = o["seen"];
  if (!s.isNull()) u.seen = (s["dd"] | 0) == 0 ? String(s["hm"] | "") : String(s["dm"] | "");
  u.five = winOf(o["five"]);
  u.week = winOf(o["week"]);
  if (!o["tokens"].isNull()) u.tokens = o["tokens"].as<long long>();
  if (!o["usd"].isNull()) u.usd = o["usd"].as<double>();
  for (JsonVariantConst v : o["days"].as<JsonArrayConst>())
    if (u.nd < 7) u.days[u.nd++] = v.as<long long>();
  u.waiting = o["waiting"] | 0;
  u.open = o["sessions"] | 0;
  return u;
}
// 2.4M, 812K, 950 (the language's separators; the pack spells the units).
String tok(long long n) {
  if (n < 1000) return String((long)n);
  if (n < 1000000) return i18n::fmt(N_("{0}K"), data::num(n / 1e3, n < 10000 ? 1 : 0));
  if (n < 1000000000) return i18n::fmt(N_("{0}M"), data::num(n / 1e6, n < 100000000 ? 1 : 0));
  return i18n::fmt(N_("{0}B"), data::num(n / 1e9, 1));
}
String pctOf(int p) { return String(p) + "%"; }
String resets(const Win& w) { return w.at.length() ? i18n::fmt(N_("Resets {0}"), w.at) : String(); }
// The one status line: offline, sessions waiting (orange), sessions open, or nothing.
String status(const U& u, uint32_t& color) {
  color = GREY;
  if (!u.online) return u.seen.length() ? i18n::fmt(N_("Mac offline since {0}"), u.seen) : String(T_("Mac offline"));
  if (u.waiting > 0) {
    color = onOwn(WAITC);
    return i18n::plural(u.waiting, NP_("{n} session waiting|{n} sessions waiting"));
  }
  if (u.open > 0) return i18n::plural(u.open, NP_("{n} session open|{n} sessions open"));
  return "";
}
// The first letter of a UTF-8 text.
String first(const char* s) {
  int n = 1;
  const uint8_t c = (uint8_t)s[0];
  if (c >= 0xF0) n = 4;
  else if (c >= 0xE0) n = 3;
  else if (c >= 0xC0) n = 2;
  return String(s).substring(0, n);
}
// The status on one line in w x h: the long form, else (offline) the short one.
void statusIn(lv_obj_t* p, const U& u, int x, int y, int w, int h) {
  uint32_t sc;
  const String st = status(u, sc);
  if (!st.length()) return;
  const bool full = u.online || textW(st.c_str(), 12, true) <= w;
  fit(p, full ? st.c_str() : T_("Mac offline"), x, y, w, h, 12, 12, true, sc, 0);
}
// The status (else the Mac's name) at the top right, when it leaves the heading room.
void headRight(lv_obj_t* p, const U& u, Card c) {
  uint32_t sc;
  const String st = status(u, sc);
  String head = st.length() ? st : u.name;
  if (st.length() && !u.online && textW(head.c_str(), 12, true) > c.w - 70) head = T_("Mac offline");
  if (head.length() && textW(head.c_str(), 12, true) <= c.w - 70) right(p, head.c_str(), 0, 12, true, st.length() ? sc : GREY);
}
// The last 7 days' tokens as columns (today last, in the accent), the weekdays under them.
void days(lv_obj_t* p, const U& u, int x, int y, int w, int h, bool values) {
  const int labH = lineH(12, false), base = y + h - labH - 2;
  const int gap = w >= 200 ? 10 : 5, bw = (w - 6 * gap) / 7;
  long long top = 1;
  for (int i = 0; i < u.nd; i++) top = max(top, u.days[i]);
  top += top / 8;
  const int barsH = base - y - (values ? labH + 2 : 0);
  const int wd0 = data::today().wday, off = 7 - u.nd;  // fewer than 7 days: today stays last
  bool whole = true;  // the weekdays' short names fit under their columns, else their first letters
  for (int i = 0; i < 7; i++) whole = whole && textW(WD_SHORT[i], 12, true) <= bw + gap - 2;
  for (int i = 0; i < 7; i++) {
    const int bx = x + i * (bw + gap);
    const long long v = i >= off ? u.days[i - off] : 0;
    const int bh = v > 0 ? max(3, (int)(v * barsH / top)) : 0;
    if (bh) rect(p, bx, base - bh, bw, bh, i == 6 ? acc() : mix(acc(), PAPER, 110), min(4, bw / 2));
    else rect(p, bx, base - 2, bw, 2, sep());
    const char* wd = WD_SHORT[(wd0 + i + 1) % 7];
    const String lab = whole ? String(wd) : first(wd);
    mid(p, lab.c_str(), bx, base + 2, bw, 12, i == 6, i == 6 ? INK : GREY);
    if (values && v > 0) {
      const String s = tok(v);
      if (textW(s.c_str(), 12, true) <= bw + gap) mid(p, s.c_str(), bx - gap / 2, base - bh - labH - 1, bw + gap, 12, true, GREY);
    }
  }
}
// One window as a row: name left, percent right, the bar, the reset time under it.
void row(lv_obj_t* p, const char* name, const Win& w, int x, int y, int wd, int px, int barH, bool withReset) {
  at(p, name, x, y + px - 12, 12, false, GREY);
  const String pc = pctOf(w.pct);
  at(p, pc.c_str(), x + wd - textW(pc.c_str(), px, true), y, px, true, INK);
  const int by = y + lineH(px, true) + 2;
  bar(p, x, by, wd, barH, w.pct, level(w.pct));
  if (withReset) {
    const String r = resets(w);
    if (r.length()) at(p, r.c_str(), x, by + barH + 4, 12, false, GREY);
  }
}
// No window figures (the status line helper is not on the Mac): tokens today, then how to get the limits (the
// short line when the long one does not fit whole).
void noLimits(lv_obj_t* p, const U& u, int x, int y, int w, int h) {
  int yy = y;
  if (u.tokens >= 0) {
    const String t = tok(u.tokens);
    fit(p, t.c_str(), x, yy, w, lineH(24, true), 24, 18, true, INK, 1);
    yy += lineH(24, true);
    at(p, N_("tokens today"), x, yy, 12, false, GREY);
    yy += lineH(12, false) + 2;
  }
  if (!fitIn(p, N_("Limits unknown: add the status line helper on your Mac"), x, yy, w, y + h - yy, 13, 12, false, GREY, 2))
    fitIn(p, T_("Limits unknown"), x, yy, w, y + h - yy, 13, 12, false, GREY, 2);
}
// A tile of the large ring look: a name, a value, what it counts (the first of `units` that fits one line).
void tile(lv_obj_t* p, int x, int y, int w, int h, const char* name, const char* value, std::initializer_list<const char*> units,
          uint32_t tint) {
  rect(p, x, y, w, h, mix(tint, PAPER, 36), 14);
  cap(p, name, GREY, x + 10, y + 10, w - 20);
  fit(p, value, x + 10, y + 28, w - 20, 26, 22, 12, true, INK, 1);
  for (const char* unit : units)
    if (*unit && fitIn(p, unit, x + 10, y + h - 10 - lineH(12, false), w - 20, lineH(12, false), 12, 12, false, GREY, 0)) break;
}
// The windows the Mac sent, in order (the 5-hour one first), with their names.
int present(const U& u, const Win* ws[2], const char* names[2]) {
  int n = 0;
  if (u.five.ok) ws[n] = &u.five, names[n++] = T_("5-hour");
  if (u.week.ok) ws[n] = &u.week, names[n++] = T_("Week");
  return n;
}

void bars(lv_obj_t* p, const Item& it, Card c, const U& u) {
  const Win* ws[2];
  const char* names[2];
  const int n = present(u, ws, names);
  if (it.size == S) {
    cap(p, "Claude", acc());
    if (!n) return noLimits(p, u, 0, 20, c.w, c.h - 20);
    for (int i = 0; i < n; i++) {  // no room for a reset line: the reset time beside the name when it fits
      String name = names[i];
      const String with = name + " · " + ws[i]->at;
      if (ws[i]->at.length() && textW(with.c_str(), 12, false) + textW(pctOf(ws[i]->pct).c_str(), 16, true) + 8 <= c.w) name = with;
      row(p, name.c_str(), *ws[i], 0, 18 + i * 46, c.w, 16, 6, false);
    }
    statusIn(p, u, 0, 100, c.w, 18);
    return;
  }
  if (it.size == M) {
    cap(p, "Claude", acc());
    headRight(p, u, c);
    if (!n) return noLimits(p, u, 0, 22, c.w, c.h - 22);
    const int cw = (c.w - 16) / 2;
    for (int i = 0; i < n; i++) {
      const int x = i * (cw + 16);
      at(p, names[i], x, 22, 12, false, GREY);
      const String pc = pctOf(ws[i]->pct);
      at(p, pc.c_str(), x, 38, 34, true, INK);
      bar(p, x, 80, cw, 8, ws[i]->pct, level(ws[i]->pct));
      const String r = resets(*ws[i]);
      if (r.length()) fit(p, r.c_str(), x, 94, cw, 18, 12, 12, false, GREY, 0);
    }
    return;
  }
  cap(p, "Claude", acc());
  headRight(p, u, c);
  if (!n) noLimits(p, u, 0, 24, c.w, 130);
  for (int i = 0; i < n; i++) row(p, names[i], *ws[i], 0, 24 + i * 68, c.w, 22, 10, true);
  rect(p, 0, 158, c.w, 1, sep());
  days(p, u, 0, 166, c.w, 62, false);
  at(p, T_("Today"), 0, 236, 13, false, GREY);
  if (u.tokens >= 0) {
    String v = i18n::fmt(N_("{0} tokens"), tok(u.tokens));
    if (u.usd > 0) v += " · ~$" + data::num(u.usd, 2);
    right(p, v.c_str(), 236, 14, true, INK);
  }
}

void ringLook(lv_obj_t* p, const Item& it, Card c, const U& u) {
  uint32_t sc;
  const String st = status(u, sc);
  const bool lim = u.five.ok;
  const String pc = lim ? pctOf(u.five.pct) : String("--");
  if (it.size == S) {
    const int d = 96, x0 = (c.w - d) / 2;
    ring(p, x0, 0, d, 10, lim ? u.five.pct : 0, lim ? level(u.five.pct) : sep());
    const int nh = lineH(28, true), uh = lineH(12, false), ny = 48 - (nh + uh) / 2;
    mid(p, pc.c_str(), 0, ny, c.w, 28, true, lim ? INK : GREY);
    mid(p, T_("5-hour"), 0, ny + nh, c.w, 12, false, GREY);
    if (st.length()) statusIn(p, u, 0, 101, c.w, 17);
    else {
      const String foot = lim ? resets(u.five) : String(T_("Limits unknown"));
      if (foot.length()) fit(p, foot.c_str(), 0, 101, c.w, 17, 12, 12, true, GREY, 0);
    }
    return;
  }
  if (it.size == M) {
    ring(p, 0, 0, 118, 12, lim ? u.five.pct : 0, lim ? level(u.five.pct) : sep());
    const int nh = lineH(34, true), uh = lineH(12, false), ny = 59 - (nh + uh) / 2;
    mid(p, pc.c_str(), 0, ny, 118, 34, true, lim ? INK : GREY);
    mid(p, T_("5-hour"), 0, ny + nh, 118, 12, false, GREY);
    const int x = 134, w = c.w - x;
    const String wk = u.week.ok ? pctOf(u.week.pct) : String(T_("Unknown"));
    const String rs = lim && u.five.at.length() ? u.five.at : String("·");
    const String stv = st.length() ? st : String(T_("No session"));
    const char* const rows[3][2] = {{T_("Week"), wk.c_str()}, {T_("5-hour resets"), rs.c_str()}, {T_("Sessions"), stv.c_str()}};
    for (int i = 0; i < 3; i++) {
      cap(p, rows[i][0], GREY, x, i * 40, w);
      fit(p, rows[i][1], x, i * 40 + 16, w, 22, 18, 12, true, i == 2 && st.length() ? sc : INK, 1);
    }
    return;
  }
  const int d = 170;
  ring(p, (c.w - d) / 2, 0, d, 16, lim ? u.five.pct : 0, lim ? level(u.five.pct) : sep());
  // the window's name, and its reset time under it when that fits inside the ring
  const String rs = lim ? resets(u.five) : String();
  const bool two = rs.length() && textW(rs.c_str(), 14, false) <= 96;
  const int nh = lineH(56, true), uh = lineH(14, false), ny = 85 - (nh + uh * (two ? 2 : 1)) / 2;
  mid(p, pc.c_str(), 0, ny, c.w, 56, true, lim ? INK : GREY);
  mid(p, T_("5-hour"), 0, ny + nh, c.w, 14, false, GREY);
  if (two) mid(p, rs.c_str(), 0, ny + nh + uh, c.w, 14, false, GREY);
  const int tw = (c.w - 16) / 3;
  const String wk = u.week.ok ? pctOf(u.week.pct) : String("--");
  const String tk = u.tokens >= 0 ? tok(u.tokens) : String("--");
  const String wt = String(u.waiting);
  const String r1 = resets(u.week), r2 = u.week.day.length() ? i18n::fmt(N_("Resets {0}"), u.week.day) : String();
  if (u.week.ok) tile(p, 0, 184, tw, 82, T_("Week"), wk.c_str(), {r1.c_str(), r2.c_str(), u.week.at.c_str()}, TERRA);
  else tile(p, 0, 184, tw, 82, T_("Week"), wk.c_str(), {T_("Unknown")}, TERRA);
  tile(p, tw + 8, 184, tw, 82, T_("Today"), tk.c_str(), {T_("tokens")}, 0x0a84ff);
  if (u.online) tile(p, 2 * (tw + 8), 184, tw, 82, T_("Waiting"), wt.c_str(), {T_("sessions")}, WAITC);
  else tile(p, 2 * (tw + 8), 184, tw, 82, "Mac", T_("Off"), {u.seen.c_str()}, WAITC);
}

void today(lv_obj_t* p, const Item& it, Card c, const U& u) {
  uint32_t sc;
  const String st = status(u, sc);
  const String t = u.tokens >= 0 ? tok(u.tokens) : String("0");
  const String usd = u.usd > 0 ? "~$" + data::num(u.usd, 2) : String();
  if (it.size == S) {
    cap(p, T_("Claude today"), acc());
    fit(p, t.c_str(), 0, 20, c.w, 40, 34, 18, true, INK, 1);
    at(p, T_("tokens"), 0, 62, 12, false, GREY);
    if (usd.length()) at(p, usd.c_str(), 0, 80, 14, true, INK);
    statusIn(p, u, 0, 100, c.w, 18);
    return;
  }
  if (it.size == M) {
    const int lw = 120;
    cap(p, T_("Claude today"), acc(), 0, 0, lw);
    fit(p, t.c_str(), 0, 20, lw, 40, 34, 18, true, INK, 1);
    at(p, T_("tokens"), 0, 62, 12, false, GREY);
    const String usdLong = usd + " " + T_("at API prices");
    if (usd.length()) fit(p, textW(usdLong.c_str(), 12, false) <= lw ? usdLong : usd, 0, 80, lw, 17, 12, 12, false, GREY, 0);
    statusIn(p, u, 0, 100, lw, 18);
    days(p, u, 136, 14, c.w - 136, c.h - 14, false);
    return;
  }
  cap(p, T_("Claude · 7 days"), acc());
  if (u.name.length()) right(p, u.name.c_str(), 0, 12, true, GREY);
  days(p, u, 0, 22, c.w, 152, true);
  rect(p, 0, 184, c.w, 1, sep());
  String ses = i18n::plural(u.open, NP_("{n} session open|{n} sessions open"));
  if (u.waiting > 0) ses += " · " + i18n::plural(u.waiting, NP_("{n} waiting|{n} waiting"));
  const String tk = i18n::fmt(N_("{0} tokens"), t);
  const char* const lines[3][2] = {{T_("Today"), tk.c_str()},
                                   {T_("At API prices"), usd.length() ? usd.c_str() : "·"},
                                   {T_("Sessions"), u.online ? ses.c_str() : st.c_str()}};
  for (int i = 0; i < 3; i++) {
    const int y = 194 + i * 24;
    at(p, lines[i][0], 0, y, 13, false, GREY);
    right(p, lines[i][1], y, 14, true, i == 2 && (u.waiting > 0 || !u.online) ? sc : INK);
  }
}

}  // namespace

void wClaude(lv_obj_t*, const Item& it, Card c) {
  lv_obj_t* p = c.o;
  const U u = usageOf();
  if (!u.has) {
    cap(p, "Claude", acc());
    return empty(c, desk::dash().isNull() ? noData() : N_("Pair EspOS Desktop on your Mac to see Claude usage"), GREY, 22);
  }
  const String s = styleOf(it);
  if (s == "ring") ringLook(p, it, c, u);
  else if (s == "today") today(p, it, c, u);
  else bars(p, it, c, u);
}

}  // namespace wid
