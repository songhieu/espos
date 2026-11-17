#include "Decl.h"

#include <Utf8.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace inkdesk::decl {
namespace {

constexpr int MAX_DEPTH = 3;      // list/group nesting
constexpr int MAX_OPS = 160;      // things drawn per layout, lists expanded
constexpr int MAX_STEPS = 4000;   // items visited, list rows, failed conditions, empty texts
constexpr int MAX_ROWS = 20;      // rows per list
constexpr int MAX_TEXT = 400;     // bytes of one expanded text
// Geometry limits, so a hostile or broken file cannot keep the renderer busy (the render lock is held
// and a sleep wake draws the face every time): positions stay within MARGIN of the target box, sizes,
// line widths, radii and icons are capped, lines are clipped to the target, and the pixels all the
// drawing may touch are counted against MAX_WORK.
constexpr int MARGIN = 2000;
constexpr int MAX_LEN = 2000;
constexpr int MAX_LW = 32;
constexpr int MAX_CIRCLE = 1000;
constexpr int MAX_ICON = 256;
constexpr long MAX_WORK = 4000000;  // pixels, about ten 800x480 screens
constexpr long MAX_MEASURED = 300000;  // characters measured while fitting texts (fonts tried x text length)
constexpr int MAX_IMAGES = 8;          // pictures drawn per layout (each is read from the card)

uint32_t upperCp(const uint32_t cp) {
  if (cp == 0x1A1 || cp == 0x1B0) return cp - 1;                  // ơ ư
  if (cp >= 0x1EA0 && cp <= 0x1EF9) return (cp & 1) ? cp - 1 : cp;  // Vietnamese letters with tones
  return utf8SmallCapsUpper(cp);
}

uint32_t lowerCp(const uint32_t cp) {
  if (cp >= 'A' && cp <= 'Z') return cp + 0x20;
  if (cp >= 0xC0 && cp <= 0xDE && cp != 0xD7) return cp + 0x20;
  if (cp == 0x178) return 0xFF;
  if (cp >= 0x100 && cp <= 0x17F) {
    const bool oddAligned = (cp >= 0x139 && cp <= 0x148) || (cp >= 0x179 && cp <= 0x17E);
    if (cp == 0x130 || cp == 0x131 || cp == 0x138 || cp == 0x149 || cp == 0x17F) return cp;
    return (cp % 2 == (oddAligned ? 1u : 0u)) ? cp + 1 : cp;
  }
  if (cp == 0x1A0 || cp == 0x1AF) return cp + 1;
  if (cp >= 0x1EA0 && cp <= 0x1EF9) return (cp & 1) ? cp : cp + 1;
  if (cp >= 0x391 && cp <= 0x3A9 && cp != 0x3A2) return cp + 0x20;
  if (cp >= 0x410 && cp <= 0x42F) return cp + 0x20;
  if (cp >= 0x400 && cp <= 0x40F) return cp + 0x50;
  return cp;
}

template <typename F>
std::string mapCase(const std::string& s, F f) {
  std::string out;
  out.reserve(s.size() + 4);
  const auto* p = reinterpret_cast<const unsigned char*>(s.c_str());
  while (*p) utf8AppendCodepoint(f(utf8NextCodepoint(&p)), out);
  return out;
}

bool number(const std::string& s, double& v) {
  if (s.empty()) return false;
  char* end = nullptr;
  v = strtod(s.c_str(), &end);
  while (end && *end == ' ') end++;
  return end && *end == '\0' && end != s.c_str();
}

// Field readers kept out of line: each inline ArduinoJson lookup costs flash at every use.
__attribute__((noinline)) double num(JsonObjectConst o, const char* k, const double def) {
  JsonVariantConst v = o[k];
  const double d = v.is<double>() ? v.as<double>() : def;
  return std::isfinite(d) ? d : def;
}
__attribute__((noinline)) const char* str(JsonObjectConst o, const char* k, const char* def = "") {
  JsonVariantConst v = o[k];
  return v.is<const char*>() ? v.as<const char*>() : def;
}

bool truthy(const std::string& v) { return !v.empty() && v != "0" && v != "false"; }

std::string toText(JsonVariantConst v) {
  if (v.is<const char*>()) return v.as<const char*>();
  if (v.is<bool>()) return v.as<bool>() ? "1" : "";
  if (v.is<long long>()) return std::to_string(v.as<long long>());
  if (v.is<double>()) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%g", v.as<double>());
    return buf;
  }
  return "";
}

