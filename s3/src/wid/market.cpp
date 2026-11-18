// The markets family (docs/widgets.md §3): rows of quotes picked by the option's "kind:code" list (list), or
// the first code big with the others beside or under it (big). web/app/src/os/wid/market.ts draws the same.
#include <cmath>

#include "../data.h"
#include "../desk.h"
#include "../fonts.h"
#include "kit.h"

namespace wid {

using namespace kit;

const std::vector<Style> MARKETS_STYLES = {{"list", N_("List")}, {"big", N_("One big quote")}};
const std::vector<Style> PRICE_STYLES = {{"table", N_("Price table")}, {"tiles", N_("Big tiles")}};

namespace {

// A separator: the own dark line, or the skin's dim colour faded.
lv_obj_t* rule(lv_obj_t* p, uint32_t line, int x, int y, int w, int h) {
  lv_obj_t* s = box(p, line, 0);
  if (!SKIN.own) lv_obj_set_style_bg_opa(s, 60, 0);
  lv_obj_set_size(s, w, h);
  lv_obj_set_pos(s, x, y);
  return s;
}

struct Row {
  String name, value, change;
  uint32_t color;
  String full, unit;
  double pct;
};

std::vector<Row> marketRows(const String& opt, bool onDark) {
  // opt: "stocks:VNINDEX,gold:SJC,..." (default: VN-Index, SJC, HNX, USD...)
  const String def = "stocks:VNINDEX,gold:SJC,stocks:HNX,fx:USD,stocks:VN30,gold:N24K,crypto:BTC,stocks:UPCOM,crypto:ETH,fx:EUR";
  std::vector<Row> out;
  for (const String& tok : data::codesOf(opt, def.c_str())) {
    const int colon = tok.indexOf(':');
    if (colon < 0) continue;
    const String k = tok.substring(0, colon), code = tok.substring(colon + 1);
    for (const auto& q : data::quotes(k.c_str(), code)) {
      String nm = q.code == "VNINDEX" ? "VNI" : q.code;
      if (k == "gold") nm = q.code == "N24K" ? String(T_("Ring")) : q.code;  // i18n: ring gold (short, a column)
      out.push_back({nm, q.value, q.change, onDark ? q.color : data::trendColor(q.pct, false), q.name, q.unit, q.pct});
    }
  }
  return out;
}

}  // namespace

void wMarkets(lv_obj_t* parent, const Item& it, Card c) {
  // The own card is dark; a skin brings its text colours (and the trend colours that read on it).
  const bool onDark = SKIN.own || ((SKIN.fg >> 16) & 255) > 128;
  const uint32_t fg = onOwn(WHITE), dim = onOwnDim(0x94a3b8), val = onOwnDim(0xcbd5e1), line = onOwnDim(0x1f2937);
  const auto rows_ = marketRows(it.opt, onDark);
  const bool big = styleOf(it) == "big";
  // The big look's heading is its first code's full name.
  if (!big || rows_.empty()) cap(c.o, N_("Markets"), dim);
  if (rows_.empty()) return empty(c, desk::dash().isNull() ? noData() : N_("No prices yet"), dim, 22);
  if (big) {
    // The first code big: its full name, value, change and unit.
    const Row& r = rows_[0];
    const int bw = it.size == M ? 150 : c.w;
    lv_obj_t* hd = label(c.o, "", 12, true, dim);
    fitText(hd, upperVi(r.full).c_str(), bw, 16, 12, 12, true);
    lv_obj_set_style_text_letter_space(hd, 1, 0);
    lv_obj_set_pos(hd, 0, 0);
    fit(c.o, r.value, -1, 18, bw, 40, it.size == L ? 34 : 28, 18, true, fg);
    const int cy = it.size == L ? 62 : 58;
    if (r.change.length()) at(c.o, r.change, 0, cy, 18, true, r.color);
    at(c.o, r.unit, 0, it.size == L ? 88 : c.h - 16, 12, false, dim);
    if (it.size == S) return;
    if (it.size == M) {
      // The next codes beside it.
      rule(c.o, line, bw + 8, 0, 1, c.h);
      const int x = bw + 20;
      for (int i = 1; i < (int)rows_.size() && i < 4; i++) {
        const int y = 4 + (i - 1) * 38;
        at(c.o, rows_[i].name, x, y, 13, true, fg);
        right(c.o, rows_[i].change.length() ? rows_[i].change : rows_[i].value, y, 13, true,
              rows_[i].change.length() ? rows_[i].color : fg);
        at(c.o, rows_[i].value, x, y + 17, 12, false, val);
      }
      return;
    }
    // Large: the others under it, as the list draws them.
    int y = 112;
    for (int i = 1; i < (int)rows_.size(); i++) {
      const Row& q = rows_[i];
      if (y + 24 > c.h) break;
      at(c.o, q.name, 0, y, 15, true, fg);
      lv_obj_t* v = label(c.o, q.value.c_str(), 15, false, onOwnDim(0xe2e8f0));
      lv_obj_align(v, LV_ALIGN_TOP_RIGHT, -84, y);
      right(c.o, q.change, y, 14, true, q.color);
      rule(c.o, line, 0, y - 6, c.w, 1);
      y += 27;
    }
    return;
  }
  if (it.size == S) {
    int y = 24;
    for (const Row& r : rows_) {
      if (y + 18 > c.h) break;
      at(c.o, r.name, 0, y, 14, false, fg);
      right(c.o, r.change.length() ? r.change : r.value, y, 14, true, r.change.length() ? r.color : fg);
      y += 25;
    }
    return;
  }
  if (it.size == M) {
    const int colW = (c.w - 12) / 2;
    for (int i = 0; i < (int)rows_.size() && i < 6; i++) {
      const Row& r = rows_[i];
      const int x = (i / 3) * (colW + 12), y = 22 + (i % 3) * 32;
      at(c.o, r.name, x, y, 13, true, fg);
      lv_obj_t* v = label(c.o, r.value.c_str(), 13, false, val);
      lv_obj_set_pos(v, x, y + 15);
      lv_obj_t* ch = label(c.o, r.change.c_str(), 12, true, r.color);
      lv_obj_set_width(ch, colW);
      lv_obj_set_style_text_align(ch, LV_TEXT_ALIGN_RIGHT, 0);
      lv_obj_set_pos(ch, x, y + 1);
    }
    return;
  }
  int y = 24;
  for (const Row& r : rows_) {
    if (y + 24 > c.h) break;
    at(c.o, r.name, 0, y, 15, true, fg);
    lv_obj_t* v = label(c.o, r.value.c_str(), 15, false, onOwnDim(0xe2e8f0));
    lv_obj_align(v, LV_ALIGN_TOP_RIGHT, -84, y);
    right(c.o, r.change, y, 14, true, r.color);
    rule(c.o, line, 0, y + 22, c.w, 1);
    y += 27;
  }
}

#pragma GCC push_options
#pragma GCC optimize("Os")  // built when a page is made (Home shows snapshots), never per frame: small over fast
// ---------------------------------------------------------------- gold, stocks, fx, crypto (the e-reader's four)
// One kind per market, its codes from the option ("gold:SJC,gold:N24K", that market's tokens only; empty = the
// defaults the server always sends): table (every column of that market) or tiles (a big number each).
String marketDefault(const String& kind) {
  if (kind == "gold") return "gold:SJC,gold:N24K";
  if (kind == "stocks") return "stocks:VNINDEX,stocks:VN30,stocks:HNX";
  if (kind == "fx") return "fx:USD,fx:EUR,fx:JPY,fx:CNY";
  return kind == "crypto" ? "crypto:BTC,crypto:ETH" : "";
}

namespace {

struct QRow {
  String name, main, buy, sub, chg;  // main: sell price, index or price; buy: gold, fx; sub: crypto in đồng
  double pct;
  const String* cell[3];  // the table's cells after the name (cells())
};
const char* const NO = "—";  // a picked code the server has not sent yet
// 1.72 bn / 48.7 mn / 950,000 (đồng)
String vnd(double v) {
  if (v >= 1e9) return i18n::fmt(N_("{0} bn"), data::num(v / 1e9, 2));  // i18n: billion (đồng), short
  if (v >= 1e6) return i18n::fmt(N_("{0} mn"), data::num(v / 1e6, 1));  // i18n: million (đồng), short
  return data::num(v, 0);
}
// A number field of a quote (def when it is not there): one lookup function for every field.
__attribute__((noinline)) double field(JsonObjectConst x, const char* k, double def) {
  JsonVariantConst v = x[k];
  return v.isNull() ? def : v.as<double>();
}
std::vector<QRow> qrows(const String& kind, const String& opt) {
  JsonArrayConst items = desk::dash()[kind.c_str()]["items"];
  const bool gold = kind == "gold", fx = kind == "fx", coin = kind == "crypto";
  const char* key = gold || fx ? "c" : "s";
  std::vector<QRow> out;
  for (const String& tok : data::codesOf(opt, marketDefault(kind).c_str())) {
    if (!tok.startsWith(kind + ":")) continue;
    const String code = tok.substring(kind.length() + 1);
    out.push_back({code, NO, "", "", "", 0, {}});
    QRow& r = out.back();
    if (gold && code == "N24K") r.name = T_("Ring 9999");  // i18n: 24-carat ring gold
    if (kind == "stocks" && code == "VNINDEX") r.name = "VN-Index";
    JsonObjectConst x;
    for (JsonObjectConst q : items)
      if (code == (q[key] | "")) {
        x = q;
        break;
      }
    if (x.isNull()) continue;
    const double sv = field(x, "s", 0), bv = field(x, "b", NAN), dv = field(x, "d", NAN), pv = field(x, "p", 0),
                 pc = field(x, "pc", 0), vv = field(x, "v", NAN);
    if (gold || fx) {
      const char* nm = x["n"] | "";
      if (*nm) r.name = nm;
      r.main = data::num(sv, gold ? 1 : sv < 1000 ? 2 : 0);
      if (!isnan(bv)) r.buy = data::num(bv, gold ? 1 : bv < 1000 ? 2 : 0);
      if (gold && !isnan(dv)) r.chg = data::signedNum(dv, 1), r.pct = dv;
      continue;
    }
    r.main = coin ? "$" + data::num(pv, pv >= 1000 ? 0 : pv >= 1 ? 2 : 4) : data::num(pv, pv >= 1000 ? 1 : 2);
    if (coin && !isnan(vv)) r.sub = vnd(vv);
    r.chg = data::signedNum(pc, 2, "%");
    r.pct = pc;
  }
  return out;
}
// Marked (N_): the helpers translate them.
const char* title(const String& k) {
  return k == "gold" ? N_("Gold prices") : k == "stocks" ? N_("Stocks") : k == "fx" ? N_("Exchange rates") : N_("Crypto");
}
const char* unit(const String& k) {
  return k == "gold" ? N_("Million dong per tael")
         : k == "stocks" ? N_("Indexes in points, shares in thousand dong")
         : k == "fx" ? N_("Dong, Vietcombank rates")
                     : N_("US dollars, and in dong");
}
// The table's columns after the name: heading and right edge (x in a 276 px wide card).
struct Col {
  const char* head;
  int edge;
};
// i18n: Buy, Sell, Price: column headings of a price table (short)
const Col GOLD_COLS[3] = {{N_("Buy"), 148}, {N_("Sell"), 214}, {"±", 276}},
          COIN_COLS[3] = {{"USD", 130}, {"VND", 210}, {"24h", 276}}, FX_COLS[2] = {{N_("Buy"), 184}, {N_("Sell"), 276}},
          STOCK_COLS[2] = {{N_("Price"), 184}, {"±%", 276}};
const Col* columns(const String& k, int& n) {
  n = k == "gold" || k == "crypto" ? 3 : 2;
  return k == "gold" ? GOLD_COLS : k == "crypto" ? COIN_COLS : k == "fx" ? FX_COLS : STOCK_COLS;
}
// The row's cells in the columns' order (a missing buy or đồng price is "—").
void cells(const String& k, QRow& r) {
  static const String no(NO);
  const String* buy = r.buy.length() ? &r.buy : &no;
  if (k == "gold") r.cell[0] = buy, r.cell[1] = &r.main, r.cell[2] = &r.chg;
  else if (k == "crypto") r.cell[0] = &r.main, r.cell[1] = r.sub.length() ? &r.sub : &no, r.cell[2] = &r.chg;
  else if (k == "fx") r.cell[0] = buy, r.cell[1] = &r.main;
  else r.cell[0] = &r.main, r.cell[1] = &r.chg;
}
// Which cell is the main price (bold) and which the change (coloured, -1 none).
int mainCell(const String& k) { return k == "gold" || k == "fx" ? 1 : 0; }
int chgCell(const String& k) { return k == "fx" ? -1 : k == "stocks" ? 1 : 2; }
// The line under a big number: what else that market says about it.
String subLine(const String& k, const QRow& r) {
  if (k == "gold" || k == "fx") return r.buy.length() ? i18n::fmt(N_("Buy {0}"), r.buy) : String("");
  return k == "crypto" && r.sub.length() ? i18n::fmt(N_("~{0} dong"), r.sub) : String("");
}

// A price in the big digit fonts (px 28 and up have digits only: a leading "$" is drawn at 24 beside them), or fitted
// at 24 and down when it does not fit w or is not a number.
void price(lv_obj_t* p, const String& s, int x, int y, int w, int px, uint32_t color) {
  const String lead = s.startsWith("$") ? "$" : "", digits = s.substring(lead.length());
  const int lw = lead.length() ? textW(lead.c_str(), 24, true) : 0;
  if (s == NO || px < 28 || lw + textW(digits.c_str(), px, true) > w) {
    fit(p, s, x, y, w, lineH(24, true), min(px, 24), 13, true, color, 1);
    return;
  }
  if (lead.length()) at(p, lead, x, y + lineH(px, true) - lineH(24, true) - 4, 24, true, color);
  at(p, digits, x + lw, y, px, true, color);
}

}  // namespace

void wQuotes(lv_obj_t*, const Item& it, Card c) {
  lv_obj_t* p = c.o;
  const String& kind = it.kind;
  const bool onDark = SKIN.own || ((SKIN.fg >> 16) & 255) > 128;
  const uint32_t fg = onOwn(WHITE), dim = onOwnDim(0x94a3b8), line = onOwnDim(0x1f2937);
  auto rows = qrows(kind, it.opt);
  bool any = false;
  for (QRow& r : rows) cells(kind, r), any |= r.main != NO;
  // No price for any picked code yet: nothing to list (as the X4 says it).
  const int n = any ? rows.size() : 0;
  const bool tiles = styleOf(it) == "tiles";
  const bool none = desk::dash().isNull();
  const String at_ = desk::dash()[kind.c_str()]["at"] | "";
  auto colorOf = [&](const QRow& r) { return r.chg.length() ? data::trendColor(r.pct, onDark) : fg; };
  if (!tiles || !n || none) {
    cap(p, title(kind), dim);
    // The time of the prices beside the title only when both fit (a small "Stocks" may have no room for it).
    const String up = upperVi(T_(title(kind)));
    const int capW = lv_text_get_width(up.c_str(), up.length(), fonts::get(12, true), 1);
    if (at_.length() && !tiles && capW + 8 + textW(at_.c_str(), 12) <= c.w) right(p, at_, 0, 12, false, dim);
  }
  if (!n || none) return empty(c, none ? noData() : N_("No prices yet"), dim, 22);
  if (tiles) {
    const uint32_t tileBg = SKIN.own ? 0x1c2433 : lv_color_to_u32(lv_color_mix(C(SKIN.fg), C(PAPER), 30)) & 0xffffff;
    // A code's tile: its name, the price as big as the tile allows, the change and what else there is.
    auto tile = [&](int x, int y, int w, int h, const QRow& r) {
      lv_obj_t* b = box(p, tileBg, 12);
      lv_obj_set_size(b, w, h);
      lv_obj_set_pos(b, x, y);
      const int chW = r.chg.length() ? textW(r.chg.c_str(), 12, true) : 0;
      if (h < 70) {
        fit(p, r.name, x + 10, y + 8, w - 28 - chW, 16, 12, 12, true, dim, 1);
        if (r.chg.length()) at(p, r.chg, x + w - 10 - chW, y + 8, 12, true, colorOf(r));
        fit(p, r.main, x + 10, y + 24, w - 20, 30, 20, 13, true, fg, 1);
        return;
      }
      const int big = h >= 110 ? (w >= 200 ? 44 : w >= 120 ? 34 : 24) : 24;
      fit(p, r.name, x + 10, y + 10, w - 20, 16, 12, 12, true, dim, 1);
      price(p, r.main, x + 10, y + 28, w - 20, big, fg);
      const int ly = y + 30 + lineH(big, true);
      const String sub = subLine(kind, r);
      const int cw = r.chg.length() ? textW(r.chg.c_str(), 13, true) : 0;
      if (r.chg.length()) at(p, r.chg, x + 10, ly, 13, true, colorOf(r));
      if (!sub.length()) return;
      const int sw = textW(sub.c_str(), 12);
      const int ch = r.chg.length() ? lineH(13, true) : 0;
      if (r.chg.length() && cw + 10 + sw <= w - 20) at(p, sub, x + w - 10 - sw, ly + 1, 12, false, dim);
      else if (ly + ch + lineH(12, false) <= y + h - 6) fit(p, sub, x + 10, ly + ch, w - 20, lineH(12, false), 12, 12, false, dim, 1);
    };
    if (it.size == S) {
      if (n == 1) {
        // One code: the whole card is its tile.
        const QRow& r = rows[0];
        cap(p, r.name.c_str(), dim);
        price(p, r.main, -1, 18, c.w, 28, fg);
        if (r.chg.length()) at(p, r.chg, 0, 60, 16, true, colorOf(r));
        const String sub = subLine(kind, r);
        if (sub.length()) fit(p, sub, 0, c.h - lineH(12, false), c.w, lineH(12, false), 12, 12, false, dim, 1);
        return;
      }
      for (int i = 0; i < 2; i++) tile(-6, -4 + i * 63, c.w + 12, 59, rows[i]);
      return;
    }
    if (it.size == M) {
      const int m = min(n, 3);
      const int tw = (c.w - 8 * (m - 1)) / m;
      for (int i = 0; i < m; i++) tile(i * (tw + 8), 0, tw, c.h, rows[i]);
      return;
    }
    // Large: one or two wide tiles, two by two, or two by three.
    const int per = n <= 2 ? 1 : 2, m = min(n, n <= 4 ? 4 : 6);
    const int tw = (c.w - 8 * (per - 1)) / per, lines = (m + per - 1) / per, th = (c.h - 8 * (lines - 1)) / lines;
    for (int i = 0; i < m; i++) tile((i % per) * (tw + 8), (i / per) * (th + 8), tw, th, rows[i]);
    return;
  }
  int nc;
  const Col* cols = columns(kind, nc);
  const int mc = mainCell(kind), cc = chgCell(kind);
  if (it.size == S) {
    if (n == 1) {
      // One code: its name, then each column as a line (Buy, Sell, ±).
      const QRow& r = rows[0];
      fit(p, r.name, 0, 20, c.w, 20, 15, 12, true, fg, 1);
      for (int i = 0; i < nc; i++) {
        const int y = 46 + i * 24;
        at(p, cols[i].head, 0, y, 13, false, dim);
        right(p, *r.cell[i], y - 1, 15, i == mc, i == cc ? colorOf(r) : fg);
      }
      return;
    }
    // Name and the main price, coloured by its change.
    const int step = min(36, max(24, 94 / n)), px = step >= 32 ? 16 : 14;
    int y = 24;
    for (const QRow& r : rows) {
      if (y + lineH(px, false) > c.h) break;
      const int vw = textW(r.main.c_str(), px, true);
      fit(p, r.name, 0, y, c.w - vw - 6, lineH(px, false), px, 12, false, fg, 1);
      right(p, r.main, y, px, true, colorOf(r));
      y += step;
    }
    return;
  }
  // The table: the rows share the height (taller rows and bigger type for a few codes), every column right-aligned
  // at its edge; the type shrinks until no column runs into the one before it.
  const bool lg = it.size == L;
  const int y0 = lg ? 42 : 36, bottom = lg ? c.h - 20 : c.h;
  const int step = lg ? min(52, max(26, (bottom - y0) / n)) : min(40, max(20, (bottom - y0) / n));
  int px = step >= 44 ? 20 : step >= 34 ? 18 : step >= 26 ? 16 : 14;
  if (nc >= 3 && px > 18) px = 18;
  for (; px > 12; px--) {
    bool ok = true;
    for (int i = 0; i < nc; i++) {
      int mw = 0;
      for (const QRow& r : rows) mw = max(mw, textW(r.cell[i]->c_str(), px, i == mc));
      if (cols[i].edge - mw < (i ? cols[i - 1].edge : 44) + 6) ok = false;
    }
    if (ok) break;
  }
  int first = 0;
  for (const QRow& r : rows) first = max(first, textW(r.cell[0]->c_str(), px, mc == 0));
  const int nameW = cols[0].edge - first - 10;
  for (int i = 0; i < nc; i++) right(p, cols[i].head, y0 - 18, 12, false, dim, cols[i].edge - c.w);
  int y = y0;
  for (const QRow& r : rows) {
    if (y + lineH(px, false) > bottom) break;
    fit(p, r.name, 0, y, nameW, lineH(px, false), px, 12, true, fg, 1);
    for (int i = 0; i < nc; i++) right(p, *r.cell[i], y, px, i == mc, i == cc ? colorOf(r) : fg, cols[i].edge - c.w);
    if (lg) rule(p, line, 0, y + step - 5, c.w, 1);
    y += step;
  }
  if (lg) at(p, unit(kind), 0, c.h - lineH(12, false), 12, false, dim);
}

#pragma GCC pop_options

}  // namespace wid
