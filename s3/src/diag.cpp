#pragma GCC optimize("Os")  // a cold screen: built small (the firmware must fit 3.75 MB)
#include "diag.h"

#include <map>
#include <string>

namespace diag {
namespace {
std::map<std::string, int32_t> vals;
}
void set(const char* key, int32_t v) { vals[key] = v; }
void inc(const char* key) { vals[key]++; }
void maxOf(const char* key, int32_t v) {
  auto it = vals.find(key);
  if (it == vals.end() || v > it->second) vals[key] = v;
}
void fill(JsonObject o) {
  for (auto& kv : vals) o[kv.first] = kv.second;
}
}  // namespace diag