const char* permFor(const char* name) {
  if (!strncmp(name, "weather.", 8)) return "data:weather";
  if (!strncmp(name, "cal.", 4)) return "data:calendar";
  if (!strncmp(name, "read.", 5)) return "data:reading";
  if (!strncmp(name, "news.", 5)) return "data:news";
  if (!strncmp(name, "market.", 7)) return "data:markets";
  return nullptr;
}

// Every number from the file becomes an int through here: no out-of-range casts (NaN gives `lo`).
int clampInt(const double v, const int lo, const int hi) {
  if (!(v > lo)) return lo;
  if (v >= hi) return hi;
  return static_cast<int>(lround(v));
}

struct Off {
  double x = 0, y = 0;
};

struct Ctx {
  Ctx(Canvas& c, const Data& d, const std::vector<std::string>& perms, JsonObjectConst root, const std::string& dir,
      Stats& st)
      : c(c), d(d), perms(perms), root(root), dir(dir), st(st) {}
  Canvas& c;
  const Data& d;
  const std::vector<std::string>& perms;
  JsonObjectConst root;
  const std::string& dir;
  Stats& st;
  double s = 1;
  double ox = 0, oy = 0;
  int tx = 0, ty = 0, tw = 0, th = 0;  // the target box
  // The list row being drawn.
  const char* list = nullptr;  // a system list, or null
  JsonVariantConst pkgItem;    // an entry of a package list
  bool inList = false;
  int index = 0, count = 0;

  bool allowed(const char* name) const {
    const char* p = permFor(name);
    return !p || std::find(perms.begin(), perms.end(), p) != perms.end();
  }
  int X(const Off& o, double v) const { return clampInt(ox + (o.x + v) * s, tx - MARGIN, tx + tw + MARGIN); }
  int Y(const Off& o, double v) const { return clampInt(oy + (o.y + v) * s, ty - MARGIN, ty + th + MARGIN); }
  int L(double v) const { return clampInt(v * s, 0, MAX_LEN); }   // widths and heights
  int W(double v) const { return clampInt(v * s, 1, MAX_LW); }    // line widths
  // One more thing drawn: false (and nothing drawn) once the layout has drawn enough.
  bool budget() {
    if (st.drawn >= MAX_OPS) {
      st.skipped++;
      return false;
    }
    st.drawn++;
    return true;
  }
  // Pixels the next drawing may touch: false (and nothing drawn) past the layout's share.
  bool work(const long px) {
    if (st.work + px > MAX_WORK) {
      st.skipped++;
      return false;
    }
    st.work += px;
    return true;
  }
  // A text measured while fitting: its width, or false once the layout measured enough.
  bool measure(const Font& f, const std::string& text, int& width) {
    if (st.measured + static_cast<long>(text.size()) > MAX_MEASURED) {
      measureOut = true;
      return false;
    }
    st.measured += static_cast<long>(text.size());
    width = c.textWidth(f, text.c_str());
    return true;
  }
  bool measureOut = false;
  // Any visit: items, list rows, failed conditions.
  bool step() {
    if (st.steps >= MAX_STEPS) return false;
    st.steps++;
    return true;
  }
};

