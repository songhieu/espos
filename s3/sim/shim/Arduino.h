#pragma once
// Host stand-in for the Arduino core: enough of String, timing and the ESP object for the UI
// code to build and run on a computer (see sim/README.md).
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <algorithm>

uint32_t millis();
uint32_t micros();
void delay(uint32_t ms);
long random(long max);
long random(long min, long max);

template <class T, class L, class H>
auto constrain(T x, L lo, H hi) -> decltype(x + lo + hi) {
  return x < lo ? lo : x > hi ? hi : x;
}
using std::max;
using std::min;

class String {
 public:
  String() {}
  String(const char* s) : s_(s ? s : "") {}
  String(const std::string& s) : s_(s) {}
  String(char c) : s_(1, c) {}
  String(int v) : s_(std::to_string(v)) {}
  String(unsigned v) : s_(std::to_string(v)) {}
  String(long v) : s_(std::to_string(v)) {}
  String(unsigned long v) : s_(std::to_string(v)) {}
  String(long long v) : s_(std::to_string(v)) {}
  String(unsigned long long v) : s_(std::to_string(v)) {}
  String(float v, int dec = 2) { fmtf(v, dec); }
  String(double v, int dec = 2) { fmtf(v, dec); }

  const char* c_str() const { return s_.c_str(); }
  unsigned length() const { return s_.size(); }
  bool isEmpty() const { return s_.empty(); }
  void reserve(unsigned n) { s_.reserve(n); }
  char charAt(unsigned i) const { return i < s_.size() ? s_[i] : 0; }
  char operator[](unsigned i) const { return charAt(i); }
  char& operator[](unsigned i) { return s_[i]; }
  void setCharAt(unsigned i, char c) {
    if (i < s_.size()) s_[i] = c;
  }

  String& operator+=(const String& o) { return s_ += o.s_, *this; }
  String& operator+=(const char* o) { return s_ += (o ? o : ""), *this; }
  String& operator+=(char c) { return s_ += c, *this; }
  String& operator+=(int v) { return s_ += std::to_string(v), *this; }
  String& operator+=(unsigned v) { return s_ += std::to_string(v), *this; }
  String& operator+=(long v) { return s_ += std::to_string(v), *this; }
  String& operator+=(unsigned long v) { return s_ += std::to_string(v), *this; }
  bool concat(const String& o) { return s_ += o.s_, true; }
  bool concat(const char* o) { return s_ += (o ? o : ""), true; }
  bool concat(char c) { return s_ += c, true; }
  bool concat(int v) { return s_ += std::to_string(v), true; }

  bool operator==(const String& o) const { return s_ == o.s_; }
  bool operator==(const char* o) const { return s_ == (o ? o : ""); }
  bool operator!=(const String& o) const { return s_ != o.s_; }
  bool operator!=(const char* o) const { return !(*this == o); }
  bool operator<(const String& o) const { return s_ < o.s_; }
  bool operator>(const String& o) const { return s_ > o.s_; }
  bool equals(const String& o) const { return s_ == o.s_; }
  bool equalsIgnoreCase(const String& o) const {
    if (o.s_.size() != s_.size()) return false;
    for (size_t i = 0; i < s_.size(); i++)
      if (tolower((unsigned char)s_[i]) != tolower((unsigned char)o.s_[i])) return false;
    return true;
  }
  int compareTo(const String& o) const { return s_.compare(o.s_); }

