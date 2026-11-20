#pragma once
// EspOS Bot simulator (sim/bot): a host stand-in for the Arduino core, enough for src/bot. String comes from the S3
// simulator's shim (sim/shim/Arduino.h, copied); Serial reads this terminal's input without blocking; the BOOT pin is
// the window's Space key (or a script); millis() is real time, or the script's own clock when headless.
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <algorithm>
#include <mutex>

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

#define INPUT 0x01
#define OUTPUT 0x03
#define INPUT_PULLUP 0x05
#define LOW 0
#define HIGH 1
void pinMode(int pin, int mode);
int digitalRead(int pin);

// trace.cpp's PSRAM ring and its lock
#define ps_malloc malloc
typedef std::mutex portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED \
  {}
#define portENTER_CRITICAL(m) (m)->lock()
#define portEXIT_CRITICAL(m) (m)->unlock()

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


// Serial: this terminal (lines typed here, or a script's "serial" lines, are the USB line protocol).
struct SerialClass {
  void begin(int) {}
  void print(const char* s) { fputs(s, stdout), fflush(stdout); }
  void print(const String& s) { print(s.c_str()); }
  void println(const char* s = "") { puts(s), fflush(stdout); }
  void println(const String& s) { println(s.c_str()); }
  int printf(const char* f, ...) __attribute__((format(printf, 2, 3))) {
    va_list a;
    va_start(a, f);
    int n = vprintf(f, a);
    va_end(a);
    fflush(stdout);
    return n;
  }
  operator bool() const { return true; }
  int available();
  int read();
  size_t write(const uint8_t* b, size_t n) { return fwrite(b, 1, n, stdout); }
  void flush() { fflush(stdout); }
  void setTxTimeoutMs(int) {}
};
extern SerialClass Serial;

struct EspClass {
  uint32_t getFreeHeap() { return 185 * 1024; }
  uint32_t getFreePsram() { return 7800 * 1024; }
  void restart();
};
extern EspClass ESP;

// Stream, for the code that writes a body somewhere (link's voice; not used by the simulator's HTTP).
struct Stream {
  virtual ~Stream() {}
  virtual size_t write(uint8_t c) = 0;
  virtual size_t write(const uint8_t* b, size_t n) = 0;
  virtual int available() = 0;
  virtual int read() = 0;
  virtual int peek() = 0;
  virtual void flush() = 0;
};