// Clips a line to the box (Liang-Barsky). False when nothing of it is inside.
bool clipLine(int& x1, int& y1, int& x2, int& y2, const int bx0, const int by0, const int bx1, const int by1) {
  const double dx = x2 - x1, dy = y2 - y1;
  double t0 = 0, t1 = 1;
  const double p[4] = {-dx, dx, -dy, dy};
  const double q[4] = {static_cast<double>(x1 - bx0), static_cast<double>(bx1 - x1), static_cast<double>(y1 - by0),
                       static_cast<double>(by1 - y1)};
  for (int i = 0; i < 4; i++) {
    if (p[i] == 0) {
      if (q[i] < 0) return false;
      continue;
    }
    const double t = q[i] / p[i];
    if (p[i] < 0) t0 = std::max(t0, t);
    else t1 = std::min(t1, t);
    if (t0 > t1) return false;
  }
  const int ax = x1, ay = y1;
  x1 = static_cast<int>(lround(ax + t0 * dx));
  y1 = static_cast<int>(lround(ay + t0 * dy));
  x2 = static_cast<int>(lround(ax + t1 * dx));
  y2 = static_cast<int>(lround(ay + t1 * dy));
  return true;
}

bool lookup(Ctx& c, const std::string& name, std::string& out) {
  out.clear();
  if (c.inList) {
    if (name == "i") return out = std::to_string(c.index + 1), true;
    if (name == "last") return out = c.index == c.count - 1 ? "1" : "", true;
    if (name == "item" || name.rfind("item.", 0) == 0) {
      const std::string f = name.size() > 5 ? name.substr(5) : std::string();
      if (!c.list) {
        out = f.empty() ? toText(c.pkgItem) : toText(c.pkgItem[f.c_str()]);
        return true;
      }
      return c.d.field(c.list, c.index, f.c_str(), out);
    }
  }
  if (!c.allowed(name.c_str())) return false;
  return c.d.value(name.c_str(), out);
}

// "{time.hm}" and friends; "{{" is a literal brace.
std::string expand(Ctx& c, const char* t) {
  std::string out;
  if (!t) return out;
  std::string key, val;
  while (*t && out.size() < MAX_TEXT) {
    if (t[0] == '{' && t[1] == '{') {
      out.push_back('{');
      t += 2;
      continue;
    }
    if (t[0] == '}' && t[1] == '}') {
      out.push_back('}');
      t += 2;
      continue;
    }
    if (*t != '{') {
      out.push_back(*t++);
      continue;
    }
    const char* end = strchr(t, '}');
    if (!end) break;
    key.assign(t + 1, end - t - 1);
    t = end + 1;
    std::string filter;
    const size_t bar = key.find('|');
    if (bar != std::string::npos) {
      filter = key.substr(bar + 1);
      key.resize(bar);
    }
    lookup(c, key, val);
    if (filter == "upper") val = upper(val);
    else if (filter == "lower") val = lower(val);
    out += val;
  }
  return out;
}

// "weather.ok", "!cal.count", "weather.temp >= 30", "i == 1".
bool condition(Ctx& c, const char* e) {
  if (!e) return true;
  while (*e == ' ') e++;
  bool neg = false;
  if (*e == '!') {
    neg = true;
    e++;
  }
  const char* p = e;
  while (*p && *p != ' ' && !strchr("=!<>", *p)) p++;
  const std::string name(e, p - e);
  while (*p == ' ') p++;
  std::string lhs;
  lookup(c, name, lhs);
  if (!*p) return truthy(lhs) != neg;
  std::string op;
  while (*p && strchr("=!<>", *p) && op.size() < 2) op.push_back(*p++);
  while (*p == ' ') p++;
  std::string rhs(p);
  while (!rhs.empty() && rhs.back() == ' ') rhs.pop_back();
  if (rhs.size() >= 2 && (rhs.front() == '\'' || rhs.front() == '"') && rhs.back() == rhs.front()) {
    rhs = rhs.substr(1, rhs.size() - 2);
  }
  double a, b;
  const bool numeric = number(lhs, a) && number(rhs, b);
  bool r = false;
  if (op == "==") r = numeric ? a == b : lhs == rhs;
  else if (op == "!=") r = numeric ? a != b : lhs != rhs;
  else if (!numeric) r = false;
  else if (op == ">") r = a > b;
  else if (op == ">=") r = a >= b;
  else if (op == "<") r = a < b;
  else if (op == "<=") r = a <= b;
  return r != neg;
}

