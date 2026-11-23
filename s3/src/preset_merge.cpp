#pragma GCC optimize("Os")
// The three-way merge of a product preset (preset_merge.h).
#include "preset_merge.h"

#include <algorithm>

namespace preset {

namespace {
const std::string& valueOf(const Values& m, const std::string& k) {
  static const std::string none;
  auto it = m.find(k);
  return it == m.end() ? none : it->second;
}
bool has(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }
}  // namespace

void mergeSettings(const Values& base, const Values& current, const Values& next, Values& set, Values& baseAfter) {
  set.clear();
  baseAfter.clear();
  for (const auto& kv : next) {
    const std::string& k = kv.first;
    const std::string& want = kv.second;
    const std::string& now = valueOf(current, k);
    auto b = base.find(k);
    if (b != base.end()) {
      if (now == b->second) {  // untouched since the preset set it: the new version's value
        if (now != want) set[k] = want;
        baseAfter[k] = want;
      } else if (now == want) {  // the person chose what the preset now says: from here on it is the preset's again
        baseAfter[k] = want;
      } else {  // the person's own choice stays; the base keeps the preset's old value, so later versions see it too
        baseAfter[k] = b->second;
      }
    } else if (now.empty()) {  // never set by a preset and never chosen
      set[k] = want;
      baseAfter[k] = want;
    }
    // else: chosen before the preset ever set it: stays, and is not the preset's
  }
}

void mergePackages(const std::vector<std::string>& base, const std::vector<std::string>& installed,
                   const std::vector<std::string>& next, std::vector<std::string>& add, std::vector<std::string>& baseAfter) {
  add.clear();
  baseAfter = base;
  for (const std::string& id : next) {
    if (has(base, id)) continue;  // asked for before: removed since by the person, or still there
    if (!has(installed, id)) add.push_back(id);
    baseAfter.push_back(id);
  }
}

}  // namespace preset
