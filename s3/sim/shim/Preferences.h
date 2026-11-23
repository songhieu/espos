#pragma once
// Host stand-in for the ESP32 Preferences (NVS): kept in memory for the run.
#include <map>
#include "Arduino.h"

class Preferences {
 public:
  bool begin(const char* ns, bool = false) { return ns_ = ns, true; }
  void end() {}
  String getString(const char* k, const String& d = String()) {
    auto it = store()[ns_ + k].find('s');
    return it == store()[ns_ + k].end() ? d : String(it->second.c_str());
  }
  size_t putString(const char* k, const String& v) { return store()[ns_ + k]['s'] = v.c_str(), v.length(); }
  int getInt(const char* k, int d = 0) {
    auto it = store()[ns_ + k].find('i');
    return it == store()[ns_ + k].end() ? d : atoi(it->second.c_str());
  }
  size_t putInt(const char* k, int v) { return store()[ns_ + k]['i'] = std::to_string(v), 4; }
  bool getBool(const char* k, bool d = false) { return getInt(k, d) != 0; }
  size_t putBool(const char* k, bool v) { return putInt(k, v); }
  bool remove(const char* k) { return store().erase(ns_ + k) > 0; }
  bool isKey(const char* k) { return store().count(ns_ + k) > 0; }
  bool clear() {  // every key of the namespace (keys are stored as namespace + key)
    for (auto it = store().begin(); it != store().end();) it = it->first.compare(0, ns_.size(), ns_) ? std::next(it) : store().erase(it);
    return true;
  }

 private:
  static std::map<std::string, std::map<char, std::string>>& store() {
    static std::map<std::string, std::map<char, std::string>> s;
    return s;
  }
  std::string ns_;
};