Col color(const char* v, const Col def, const bool forText) {
  if (!v || !*v) return def;
  if (!strcmp(v, "ink") || !strcmp(v, "accent")) return Col::Ink;
  if (!strcmp(v, "paper") || !strcmp(v, "card")) return Col::Paper;
  if (!strcmp(v, "muted")) return forText ? Col::Ink : Col::Gray;
  if (!strcmp(v, "none")) return Col::None;
  if (v[0] == '#' && strlen(v) == 7) {
    const long rgb = strtol(v + 1, nullptr, 16);
    const double l = (0.2126 * ((rgb >> 16) & 255) + 0.7152 * ((rgb >> 8) & 255) + 0.0722 * (rgb & 255)) / 255.0;
    if (l < 0.35) return Col::Ink;
    if (l > 0.75) return Col::Paper;
    return forText ? Col::Ink : Col::Gray;
  }
  return def;
}

Family family(const char* f) {
  if (!f) return Family::Sans;
  if (!strcmp(f, "serif")) return Family::Serif;
  if (!strcmp(f, "digits")) return Family::Digits;
  if (!strcmp(f, "ui")) return Family::Ui;
  return Family::Sans;
}

// The digit fonts hold only " %+,-./0-9:°" (src/inkdesk/fonts/ink_*_digits.h).
bool digitsOnly(const char* s) {
  const auto* p = reinterpret_cast<const unsigned char*>(s);
  while (*p) {
    const uint32_t cp = utf8NextCodepoint(&p);
    if (!(cp == ' ' || cp == '%' || (cp >= '+' && cp <= ':') || cp == 0xB0)) return false;
  }
  return true;
}

// Fonts of a family that can draw `s`, biggest first, one weight per size (the asked one when there is
// one), then the smaller UI fonts so a long text can still shrink before it wraps.
std::vector<const Font*> fontsFor(const Canvas& c, Family fam, const bool bold, const char* s) {
  std::vector<const Font*> out;
  if (fam == Family::Digits && !digitsOnly(s)) fam = Family::Sans;
  for (const Font& f : c.fonts()) {
    if (f.family == fam) out.push_back(&f);
  }
  const auto order = [bold](const Font* a, const Font* b) {
    if (a->px != b->px) return a->px > b->px;
    return (a->bold == bold) > (b->bold == bold);
  };
  const auto samePx = [](const Font* a, const Font* b) { return a->px == b->px; };
  std::sort(out.begin(), out.end(), order);
  out.erase(std::unique(out.begin(), out.end(), samePx), out.end());
  if (fam != Family::Ui) {
    const int smallest = out.empty() ? 1 << 30 : out.back()->px;
    std::vector<const Font*> tail;
    for (const Font& f : c.fonts()) {
      if (f.family == Family::Ui && f.px < smallest) tail.push_back(&f);
    }
    std::sort(tail.begin(), tail.end(), order);
    tail.erase(std::unique(tail.begin(), tail.end(), samePx), tail.end());
    out.insert(out.end(), tail.begin(), tail.end());
  }
  return out;
}

