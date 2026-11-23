// Host tests for the settings value rules (src/appset_rules.cpp) against the SDK's shared cases. Build and run from
// s3/:
//   c++ -std=c++17 -Wall -Wextra -O1 -fsanitize=address,undefined -I src -I .pio/libdeps/s3desk/ArduinoJson/src \
//       src/appset_rules.cpp test_host/appset_test.cpp -o /tmp/appset_test && /tmp/appset_test
// The cases: sdk/packages/sdk/schemas/settings-vectors.json ({field, value, ok}), the same file the SDK, the server's
// app_settings_check and the web form run (docs/manifest-driven.md §1.3). Then the device's own extras: defaults as
// stored text, strict JSON, numbers printed as JS prints them, and secrets' send_to matching.
#include "appset_rules.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace {

int g_run = 0, g_fail = 0;

void expect(bool ok, const std::string& what) {
  ++g_run;
  if (!ok) ++g_fail, std::printf("FAIL %s\n", what.c_str());
}

std::string readFile(const char* path) {
  std::ifstream f(path);
  std::stringstream s;
  s << f.rdbuf();
  return s.str();
}

JsonDocument field(const char* json) {
  JsonDocument d;
  deserializeJson(d, json);
  return d;
}

void vectors(const char* path) {
  const std::string text = readFile(path);
  JsonDocument doc;
  if (deserializeJson(doc, text) != DeserializationError::Ok) {
    expect(false, std::string("read ") + path);
    return;
  }
  int n = 0;
  for (JsonObjectConst c : doc["cases"].as<JsonArrayConst>()) {
    const bool want = c["ok"];
    const appset::Check got = appset::check(c["field"].as<JsonObjectConst>(), c["value"] | "");
    std::string f, v;
    serializeJson(c["field"], f);
    serializeJson(c["value"], v);
    expect(bool(got) == want, "vector " + std::to_string(n) + " " + f + " value " + v + ": want " +
                                  (want ? "ok" : "refused") + ", got err " + std::to_string(got.err));
    n++;
  }
  std::printf("vectors: %d cases from %s\n", n, path);
  expect(n >= 44, "at least 44 vectors");
}

void defaults() {
  char b[256];
  struct D {
    const char* field;
    const char* want;
  } cases[] = {
      {R"({"type":"bool","default":true})", "1"},
      {R"({"type":"bool","default":false})", "0"},
      {R"({"type":"number","default":4})", "4"},
      {R"({"type":"number","default":2.5})", "2.5"},
      {R"({"type":"number","default":100})", "100"},
      {R"({"type":"number","default":-0.25})", "-0.25"},
      {R"({"type":"number","default":0.1})", "0.1"},
      {R"({"type":"string","default":"Hi"})", "Hi"},
      {R"({"type":"list","default":["a","b c"]})", R"(["a","b c"])"},
      {R"({"type":"time","default":"07:00"})", "07:00"},
      {R"({"type":"string"})", ""},
      {R"({"type":"secret","where":"device"})", ""},
  };
  for (const D& d : cases) {
    JsonDocument f = field(d.field);
    appset::defaultText(f.as<JsonObjectConst>(), b, sizeof b);
    expect(std::string(b) == d.want, std::string("default of ") + d.field + ": got " + b + ", want " + d.want);
  }
  // Too small a buffer: cut, NUL ended, the length it needed returned.
  JsonDocument f = field(R"({"type":"list","default":["abcdef","ghijkl"]})");
  char small[8];
  const size_t need = appset::defaultText(f.as<JsonObjectConst>(), small, sizeof small);
  expect(need > sizeof small - 1, "default needs more than a small buffer");
}

void strictJson() {
  const char* ok[] = {"[]", " [ \"a\" , \"b\" ] ", "{\"name\":\"x\",\"lat\":1.5e1,\"lon\":-0}", "[\"\\u00e9\\n\"]",
                      "[[1,[2]],{}]", "null", "\"s\""};
  const char* bad[] = {"", "[1] x", "['a']", "[\"a\",]", "{name:1}", "[01]", "[1.]", "[.5]", "[\"a\tb\"]",
                       "[\"\\x\"]", "[", "{\"a\" 1}", "tru", "[1e]", "[-]"};
  for (const char* s : ok) expect(appset::strictJson(s), std::string("strict JSON accepts ") + s);
  for (const char* s : bad) expect(!appset::strictJson(s), std::string("strict JSON refuses ") + s);
}

void numbers() {
  char b[48];
  struct N {
    double v;
    const char* want;
  } cases[] = {{4, "4"}, {2.5, "2.5"}, {-0.0, "0"}, {0.1, "0.1"}, {100, "100"}, {1234.5678, "1234.5678"}, {1e-6, "0.000001"}};
  for (const N& n : cases) {
    appset::numText(n.v, b, sizeof b);
    expect(std::string(b) == n.want, std::string("number ") + n.want + ": got " + b);
  }
}

