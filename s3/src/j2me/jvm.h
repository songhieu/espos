#pragma once
// EspOS's J2ME virtual machine (CLDC 1.1 + MIDP 2.0 for old phone games, docs/j2me.md).
// A bytecode interpreter for the classes javac makes for Java 8 and older (the games were made for 1.1-1.4), with
// green threads (each thread its own frames; the VM switches between them), a mark-sweep collector, and natives in
// C++. The class library (java.lang, java.util, java.io, then javax.microedition) is Java code in s3/j2me/classlib
// compiled into a JAR (or folder) the VM loads first. Plain C++17, no exceptions, no RTTI: built for the computer
// (tools/j2me) and for the board (ESP_PLATFORM: every VM table, class, stack and the Java heap live in PSRAM).
//
// GC rule for natives (docs/j2me.md "Natives"): the collector runs only inside allocations (alloc, newArray,
// newString*, intern, mirror, call, throwNew, loadClass/initClass...). Every Object* the VM hands to C++ (the results
// of those functions, call()'s return value) stays alive until the native that is running returns; outside natives
// (host code between step()s) until the next step()/runMain(). Objects in a native's args stay alive (they are on the
// Java stack). An Object* read out of a field or an array is only as alive as that field: pass it to keep() if you
// overwrite the field and allocate after. Use addRoot()/removeRoot() for references kept across calls.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace j2me {

struct Class;
struct Object;
struct Thread;
class Vm;

// Memory of the VM (Java heap, classes, stacks, tables): PSRAM on the board (heap_caps_malloc), malloc elsewhere.
void* vmMalloc(size_t n);
void vmFree(void* p);

template <class T>
struct PsAlloc {
  using value_type = T;
  PsAlloc() = default;
  template <class U>
  PsAlloc(const PsAlloc<U>&) {}
  T* allocate(size_t n) {
    void* p = vmMalloc(n * sizeof(T));
    if (!p) std::abort();  // VM tables only (the Java heap reports OutOfMemoryError instead)
    return static_cast<T*>(p);
  }
  void deallocate(T* p, size_t) { vmFree(p); }
  template <class U>
  bool operator==(const PsAlloc<U>&) const { return true; }
  template <class U>
  bool operator!=(const PsAlloc<U>&) const { return false; }
};
template <class T>
using PVec = std::vector<T, PsAlloc<T>>;
using PStr = std::basic_string<char, std::char_traits<char>, PsAlloc<char>>;
using PStr16 = std::basic_string<char16_t, std::char_traits<char16_t>, PsAlloc<char16_t>>;
struct StrHash {
  size_t operator()(const PStr& s) const;
  size_t operator()(const PStr16& s) const;
};
template <class K, class V, class H = std::hash<K>>
using PMap = std::unordered_map<K, V, H, std::equal_to<K>, PsAlloc<std::pair<const K, V>>>;

union Slot {
  int32_t i;
  float f;
  int64_t j;
  double d;
  Object* a;
};

// A native method: args are the receiver (non-static) then the arguments, one Slot each (long and double too).
// It returns through ret; it may throw with vm.throwNew(...) and then must return at once.
using Native = void (*)(Vm& vm, Thread& t, Slot* args, Slot& ret);

// An interned name: equal names share one pointer. Compare with == against another Sym (pointer), a C string or a
// std::string (characters).
struct Sym {
  const char* p = nullptr;
  Sym() = default;
  explicit Sym(const char* s) : p(s) {}
  const char* c_str() const { return p ? p : ""; }
  std::string str() const { return c_str(); }
  bool ok() const { return p != nullptr; }
  bool operator==(const Sym& o) const { return p == o.p; }
  bool operator!=(const Sym& o) const { return p != o.p; }
  bool operator==(const char* s) const { return std::strcmp(c_str(), s) == 0; }
  bool operator!=(const char* s) const { return !(*this == s); }
  bool operator==(const std::string& s) const { return s == c_str(); }
  bool operator!=(const std::string& s) const { return !(*this == s); }
  char operator[](size_t i) const { return p[i]; }
  size_t size() const { return p ? std::strlen(p) : 0; }
};

struct Field {
  Sym name, desc;
  uint16_t flags = 0;
  bool isStatic = false;
  int slot = 0;  // instance: the Slot in the object (obj->slots()[slot]); static: the Slot in owner->statics
  Class* owner = nullptr;
  Slot* addr = nullptr;    // statics: &owner->statics[slot]
  uint16_t constIdx = 0;   // ConstantValue (static finals)
  bool isRef() const { return desc.p[0] == 'L' || desc.p[0] == '['; }
  bool isWide() const { return desc.p[0] == 'J' || desc.p[0] == 'D'; }
};

