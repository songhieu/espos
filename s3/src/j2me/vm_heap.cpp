// The Java heap: allocation, strings, and a mark-sweep collector (docs/j2me.md "Memory").
// Objects are malloc'd one by one (PSRAM on the board) and listed in Vm::heap. Roots: every thread's Java stack,
// scanned conservatively (a slot counts when it equals the address of a live object: binary search in the heap list,
// sorted at each collection), plus the exact roots (statics, mirrors, interned strings, monitors, pending exceptions,
// addRoot pointers, and the objects handed to C++ in Vm::locals). Objects never move.
#include <algorithm>

#include "vm_internal.h"

namespace j2me {

size_t Vm::objSize(Object* o) {
  Class* c = o->cls;
  if (c->isArray) return sizeof(Object) + (size_t)o->length * (size_t)elemSize(c->elem);
  return sizeof(Object) + (size_t)c->instanceSlots * sizeof(Slot);
}

void Vm::oom(Thread& t) {
  t.pending = oomError;
  if (!oomError) log("j2me: out of memory while starting");
}

Object* Vm::allocRaw(size_t bytes, Class* c) {
  if (liveBytes + bytes > nextGc || (gcStress && ++allocCount % gcStress == 0)) gc();
  if (liveBytes + bytes > heapLimit) {
    if (cur) oom(*cur);
    return nullptr;
  }
  void* p = vmMalloc(bytes);
  if (!p) {
    gc();
    p = vmMalloc(bytes);
    if (!p) {
      if (cur) oom(*cur);
      return nullptr;
    }
  }
  std::memset(p, 0, bytes);
  Object* o = static_cast<Object*>(p);
  o->cls = c;
  heap.push_back(o);
  liveBytes += bytes;
  return o;
}

Object* Vm::newObject(Class* c) { return allocRaw(sizeof(Object) + (size_t)c->instanceSlots * sizeof(Slot), c); }

Object* Vm::newArrayRaw(Class* ac, int32_t n) {
  if (!ac || n < 0) return nullptr;
  size_t es = (size_t)elemSize(ac->elem);
  if ((size_t)n > (heapLimit / es) + 1) {
    if (cur) oom(*cur);
    return nullptr;
  }
  Object* o = allocRaw(sizeof(Object) + (size_t)n * es, ac);
  if (o) o->length = n;
  return o;
}

Object* Vm::alloc(Class* c) {
  if (!c) return nullptr;
  if (c->isArray || c->isInterface() || c->isAbstract()) {
    if (cur) throwNew(*cur, "java/lang/InstantiationException", className(c));
    return nullptr;
  }
  return keep(newObject(c));
}

Object* Vm::newArray(char elem, Class* elemClass, int32_t n) {
  if (n < 0) {
    if (cur) throwNew(*cur, "java/lang/NegativeArraySizeException", std::to_string(n));
    return nullptr;
  }
  Class* ac = (elem == 'L' || elem == '[') ? (elemClass ? arrayOf(elemClass) : nullptr) : primArrayClass(elem);
  if (!ac) return nullptr;
  return keep(newArrayRaw(ac, n));
}

Object* Vm::multiArray(Thread& t, Class* c, const int32_t* dims, int nd) {
  (void)t;
  Object* a = newArrayRaw(c, dims[0]);
  if (!a || nd <= 1 || c->elem != 'L' || !c->elemClass || !c->elemClass->isArray) return a;
  Scope s(*this);
  keep(a);
  for (int32_t i = 0; i < dims[0]; i++) {
    Object* sub = multiArray(t, c->elemClass, dims + 1, nd - 1);
    if (!sub) return nullptr;
    a->elems<Object*>()[i] = sub;
  }
  return a;
}

Object* Vm::newMultiArray(Thread& t, Class* c, const Slot* dims, int nd) {
  int32_t d[256];
  for (int i = 0; i < nd; i++) {
    d[i] = dims[i].i;
    if (d[i] < 0) {
      throwNew(t, "java/lang/NegativeArraySizeException", std::to_string(d[i]));
      return nullptr;
    }
  }
  return multiArray(t, c, d, nd);
}

// ---- strings ----

Object* Vm::string16Raw(const char16_t* s, int n) {
  Object* arr = newArrayRaw(primArrayClass('C'), n);
  if (!arr) return nullptr;
  if (n) std::memcpy(arr->data(), s, (size_t)n * 2);
  locals.push_back(arr);
  Object* o = newObject(cString);
  locals.pop_back();
  if (!o) return nullptr;
  o->slots()[fStrValue].a = arr;
  o->slots()[fStrOffset].i = 0;
  o->slots()[fStrCount].i = n;
  return o;
}

Object* Vm::newString16(const char16_t* s, int n) { return keep(string16Raw(s, n)); }

Object* Vm::newString(const std::string& utf8) {
  PStr16 u;
  utf8To16(utf8.data(), utf8.size(), u);
  return keep(string16Raw(u.data(), (int)u.size()));
}

Object* Vm::internRaw(const PStr16& s) {
  auto it = interned.find(s);
  if (it != interned.end()) return it->second;
  Object* o = string16Raw(s.data(), (int)s.size());
  if (o) interned[s] = o;
  return o;
}

Object* Vm::intern(const std::string& utf8) {
  PStr16 u;
  utf8To16(utf8.data(), utf8.size(), u);
  return internRaw(u);
}

Object* Vm::ldcString(Class* c, uint16_t idx) {
  CpEntry& e = c->cp[idx];
  if (e.resolved) return static_cast<Object*>(e.resolved);
  CpEntry& u = c->cp[e.a];
  PStr16 s;
  utf8To16(reinterpret_cast<const char*>(c->bytes.data()) + u.a, u.b, s);
  Object* o = internRaw(s);
  if (o) e.resolved = o;
  return o;
}

int Vm::strChars(Object* s, const char16_t** chars) {
  *chars = nullptr;
  if (!s) return -1;
  Object* arr = s->slots()[fStrValue].a;
  int off = s->slots()[fStrOffset].i, n = s->slots()[fStrCount].i;
  if (!arr || off < 0 || n < 0 || off + n > arr->length) return 0;
  *chars = arr->elems<char16_t>() + off;
  return n;
}

std::string Vm::str(Object* s) {
  if (!s) return std::string();
  Object* arr = s->slots()[fStrValue].a;
  if (!arr) return std::string();
  int off = s->slots()[fStrOffset].i, n = s->slots()[fStrCount].i;
  if (off < 0 || n < 0 || off + n > arr->length) return std::string();
  return utf16To8(arr->elems<char16_t>() + off, (size_t)n);
}

// ---- the collector ----

void Vm::markFrom(Object* o) {
  if (!o || o->mark) return;
  o->mark = 1;
  markStack.push_back(o);
}

void Vm::markMaybe(const void* p) {
  if (!p || heap.empty()) return;
  Object* o = static_cast<Object*>(const_cast<void*>(p));
  if (o < heap.front() || o > heap.back()) return;
  auto it = std::lower_bound(heap.begin(), heap.end(), o);
  if (it != heap.end() && *it == o) markFrom(o);
}

void Vm::gc() {
  gcCount++;
  std::sort(heap.begin(), heap.end());
  markStack.clear();
  for (Thread* t : threads) {
    if (t->stack) {
      Slot* top = t->depth ? t->frames[t->depth - 1].sp : t->stack;
      // a call() being set up writes its arguments above the top frame: cover the frame's whole operand stack
      if (t->depth) {
        Frame& f = t->frames[t->depth - 1];
        Slot* lim = f.locals + f.m->maxLocals + f.m->maxStack;
        if (lim > top && lim <= t->stackEnd) top = lim;
      }
      for (Slot* s = t->stack; s < top; s++) markMaybe(s->a);
      for (int i = 0; i < t->depth; i++) markFrom(t->frames[i].sync);
    }
    markFrom(t->jthread);
    markFrom(t->pending);
    markFrom(t->waitOn);
    markMaybe(t->ret.a);
  }
  for (Object* o : locals) markFrom(o);
  for (Object** r : roots)
    if (r) markFrom(*r);
  for (auto& kv : classes) {
    Class* c = kv.second;
    markFrom(c->mirror);
    for (uint16_t s : c->staticRefs) markFrom(c->statics[s].a);
  }
  for (auto& kv : interned) markFrom(kv.second);
  for (Monitor& m : monitors) markFrom(m.obj);
  markFrom(oomError);
  markFrom(soError);
  while (!markStack.empty()) {
    Object* o = markStack.back();
    markStack.pop_back();
    Class* c = o->cls;
    if (c->isArray) {
      if (c->elem == 'L') {
        Object** e = o->elems<Object*>();
        for (int32_t i = 0; i < o->length; i++) markFrom(e[i]);
      }
    } else {
      Slot* s = o->slots();
      for (uint16_t k : c->refSlots) markFrom(s[k].a);
    }
  }
  size_t w = 0;
  for (size_t i = 0; i < heap.size(); i++) {
    Object* o = heap[i];
    if (o->mark) {
      o->mark = 0;
      heap[w++] = o;
    } else {
      liveBytes -= objSize(o);
      vmFree(o);
    }
  }
  heap.resize(w);
  if (markStack.capacity() > 4096) PVec<Object*>().swap(markStack);
  size_t grow = std::max(liveBytes, heapLimit / 4);
  nextGc = std::min(heapLimit, liveBytes + grow);
}

}  // namespace j2me
