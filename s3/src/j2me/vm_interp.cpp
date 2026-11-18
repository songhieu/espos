// The bytecode interpreter (docs/j2me.md "Interpreter"): computed-goto dispatch (GCC and clang), every JVM opcode
// javac emits for Java 1.1-8 but invokedynamic. Each JVM slot is one 8-byte Slot; long and double take two (the value
// in the first). The first run of a field access, call, new, checkcast/instanceof or anewarray resolves it and
// rewrites the instruction in place into a quick form (vm_internal.h OP_*_Q) that skips resolution from then on.
#include "vm_internal.h"

#ifndef J2ME_COUNT
#ifdef ESP_PLATFORM
#define J2ME_COUNT 0
#else
#define J2ME_COUNT 1
#endif
#endif

namespace j2me {

static const int kCheckEvery = 2048;  // backward branches and calls between clock reads

// Exceptions the interpreter raises, built out of line (cold): run() keeps a small C stack frame, which matters on the
// board, where nested calls (class initializers, callbacks) each take one run() frame.
#define COLD [[gnu::noinline, gnu::cold]]
COLD static void raise(Vm& vm, Thread& t, const char* cls, const char* msg) { vm.throwNew(t, cls, msg ? msg : ""); }
COLD static void raiseInt(Vm& vm, Thread& t, const char* cls, int64_t v) { vm.throwNew(t, cls, std::to_string(v)); }
COLD static void raiseText(Vm& vm, Thread& t, const char* cls, const char* prefix, Class* c, const char* sep, const char* tail) {
  vm.throwNew(t, cls, std::string(prefix) + (c ? vm.className(c) : std::string()) + sep + tail);
}
COLD static void raiseCast(Vm& vm, Thread& t, Class* from, Class* to) {
  vm.throwNew(t, "java/lang/ClassCastException", vm.className(from) + " cannot be cast to " + vm.className(to));
}
COLD static void raiseNoNative(Vm& vm, Thread& t, Method* m) {
  vm.throwNew(t, "java/lang/Error", "native method missing: " + m->owner->name.str() + "." + m->name.str() + m->desc.str());
}
COLD static void raiseBad(Vm& vm, Thread& t, int op, Method* m) {
  vm.throwNew(t, "java/lang/Error", "bad opcode " + std::to_string(op) + " in " + vm.className(m->owner) + "." + m->name.str());
}
#undef COLD

// A one-slot value (int, float, reference). On 32-bit targets (the board) a reference fits in the low half of a Slot,
// so only 4 bytes move (half the loads and stores on Xtensa); on 64-bit hosts the whole Slot does.
#if UINTPTR_MAX == 0xffffffffu
#define MOVE1(dst, src) ((dst).i = (src).i)
#else
#define MOVE1(dst, src) ((dst) = (src))
#endif

void Vm::run(Thread& t, int untilDepth, uint64_t deadline) {
  if (t.depth <= untilDepth || exited) return;
  cur = &t;
  static void* labels[256];
  static bool tableReady = false;
  if (!tableReady) {
    for (int i = 0; i < 256; i++) labels[i] = &&op_bad;
#define L(n, name) labels[n] = &&op_##name;
    L(0x00, nop) L(0x01, aconst_null) L(0x02, iconst_m1) L(0x03, iconst_0) L(0x04, iconst_1) L(0x05, iconst_2)
    L(0x06, iconst_3) L(0x07, iconst_4) L(0x08, iconst_5) L(0x09, lconst_0) L(0x0a, lconst_1) L(0x0b, fconst_0)
    L(0x0c, fconst_1) L(0x0d, fconst_2) L(0x0e, dconst_0) L(0x0f, dconst_1) L(0x10, bipush) L(0x11, sipush)
    L(0x12, ldc) L(0x13, ldc_w) L(0x14, ldc2_w) L(0x15, iload) L(0x16, lload) L(0x17, iload) L(0x18, lload)
    L(0x19, iload) L(0x1a, load_0) L(0x1b, load_1) L(0x1c, load_2) L(0x1d, load_3) L(0x1e, lload_0)
    L(0x1f, lload_1) L(0x20, lload_2) L(0x21, lload_3) L(0x22, load_0) L(0x23, load_1) L(0x24, load_2)
    L(0x25, load_3) L(0x26, lload_0) L(0x27, lload_1) L(0x28, lload_2) L(0x29, lload_3) L(0x2a, load_0)
    L(0x2b, load_1) L(0x2c, load_2) L(0x2d, load_3) L(0x2e, iaload) L(0x2f, laload) L(0x30, faload)
    L(0x31, daload) L(0x32, aaload) L(0x33, baload) L(0x34, caload) L(0x35, saload) L(0x36, istore)
    L(0x37, lstore) L(0x38, istore) L(0x39, lstore) L(0x3a, istore) L(0x3b, store_0) L(0x3c, store_1)
    L(0x3d, store_2) L(0x3e, store_3) L(0x3f, lstore_0) L(0x40, lstore_1) L(0x41, lstore_2) L(0x42, lstore_3)
    L(0x43, store_0) L(0x44, store_1) L(0x45, store_2) L(0x46, store_3) L(0x47, lstore_0) L(0x48, lstore_1)
    L(0x49, lstore_2) L(0x4a, lstore_3) L(0x4b, store_0) L(0x4c, store_1) L(0x4d, store_2) L(0x4e, store_3)
    L(0x4f, iastore) L(0x50, lastore) L(0x51, fastore) L(0x52, dastore) L(0x53, aastore) L(0x54, bastore)
    L(0x55, castore) L(0x56, sastore) L(0x57, pop) L(0x58, pop2) L(0x59, dup) L(0x5a, dup_x1) L(0x5b, dup_x2)
    L(0x5c, dup2) L(0x5d, dup2_x1) L(0x5e, dup2_x2) L(0x5f, swap) L(0x60, iadd) L(0x61, ladd) L(0x62, fadd)
    L(0x63, dadd) L(0x64, isub) L(0x65, lsub) L(0x66, fsub) L(0x67, dsub) L(0x68, imul) L(0x69, lmul)
    L(0x6a, fmul) L(0x6b, dmul) L(0x6c, idiv) L(0x6d, ldiv) L(0x6e, fdiv) L(0x6f, ddiv) L(0x70, irem)
    L(0x71, lrem) L(0x72, frem) L(0x73, drem) L(0x74, ineg) L(0x75, lneg) L(0x76, fneg) L(0x77, dneg)
    L(0x78, ishl) L(0x79, lshl) L(0x7a, ishr) L(0x7b, lshr) L(0x7c, iushr) L(0x7d, lushr) L(0x7e, iand)
    L(0x7f, land) L(0x80, ior) L(0x81, lor) L(0x82, ixor) L(0x83, lxor) L(0x84, iinc) L(0x85, i2l) L(0x86, i2f)
    L(0x87, i2d) L(0x88, l2i) L(0x89, l2f) L(0x8a, l2d) L(0x8b, f2i) L(0x8c, f2l) L(0x8d, f2d) L(0x8e, d2i)
    L(0x8f, d2l) L(0x90, d2f) L(0x91, i2b) L(0x92, i2c) L(0x93, i2s) L(0x94, lcmp) L(0x95, fcmpl) L(0x96, fcmpg)
    L(0x97, dcmpl) L(0x98, dcmpg) L(0x99, ifeq) L(0x9a, ifne) L(0x9b, iflt) L(0x9c, ifge) L(0x9d, ifgt)
    L(0x9e, ifle) L(0x9f, if_icmpeq) L(0xa0, if_icmpne) L(0xa1, if_icmplt) L(0xa2, if_icmpge) L(0xa3, if_icmpgt)
    L(0xa4, if_icmple) L(0xa5, if_acmpeq) L(0xa6, if_acmpne) L(0xa7, goto) L(0xa8, jsr) L(0xa9, ret)
    L(0xaa, tableswitch) L(0xab, lookupswitch) L(0xac, ireturn) L(0xad, lreturn) L(0xae, ireturn)
    L(0xaf, lreturn) L(0xb0, ireturn) L(0xb1, return) L(0xb2, getstatic) L(0xb3, putstatic) L(0xb4, getfield)
    L(0xb5, putfield) L(0xb6, invokevirtual) L(0xb7, invokespecial) L(0xb8, invokestatic)
    L(0xb9, invokeinterface) L(0xba, invokedynamic) L(0xbb, new) L(0xbc, newarray) L(0xbd, anewarray)
    L(0xbe, arraylength) L(0xbf, athrow) L(0xc0, checkcast) L(0xc1, instanceof) L(0xc2, monitorenter)
    L(0xc3, monitorexit) L(0xc4, wide) L(0xc5, multianewarray) L(0xc6, ifnull) L(0xc7, ifnonnull)
    L(0xc8, goto_w) L(0xc9, jsr_w) L(OP_GETFIELD_Q, getfield_q) L(OP_GETFIELD_Q2, getfield_q2)
    L(OP_PUTFIELD_Q, putfield_q) L(OP_PUTFIELD_Q2, putfield_q2) L(OP_GETSTATIC_Q, getstatic_q)
    L(OP_GETSTATIC_Q2, getstatic_q2) L(OP_PUTSTATIC_Q, putstatic_q) L(OP_PUTSTATIC_Q2, putstatic_q2)
    L(OP_INVOKEVIRTUAL_Q, invokevirtual_q) L(OP_INVOKEDIRECT_Q, invokedirect_q)
    L(OP_INVOKEDIRECT_QS, invokedirect_qs) L(OP_INVOKEINTERFACE_Q, invokeinterface_q) L(OP_NEW_Q, new_q)
    L(OP_CHECKCAST_Q, checkcast_q) L(OP_INSTANCEOF_Q, instanceof_q) L(OP_ANEWARRAY_Q, anewarray_q)
#undef L
    tableReady = true;
  }

  Frame* fr;
  Method* m;
  uint8_t* code;
  uint8_t* pc;
  Slot* sp;
  Slot* lv;
  CpEntry* cp;
  Method* callee = nullptr;
  int ilen = 3;
  uint32_t cidx = 0;
  int clen = 0;
  Slot rv;
  int rn = 0;
  int slice = kCheckEvery;
  rv.j = 0;
#if J2ME_COUNT
  uint64_t nb = 0;
#define COUNT() (++nb)
#define FLUSH() (bytecodes += nb, nb = 0)
#else
#define COUNT() ((void)0)
#define FLUSH() ((void)0)
#endif
#define LOAD()                          \
  do {                                  \
    fr = &t.frames[t.depth - 1];        \
    m = fr->m;                          \
    code = m->code;                     \
    pc = code + fr->pc;                 \
    sp = fr->sp;                        \
    lv = fr->locals;                    \
    cp = m->owner->cp.data();           \
  } while (0)
#define SAVE()                          \
  do {                                  \
    fr->pc = (uint32_t)(pc - code);     \
    fr->sp = sp;                        \
  } while (0)
#define DISPATCH() goto* labels[*pc]
#define NEXT(n)    \
  do {             \
    pc += (n);     \
    COUNT();       \
    DISPATCH();    \
  } while (0)
#define CHECK_TIME()                                       \
  do {                                                     \
    if (--slice <= 0) {                                    \
      slice = kCheckEvery;                                 \
      if (deadline && nowUs() >= deadline) {               \
        SAVE();                                            \
        goto out;                                          \
      }                                                    \
    }                                                      \
  } while (0)
#define BRANCH(off)                   \
  do {                                \
    int32_t o_ = (int32_t)(off);      \
    pc += o_;                         \
    if (o_ <= 0) CHECK_TIME();        \
    COUNT();                          \
    DISPATCH();                       \
  } while (0)
#define RAISE(call)              \
  do {                           \
    SAVE();                      \
    call;                        \
    goto exception;              \
  } while (0)
#define THROW(cls, msg) RAISE(raise(*this, t, cls, msg))
#define NPE() THROW("java/lang/NullPointerException", nullptr)
#define ARRAY(n, back)                                                                  \
  Object* arr = sp[-(back)].a;                                                          \
  int32_t idx = sp[-(back) + 1].i;                                                      \
  if (!arr) NPE();                                                                      \
  if ((uint32_t)idx >= (uint32_t)arr->length)                                           \
    RAISE(raiseInt(*this, t, "java/lang/ArrayIndexOutOfBoundsException", idx));            \
  (void)(n)

  LOAD();
  if (fr->skip) {
    uint8_t s = fr->skip;
    fr->skip = 0;
    if (t.pending) goto exception;
    pc += s;
  } else if (t.pending) {
    goto exception;
  }
  DISPATCH();

  // ---- constants ----
op_nop:
  NEXT(1);
op_aconst_null:
  sp->a = nullptr;
  sp++;
  NEXT(1);
op_iconst_m1:
  (sp++)->i = -1;
  NEXT(1);
op_iconst_0:
  (sp++)->i = 0;
  NEXT(1);
op_iconst_1:
  (sp++)->i = 1;
  NEXT(1);
op_iconst_2:
  (sp++)->i = 2;
  NEXT(1);
op_iconst_3:
  (sp++)->i = 3;
  NEXT(1);
op_iconst_4:
  (sp++)->i = 4;
  NEXT(1);
op_iconst_5:
  (sp++)->i = 5;
  NEXT(1);
op_lconst_0:
  sp->j = 0;
  sp += 2;
  NEXT(1);
op_lconst_1:
  sp->j = 1;
  sp += 2;
  NEXT(1);
op_fconst_0:
  (sp++)->f = 0.0f;
  NEXT(1);
op_fconst_1:
  (sp++)->f = 1.0f;
  NEXT(1);
op_fconst_2:
  (sp++)->f = 2.0f;
  NEXT(1);
op_dconst_0:
  sp->d = 0.0;
  sp += 2;
  NEXT(1);
op_dconst_1:
  sp->d = 1.0;
  sp += 2;
  NEXT(1);
op_bipush:
  (sp++)->i = (int8_t)pc[1];
  NEXT(2);
op_sipush:
  (sp++)->i = s2(pc + 1);
  NEXT(3);
op_ldc:
  cidx = pc[1];
  clen = 2;
  goto ldc;
op_ldc_w:
  cidx = u2(pc + 1);
  clen = 3;
  goto ldc;
ldc : {
  CpEntry& e = cp[cidx];
  switch (e.tag) {
    case CP_Integer:
    case CP_Float:
      (sp++)->i = (int32_t)e.a;
      break;
    case CP_String:
      if (!e.resolved) {
        SAVE();
        if (!ldcString(m->owner, (uint16_t)cidx)) goto exception;
      }
      (sp++)->a = static_cast<Object*>(e.resolved);
      break;
    case CP_Class: {
      Class* c = static_cast<Class*>(e.resolved);
      if (!c || !c->mirror) {
        SAVE();
        c = resolveClass(t, m->owner, (uint16_t)cidx);
        if (!c || !mirror(c)) goto exception;
      }
      (sp++)->a = c->mirror;
      break;
    }
    default:
      THROW("java/lang/Error", "unsupported ldc constant");
  }
  NEXT(clen);
}
op_ldc2_w : {
  CpEntry& e = cp[u2(pc + 1)];
  sp->j = (int64_t)(((uint64_t)e.a << 32) | e.b);
  sp += 2;
  NEXT(3);
}

  // ---- locals ----
op_iload:  // also fload, aload
  MOVE1(*sp, lv[pc[1]]);
  sp++;
  NEXT(2);
op_lload:  // also dload
  *sp = lv[pc[1]];
  sp += 2;
  NEXT(2);
op_load_0:
  MOVE1(*sp, lv[0]);
  sp++;
  NEXT(1);
op_load_1:
  MOVE1(*sp, lv[1]);
  sp++;
  NEXT(1);
op_load_2:
  MOVE1(*sp, lv[2]);
  sp++;
  NEXT(1);
op_load_3:
  MOVE1(*sp, lv[3]);
  sp++;
  NEXT(1);
op_lload_0:
  *sp = lv[0];
  sp += 2;
  NEXT(1);
op_lload_1:
  *sp = lv[1];
  sp += 2;
  NEXT(1);
op_lload_2:
  *sp = lv[2];
  sp += 2;
  NEXT(1);
op_lload_3:
  *sp = lv[3];
  sp += 2;
  NEXT(1);
op_istore:  // also fstore, astore
  sp--;
  MOVE1(lv[pc[1]], *sp);
  NEXT(2);
op_lstore:
  sp -= 2;
  lv[pc[1]] = *sp;
  NEXT(2);
op_store_0:
  sp--;
  MOVE1(lv[0], *sp);
  NEXT(1);
op_store_1:
  sp--;
  MOVE1(lv[1], *sp);
  NEXT(1);
op_store_2:
  sp--;
  MOVE1(lv[2], *sp);
  NEXT(1);
op_store_3:
  sp--;
  MOVE1(lv[3], *sp);
  NEXT(1);
op_lstore_0:
  sp -= 2;
  lv[0] = *sp;
  NEXT(1);
op_lstore_1:
  sp -= 2;
  lv[1] = *sp;
  NEXT(1);
op_lstore_2:
  sp -= 2;
  lv[2] = *sp;
  NEXT(1);
op_lstore_3:
  sp -= 2;
  lv[3] = *sp;
  NEXT(1);
op_iinc : {
  Slot& s = lv[pc[1]];
  s.i = (int32_t)((uint32_t)s.i + (uint32_t)(int32_t)(int8_t)pc[2]);
  NEXT(3);
}

  // ---- arrays ----
op_iaload : {
  ARRAY(4, 2);
  sp[-2].i = arr->elems<int32_t>()[idx];
  sp--;
  NEXT(1);
}
op_faload : {
  ARRAY(4, 2);
  sp[-2].f = arr->elems<float>()[idx];
  sp--;
  NEXT(1);
}
op_laload : {
  ARRAY(8, 2);
  sp[-2].j = arr->elems<int64_t>()[idx];
  NEXT(1);
}
op_daload : {
  ARRAY(8, 2);
  sp[-2].d = arr->elems<double>()[idx];
  NEXT(1);
}
op_aaload : {
  ARRAY(0, 2);
  sp[-2].a = arr->elems<Object*>()[idx];
  sp--;
  NEXT(1);
}
op_baload : {
  ARRAY(1, 2);
  sp[-2].i = arr->elems<int8_t>()[idx];
  sp--;
  NEXT(1);
}
op_caload : {
  ARRAY(2, 2);
  sp[-2].i = arr->elems<uint16_t>()[idx];
  sp--;
  NEXT(1);
}
op_saload : {
  ARRAY(2, 2);
  sp[-2].i = arr->elems<int16_t>()[idx];
  sp--;
  NEXT(1);
}
op_iastore : {
  ARRAY(4, 3);
  arr->elems<int32_t>()[idx] = sp[-1].i;
  sp -= 3;
  NEXT(1);
}
op_fastore : {
  ARRAY(4, 3);
  arr->elems<float>()[idx] = sp[-1].f;
  sp -= 3;
  NEXT(1);
}
op_lastore : {
  ARRAY(8, 4);
  arr->elems<int64_t>()[idx] = sp[-2].j;
  sp -= 4;
  NEXT(1);
}
op_dastore : {
  ARRAY(8, 4);
  arr->elems<double>()[idx] = sp[-2].d;
  sp -= 4;
  NEXT(1);
}
op_aastore : {
  ARRAY(0, 3);
  Object* v = sp[-1].a;
  if (v && v->cls != arr->cls->elemClass && !assignable(arr->cls->elemClass, v->cls))
    RAISE(raiseText(*this, t, "java/lang/ArrayStoreException", "", v->cls, "", ""));
  arr->elems<Object*>()[idx] = v;
  sp -= 3;
  NEXT(1);
}
op_bastore : {
  ARRAY(1, 3);
  arr->elems<int8_t>()[idx] = (int8_t)(arr->cls->elem == 'Z' ? (sp[-1].i & 1) : sp[-1].i);
  sp -= 3;
  NEXT(1);
}
op_castore : {
  ARRAY(2, 3);
  arr->elems<uint16_t>()[idx] = (uint16_t)sp[-1].i;
  sp -= 3;
  NEXT(1);
}
op_sastore : {
  ARRAY(2, 3);
  arr->elems<int16_t>()[idx] = (int16_t)sp[-1].i;
  sp -= 3;
  NEXT(1);
}
op_arraylength : {
  Object* a = sp[-1].a;
  if (!a) NPE();
  sp[-1].i = a->length;
  NEXT(1);
}

  // ---- the operand stack ----
op_pop:
  sp--;
  NEXT(1);
op_pop2:
  sp -= 2;
  NEXT(1);
op_dup:
  MOVE1(sp[0], sp[-1]);
  sp++;
  NEXT(1);
op_dup_x1 : {
  Slot v1 = sp[-1], v2 = sp[-2];
  sp[-2] = v1;
  sp[-1] = v2;
  sp[0] = v1;
  sp++;
  NEXT(1);
}
op_dup_x2 : {
  Slot v1 = sp[-1], v2 = sp[-2], v3 = sp[-3];
  sp[-3] = v1;
  sp[-2] = v3;
  sp[-1] = v2;
  sp[0] = v1;
  sp++;
  NEXT(1);
}
op_dup2:
  sp[0] = sp[-2];
  sp[1] = sp[-1];
  sp += 2;
  NEXT(1);
op_dup2_x1 : {
  Slot v1 = sp[-1], v2 = sp[-2], v3 = sp[-3];
  sp[-3] = v2;
  sp[-2] = v1;
  sp[-1] = v3;
  sp[0] = v2;
  sp[1] = v1;
  sp += 2;
  NEXT(1);
}
op_dup2_x2 : {
  Slot v1 = sp[-1], v2 = sp[-2], v3 = sp[-3], v4 = sp[-4];
  sp[-4] = v2;
  sp[-3] = v1;
  sp[-2] = v4;
  sp[-1] = v3;
  sp[0] = v2;
  sp[1] = v1;
  sp += 2;
  NEXT(1);
}
op_swap : {
  Slot v = sp[-1];
  sp[-1] = sp[-2];
  sp[-2] = v;
  NEXT(1);
}

  // ---- arithmetic (wrapping like Java: unsigned maths where C++ calls overflow undefined) ----
op_iadd:
  sp[-2].i = (int32_t)((uint32_t)sp[-2].i + (uint32_t)sp[-1].i);
  sp--;
  NEXT(1);
op_ladd:
  sp[-4].j = (int64_t)((uint64_t)sp[-4].j + (uint64_t)sp[-2].j);
  sp -= 2;
  NEXT(1);
op_fadd:
  sp[-2].f = sp[-2].f + sp[-1].f;
  sp--;
  NEXT(1);
op_dadd:
  sp[-4].d = sp[-4].d + sp[-2].d;
  sp -= 2;
  NEXT(1);
op_isub:
  sp[-2].i = (int32_t)((uint32_t)sp[-2].i - (uint32_t)sp[-1].i);
  sp--;
  NEXT(1);
op_lsub:
  sp[-4].j = (int64_t)((uint64_t)sp[-4].j - (uint64_t)sp[-2].j);
  sp -= 2;
  NEXT(1);
op_fsub:
  sp[-2].f = sp[-2].f - sp[-1].f;
  sp--;
  NEXT(1);
op_dsub:
  sp[-4].d = sp[-4].d - sp[-2].d;
  sp -= 2;
  NEXT(1);
op_imul:
  sp[-2].i = (int32_t)((uint32_t)sp[-2].i * (uint32_t)sp[-1].i);
  sp--;
  NEXT(1);
op_lmul:
  sp[-4].j = (int64_t)((uint64_t)sp[-4].j * (uint64_t)sp[-2].j);
  sp -= 2;
  NEXT(1);
op_fmul:
  sp[-2].f = sp[-2].f * sp[-1].f;
  sp--;
  NEXT(1);
op_dmul:
  sp[-4].d = sp[-4].d * sp[-2].d;
  sp -= 2;
  NEXT(1);
op_idiv : {
  int32_t b = sp[-1].i, a = sp[-2].i;
  if (b == 0) THROW("java/lang/ArithmeticException", "/ by zero");
  sp[-2].i = b == -1 ? (int32_t)(0u - (uint32_t)a) : a / b;
  sp--;
  NEXT(1);
}
op_ldiv : {
  int64_t b = sp[-2].j, a = sp[-4].j;
  if (b == 0) THROW("java/lang/ArithmeticException", "/ by zero");
  sp[-4].j = b == -1 ? (int64_t)(0u - (uint64_t)a) : a / b;
  sp -= 2;
  NEXT(1);
}
op_fdiv:
  sp[-2].f = sp[-2].f / sp[-1].f;
  sp--;
  NEXT(1);
op_ddiv:
  sp[-4].d = sp[-4].d / sp[-2].d;
  sp -= 2;
  NEXT(1);
op_irem : {
  int32_t b = sp[-1].i, a = sp[-2].i;
  if (b == 0) THROW("java/lang/ArithmeticException", "/ by zero");
  sp[-2].i = b == -1 ? 0 : a % b;
  sp--;
  NEXT(1);
}
op_lrem : {
  int64_t b = sp[-2].j, a = sp[-4].j;
  if (b == 0) THROW("java/lang/ArithmeticException", "/ by zero");
  sp[-4].j = b == -1 ? 0 : a % b;
  sp -= 2;
  NEXT(1);
}
op_frem:
  sp[-2].f = std::fmod(sp[-2].f, sp[-1].f);
  sp--;
  NEXT(1);
op_drem:
  sp[-4].d = std::fmod(sp[-4].d, sp[-2].d);
  sp -= 2;
  NEXT(1);
op_ineg:
  sp[-1].i = (int32_t)(0u - (uint32_t)sp[-1].i);
  NEXT(1);
op_lneg:
  sp[-2].j = (int64_t)(0u - (uint64_t)sp[-2].j);
  NEXT(1);
op_fneg:
  sp[-1].f = -sp[-1].f;
  NEXT(1);
op_dneg:
  sp[-2].d = -sp[-2].d;
  NEXT(1);
op_ishl:
  sp[-2].i = (int32_t)((uint32_t)sp[-2].i << (sp[-1].i & 31));
  sp--;
  NEXT(1);
op_lshl:
  sp[-3].j = (int64_t)((uint64_t)sp[-3].j << (sp[-1].i & 63));
  sp--;
  NEXT(1);
op_ishr:
  sp[-2].i = sp[-2].i >> (sp[-1].i & 31);
  sp--;
  NEXT(1);
op_lshr:
  sp[-3].j = sp[-3].j >> (sp[-1].i & 63);
  sp--;
  NEXT(1);
op_iushr:
  sp[-2].i = (int32_t)((uint32_t)sp[-2].i >> (sp[-1].i & 31));
  sp--;
  NEXT(1);
op_lushr:
  sp[-3].j = (int64_t)((uint64_t)sp[-3].j >> (sp[-1].i & 63));
  sp--;
  NEXT(1);
op_iand:
  sp[-2].i &= sp[-1].i;
  sp--;
  NEXT(1);
op_land:
  sp[-4].j &= sp[-2].j;
  sp -= 2;
  NEXT(1);
op_ior:
  sp[-2].i |= sp[-1].i;
  sp--;
  NEXT(1);
op_lor:
  sp[-4].j |= sp[-2].j;
  sp -= 2;
  NEXT(1);
op_ixor:
  sp[-2].i ^= sp[-1].i;
  sp--;
  NEXT(1);
op_lxor:
  sp[-4].j ^= sp[-2].j;
  sp -= 2;
  NEXT(1);

  // ---- conversions ----
op_i2l:
  sp[-1].j = (int64_t)sp[-1].i;
  sp++;
  NEXT(1);
op_i2f:
  sp[-1].f = (float)sp[-1].i;
  NEXT(1);
op_i2d:
  sp[-1].d = (double)sp[-1].i;
  sp++;
  NEXT(1);
op_l2i:
  sp[-2].i = (int32_t)(uint32_t)(uint64_t)sp[-2].j;
  sp--;
  NEXT(1);
op_l2f:
  sp[-2].f = (float)sp[-2].j;
  sp--;
  NEXT(1);
op_l2d:
  sp[-2].d = (double)sp[-2].j;
  NEXT(1);
op_f2i:
  sp[-1].i = f2i(sp[-1].f);
  NEXT(1);
op_f2l:
  sp[-1].j = f2l(sp[-1].f);
  sp++;
  NEXT(1);
op_f2d:
  sp[-1].d = (double)sp[-1].f;
  sp++;
  NEXT(1);
op_d2i:
  sp[-2].i = f2i(sp[-2].d);
  sp--;
  NEXT(1);
op_d2l:
  sp[-2].j = f2l(sp[-2].d);
  NEXT(1);
op_d2f:
  sp[-2].f = (float)sp[-2].d;
  sp--;
  NEXT(1);
op_i2b:
  sp[-1].i = (int8_t)sp[-1].i;
  NEXT(1);
op_i2c:
  sp[-1].i = (uint16_t)sp[-1].i;
  NEXT(1);
op_i2s:
  sp[-1].i = (int16_t)sp[-1].i;
  NEXT(1);

  // ---- comparisons and branches ----
op_lcmp : {
  int64_t a = sp[-4].j, b = sp[-2].j;
  sp[-4].i = a > b ? 1 : a < b ? -1 : 0;
  sp -= 3;
  NEXT(1);
}
op_fcmpl : {
  float a = sp[-2].f, b = sp[-1].f;
  sp[-2].i = a > b ? 1 : a == b ? 0 : -1;
  sp--;
  NEXT(1);
}
op_fcmpg : {
  float a = sp[-2].f, b = sp[-1].f;
  sp[-2].i = a < b ? -1 : a == b ? 0 : 1;
  sp--;
  NEXT(1);
}
op_dcmpl : {
  double a = sp[-4].d, b = sp[-2].d;
  sp[-4].i = a > b ? 1 : a == b ? 0 : -1;
  sp -= 3;
  NEXT(1);
}
op_dcmpg : {
  double a = sp[-4].d, b = sp[-2].d;
  sp[-4].i = a < b ? -1 : a == b ? 0 : 1;
  sp -= 3;
  NEXT(1);
}
#define IF1(name, cond)               \
  op_##name : {                       \
    int32_t v = (--sp)->i;            \
    if (cond) BRANCH(s2(pc + 1));     \
    NEXT(3);                          \
  }
  IF1(ifeq, v == 0)
  IF1(ifne, v != 0)
  IF1(iflt, v < 0)
  IF1(ifge, v >= 0)
  IF1(ifgt, v > 0)
  IF1(ifle, v <= 0)
#define IF2(name, cond)               \
  op_##name : {                       \
    int32_t b = sp[-1].i, a = sp[-2].i; \
    sp -= 2;                          \
    if (cond) BRANCH(s2(pc + 1));     \
    NEXT(3);                          \
  }
  IF2(if_icmpeq, a == b)
  IF2(if_icmpne, a != b)
  IF2(if_icmplt, a < b)
  IF2(if_icmpge, a >= b)
  IF2(if_icmpgt, a > b)
  IF2(if_icmple, a <= b)
