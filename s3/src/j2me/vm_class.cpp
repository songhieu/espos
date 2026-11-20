#pragma GCC optimize("Os")  // cold code: built small (0.5.7 flash diet; the app slot is full)
// Classes: the class file parser, linking (field layout, vtables), resolution of constant pool references, array
// classes, java.lang.Class mirrors, and static initialization (docs/j2me.md "Classes").
#if defined(ESP_PLATFORM) && !defined(J2ME_NO_OS)
#pragma GCC optimize("Os")  // cold code: small on the board (flash is short); the interpreter stays -O2
#endif
#include <algorithm>
#include <new>

#include "vm_internal.h"

namespace j2me {

static int loadDepth = 0;

bool Vm::boot() {
  if (booted) return true;
  if (booting) return false;
  booting = true;
  sInit = syms.intern("<init>");
  sClinit = syms.intern("<clinit>");
  sVoidDesc = syms.intern("()V");
  cObject = loadClass("java/lang/Object");
  cClass = cObject ? loadClass("java/lang/Class") : nullptr;
  cString = cClass ? loadClass("java/lang/String") : nullptr;
  cThread = cString ? loadClass("java/lang/Thread") : nullptr;
  cThrowable = cThread ? loadClass("java/lang/Throwable") : nullptr;
  if (!cThrowable) {
    log("j2me: the class library is missing (java/lang/Object, Class, String, Thread, Throwable)");
    booting = false;
    return false;
  }
  struct {
    Class* c;
    const char *n, *d;
    int* slot;
  } want[] = {
      {cString, "value", "[C", &fStrValue},        {cString, "offset", "I", &fStrOffset},
      {cString, "count", "I", &fStrCount},         {cClass, "vmClass", "J", &fClassVm},
      {cThread, "vmThread", "I", &fThreadVm},      {cThread, "name", "Ljava/lang/String;", &fThreadName},
      {cThrowable, "trace", "Ljava/lang/Object;", &fThrowTrace}, {cThrowable, "message", "Ljava/lang/String;", &fThrowMsg},
  };
  for (auto& w : want) {
    Field* f = w.c->findField(std::string(w.n), std::string(w.d));
    if (!f || f->isStatic) {
      log(std::string("j2me: the class library lacks ") + w.c->name.c_str() + "." + w.n + " " + w.d);
      booting = false;
      return false;
    }
    *w.slot = f->slot;
  }
  booted = true;
  bootThread = makeThread(nullptr, true);
  if (!bootThread) {
    booted = false;
    booting = false;
    return false;
  }
  Thread* saved = cur;
  cur = bootThread;
  initClass(*bootThread, cObject);
  initClass(*bootThread, cString);
  bootThread->pending = nullptr;
  oomError = makeThrowable(*bootThread, "java/lang/OutOfMemoryError", "");
  soError = makeThrowable(*bootThread, "java/lang/StackOverflowError", "");
  bootThread->pending = nullptr;
  cur = saved;
  booting = false;
  locals.clear();
  return true;
}

Class* Vm::findLoaded(Sym name) {
  auto it = classes.find(name.p);
  return it == classes.end() ? nullptr : it->second;
}

Class* Vm::loadClass(const std::string& name0) {
  if (!booted && !booting && !boot()) return nullptr;
  std::string name = name0;
  for (char& c : name)
    if (c == '.') c = '/';
  if (name.empty()) return nullptr;
  Sym s = syms.intern(name.c_str(), name.size());
  if (Class* c = findLoaded(s)) return c;
  if (name[0] == '[') return arrayClassNamed(s);
  PVec<uint8_t> bytes;
  if (!resource(name + ".class", bytes)) {
    if (cur && throwDepth < 3) throwNew(*cur, "java/lang/NoClassDefFoundError", javaName(s));
    return nullptr;
  }
  return defineClass(s, bytes);
}

Sym Vm::utf8(Class* c, uint16_t idx) {
  if (idx == 0 || idx >= c->cp.size()) return Sym();
  CpEntry& e = c->cp[idx];
  if (e.tag != CP_Utf8) return Sym();
  if (!e.resolved) e.resolved = const_cast<char*>(syms.intern(reinterpret_cast<const char*>(c->bytes.data()) + e.a, e.b).p);
  return Sym(static_cast<const char*>(e.resolved));
}

namespace {
struct ClassFile {
  Sym superName;
  PVec<Sym> ifaces;
};
}  // namespace

static bool parse(Vm* vm, Class* c, ClassFile& cf, Sym (Vm::*utf8)(Class*, uint16_t)) {
  const uint8_t* b = c->bytes.data();
  size_t n = c->bytes.size(), p = 0;
  auto need = [&](size_t k) { return p + k <= n; };
  auto U = [&](uint16_t i) { return (vm->*utf8)(c, i); };
  auto classAt = [&](uint16_t i) -> Sym {
    if (i == 0 || i >= c->cp.size() || c->cp[i].tag != CP_Class) return Sym();
    return U((uint16_t)c->cp[i].a);
  };
  if (!need(10) || u4(b) != 0xCAFEBABEu) return false;
  uint16_t cpn = u2(b + 8);
  p = 10;
  c->cp.resize(cpn);
  for (uint32_t i = 1; i < cpn; i++) {
    if (!need(1)) return false;
    CpEntry& e = c->cp[i];
    e.tag = b[p++];
    switch (e.tag) {
      case CP_Utf8:
        if (!need(2)) return false;
        e.b = u2(b + p);
        e.a = (uint32_t)(p + 2);
        p += 2 + e.b;
        if (p > n) return false;
        break;
      case CP_Integer:
      case CP_Float:
        if (!need(4)) return false;
        e.a = u4(b + p);
        p += 4;
        break;
      case CP_Long:
      case CP_Double:
        if (!need(8)) return false;
        e.a = u4(b + p);
        e.b = u4(b + p + 4);
        p += 8;
        i++;
        break;
      case CP_Class:
      case CP_String:
      case CP_MethodType:
      case CP_Module:
      case CP_Package:
        if (!need(2)) return false;
        e.a = u2(b + p);
        p += 2;
        break;
      case CP_Fieldref:
      case CP_Methodref:
      case CP_IMethodref:
      case CP_NameAndType:
      case CP_InvokeDynamic:
      case CP_Dynamic:
        if (!need(4)) return false;
        e.a = u2(b + p);
        e.b = u2(b + p + 2);
        p += 4;
        break;
      case CP_MethodHandle:
        if (!need(3)) return false;
        e.a = b[p];
        e.b = u2(b + p + 1);
        p += 3;
        break;
      default:
        return false;
    }
  }
  if (!need(8)) return false;
  c->flags = u2(b + p);
  uint16_t thisIdx = u2(b + p + 2), superIdx = u2(b + p + 4), nif = u2(b + p + 6);
  p += 8;
  Sym thisName = classAt(thisIdx);
  if (thisName != c->name) {
    vm->log(std::string("j2me: ") + c->name.c_str() + ".class holds " + thisName.c_str());
    return false;
  }
  cf.superName = superIdx ? classAt(superIdx) : Sym();
  if (superIdx && !cf.superName.ok()) return false;
  for (uint16_t i = 0; i < nif; i++) {
    if (!need(2)) return false;
    Sym s = classAt(u2(b + p));
    if (!s.ok()) return false;
    cf.ifaces.push_back(s);
    p += 2;
  }
  // fields
  if (!need(2)) return false;
  uint16_t nf = u2(b + p);
  p += 2;
  c->fields.resize(nf);
  for (uint16_t i = 0; i < nf; i++) {
    if (!need(8)) return false;
    Field& f = c->fields[i];
    f.flags = u2(b + p);
    f.isStatic = (f.flags & ACC_STATIC) != 0;
    f.name = U(u2(b + p + 2));
    f.desc = U(u2(b + p + 4));
    if (!f.name.ok() || !f.desc.ok()) return false;
    uint16_t na = u2(b + p + 6);
    p += 8;
    for (uint16_t k = 0; k < na; k++) {
      if (!need(6)) return false;
      Sym an = U(u2(b + p));
      uint32_t len = u4(b + p + 2);
      p += 6;
      if (!need(len)) return false;
      if (an == "ConstantValue" && len >= 2) f.constIdx = u2(b + p);
      p += len;
    }
  }
  // methods
  if (!need(2)) return false;
  uint16_t nm = u2(b + p);
  p += 2;
  c->methods.resize(nm);
  for (uint16_t i = 0; i < nm; i++) {
    if (!need(8)) return false;
    Method& m = c->methods[i];
    m.flags = u2(b + p);
    m.name = U(u2(b + p + 2));
    m.desc = U(u2(b + p + 4));
    if (!m.name.ok() || !m.desc.ok()) return false;
    uint16_t na = u2(b + p + 6);
    p += 8;
    for (uint16_t k = 0; k < na; k++) {
      if (!need(6)) return false;
      Sym an = U(u2(b + p));
      uint32_t len = u4(b + p + 2);
      p += 6;
      if (!need(len)) return false;
      if (an == "Code" && len >= 12) {
        const uint8_t* a = b + p;
        m.maxStack = u2(a);
        m.maxLocals = u2(a + 2);
        uint32_t cl = u4(a + 4);
        if ((size_t)cl + 12 > len) return false;
        m.code = c->bytes.data() + p + 8;
        m.codeLen = cl;
        size_t q = p + 8 + cl;
        uint16_t ne = u2(b + q);
        q += 2;
        if (q + (size_t)ne * 8 + 2 > p + len) return false;
        m.ex.resize(ne);
        for (uint16_t j = 0; j < ne; j++) {
          m.ex[j].start = u2(b + q);
          m.ex[j].end = u2(b + q + 2);
          m.ex[j].handler = u2(b + q + 4);
          m.ex[j].type = u2(b + q + 6);
          q += 8;
        }
        uint16_t ca = u2(b + q);
        q += 2;
        for (uint16_t j = 0; j < ca; j++) {
          if (q + 6 > p + len) return false;
          Sym an2 = U(u2(b + q));
          uint32_t l2 = u4(b + q + 2);
          q += 6;
          if (q + l2 > p + len) return false;
          if (an2 == "LineNumberTable" && l2 >= 2) {
            m.lineCount = u2(b + q);
            m.lines = b + q + 2;
            if ((size_t)m.lineCount * 4 + 2 > l2) m.lineCount = 0;
          }
          q += l2;
        }
      }
      p += len;
    }
  }
  // class attributes
  if (!need(2)) return false;
  uint16_t na = u2(b + p);
  p += 2;
  for (uint16_t k = 0; k < na; k++) {
    if (!need(6)) return false;
    Sym an = U(u2(b + p));
    uint32_t len = u4(b + p + 2);
    p += 6;
    if (!need(len)) return false;
    if (an == "SourceFile" && len >= 2) c->sourceFile = U(u2(b + p));
    p += len;
  }
  return true;
}

static void destroyClass(Class* c) {
  c->~Class();
  vmFree(c);
}

Class* Vm::defineClass(Sym name, PVec<uint8_t>& bytes) {
  if (loadDepth > 64) {
    log(std::string("j2me: class loading goes too deep at ") + name.c_str());
    return nullptr;
  }
  Class* c = new (vmMalloc(sizeof(Class))) Class();
  c->name = name;
  c->bytes.swap(bytes);
  ClassFile cf;
  if (!parse(this, c, cf, &Vm::utf8)) {
    log(std::string("j2me: bad class file ") + name.c_str() + ".class");
    destroyClass(c);
    if (cur && throwDepth < 3) throwNew(*cur, "java/lang/NoClassDefFoundError", javaName(name) + " (bad class file)");
    return nullptr;
  }
  loadDepth++;
  bool ok = true;
  // superclass and interfaces
  if (cf.superName.ok()) {
    Class* s = findLoaded(cf.superName);
    if (!s) s = loadClass(cf.superName.str());
    if (!s || s->isInterface() || s->isArray) {
      if (s && cur) throwNew(*cur, "java/lang/IncompatibleClassChangeError", javaName(name));
      ok = false;
    }
    c->super = s;
  } else if (name != "java/lang/Object") {
    ok = false;
  }
  for (size_t i = 0; ok && i < cf.ifaces.size(); i++) {
    Class* ic = findLoaded(cf.ifaces[i]);
    if (!ic) ic = loadClass(cf.ifaces[i].str());
    if (!ic || !ic->isInterface()) {
      if (ic && cur) throwNew(*cur, "java/lang/IncompatibleClassChangeError", javaName(name));
      ok = false;
      break;
    }
    c->interfaces.push_back(ic);
  }
  loadDepth--;
  if (!ok) {
    destroyClass(c);
    return nullptr;
  }
  c->isArray = false;
  if (c->super) c->allIfaces = c->super->allIfaces;
  auto addIface = [c](Class* i) {
    if (std::find(c->allIfaces.begin(), c->allIfaces.end(), i) == c->allIfaces.end()) c->allIfaces.push_back(i);
  };
  for (Class* i : c->interfaces) {
    addIface(i);
    for (Class* j : i->allIfaces) addIface(j);
  }
  // fields: superclass slots first
  int slots = c->super ? c->super->instanceSlots : 0;
  if (c->super) c->refSlots = c->super->refSlots;
  int nstat = 0;
  for (Field& f : c->fields) {
    f.owner = c;
    if (f.isStatic) {
      f.slot = nstat++;
      if (f.isRef()) c->staticRefs.push_back((uint16_t)f.slot);
    } else {
      f.slot = slots++;
      if (f.isRef()) c->refSlots.push_back((uint16_t)f.slot);
    }
  }
  c->instanceSlots = slots;
  c->statics.resize((size_t)nstat);
  for (Slot& s : c->statics) s.j = 0;
  for (Field& f : c->fields)
    if (f.isStatic) f.addr = &c->statics[(size_t)f.slot];
  // methods
  for (Method& m : c->methods) {
    m.owner = c;
    if (!parseMethodDesc(m.desc.p, m.argSlots, m.argCount, m.ret, m.wideArgs)) {
      log(std::string("j2me: bad method descriptor ") + m.desc.c_str() + " in " + name.c_str());
      destroyClass(c);
      return nullptr;
    }
    if (!m.isStatic()) {
      m.argSlots++;
      m.argCount++;
      m.wideArgs <<= 1;
    }
    if (m.isNative()) bindNative(&m);
    if (!m.code) m.maxLocals = (uint16_t)m.argSlots;
    if (m.maxLocals < m.argSlots) m.maxLocals = (uint16_t)m.argSlots;
  }
  // the vtable: the superclass's, overrides in place, new methods appended, then interface methods nobody
  // implements (abstract classes: so invokevirtual through them finds an index)
  if (!c->isInterface()) {
    if (c->super) c->vtable = c->super->vtable;
    for (Method& m : c->methods) {
      if (m.isStatic() || m.isPrivate() || m.name == sInit || m.name == sClinit) continue;
      int found = -1;
      for (size_t i = 0; i < c->vtable.size(); i++)
        if (c->vtable[i]->name == m.name && c->vtable[i]->desc == m.desc) {
          found = (int)i;
          break;
        }
      if (found >= 0) {
        c->vtable[(size_t)found] = &m;
        m.vindex = found;
      } else {
        m.vindex = (int)c->vtable.size();
        c->vtable.push_back(&m);
      }
    }
    for (Class* i : c->allIfaces)
      for (Method& im : i->methods) {
        if (im.isStatic() || im.name == sClinit) continue;
        bool have = false;
        for (Method* v : c->vtable)
          if (v->name == im.name && v->desc == im.desc) {
            have = true;
            break;
          }
        if (!have) c->vtable.push_back(&im);
      }
  }
  for (Method& m : c->methods) {
    m.id = (uint32_t)allMethods.size();
    allMethods.push_back(&m);
  }
  c->state = CLS_LINKED;
  classes[name.p] = c;
  return c;
}

// ---- arrays ----

Class* Vm::arrayClassNamed(Sym name) {
  if (Class* c = findLoaded(name)) return c;
  const char* n = name.p;
  size_t len = std::strlen(n);
  if (len < 2 || n[0] != '[' || !cObject) return nullptr;
  char e = n[1], elem = 0;
  Class* comp = nullptr;
  if (e == 'L') {
    if (n[len - 1] != ';' || len < 4) return nullptr;
    comp = loadClass(std::string(n + 2, len - 3));
    if (!comp) return nullptr;
    elem = 'L';
  } else if (e == '[') {
    comp = arrayClassNamed(syms.intern(n + 1, len - 1));
    if (!comp) return nullptr;
    elem = 'L';
  } else if (len == 2 && std::strchr("ZBCSIJFD", e)) {
    elem = e;
  } else {
    if (cur && throwDepth < 3) throwNew(*cur, "java/lang/NoClassDefFoundError", javaName(name));
    return nullptr;
  }
  Class* a = new (vmMalloc(sizeof(Class))) Class();
  a->name = name;
  a->flags = ACC_PUBLIC | ACC_FINAL | ACC_ABSTRACT;
  a->super = cObject;
  a->isArray = true;
  a->elem = elem;
  a->elemClass = comp;
  a->vtable = cObject->vtable;
  a->state = CLS_READY;
  classes[name.p] = a;
  if (comp) comp->arrayClass = a;
  return a;
}

Class* Vm::arrayOf(Class* comp) {
  if (comp->arrayClass) return comp->arrayClass;
  std::string n = comp->isArray ? "[" + comp->name.str() : "[L" + comp->name.str() + ";";
  return arrayClassNamed(syms.intern(n.c_str(), n.size()));
}

Class* Vm::primArrayClass(char elem) {
  int k = 0;
  switch (elem) {
    case 'Z': k = 0; break;
    case 'B': k = 1; break;
    case 'C': k = 2; break;
    case 'S': k = 3; break;
    case 'I': k = 4; break;
    case 'J': k = 5; break;
    case 'F': k = 6; break;
    case 'D': k = 7; break;
    default: return nullptr;
  }
  if (!primArray[k]) {
    char n[3] = {'[', elem, 0};
    primArray[k] = arrayClassNamed(syms.intern(n, 2));
  }
  return primArray[k];
}

// ---- types ----

bool Vm::assignable(Class* to, Class* from) {
  if (to == from) return true;
  if (!to || !from) return false;
  if (from->isArray) {
    if (!to->isArray) return to == cObject;
    if (from->elem != 'L' || to->elem != 'L') return from->elem == to->elem;
    return assignable(to->elemClass, from->elemClass);
  }
  if (to->isInterface()) {
    for (Class* i : from->allIfaces)
      if (i == to) return true;
    return false;
  }
  for (Class* k = from->super; k; k = k->super)
    if (k == to) return true;
  return false;
}

bool Vm::isInstance(Object* o, Class* c) { return o && (o->cls == c || assignable(c, o->cls)); }

Object* Vm::mirror(Class* c) {
  if (c->mirror) return c->mirror;
  Object* m = newObject(cClass);
  if (!m) return nullptr;
  m->slots()[fClassVm].j = (int64_t)reinterpret_cast<intptr_t>(c);
  c->mirror = m;
  return m;
}

Class* Vm::classOf(Object* m) {
  if (!m || m->cls != cClass) return nullptr;
  return reinterpret_cast<Class*>((intptr_t)m->slots()[fClassVm].j);
}

// ---- lookup and resolution ----

Method* Vm::vtableFind(Class* c, Sym n, Sym d) {
  for (Method* v : c->vtable)
    if (v->name == n && v->desc == d) return v;
  return nullptr;
}

Method* Vm::lookupMethod(Class* c, Sym n, Sym d) {
  for (Class* k = c; k; k = k->super)
    if (Method* m = k->findMethod(n, d)) return m;
  for (Class* i : c->allIfaces)
    if (Method* m = i->findMethod(n, d)) return m;
  if (c->isInterface() && cObject)
    if (Method* m = cObject->findMethod(n, d)) return m;
  return nullptr;
}

Method* Vm::findVirtual(Class* c, const std::string& name, const std::string& desc) {
  Sym n = syms.find(name.c_str(), name.size()), d = syms.find(desc.c_str(), desc.size());
  if (!n.ok() || !d.ok() || !c) return nullptr;
  if (Method* m = vtableFind(c, n, d)) return m;
  return lookupMethod(c, n, d);
}

Method* Vm::findStatic(Class* c, const std::string& name, const std::string& desc) {
  Sym n = syms.find(name.c_str(), name.size()), d = syms.find(desc.c_str(), desc.size());
  if (!n.ok() || !d.ok()) return nullptr;
  for (Class* k = c; k; k = k->super)
    if (Method* m = k->findMethod(n, d))
      if (m->isStatic()) return m;
  return nullptr;
}

Field* Vm::findField(Class* c, const std::string& name, const std::string& desc) {
  Sym n = syms.find(name.c_str(), name.size()), d = syms.find(desc.c_str(), desc.size());
  if (!n.ok() || !d.ok()) return nullptr;
  for (Class* k = c; k; k = k->super) {
    if (Field* f = k->findField(n, d)) return f;
    for (Class* i : k->allIfaces)
      if (Field* f = i->findField(n, d)) return f;
  }
  return nullptr;
}

Class* Vm::resolveClass(Thread& t, Class* from, uint16_t idx) {
  CpEntry& e = from->cp[idx];
  if (e.resolved) return static_cast<Class*>(e.resolved);
  Sym n = utf8(from, (uint16_t)e.a);
  Class* c = findLoaded(n);
  if (!c) {
    Thread* saved = cur;
    cur = &t;
    c = loadClass(n.str());
    cur = saved;
  }
  if (c) e.resolved = c;
  return c;
}

Field* Vm::resolveField(Thread& t, Class* from, uint16_t idx) {
  CpEntry& e = from->cp[idx];
  if (e.resolved) return static_cast<Field*>(e.resolved);
  Class* c = resolveClass(t, from, (uint16_t)e.a);
  if (!c) return nullptr;
  CpEntry& nt = from->cp[e.b];
  Sym n = utf8(from, (uint16_t)nt.a), d = utf8(from, (uint16_t)nt.b);
  Field* f = nullptr;
  for (Class* k = c; k && !f; k = k->super) {
    f = k->findField(n, d);
    for (size_t i = 0; !f && i < k->allIfaces.size(); i++) f = k->allIfaces[i]->findField(n, d);
  }
  if (!f) {
    throwNew(t, "java/lang/NoSuchFieldError", className(c) + "." + n.str());
    return nullptr;
  }
  e.resolved = f;
  return f;
}

Method* Vm::resolveMethod(Thread& t, Class* from, uint16_t idx, bool iface) {
  (void)iface;
  CpEntry& e = from->cp[idx];
  if (e.resolved) return static_cast<Method*>(e.resolved);
  Class* c = resolveClass(t, from, (uint16_t)e.a);
  if (!c) return nullptr;
  CpEntry& nt = from->cp[e.b];
  Sym n = utf8(from, (uint16_t)nt.a), d = utf8(from, (uint16_t)nt.b);
  Method* m = lookupMethod(c, n, d);
  if (!m) {
    throwNew(t, "java/lang/NoSuchMethodError", className(c) + "." + n.str() + d.str());
    return nullptr;
  }
  e.resolved = m;
  return m;
}

Method* Vm::superTarget(Class* from, Method* m) {
  if (m->name == sInit || m->isPrivate() || m->isStatic()) return m;
  if (!(from->flags & ACC_SUPER)) return m;
  bool isSuper = false;
  for (Class* k = from->super; k; k = k->super)
    if (k == m->owner) isSuper = true;
  if (!isSuper) return m;
  for (Class* k = from->super; k; k = k->super)
    if (Method* x = k->findMethod(m->name, m->desc)) return x;
  return m;
}

Method* Vm::ifaceTarget(Class* c, Method* im) { return vtableFind(c, im->name, im->desc); }

// ---- static initialization ----

int Vm::initForInterp(Thread& t, Class* c) {
  if (c->state == CLS_READY) return 0;
  if (c->state == CLS_INITING) {
    if (c->initThread == &t) return 0;
    t.waitClass = c;
    t.state = TState::Blocked;
    return 2;
  }
  return initClass(t, c) ? 0 : 1;
}

bool Vm::initClass(Thread& t, Class* c) {
  for (;;) {
    if (c->state == CLS_READY) return true;
    if (c->state == CLS_FAILED) {
      throwNew(t, "java/lang/NoClassDefFoundError", className(c));
      return false;
    }
    if (c->state == CLS_INITING) {
      if (c->initThread == &t) return true;
      t.waitClass = c;
      t.state = TState::Blocked;
      blockUntilRunnable(t);
      if (t.pending || exited) return false;
      continue;
    }
    break;
  }
  c->state = CLS_INITING;
  c->initThread = &t;
  if (c->super && c->super->state != CLS_READY && !initClass(t, c->super)) {
    c->state = CLS_FAILED;
    c->initThread = nullptr;
    return false;
  }
  for (Field& f : c->fields) {
    if (!f.isStatic || !f.constIdx || f.constIdx >= c->cp.size()) continue;
    CpEntry& k = c->cp[f.constIdx];
    switch (k.tag) {
      case CP_Integer:
      case CP_Float:
        f.addr->i = (int32_t)k.a;
        break;
      case CP_Long:
      case CP_Double:
        f.addr->j = (int64_t)(((uint64_t)k.a << 32) | k.b);
        break;
      case CP_String:
        f.addr->a = ldcString(c, f.constIdx);
        break;
      default:
        break;
    }
  }
  if (Method* clinit = c->findMethod(sClinit, sVoidDesc)) call(t, clinit, nullptr);
  if (t.pending) {
    Object* ex = t.pending;
    c->state = CLS_FAILED;
    c->initThread = nullptr;
    Class* err = loadClass("java/lang/Error");
    if (err && !assignable(err, ex->cls)) {
      printTrace(t, ex, "j2me: exception in the static initializer of " + className(c) + ": ");
      t.pending = nullptr;
      Object* why = ex->slots()[fThrowMsg].a;
      std::string msg = className(ex->cls);
      if (why) msg += ": " + str(why);
      throwNew(t, "java/lang/ExceptionInInitializerError", msg);
    }
    return false;
  }
  c->state = CLS_READY;
  c->initThread = nullptr;
  return true;
}

}  // namespace j2me
