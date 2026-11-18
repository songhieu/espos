// j2me-run: runs a Java main class on EspOS's J2ME VM (src/j2me) on the computer. docs/j2me.md.
//   build/j2me-run [-cp folder_or_jar]... [-Xmx 2m] [-Xss slots] [-bench] [-gcstress N] [-boot folder_or_jar] Class [args]
// -gcstress N: a full GC every N allocations (1 = each), to catch objects C++ forgot to keep alive.
// The class library (j2me/build/classlib) loads first. Exit status: the program's System.exit code; 1 when the main
// thread died of an exception (its stack trace is printed); 2 on a deadlock or a missing class.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../../src/j2me/jvm.h"

#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

static std::string exeDir() {
  char buf[PATH_MAX];
#ifdef __APPLE__
  uint32_t n = sizeof buf;
  if (_NSGetExecutablePath(buf, &n) != 0) return ".";
  char real[PATH_MAX];
  if (realpath(buf, real)) std::strcpy(buf, real);
#else
  ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
  if (n <= 0) return ".";
  buf[n] = 0;
#endif
  std::string s = buf;
  size_t sl = s.rfind('/');
  return sl == std::string::npos ? "." : s.substr(0, sl);
}

static bool isDir(const std::string& p) {
  struct stat st;
  return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

static bool readFile(const std::string& p, std::vector<uint8_t>& out) {
  FILE* f = std::fopen(p.c_str(), "rb");
  if (!f) return false;
  std::fseek(f, 0, SEEK_END);
  long n = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  out.resize(n > 0 ? (size_t)n : 0);
  bool ok = n <= 0 || std::fread(out.data(), 1, (size_t)n, f) == (size_t)n;
  std::fclose(f);
  return ok;
}

static size_t parseSize(const char* s) {
  char* e;
  double v = std::strtod(s, &e);
  if (*e == 'k' || *e == 'K') v *= 1024;
  if (*e == 'm' || *e == 'M') v *= 1024 * 1024;
  if (*e == 'g' || *e == 'G') v *= 1024.0 * 1024 * 1024;
  return (size_t)v;
}

static bool addPath(j2me::Vm& vm, const std::string& p) {
  if (isDir(p)) {
    vm.addDir(p);
    return true;
  }
  std::vector<uint8_t> b;
  if (!readFile(p, b)) {
    std::fprintf(stderr, "j2me-run: cannot read %s\n", p.c_str());
    return false;
  }
  vm.addJar(b.data(), b.size());
  return true;
}

// Natives for j2me/test/CallbackTest.java: they call back into Java the way the MIDP layer does.
static j2me::Thread* eventThread;

static void runRunnable(j2me::Vm& vm, j2me::Thread& t, j2me::Object* r) {
  j2me::Method* m = vm.findVirtual(r->cls, "run", "()V");
  j2me::Slot a;
  a.a = r;
  vm.call(t, m, &a);  // an exception stays in t.pending: returning passes it on
}

static void registerTestNatives(j2me::Vm& vm) {
  vm.registerNative("CallbackTest.callRun(Ljava/lang/Runnable;)V",
                    [](j2me::Vm& vm, j2me::Thread& t, j2me::Slot* args, j2me::Slot&) { runRunnable(vm, t, args[0].a); });
  vm.registerNative("CallbackTest.callOnHost(Ljava/lang/Runnable;)V", [](j2me::Vm& vm, j2me::Thread& t, j2me::Slot* args, j2me::Slot&) {
    if (!eventThread) eventThread = vm.newThread("event");
    runRunnable(vm, *eventThread, args[0].a);
    if (eventThread->pending) {  // move the exception to the calling thread
      t.pending = eventThread->pending;
      eventThread->pending = nullptr;
    }
  });
  vm.registerNative("CallbackTest.callAdd(IJD)D", [](j2me::Vm& vm, j2me::Thread& t, j2me::Slot* args, j2me::Slot& ret) {
    j2me::Method* m = vm.findStatic(vm.loadClass("CallbackTest"), "add", "(IJD)D");
    ret = vm.call(t, m, args);
  });
  vm.registerNative("CallbackTest.allocMany(I)[Ljava/lang/Object;", [](j2me::Vm& vm, j2me::Thread&, j2me::Slot* args, j2me::Slot& ret) {
    int n = args[0].i;
    // the strings are allocated first and kept only by the GC rule (Vm::locals), then the array
    std::vector<j2me::Object*> made;
    for (int i = 0; i < n; i++) {
      j2me::Object* s = vm.newString("s" + std::to_string(i));
      if (!s) return;
      made.push_back(s);
    }
    j2me::Object* arr = vm.newArray('L', vm.loadClass("java/lang/Object"), n);
    if (!arr) return;
    for (int i = 0; i < n; i++) arr->elems<j2me::Object*>()[i] = made[(size_t)i];
    ret.a = arr;
  });
}

int main(int argc, char** argv) {
  std::vector<std::string> cp, args;
  std::string boot, cls;
  size_t heap = 0;
  int stack = 0;
  bool bench = false, verbose = false;
  uint32_t gcStress = 0;
  int i = 1;
  for (; i < argc; i++) {
    std::string a = argv[i];
    if ((a == "-cp" || a == "-classpath") && i + 1 < argc) {
      std::string v = argv[++i];
      size_t s = 0;
      while (s <= v.size()) {
        size_t c = v.find(':', s);
        if (c == std::string::npos) c = v.size();
        if (c > s) cp.push_back(v.substr(s, c - s));
        s = c + 1;
      }
    } else if (a == "-boot" && i + 1 < argc) {
      boot = argv[++i];
    } else if (a.rfind("-Xmx", 0) == 0) {
      heap = parseSize(a.size() > 4 ? a.c_str() + 4 : (i + 1 < argc ? argv[++i] : "0"));
    } else if (a.rfind("-Xss", 0) == 0) {
      stack = (int)parseSize(a.size() > 4 ? a.c_str() + 4 : (i + 1 < argc ? argv[++i] : "0"));
    } else if (a == "-gcstress" && i + 1 < argc) {
      gcStress = (uint32_t)std::atoi(argv[++i]);
    } else if (a == "-bench") {
      bench = true;
    } else if (a == "-v") {
      verbose = true;
    } else if (!a.empty() && a[0] == '-') {
      std::fprintf(stderr, "j2me-run: unknown option %s\n", a.c_str());
      return 2;
    } else {
      cls = a;
      i++;
      break;
    }
  }
  for (; i < argc; i++) args.push_back(argv[i]);
  if (cls.empty()) {
    std::fprintf(stderr,
                 "usage: j2me-run [-cp folder_or_jar[:...]]... [-Xmx 2m] [-Xss slots] [-bench] [-boot classlib] Class [args]\n");
    return 2;
  }
  if (boot.empty()) {
    std::string d = exeDir();
    for (const char* rel : {"/../../../j2me/build/classlib", "/../../j2me/build/classlib", "/../j2me/build/classlib"})
      if (isDir(d + rel)) {
        boot = d + rel;
        break;
      }
    if (boot.empty()) boot = "j2me/build/classlib";
  }
  j2me::Vm vm([](const std::string& s) { std::fprintf(stderr, "%s\n", s.c_str()); });
  vm.out = [](int fd, const char* s, size_t n) { std::fwrite(s, 1, n, fd == 2 ? stderr : stdout); };
  if (heap) vm.heapLimit = heap;
  if (stack) vm.stackSlots = stack;
  vm.gcStress = gcStress;
  registerTestNatives(vm);
  if (!addPath(vm, boot)) return 2;
  for (auto& p : cp)
    if (!addPath(vm, p)) return 2;
  uint64_t t0 = vm.nowUs();
  bool ok = vm.runMain(cls, args);
  uint64_t t1 = vm.nowUs();
  std::fflush(stdout);
  if (bench || verbose) {
    double s = (double)(t1 - t0) / 1e6;
    std::fprintf(stderr, "j2me-run: %.3f s, %llu bytecodes, %.1f M bytecodes/s, %u GCs, heap %zu KB in use\n", s,
                 (unsigned long long)vm.bytecodes, s > 0 ? (double)vm.bytecodes / s / 1e6 : 0.0, vm.gcCount, vm.heapBytes() / 1024);
  }
  if (!ok) return 2;
  if (vm.exited) return vm.exitCode;
  if (vm.mainUncaught) return 1;
  if (vm.deadlocked) return 2;
  return 0;
}