op_if_acmpeq : {
  Object *b = sp[-1].a, *a = sp[-2].a;
  sp -= 2;
  if (a == b) BRANCH(s2(pc + 1));
  NEXT(3);
}
op_if_acmpne : {
  Object *b = sp[-1].a, *a = sp[-2].a;
  sp -= 2;
  if (a != b) BRANCH(s2(pc + 1));
  NEXT(3);
}
op_ifnull : {
  Object* a = (--sp)->a;
  if (!a) BRANCH(s2(pc + 1));
  NEXT(3);
}
op_ifnonnull : {
  Object* a = (--sp)->a;
  if (a) BRANCH(s2(pc + 1));
  NEXT(3);
}
op_goto:
  BRANCH(s2(pc + 1));
op_goto_w:
  BRANCH(s4(pc + 1));
op_jsr:
  (sp++)->i = (int32_t)(pc + 3 - code);
  BRANCH(s2(pc + 1));
op_jsr_w:
  (sp++)->i = (int32_t)(pc + 5 - code);
  BRANCH(s4(pc + 1));
op_ret:
  pc = code + lv[pc[1]].i;
  COUNT();
  DISPATCH();
op_tableswitch : {
  uint32_t off = (uint32_t)(pc - code);
  const uint8_t* p = code + ((off + 4) & ~3u);
  int32_t v = (--sp)->i, def = s4(p), lo = s4(p + 4), hi = s4(p + 8);
  if (v < lo || v > hi) BRANCH(def);
  BRANCH(s4(p + 12 + (size_t)((int64_t)v - lo) * 4));
}
op_lookupswitch : {
  uint32_t off = (uint32_t)(pc - code);
  const uint8_t* p = code + ((off + 4) & ~3u);
  int32_t v = (--sp)->i, def = s4(p), n = s4(p + 4);
  int32_t lo = 0, hi = n - 1;
  while (lo <= hi) {
    int32_t mid = (lo + hi) >> 1;
    int32_t key = s4(p + 8 + (size_t)mid * 8);
    if (key == v) BRANCH(s4(p + 12 + (size_t)mid * 8));
    if (key < v)
      lo = mid + 1;
    else
      hi = mid - 1;
  }
  BRANCH(def);
}

  // ---- returns ----
