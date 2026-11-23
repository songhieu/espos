#pragma once
// The three-way merge of a product preset (docs/app-slots.md §17.3), plain C++ so the host test runs it
// (test_host/preset_test.cpp, cases sdk/packages/sdk/schemas/preset-merge-vectors.json). src/preset.cpp feeds it the
// device's values.
#include <map>
#include <string>
#include <vector>

namespace preset {

using Values = std::map<std::string, std::string>;

// base: what the preset set last time; current: the device's values (absent or "" = never chosen); next: the new
// version's settings. Fills `set` (keys to write: only where they differ from current) and `baseAfter`.
void mergeSettings(const Values& base, const Values& current, const Values& next, Values& set, Values& baseAfter);

// Packages: added when new to the preset (not in base) and not installed; never removed; base grows.
void mergePackages(const std::vector<std::string>& base, const std::vector<std::string>& installed,
                   const std::vector<std::string>& next, std::vector<std::string>& add, std::vector<std::string>& baseAfter);

}  // namespace preset
