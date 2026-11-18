// Green threads (docs/j2me.md "Threads"): all Java threads share the caller's OS thread. step() gives ready threads
// time slices; sleep/wait/monitor contention park a thread; nested call()s from natives run Java to completion on
// one thread (other threads only run while that one is blocked). Monitors, wait/notify, exceptions, stack traces.
#if defined(ESP_PLATFORM) && !defined(J2ME_NO_OS)
#pragma GCC optimize("Os")  // cold code: small on the board (flash is short); the interpreter stays -O2
#endif
#include <algorithm>
#include <new>

#include "vm_internal.h"

#ifndef ESP_PLATFORM
#include <unistd.h>
#else
#include <unistd.h>
#endif

namespace j2me {

static const uint32_t kSliceUs = 4000;

static void sleepFor(Vm& vm, uint32_t us, const std::function<void(uint32_t)>& hook) {
  (void)vm;
  if (us > 100000) us = 100000;
  if (hook)
    hook(us);
  else
    usleep(us);
}

// ---- threads ----

Thread* Vm::makeThread(Object* jthread, bool host) {
  Thread* t = new (vmMalloc(sizeof(Thread))) Thread();
  t->id = nextThreadId++;
  t->host = host;
  t->jthread = jthread;
  size_t n = (size_t)std::max(stackSlots, 1024);
  t->maxDepth = std::max(64, (int)(n / 4));
  t->stack = static_cast<Slot*>(vmMalloc(n * sizeof(Slot)));
  t->frames = static_cast<Frame*>(vmMalloc(sizeof(Frame) * (size_t)t->maxDepth));
  if (!t->stack || !t->frames) {
    log("j2me: no memory for a thread's stack");
    freeThread(t);
    return nullptr;
  }
  std::memset(t->stack, 0, n * sizeof(Slot));
  t->stackEnd = t->stack + n;
  t->stackLimit = t->stackEnd - kStackReserve;
  t->depthLimit = t->maxDepth - kFrameReserve;
  threads.push_back(t);
  return t;
}

void Vm::freeThread(Thread* t) {
  vmFree(t->stack);
  vmFree(t->frames);
  t->~Thread();
  vmFree(t);
}

Object* Vm::makeJThread(Thread& t, const std::string& name) {
  Scope sc(*this);
  Object* o = keep(newObject(cThread));
  if (!o) return nullptr;
  o->slots()[fThreadVm].i = t.id;
  t.jthread = o;
  Object* s = newString(name);
  Method* init = cThread->findMethod(std::string("<init>"), std::string("(Ljava/lang/String;)V"));
  if (init && s) {
    Slot a[2];
    a[0].a = o;
    a[1].a = s;
    call(t, init, a);
    t.pending = nullptr;
  }
  return o;
}

Thread* Vm::startThread(Object* jthread) {
  Method* run = findVirtual(jthread->cls, "run", "()V");
  if (!run || run->isNative() || run->isAbstract()) return nullptr;
  Thread* t = makeThread(jthread, false);
  if (!t) {
    if (cur) oom(*cur);
    return nullptr;
  }
  jthread->slots()[fThreadVm].i = t->id;
  t->stack[0].a = jthread;
  pushFrame(*t, run, t->stack, false);
  t->state = TState::Ready;
  return t;
}

Thread* Vm::newThread(const std::string& name) {
  if (!boot()) return nullptr;
  Thread* t = makeThread(nullptr, true);
  if (!t) return nullptr;
  Thread* saved = cur;
  makeJThread(*t, name);
  cur = saved;
  return t;
}

Thread* Vm::threadOf(Object* jthread) {
  if (!jthread) return nullptr;
  int id = jthread->slots()[fThreadVm].i;
  for (Thread* t : threads)
    if (t->id == id) return t;
  return nullptr;
}

void Vm::finishThread(Thread& t) {
  if (t.pending) {
    Object* ex = t.pending;
    t.pending = nullptr;
    uncaught++;
    if (&t == mainThread) mainUncaught = true;
    std::string name = "Thread-" + std::to_string(t.id);
    if (t.jthread && t.jthread->slots()[fThreadName].a) name = str(t.jthread->slots()[fThreadName].a);
    t.depth = 0;
    printTrace(t, ex, "Exception in thread \"" + name + "\" ");
  }
  t.depth = 0;
  t.state = TState::Done;
  if (t.jthread) notify(t.jthread, true);  // join() waits on the Thread object
}

bool Vm::pushFrame(Thread& t, Method* m, Slot* args, bool soft) {
  Slot* nsp = args + m->maxLocals;
  Slot* limit = soft ? t.stackLimit : t.stackEnd;
  int dlimit = soft ? t.depthLimit : t.maxDepth;
  if (nsp + m->maxStack > limit || t.depth >= dlimit) return false;
  for (Slot* s = args + m->argSlots; s < nsp; s++) s->j = 0;
  Frame& f = t.frames[t.depth++];
  f.m = m;
  f.pc = 0;
  f.locals = args;
  f.sp = nsp;
  f.sync = nullptr;
  f.skip = 0;
  return true;
}

// ---- monitors ----

Vm::Monitor* Vm::findMonitor(Object* o) {
  for (size_t i = monitors.size(); i-- > 0;)
    if (monitors[i].obj == o) return &monitors[i];
  return nullptr;
}

bool Vm::enter(Thread& t, Object* o) {
  Monitor* m = findMonitor(o);
  if (!m) {
    monitors.push_back(Monitor{o, &t, 1});
    return true;
  }
  if (m->owner == &t) {
    m->count++;
    return true;
  }
  return false;
}

bool Vm::exit(Thread& t, Object* o) {
  Monitor* m = findMonitor(o);
  if (!m || m->owner != &t) return false;
  if (--m->count == 0) {
    *m = monitors.back();
    monitors.pop_back();
  }
  return true;
}

void Vm::wait(Thread& t, Object* o, int64_t ms) {
  Monitor* m = findMonitor(o);
  if (!m || m->owner != &t) {
    throwNew(t, "java/lang/IllegalMonitorStateException", "wait() without owning the monitor");
    return;
  }
  if (ms < 0) {
    throwNew(t, "java/lang/IllegalArgumentException", "timeout value is negative");
    return;
  }
  if (t.interrupted) {
    t.interrupted = false;
    throwNew(t, "java/lang/InterruptedException", "");
    return;
  }
  t.waitCount = m->count;
  *m = monitors.back();
  monitors.pop_back();
  t.waitOn = o;
  t.state = TState::Waiting;
  t.wakeAt = ms > 0 ? nowUs() + (uint64_t)(ms < 1000000000000LL ? ms : 1000000000000LL) * 1000u : 0;
}

void Vm::notify(Object* o, bool all) {
  for (Thread* x : threads)
    if (x->state == TState::Waiting && x->waitOn == o) {
      x->state = TState::Blocked;  // takes the monitor back before it goes on
      if (!all) return;
    }
}

// ---- scheduling ----

void Vm::wakeUp(uint64_t now) {
  for (Thread* x : threads) {
    switch (x->state) {
      case TState::Sleeping:
        if (now >= x->wakeAt) x->state = TState::Ready;
        break;
      case TState::Waiting:
        if (!x->wakeAt || now < x->wakeAt) break;
        x->state = TState::Blocked;  // timed out: take the monitor back (below)
        [[fallthrough]];
      case TState::Blocked:
        if (x->waitClass) {
          if (x->waitClass->state != CLS_INITING) {
            x->waitClass = nullptr;
            x->state = TState::Ready;
          }
        } else if (x->waitCount > 0) {
          if (!findMonitor(x->waitOn)) {
            monitors.push_back(Monitor{x->waitOn, x, x->waitCount});
            x->waitCount = 0;
            x->waitOn = nullptr;
            x->state = TState::Ready;
          }
        } else {
          Monitor* m = findMonitor(x->waitOn);
          if (!m || m->owner == x) {
            x->waitOn = nullptr;
            x->state = TState::Ready;  // runs monitorenter (or the synchronized call) again
          }
        }
        break;
      default:
        break;
    }
  }
}

void Vm::runSlice(Thread& x, uint64_t deadline) {
  int until = x.nestLevel > 0 ? x.nestBase : 0;
  Thread* saved = cur;
  run(x, until, deadline);
  cur = saved;
  if (x.depth <= until && !exited) {
    if (x.nestLevel > 0)
      x.nestDone = true;
    else
      finishThread(x);
  }
}

int Vm::schedule(Thread* exclude, uint64_t deadline, uint64_t* nextWake) {
  wakeUp(nowUs());
  size_t n = threads.size();
  for (size_t k = 0; k < n; k++) {
    size_t i = (rr + k) % n;
    Thread* x = threads[i];
    if (x == exclude || x->state != TState::Ready || x->nestDone || x->inNative) continue;
    if (x->depth <= (x->nestLevel > 0 ? x->nestBase : 0)) continue;
    rr = i + 1;
    runSlice(*x, deadline);
    return 1;
  }
  uint64_t next = UINT64_MAX;
  for (Thread* x : threads)
    if ((x->state == TState::Sleeping || (x->state == TState::Waiting && x->wakeAt)) && x->wakeAt < next) next = x->wakeAt;
  if (nextWake) *nextWake = next;
  return 0;
}

void Vm::blockUntilRunnable(Thread& t) {
  while (!exited) {
    wakeUp(nowUs());
    if (t.state == TState::Ready || t.nestDone) return;
    uint64_t next = UINT64_MAX;
    if (schedule(&t, nowUs() + kSliceUs, &next)) continue;
    if (t.state == TState::Ready) return;
    if (next == UINT64_MAX) {
      // nothing can ever wake it: throw into it rather than hang the host
      log("j2me: thread " + std::to_string(t.id) + " is blocked in a nested call and nothing can wake it (deadlock)");
      if (t.waitCount > 0 && !findMonitor(t.waitOn)) monitors.push_back(Monitor{t.waitOn, &t, t.waitCount});
      t.waitCount = 0;
      t.waitOn = nullptr;
      t.waitClass = nullptr;
      t.wakeAt = 0;
      t.state = TState::Ready;
      Object* e = makeThrowable(t, "java/lang/Error", "deadlock");
      t.pending = e;
      return;
    }
    uint64_t now = nowUs();
    if (next > now) sleepFor(*this, (uint32_t)std::min<uint64_t>(next - now, 100000), idleSleep);
  }
}

Slot Vm::call(Thread& t, Method* m, const Slot* args) {
  Slot r;
  r.j = 0;
  if (exited || !m) return r;
  if (t.pending) {  // the caller did not handle an earlier exception: say so rather than skip this call
    Object* stale = t.pending;
    t.pending = nullptr;
    log("j2me: call() on a thread with an exception pending (dropped): " + className(stale->cls));
  }
  Thread* saved = cur;
  cur = &t;
  if (m->isStatic() && m->owner->state != CLS_READY && !initClass(t, m->owner)) {
    cur = saved;
    return r;
  }
  if (m->isAbstract()) {
    throwNew(t, "java/lang/AbstractMethodError", className(m->owner) + "." + m->name.str());
    cur = saved;
    return r;
  }
  TState sState = t.state;
  uint64_t sWake = t.wakeAt;
  Object* sWaitOn = t.waitOn;
  Class* sWaitClass = t.waitClass;
  int sWaitCount = t.waitCount;
  t.state = TState::Ready;
  t.waitOn = nullptr;
  t.waitClass = nullptr;
  t.waitCount = 0;
  Object* lock = nullptr;
  if (m->isSync()) {
    lock = m->isStatic() ? mirror(m->owner) : args[0].a;
    while (lock && !enter(t, lock) && !exited) {
      t.state = TState::Blocked;
      t.waitOn = lock;
      blockUntilRunnable(t);
      if (t.pending) {
        lock = nullptr;
        break;
      }
    }
  }
  if (!t.pending && !exited) {
    if (m->isNative()) {
      if (!m->native) bindNative(m);
      if (!m->native) {
        throwNew(t, "java/lang/Error", "native method missing: " + m->owner->name.str() + "." + m->name.str() + m->desc.str());
      } else {
        Slot small[8];
        int n = m->argCount;
        Slot* a = n > 8 ? static_cast<Slot*>(vmMalloc(sizeof(Slot) * (size_t)n)) : small;
        if (!a) {
          oom(t);
        } else {
          for (int i = 0; i < n; i++) a[i] = args[i];
          size_t mark = locals.size();
          bool wasNative = t.inNative;
          t.inNative = true;
          m->native(*this, t, a, r);
          t.inNative = wasNative;
          locals.resize(mark);
          if (a != small) vmFree(a);
        }
      }
    } else {
      Slot* p = t.depth ? t.frames[t.depth - 1].sp : t.stack;
      if (!pushFrame(t, m, p, false)) {  // (the frame's room covers the arguments: maxLocals >= argSlots)
        t.pending = soError;
      } else {
        // the arguments: one Slot each in args; long/double take two slots on the Java stack
        Slot* q = p;
        int k = 0;
        if (!m->isStatic()) *q++ = args[k++];
        for (const char* d = m->desc.p + 1; *d && *d != ')';) {
          char c = *d;
          *q = args[k++];
          q += (c == 'J' || c == 'D') ? 2 : 1;
          while (*d == '[') d++;
          if (*d == 'L')
            while (*d && *d != ';') d++;
          if (*d) d++;
        }
        int base = t.depth - 1;
        int sBase = t.nestBase;
        bool sDone = t.nestDone, sNative = t.inNative;
        t.nestBase = base;
        t.nestLevel++;
        t.nestDone = false;
        t.inNative = false;
        for (;;) {
          if (t.depth <= base || exited) break;
          if (t.state == TState::Ready) run(t, base, 0);
          if (t.depth <= base || exited) break;
          blockUntilRunnable(t);
        }
        t.nestLevel--;
        t.nestBase = sBase;
        t.nestDone = sDone;
        t.inNative = sNative;
        if (!t.pending) r = t.ret;
      }
    }
  }
  if (lock) exit(t, lock);
  t.state = sState;
  t.wakeAt = sWake;
  t.waitOn = sWaitOn;
  t.waitClass = sWaitClass;
  t.waitCount = sWaitCount;
  if ((m->ret == 'L' || m->ret == '[') && !t.pending) keep(r.a);
  cur = saved;
  return r;
}

bool Vm::step(uint32_t budgetUs) {
  if (!boot()) return false;
  if (exited) {  // System.exit or stop(): every thread ends here
    for (Thread* x : threads) {
      x->state = TState::Done;
      x->depth = 0;
    }
    idleUs = 0;
    cur = nullptr;
    return false;
  }
  locals.clear();
  for (size_t i = 0; i < threads.size();) {
    Thread* x = threads[i];
    if (x->state == TState::Done && !x->host && x->nestLevel == 0 && x != bootThread) {
      if (x == mainThread) mainThread = nullptr;
      freeThread(x);
      threads.erase(threads.begin() + (long)i);
    } else {
      i++;
    }
  }
  uint64_t deadline = nowUs() + budgetUs;
  for (;;) {
    uint64_t next = UINT64_MAX;
    uint64_t now = nowUs();
    int r = schedule(nullptr, std::min(deadline, now + kSliceUs), &next);
    if (exited) {
      cur = nullptr;
      idleUs = 0;
      return false;
    }
    now = nowUs();
    if (r == 0) {
      bool alive = false;
      for (Thread* x : threads)
        if (!x->host && x->state != TState::Done) alive = true;
      cur = nullptr;
      if (!alive) {
        idleUs = 0;
        return false;
      }
      if (next == UINT64_MAX) {
        idleUs = kForever;
      } else if (next <= now) {
        if (now < deadline) continue;
        idleUs = 0;
      } else {
        idleUs = (uint32_t)std::min<uint64_t>(next - now, kForever - 1);
      }
      return true;
    }
    if (now >= deadline) {
      cur = nullptr;
      idleUs = 0;
      return true;
    }
  }
}

bool Vm::runMain(const std::string& cls, const std::vector<std::string>& args) {
  if (!startMain(cls, args)) return false;
  while (step(100000)) {
    if (idleUs == kForever) {
      log("j2me: every thread waits and nothing can wake them (deadlock)");
      log(threadDump());
      deadlocked = true;
      break;
    }
    if (idleUs) sleepFor(*this, idleUs, idleSleep);
  }
  return true;
}

bool Vm::startMain(const std::string& cls, const std::vector<std::string>& args) {
  if (!boot()) return false;
  cur = nullptr;
  Class* c = loadClass(cls);
  if (!c) {
    log("j2me: class " + cls + " not found");
    return false;
  }
  Method* m = c->findMethod(std::string("main"), std::string("([Ljava/lang/String;)V"));
  if (!m || !m->isStatic()) {
    log("j2me: " + cls + " has no static main(String[])");
    return false;
  }
  Thread* t = makeThread(nullptr, false);
  if (!t) return false;
  mainThread = t;
  makeJThread(*t, "main");
  cur = t;
  Object* arr = newArray('L', cString, (int32_t)args.size());
  for (size_t i = 0; arr && i < args.size(); i++) arr->elems<Object*>()[i] = newString(args[i]);
  if (arr && initClass(*t, c)) {
    t->stack[0].a = arr;
    pushFrame(*t, m, t->stack, false);
  }
  if (t->pending || t->depth == 0) finishThread(*t);
  locals.clear();
  cur = nullptr;
  return true;
}

// ---- exceptions ----

Object* Vm::makeThrowable(Thread& t, const char* cls, const std::string& msg) {
  if (throwDepth > 3) return oomError;
  throwDepth++;
  Thread* saved = cur;
  cur = &t;
  t.pending = nullptr;
  Scope sc(*this);
  Object* o = nullptr;
  Class* c = loadClass(cls);
  if (c && initClass(t, c)) {
    o = keep(newObject(c));
    if (o) {
      Object* s = msg.empty() ? nullptr : newString(msg);
      Method* init = s ? c->findMethod(std::string("<init>"), std::string("(Ljava/lang/String;)V")) : nullptr;
      if (init) {
        Slot a[2];
        a[0].a = o;
        a[1].a = s;
        call(t, init, a);
      } else if ((init = c->findMethod(sInit, sVoidDesc))) {
        Slot a[1];
        a[0].a = o;
        call(t, init, a);
        if (s) o->slots()[fThrowMsg].a = s;
      }
    }
  }
  if (t.pending) {
    o = t.pending;
    t.pending = nullptr;
  }
  if (!o) o = oomError;
  cur = saved;
  throwDepth--;
  return o;
}

void Vm::throwNew(Thread& t, const char* cls, const std::string& msg) {
  Object* o = makeThrowable(t, cls, msg);
  t.pending = o;
}

void Vm::fillTrace(Thread& t, Object* ex) {
  int d = t.depth - 1;
  while (d >= 0 && t.frames[d].m->name == sInit && assignable(cThrowable, t.frames[d].m->owner)) d--;
  int n = std::min(d + 1, 64);
  Object* saved = t.pending;
  Object* arr = newArrayRaw(primArrayClass('I'), n * 2);
  t.pending = saved;
  if (!arr) return;
  int32_t* e = arr->elems<int32_t>();
  for (int i = 0; i < n; i++) {
    Frame& f = t.frames[d - i];
    e[2 * i] = (int32_t)f.m->id;
    e[2 * i + 1] = (int32_t)f.pc;
  }
  ex->slots()[fThrowTrace].a = arr;
}

static std::string frameText(Method* m, uint32_t pc) {
  std::string s = javaName(m->owner->name) + "." + m->name.str() + "(";
  int line = m->line(pc);
  if (m->isNative())
    s += "Native Method";
  else if (line >= 0)
    s += std::string(m->owner->sourceFile.ok() ? m->owner->sourceFile.c_str() : "Unknown Source") + ":" + std::to_string(line);
  else
    s += "pc " + std::to_string(pc);
  return s + ")";
}

void Vm::printTrace(Thread& t, Object* ex, const std::string& head) {
  Scope sc(*this);
  keep(ex);  // toString() runs Java: ex may be nowhere else
  Object* saved = t.pending;
  t.pending = nullptr;
  std::string s;
  if (Method* ts = findVirtual(ex->cls, "toString", "()Ljava/lang/String;")) {
    Slot a;
    a.a = ex;
    Slot r = call(t, ts, &a);
    if (!t.pending) s = str(r.a);
    t.pending = nullptr;
  }
  if (s.empty()) s = className(ex->cls);
  std::string text = head + s + "\n";
  Object* tr = ex->slots()[fThrowTrace].a;
  if (tr && tr->cls == primArrayClass('I')) {
    int32_t* e = tr->elems<int32_t>();
    for (int32_t i = 0; i + 1 < tr->length; i += 2)
      if ((uint32_t)e[i] < allMethods.size()) text += "\tat " + frameText(allMethods[(size_t)e[i]], (uint32_t)e[i + 1]) + "\n";
  }
  output(2, text);
  t.pending = saved;
}

std::string Vm::threadDump() {
  static const char* names[] = {"ready", "sleeping", "blocked", "waiting", "done"};
  std::string s;
  for (Thread* t : threads) {
    std::string name = "Thread-" + std::to_string(t->id);
    if (t->jthread && t->jthread->slots()[fThreadName].a) name = str(t->jthread->slots()[fThreadName].a);
    s += "thread " + std::to_string(t->id) + " \"" + name + "\" " + names[(int)t->state];
    if (t->host) s += " (host)";
    if (t->waitOn) s += " on " + className(t->waitOn->cls) + "@" + std::to_string(identityHash(t->waitOn));
    if (t->waitClass) s += " for the initialization of " + className(t->waitClass);
    if (t->wakeAt && (t->state == TState::Sleeping || t->state == TState::Waiting))
      s += " wakes in " + std::to_string((long long)(t->wakeAt - nowUs()) / 1000) + " ms";
    s += "\n" + stackTrace(*t);
  }
  for (Monitor& m : monitors)
    s += "monitor " + className(m.obj->cls) + "@" + std::to_string(identityHash(m.obj)) + " held by thread " +
         std::to_string(m.owner->id) + " x" + std::to_string(m.count) + "\n";
  return s;
}

std::string Vm::stackTrace(Thread& t) {
  std::string s;
  for (int d = t.depth - 1; d >= 0; d--) s += "\tat " + frameText(t.frames[d].m, t.frames[d].pc) + "\n";
  return s;
}

}  // namespace j2me