op_ireturn:  // also freturn, areturn
  rv = sp[-1];
  rn = 1;
  goto doreturn;
op_lreturn:
  rv = sp[-2];
  rn = 2;
  goto doreturn;
op_return:
  rn = 0;
  goto doreturn;
doreturn : {
  if (fr->sync) exit(t, fr->sync);
  t.depth--;
  if (t.depth <= untilDepth) {
    t.ret = rv;
    goto out;
  }
  LOAD();
  pc += (*pc == OP_INVOKEINTERFACE || *pc == OP_INVOKEINTERFACE_Q) ? 5 : 3;
  if (rn == 1)
    *sp++ = rv;
  else if (rn == 2) {
    *sp = rv;
    sp += 2;
  }
  COUNT();
  DISPATCH();
}

  // ---- fields ----
op_getstatic : {
  SAVE();
  Field* f = resolveField(t, m->owner, u2(pc + 1));
  if (!f) goto exception;
  if (!f->isStatic) THROW("java/lang/IncompatibleClassChangeError", f->name.c_str());
  int r = initForInterp(t, f->owner);
  if (r == 1) goto exception;
  if (r == 2) goto out;
  if (f->owner->state == CLS_READY) {
    *pc = f->isWide() ? OP_GETSTATIC_Q2 : OP_GETSTATIC_Q;
    DISPATCH();
  }
  *sp = *f->addr;
  sp += f->isWide() ? 2 : 1;
  NEXT(3);
}
op_putstatic : {
  SAVE();
  Field* f = resolveField(t, m->owner, u2(pc + 1));
  if (!f) goto exception;
  if (!f->isStatic) THROW("java/lang/IncompatibleClassChangeError", f->name.c_str());
  int r = initForInterp(t, f->owner);
  if (r == 1) goto exception;
  if (r == 2) goto out;
  if (f->owner->state == CLS_READY) {
    *pc = f->isWide() ? OP_PUTSTATIC_Q2 : OP_PUTSTATIC_Q;
    DISPATCH();
  }
  sp -= f->isWide() ? 2 : 1;
  *f->addr = *sp;
  NEXT(3);
}
op_getstatic_q:
  MOVE1(*sp, *static_cast<Field*>(cp[u2(pc + 1)].resolved)->addr);
  sp++;
  NEXT(3);
