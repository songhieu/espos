#pragma once
// ESP32 Preferences (NVS) on the computer: one file ($BOT_SIM_DATA/nvs.txt), written on every change, so the
// simulated bot keeps its WiFi, link and settings from run to run (FACTORY_RESET or --reset clears it).
#include <map>
#include <string>

#include "Arduino.h"

class Preferences {
 public:
  bool begin(const char* ns, bool = false) { return load(), ns_ = std::string(ns) + ".", true; }
  void end() {}
  bool isKey(const char* k) { return all().count(ns_ + k) > 0; }
  bool remove(const char* k) { return all().erase(ns_ + k) ? save(), true : false; }
  bool clear() {
    for (auto it = all().begin(); it != all().end();) it = it->first.rfind(ns_, 0) == 0 ? all().erase(it) : std::next(it);
    return save(), true;
  }
  String getString(const char* k, const String& d = String()) { return isKey(k) ? String(all()[ns_ + k].c_str()) : d; }
  size_t putString(const char* k, const String& v) { return put(k, v.c_str()), v.length(); }
  size_t putString(const char* k, const char* v) { return put(k, v ? v : ""), 1; }
  int getInt(const char* k, int d = 0) { return isKey(k) ? atoi(all()[ns_ + k].c_str()) : d; }
  size_t putInt(const char* k, int v) { return put(k, std::to_string(v)), 4; }
  int64_t getLong64(const char* k, int64_t d = 0) { return isKey(k) ? atoll(all()[ns_ + k].c_str()) : d; }
  size_t putLong64(const char* k, int64_t v) { return put(k, std::to_string(v)), 8; }
  bool getBool(const char* k, bool d = false) { return getInt(k, d) != 0; }
  size_t putBool(const char* k, bool v) { return putInt(k, v); }
  size_t getBytesLength(const char* k) { return isKey(k) ? unhex(all()[ns_ + k]).size() : 0; }
  size_t getBytes(const char* k, void* p, size_t n) {
    const std::string b = unhex(all()[ns_ + k]);
    const size_t m = b.size() < n ? b.size() : n;
    memcpy(p, b.data(), m);
    return m;
  }
  size_t putBytes(const char* k, const void* p, size_t n) {
    static const char* H = "0123456789abcdef";
    std::string h;
    for (size_t i = 0; i < n; i++) h += H[((const uint8_t*)p)[i] >> 4], h += H[((const uint8_t*)p)[i] & 15];
    return put(k, h), n;
  }
  static void path(const std::string& p) { file() = p; }

 private:
  std::string ns_;
  static std::string& file() {
    static std::string f;
    return f;
  }
  static std::map<std::string, std::string>& all() {
    static std::map<std::string, std::string> m;
    return m;
  }
  static void load() {
    static bool done = false;
    if (done || file().empty()) return;
    done = true;
    FILE* f = fopen(file().c_str(), "r");
    if (!f) return;
    char line[16384];
    while (fgets(line, sizeof line, f)) {
      std::string s = line;
      while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
      const size_t t = s.find('\t');
      if (t != std::string::npos) all()[s.substr(0, t)] = unhexText(s.substr(t + 1));
    }
    fclose(f);
  }
  static void save() {
    if (file().empty()) return;
    FILE* f = fopen(file().c_str(), "w");
    if (!f) return;
    for (auto& kv : all()) fprintf(f, "%s\t%s\n", kv.first.c_str(), hexText(kv.second).c_str());
    fclose(f);
  }
  void put(const char* k, const std::string& v) { all()[ns_ + k] = v, save(); }
  // values are kept as text with \t and \n escaped
  static std::string hexText(const std::string& v) {
    std::string o;
    for (char c : v) o += c == '\\' ? std::string("\\\\") : c == '\n' ? std::string("\\n") : c == '\t' ? std::string("\\t") : std::string(1, c);
    return o;
  }
  static std::string unhexText(const std::string& v) {
    std::string o;
    for (size_t i = 0; i < v.size(); i++) {
      if (v[i] == '\\' && i + 1 < v.size()) {
        const char n = v[++i];
        o += n == 'n' ? '\n' : n == 't' ? '\t' : n;
      } else {
        o += v[i];
      }
    }
    return o;
  }
  static std::string unhex(const std::string& h) {
    std::string b;
    for (size_t i = 0; i + 1 < h.size(); i += 2) b += (char)strtol(h.substr(i, 2).c_str(), nullptr, 16);
    return b;
  }
};