void extras() {
  // The device's own cases beyond the shared vectors (each mirrors a line of checkValue).
  struct C {
    const char* field;
    const char* value;
    bool ok;
  } cases[] = {
      {R"({"type":"string"})", "tab\tok", true},
      {R"({"type":"string","max":3})", "Việt", false},
      {R"({"type":"string","max":4})", "Việt", true},
      {R"({"type":"text"})", "a\r\nb", true},
      {R"({"type":"text"})", "bell\x07", false},
      {R"({"type":"number"})", ".5", true},
      {R"({"type":"number"})", "-.5", true},
      {R"({"type":"number"})", "5.", false},
      {R"({"type":"number"})", "", false},
      {R"({"type":"number"})", "+1", false},
      {R"({"type":"number","min":0,"max":1,"step":0.1})", "0.3", true},
      {R"({"type":"number","min":0,"max":1,"step":0.1})", "0.35", false},
      {R"({"type":"number","int":true})", "-3", true},
      {R"({"type":"date"})", "2024-02-29", true},
      {R"({"type":"date"})", "2100-02-29", false},
      {R"({"type":"date"})", "0099-01-01", false},
      {R"({"type":"date"})", "2026-13-01", false},
      {R"({"type":"time"})", "23:59", true},
      {R"({"type":"time"})", "12:60", false},
      {R"({"type":"url"})", "https://a.b", true},
      {R"({"type":"url"})", "https://.ab", false},
      {R"({"type":"url"})", "https://ab.", false},
      {R"({"type":"url"})", "https://shop.example.com/a b", false},
      {R"({"type":"url"})", "https://shop.example.com?q=1#x", true},
      {R"({"type":"url"})", "https://shop.example.com\xc2\xa0/x", false},
      {R"({"type":"city"})", R"({"name":"  ","lat":1,"lon":2})", false},
      {R"({"type":"city"})", R"({"name":"Hà Nội","lat":21.0285,"lon":105.8542,"tz":7})", true},
      {R"({"type":"city"})", R"([1,2])", false},
      {R"({"type":"city"})", R"({"name":"x","lat":"1","lon":2})", false},
      {R"({"type":"list","of":{"type":"string"}})", R"(["a\nb"])", false},
      {R"({"type":"list","of":{"type":"string"}})", R"(["a"] x)", false},
      {R"({"type":"list"})", R"(["no of: strings"])", true},
      {R"({"type":"list","of":{"type":"enum","options":[{"v":"a","label":"A"}]}})", R"([])", true},
      {R"({"type":"enum","options":[{"v":"a","label":"A"}]})", "A", false},
      {R"({"type":"photo"})", "", false},
      {R"({"type":"section","label":"x"})", "", false},
      {R"({"label":"no type"})", "x", false},
  };
  for (const C& c : cases) {
    JsonDocument f = field(c.field);
    const appset::Check got = appset::check(f.as<JsonObjectConst>(), c.value);
    expect(bool(got) == c.ok, std::string("extra ") + c.field + " value \"" + c.value + "\": want " +
                                  (c.ok ? "ok" : "refused") + ", got err " + std::to_string(got.err));
  }
  // The limit a message shows, and that a list's item error says so.
  JsonDocument f = field(R"({"type":"list","of":{"type":"string","max":3},"max_items":2})");
  appset::Check c = appset::check(f.as<JsonObjectConst>(), R"(["abcd"])");
  expect(c.err == appset::E_LONG && c.n == 3 && c.item, "list item error: E_LONG 3, item");
  c = appset::check(f.as<JsonObjectConst>(), R"(["a","b","c"])");
  expect(c.err == appset::E_ITEMS && c.n == 2, "list too long: E_ITEMS 2");
}

void sendTo() {
  struct M {
    const char *pattern, *prefix, *url;
    bool ok;
  } cases[] = {
      {"{shop_url}/wp-json/*", "https://shop.example.com", "https://shop.example.com/wp-json/wc/v3/orders", true},
      {"{shop_url}/wp-json/*", "https://shop.example.com", "https://shop.example.com/other", false},
      {"{shop_url}/wp-json/*", "https://shop.example.com", "https://shop.example.com.evil.io/wp-json/x", false},
      {"{shop_url}/wp-json/*", "", "https://shop.example.com/wp-json/x", false},
      {"{shop_url}/wp-json/*", nullptr, "https://shop.example.com/wp-json/x", false},
      {"https://api.example.com/v1/*", nullptr, "https://API.example.com/v1/rates?x=1", true},
      {"https://api.example.com/v1/*", nullptr, "http://api.example.com/v1/rates", false},
      {"https://api.example.com*", nullptr, "https://api.example.com.evil.io/", false},
      {"https://api.example.com*", nullptr, "https://api.example.com/anything", true},
      {"https://api.example.com/exact", nullptr, "https://api.example.com/exact", true},
      {"https://api.example.com/exact", nullptr, "https://api.example.com/exact/more", false},
      {"https://api.example.com/*/items", nullptr, "https://api.example.com/a/b/items", true},
  };
  for (const M& m : cases)
    expect(appset::urlMatches(m.pattern, m.prefix, m.url) == m.ok,
           std::string("send_to ") + m.pattern + " vs " + m.url + (m.ok ? " should match" : " should not match"));
}

}  // namespace

int main(int argc, char** argv) {
  vectors(argc > 1 ? argv[1] : "sdk/packages/sdk/schemas/settings-vectors.json");
  defaults();
  strictJson();
  numbers();
  extras();
  sendTo();
  std::printf("%d checks, %d failed\n", g_run, g_fail);
  return g_fail ? 1 : 0;
}