// Greedy word wrap; a word wider than the line is cut between letters. Stops (lines incomplete, measureOut
// set) when the layout has measured enough.
std::vector<std::string> wrap(Ctx& c, const Font& f, const std::string& s, const int w) {
  std::vector<std::string> lines;
  std::string cur;
  size_t i = 0;
  int tw = 0;
  while (i <= s.size()) {
    size_t sp = s.find(' ', i);
    if (sp == std::string::npos) sp = s.size();
    std::string word = s.substr(i, sp - i);
    i = sp + 1;
    if (word.empty()) continue;
    std::string tryLine = cur.empty() ? word : cur + " " + word;
    if (!c.measure(f, tryLine, tw)) return lines;
    if (tw <= w) {
      cur = std::move(tryLine);
      continue;
    }
    if (!cur.empty()) lines.push_back(cur);
    cur.clear();
    // The word alone; cut it when it is still too wide.
    while (true) {
      if (!c.measure(f, word, tw)) return lines;
      if (tw <= w) break;
      std::string head;
      const auto* p = reinterpret_cast<const unsigned char*>(word.c_str());
      while (*p) {
        const auto* q = p;
        std::string next = head;
        utf8AppendCodepoint(utf8NextCodepoint(&q), next);
        if (!head.empty()) {
          if (!c.measure(f, next, tw)) return lines;
          if (tw > w) break;
        }
        head = std::move(next);
        p = q;
      }
      lines.push_back(head);
      word = word.substr(head.size());
      if (word.empty()) break;
    }
    cur = word;
  }
  if (!cur.empty()) lines.push_back(cur);
  return lines;
}

void drawText(Ctx& c, JsonObjectConst it, const Off& o) {
  // Fitting measures the text in many fonts: only when it may still be drawn.
  if (c.st.drawn >= MAX_OPS || c.st.work >= MAX_WORK || c.measureOut) {
    c.st.skipped++;
    return;
  }
  const std::string s = expand(c, str(it, "text"));
  if (s.empty()) return;
  const int x = c.X(o, num(it, "x", 0.0)), y = c.Y(o, num(it, "y", 0.0));
  const int w = c.L(num(it, "w", 0.0)), h = c.L(num(it, "h", 0.0));
  if (w <= 0) return;
  const int size = c.L(num(it, "size", 24.0));
  const int maxLines = clampInt(num(it, "lines", 1), 1, 8);
  const Col col = color(str(it, "color"), Col::Ink, true);
  if (col == Col::None) return;
  const auto fonts = fontsFor(c.c, family(str(it, "font")), it["bold"] | false, s.c_str());
  if (fonts.empty()) return;
  size_t k = 0;
  while (k + 1 < fonts.size() && fonts[k]->px > size) k++;
  // Biggest font first: one line, else wrapped into the allowed lines when the box is tall enough;
  // then smaller. The smallest wraps as much as it needs: the text is never cut (owner rule: no "…").
  std::vector<std::string> lines;
  const Font* f = nullptr;
  int tw = 0;
  for (; k < fonts.size(); k++) {
    f = fonts[k];
    if (!c.measure(*f, s, tw)) break;
    if (tw <= w) {
      lines = {s};
      break;
    }
    if (maxLines > 1) {
      auto wl = wrap(c, *f, s, w);
      if (c.measureOut) break;
      const int lineH = f->inkH > 0 ? f->inkH : f->lineH;
      if (static_cast<int>(wl.size()) <= maxLines && (h <= 0 || static_cast<int>(wl.size()) * lineH <= h)) {
        lines = std::move(wl);
        break;
      }
    }
  }
  if (lines.empty() && !c.measureOut) lines = wrap(c, *f, s, w);
  // Out of measuring: the layout drew enough text; this one is skipped rather than drawn half.
  if (c.measureOut) {
    c.st.skipped++;
    return;
  }
  // Digits: place by the digits' own height (their line box has a tall empty top).
  const int lineH = f->inkH > 0 ? f->inkH : f->lineH;
  const int blockH = static_cast<int>(lines.size()) * lineH;
  const char* va = str(it, "valign", "top");
  int ty = y;
  if (h > 0 && !strcmp(va, "middle")) ty = y + (h - blockH) / 2;
  else if (h > 0 && !strcmp(va, "bottom")) ty = y + h - blockH;
  const char* al = str(it, "align", "left");
  for (const auto& line : lines) {
    const int lw = c.c.textWidth(*f, line.c_str());
    if (!c.work(static_cast<long>(std::min(lw, MAX_LEN)) * f->lineH) || !c.budget()) return;
    int tx = x;
    if (!strcmp(al, "center")) tx = x + (w - lw) / 2;
    else if (!strcmp(al, "right")) tx = x + w - lw;
    c.c.text(*f, tx, ty - f->inkTop, line.c_str(), col);
    ty += lineH;
  }
}