struct ExEntry {
  uint16_t start, end, handler, type;
};

struct Method {
  Sym name, desc;
  uint16_t flags = 0;
  Class* owner = nullptr;
  uint16_t maxStack = 0, maxLocals = 0;
  uint8_t* code = nullptr;  // in owner->bytes; the interpreter rewrites resolved instructions in place
  uint32_t codeLen = 0;
  PVec<ExEntry> ex;
  const uint8_t* lines = nullptr;  // LineNumberTable (raw, big-endian start_pc/line pairs)
  uint16_t lineCount = 0;
  int argSlots = 0;  // JVM slots of the arguments, the receiver included (long/double take two)
  int argCount = 0;  // arguments in Slots (the receiver included), as natives get them
  char ret = 'V';    // the return type's first descriptor character ('L' and '[' for references)
  uint32_t wideArgs = 0;  // bit i: argument i (receiver = 0) is a long/double
  Native native = nullptr;
  int vindex = -1;  // its vtable entry (virtual methods)
  uint32_t id = 0;  // index in the VM's method table (stack traces)
  bool isStatic() const { return flags & 0x0008; }
  bool isNative() const { return flags & 0x0100; }
  bool isSync() const { return flags & 0x0020; }
  bool isAbstract() const { return flags & 0x0400; }
  bool isPrivate() const { return flags & 0x0002; }
  bool isFinal() const { return flags & 0x0010; }
  int line(uint32_t pc) const;  // -1: unknown
};

struct CpEntry {
  uint8_t tag = 0;
  uint8_t flag = 0;
  uint16_t extra = 0;        // invokevirtual: the vtable index
  uint32_t a = 0, b = 0;     // indexes, or the raw value (Utf8: offset in the class file, length)
  void* resolved = nullptr;  // Class*, Field*, Method*, Object* (a String), Sym text (Utf8)
};

struct Class {
  Sym name;  // "java/lang/String", "[I", "[Ljava/lang/Object;"
  uint16_t flags = 0;
  Class* super = nullptr;
  PVec<Class*> interfaces;  // declared
  PVec<Class*> allIfaces;   // every interface it implements, inherited ones too
  PVec<CpEntry> cp;
  PVec<Field> fields;
  PVec<Method> methods;
  PVec<uint8_t> bytes;  // the class file (code points into it)
  int instanceSlots = 0;  // with the superclasses'
  PVec<uint16_t> refSlots;  // instance Slots that hold references (the collector)
  PVec<Slot> statics;
  PVec<uint16_t> staticRefs;
  PVec<Method*> vtable;
  int state = 0;  // 0 loaded, 1 linked, 2 initializing, 3 ready, 4 failed
  Thread* initThread = nullptr;
  Sym sourceFile;
  // arrays
  bool isArray = false;
  char elem = 0;            // 'I','B','C','S','Z','J','F','D', or 'L' for references (arrays of arrays too)
  Class* elemClass = nullptr;  // references: the component class
  Class* arrayClass = nullptr;  // the class of arrays of this one, once made
  Object* mirror = nullptr;     // its java.lang.Class (Vm::mirror makes it)
  bool isInterface() const { return flags & 0x0200; }
  bool isAbstract() const { return flags & 0x0400; }
  Method* findMethod(const std::string& name, const std::string& desc) const;  // this class only
  Field* findField(const std::string& name, const std::string& desc) const;    // this class only
  Method* findMethod(Sym name, Sym desc) const;
  Field* findField(Sym name, Sym desc) const;
};

struct alignas(8) Object {
  Class* cls;
  uint32_t mark;   // the collector's
  int32_t length;  // arrays
  // then: instance Slots, or the array's elements (boolean/byte int8_t, char uint16_t, short int16_t, int int32_t,
  // float, long int64_t, double, references Object*)
  Slot* slots() { return reinterpret_cast<Slot*>(this + 1); }
  uint8_t* data() { return reinterpret_cast<uint8_t*>(this + 1); }
  template <class T>
  T* elems() { return reinterpret_cast<T*>(this + 1); }
};

static_assert(sizeof(Object) % 8 == 0, "instance Slots and long/double elements must stay 8-byte aligned");