op_getstatic_q2:
  *sp = *static_cast<Field*>(cp[u2(pc + 1)].resolved)->addr;
  sp += 2;
  NEXT(3);
op_putstatic_q:
  sp--;
  MOVE1(*static_cast<Field*>(cp[u2(pc + 1)].resolved)->addr, *sp);
  NEXT(3);
op_putstatic_q2:
  sp -= 2;
  *static_cast<Field*>(cp[u2(pc + 1)].resolved)->addr = *sp;
  NEXT(3);
op_getfield:
op_putfield : {
  SAVE();
  Field* f = resolveField(t, m->owner, u2(pc + 1));
  if (!f) goto exception;
  if (f->isStatic) THROW("java/lang/IncompatibleClassChangeError", f->name.c_str());
  bool get = *pc == 0xb4;
  *pc = get ? (f->isWide() ? OP_GETFIELD_Q2 : OP_GETFIELD_Q) : (f->isWide() ? OP_PUTFIELD_Q2 : OP_PUTFIELD_Q);
  pc[1] = (uint8_t)(f->slot >> 8);
  pc[2] = (uint8_t)f->slot;
  DISPATCH();
}
op_getfield_q : {
  Object* o = sp[-1].a;
  if (!o) NPE();
  MOVE1(sp[-1], o->slots()[u2(pc + 1)]);
  NEXT(3);
}
op_getfield_q2 : {
  Object* o = sp[-1].a;
  if (!o) NPE();
  sp[-1] = o->slots()[u2(pc + 1)];
  sp++;
  NEXT(3);
}
op_putfield_q : {
  Object* o = sp[-2].a;
  if (!o) NPE();
  MOVE1(o->slots()[u2(pc + 1)], sp[-1]);
  sp -= 2;
  NEXT(3);
}
op_putfield_q2 : {
  Object* o = sp[-3].a;
  if (!o) NPE();
  o->slots()[u2(pc + 1)] = sp[-2];
  sp -= 3;
  NEXT(3);
}

  // ---- calls ----
