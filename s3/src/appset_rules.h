#pragma once
// The rules of a package's settings values (manifest "settings", docs/manifest-driven.md §1.3): a C++ port of the
// SDK's checkValue (sdk/packages/cli/src/settings.ts) line for line, so the device refuses what the server and the
// web refuse. Plain C++ and ArduinoJson only (no Arduino, no LVGL): test_host/appset_test.cpp runs every case of
// sdk/packages/sdk/schemas/settings-vectors.json against it. The form, the value store and JS are appset.h.
#include <ArduinoJson.h>

#include <cstddef>

namespace appset {

// Why a value is refused: a code and, for some, a number (a limit). The form shows them translated (appset.cpp).
enum Err : unsigned char {
  OK = 0,
  E_LONG,       // more than n characters
  E_CONTROL,    // control characters, or a line break outside "text"
  E_NUMBER,     // not a number like 4 or 2.5
  E_MIN,        // under n
  E_MAX,        // over n
  E_INT,        // not a whole number
  E_STEP,       // not in steps of n
  E_BOOL,       // not "1" / "0"
  E_ENUM,       // not one of the options
  E_LIST,       // not a JSON list of texts
  E_ITEMS,      // more than n items
  E_TIME,       // not HH:MM
  E_DATE,       // not YYYY-MM-DD
  E_REALDATE,   // not a real date (Feb 30)
  E_COLOR,      // not #rrggbb
  E_URL,        // not an https:// address of at most n characters
  E_CITY,       // not {"name", "lat", "lon"}
  E_PHOTO,      // not a photo id
  E_SECRET,     // a secret is never a setting value (the OS keeps it apart)
  E_SECTION,    // a section has no value
  E_TYPE,       // unknown type
};
struct Check {
  Err err = OK;
  double n = 0;  // the limit of E_LONG, E_MIN, E_MAX, E_STEP, E_ITEMS, E_URL
  bool item = false;  // the error is one item's of a list
  explicit operator bool() const { return err == OK; }
};

constexpr int MAX_VALUE = 2000;      // characters of one stored value
constexpr int MAX_APP_BYTES = 8192;  // all of one package's values on one device
constexpr int STRING_DEFAULT_MAX = 80, TEXT_DEFAULT_MAX = 500, URL_MAX = 300, LIST_MAX_ITEMS = 50;
constexpr int MAX_FIELDS = 40;

// One value (always text: a bool is "1"/"0", a list or a city JSON) against one field of the schema.
Check check(JsonObjectConst field, const char* raw);

// The field's default as stored text ("" when it has none): a bool "1"/"0", a number as JS's String(), a list JSON.
// Writes at most cap - 1 bytes and a NUL into out; returns the length it needed.
size_t defaultText(JsonObjectConst field, char* out, size_t cap);

// A number of the schema as its JSON text meant it: ArduinoJson keeps short decimals (0.1) as floats; this reads the
// float's shortest decimal back as a double (0.1, not 0.100000001), as JavaScript's JSON.parse reads it.
double num(JsonVariantConst v);
// Characters (Unicode code points) of UTF-8 text, as the SDK's [...s].length.
size_t chars(const char* s);
// A number as JavaScript's String(n) prints it for the values the rules allow ("4", "2.5", "-0.25").
size_t numText(double v, char* out, size_t cap);
// Strict JSON (JSON.parse's grammar): one value and only white space around it.
bool strictJson(const char* s);

// A secret's send_to pattern against a request URL: "https://host/path*" ('*' at the end: any rest), "{shop_url}/x/*"
// with `prefix` the url field's value put in place of the placeholder (nullptr: the pattern has none). Only https.
bool urlMatches(const char* pattern, const char* prefix, const char* url);

}  // namespace appset
