// Host test of the preset merge (src/preset_merge.cpp) against the SDK's shared cases. Build and run from s3/:
//   c++ -std=c++17 -Wall -Wextra -O1 -fsanitize=address,undefined -I src -I .pio/libdeps/s3desk/ArduinoJson/src \
//       src/preset_merge.cpp test_host/preset_test.cpp -o /tmp/preset_test && /tmp/preset_test
// The cases: sdk/packages/sdk/schemas/preset-merge-vectors.json (docs/app-slots.md §17.3).
#include "preset_merge.h"

#include <ArduinoJson.h>

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

preset::Values values(JsonObjectConst o) {
  preset::Values m;
  for (JsonPairConst kv : o) m[kv.key().c_str()] = kv.value().as<const char*>() ? kv.value().as<const char*>() : "";
  return m;
}

std::vector<std::string> list(JsonArrayConst a) {
  std::vector<std::string> v;
  for (JsonVariantConst x : a) v.push_back(x.as<const char*>());
  return v;
}

std::string show(const preset::Values& m) {
  std::string s = "{";
  for (const auto& kv : m) s += kv.first + "=" + kv.second + " ";
  return s + "}";
}

std::string show(const std::vector<std::string>& v) {
  std::string s = "[";
  for (const auto& x : v) s += x + " ";
  return s + "]";
}

}  // namespace

int main() {
  const std::string text = readFile("sdk/packages/sdk/schemas/preset-merge-vectors.json");
  JsonDocument doc;
  if (deserializeJson(doc, text) != DeserializationError::Ok) {
    std::printf("cannot read the cases (run from s3/)\n");
    return 1;
  }
  for (JsonObjectConst c : doc["settings"].as<JsonArrayConst>()) {
    preset::Values set, after;
    preset::mergeSettings(values(c["base"]), values(c["current"]), values(c["next"]), set, after);
    const std::string name = c["name"].as<const char*>();
    expect(set == values(c["set"]), name + ": set " + show(set) + " want " + show(values(c["set"])));
    expect(after == values(c["base_after"]), name + ": base " + show(after) + " want " + show(values(c["base_after"])));
  }
  for (JsonObjectConst c : doc["packages"].as<JsonArrayConst>()) {
    std::vector<std::string> add, after;
    preset::mergePackages(list(c["base"]), list(c["installed"]), list(c["next"]), add, after);
    const std::string name = c["name"].as<const char*>();
    expect(add == list(c["add"]), name + ": add " + show(add) + " want " + show(list(c["add"])));
    expect(after == list(c["base_after"]), name + ": base " + show(after) + " want " + show(list(c["base_after"])));
  }
  std::printf("%d checks, %d failed\n", g_run, g_fail);
  return g_fail ? 1 : 0;
}