  int indexOf(char c, unsigned from = 0) const { return pos(s_.find(c, from)); }
  int indexOf(const String& t, unsigned from = 0) const { return pos(s_.find(t.s_, from)); }
  int lastIndexOf(char c) const { return pos(s_.rfind(c)); }
  int lastIndexOf(const String& t) const { return pos(s_.rfind(t.s_)); }
  String substring(unsigned a) const { return a >= s_.size() ? String() : String(s_.substr(a)); }
  String substring(unsigned a, unsigned b) const {
    if (b > s_.size()) b = s_.size();
    if (a >= b) return String();
    return String(s_.substr(a, b - a));
  }
  bool startsWith(const String& p) const { return s_.compare(0, p.s_.size(), p.s_) == 0; }
  bool startsWith(const String& p, unsigned int off) const { return off <= s_.size() && s_.compare(off, p.s_.size(), p.s_) == 0; }
  bool endsWith(const String& p) const {
    return p.s_.size() <= s_.size() && s_.compare(s_.size() - p.s_.size(), p.s_.size(), p.s_) == 0;
  }
  void trim() {
    size_t a = 0, b = s_.size();
    while (a < b && isspace((unsigned char)s_[a])) a++;
    while (b > a && isspace((unsigned char)s_[b - 1])) b--;
    s_ = s_.substr(a, b - a);
  }
  void toLowerCase() {
    for (auto& c : s_) c = tolower((unsigned char)c);
  }
  void toUpperCase() {
    for (auto& c : s_) c = toupper((unsigned char)c);
  }
  void replace(const String& a, const String& b) {
    if (a.s_.empty()) return;
    size_t p = 0;
    while ((p = s_.find(a.s_, p)) != std::string::npos) s_.replace(p, a.s_.size(), b.s_), p += b.s_.size();
  }
  void remove(unsigned i) { s_ = s_.substr(0, std::min<size_t>(i, s_.size())); }
  void remove(unsigned i, unsigned n) {
    if (i < s_.size()) s_.erase(i, n);
  }
  long toInt() const { return strtol(s_.c_str(), nullptr, 10); }
  float toFloat() const { return strtof(s_.c_str(), nullptr); }
  double toDouble() const { return strtod(s_.c_str(), nullptr); }

  // ArduinoJson's Arduino String adapter.
  bool concat(const char* p, size_t n) { return s_.append(p, n), true; }
  const std::string& std() const { return s_; }

 private:
  static int pos(size_t p) { return p == std::string::npos ? -1 : (int)p; }
  void fmtf(double v, int dec) {
    char b[48];
    snprintf(b, sizeof b, "%.*f", dec, v);
    s_ = b;
  }
  std::string s_;
};

inline String operator+(const String& a, const String& b) {
  String r = a;
  r += b;
  return r;
}
inline String operator+(const String& a, const char* b) {
  String r = a;
  r += b;
  return r;
}
inline String operator+(const char* a, const String& b) {
  String r(a);
  r += b;
  return r;
}
inline String operator+(const String& a, char b) {
  String r = a;
  r += b;
  return r;
}
inline String operator+(const String& a, int b) {
  String r = a;
  r += String(b);
  return r;
}
inline String operator+(const String& a, unsigned b) {
  String r = a;
  r += String(b);
  return r;
}
inline String operator+(const String& a, long b) {
  String r = a;
  r += String(b);
  return r;
}
inline String operator+(const String& a, unsigned long b) {
  String r = a;
  r += String(b);
  return r;
}

struct SerialClass {
  void begin(int) {}
  void print(const char* s) { fputs(s, stdout); }
  void print(const String& s) { fputs(s.c_str(), stdout); }
  void println(const char* s = "") { puts(s); }
  void println(const String& s) { puts(s.c_str()); }
  int printf(const char* f, ...) {
    va_list a;
    va_start(a, f);
    int n = vprintf(f, a);
    va_end(a);
    return n;
  }
  operator bool() const { return true; }
  int available() { return 0; }
  int read() { return -1; }
  size_t write(const uint8_t* b, size_t n) { return fwrite(b, 1, n, stdout); }
  size_t readBytes(uint8_t*, size_t) { return 0; }
  void flush() { fflush(stdout); }
  void setTxTimeoutMs(int) {}
  void setRxBufferSize(int) {}
};
extern SerialClass Serial;

struct EspClass {
  uint32_t getFreeHeap() { return 180 * 1024; }
  uint32_t getMinFreeHeap() { return 150 * 1024; }
  uint32_t getFreePsram() { return 6 * 1024 * 1024; }
  const char* getChipModel() { return "ESP32-S3 (sim)"; }
  int getChipRevision() { return 2; }
  void restart() { exit(0); }
};
extern EspClass ESP;

// heap_caps for code that keeps big buffers in PSRAM.
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_INTERNAL 2
#define MALLOC_CAP_8BIT 4
#define MALLOC_CAP_DMA 8
// sim/stubs.cpp: counted, so the live simulator can answer what is left as the board would (simBoard).
void* heap_caps_malloc(size_t n, uint32_t caps);
void* heap_caps_calloc(size_t n, size_t s, uint32_t caps);
void* heap_caps_realloc(void* p, size_t n, uint32_t caps);
void heap_caps_free(void* p);
size_t heap_caps_get_free_size(uint32_t caps);
size_t heap_caps_get_largest_free_block(uint32_t caps);

#define PI 3.14159265358979323846
#define DEG_TO_RAD 0.017453292519943295
