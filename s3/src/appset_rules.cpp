#pragma GCC optimize("Os")  // cold code: built small (the app slot is nearly full)
// The settings value rules (appset_rules.h): sdk/packages/cli/src/settings.ts checkValue, ported line for line.
#include "appset_rules.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace appset {
namespace {

bool isNum(JsonVariantConst v) { return v.is<double>() && std::isfinite(v.as<double>()); }

}  // namespace

double num(JsonVariantConst v) {
  const double d = v.as<double>();
  const float f = (float)d;
  if ((double)f != d) return d;
  char b[24];
  for (int p = 1; p <= 9; p++) {
    snprintf(b, sizeof b, "%.*g", p, d);
    if ((float)strtod(b, nullptr) == f) return strtod(b, nullptr);
  }
  return d;
}

namespace {
bool isDigit(char c) { return c >= '0' && c <= '9'; }

// JavaScript's \s at p (UTF-8): its length in bytes, 0 when it is not white space.
int spaceAt(const char* p) {
  const unsigned char c = (unsigned char)p[0];
  if (c == ' ' || (c >= 9 && c <= 13)) return 1;
  if (c < 0xc2) return 0;
  const unsigned char c1 = (unsigned char)p[1], c2 = c1 ? (unsigned char)p[2] : 0;
  if (c == 0xc2 && c1 == 0xa0) return 2;                                          // U+00A0
  if (c == 0xe1 && c1 == 0x9a && c2 == 0x80) return 3;                             // U+1680
  if (c == 0xe2 && c1 == 0x80 && ((c2 >= 0x80 && c2 <= 0x8a) || c2 == 0xa8 || c2 == 0xa9 || c2 == 0xaf)) return 3;
  if (c == 0xe2 && c1 == 0x81 && c2 == 0x9f) return 3;                             // U+205F
  if (c == 0xe3 && c1 == 0x80 && c2 == 0x80) return 3;                             // U+3000
  if (c == 0xef && c1 == 0xbb && c2 == 0xbf) return 3;                             // U+FEFF
  return 0;
}

// /[\u0000-\u0008\u000b\u000c\u000e-\u001f\u007f]/, and \r \n unless `lines`.
bool hasControl(const char* s, bool lines) {
  for (; *s; s++) {
    const unsigned char c = (unsigned char)*s;
    if (c == '\n' || c == '\r') {
      if (!lines) return true;
    } else if ((c < 0x20 && c != '\t') || c == 0x7f) {
      return true;
    }
  }
  return false;
}

// /^-?(\d+(\.\d+)?|\.\d+)$/
bool numberSyntax(const char* s) {
  if (*s == '-') s++;
  const char* d = s;
  while (isDigit(*s)) s++;
  const bool intPart = s > d;
  if (*s == '.') {
    const char* f = ++s;
    while (isDigit(*s)) s++;
    if (s == f) return false;
  } else if (!intPart) {
    return false;
  }
  return *s == 0;
}

int daysIn(int y, int m) {
  static const unsigned char D[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  return m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 29 : D[m - 1];
}

// ---- strict JSON (JSON.parse's grammar), for "list" and "city" values
const char* ws(const char* p) {
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
  return p;
}
bool isHex(char c) { return isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

const char* value(const char* p, int depth);

const char* string(const char* p) {
  if (*p++ != '"') return nullptr;
  for (;;) {
    const unsigned char c = (unsigned char)*p++;
    if (c == '"') return p;
    if (c < 0x20) return nullptr;  // also the end of the text
    if (c != '\\') continue;
    const char e = *p++;
    if (e == 'u') {
      for (int i = 0; i < 4; i++)
        if (!isHex(*p++)) return nullptr;
    } else if (!strchr("\"\\/bfnrt", e) || !e) {
      return nullptr;
    }
  }
}

const char* number(const char* p) {
  if (*p == '-') p++;
  if (*p == '0') p++;
  else if (isDigit(*p)) while (isDigit(*p)) p++;
  else return nullptr;
  if (*p == '.') {
    if (!isDigit(*++p)) return nullptr;
    while (isDigit(*p)) p++;
  }
  if (*p == 'e' || *p == 'E') {
    p++;
    if (*p == '+' || *p == '-') p++;
    if (!isDigit(*p)) return nullptr;
    while (isDigit(*p)) p++;
  }
  return p;
}

const char* items(const char* p, int depth, char close, bool keyed) {
  p = ws(p + 1);
  if (*p == close) return p + 1;
  for (;;) {
    if (keyed) {
      if (!(p = string(p))) return nullptr;
      p = ws(p);
      if (*p++ != ':') return nullptr;
    }
    if (!(p = value(p, depth))) return nullptr;
    if (*p == close) return p + 1;
    if (*p++ != ',') return nullptr;
    p = ws(p);
  }
}

// One value and the white space after it; nullptr when the text is not JSON there.
const char* value(const char* p, int depth) {
  p = ws(p);
  if (depth > 16) return nullptr;
  const char* q;
  switch (*p) {
    case '{': q = items(p, depth + 1, '}', true); break;
    case '[': q = items(p, depth + 1, ']', false); break;
    case '"': q = string(p); break;
    case 't': q = strncmp(p, "true", 4) ? nullptr : p + 4; break;
    case 'f': q = strncmp(p, "false", 5) ? nullptr : p + 5; break;
    case 'n': q = strncmp(p, "null", 4) ? nullptr : p + 4; break;
    default: q = number(p);
  }
  return q ? ws(q) : nullptr;
}

// JSON.parse(raw) into doc; false when JSON.parse would throw.
bool parse(const char* raw, JsonDocument& doc) {
  return strictJson(raw) && deserializeJson(doc, raw, DeserializationOption::NestingLimit(16)) == DeserializationError::Ok;
}

// String.prototype.trim()'s length in characters is at least 1.
bool hasNonSpace(const char* s) {
  while (*s) {
    const int n = spaceAt(s);
    if (!n) return true;
    s += n;
  }
  return false;
}

Check bad(Err e, double n = 0) {
  Check c;
  c.err = e, c.n = n;
  return c;
}

}  // namespace

size_t chars(const char* s) {
  size_t n = 0;
  for (; *s; s++) n += ((unsigned char)*s & 0xc0) != 0x80;
  return n;
}

bool strictJson(const char* s) {
  const char* e = value(s, 0);
  return e && !*e;
}

size_t numText(double v, char* out, size_t cap) {
  if (v == 0) v = 0;  // -0 prints "0", as String(-0)
  char b[48];
  int n = 0;
  const double a = std::fabs(v);
  // The fewest decimals that read back as the same number; JS prints 1e21 and up, and under 1e-6, with an exponent.
  for (int d = 0; d <= 20; d++) {
    n = a >= 1e21 || (a < 1e-6 && a > 0) ? snprintf(b, sizeof b, "%.*g", d + 1, v) : snprintf(b, sizeof b, "%.*f", d, v);
    if (strtod(b, nullptr) == v) break;
  }
  return snprintf(out, cap, "%s", b) < 0 ? 0 : (size_t)n;
}

Check check(JsonObjectConst f, const char* raw) {
  if (!raw) raw = "";
  const char* type = f["type"] | "";
  const size_t len = chars(raw);
  if (len > (size_t)MAX_VALUE) return bad(E_LONG, MAX_VALUE);
  if (hasControl(raw, !strcmp(type, "text"))) return bad(E_CONTROL);
  if (!strcmp(type, "string") || !strcmp(type, "text")) {
    const double max = isNum(f["max"]) ? num(f["max"]) : (type[0] == 's' ? STRING_DEFAULT_MAX : TEXT_DEFAULT_MAX);
    return len <= max ? Check() : bad(E_LONG, max);
  }
  if (!strcmp(type, "number")) {
    if (!numberSyntax(raw)) return bad(E_NUMBER);
    const double v = strtod(raw, nullptr);
    const bool hasMin = isNum(f["min"]);
    const double min = hasMin ? num(f["min"]) : 0;
    if (hasMin && v < min) return bad(E_MIN, min);
    if (isNum(f["max"]) && v > num(f["max"])) return bad(E_MAX, num(f["max"]));
    if (f["int"].as<bool>() && v != std::floor(v)) return bad(E_INT);
    if (isNum(f["step"]) && num(f["step"]) > 0) {
      const double step = num(f["step"]), n = (v - min) / step;
      if (std::fabs(n - std::floor(n + 0.5)) > 1e-9) return bad(E_STEP, step);
    }
    return Check();
  }
  if (!strcmp(type, "bool")) return !strcmp(raw, "1") || !strcmp(raw, "0") ? Check() : bad(E_BOOL);
  if (!strcmp(type, "enum")) {
    for (JsonVariantConst o : f["options"].as<JsonArrayConst>())
      if (o.is<JsonObjectConst>() && o["v"].is<const char*>() && !strcmp(o["v"].as<const char*>(), raw)) return Check();
    return bad(E_ENUM);
  }
  if (!strcmp(type, "list")) {
    JsonDocument d;
    if (!parse(raw, d) || !d.is<JsonArrayConst>()) return bad(E_LIST);
    JsonArrayConst a = d.as<JsonArrayConst>();
    const double max = isNum(f["max_items"]) ? num(f["max_items"]) : LIST_MAX_ITEMS;
    if (a.size() > max) return bad(E_ITEMS, max);
    JsonDocument dflt;
    JsonObjectConst of = f["of"].as<JsonObjectConst>();
    if (of.isNull()) dflt["type"] = "string", of = dflt.as<JsonObjectConst>();
    for (JsonVariantConst x : a) {
      if (!x.is<const char*>()) return bad(E_LIST);
      Check c = check(of, x.as<const char*>());
      if (!c) return c.item = true, c;
    }
    return Check();
  }
  if (!strcmp(type, "time")) {
    const bool shape = isDigit(raw[0]) && isDigit(raw[1]) && raw[2] == ':' && isDigit(raw[3]) && isDigit(raw[4]) && !raw[5];
    return shape && atoi(raw) < 24 && atoi(raw + 3) < 60 ? Check() : bad(E_TIME);
  }
  if (!strcmp(type, "date")) {
    bool shape = strlen(raw) == 10 && raw[4] == '-' && raw[7] == '-';
    for (int i = 0; shape && i < 10; i++) shape = i == 4 || i == 7 || isDigit(raw[i]);
    if (!shape) return bad(E_DATE);
    const int y = atoi(raw), m = atoi(raw + 5), d = atoi(raw + 8);
    // JS's Date.UTC reads the years 0-99 as 1900-1999, so the SDK refuses them: the same here.
    return y >= 100 && m >= 1 && m <= 12 && d >= 1 && d <= daysIn(y, m) ? Check() : bad(E_REALDATE);
  }
  if (!strcmp(type, "color")) {
    bool ok = raw[0] == '#' && strlen(raw) == 7;
    for (int i = 1; ok && i < 7; i++) ok = isHex(raw[i]);
    return ok ? Check() : bad(E_COLOR);
  }
  if (!strcmp(type, "url")) {
    // /^https:\/\/[^\s/?#]+\.[^\s/?#]+([/?#]\S*)?$/: a host with a dot inside it, then nothing or [/?#] and no space.
    bool ok = len <= (size_t)URL_MAX && !strncmp(raw, "https://", 8);
    const char* h = raw + 8;
    const char* p = h;
    while (ok && *p && !strchr("/?#", *p) && !spaceAt(p)) p++;
    if (ok) {
      const char* dot = nullptr;
      for (const char* q = h + 1; q < p - 1 && !dot; q++)
        if (*q == '.') dot = q;
      ok = dot != nullptr;
      for (const char* q = p; ok && *q; q++) ok = !spaceAt(q);
    }
    return ok ? Check() : bad(E_URL, URL_MAX);
  }
  if (!strcmp(type, "city")) {
    JsonDocument d;
    if (!parse(raw, d) || !d.is<JsonObjectConst>()) return bad(E_CITY);
    const char* name = d["name"].is<const char*>() ? d["name"].as<const char*>() : nullptr;
    if (!name || !hasNonSpace(name) || chars(name) > 80) return bad(E_CITY);
    if (!isNum(d["lat"]) || !isNum(d["lon"])) return bad(E_CITY);
    const double lat = d["lat"], lon = d["lon"];
    return lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180 ? Check() : bad(E_CITY);
  }
  if (!strcmp(type, "photo")) {
    bool ok = len >= 1 && len <= 40;
    for (const char* q = raw; ok && *q; q++) ok = (*q >= 'a' && *q <= 'z') || isDigit(*q) || *q == '_' || *q == '-';
    return ok ? Check() : bad(E_PHOTO);
  }
  if (!strcmp(type, "secret")) return bad(E_SECRET);
  if (!strcmp(type, "section")) return bad(E_SECTION);
  return bad(E_TYPE);
}

size_t defaultText(JsonObjectConst f, char* out, size_t cap) {
  JsonVariantConst d = f["default"];
  const char* type = f["type"] | "";
  if (cap) out[0] = 0;
  if (d.isNull() || !strcmp(type, "secret") || !strcmp(type, "section")) return 0;
  if (!strcmp(type, "bool")) return snprintf(out, cap, "%s", d.as<bool>() ? "1" : "0");
  if (d.is<const char*>()) return snprintf(out, cap, "%s", d.as<const char*>());
  if (isNum(d)) return numText(num(d), out, cap);
  return measureJson(d) < cap ? serializeJson(d, out, cap) : measureJson(d) + 1;  // a list: JSON.stringify
}

bool urlMatches(const char* pattern, const char* prefix, const char* url) {
  if (!pattern || !url) return false;
  // The pattern with the url field's value in place of "{key}".
  char pat[640];
  if (pattern[0] == '{') {
    const char* close = strchr(pattern, '}');
    if (!close || !prefix || !*prefix) return false;
    if (snprintf(pat, sizeof pat, "%s%s", prefix, close + 1) >= (int)sizeof pat) return false;
  } else if (snprintf(pat, sizeof pat, "%s", pattern) >= (int)sizeof pat) {
    return false;
  }
  if (strncmp(pat, "https://", 8) || strncmp(url, "https://", 8)) return false;
  // The host is compared whole (any case): "https://api.example.com*" never matches api.example.com.evil.com.
  const char* ph = pat + 8;
  const char* uh = url + 8;
  size_t pn = strcspn(ph, "/?#*"), un = strcspn(uh, "/?#");
  if (pn != un || !pn) return false;
  for (size_t i = 0; i < pn; i++) {
    char a = ph[i], b = uh[i];
    if (a >= 'A' && a <= 'Z') a += 32;
    if (b >= 'A' && b <= 'Z') b += 32;
    if (a != b) return false;
  }
  // The rest: '*' matches any run of characters.
  const char* p = ph + pn;
  const char* s = uh + un;
  const char *star = nullptr, *mark = nullptr;
  while (*s) {
    if (*p == '*') star = p++, mark = s;
    else if (*p == *s) p++, s++;
    else if (star) p = star + 1, s = ++mark;
    else return false;
  }
  while (*p == '*') p++;
  return !*p;
}

}  // namespace appset