op_invokevirtual : {
  SAVE();
  uint16_t ci = u2(pc + 1);
  Method* rm = resolveMethod(t, m->owner, ci, false);
  if (!rm) goto exception;
  if (rm->isStatic()) THROW("java/lang/IncompatibleClassChangeError", rm->name.c_str());
  Class* rc = static_cast<Class*>(cp[cp[ci].a].resolved);
  if (rm->isPrivate() || rm->isFinal() || (rc->flags & ACC_FINAL)) {
    Method* target = rm->isPrivate() || rm->isFinal() ? rm : vtableFind(rc, rm->name, rm->desc);
    if (!target) target = rm;
    if (direct.size() < 65535) {
      size_t di = direct.size();
      direct.push_back(target);
      *pc = OP_INVOKEDIRECT_Q;
      pc[1] = (uint8_t)(di >> 8);
      pc[2] = (uint8_t)di;
      DISPATCH();
    }
    if (!sp[-target->argSlots].a) NPE();
    callee = target;
    ilen = 3;
    goto invoke;
  }
  int vi = -1;
  for (size_t i = 0; i < rc->vtable.size(); i++)
    if (rc->vtable[i]->name == rm->name && rc->vtable[i]->desc == rm->desc) {
      vi = (int)i;
      break;
    }
  if (vi < 0) RAISE(raiseText(*this, t, "java/lang/AbstractMethodError", "", rc, ".", rm->name.c_str()));
  cp[ci].extra = (uint16_t)vi;
  *pc = OP_INVOKEVIRTUAL_Q;
  DISPATCH();
}
op_invokevirtual_q : {
  CpEntry& e = cp[u2(pc + 1)];
  Object* recv = sp[-static_cast<Method*>(e.resolved)->argSlots].a;
  if (!recv) NPE();
  callee = recv->cls->vtable[e.extra];
  ilen = 3;
  goto invoke;
}
op_invokespecial : {
  SAVE();
  Method* rm = resolveMethod(t, m->owner, u2(pc + 1), false);
  if (!rm) goto exception;
  if (rm->isStatic()) THROW("java/lang/IncompatibleClassChangeError", rm->name.c_str());
  Method* target = superTarget(m->owner, rm);
  if (direct.size() < 65535) {
    size_t di = direct.size();
    direct.push_back(target);
    *pc = OP_INVOKEDIRECT_Q;
    pc[1] = (uint8_t)(di >> 8);
    pc[2] = (uint8_t)di;
    DISPATCH();
  }
  if (!sp[-target->argSlots].a) NPE();
  callee = target;
  ilen = 3;
  goto invoke;
}
op_invokestatic : {
  SAVE();
  Method* rm = resolveMethod(t, m->owner, u2(pc + 1), false);
  if (!rm) goto exception;
  if (!rm->isStatic()) THROW("java/lang/IncompatibleClassChangeError", rm->name.c_str());
  int r = initForInterp(t, rm->owner);
  if (r == 1) goto exception;
  if (r == 2) goto out;
  if (rm->owner->state == CLS_READY && direct.size() < 65535) {
    size_t di = direct.size();
    direct.push_back(rm);
    *pc = OP_INVOKEDIRECT_QS;
    pc[1] = (uint8_t)(di >> 8);
    pc[2] = (uint8_t)di;
    DISPATCH();
  }
  callee = rm;
  ilen = 3;
  goto invoke;
}
op_invokedirect_q:
  callee = direct[u2(pc + 1)];
  if (!sp[-callee->argSlots].a) NPE();
  ilen = 3;
  goto invoke;