struct Frame {
  Method* m;
  uint32_t pc;    // the instruction running (while a call it makes runs: the invoke)
  Slot* locals;
  Slot* sp;       // next free operand slot
  Object* sync;   // the monitor a synchronized method holds
  uint8_t skip;   // a native blocked the thread here: on resume pc moves past the invoke (this many bytes)
};

enum class TState : uint8_t { Ready, Sleeping, Blocked, Waiting, Done };

struct Thread {
  int id = 0;
  Object* jthread = nullptr;  // its java.lang.Thread
  Slot* stack = nullptr;      // [stack, stackEnd)
  Slot* stackEnd = nullptr;
  Frame* frames = nullptr;
  int depth = 0, maxDepth = 0;
  Slot* stackLimit = nullptr;  // the interpreter's limits: a reserve below the real ends (StackOverflowError's own
  int depthLimit = 0;          // frames use it)
  TState state = TState::Ready;
  uint64_t wakeAt = 0;        // Sleeping/Waiting: nowUs() to wake at (0: no timeout)
  Object* waitOn = nullptr;   // the monitor it waits to enter, or waits on
  Class* waitClass = nullptr; // Blocked: another thread is initializing this class
  int waitCount = 0;          // >0: after wait(), the monitor's count to take back
  Object* pending = nullptr;  // an exception being thrown
  Slot ret{};                 // what the last nested call returned
  bool host = false;          // made by Vm::newThread: runs only what call() gives it
  bool interrupted = false;
  bool yieldReq = false;
  int nestLevel = 0;          // call()s running on it
  int nestBase = 0;           // the innermost call()'s base depth: run() stops there
  bool nestDone = false;      // that call's Java code has returned; its C++ caller has not picked it up yet
  bool inNative = false;      // its innermost activation is a native (C++ is running on it): never scheduled then
};

class Vm {
 public:
  explicit Vm(std::function<void(const std::string&)> log);
  ~Vm();
  Vm(const Vm&) = delete;
  Vm& operator=(const Vm&) = delete;

  // Settings (before the first class loads).
  size_t heapLimit;      // Java heap bytes; past it (after a full GC) OutOfMemoryError. Board 3 MB, computer 64 MB.
  int stackSlots;        // per thread (8 bytes each); deeper recursion throws StackOverflowError
  bool allowExit = true; // false: System.exit/Runtime.exit throw SecurityException (MIDP)
  uint32_t gcStress = 0;  // tests: a full collection every this many allocations (1 = each one)
  // System.out (fd 1) / System.err (fd 2) text as printed. Unset: whole lines go to the log.
  std::function<void(int fd, const char* s, size_t n)> out;
  // How the VM waits when a nested call() blocks and every thread sleeps. Unset: usleep.
  std::function<void(uint32_t us)> idleSleep;
  void setProperty(const std::string& key, const std::string& value);  // System.getProperty

  // Where classes come from: JARs (in memory) and folders, searched in order; resources too.
  // copy=false: the bytes stay the caller's and must outlive the Vm (e.g. memory-mapped flash).
  void addJar(const uint8_t* zip, size_t len, bool copy = true);
  void addDir(const std::string& path);
  bool resource(const std::string& name, std::vector<uint8_t>& out);
  bool resource(const std::string& name, PVec<uint8_t>& out);  // the same into PSRAM (use this one on the board)
  // Runs Class.main(String[]) until every thread has ended. false: the class or main is missing.
  bool runMain(const std::string& cls, const std::vector<std::string>& args = {});
  // The same, but returns once the main thread exists (its class initialized): the host then calls step() in its
  // own loop. false: the class or main is missing.
  bool startMain(const std::string& cls, const std::vector<std::string>& args = {});
  // Ends every Java thread: no Java code runs after it (finally blocks do not run either), call() does nothing and
  // the next step() returns false. Safe inside a native (it takes effect when the native returns) and between
  // steps. Delete the Vm afterwards to free everything.
  void stop() { exited = true; }
  // A slice of every ready thread, for up to budgetUs, then returns: false when no thread is left (host threads
  // from newThread do not count). Hosts call it in their loop. After it, idleUs says how long nothing will run:
  // 0 = threads still have work (call again soon), kForever = all threads wait (only notify/interrupt/a host call
  // can wake them), else the microseconds until the earliest sleep/wait timeout. The host may sleep that long.
  bool step(uint32_t budgetUs);
  uint32_t idleUs = 0;
  static constexpr uint32_t kForever = 0xffffffffu;
  bool exited = false;   // System.exit or stop() ran
  int exitCode = 0;
  int uncaught = 0;      // threads that died of an exception
  bool mainUncaught = false;  // runMain's thread did
  bool deadlocked = false;    // runMain stopped: every thread waited forever