void drawItems(Ctx& c, JsonArrayConst items, const Off& o, int depth);

void drawList(Ctx& c, JsonObjectConst it, const Off& o, const int depth) {
  const char* of = str(it, "of");
  JsonArrayConst pkgList;
  const char* sysList = nullptr;
  int n = 0;
  if (!strncmp(of, "pkg.", 4)) {
    pkgList = c.root["lists"][of + 4].as<JsonArrayConst>();
    n = static_cast<int>(pkgList.size());
  } else if (*of && c.allowed(of)) {
    sysList = of;
    n = c.d.count(of);
  }
  int first = 0;
  int rows = std::min(n, clampInt(num(it, "max", MAX_ROWS), 0, MAX_ROWS));
  if (n > 0 && !strcmp(str(it, "pick"), "day")) {
    std::string yd;
    double v = 0;
    c.d.value("date.yday", yd);
    number(yd, v);
    first = clampInt(v, 0, 1 << 30) % n;
    rows = 1;
  }
  const double dx = num(it, "dx", 0.0), dy = num(it, "dy", 0.0);
  const Off base{o.x + (num(it, "x", 0.0)), o.y + (num(it, "y", 0.0))};
  // Keep the outer row (nested lists).
  const char* sList = c.list;
  JsonVariantConst sItem = c.pkgItem;
  const bool sIn = c.inList;
  const int sIndex = c.index, sCount = c.count;
  for (int k = 0; k < rows && c.step(); k++) {
    c.inList = true;
    c.list = sysList;
    c.index = first + k;
    c.count = n;
    if (!sysList) c.pkgItem = pkgList[first + k];
    drawItems(c, it["items"].as<JsonArrayConst>(), Off{base.x + dx * k, base.y + dy * k}, depth + 1);
  }
  c.list = sList;
  c.pkgItem = sItem;
  c.inList = sIn;
  c.index = sIndex;
  c.count = sCount;
}