op_invokedirect_qs:
  callee = direct[u2(pc + 1)];
  ilen = 3;
  goto invoke;
op_invokeinterface : {
  SAVE();
  if (!resolveMethod(t, m->owner, u2(pc + 1), true)) goto exception;
  *pc = OP_INVOKEINTERFACE_Q;
  DISPATCH();
}
op_invokeinterface_q : {
  Method* im = static_cast<Method*>(cp[u2(pc + 1)].resolved);
  Object* recv = sp[-im->argSlots].a;
  if (!recv) NPE();
  Class* rc = recv->cls;
  ICache& ic = icache[((reinterpret_cast<uintptr_t>(rc) >> 3) ^ (reinterpret_cast<uintptr_t>(im) >> 4)) & 255];
  if (ic.cls == rc && ic.im == im) {
    callee = ic.target;
  } else {
    callee = ifaceTarget(rc, im);
    if (!callee) RAISE(raiseText(*this, t, "java/lang/IncompatibleClassChangeError", "", rc, " does not implement ", im->name.c_str()));
    ic.cls = rc;
    ic.im = im;
    ic.target = callee;
  }
  ilen = 5;
  goto invoke;
}
op_invokedynamic:
  THROW("java/lang/Error", "invokedynamic is not supported (compile without lambdas, -target 8 or older)");

