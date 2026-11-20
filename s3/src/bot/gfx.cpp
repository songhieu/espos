#include "gfx.h"

#include <string.h>

#include <algorithm>

#include "art.h"
#include "vendor/qrcodegen.h"

namespace bot {
namespace gfx {

uint8_t fb[W * H / 8];

namespace {
int cx0 = 0, cy0 = 0, cx1 = W - 1, cy1 = H - 1;  // the clip box

inline void pxc(int x, int y, bool on) {
  if (x < cx0 || x > cx1 || y < cy0 || y > cy1) return;
  px(x, y, on);
}

// One UTF-8 code point from s at i (i moves past it); 0 at the end. A broken sequence gives '?'.
uint32_t next(const std::string& s, size_t& i) {
  if (i >= s.size()) return 0;
  const uint8_t c = s[i++];
  if (c < 0x80) return c;
  int n = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
  if (!n) return '?';
  uint32_t cp = c & (0x3F >> n);
  while (n--) {
    if (i >= s.size() || ((uint8_t)s[i] & 0xC0) != 0x80) return '?';
    cp = cp << 6 | ((uint8_t)s[i++] & 0x3F);
  }
  return cp;
}

using G = art::Glyph;

const G* find(Font f, uint32_t cp) {
  const G* t = f == F11 ? art::F11_G : f == T5 ? art::T5_G : art::F16B_G;
  int lo = 0, hi = (f == F11 ? art::F11_N : f == T5 ? art::T5_N : art::F16B_N) - 1;
  while (lo <= hi) {
    const int m = (lo + hi) / 2;
    if (t[m].cp == cp) return &t[m];
    if (t[m].cp < cp) lo = m + 1;
    else hi = m - 1;
  }
  return nullptr;
}

const G* glyphOf(Font f, uint32_t cp) {
  const G* g = find(f, cp);
  if (g) return g;
  return find(f, '?');
}

int cellH(Font f) { return f == F11 ? art::F11_H : f == T5 ? art::T5_H : art::F16B_H; }
int cellTop(Font f) { return f == F11 ? art::F11_TOP : f == T5 ? art::T5_TOP : 0; }
const uint8_t* bitsOf(Font f) { return f == F11 ? art::F11_BITS : f == T5 ? art::T5_BITS : art::F16B_BITS; }
}  // namespace

void clear() { memset(fb, 0, sizeof fb); }

void fill(int x0, int y0, int x1, int y1, bool on) {
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++) px(x, y, on);
}

void rect(int x0, int y0, int x1, int y1) {
  for (int x = x0; x <= x1; x++) px(x, y0, true), px(x, y1, true);
  for (int y = y0; y <= y1; y++) px(x0, y, true), px(x1, y, true);
}

void hline(int x0, int x1, int y) {
  for (int x = x0; x <= x1; x++) px(x, y, true);
}

void frameInto(uint16_t id, uint8_t* dst) {
  if (id >= art::FRAMES) return (void)memset(dst, 0, sizeof fb);
  const uint8_t* p = art::FRAME_DATA + art::FRAME_OFF[id];
  const uint8_t* end = art::FRAME_DATA + art::FRAME_OFF[id + 1];
  int o = 0;
  while (p < end && o < (int)sizeof fb) {
    const uint8_t t = *p++;
    if (t & 0x80) {
      const int n = std::min(t & 0x7F, (int)sizeof fb - o);
      memset(dst + o, *p++, n);
      o += n;
    } else {
      const int n = std::min((int)t, (int)sizeof fb - o);
      memcpy(dst + o, p, n);
      p += t, o += n;
    }
  }
}

void frame(uint16_t id) { frameInto(id, fb); }

void invert() {
  for (uint8_t& b : fb) b = ~b;
}

void shift(int dx, int dy) {
  if (!dx && !dy) return;
  static uint8_t t[sizeof fb];
  memcpy(t, fb, sizeof fb);
  clear();
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++)
      if (t[(y >> 3) * W + x] >> (y & 7) & 1) px(x + dx, y + dy, true);
}

void clip(int x0, int y0, int x1, int y1) { cx0 = x0, cy0 = y0, cx1 = x1, cy1 = y1; }
void clipAll() { cx0 = 0, cy0 = 0, cx1 = W - 1, cy1 = H - 1; }

int bitmap(const uint8_t* g, int x, int y, bool on) {
  const int w = g[0], h = g[1], bpr = (w + 7) / 8;
  for (int r = 0; r < h; r++)
    for (int c = 0; c < w; c++)
      if (g[2 + r * bpr + c / 8] & (0x80 >> (c % 8))) pxc(x + c, y + r, on);
  return w;
}

namespace {
// The index of the code point in the set's chars (UTF-8), -1 when it has none.
int digitIndex(const art::DigitSet& d, uint32_t cp) {
  const std::string chars = d.chars;
  size_t i = 0;
  int k = 0;
  while (uint32_t c = next(chars, i)) {
    if (c == cp) return k;
    k++;
  }
  return -1;
}
}  // namespace

int digitsW(uint8_t set, const std::string& s) {
  const art::DigitSet& d = art::DIGIT_SETS[set];
  int w = 0, n = 0;
  size_t i = 0;
  while (uint32_t cp = next(s, i)) {
    const int k = digitIndex(d, cp);
    if (k >= 0) w += d.glyphs[k][0] + d.gap, n++;
  }
  return n ? w - d.gap : 0;
}