void drawItem(Ctx& c, JsonObjectConst it, const Off& o, const int depth) {
  if (!condition(c, str(it, "if", nullptr))) {
    c.st.skipped++;
    return;
  }
  const char* t = str(it, "t");
  const double x = num(it, "x", 0.0), y = num(it, "y", 0.0), w = num(it, "w", 0.0), h = num(it, "h", 0.0);
  if (!strcmp(t, "text")) {
    drawText(c, it, o);
  } else if (!strcmp(t, "box") || !strcmp(t, "bar")) {
    const bool bar = t[1] == 'a';
    const Col fill = color(str(it, "fill"), bar ? Col::Ink : Col::None, false);
    const Col stroke = color(str(it, "stroke"), bar || fill == Col::None ? Col::Ink : Col::None, false);
    const int X = c.X(o, x), Y = c.Y(o, y), Wd = c.L(w), Ht = c.L(h);
    const int lw = c.W(num(it, "lw", (bar ? 2.0 : 1.0)));
    // "r": 0 stays square; a radius is at most half the shorter side.
    const int r = std::min(c.L(num(it, "r", 0.0)), std::min(Wd, Ht) / 2);
    if (Wd <= 0 || Ht <= 0) return;
    const long area = static_cast<long>(Wd) * Ht;
    const long cost = (fill != Col::None ? area : 0) +
                      (stroke != Col::None ? std::min(area, 2L * (Wd + Ht) * lw + 4L * r * r) : 0);
    if (!c.work(cost) || !c.budget()) return;
    if (bar) {
      double v = 0;
      number(expand(c, str(it, "value", "0")), v);
      const double max = num(it, "max", 100.0);
      const double pct = max > 0 ? std::max(0.0, std::min(1.0, v / max)) : 0;
      const int inner = static_cast<int>((Wd - 2 * lw) * pct);
      if (inner > 0 && fill != Col::None) c.c.fillRect(X + lw, Y + lw, inner, Ht - 2 * lw, std::max(0, r - lw), fill);
      if (stroke != Col::None) c.c.strokeRect(X, Y, Wd, Ht, r, lw, stroke);
    } else {
      if (fill != Col::None) c.c.fillRect(X, Y, Wd, Ht, r, fill);
      if (stroke != Col::None) c.c.strokeRect(X, Y, Wd, Ht, r, lw, stroke);
    }
  } else if (!strcmp(t, "line")) {
    const Col stroke = color(str(it, "stroke"), Col::Ink, false);
    const int lw = c.W(num(it, "lw", 1.0));
    int x1 = c.X(o, x), y1 = c.Y(o, y), x2 = c.X(o, num(it, "x2", x)), y2 = c.Y(o, num(it, "y2", y));
    // Only what can show: the line clipped to the target box (widened by the line width).
    if (stroke == Col::None ||
        !clipLine(x1, y1, x2, y2, c.tx - lw, c.ty - lw, c.tx + c.tw + lw, c.ty + c.th + lw)) {
      return;
    }
    const long len = std::max(std::abs(x2 - x1), std::abs(y2 - y1)) + 1;
    if (!c.work(len * lw) || !c.budget()) return;
    c.c.line(x1, y1, x2, y2, lw, stroke);
  } else if (!strcmp(t, "circle")) {
    const Col fill = color(str(it, "fill"), Col::None, false);
    const Col stroke = color(str(it, "stroke"), fill == Col::None ? Col::Ink : Col::None, false);
    const int r = std::min(c.L(num(it, "r", 0.0)), MAX_CIRCLE);
    if (r <= 0 || !c.work(4L * r * r) || !c.budget()) return;
    if (fill != Col::None) c.c.fillCircle(c.X(o, x), c.Y(o, y), r, fill);
    if (stroke != Col::None) c.c.strokeCircle(c.X(o, x), c.Y(o, y), r, c.W(num(it, "lw", 1.0)), stroke);
  } else if (!strcmp(t, "icon")) {
    const std::string name = expand(c, str(it, "name"));
    const Col col = color(str(it, "color"), Col::Ink, false);
    const int Wd = c.L(w), Ht = c.L(h);
    const int size = std::min({Wd, Ht, MAX_ICON});
    if (name.empty() || size <= 0 || col == Col::None || !c.work(static_cast<long>(size) * size) || !c.budget()) return;
    c.c.icon(name.c_str(), c.X(o, x) + Wd / 2, c.Y(o, y) + Ht / 2, size, col);
  } else if (!strcmp(t, "image")) {
    const std::string src = expand(c, str(it, "src"));
    const int Wd = c.L(w), Ht = c.L(h);
    // Package files only.
    if (src.empty() || src[0] == '/' || src.find("..") != std::string::npos || Wd <= 0 || Ht <= 0) return;
    if (c.st.images >= MAX_IMAGES) {
      c.st.imagesSkipped++;
      c.st.skipped++;
      return;
    }
    if (!c.work(static_cast<long>(Wd) * Ht) || !c.budget()) return;
    c.st.images++;
    if (!c.c.image(c.dir + "/" + src, c.X(o, x), c.Y(o, y), Wd, Ht)) c.st.skipped++;
  } else if (!strcmp(t, "group")) {
    if (depth < MAX_DEPTH) drawItems(c, it["items"].as<JsonArrayConst>(), Off{o.x + x, o.y + y}, depth + 1);
  } else if (!strcmp(t, "list")) {
    if (depth < MAX_DEPTH) drawList(c, it, o, depth);
  } else {
    c.st.skipped++;
  }
}