invoke : {
  if (callee->flags & ACC_NATIVE) goto native;
  if (callee->flags & ACC_ABSTRACT) RAISE(raiseText(*this, t, "java/lang/AbstractMethodError", "", callee->owner, ".", callee->name.c_str()));
  Slot* args = sp - callee->argSlots;
  Slot* nsp = args + callee->maxLocals;
  if (nsp + callee->maxStack > t.stackLimit || t.depth >= t.depthLimit) {
    // the error's constructors run in the reserve
    SAVE();
    Slot* sl = t.stackLimit;
    int dl = t.depthLimit;
    t.stackLimit = t.stackEnd;
    t.depthLimit = t.maxDepth;
    if (sl == t.stackEnd)
      t.pending = soError;  // overflowed again while making one
    else
      raise(*this, t, "java/lang/StackOverflowError", nullptr);
    t.stackLimit = sl;
    t.depthLimit = dl;
    goto exception;
  }
  Object* lock = nullptr;
  if (callee->flags & ACC_SYNCHRONIZED) {
    if (callee->isStatic()) {
      lock = callee->owner->mirror;
      if (!lock) {
        SAVE();
        lock = mirror(callee->owner);
        if (!lock) goto exception;
      }
    } else {
      lock = args[0].a;
    }
    if (!enter(t, lock)) {
      SAVE();
      t.state = TState::Blocked;
      t.waitOn = lock;
      goto out;
    }
  }
  fr->pc = (uint32_t)(pc - code);
  fr->sp = args;
  for (Slot* s = args + callee->argSlots; s < nsp; s++) s->j = 0;
  fr = &t.frames[t.depth++];
  fr->m = callee;
  fr->pc = 0;
  fr->locals = args;
  fr->sp = nsp;
  fr->sync = lock;
  fr->skip = 0;
  m = callee;
  code = m->code;
  pc = code;
  lv = args;
  sp = nsp;
  cp = m->owner->cp.data();
  CHECK_TIME();
  COUNT();
  DISPATCH();
}
native : {
  SAVE();
  if (!callee->native) bindNative(callee);
  if (!callee->native)
    RAISE(raiseNoNative(*this, t, callee));
  Slot* args = sp - callee->argSlots;
  Slot packed[8];
  Slot* a = args;
  if (callee->wideArgs) {
    // natives get one Slot per argument: drop the second slot of each long/double
    int n = callee->argCount < 32 ? callee->argCount : 32;
    if (n > 8) a = static_cast<Slot*>(vmMalloc(sizeof(Slot) * (size_t)n));
    else a = packed;
    if (!a) RAISE(oom(t));
    Slot* s = args;
    for (int i = 0; i < n; i++) {
      a[i] = *s;
      s += (callee->wideArgs >> i) & 1 ? 2 : 1;
    }
  }
  Object* lock = nullptr;
  if (callee->flags & ACC_SYNCHRONIZED) {
    lock = callee->isStatic() ? mirror(callee->owner) : args[0].a;
    if (!lock) goto exception;
    if (!enter(t, lock)) {
      t.state = TState::Blocked;
      t.waitOn = lock;
      goto out;
    }
  }
  size_t mark = locals.size();
  Slot r;
  r.j = 0;
  bool wasNative = t.inNative;
  t.inNative = true;
  callee->native(*this, t, a, r);
  t.inNative = wasNative;
  if (a != args && a != packed) vmFree(a);
  locals.resize(mark);
  if (lock) exit(t, lock);
  if (exited) goto out;
  sp = args;
  if (t.pending) {
    fr->sp = sp;
    goto exception;
  }
  switch (callee->ret) {
    case 'V':
      break;
    case 'J':
    case 'D':
      *sp = r;
      sp += 2;
      break;
    default:
      *sp++ = r;
      break;
  }
  if (t.state != TState::Ready || t.yieldReq) {
    if (t.state == TState::Ready && deadline == 0) {
      t.yieldReq = false;  // a nested call runs to its end: yield() does nothing there
    } else {
      t.yieldReq = false;
      fr->sp = sp;
      fr->skip = (uint8_t)ilen;
      goto out;
    }
  }
  NEXT(ilen);
}

  // ---- objects ----