  // For natives
  Class* loadClass(const std::string& name);  // "java/lang/String" or an array name; nullptr + pending exception
  bool initClass(Thread& t, Class* c);        // runs <clinit> if needed; false + pending exception
  Object* alloc(Class* c);
  Object* newArray(char elem, Class* elemClass, int32_t n);  // elem 'L': elemClass is the component class
  Object* newString(const std::string& utf8);
  Object* newString16(const char16_t* s, int n);
  Object* intern(const std::string& utf8);
  std::string str(Object* s);  // UTF-8 of a java.lang.String ("" for null)
  // A String's UTF-16 characters without a copy: returns the length and points *chars at the first one (the
  // String's offset applied); -1 for null. Valid while the String lives (objects never move).
  int strChars(Object* s, const char16_t** chars);
  void throwNew(Thread& t, const char* cls, const std::string& msg = "");
  // Runs Java to its return on thread t (nested: no switching to other threads while it can run; if it blocks on a
  // monitor, wait() or sleep(), the other threads run until it can go on). args: receiver then arguments, one Slot
  // each. A thrown exception is left in t.pending (the native must then return). Static methods' classes are
  // initialized first.
  Slot call(Thread& t, Method* m, const Slot* args);
  Method* findVirtual(Class* c, const std::string& name, const std::string& desc);  // what a virtual call runs
  Method* findStatic(Class* c, const std::string& name, const std::string& desc);   // c and its superclasses
  Field* findField(Class* c, const std::string& name, const std::string& desc);     // c and its superclasses
  bool isInstance(Object* o, Class* c);
  bool assignable(Class* to, Class* from);
  Object* mirror(Class* c);
  Class* classOf(Object* mirror);
  void registerNative(const std::string& key, Native fn);  // "java/lang/Math.sqrt(D)D"
  Thread* startThread(Object* jthread);       // runs jthread.run() on a new green thread
  Thread* newThread(const std::string& name);  // a host thread (with a java.lang.Thread) for call()s
  Thread* current() { return cur; }
  bool enter(Thread& t, Object* o);  // false: owned by another thread (the caller must block t and retry)
  bool exit(Thread& t, Object* o);   // false: t does not own it
  void wait(Thread& t, Object* o, int64_t ms);  // natives: t blocks when the native returns
  void notify(Object* o, bool all);
  void gc();
  uint64_t nowUs();
  size_t heapBytes() const { return liveBytes; }
  void log(const std::string& s) { logFn(s); }
  void addRoot(Object** r) { roots.push_back(r); }
  void removeRoot(Object** r);
  Object* keep(Object* o);  // keeps o alive like an allocation result (see the GC rule above)
  struct Scope {            // objects kept inside it are released when it ends (long loops in natives)
    explicit Scope(Vm& v) : vm(v), mark(v.locals.size()) {}
    ~Scope() { vm.locals.resize(mark); }
    Vm& vm;
    size_t mark;
  };
  std::string stackTrace(Thread& t);  // the thread's Java frames, innermost first
  std::string threadDump();           // every thread: state, what it waits for, frames; held monitors
  std::string className(Class* c);    // "java.lang.String"

  uint64_t bytecodes = 0;  // run so far (benchmarks; the board build may compile the counting out)
  uint32_t gcCount = 0;

 private:
  struct SymTab {
    ~SymTab();
    Sym intern(const char* s, size_t n);
    Sym intern(const char* s) { return intern(s, std::strlen(s)); }
    Sym find(const char* s, size_t n) const;
    void grow();
    const char** table = nullptr;
    size_t cap = 0, count = 0;
    PVec<char*> chunks;
    char* at = nullptr;
    size_t left = 0;
  };
  struct Entry {
    uint32_t offset, csize, usize;
    uint16_t method;
  };
  struct Source {
    const uint8_t* zip = nullptr;
    size_t len = 0;
    bool owned = false;
    std::string dir;
    PMap<PStr, Entry, StrHash> entries;
  };
  struct Monitor {
    Object* obj;
    Thread* owner;
    int count;
  };
  struct ICache {
    Class* cls;
    Method* im;
    Method* target;
  };

