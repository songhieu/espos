// Hồ cá cảnh: what lies over the water (the hand-off's ref/ui.js): the top bar (coins, level, three gauges), the
// dock (feed, wipe, change water, lamp, shop), the food chooser, the sponge, the fish card, the shop sheet, toasts.
// The bars hide by themselves after 9 quiet seconds: the tank is the picture. Everything is placed from W and H
// (lay()), so upright and on its side share one description; sizes and colours are the page's. Text: Ubuntu, bold
// 12-16 and regular 12/16, never cut with "…" (whole items or whole words go).
#pragma GCC optimize("Os")
#include <cmath>

#include "tank_int.h"

namespace tank {
namespace ui {

bool saver = false, homeLine = false;

namespace {

using namespace gfx;
constexpr float ROW_H = 66;
const char* const TABS[7] = {"Cá", "Cây", "Đồ", "Máy", "Hồ", "Sổ", "Việc"};

struct Toast {
  String s;
  float t;
};
struct Float {
  String s;
  float x, y, t;
};
struct Press {
  bool on = false;
  float x = 0, y = 0, t = 0, sy = 0, lastY = 0, vel = 0;
  bool moved = false, wasHidden = false;
  int k = 0, i = 0, row = -1, kind = 0;  // k: what was hit
  bool btn = false;
};
enum Hit { H_NONE, H_CLOSE, H_TAB, H_LIST, H_CARD_NONE, H_SELL, H_CURE, H_CARDCLOSE, H_FOOD, H_FOODCLOSE, H_SPONGEOFF, H_WIPE, H_DOCK, H_WATER };

std::vector<Toast> toasts;
std::vector<Float> floats;
bool sheet = false, card = false, sponge = false, foodPick = false;
uint32_t cardUid = 0;
float quiet = 0, show = 1, pulse = 0, scroll = 0, fling = 0;
int tab = 0, pressed = -1;
float spX = 0, spY = 0, lastTapX = 0, lastTapY = 0;
std::vector<game::Row> rows;
struct Fit3 {
  String name, sub, note;
};
std::vector<Fit3> fitted;
Press P;
struct Fx {
  int k = 0;  // 0 none, 1 water, 2 level
  float t = 0;
} fxNow;

inline uint16_t H(uint32_t c) { return hex565(c); }
int A(float a) { return (int)(a * 256 + 0.5f); }

void refreshRows() {
  rows = game::shop(tab);
  fitted.clear();
  const Lay L = lay();
  for (const game::Row& r : rows) {
    const bool hasBtn = (r.price >= 0 && !r.tag.length()) || r.tag.length();
    const float room = L.shW - 74 - (hasBtn ? 98 : 14);
    fitted.push_back({fit1(r.name.c_str(), 13, true, room), fit1(r.sub.c_str(), 12, false, room), fit1(r.note.c_str(), 12, false, L.shW - 74 - 14)});
  }
}
float clampScroll(float v) {
  const Lay L = lay();
  return std::max(0.0f, std::min(std::max(0.0f, rows.size() * ROW_H - (L.shY + L.shH - L.listY) + 8), v));
}
struct TabR {
  float x, y, w, h;
  String name;
};
std::vector<TabR> tabRects(const Lay& L) {
  const bool marine = game::T().marine;
  float ws[7];
  String names[7];
  for (int i = 0; i < 7; i++) names[i] = i == 1 && marine ? "San hô" : TABS[i], ws[i] = textW(names[i].c_str(), 12, true);
  int pad = 7;
  const float gap = 4, room = L.shW - 12;
  auto need = [&]() {
    float s = 0;
    for (float w : ws) s += w + pad * 2;
    return s + 6 * gap;
  };
  while (need() > room && pad > 3) pad--;
  float x = L.shX + std::max(6.0f, (L.shW - need()) / 2);
  std::vector<TabR> out;
  for (int i = 0; i < 7; i++) out.push_back({x, L.shY + 42, ws[i] + pad * 2, 26, names[i]}), x += ws[i] + pad * 2 + gap;
  return out;
}
const FishRec* cardFish() { return card ? game::fishRec(cardUid) : nullptr; }
void needs(const char** out, int& n) {
  const TankRec& t = game::T();
  n = 0;
  if (t.food < 35) out[n++] = "n_food";
  if (t.clean < 45) out[n++] = "n_bucket";
  if (t.algae > 45) out[n++] = "n_sponge";
}
const Spr* thing(const char* name) { return sprNamed(P_THINGS, name); }

int hit(float x, float y, int& i, int& row, bool& btn) {
  const Lay L = lay();
  if (sheet) {
    if (x < L.shX || y < L.shY) return H_CLOSE;
    if (y < L.shY + 38) return x > L.shX + L.shW - 42 ? H_CLOSE : H_NONE;
    if (y < L.listY - 4) {
      const std::vector<TabR> tr = tabRects(L);
      for (int k = 0; k < 7; k++)
        if (x >= tr[k].x - 2 && x < tr[k].x + tr[k].w + 2) return i = k, H_TAB;
      return H_NONE;
    }
    row = (int)floorf((y - L.listY + scroll) / ROW_H), btn = x > L.shX + L.shW - 94;
    return H_LIST;
  }
  if (card) {
    if (y > L.cardY && x >= L.cardX && x < L.cardX + L.cardW) {
      if (y > L.cardY + 78) {
        const FishRec* d = cardFish();
        const float u = x - L.cardX;
        if (d && d->sick) return u < 106 ? H_SELL : u < 204 ? H_CURE : H_CARDCLOSE;
        return u < 152 ? H_SELL : H_CARDCLOSE;
      }
      return H_CARD_NONE;
    }
    return H_CARDCLOSE;
  }
  if (foodPick) {
    int ds[F_N];
    const int n = game::diets(game::T(), ds);
    for (int k = 0; k < n; k++)
      if (fabsf(y - L.foodY[k]) < 26 && x >= L.foodX - 4 && x < L.foodX + 158) return i = ds[k], H_FOOD;
    return H_FOODCLOSE;
  }
  if (sponge) return y > L.dy - L.dr && fabsf(x - L.W / 2) < 46 ? H_SPONGEOFF : H_WIPE;
  if (show > 0.5f && !saver)
    for (int k = 0; k < 5; k++)
      if (hypotf(x - L.dx[k], y - L.dy) < L.dr + 4) return i = k, H_DOCK;
  return H_WATER;
}

// ---------------------------------------------------------------- drawing pieces
void bar(const Tgt& t, float x, float y, float w, float v, uint32_t col, int a) {
  rrect(t, x, y, w, 7, 3.5f, H(0x142840), a * 41 / 256);  // rgba(20,40,60,.16)
  const float k = std::max(0.08f, std::min(1.0f, v / 100));
  rrect(t, x, y, w * k, 7, 3.5f, v < 30 ? (sinf(pulse * 6) > 0 ? H(0xe5484d) : H(0xf08a8d)) : H(col), a);
}
void drawDock(const Tgt& t, const Lay& L, int a) {
  static const char* const ICON[5] = {"i_food", "i_sponge", "i_bucket", "i_lamp", "i_shop"};
  const TankRec& T = game::T();
  for (int i = 0; i < 5; i++) {
    const float x = L.dx[i], y = L.dy, k = pressed == i ? 0.9f : 1, r = L.dr * k;
    dockButton(t, x, y, r, a);
    bool dim = false, badge = false, good = false;
    if (i == 4) good = game::tasksReady() > 0;
    if (i == 0) badge = T.food < 35;
    else if (i == 1) badge = T.algae > 45;
    else if (i == 2) badge = T.clean < 45, dim = game::waterWait() > 0 && !badge;
    else if (i == 3) dim = !game::lampOn(T);
    fit(t, thing(ICON[i]), x - 18 * k, y - 18 * k, 36 * k, 36 * k, dim ? a * 115 / 256 : a);
    if (badge || good) {
      const float rr = 5.5f + sinf(pulse * 5);
      circle(t, x + 17, y - 17, rr + 0.6f, 0xFFFF, a);
      circle(t, x + 17, y - 17, rr - 0.6f, badge ? H(0xe5484d) : H(0x2fae5b), a);
    }
  }
}
void drawToasts(const Tgt& t, const Lay& L) {
  float y = 44;
  for (const Toast& q : toasts) {
    const float a = std::min(1.0f, std::min(q.t * 6, (2.8f - q.t) * 3));
    String l0, l1;
    wrap2(q.s.c_str(), 12, true, L.W - 44, l0, l1);
    const float w = std::min(L.W - 16, std::max(textW(l0.c_str(), 12, true), textW(l1.c_str(), 12, true)) + 26), h = l1.length() ? 40 : 26;
    rrect(t, L.W / 2 - w / 2, y, w, h, 13, H(0x081420), A(a * 0.8f));
    text(t, l0.c_str(), L.W / 2, y + 13.5f, 12, true, 0xFFFF, 1, A(a));
    if (l1.length()) text(t, l1.c_str(), L.W / 2, y + 28.5f, 12, true, 0xFFFF, 1, A(a));
    y += h + 4;
  }
}
void foodArt(const Tgt& t, int kind, float x, float y) {
  if (kind == F_flake) return fit(t, thing("f_food"), x - 15, y - 15, 30, 30);
  const Spr* s = sprNamed(P_UI, kind == F_sink ? "f_sink" : "f_live");
  if (s) return fit(t, s, x - 15, y - 15, 30, 30);
  static const float TAB[5][2] = {{-7, -3}, {6, -5}, {0, 6}, {-9, 8}, {9, 7}};
  if (kind == F_sink)  // the page's five green tablets, when the package has no picture of them
    for (const auto& p : TAB) rrect(t, x + p[0] - 5.2f, y + p[1] - 3.6f, 10.4f, 7.2f, 3.6f, H(0x4f6a2a), 256);
  else
    for (int k = 0; k < 3; k++) line(t, x - 12 + k * 5, y - 6 + k * 7, x - 2 + k * 5, y - 7 + k * 7, 2.6f, H(0xd23a33), 256);
}
void drawFoodPick(const Tgt& t, const Lay& L) {
  const TankRec& T = game::T();
  int ds[F_N];
  const int n = game::diets(T, ds);
  const float x0 = L.foodX;
  dim(t, 0, 0, L.W, L.H, H(0x040e18), A(0.28f));
  for (int i = 0; i < n; i++) {
    const float y = L.foodY[i];
    const int k = ds[i], cost = D.R.dietCost[k];
    glass(t, x0, y - 24, 154, 48, 24, 240);
    circle(t, x0 + 24, y, 20, H(0xe4f1f8), 256);
    foodArt(t, k, x0 + 24, y);
    text(t, (String(D.R.dietVi[k]) + (cost ? "  " + String(cost) + " xu" : "")).c_str(), x0 + 52, y - 7, 13, true, H(0x1c2a38));
    bar(t, x0 + 52, y + 6, 88, T.fed[k], 0x4fb84a, 256);
  }
  text(t, "Mỗi loài ăn một loại", x0 + 77, L.foodY[n - 1] - 38, 12, true, 0xFFFF, 1);
}
void btn(const Tgt& t, float bx, float y, float w, const String& label, uint32_t col, uint32_t ink) {
  rrect(t, bx, y + 82, w, 30, 15, H(col), 256);
  text(t, label.c_str(), bx + w / 2, y + 97.5f, 13, true, H(ink), 1);
}
void drawCard(const Tgt& t, const Lay& L) {
  const FishRec* d = cardFish();
  if (!d) return;
  const Species& s = D.sp[d->sp];
  const bool adult = game::isAdult(*d);
  const std::vector<String>& why = game::reasons(d->uid);
  const int val = std::max(1, (int)lroundf(s.val * (adult ? 1 : D.R.coinYoung)));
  const float x = L.cardX, y = L.cardY;
  glass(t, x, y, L.cardW, L.cardH, 16, 245);
  rrect(t, x + 8, y + 8, 76, 54, 10, H(0xd9ecf5), 256);
  fit(t, spr(s.marine ? P_MARINE : P_FRESH, s.pic), x + 11, y + 11, 70, 48);
  text(t, s.vi, x + 94, y + 18, 15, true, H(0x1c2a38));
  const String age = adult ? String("Trưởng thành") : "Cá non, lớn " + String((int)lroundf(d->age / D.R.adultH * 100)) + "%";
  text(t, fit1((age + " \xC2\xB7 " + game::roleVi(s.role)).c_str(), 12, false, 204).c_str(), x + 94, y + 35, 12, false, H(0x566575));
  if (why.size()) {
    String all = "Buồn: ";
    for (size_t i = 0; i < why.size(); i++) {
      String w = why[i];
      // the page lowers the first letter; the reasons start with a capital (Đ, B, N, C, T, S)
      if (w.startsWith("\xC4\x90")) w = "\xC4\x91" + w.substring(2);
      else if (w.length() && w[0] >= 'A' && w[0] <= 'Z') w.setCharAt(0, w[0] + 32);
      all += (i ? ", " : "") + w;
    }
    String l0, l1;
    wrap2(all.c_str(), 12, false, 204, l0, l1);
    text(t, l0.c_str(), x + 94, y + 51, 12, false, H(0xd6453d));
    if (l1.length()) text(t, l1.c_str(), x + 94, y + 66, 12, false, H(0xd6453d));
  } else {
    text(t, s.val ? ("Đang vui \xC2\xB7 nhả " + String(val) + " xu mỗi lần").c_str() : s.eats == 2 ? "Đang vui \xC2\xB7 ăn rêu trên kính" : "Đang vui \xC2\xB7 nhặt thức ăn thừa",
         x + 94, y + 51, 12, false, H(0x2b8a4b));
    text(t, fit1(game::traits(s).c_str(), 12, false, 204).c_str(), x + 94, y + 66, 12, false, H(0x667585));
  }
  if (d->sick) {
    btn(t, x + 10, y, 92, "Bán +" + String(game::sellPrice(*d)), 0xf2b824, 0x3a2c06);
    btn(t, x + 106, y, 92, "Chữa " + String(D.R.cureCost) + " xu", 0x2fae5b, 0xffffff);
    btn(t, x + 202, y, 92, "Đóng", 0xdfe6ee, 0x243140);
  } else {
    btn(t, x + 12, y, 136, "Bán +" + String(game::sellPrice(*d)) + " xu", 0xf2b824, 0x3a2c06);
    btn(t, x + 156, y, 136, "Đóng", 0xdfe6ee, 0x243140);
  }
}
// the corners of a box, rounded off by painting the page's colour over what lies outside them
void corners(const Tgt& t, float x, float y, float w, float h, float r, uint16_t bg) {
  Tgt c = t;
  for (int k = 0; k < 4; k++) {
    const float cx = k & 1 ? x + w - r : x, cy = k & 2 ? y + h - r : y;
    // a square r x r: paint where it lies outside the circle of radius r round the inner corner point
    const float ox = k & 1 ? x + w - r : x + r, oy = k & 2 ? y + h - r : y + r;
    for (float py = cy; py < cy + r; py += 1)
      for (float px = cx; px < cx + r; px += 1) {
        const float dx = px + 0.5f - ox, dy = py + 0.5f - oy, out = sqrtf(dx * dx + dy * dy) - r + 0.5f;
        if (out > 0) rrect(c, px, py, 1, 1, 0, bg, (int)(std::min(1.0f, out) * 256));
      }
  }
}
void drawSheet(const Tgt& t, const Lay& L) {
  const float sx = L.shX, sy = L.shY, sw = L.shW;
  const TankRec& T = game::T();
  if (L.land) rrect(t, sx, sy, sw, L.shH, 0, H(0xf6f8fb), 256);
  else rrect(t, sx, sy, sw, L.shH + 20, 16, H(0xf6f8fb), 256);
  int have, all;
  game::albumCount(have, all);
  const String names[7] = {!T.marine ? "Cá nước ngọt" : "Cá nước mặn", !T.marine ? "Cây thủy sinh" : "San hô", "Đồ trang trí", "Máy móc",
                           "Kiểu hồ", "Sổ cá " + String(have) + " / " + String(all), "Việc hôm nay"};
  text(t, names[tab].c_str(), sx + 14, sy + 20, 15, true, H(0x1c2a38));
  const String cs = String(game::coins());
  const float cw = textW(cs.c_str(), 13, true);
  text(t, cs.c_str(), sx + sw - 46, sy + 20.5f, 13, true, H(0x3a2c06), 2);
  fit(t, thing("s_coin"), sx + sw - 68 - cw, sy + 11, 18, 18);
  line(t, sx + sw - 27, sy + 13, sx + sw - 13, sy + 27, 2, H(0x6b7a8a), 256);
  line(t, sx + sw - 13, sy + 13, sx + sw - 27, sy + 27, 2, H(0x6b7a8a), 256);
  if (tab == 0) {
    String u = game::used(T) == (int)game::used(T) ? String((int)game::used(T)) : String(game::used(T), 1);
    text(t, ("Chỗ " + u + "/" + String(game::capacity(T))).c_str(), sx + sw - 76 - cw, sy + 20.5f, 12, false, H(0x566575), 2);
  }
  const std::vector<TabR> tr = tabRects(L);
  for (int i = 0; i < 7; i++) {
    const bool on = i == tab;
    rrect(t, tr[i].x, tr[i].y, tr[i].w, tr[i].h, 13, on ? H(0x2f6fb5) : H(0xe3e9f0), 256);
    text(t, tr[i].name.c_str(), tr[i].x + tr[i].w / 2, tr[i].y + 13.5f, 12, true, on ? 0xFFFF : H(0x3c4a5a), 1);
    if (i == 6 && !on && game::tasksReady()) circle(t, tr[i].x + tr[i].w - 3, tr[i].y + 3, 4, H(0x2fae5b), 256);
  }
  // the list, clipped to its window
  Tgt c = t;
  const int S0 = (int)lroundf((L.listY - 2) * sc->SC), S1 = (int)lroundf((sy + L.shH) * sc->SC);
  c.y0 = std::max(t.y0, S0), c.y1 = std::min(t.y1, S1);
  if (c.y0 >= c.y1) return;
  c.p = t.at(t.x0, c.y0);
  for (size_t i = 0; i < rows.size() && i < fitted.size(); i++) {
    const float y = L.listY + i * ROW_H - scroll;
    if (y > L.H || y < L.listY - ROW_H) continue;
    const game::Row& r = rows[i];
    const Fit3& f = fitted[i];
    const float tx = sx + 74;
    rrect(c, sx + 8, y + 3, sw - 16, ROW_H - 6, 12, H(0xe3e9f0), 256);
    rrect(c, sx + 9, y + 4, sw - 18, ROW_H - 8, 11, 0xFFFF, 256);
    rrect(c, sx + 14, y + 8, 52, ROW_H - 16, 9, r.look ? H(0x0c2233) : H(0xdcedf6), 256);
    const int pa = r.dim && !r.shade ? 115 : 256;
    if (r.up >= 0) {
      static const char* const ALT[NUP] = {nullptr, nullptr, "i_lamp", "bubbler", "i_food", nullptr, nullptr, nullptr};
      if (ALT[r.up]) fit(c, thing(ALT[r.up]), sx + 18, y + 11, 44, ROW_H - 22, pa);
      else fit(c, sprNamed(P_UI, (String("g_") + D.up[r.up].id).c_str()), sx + 14, y + 8, 52, ROW_H - 16, pa);
    } else if (r.look) {
      blitScaled(c, spr(P_THUMBS, r.art), sx + 14, y + 8, 52, ROW_H - 16, pa);
      corners(c, sx + 14, y + 8, 52, ROW_H - 16, 9, 0xFFFF);
    } else fit(c, spr(r.artPack, r.art), sx + 17, y + 11, 46, ROW_H - 22, pa, r.shade);
    if (r.count) {
      const String s = "\xC3\x97" + String(r.count);
      const float w = textW(s.c_str(), 12, true) + 10;
      rrect(c, sx + 66 - w, y + ROW_H - 26, w, 17, 8.5f, H(0x2f6fb5), 256);
      text(c, s.c_str(), sx + 66 - w / 2, y + ROW_H - 17, 12, true, 0xFFFF, 1);
    }
    text(c, f.name.c_str(), tx, y + 17, 13, true, r.dim ? H(0x8a96a3) : H(0x1c2a38));
    text(c, f.sub.c_str(), tx, y + 33, 12, false, H(0x667585));
    if (f.note.length()) text(c, f.note.c_str(), tx, y + 50, 12, false, r.bad ? H(0xd6453d) : r.warn ? H(0xc77700) : H(0x8a96a3));
    const float bx = sx + sw - 90;
    if (r.price >= 0 && !r.tag.length()) {
      rrect(c, bx, y + 9, 78, 28, 14, r.can ? H(0xf2b824) : H(0xe6dcc0), 256);
      fit(c, thing("s_coin"), bx + 6, y + 14, 18, 18);
      text(c, String(r.price).c_str(), bx + 28, y + 23.5f, 13, true, r.can ? H(0x3a2c06) : H(0x9a8c66));
    } else if (r.tag.length()) {
      if (r.can) rrect(c, bx, y + 9, 78, 28, 14, r.tag == "Nhận" ? H(0x2fae5b) : H(0x2f6fb5), 256);
      text(c, r.tag.c_str(), bx + 39, y + 23.5f, 12, true, r.can ? 0xFFFF : H(0x8a96a3), 1);
    }
  }
}

}  // namespace

// ---------------------------------------------------------------- where things are, for the screen as it is held
Lay lay() {
  Lay L;
  L.W = sc ? sc->VW : 320, L.H = sc ? sc->VH : 480, L.land = L.W > L.H;
  L.barX = roundf((L.W - 304) / 2), L.barY = 6, L.barW = 304, L.barH = 30;
  L.dy = L.H - 36, L.dr = 25;
  for (int i = 0; i < 5; i++) L.dx[i] = L.W / 2 + (i - 2) * 62;
  if (L.land) L.shX = L.W - 320, L.shY = 0, L.shW = 320, L.shH = L.H;
  else L.shX = 0, L.shY = 62, L.shW = 320, L.shH = L.H - 62;
  L.listY = L.shY + 76;
  L.cardX = roundf((L.W - 304) / 2), L.cardY = L.H - 126, L.cardW = 304, L.cardH = 120;
  L.foodX = L.dx[0] - 24, L.foodY[0] = L.dy - 62, L.foodY[1] = L.dy - 118, L.foodY[2] = L.dy - 174;
  return L;
}

void say(const String& s) {
  if (!s.length() || (toasts.size() && toasts.back().s == s)) return;
  toasts.push_back({s, 0});
  if (toasts.size() > 3) toasts.erase(toasts.begin());
}
void fx(const char* k, int v) {
  if (!strcmp(k, "water")) fxNow = {1, 0};
  else if (!strcmp(k, "level")) fxNow = {2, 0};
  else if (!strcmp(k, "coin")) floats.push_back({"+" + String(v), lastTapX, lastTapY, 0});
}
void wake() { quiet = 0; }
#if TANK_QA
void devOpen(const char* what, int arg) {  // the dev console: a screen straight away (shots)
  if (!strcmp(what, "scroll")) {
    scroll = clampScroll(arg);
    return;
  }
  reset();
  wake();
  show = 1;
  if (!strcmp(what, "card") && sc && sc->nfish) {
    const Fish* f = &sc->fish[0];
    for (int i = 0; i < sc->nfish; i++)
      if (game::fishRec(sc->fish[i].uid) && (arg < 0 || game::fishRec(sc->fish[i].uid)->sick)) f = &sc->fish[i];
    card = true, cardUid = f->uid;
  } else if (!strcmp(what, "tab")) sheet = true, tab = std::max(0, std::min(6, arg)), scroll = 0, refreshRows();
  else if (!strcmp(what, "food")) foodPick = true;
  else if (!strcmp(what, "sponge")) sponge = true;
}
#endif
bool busy() { return sheet || card || sponge || foodPick; }
void reset() { sheet = card = sponge = foodPick = false, P.on = false, pressed = -1; }

void cover(float& x0, float& y0, float& x1, float& y1) {  // the sheet hides the water under it
  x0 = y0 = x1 = y1 = 0;
  if (!sheet) return;
  const Lay L = lay();
  if (L.land) x0 = L.shX, y0 = 0, x1 = L.W, y1 = L.H;
  else x0 = 0, y0 = L.shY + 16, x1 = L.W, y1 = L.H;
}

// ---------------------------------------------------------------- the pointer
void down(float x, float y) {
  const bool wasHidden = show < 0.5f && !sheet && !card;
  wake();
  lastTapX = x, lastTapY = y;
  P = Press();
  P.on = true, P.x = x, P.y = y, P.lastY = y, P.sy = scroll, P.wasHidden = wasHidden;
  P.k = hit(x, y, P.i, P.row, P.btn);
  fling = 0;
  if (P.k == H_DOCK) pressed = P.i;
  if (P.k == H_WIPE) game::wipeAt(x, y), spX = x, spY = y;
}
void move(float x, float y) {
  if (!P.on) return;
  if (hypotf(x - P.x, y - P.y) > 6) P.moved = true;
  wake();
  if (P.k == H_LIST) {
    P.vel = P.vel * 0.5f + (P.lastY - y) * 0.5f, P.lastY = y;
    scroll = clampScroll(P.sy - (y - P.y));
  } else if (P.k == H_WIPE) game::wipeAt(x, y), spX = x, spY = y;
  else if (P.k == H_WATER && sc && sc->fingerOn) sc->fingerX = x, sc->fingerY = y;
}
void up(float x, float y) {
  if (!P.on) return;
  P.on = false, pressed = -1;
  if (sc && sc->fingerOn) {
    sc->fingerOn = false;
    return;
  }
  const int k = P.k;
  if (k == H_LIST && P.moved) fling = P.vel * 30;  // let the list run on a little (units a second)
  if (k == H_WIPE) {
    if (game::T().algae < 6) game::wipeDone(), sponge = false;
    return;
  }
  if (P.moved && k != H_DOCK) return;
  switch (k) {
    case H_CLOSE: sheet = false; return;
    case H_CARDCLOSE: card = false; return;
    case H_SPONGEOFF: sponge = false; return;
    case H_FOOD: foodPick = false, game::feed(P.i); return;
    case H_FOODCLOSE: foodPick = false; return;
    case H_CURE: game::cure(cardUid); return;
    case H_TAB: tab = P.i, scroll = 0, refreshRows(); return;
    case H_LIST: {
      if (P.row < 0 || P.row >= (int)rows.size() || !P.btn) return;
      const game::Row r = rows[P.row];
      if (r.act > 0 && r.can) game::press(r);
      else if (r.act > 0 && r.tag != "")
        say(r.bad ? r.note : r.tag.startsWith("Cấp") ? "Cần " + String("c") + r.tag.substring(1)
            : r.price >= 0 && game::coins() < r.price ? String("Chưa đủ xu") : r.tag == "Xong" ? String("Đã nhận rồi") : String("Chưa được"));
      refreshRows();
      return;
    }
    case H_SELL:
      if (card) game::sellFish(cardUid), card = false;
      return;
    case H_DOCK: {
      const int i = P.i;
      const TankRec& T = game::T();
      if (i == 0) {
        int ds[F_N];
        if (game::diets(T, ds) > 1) foodPick = true;
        else game::feed(F_flake);
      } else if (i == 1) {
        if (T.algae < 1.5f) say("Kính đang sạch");
        else sponge = true, spX = sc->VW / 2, spY = sc->VH / 2;
      } else if (i == 2) game::changeWater();
      else if (i == 3) game::setLamp(!T.lamp);
      else sheet = true, scroll = 0, refreshRows();
      return;
    }
    case H_WATER: {
      if (game::collect(x, y)) return;
      if (P.wasHidden) return;
      Fish* f = sc ? sc->fishAt(x, y) : nullptr;
      if (f && f->uid && game::fishRec(f->uid)) {
        card = true, cardUid = f->uid;
        return;
      }
      if (sc) sc->tapGlass(x, y);
      game::tapped();
      return;
    }
  }
}

// ---------------------------------------------------------------- time
void tick(float dt) {
  pulse += dt;
  if (P.on) {
    P.t += dt;
    if (P.k == H_WATER && !P.moved && P.t > 0.4f && sc && !sc->fingerOn && !saver) sc->fingerOn = true, sc->fingerX = P.x, sc->fingerY = P.y;
    quiet = 0;
  }
  if (!busy()) quiet += dt;
  else quiet = 0;
  const float want = saver ? 0 : quiet > 9 ? 0 : 1;
  show += (want - show) * std::min(1.0f, dt * (want ? 9 : 2.5f));
  for (Toast& q : toasts) q.t += dt;
  while (toasts.size() && toasts.front().t >= 2.8f) toasts.erase(toasts.begin());
  for (Float& f : floats) f.t += dt;
  while (floats.size() && floats.front().t >= 1) floats.erase(floats.begin());
  if (fxNow.k) {
    fxNow.t += dt;
    if (fxNow.t > (fxNow.k == 1 ? 2.6f : 1.4f)) fxNow.k = 0;
    else if (fxNow.k == 1 && sc && sc->nbub < MAXBUB && (float)rand() / RAND_MAX < dt * 40)
      sc->bub[sc->nbub++] = {(float)rand() / RAND_MAX * sc->VW, 60 + (float)rand() / RAND_MAX * (sc->VH - 120),
                             0.8f + (float)rand() / RAND_MAX * 2, 40 + (float)rand() / RAND_MAX * 40, (float)rand() / RAND_MAX * 6};
  }
  if (card && !cardFish()) card = false;
  if (sheet && !P.on && fling != 0) {
    scroll = clampScroll(scroll + fling * dt);
    fling *= std::max(0.0f, 1 - dt * 4);
    if (fabsf(fling) < 5) fling = 0;
  }
  if (sheet && (int)(pulse * 2) != (int)((pulse - dt) * 2)) refreshRows(), scroll = clampScroll(scroll);
}

// ---------------------------------------------------------------- the drawing
void draw(const Tgt& t) {
  const Lay L = lay();
  const float W = L.W, Hh = L.H;
  if (fxNow.k == 1) {
    const float k = fxNow.t / 2.6f, yl = 50 + (Hh - 110) * (k < 0.5f ? k * 2 : 2 - k * 2);
    dim(t, 0, 0, W, yl, H(0xd2f0ff), A(0.16f));
    dim(t, 0, yl - 1, W, 2, 0xFFFF, A(0.7f));
  }
  const char* n[3];
  int nn;
  if (saver) {  // the standby face: a soft dark fade under the page's clock, the needs at the bottom left
    for (int y = 0; y < 190; y += 2) dim(t, 0, y, W, 2, H(0x000a18), A(0.5f * (1 - y / 190.0f)));
    needs(n, nn);
    for (int i = 0; i < nn; i++) {
      const int a = A(0.7f + 0.3f * sinf(pulse * 3));
      glass(t, 10 + i * 40, Hh - 42, 34, 34, 17, 204, a);
      fit(t, thing(n[i]), 14 + i * 40, Hh - 38, 26, 26, a);
    }
    return;
  }
  const TankRec& T = game::T();
  const int a = A(show);
  if (show > 0.02f) {
    glass(t, L.barX, L.barY, L.barW, L.barH, 15, 219, a);
    const float bx = L.barX, by = L.barY;
    fit(t, thing("s_coin"), bx + 5, by + 5, 20, 20, a);
    text(t, String(game::coins()).c_str(), bx + 28, by + 15.5f, 14, true, H(0x3a2c06), 0, a);
    const float lx = bx + 74;
    rrect(t, lx, by + 5, 46, 20, 10, H(0x2f6fb5), a);
    Tgt part = t;  // the experience as the pill's fill
    part.x1 = std::min(t.x1, (int)lroundf((lx + 46 * game::levelSpan()) * sc->SC));
    if (part.x1 > part.x0) rrect(part, lx, by + 5, 46, 20, 10, H(0x4f9ae8), a);
    text(t, ("Cấp " + String(game::level())).c_str(), lx + 23, by + 15.5f, 12, true, 0xFFFF, 1, a);
    fit(t, thing("s_food"), bx + 126, by + 6, 17, 18, a);
    bar(t, bx + 146, by + 12, 32, T.food, 0x4fb84a, a);
    fit(t, thing("s_drop"), bx + 184, by + 6, 16, 18, a);
    bar(t, bx + 203, by + 12, 32, T.clean, 0x2f9be8, a);
    fit(t, thing("s_heart"), bx + 241, by + 7, 17, 16, a);
    bar(t, bx + 261, by + 12, 32, game::comfort(T), 0xe5484d, a);
    if (!sponge) drawDock(t, L, a);
  }
  if (show < 0.6f && !sheet && !card) {
    needs(n, nn);
    const int b = A((1 - show) * (0.75f + 0.25f * sinf(pulse * 4)));
    for (int i = 0; i < nn; i++) {
      glass(t, 10 + i * 40, Hh - 42, 34, 34, 17, 204, b);
      fit(t, thing(n[i]), 14 + i * 40, Hh - 38, 26, 26, b);
      circle(t, 40 + i * 40, Hh - 38, 4.5f, H(0xe5484d), b);
    }
  }
  if (sponge) {
    glass(t, W / 2 - 100, 46, 200, 26, 13, 224);
    text(t, "Kéo miếng bọt để lau kính", W / 2, 59.5f, 12, true, H(0x243140), 1);
    glass(t, W / 2 - 42, L.dy - 17, 84, 34, 17, 230);
    text(t, "Xong", W / 2, L.dy + 0.5f, 14, true, H(0x243140), 1);
    if (P.on && P.k == H_WIPE) fit(t, thing("c_sponge"), spX - 22, spY - 20, 44, 40);
  }
  for (const Float& f : floats) {
    const int fa = A(1 - f.t);
    text(t, f.s.c_str(), f.x + 1, f.y - 14 - f.t * 26 + 1, 16, true, H(0x3c1e00), 1, fa * 7 / 10);
    text(t, f.s.c_str(), f.x, f.y - 14 - f.t * 26, 16, true, H(0xffd84a), 1, fa);
  }
  if (fxNow.k == 2) {
    const float k = fxNow.t / 1.4f;
    ring(t, W / 2, Hh / 2 - 10, 30 + 150 * k, 5 * (1 - k) + 1, H(0xffd84a), A(1 - k));
  }
  if (foodPick) drawFoodPick(t, L);
  if (card) drawCard(t, L);
  if (sheet) {
    dim(t, 0, 0, W, Hh, H(0x040e18), A(0.45f));
    drawSheet(t, L);
  }
  drawToasts(t, L);
  if (homeLine) rrect(t, (W - 110) / 2, Hh - 9, 110, 4, 2, 0xFFFF, 128);  // as os.cpp's, white at half
}

}  // namespace ui
}  // namespace tank
