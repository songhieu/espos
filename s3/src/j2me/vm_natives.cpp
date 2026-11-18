// Natives of the class library's java.lang / java.io classes (j2me/classlib). docs/j2me.md "Natives" tells how to
// add one: a function void(Vm&, Thread&, Slot* args, Slot& ret) registered under "pkg/Class.name(desc)".
#if defined(ESP_PLATFORM) && !defined(J2ME_NO_OS)
#pragma GCC optimize("Os")  // cold code: small on the board (flash is short); the interpreter stays -O2
#endif
#include <cstdio>
#include <cstdlib>
#include <ctime>

#include "vm_internal.h"

#ifndef ESP_PLATFORM
#include <sys/time.h>
#else
#include <sys/time.h>
#endif

namespace j2me {

struct Natives {
  // ---- helpers ----
  static bool npe(Vm& vm, Thread& t, Object* o) {
    if (o) return false;
    vm.throwNew(t, "java/lang/NullPointerException");
    return true;
  }
  static std::string lower(std::string s) {
    for (char& c : s)
      if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return s;
  }
  // 0 ISO-8859-1 (also the default), 1 UTF-8, -1 unknown
  static int encoding(Vm& vm, Object* enc) {
    if (!enc) return 0;
    std::string e = lower(vm.str(enc));
    if (e == "utf-8" || e == "utf8") return 1;
    if (e == "iso-8859-1" || e == "iso8859_1" || e == "iso8859-1" || e == "iso-latin-1" || e == "latin1" ||
        e == "us-ascii" || e == "ascii" || e == "8859_1" || e == "cp1252" || e == "windows-1252")
      return 0;
    return -1;
  }
  static std::string javaDouble(double d, bool isFloat) {
    if (d != d) return "NaN";
    if (std::isinf(d)) return d > 0 ? "Infinity" : "-Infinity";
    if (d == 0) return std::signbit(d) ? "-0.0" : "0.0";
    char buf[64];
    for (int prec = 1; prec <= 17; prec++) {
      std::snprintf(buf, sizeof buf, "%.*e", prec - 1, d);
      if (isFloat ? (std::strtof(buf, nullptr) == (float)d) : (std::strtod(buf, nullptr) == d)) break;
    }
    std::string s = buf;
    bool neg = s[0] == '-';
    if (neg) s.erase(0, 1);
    size_t e = s.find('e');
    int exp = std::atoi(s.c_str() + e + 1);
    std::string digits;
    for (size_t i = 0; i < e; i++)
      if (s[i] != '.') digits.push_back(s[i]);
    while (digits.size() > 1 && digits.back() == '0') digits.pop_back();
    std::string out;
    if (exp >= -3 && exp < 7) {
      if (exp >= 0) {
        size_t ni = (size_t)exp + 1;
        std::string ip = digits.substr(0, std::min(digits.size(), ni));
        while (ip.size() < ni) ip.push_back('0');
        out = ip + "." + (digits.size() > ni ? digits.substr(ni) : std::string("0"));
      } else {
        out = "0." + std::string((size_t)(-exp - 1), '0') + digits;
      }
    } else {
      out = digits.substr(0, 1) + "." + (digits.size() > 1 ? digits.substr(1) : std::string("0")) + "E" + std::to_string(exp);
    }
    return neg ? "-" + out : out;
  }
  // Java's Double.parseDouble grammar (decimal; NaN, Infinity; an f/F/d/D suffix). false: not a number.
  static bool parseJava(const std::string& in, double& out) {
    size_t b = 0, e = in.size();
    while (b < e && (unsigned char)in[b] <= ' ') b++;
    while (e > b && (unsigned char)in[e - 1] <= ' ') e--;
    std::string s = in.substr(b, e - b);
    size_t i = 0;
    bool neg = false;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) neg = s[i++] == '-';
    std::string rest = s.substr(i);
    if (rest == "NaN") {
      out = NAN;
      return true;
    }
    if (rest == "Infinity") {
      out = neg ? -INFINITY : INFINITY;
      return true;
    }
    size_t digits = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') i++, digits++;
    if (i < s.size() && s[i] == '.') {
      i++;
      while (i < s.size() && s[i] >= '0' && s[i] <= '9') i++, digits++;
    }
    if (!digits) return false;
    if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
      i++;
      if (i < s.size() && (s[i] == '+' || s[i] == '-')) i++;
      size_t ed = 0;
      while (i < s.size() && s[i] >= '0' && s[i] <= '9') i++, ed++;
      if (!ed) return false;
    }
    size_t end = i;
    if (i < s.size() && std::strchr("fFdD", s[i])) i++;
    if (i != s.size()) return false;
    out = std::strtod(s.substr(0, end).c_str(), nullptr);
    return true;
  }
  static Object* inputStream(Vm& vm, Thread& t, const uint8_t* data, size_t n) {
    Object* arr = vm.newArray('B', nullptr, (int32_t)n);
    if (!arr) return nullptr;
    if (n) std::memcpy(arr->data(), data, n);
    Class* c = vm.loadClass("java/io/ByteArrayInputStream");
    if (!c || !vm.initClass(t, c)) return nullptr;
    Object* o = vm.alloc(c);
    Method* init = c->findMethod(std::string("<init>"), std::string("([B)V"));
    if (!o || !init) return nullptr;
    Slot a[2];
    a[0].a = o;
    a[1].a = arr;
    vm.call(t, init, a);
    return t.pending ? nullptr : o;
  }

  // ---- java.lang.Object ----
  static void getClass(Vm& vm, Thread&, Slot* a, Slot& r) { r.a = vm.mirror(a[0].a->cls); }
  static void hashCode(Vm&, Thread&, Slot* a, Slot& r) { r.i = (int32_t)identityHash(a[0].a); }
  static void notify(Vm& vm, Thread& t, Slot* a, Slot&) { notifyCommon(vm, t, a[0].a, false); }
  static void notifyAll(Vm& vm, Thread& t, Slot* a, Slot&) { notifyCommon(vm, t, a[0].a, true); }
  static void notifyCommon(Vm& vm, Thread& t, Object* o, bool all) {
    Vm::Monitor* m = vm.findMonitor(o);
    if (!m || m->owner != &t) {
      vm.throwNew(t, "java/lang/IllegalMonitorStateException", "notify() without owning the monitor");
      return;
    }
    vm.notify(o, all);
  }
  static void wait(Vm& vm, Thread& t, Slot* a, Slot&) { vm.wait(t, a[0].a, a[1].j); }
  static void clone(Vm& vm, Thread& t, Slot* a, Slot& r) {
    Object* o = a[0].a;
    if (!o->cls->isArray) {
      vm.throwNew(t, "java/lang/CloneNotSupportedException", vm.className(o->cls));
      return;
    }
    Object* c = vm.keep(vm.newArrayRaw(o->cls, o->length));
    if (!c) return;
    std::memcpy(c->data(), o->data(), (size_t)o->length * (size_t)elemSize(o->cls->elem));
    r.a = c;
  }

  // ---- java.lang.Class ----
  static void getName(Vm& vm, Thread&, Slot* a, Slot& r) { r.a = vm.newString(vm.className(vm.classOf(a[0].a))); }
  static void isInstance(Vm& vm, Thread&, Slot* a, Slot& r) { r.i = vm.isInstance(a[1].a, vm.classOf(a[0].a)); }
  static void isArray(Vm& vm, Thread&, Slot* a, Slot& r) { r.i = vm.classOf(a[0].a)->isArray; }
  static void isInterface(Vm& vm, Thread&, Slot* a, Slot& r) { r.i = vm.classOf(a[0].a)->isInterface(); }
  static void isAssignableFrom(Vm& vm, Thread& t, Slot* a, Slot& r) {
    if (npe(vm, t, a[1].a)) return;
    r.i = vm.assignable(vm.classOf(a[0].a), vm.classOf(a[1].a));
  }
  static void forName(Vm& vm, Thread& t, Slot* a, Slot& r) {
    if (npe(vm, t, a[0].a)) return;
    std::string n = vm.str(a[0].a);
    Class* c = vm.loadClass(n);
    if (!c) {
      t.pending = nullptr;
      vm.throwNew(t, "java/lang/ClassNotFoundException", n);
      return;
    }
    if (!vm.initClass(t, c)) return;
    r.a = vm.mirror(c);
  }
  static void newInstance(Vm& vm, Thread& t, Slot* a, Slot& r) {
    Class* c = vm.classOf(a[0].a);
    if (c->isArray || c->isInterface() || c->isAbstract()) {
      vm.throwNew(t, "java/lang/InstantiationException", vm.className(c));
      return;
    }
    Method* init = c->findMethod(vm.sInit, vm.sVoidDesc);
    if (!init) {
      vm.throwNew(t, "java/lang/InstantiationException", vm.className(c) + " has no constructor without arguments");
      return;
    }
    if (!(init->flags & ACC_PUBLIC)) {
      vm.throwNew(t, "java/lang/IllegalAccessException", vm.className(c));
      return;
    }
    if (!vm.initClass(t, c)) return;
    Object* o = vm.alloc(c);
    if (!o) return;
    Slot s;
    s.a = o;
    vm.call(t, init, &s);
    if (!t.pending) r.a = o;
  }
  static void getResourceAsStream(Vm& vm, Thread& t, Slot* a, Slot& r) {
    if (npe(vm, t, a[1].a)) return;
    std::string n = vm.str(a[1].a);
    if (n.empty()) return;
    if (n[0] == '/') {
      n.erase(0, 1);
    } else {
      std::string cn = vm.classOf(a[0].a)->name.str();
      size_t sl = cn.rfind('/');
      if (sl != std::string::npos) n = cn.substr(0, sl + 1) + n;
    }
    PVec<uint8_t> buf;
    if (!vm.resource(n, buf)) return;
    r.a = inputStream(vm, t, buf.data(), buf.size());
  }

  // ---- java.lang.String ----
  static void decode(Vm& vm, Thread& t, Slot* a, Slot& r) {
    Object* b = a[0].a;
    int32_t off = a[1].i, n = a[2].i;
    if (npe(vm, t, b)) return;
    if (off < 0 || n < 0 || (int64_t)off + n > b->length) {
      vm.throwNew(t, "java/lang/StringIndexOutOfBoundsException", std::to_string(off) + "+" + std::to_string(n));
      return;
    }
    int enc = encoding(vm, a[3].a);
    if (enc < 0) {
      vm.throwNew(t, "java/io/UnsupportedEncodingException", vm.str(a[3].a));
      return;
    }
    const uint8_t* p = b->data() + off;
    PStr16 u;
    if (enc == 1) {
      utf8To16(reinterpret_cast<const char*>(p), (size_t)n, u);
    } else {
      u.resize((size_t)n);
      for (int32_t i = 0; i < n; i++) u[(size_t)i] = p[i];
    }
    r.a = vm.newString16(u.data(), (int)u.size());
  }
  static Object* bytes(Vm& vm, Thread& t, Object* s, int enc) {
    (void)t;
    Object* arr = s->slots()[vm.fStrValue].a;
    int off = s->slots()[vm.fStrOffset].i, n = s->slots()[vm.fStrCount].i;
    const char16_t* c = arr->elems<char16_t>() + off;
    if (enc == 1) {
      std::string u = utf16To8(c, (size_t)n);
      Object* b = vm.newArray('B', nullptr, (int32_t)u.size());
      if (b && !u.empty()) std::memcpy(b->data(), u.data(), u.size());
      return b;
    }
    Object* b = vm.newArray('B', nullptr, n);
    if (!b) return nullptr;
    c = s->slots()[vm.fStrValue].a->elems<char16_t>() + off;  // the array cannot move, but keep it simple
    for (int i = 0; i < n; i++) b->data()[i] = (uint8_t)(c[i] < 256 ? c[i] : '?');
    return b;
  }
  static void getBytes(Vm& vm, Thread& t, Slot* a, Slot& r) { r.a = bytes(vm, t, a[0].a, 0); }
  static void getBytes0(Vm& vm, Thread& t, Slot* a, Slot& r) {
    int enc = encoding(vm, a[1].a);
    if (enc < 0) {
      vm.throwNew(t, "java/io/UnsupportedEncodingException", vm.str(a[1].a));
      return;
    }
    r.a = bytes(vm, t, a[0].a, enc);
  }
  static void internS(Vm& vm, Thread&, Slot* a, Slot& r) {
    Object* s = a[0].a;
    Object* arr = s->slots()[vm.fStrValue].a;
    int off = s->slots()[vm.fStrOffset].i, n = s->slots()[vm.fStrCount].i;
    PStr16 k(arr->elems<char16_t>() + off, (size_t)n);
    auto it = vm.interned.find(k);
    if (it != vm.interned.end()) {
      r.a = it->second;
      return;
    }
    vm.interned[k] = s;
    r.a = s;
  }

  // ---- java.lang.System, Runtime ----
  static void currentTimeMillis(Vm&, Thread&, Slot*, Slot& r) {
    timeval tv;
    gettimeofday(&tv, nullptr);
    r.j = (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
  }
  static void arraycopy(Vm& vm, Thread& t, Slot* a, Slot&) {
    Object *src = a[0].a, *dst = a[2].a;
    int32_t sp = a[1].i, dp = a[3].i, n = a[4].i;
    if (!src || !dst) {
      vm.throwNew(t, "java/lang/NullPointerException");
      return;
    }
    Class *sc = src->cls, *dc = dst->cls;
    if (!sc->isArray || !dc->isArray || (sc->elem == 'L') != (dc->elem == 'L') || (sc->elem != 'L' && sc->elem != dc->elem)) {
      vm.throwNew(t, "java/lang/ArrayStoreException", vm.className(sc) + " -> " + vm.className(dc));
      return;
    }
    if (n < 0 || sp < 0 || dp < 0 || (int64_t)sp + n > src->length || (int64_t)dp + n > dst->length) {
      vm.throwNew(t, "java/lang/ArrayIndexOutOfBoundsException",
                  "arraycopy: " + std::to_string(sp) + "+" + std::to_string(n) + " of " + std::to_string(src->length) + " -> " +
                      std::to_string(dp) + " of " + std::to_string(dst->length));
      return;
    }
    if (n == 0) return;
    size_t es = (size_t)elemSize(sc->elem);
    if (sc->elem != 'L' || src == dst || vm.assignable(dc->elemClass, sc->elemClass)) {
      std::memmove(dst->data() + (size_t)dp * es, src->data() + (size_t)sp * es, (size_t)n * es);
      return;
    }
    Object** s = src->elems<Object*>() + sp;
    Object** d = dst->elems<Object*>() + dp;
    for (int32_t i = 0; i < n; i++) {
      Object* o = s[i];
      if (o && !vm.assignable(dc->elemClass, o->cls)) {
        vm.throwNew(t, "java/lang/ArrayStoreException", vm.className(o->cls));
        return;
      }
      d[i] = o;
    }
  }
  static void identityHashCode(Vm&, Thread&, Slot* a, Slot& r) { r.i = a[0].a ? (int32_t)identityHash(a[0].a) : 0; }
  static void getProperty(Vm& vm, Thread& t, Slot* a, Slot& r) {
    if (npe(vm, t, a[0].a)) return;
    std::string k = vm.str(a[0].a);
    auto it = vm.props.find(PStr(k.c_str(), k.size()));
    if (it != vm.props.end()) r.a = vm.newString(std::string(it->second.c_str(), it->second.size()));
  }
  static void exitR(Vm& vm, Thread& t, Slot* a, Slot&) {
    if (!vm.allowExit) {
      vm.throwNew(t, "java/lang/SecurityException", "exit is not allowed");
      return;
    }
    vm.exitCode = a[1].i;
    vm.exited = true;
  }
  static void freeMemory(Vm& vm, Thread&, Slot*, Slot& r) {
    r.j = (int64_t)(vm.heapLimit > vm.liveBytes ? vm.heapLimit - vm.liveBytes : 0);
  }
  static void totalMemory(Vm& vm, Thread&, Slot*, Slot& r) { r.j = (int64_t)vm.heapLimit; }
  static void gcR(Vm& vm, Thread&, Slot*, Slot&) { vm.gc(); }

  // ---- numbers ----
  static void floatToIntBits(Vm&, Thread&, Slot* a, Slot& r) {
    float f = a[0].f;
    if (f != f) {
      r.i = 0x7fc00000;
      return;
    }
    std::memcpy(&r.i, &f, 4);
  }
  static void intBitsToFloat(Vm&, Thread&, Slot* a, Slot& r) {
    int32_t i = a[0].i;
    std::memcpy(&r.f, &i, 4);
  }
  static void doubleToLongBits(Vm&, Thread&, Slot* a, Slot& r) {
    double d = a[0].d;
    if (d != d) {
      r.j = 0x7ff8000000000000LL;
      return;
    }
    std::memcpy(&r.j, &d, 8);
  }
  static void longBitsToDouble(Vm&, Thread&, Slot* a, Slot& r) {
    int64_t j = a[0].j;
    std::memcpy(&r.d, &j, 8);
  }
  static void doubleToString(Vm& vm, Thread&, Slot* a, Slot& r) { r.a = vm.newString(javaDouble(a[0].d, false)); }
  static void floatToString(Vm& vm, Thread&, Slot* a, Slot& r) { r.a = vm.newString(javaDouble((double)a[0].f, true)); }
  static void parseDouble(Vm& vm, Thread& t, Slot* a, Slot& r) {
    if (npe(vm, t, a[0].a)) return;
    std::string s = vm.str(a[0].a);
    double d;
    if (!parseJava(s, d)) {
      vm.throwNew(t, "java/lang/NumberFormatException", s);
      return;
    }
    r.d = d;
  }
  static void sin(Vm&, Thread&, Slot* a, Slot& r) { r.d = std::sin(a[0].d); }
  static void cos(Vm&, Thread&, Slot* a, Slot& r) { r.d = std::cos(a[0].d); }
  static void tan(Vm&, Thread&, Slot* a, Slot& r) { r.d = std::tan(a[0].d); }
  static void sqrt(Vm&, Thread&, Slot* a, Slot& r) { r.d = std::sqrt(a[0].d); }
  static void ceil(Vm&, Thread&, Slot* a, Slot& r) { r.d = std::ceil(a[0].d); }
  static void floor(Vm&, Thread&, Slot* a, Slot& r) { r.d = std::floor(a[0].d); }

  // java.util.TimeZone's default when no property sets it: the C library's local time (TZ on the board)
  static void hostOffset(Vm&, Thread&, Slot*, Slot& r) {
    time_t now = time(nullptr);
    struct tm lt, gt;
    localtime_r(&now, &lt);
    gmtime_r(&now, &gt);
    int d = lt.tm_yday - gt.tm_yday;
    if (lt.tm_year != gt.tm_year) d = lt.tm_year > gt.tm_year ? 1 : -1;
    r.i = d * 1440 + (lt.tm_hour - gt.tm_hour) * 60 + (lt.tm_min - gt.tm_min);
  }

  // ---- java.lang.Thread ----
  static void start(Vm& vm, Thread& t, Slot* a, Slot&) {
    Object* jt = a[0].a;
    if (jt->slots()[vm.fThreadVm].i != 0) {
      vm.throwNew(t, "java/lang/IllegalThreadStateException", "already started");
      return;
    }
    vm.startThread(jt);
  }
  static void currentThread(Vm& vm, Thread& t, Slot*, Slot& r) {
    if (!t.jthread) vm.makeJThread(t, "Thread-" + std::to_string(t.id));
    r.a = t.jthread;
  }
  static void sleep(Vm& vm, Thread& t, Slot* a, Slot&) {
    int64_t ms = a[0].j;
    if (ms < 0) {
      vm.throwNew(t, "java/lang/IllegalArgumentException", "timeout value is negative");
      return;
    }
    if (t.interrupted) {
      t.interrupted = false;
      vm.throwNew(t, "java/lang/InterruptedException", "");
      return;
    }
    if (ms == 0) {
      t.yieldReq = true;
      return;
    }
    t.state = TState::Sleeping;
    t.wakeAt = vm.nowUs() + (uint64_t)(ms < 1000000000000LL ? ms : 1000000000000LL) * 1000u;  // capped at ~31 years
  }
  static void yield(Vm&, Thread& t, Slot*, Slot&) { t.yieldReq = true; }
  static void activeCount(Vm& vm, Thread&, Slot*, Slot& r) {
    int n = 0;
    for (Thread* x : vm.threads)
      if (!x->host && x->state != TState::Done) n++;
    r.i = n;
  }
  static void isAlive(Vm& vm, Thread&, Slot* a, Slot& r) {
    Thread* x = vm.threadOf(a[0].a);
    r.i = x && x->state != TState::Done && (x->depth > 0 || x->host || x->nestLevel > 0);
  }
  static void interrupt(Vm& vm, Thread& t, Slot* a, Slot&) {
    Thread* x = vm.threadOf(a[0].a);
    if (!x || x->state == TState::Done) return;
    if (x != &t && (x->state == TState::Sleeping || x->state == TState::Waiting)) {
      Object* e = vm.makeThrowable(t, "java/lang/InterruptedException", "");
      x->pending = e;
      x->state = x->state == TState::Sleeping ? TState::Ready : TState::Blocked;
      x->wakeAt = 0;
    } else {
      x->interrupted = true;
    }
  }

  // ---- java.lang.Throwable, java.io.PrintStream ----
  static void fillIn(Vm& vm, Thread& t, Slot* a, Slot&) { vm.fillTrace(t, a[0].a); }
  static void printStackTrace(Vm& vm, Thread& t, Slot* a, Slot&) { vm.printTrace(t, a[0].a, ""); }
  static void put(Vm& vm, Thread&, Slot* a, Slot&) { vm.output(a[0].i, a[1].a ? vm.str(a[1].a) : std::string("null")); }
};