  std::function<void(const std::string&)> logFn;
  std::string lineBuf[3];
  SymTab syms;
  PVec<Source> sources;
  PMap<const char*, Class*> classes;  // by Sym pointer
  PMap<PStr, Native, StrHash> natives;
  PMap<PStr16, Object*, StrHash> interned;
  PMap<PStr, PStr, StrHash> props;
  PVec<Monitor> monitors;
  PVec<Thread*> threads;
  PVec<Object**> roots;
  PVec<Object*> locals;   // objects handed to C++ (the GC rule)
  PVec<Object*> heap;     // every object
  size_t heapSorted = 0;  // heap[0, heapSorted) is in address order
  PVec<Object*> markStack;
  PVec<Method*> allMethods;
  PVec<Method*> direct;   // invokedirect_q's targets
  ICache* icache = nullptr;  // 256 entries (PSRAM): class + interface method -> target
  Thread* cur = nullptr;
  Thread* bootThread = nullptr;
  Thread* mainThread = nullptr;
  size_t liveBytes = 0, nextGc = 0;
  uint32_t allocCount = 0;
  int nextThreadId = 1;
  size_t rr = 0;  // round-robin position
  bool booted = false, booting = false;
  int throwDepth = 0;
  Object* oomError = nullptr;
  Object* soError = nullptr;
  Class* primArray[16] = {};
  // core classes and their fields' slots
  Class *cObject = nullptr, *cString = nullptr, *cClass = nullptr, *cThread = nullptr, *cThrowable = nullptr;
  int fStrValue = 0, fStrOffset = 0, fStrCount = 0, fClassVm = 0, fThreadVm = 0, fThreadName = 0;
  int fThrowTrace = 0, fThrowMsg = 0;
  Sym sInit, sClinit, sVoidDesc;

  friend struct Interp;
  friend struct Natives;
  bool boot();
  bool readEntry(Source& s, const Entry& e, uint8_t* (*sink)(void*, size_t), void* ctx);
  bool readResource(const std::string& name, uint8_t* (*sink)(void*, size_t), void* ctx);
  Class* findLoaded(Sym name);
  Class* defineClass(Sym name, PVec<uint8_t>& bytes);
  Class* arrayClassNamed(Sym name);
  Class* arrayOf(Class* comp);
  Class* primArrayClass(char elem);
  void bindNative(Method* m);
  Sym utf8(Class* c, uint16_t idx);
  Class* resolveClass(Thread& t, Class* from, uint16_t idx);
  Field* resolveField(Thread& t, Class* from, uint16_t idx);
  Method* resolveMethod(Thread& t, Class* from, uint16_t idx, bool iface);
  Method* lookupMethod(Class* c, Sym name, Sym desc);  // c, superclasses, then interfaces
  Method* vtableFind(Class* c, Sym name, Sym desc);
  Method* superTarget(Class* from, Method* m);
  Method* ifaceTarget(Class* c, Method* im);
  int initForInterp(Thread& t, Class* c);  // 0 ready (or this thread's), 1 exception, 2 wait for another thread
  Object* allocRaw(size_t bytes, Class* c);
  Object* newObject(Class* c);  // uninitialized instance (not kept)
  Object* newArrayRaw(Class* arrCls, int32_t n);
  Object* string16Raw(const char16_t* s, int n);
  Object* internRaw(const PStr16& s);
  Object* ldcString(Class* c, uint16_t idx);
  Object* multiArray(Thread& t, Class* c, const int32_t* dims, int nd);
  Object* newMultiArray(Thread& t, Class* c, const Slot* dims, int nd);  // multianewarray (checks the sizes)
  void oom(Thread& t);
  void markFrom(Object* o);
  void markMaybe(const void* p);
  size_t objSize(Object* o);
  Object* makeThrowable(Thread& t, const char* cls, const std::string& msg);
  void fillTrace(Thread& t, Object* ex);
  void printTrace(Thread& t, Object* ex, const std::string& head);
  void run(Thread& t, int untilDepth, uint64_t deadline);
  bool pushFrame(Thread& t, Method* m, Slot* args, bool soft);
  Thread* makeThread(Object* jthread, bool host);
  Object* makeJThread(Thread& t, const std::string& name);
  void freeThread(Thread* t);
  void finishThread(Thread& t);
  Thread* threadOf(Object* jthread);
  Monitor* findMonitor(Object* o);
  void wakeUp(uint64_t now);
  int schedule(Thread* prefer, uint64_t deadline, uint64_t* nextWake);
  void runSlice(Thread& t, uint64_t deadline);
  void blockUntilRunnable(Thread& t);
  void output(int fd, const std::string& s);
  void registerBuiltins();
};

}  // namespace j2me
