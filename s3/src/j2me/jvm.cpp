// The VM's core: memory and time on each platform, construction, names, strings, roots, output (docs/j2me.md).
// The rest is in vm_class.cpp (class files, linking), vm_heap.cpp (allocation, GC), vm_interp.cpp (bytecodes),
// vm_thread.cpp (green threads, call, exceptions), vm_natives.cpp (java.* natives), vm_zip.cpp (JARs).
#if defined(ESP_PLATFORM) && !defined(J2ME_NO_OS)
#pragma GCC optimize("Os")  // cold code: small on the board (flash is short); the interpreter stays -O2
#endif
#include <new>

#include "vm_internal.h"

#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#include <esp_timer.h>
#else
#include <time.h>
#endif

namespace j2me {

#ifdef ESP_PLATFORM
void* vmMalloc(size_t n) { return heap_caps_aligned_alloc(8, n ? n : 8, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
void vmFree(void* p) { heap_caps_free(p); }
uint64_t Vm::nowUs() { return (uint64_t)esp_timer_get_time(); }
static const size_t kDefaultHeap = 3u * 1024 * 1024;
static const int kDefaultStack = 8192;
#else
void* vmMalloc(size_t n) { return std::malloc(n ? n : 8); }
void vmFree(void* p) { std::free(p); }
uint64_t Vm::nowUs() {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}
static const size_t kDefaultHeap = 64u * 1024 * 1024;
static const int kDefaultStack = 65536;
#endif

static size_t fnv(const void* p, size_t n) {
  const uint8_t* b = static_cast<const uint8_t*>(p);
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 16777619u;
  return h;
}
size_t StrHash::operator()(const PStr& s) const { return fnv(s.data(), s.size()); }
size_t StrHash::operator()(const PStr16& s) const { return fnv(s.data(), s.size() * 2); }

// ---- names ----

Vm::SymTab::~SymTab() {
  for (char* c : chunks) vmFree(c);
  vmFree(table);
}

void Vm::SymTab::grow() {
  size_t ncap = cap ? cap * 2 : 1024;
  const char** nt = static_cast<const char**>(vmMalloc(ncap * sizeof(char*)));
  if (!nt) std::abort();
  std::memset(nt, 0, ncap * sizeof(char*));
  for (size_t i = 0; i < cap; i++)
    if (table[i]) {
      size_t h = fnv(table[i], std::strlen(table[i])) & (ncap - 1);
      while (nt[h]) h = (h + 1) & (ncap - 1);
      nt[h] = table[i];
    }
  vmFree(table);
  table = nt;
  cap = ncap;
}

Sym Vm::SymTab::find(const char* s, size_t n) const {
  if (!cap) return Sym();
  size_t h = fnv(s, n) & (cap - 1);
  while (const char* e = table[h]) {
    if (std::strncmp(e, s, n) == 0 && e[n] == 0) return Sym(e);
    h = (h + 1) & (cap - 1);
  }
  return Sym();
}

Sym Vm::SymTab::intern(const char* s, size_t n) {
  Sym f = find(s, n);
  if (f.ok()) return f;
  if ((count + 1) * 2 > cap) grow();
  char* d;
  if (n + 1 > 4096) {
    d = static_cast<char*>(vmMalloc(n + 1));
    if (!d) std::abort();
    chunks.push_back(d);
  } else {
    if (left < n + 1) {
      at = static_cast<char*>(vmMalloc(16384));
      if (!at) std::abort();
      chunks.push_back(at);
      left = 16384;
    }
    d = at;
    at += n + 1;
    left -= n + 1;
  }
  std::memcpy(d, s, n);
  d[n] = 0;
  size_t h = fnv(d, n) & (cap - 1);
  while (table[h]) h = (h + 1) & (cap - 1);
  table[h] = d;
  count++;
  return Sym(d);
}

std::string javaName(Sym internal) {
  std::string s = internal.str();
  for (char& c : s)
    if (c == '/') c = '.';
  return s;
}

std::string Vm::className(Class* c) { return c ? javaName(c->name) : std::string("null"); }

// ---- text ----

void utf8To16(const char* s, size_t n, PStr16& out) {
  out.clear();
  out.reserve(n);
  const uint8_t* b = reinterpret_cast<const uint8_t*>(s);
  size_t i = 0;
  while (i < n) {
    uint32_t c = b[i];
    if (c < 0x80) {
      out.push_back((char16_t)c);
      i++;
      continue;
    }
    int need = (c & 0xe0) == 0xc0 ? 1 : (c & 0xf0) == 0xe0 ? 2 : (c & 0xf8) == 0xf0 ? 3 : -1;
    if (need < 0) {  // a stray continuation byte or an invalid lead byte
      out.push_back(u'\ufffd');
      i++;
      continue;
    }
    uint32_t cp = c & (0x3fu >> need);
    int k = 1;
    for (; k <= need && i + k < n && (b[i + k] & 0xc0) == 0x80; k++) cp = (cp << 6) | (b[i + k] & 0x3f);
    if (k <= need) {  // cut short: one replacement for the whole broken sequence (as the JDK does)
      out.push_back(u'\ufffd');
      i += k;
      continue;
    }
    i += k;
    if (cp >= 0x10000) {
      cp -= 0x10000;
      out.push_back((char16_t)(0xd800 + (cp >> 10)));
      out.push_back((char16_t)(0xdc00 + (cp & 0x3ff)));
    } else {
      out.push_back((char16_t)cp);
    }
  }
}

std::string utf16To8(const char16_t* s, size_t n) {
  std::string o;
  o.reserve(n);
  for (size_t i = 0; i < n; i++) {
    uint32_t c = s[i];
    if (c >= 0xd800 && c < 0xdc00 && i + 1 < n && s[i + 1] >= 0xdc00 && s[i + 1] < 0xe000) {
      c = 0x10000 + ((c - 0xd800) << 10) + (s[i + 1] - 0xdc00);
      i++;
    }
    if (c < 0x80) {
      o.push_back((char)c);
    } else if (c < 0x800) {
      o.push_back((char)(0xc0 | (c >> 6)));
      o.push_back((char)(0x80 | (c & 0x3f)));
    } else if (c < 0x10000) {
      o.push_back((char)(0xe0 | (c >> 12)));
      o.push_back((char)(0x80 | ((c >> 6) & 0x3f)));
      o.push_back((char)(0x80 | (c & 0x3f)));
    } else {
      o.push_back((char)(0xf0 | (c >> 18)));
      o.push_back((char)(0x80 | ((c >> 12) & 0x3f)));
      o.push_back((char)(0x80 | ((c >> 6) & 0x3f)));
      o.push_back((char)(0x80 | (c & 0x3f)));
    }
  }
  return o;
}

// ---- the VM ----

Vm::Vm(std::function<void(const std::string&)> log) : heapLimit(kDefaultHeap), stackSlots(kDefaultStack), logFn(std::move(log)) {
  if (!logFn) logFn = [](const std::string&) {};
  icache = static_cast<ICache*>(vmMalloc(256 * sizeof(ICache)));
  if (!icache) std::abort();
  std::memset(icache, 0, 256 * sizeof(ICache));
  setProperty("microedition.platform", "EspOS-S3");
  setProperty("microedition.configuration", "CLDC-1.1");
  setProperty("microedition.profiles", "MIDP-2.0");
  setProperty("microedition.encoding", "ISO-8859-1");
  setProperty("microedition.locale", "en-US");
  setProperty("microedition.jtwi.version", "1.0");
  setProperty("java.vendor", "EspOS");
  registerBuiltins();
}

Vm::~Vm() {
  for (Object* o : heap) vmFree(o);
  heap.clear();
  for (Thread* t : threads) freeThread(t);
  threads.clear();
  for (auto& kv : classes) {
    kv.second->~Class();
    vmFree(kv.second);
  }
  classes.clear();
  for (Source& s : sources)
    if (s.owned) vmFree(const_cast<uint8_t*>(s.zip));
  vmFree(icache);
}

void Vm::setProperty(const std::string& key, const std::string& value) {
  props[PStr(key.c_str(), key.size())] = PStr(value.c_str(), value.size());
}

void Vm::removeRoot(Object** r) {
  for (size_t i = roots.size(); i-- > 0;)
    if (roots[i] == r) {
      roots.erase(roots.begin() + (long)i);
      return;
    }
}

Object* Vm::keep(Object* o) {
  if (o) locals.push_back(o);
  return o;
}

void Vm::output(int fd, const std::string& s) {
  if (out) {
    out(fd, s.data(), s.size());
    return;
  }
  std::string& b = lineBuf[fd == 2 ? 2 : 1];
  for (char c : s) {
    if (c == '\n') {
      logFn(b);
      b.clear();
    } else {
      b.push_back(c);
    }
  }
  if (b.size() > 4096) {
    logFn(b);
    b.clear();
  }
}

void Vm::registerNative(const std::string& key, Native fn) {
  natives[PStr(key.c_str(), key.size())] = fn;
  // a class already loaded: bind now
  size_t dot = key.find('.'), par = key.find('(');
  if (dot == std::string::npos || par == std::string::npos || par < dot) return;
  Sym cn = syms.find(key.c_str(), dot);
  if (!cn.ok()) return;
  Class* c = findLoaded(cn);
  if (!c) return;
  Sym n = syms.find(key.c_str() + dot + 1, par - dot - 1), d = syms.find(key.c_str() + par, key.size() - par);
  if (!n.ok() || !d.ok()) return;
  if (Method* m = c->findMethod(n, d)) m->native = fn;
}

void Vm::bindNative(Method* m) {
  std::string key = m->owner->name.str() + "." + m->name.str() + m->desc.str();
  auto it = natives.find(PStr(key.c_str(), key.size()));
  if (it != natives.end()) m->native = it->second;
}

int Method::line(uint32_t pc) const {
  int best = -1;
  uint32_t bestStart = 0;
  for (uint16_t i = 0; i < lineCount; i++) {
    uint32_t start = u2(lines + i * 4);
    if (start <= pc && (best < 0 || start >= bestStart)) {
      best = u2(lines + i * 4 + 2);
      bestStart = start;
    }
  }
  return best;
}

Method* Class::findMethod(Sym n, Sym d) const {
  for (const Method& m : methods)
    if (m.name == n && m.desc == d) return const_cast<Method*>(&m);
  return nullptr;
}

Field* Class::findField(Sym n, Sym d) const {
  for (const Field& f : fields)
    if (f.name == n && f.desc == d) return const_cast<Field*>(&f);
  return nullptr;
}

Method* Class::findMethod(const std::string& n, const std::string& d) const {
  for (const Method& m : methods)
    if (m.name == n && m.desc == d) return const_cast<Method*>(&m);
  return nullptr;
}

Field* Class::findField(const std::string& n, const std::string& d) const {
  for (const Field& f : fields)
    if (f.name == n && f.desc == d) return const_cast<Field*>(&f);
  return nullptr;
}

bool parseMethodDesc(const char* d, int& argSlots, int& argCount, char& ret, uint32_t& wideArgs) {
  argSlots = 0;
  argCount = 0;
  wideArgs = 0;
  if (*d != '(') return false;
  d++;
  while (*d != ')') {
    char c = *d;
    if (c == 'J' || c == 'D') {
      if (argCount < 32) wideArgs |= 1u << argCount;
      argSlots += 2;
      d++;
    } else if (c == 'L') {
      while (*d && *d != ';') d++;
      if (!*d) return false;
      d++;
      argSlots++;
    } else if (c == '[') {
      while (*d == '[') d++;
      if (*d == 'L') {
        while (*d && *d != ';') d++;
        if (!*d) return false;
      } else if (!*d) {
        return false;
      }
      d++;
      argSlots++;
    } else if (c && std::strchr("BCFISZ", c)) {
      argSlots++;
      d++;
    } else {
      return false;
    }
    argCount++;
  }
  ret = d[1];
  return ret != 0;
}

}  // namespace j2me