void Vm::registerBuiltins() {
  struct {
    const char* key;
    Native fn;
  } list[] = {
      {"java/lang/Object.getClass()Ljava/lang/Class;", Natives::getClass},
      {"java/lang/Object.hashCode()I", Natives::hashCode},
      {"java/lang/Object.notify()V", Natives::notify},
      {"java/lang/Object.notifyAll()V", Natives::notifyAll},
      {"java/lang/Object.wait(J)V", Natives::wait},
      {"java/lang/Object.clone()Ljava/lang/Object;", Natives::clone},
      {"java/lang/Class.getName()Ljava/lang/String;", Natives::getName},
      {"java/lang/Class.isInstance(Ljava/lang/Object;)Z", Natives::isInstance},
      {"java/lang/Class.isArray()Z", Natives::isArray},
      {"java/lang/Class.isInterface()Z", Natives::isInterface},
      {"java/lang/Class.isAssignableFrom(Ljava/lang/Class;)Z", Natives::isAssignableFrom},
      {"java/lang/Class.forName(Ljava/lang/String;)Ljava/lang/Class;", Natives::forName},
      {"java/lang/Class.newInstance()Ljava/lang/Object;", Natives::newInstance},
      {"java/lang/Class.getResourceAsStream(Ljava/lang/String;)Ljava/io/InputStream;", Natives::getResourceAsStream},
      {"java/lang/String.decode([BIILjava/lang/String;)Ljava/lang/String;", Natives::decode},
      {"java/lang/String.getBytes()[B", Natives::getBytes},
      {"java/lang/String.getBytes0(Ljava/lang/String;)[B", Natives::getBytes0},
      {"java/lang/String.intern()Ljava/lang/String;", Natives::internS},
      {"java/lang/System.currentTimeMillis()J", Natives::currentTimeMillis},
      {"java/lang/System.arraycopy(Ljava/lang/Object;ILjava/lang/Object;II)V", Natives::arraycopy},
      {"java/lang/System.identityHashCode(Ljava/lang/Object;)I", Natives::identityHashCode},
      {"java/lang/System.getProperty(Ljava/lang/String;)Ljava/lang/String;", Natives::getProperty},
      {"java/lang/Runtime.exit(I)V", Natives::exitR},
      {"java/lang/Runtime.freeMemory()J", Natives::freeMemory},
      {"java/lang/Runtime.totalMemory()J", Natives::totalMemory},
      {"java/lang/Runtime.gc()V", Natives::gcR},
      {"java/lang/Float.floatToIntBits(F)I", Natives::floatToIntBits},
      {"java/lang/Float.intBitsToFloat(I)F", Natives::intBitsToFloat},
      {"java/lang/Float.toString(F)Ljava/lang/String;", Natives::floatToString},
      {"java/lang/Double.doubleToLongBits(D)J", Natives::doubleToLongBits},
      {"java/lang/Double.longBitsToDouble(J)D", Natives::longBitsToDouble},
      {"java/lang/Double.toString(D)Ljava/lang/String;", Natives::doubleToString},
      {"java/lang/Double.parseDouble(Ljava/lang/String;)D", Natives::parseDouble},
      {"java/lang/Math.sin(D)D", Natives::sin},
      {"java/lang/Math.cos(D)D", Natives::cos},
      {"java/lang/Math.tan(D)D", Natives::tan},
      {"java/lang/Math.sqrt(D)D", Natives::sqrt},
      {"java/lang/Math.ceil(D)D", Natives::ceil},
      {"java/lang/Math.floor(D)D", Natives::floor},
      {"java/lang/Thread.start()V", Natives::start},
      {"java/lang/Thread.currentThread()Ljava/lang/Thread;", Natives::currentThread},
      {"java/lang/Thread.sleep(J)V", Natives::sleep},
      {"java/lang/Thread.yield()V", Natives::yield},
      {"java/lang/Thread.activeCount()I", Natives::activeCount},
      {"java/lang/Thread.isAlive()Z", Natives::isAlive},
      {"java/lang/Thread.interrupt()V", Natives::interrupt},
      {"java/lang/Throwable.fillIn()V", Natives::fillIn},
      {"java/lang/Throwable.printStackTrace()V", Natives::printStackTrace},
      {"java/io/PrintStream.put(ILjava/lang/String;)V", Natives::put},
      {"java/util/TimeZone.hostOffset()I", Natives::hostOffset},
  };
  for (auto& n : list) registerNative(n.key, n.fn);
}

}  // namespace j2me
