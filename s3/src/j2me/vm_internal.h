#pragma once
// Internals shared by the VM's files (src/j2me/vm_*.cpp). Not for natives: they use jvm.h.
#include <cmath>
#include <cstdint>
#include <cstring>

#include "jvm.h"
#include "vm_inflate.h"

namespace j2me {

enum : int { CLS_LOADED = 0, CLS_LINKED = 1, CLS_INITING = 2, CLS_READY = 3, CLS_FAILED = 4 };

enum : uint8_t {
  CP_Utf8 = 1,
  CP_Integer = 3,
  CP_Float = 4,
  CP_Long = 5,
  CP_Double = 6,
  CP_Class = 7,
  CP_String = 8,
  CP_Fieldref = 9,
  CP_Methodref = 10,
  CP_IMethodref = 11,
  CP_NameAndType = 12,
  CP_MethodHandle = 15,
  CP_MethodType = 16,
  CP_Dynamic = 17,
  CP_InvokeDynamic = 18,
  CP_Module = 19,
  CP_Package = 20,
};

enum : uint16_t {
  ACC_PUBLIC = 0x0001,
  ACC_PRIVATE = 0x0002,
  ACC_PROTECTED = 0x0004,
  ACC_STATIC = 0x0008,
  ACC_FINAL = 0x0010,
  ACC_SYNCHRONIZED = 0x0020,
  ACC_SUPER = 0x0020,
  ACC_NATIVE = 0x0100,
  ACC_INTERFACE = 0x0200,
  ACC_ABSTRACT = 0x0400,
};

// Opcodes the interpreter rewrites resolved instructions into (free numbers after jsr_w = 0xc9).
enum : uint8_t {
  OP_INVOKEVIRTUAL = 0xb6,
  OP_INVOKESPECIAL = 0xb7,
  OP_INVOKESTATIC = 0xb8,
  OP_INVOKEINTERFACE = 0xb9,
  OP_GETFIELD_Q = 0xcb,     // u2 slot
  OP_GETFIELD_Q2 = 0xcc,    // u2 slot, long/double
  OP_PUTFIELD_Q = 0xcd,
  OP_PUTFIELD_Q2 = 0xce,
  OP_GETSTATIC_Q = 0xcf,    // u2 cp (resolved Field*)
  OP_GETSTATIC_Q2 = 0xd0,
  OP_PUTSTATIC_Q = 0xd1,
  OP_PUTSTATIC_Q2 = 0xd2,
  OP_INVOKEVIRTUAL_Q = 0xd3,    // u2 cp (resolved Method*, extra = vtable index)
  OP_INVOKEDIRECT_Q = 0xd4,     // u2 index in Vm::direct
  OP_INVOKEINTERFACE_Q = 0xd5,  // u2 cp (resolved interface Method*), count, 0
  OP_NEW_Q = 0xd6,              // u2 cp (resolved Class*, initialized)
  OP_CHECKCAST_Q = 0xd7,
  OP_INSTANCEOF_Q = 0xd8,
  OP_ANEWARRAY_Q = 0xd9,        // u2 cp (resolved: the array class)
  OP_INVOKEDIRECT_QS = 0xda,    // static methods (no receiver to check)
};

// Room kept free at the top of every Java stack so that a StackOverflowError (or an exception made by a native)
// can still be constructed: the interpreter's limit is this much below the stack's real end.
static const int kStackReserve = 512;
static const int kFrameReserve = 24;

inline uint16_t u2(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
inline int16_t s2(const uint8_t* p) { return (int16_t)u2(p); }
inline uint32_t u4(const uint8_t* p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
inline int32_t s4(const uint8_t* p) { return (int32_t)u4(p); }
inline uint16_t le2(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
inline uint32_t le4(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

inline int elemSize(char e) {
  switch (e) {
    case 'Z':
    case 'B':
      return 1;
    case 'C':
    case 'S':
      return 2;
    case 'I':
    case 'F':
      return 4;
    case 'J':
    case 'D':
      return 8;
    default:
      return (int)sizeof(Object*);
  }
}

inline uint32_t identityHash(const Object* o) {
  uintptr_t v = reinterpret_cast<uintptr_t>(o) >> 3;
  uint32_t h = (uint32_t)v ^ (uint32_t)((uint64_t)v >> 32);
  h *= 2654435761u;
  return (h ^ (h >> 15)) & 0x7fffffff;
}

// Java's float/double to int/long conversions (NaN 0, saturating).
inline int32_t f2i(double d) {
  if (d != d) return 0;
  if (d >= 2147483647.0) return 0x7fffffff;
  if (d <= -2147483648.0) return (int32_t)0x80000000u;
  return (int32_t)d;
}
inline int64_t f2l(double d) {
  if (d != d) return 0;
  if (d >= 9223372036854775807.0) return INT64_MAX;
  if (d <= -9223372036854775808.0) return INT64_MIN;
  return (int64_t)d;
}

// UTF-8 (also Java's modified UTF-8) to UTF-16, and back.
void utf8To16(const char* s, size_t n, PStr16& out);
std::string utf16To8(const char16_t* s, size_t n);
std::string javaName(Sym internal);  // "java/lang/String" -> "java.lang.String"


// Descriptors
bool parseMethodDesc(const char* d, int& argSlots, int& argCount, char& ret, uint32_t& wideArgs);

}  // namespace j2me