op_new : {
  SAVE();
  Class* c = resolveClass(t, m->owner, u2(pc + 1));
  if (!c) goto exception;
  if (c->isInterface() || c->isAbstract() || c->isArray) RAISE(raiseText(*this, t, "java/lang/Error", "cannot instantiate ", c, "", ""));
  int r = initForInterp(t, c);
  if (r == 1) goto exception;
  if (r == 2) goto out;
  if (c->state == CLS_READY) {
    *pc = OP_NEW_Q;
    DISPATCH();
  }
  Object* o = newObject(c);
  if (!o) goto exception;
  (sp++)->a = o;
  NEXT(3);
}
op_new_q : {
  SAVE();
  Object* o = newObject(static_cast<Class*>(cp[u2(pc + 1)].resolved));
  if (!o) goto exception;
  (sp++)->a = o;
  NEXT(3);
}
op_newarray : {
  int32_t n = sp[-1].i;
  if (n < 0) RAISE(raiseInt(*this, t, "java/lang/NegativeArraySizeException", n));
  static const char types[12] = {0, 0, 0, 0, 'Z', 'C', 'F', 'D', 'B', 'S', 'I', 'J'};
  char e = pc[1] < 12 ? types[pc[1]] : 0;
  if (!e) THROW("java/lang/Error", "bad newarray type");
  SAVE();
  Object* a = newArrayRaw(primArrayClass(e), n);
  if (!a) goto exception;
  sp[-1].a = a;
  NEXT(2);
}
op_anewarray : {
  SAVE();
  Class* c = resolveClass(t, m->owner, u2(pc + 1));
  if (!c) goto exception;
  if (!arrayOf(c)) goto exception;
  *pc = OP_ANEWARRAY_Q;
  DISPATCH();
}
op_anewarray_q : {
  int32_t n = sp[-1].i;
  if (n < 0) RAISE(raiseInt(*this, t, "java/lang/NegativeArraySizeException", n));
  SAVE();
  Object* a = newArrayRaw(static_cast<Class*>(cp[u2(pc + 1)].resolved)->arrayClass, n);
  if (!a) goto exception;
  sp[-1].a = a;
  NEXT(3);
}
op_multianewarray : {
  SAVE();
  Class* c = resolveClass(t, m->owner, u2(pc + 1));
  if (!c) goto exception;
  int nd = pc[3];
  Object* a = newMultiArray(t, c, sp - nd, nd);
  if (!a) goto exception;
  sp -= nd;
  (sp++)->a = a;
  NEXT(4);
}
op_athrow : {
  Object* o = sp[-1].a;
  if (!o) NPE();
  SAVE();
  t.pending = o;
  goto exception;
}
op_checkcast:
op_instanceof : {
  SAVE();
  Class* c = resolveClass(t, m->owner, u2(pc + 1));
  if (!c) goto exception;
  *pc = *pc == 0xc0 ? OP_CHECKCAST_Q : OP_INSTANCEOF_Q;
  DISPATCH();
}
op_checkcast_q : {
  Object* o = sp[-1].a;
  Class* c = static_cast<Class*>(cp[u2(pc + 1)].resolved);
  if (o && o->cls != c && !assignable(c, o->cls))
    RAISE(raiseCast(*this, t, o->cls, c));
  NEXT(3);
}
op_instanceof_q : {
  Object* o = sp[-1].a;
  Class* c = static_cast<Class*>(cp[u2(pc + 1)].resolved);
  sp[-1].i = (o && (o->cls == c || assignable(c, o->cls))) ? 1 : 0;
  NEXT(3);
}
op_monitorenter : {
  Object* o = sp[-1].a;
  if (!o) NPE();
  if (!enter(t, o)) {
    SAVE();
    t.state = TState::Blocked;
    t.waitOn = o;
    goto out;
  }
  sp--;
  NEXT(1);
}
op_monitorexit : {
  Object* o = sp[-1].a;
  if (!o) NPE();
  if (!exit(t, o)) THROW("java/lang/IllegalMonitorStateException", nullptr);
  sp--;
  NEXT(1);
}
op_wide : {
  uint16_t i = u2(pc + 2);
  switch (pc[1]) {
    case 0x15:
    case 0x17:
    case 0x19:
      *sp++ = lv[i];
      NEXT(4);
    case 0x16:
    case 0x18:
      *sp = lv[i];
      sp += 2;
      NEXT(4);
    case 0x36:
    case 0x38:
    case 0x3a:
      lv[i] = *--sp;
      NEXT(4);
    case 0x37:
    case 0x39:
      sp -= 2;
      lv[i] = *sp;
      NEXT(4);
    case 0x84:
      lv[i].i = (int32_t)((uint32_t)lv[i].i + (uint32_t)(int32_t)s2(pc + 4));
      NEXT(6);
    case 0xa9:
      pc = code + lv[i].i;
      COUNT();
      DISPATCH();
    default:
      THROW("java/lang/Error", "bad wide instruction");
  }
}
op_bad:
  RAISE(raiseBad(*this, t, *pc, m));

  // ---- exceptions: find a handler in this frame, else pop it and look in the caller ----
exception : {
  if (!t.pending) raise(*this, t, "java/lang/Error", "internal: exception without an object");
  for (;;) {
    Object* ex = t.pending;
    uint32_t off = fr->pc;
    int h = -1;
    for (const ExEntry& e : m->ex) {
      if (off < e.start || off >= e.end) continue;
      if (e.type == 0) {
        h = e.handler;
        break;
      }
      Class* ct = static_cast<Class*>(cp[e.type].resolved);
      if (!ct) {
        t.pending = nullptr;
        locals.push_back(ex);  // loading the class may throw (allocate): keep ex alive meanwhile
        ct = resolveClass(t, m->owner, e.type);
        locals.pop_back();
        t.pending = ex;
        if (!ct) continue;
      }
      if (ex->cls == ct || assignable(ct, ex->cls)) {
        h = e.handler;
        break;
      }
    }
    if (h >= 0) {
      t.pending = nullptr;
      sp = lv + m->maxLocals;
      (sp++)->a = ex;
      pc = code + h;
      COUNT();
      DISPATCH();
    }
    if (fr->sync) exit(t, fr->sync);
    t.depth--;
    if (t.depth <= untilDepth) goto out;
    LOAD();
  }
}

out:
  FLUSH();
  return;
#undef COUNT
#undef FLUSH
#undef LOAD
#undef SAVE
#undef DISPATCH
#undef NEXT
#undef CHECK_TIME
#undef BRANCH
#undef THROW
#undef RAISE
#undef NPE
#undef ARRAY
#undef IF1
#undef IF2
#undef MOVE1
}

}  // namespace j2me