void digits(uint8_t set, const std::string& s, int x, int y, int align, bool colon, bool on) {
  const art::DigitSet& d = art::DIGIT_SETS[set];
  const int w = digitsW(set, s);
  if (align == 1) x -= w / 2;
  else if (align == 2) x -= w;
  size_t i = 0;
  while (uint32_t cp = next(s, i)) {
    const int k = digitIndex(d, cp);
    if (k < 0) continue;
    const uint8_t* g = d.glyphs[k];
    if (cp != ':' || colon) bitmap(g, x, y, on);
    x += g[0] + d.gap;
  }
}

std::string upper(const std::string& s) {
  std::string o;
  size_t i = 0;
  auto put = [&](uint32_t c) {
    if (c < 0x80) o += (char)c;
    else if (c < 0x800) o += (char)(0xC0 | c >> 6), o += (char)(0x80 | (c & 63));
    else o += (char)(0xE0 | c >> 12), o += (char)(0x80 | (c >> 6 & 63)), o += (char)(0x80 | (c & 63));
  };
  while (uint32_t c = next(s, i)) {
    if (c >= 'a' && c <= 'z') c -= 32;
    else if (c >= 0xE0 && c <= 0xFE && c != 0xF7) c -= 32;           // à .. þ
    else if (c == 0x103 || c == 0x111 || c == 0x129 || c == 0x169) c -= 1;  // ă đ ĩ ũ
    else if (c == 0x1A1 || c == 0x1B0) c -= 1;                       // ơ ư
    else if (c >= 0x1EA0 && c <= 0x1EF9 && (c & 1)) c -= 1;          // ạ ả ấ ... ỹ
    put(c);
  }
  return o;
}

int lineH(Font f) { return f == F11 ? 13 : f == T5 ? 9 : 18; }  // art2's line heights

int textW(Font f, const std::string& s) {
  int w = 0;
  size_t i = 0;
  while (uint32_t cp = next(s, i)) {
    const G* g = glyphOf(f, cp);
    if (g) w += g->adv;
  }
  return w;
}

bool covers(Font f, const std::string& s) {
  size_t i = 0;
  while (uint32_t cp = next(s, i))
    if (!find(f, cp)) return false;
  return true;
}

void text(Font f, const std::string& s, int x, int y, int align, bool on) {
  if (align == 1) x -= textW(f, s) / 2;
  else if (align == 2) x -= textW(f, s);
  // The cell has room above for the marks over capitals: F11's line top, T5's capitals, at y.
  const int top = y - cellTop(f), h = cellH(f);
  const uint8_t* bits = bitsOf(f);
  size_t i = 0;
  while (uint32_t cp = next(s, i)) {
    const G* g = glyphOf(f, cp);
    if (!g) continue;
    const int bpr = (g->w + 7) / 8;
    for (int r = 0; r < h; r++)
      for (int c = 0; c < g->w; c++)
        if (bits[g->off + r * bpr + c / 8] & (0x80 >> (c % 8))) pxc(x + g->xo + c, top + r, on);
    x += g->adv;
  }
}

std::vector<std::string> wrap(Font f, const std::string& s, int w) {
  std::vector<std::string> lines;
  std::string cur;
  size_t i = 0;
  while (i < s.size()) {
    // the next word (to a space or the end), and the spaces after it
    size_t j = i;
    while (j < s.size() && s[j] != ' ' && s[j] != '\n') j++;
    std::string word = s.substr(i, j - i);
    const bool nl = j < s.size() && s[j] == '\n';
    i = j < s.size() ? j + 1 : j;
    if (word.empty() && !nl) continue;
    std::string tryLine = cur.empty() ? word : cur + " " + word;
    if (textW(f, tryLine) <= w) {
      cur = tryLine;
    } else {
      if (!cur.empty()) lines.push_back(cur), cur.clear();
      // a word wider than the box: cut between letters (never shortened)
      while (textW(f, word) > w) {
        size_t k = 0, last = 0;
        while (k < word.size()) {
          size_t k2 = k;
          next(word, k2);
          if (textW(f, word.substr(0, k2)) > w) break;
          last = k2, k = k2;
        }
        if (!last) last = k;  // a single glyph wider than the box
        lines.push_back(word.substr(0, last));
        word = word.substr(last);
      }
      cur = word;
    }
    if (nl) lines.push_back(cur), cur.clear();
  }
  if (!cur.empty()) lines.push_back(cur);
  return lines;
}

namespace {
uint8_t qrBuf[qrcodegen_BUFFER_LEN_FOR_VERSION(6)], qrTmp[qrcodegen_BUFFER_LEN_FOR_VERSION(6)];
std::string qrText;
int qrSide = 0;  // modules; 0: does not fit version 6

bool encode(const std::string& s) {
  if (s == qrText && qrSide) return true;
  qrText = s;
  qrSide = 0;
  if (!qrcodegen_encodeText(s.c_str(), qrTmp, qrBuf, qrcodegen_Ecc_LOW, 1, 6, qrcodegen_Mask_AUTO, true)) return false;
  qrSide = qrcodegen_getSize(qrBuf);
  return true;
}
}  // namespace

int qrSize(const std::string& s, int scale) { return encode(s) ? (qrSide + 2) * scale : 0; }

int qr(const std::string& s, int x, int y, int scale) {
  const int size = qrSize(s, scale);
  if (!size) return 0;
  fill(x, y, x + size - 1, y + size - 1, true);  // a lit square with dark modules: the usual way round for phones
  for (int my = 0; my < qrSide; my++)
    for (int mx = 0; mx < qrSide; mx++)
      if (qrcodegen_getModule(qrBuf, mx, my))
        fill(x + (mx + 1) * scale, y + (my + 1) * scale, x + (mx + 2) * scale - 1, y + (my + 2) * scale - 1, false);
  return size;
}

}  // namespace gfx
}  // namespace bot