void drawItems(Ctx& c, JsonArrayConst items, const Off& o, const int depth) {
  for (JsonObjectConst it : items) {
    if (!c.step()) {
      c.st.skipped++;
      return;
    }
    drawItem(c, it, o, depth);
  }
}

}  // namespace

std::string upper(const std::string& s) { return mapCase(s, upperCp); }
std::string lower(const std::string& s) { return mapCase(s, lowerCp); }

std::string title(const std::string& s) {
  std::string out;
  bool start = true;
  const auto* p = reinterpret_cast<const unsigned char*>(s.c_str());
  while (*p) {
    const uint32_t cp = utf8NextCodepoint(&p);
    utf8AppendCodepoint(start ? upperCp(cp) : lowerCp(cp), out);
    start = cp == ' ';
  }
  return out;
}

std::vector<Size> sizes(JsonVariantConst doc) {
  std::vector<Size> out;
  for (JsonObjectConst l : doc["layouts"].as<JsonArrayConst>()) {
    const int w = l["size"][0] | 0, h = l["size"][1] | 0;
    out.push_back({w, h});
  }
  return out;
}

int pick(JsonVariantConst doc, const int w, const int h) {
  const auto all = sizes(doc);
  int best = -1;
  double bestDiff = 1e9;
  long bestArea = -1;
  const double want = h > 0 ? static_cast<double>(w) / h : 1;
  for (size_t i = 0; i < all.size(); i++) {
    if (all[i].w <= 0 || all[i].h <= 0) continue;
    if (all[i].w == w && all[i].h == h) return static_cast<int>(i);
    const double diff = std::fabs(std::log(static_cast<double>(all[i].w) / all[i].h / want));
    const long area = static_cast<long>(all[i].w) * all[i].h;
    if (diff < bestDiff - 1e-6 || (std::fabs(diff - bestDiff) <= 1e-6 && area > bestArea)) {
      best = static_cast<int>(i);
      bestDiff = diff;
      bestArea = area;
    }
  }
  return best;
}

bool render(JsonVariantConst doc, int index, Canvas& c, const Data& d, const std::vector<std::string>& perms,
            const std::string& dir, const int tx, const int ty, const int tw, const int th, Stats* st) {
  Stats local;
  Stats& s = st ? *st : local;
  const int level = doc["decl"] | 0;
  if (level < 1 || level > LEVEL || tw <= 0 || th <= 0) return false;
  if (index < 0) index = pick(doc, tw, th);
  JsonObjectConst l = doc["layouts"][index];
  const int lw = l["size"][0] | 0, lh = l["size"][1] | 0;
  if (l.isNull() || lw <= 0 || lh <= 0) return false;
  Ctx ctx(c, d, perms, doc.as<JsonObjectConst>(), dir, s);
  ctx.tx = tx;
  ctx.ty = ty;
  ctx.tw = tw;
  ctx.th = th;
  ctx.s = std::min(static_cast<double>(tw) / lw, static_cast<double>(th) / lh);
  ctx.ox = tx + (tw - lw * ctx.s) / 2;
  ctx.oy = ty + (th - lh * ctx.s) / 2;
  s.w = lw;
  s.h = lh;
  const Col bg = color(l["bg"] | "paper", Col::Paper, false);
  if (bg != Col::Paper && bg != Col::None && ctx.work(static_cast<long>(tw) * th)) c.fillRect(tx, ty, tw, th, 0, bg);
  drawItems(ctx, l["items"].as<JsonArrayConst>(), Off{}, 0);
  return true;
}

}  // namespace inkdesk::decl
