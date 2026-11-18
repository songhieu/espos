#pragma GCC optimize("Os")  // a cold screen: built small (the firmware must fit 3.75 MB)
// Vietnamese Telex input engine. See telex.h.
//
// Each character of the word is split into base letter + modifier (hat, breve, horn, stroke)
// + tone. A key edits that model, then the tone is placed again from scratch on the syllable
// (initial consonant + vowel nucleus + final consonant), and the word is composed back to
// precomposed Unicode (NFC). The engine keeps no state between calls.

#include "telex.h"

#include <cstdint>
#include <cstring>

namespace telex {
namespace {

// ---- Letter model -------------------------------------------------------------------------

enum : uint8_t { kNone = 0, kHat, kBreve, kHorn, kStroke };

// Vowel forms (base + modifier) in row order of kVowel. â before ă and ô before ơ: that is
// also the order of the long-press picker.
enum : int { fA, fAa, fAw, fE, fEe, fI, fO, fOo, fOw, fU, fUw, fY, kForms };
const char kFormBase[kForms] = {'a', 'a', 'a', 'e', 'e', 'i', 'o', 'o', 'o', 'u', 'u', 'y'};
const uint8_t kFormMod[kForms] = {kNone, kHat, kBreve, kNone, kHat, kNone,
                                  kNone, kHat, kHorn,  kNone, kHorn, kNone};

// Lowercase precomposed vowels, [form][tone]. Tone 0 none, 1 sắc, 2 huyền, 3 hỏi, 4 ngã, 5 nặng.
const char16_t kVowel[kForms][6] = {
    {0x0061, 0x00E1, 0x00E0, 0x1EA3, 0x00E3, 0x1EA1},  // a á à ả ã ạ
    {0x00E2, 0x1EA5, 0x1EA7, 0x1EA9, 0x1EAB, 0x1EAD},  // â ấ ầ ẩ ẫ ậ
    {0x0103, 0x1EAF, 0x1EB1, 0x1EB3, 0x1EB5, 0x1EB7},  // ă ắ ằ ẳ ẵ ặ
    {0x0065, 0x00E9, 0x00E8, 0x1EBB, 0x1EBD, 0x1EB9},  // e é è ẻ ẽ ẹ
    {0x00EA, 0x1EBF, 0x1EC1, 0x1EC3, 0x1EC5, 0x1EC7},  // ê ế ề ể ễ ệ
    {0x0069, 0x00ED, 0x00EC, 0x1EC9, 0x0129, 0x1ECB},  // i í ì ỉ ĩ ị
    {0x006F, 0x00F3, 0x00F2, 0x1ECF, 0x00F5, 0x1ECD},  // o ó ò ỏ õ ọ
    {0x00F4, 0x1ED1, 0x1ED3, 0x1ED5, 0x1ED7, 0x1ED9},  // ô ố ồ ổ ỗ ộ
    {0x01A1, 0x1EDB, 0x1EDD, 0x1EDF, 0x1EE1, 0x1EE3},  // ơ ớ ờ ở ỡ ợ
    {0x0075, 0x00FA, 0x00F9, 0x1EE7, 0x0169, 0x1EE5},  // u ú ù ủ ũ ụ
    {0x01B0, 0x1EE9, 0x1EEB, 0x1EED, 0x1EEF, 0x1EF1},  // ư ứ ừ ử ữ ự
    {0x0079, 0x00FD, 0x1EF3, 0x1EF7, 0x1EF9, 0x1EF5},  // y ý ỳ ỷ ỹ ỵ
};
const char32_t kDStroke = 0x0111;  // đ (Đ = 0x0110)

const char kToneKeys[] = "sfrxj";  // tone 1..5

// Every vowel nucleus Vietnamese allows, VNI style: 6 = hat, 7 = horn, 8 = breve.
// A typed plain vowel also matches its modified form, so "uo" (on the way to "ươ") is valid.
const char kNuclei[] =
    "a ai ao au a6 a6u a6y a8 ay e eo e6 e6u i ia ie6 ie6u iu o oa oa8 oai oao oay oe oeo oi "
    "oo o6 o6i o7 o7i u ua ua6 ua6y ue6 ui uo6 uo6i uo7 uy uya uye6 uyu u7 u7a u7i u7u u7o7 "
    "u7o7i u7o7u y ye6 ye6u";
// Initial consonants ('#' = đ). "qu" and "gi" are handled in analyze().
const char kInitials[] = " b c ch d # g gh h k kh l m n ng ngh nh p ph r s t th tr v x ";
const char kFinals[] = " c ch m n ng nh p t ";

// Uppercase of a lowercase letter from kVowel, a-z or đ (not a general case mapping).
char32_t upperOf(char32_t c) {
  if (c >= 'a' && c <= 'z') return c - 0x20;
  if (c >= 0xE0 && c <= 0xFE) return c - 0x20;                             // Latin-1
  if ((c >= 0x100 && c <= 0x1B0) || (c >= 0x1EA0 && c <= 0x1EF9)) return c - 1;  // pairs
  return c;
}

struct Ch {
  char32_t raw;   // the original code point; used when base == 0
  char base;      // lowercase ASCII letter, 0 when not a letter
  uint8_t mod;    // kNone, kHat, kBreve, kHorn, kStroke
  uint8_t tone;   // 0..5
  bool upper;
};

int formOf(char base, uint8_t mod) {
  for (int f = 0; f < kForms; ++f)
    if (kFormBase[f] == base && kFormMod[f] == mod) return f;
  return -1;
}

Ch decode(char32_t c) {
  Ch ch{c, 0, kNone, 0, false};
  if (c >= 'a' && c <= 'z') {
    ch.base = char(c);
  } else if (c >= 'A' && c <= 'Z') {
    ch.base = char(c - 'A' + 'a');
    ch.upper = true;
  } else if (c == kDStroke || c == kDStroke - 1u) {
    ch.base = 'd';
    ch.mod = kStroke;
    ch.upper = c != kDStroke;
  } else if (c >= 0xC0) {
    for (int f = 0; f < kForms; ++f)
      for (int t = 0; t < 6; ++t) {
        const char32_t lo = kVowel[f][t];
        if (c == lo || c == upperOf(lo)) {
          ch.base = kFormBase[f];
          ch.mod = kFormMod[f];
          ch.tone = uint8_t(t);
          ch.upper = c != lo;
          return ch;
        }
      }
  }
  return ch;
}

char32_t encode(const Ch& ch) {
  if (!ch.base) return ch.raw;
  char32_t lo = char32_t(ch.base);
  if (ch.mod == kStroke) {
    lo = kDStroke;
  } else {
    const int f = formOf(ch.base, ch.mod);
    if (f >= 0) lo = kVowel[f][ch.tone];
  }
  return ch.upper ? upperOf(lo) : lo;
}

bool isVowel(const Ch& ch) { return ch.base && std::strchr("aeiouy", ch.base); }

// The longest word worth analysing: "nghiêng" is 7 letters, so anything longer than this
// cannot become a syllable and is passed through untouched.
constexpr int kMaxWord = 16;

struct Word {
  Ch c[kMaxWord + 2];
  int n = 0;
  void push(const Ch& ch) {
    if (n < kMaxWord + 2) c[n++] = ch;
  }
};

// ---- Syllable analysis --------------------------------------------------------------------

struct Syl {
  bool ok;     // a plausible Vietnamese syllable (or the start of one)
  int ns, ne;  // vowel nucleus [ns, ne); empty when the word has no vowel yet
  bool final;  // has a final consonant
};

bool inSet(const char* set, const char* s, int len) {
  char key[8];
  key[0] = ' ';
  std::memcpy(key + 1, s, size_t(len));
  key[len + 1] = ' ';
  key[len + 2] = 0;
  return std::strstr(set, key) != nullptr;
}

bool nucleusValid(const Ch* v, int len) {
  if (len < 1 || len > 3) return false;
  for (const char* p = kNuclei; *p;) {
    int k = 0;
    bool match = true;
    while (*p && *p != ' ') {
      const char base = *p++;
      uint8_t mod = kNone;
      if (*p == '6') mod = kHat;
      else if (*p == '7') mod = kHorn;
      else if (*p == '8') mod = kBreve;
      if (mod != kNone) ++p;
      if (k >= len || v[k].base != base || (v[k].mod != mod && v[k].mod != kNone)) match = false;
      ++k;
    }
    if (*p == ' ') ++p;
    if (match && k == len) return true;
  }
  return false;
}

char consKey(const Ch& ch) { return ch.mod == kStroke ? '#' : ch.base; }

// Tones are ignored here; the caller strips them first.
Syl analyze(const Ch* c, int n) {
  Syl s{false, 0, 0, false};
  for (int i = 0; i < n; ++i)
    if (!c[i].base) return s;

  int i = 0;
  char k[4];
  int kl = 0;
  while (i < n && !isVowel(c[i])) {
    if (kl == 3) return s;
    k[kl++] = consKey(c[i++]);
  }
  if (kl == 1 && k[0] == 'q') {
    // "qu": the u belongs to the consonant when a vowel follows it.
    if (i < n) {
      if (c[i].base != 'u' || c[i].mod != kNone) return s;
      if (i + 1 < n && isVowel(c[i + 1])) ++i;
    }
  } else if (kl > 0 && !inSet(kInitials, k, kl)) {
    return s;
  } else if (kl == 1 && k[0] == 'g' && i + 1 < n && c[i].base == 'i' && c[i].mod == kNone &&
             isVowel(c[i + 1])) {
    ++i;  // "gi" + vowel: the i belongs to the consonant
  }

  s.ns = i;
  while (i < n && isVowel(c[i])) ++i;
  s.ne = i;

  kl = 0;
  while (i < n) {
    if (isVowel(c[i]) || kl == 2) return s;
    k[kl++] = consKey(c[i++]);
  }
  if (kl > 0 && !inSet(kFinals, k, kl)) return s;
  if (s.ne > s.ns && !nucleusValid(c + s.ns, s.ne - s.ns)) return s;
  s.final = kl > 0;
  s.ok = true;
  return s;
}

// Traditional placement (hòa, thủy). Needs a non-empty nucleus.
int tonePos(const Ch* c, const Syl& s) {
  for (int i = s.ns; i < s.ne; ++i)
    if (c[i].base == 'o' && c[i].mod == kHorn) return i;  // ơ, also in ươ
  for (int i = s.ns; i < s.ne; ++i)
    if (c[i].mod != kNone) return i;  // ă â ê ô ư
  const int len = s.ne - s.ns;
  if (len == 1) return s.ns;
  if (len >= 3) return s.ns + 1;
  return s.final ? s.ns + 1 : s.ns;
}

void placeTone(Word& w, uint8_t tone, int toneAt) {
  const Syl s = analyze(w.c, w.n);
  int at = (s.ok && s.ne > s.ns) ? tonePos(w.c, s) : toneAt;  // not a syllable: leave it be
  if (at < 0 || at >= w.n || !isVowel(w.c[at])) {
    at = -1;
    for (int i = w.n - 1; i >= 0 && at < 0; --i)
      if (isVowel(w.c[i])) at = i;
  }
  if (at >= 0) w.c[at].tone = tone;
}

// ---- Keys ---------------------------------------------------------------------------------

// The key goes in as a letter. A trailing bare ư that makes the word impossible came from a
// lone w meant as English ("website", "tweet"), so it turns back into w.
void appendPlain(Word& w, const Ch& key, int toneAt) {
  const int last = w.n - 1;
  const bool bareUw =
      last >= 0 && last != toneAt && w.c[last].base == 'u' && w.c[last].mod == kHorn;
  w.push(key);
  if (bareUw && !analyze(w.c, w.n).ok) {
    w.c[last].base = 'w';
    w.c[last].mod = kNone;
  }
}

// dd: the initial d becomes đ. The second d is the key itself or a trailing d ("didd" -> "đi").
bool applyStroke(Word& w, const Ch& key) {
  if (w.n == 0) return false;
  Ch& first = w.c[0];
  Ch& last = w.c[w.n - 1];
  if (first.base != 'd' || last.base != 'd') return false;
  if (last.mod == kStroke) {  // "đ" + d -> "dd"
    last.mod = kNone;
    w.push(key);
    return true;
  }
  if (w.n == 1) {  // "d" + d -> "đ"
    first.mod = kStroke;
    return true;
  }
  if (first.mod == kStroke) {  // "đid" + d -> "didd"
    first.mod = kNone;
    w.push(key);
    return true;
  }
  if (!analyze(w.c, w.n - 1).ok) return false;
  first.mod = kStroke;  // "did" + d -> "đi"
  --w.n;
  return true;
}

// aa ee oo: hat on the matching vowel of the nucleus, or undo it.
bool applyHat(Word& w, const Syl& s, const Ch& key) {
  const char b = key.base;
  for (int i = s.ns; i < s.ne; ++i)
    if (w.c[i].base == b && w.c[i].mod == kHat) {
      w.c[i].mod = kNone;
      w.push(key);
      return true;
    }
  for (int i = s.ne - 1; i >= s.ns; --i) {
    Ch& v = w.c[i];
    if (v.base != b) continue;
    const uint8_t old = v.mod;
    // ươ + o -> uô: the u drops its horn along with the o.
    Ch* u = (old == kHorn && i > s.ns && w.c[i - 1].base == 'u' && w.c[i - 1].mod == kHorn)
                ? &w.c[i - 1]
                : nullptr;
    v.mod = kHat;
    if (u) u->mod = kNone;
    if (nucleusValid(w.c + s.ns, s.ne - s.ns)) return true;
    v.mod = old;
    if (u) u->mod = kHorn;
  }
  return false;
}

// w: uo -> ươ, a -> ă, o -> ơ, u -> ư; undo; or a new ư.
bool applyW(Word& w, const Syl& s, const Ch& key, int toneAt) {
  Ch* nuc = w.c + s.ns;
  const int len = s.ne - s.ns;
  for (int i = 0; i + 1 < len; ++i) {
    Ch& u = nuc[i];
    Ch& o = nuc[i + 1];
    if (u.base != 'u' || o.base != 'o' || (u.mod == kHorn && o.mod == kHorn)) continue;
    const uint8_t mu = u.mod, mo = o.mod;
    u.mod = o.mod = kHorn;
    if (nucleusValid(nuc, len)) return true;
    u.mod = mu;
    o.mod = mo;
  }
  for (int i = len - 1; i >= 0; --i) {  // nearest the cursor first
    Ch& v = nuc[i];
    const uint8_t want = v.base == 'a' ? kBreve : (v.base == 'o' || v.base == 'u') ? kHorn : kNone;
    if (want == kNone || v.mod == want) continue;
    const uint8_t old = v.mod;
    v.mod = want;
    if (nucleusValid(nuc, len)) return true;
    v.mod = old;
  }
  bool hooked = false;
  for (int i = 0; i < len; ++i)
    if (nuc[i].mod == kBreve || nuc[i].mod == kHorn) hooked = true;
  if (hooked) {
    if (len == 1 && s.ne == w.n && nuc[0].base == 'u' && s.ns != toneAt) {
      nuc[0].base = 'w';  // a trailing bare ư came from a lone w: "ww" -> "w", "tww" -> "tw"
      nuc[0].mod = kNone;
      return true;
    }
    for (int i = 0; i < len; ++i)
      if (nuc[i].mod == kBreve || nuc[i].mod == kHorn) nuc[i].mod = kNone;
    w.push(key);  // "aww" -> "aw"
    return true;
  }
  Ch uw = key;
  uw.base = 'u';
  uw.mod = kHorn;
  w.push(uw);  // "tw" -> "tư"
  if (analyze(w.c, w.n).ok) return true;
  --w.n;
  return false;
}

}  // namespace

std::u32string type(const std::u32string& word, char32_t key) {
  const bool keyUpper = key >= 'A' && key <= 'Z';
  if ((!keyUpper && (key < 'a' || key > 'z')) || word.size() > size_t(kMaxWord)) {
    std::u32string out = word;
    out.push_back(key);
    return out;
  }
  const char k = char(keyUpper ? key - 'A' + 'a' : key);
  const Ch keyCh = decode(key);

  Word w;
  for (char32_t c : word) w.push(decode(c));

  // Lift the tone off; it is placed again after the key.
  uint8_t tone = 0;
  int toneAt = -1;
  for (int i = 0; i < w.n; ++i)
    if (w.c[i].tone) {
      if (!tone) {
        tone = w.c[i].tone;
        toneAt = i;
      }
      w.c[i].tone = 0;
    }

  const Syl syl = analyze(w.c, w.n);
  bool done = false;
  switch (k) {
    case 's': case 'f': case 'r': case 'x': case 'j':
      if (syl.ok && syl.ne > syl.ns) {
        const uint8_t t = uint8_t(std::strchr(kToneKeys, k) - kToneKeys + 1);
        if (tone == t) {  // "ass" -> "as"
          tone = 0;
          w.push(keyCh);
        } else {
          tone = t;
        }
        done = true;
      }
      break;
    case 'z':
      if (tone) {
        tone = 0;
        done = true;
      }
      break;
    case 'd':
      done = applyStroke(w, keyCh);
      break;
    case 'a': case 'e': case 'o':
      done = syl.ok && applyHat(w, syl, keyCh);
      break;
    case 'w':
      done = syl.ok && applyW(w, syl, keyCh, toneAt);
      break;
    default:
      break;
  }
  if (!done) appendPlain(w, keyCh, toneAt);
  if (tone) placeTone(w, tone, toneAt);

  std::u32string out;
  out.reserve(size_t(w.n));
  for (int i = 0; i < w.n; ++i) out.push_back(encode(w.c[i]));
  return out;
}

bool isLetter(char32_t c) { return decode(c).base != 0; }

std::vector<char32_t> variants(char32_t base) {
  const Ch ch = decode(base);
  std::vector<char32_t> out;
  auto add = [&](int form, int tone) {
    const char32_t lo = kVowel[form][tone];
    out.push_back(ch.upper ? upperOf(lo) : lo);
  };
  if (ch.base == 'd') {
    out.push_back(ch.upper ? upperOf(kDStroke) : kDStroke);
    return out;
  }
  const int plain = formOf(ch.base, kNone);
  if (plain < 0) return out;
  int mods[2];
  int nm = 0;
  for (int f = 0; f < kForms; ++f)
    if (kFormBase[f] == ch.base && kFormMod[f] != kNone) mods[nm++] = f;
  // The five tones, then the modified vowels, then their most used tones:
  // one family (ê, ư) gets all five; two families (â ă, ô ơ) get sắc huyền nặng + sắc nặng.
  for (int t = 1; t <= 5; ++t) add(plain, t);
  for (int i = 0; i < nm; ++i) add(mods[i], 0);
  if (nm == 1) {
    for (int t = 1; t <= 5; ++t) add(mods[0], t);
  } else if (nm == 2) {
    add(mods[0], 1);
    add(mods[0], 2);
    add(mods[0], 5);
    add(mods[1], 1);
    add(mods[1], 5);
  }
  return out;
}

std::u32string fromUtf8(const std::string& s) {
  std::u32string out;
  out.reserve(s.size());
  const size_t n = s.size();
  for (size_t i = 0; i < n;) {
    const unsigned char b = static_cast<unsigned char>(s[i]);
    if (b < 0x80) {
      out.push_back(b);
      ++i;
      continue;
    }
    int len;
    char32_t cp;
    if (b >= 0xC2 && b <= 0xDF) {
      len = 2;
      cp = b & 0x1Fu;
    } else if ((b & 0xF0) == 0xE0) {
      len = 3;
      cp = b & 0x0Fu;
    } else if (b >= 0xF0 && b <= 0xF4) {
      len = 4;
      cp = b & 0x07u;
    } else {
      out.push_back(0xFFFD);
      ++i;
      continue;
    }
    int k = 1;
    for (; k < len && i + size_t(k) < n; ++k) {
      const unsigned char cb = static_cast<unsigned char>(s[i + size_t(k)]);
      if ((cb & 0xC0) != 0x80) break;
      cp = (cp << 6) | (cb & 0x3Fu);
    }
    const bool bad = k < len || (len == 3 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))) ||
                     (len == 4 && (cp < 0x10000 || cp > 0x10FFFF));
    if (bad) {
      out.push_back(0xFFFD);  // one replacement per bad lead byte, then resync
      ++i;
      continue;
    }
    out.push_back(cp);
    i += size_t(len);
  }
  return out;
}

std::string toUtf8(const std::u32string& s) {
  std::string out;
  out.reserve(s.size());
  for (char32_t c : s) {
    if (c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) c = 0xFFFD;
    if (c < 0x80) {
      out.push_back(char(c));
    } else if (c < 0x800) {
      out.push_back(char(0xC0 | (c >> 6)));
      out.push_back(char(0x80 | (c & 0x3F)));
    } else if (c < 0x10000) {
      out.push_back(char(0xE0 | (c >> 12)));
      out.push_back(char(0x80 | ((c >> 6) & 0x3F)));
      out.push_back(char(0x80 | (c & 0x3F)));
    } else {
      out.push_back(char(0xF0 | (c >> 18)));
      out.push_back(char(0x80 | ((c >> 12) & 0x3F)));
      out.push_back(char(0x80 | ((c >> 6) & 0x3F)));
      out.push_back(char(0x80 | (c & 0x3F)));
    }
  }
  return out;
}

}  // namespace telex
