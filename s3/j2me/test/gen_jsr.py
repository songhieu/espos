#!/usr/bin/env python3
# Writes Jsr.class: try/finally compiled the way javac 1.1-1.4 did it (jsr/ret subroutines; also jsr_w and
# wide astore/ret), class file version 45.3. JDK 17's javac cannot emit these, old games are full of them.
#   static int counter;
#   static int f(int x) { int r = 0; try { r = 10 / x; return r; } finally { counter++; } }   // jsr, ret
#   static int g(int x) { same, with jsr_w and wide astore/ret }
import struct, sys

out = sys.argv[1] if len(sys.argv) > 1 else "Jsr.class"
cp = []
def utf8(s): cp.append(b"\x01" + struct.pack(">H", len(s)) + s.encode()); return len(cp)
def cls(n): cp.append(b"\x07" + struct.pack(">H", n)); return len(cp)
def nat(n, d): cp.append(b"\x0c" + struct.pack(">HH", n, d)); return len(cp)
def fref(c, nt): cp.append(b"\x09" + struct.pack(">HH", c, nt)); return len(cp)
def mref(c, nt): cp.append(b"\x0a" + struct.pack(">HH", c, nt)); return len(cp)

this = cls(utf8("Jsr"))
obj = cls(utf8("java/lang/Object"))
counter_n, int_d = utf8("counter"), utf8("I")
counter = fref(this, nat(counter_n, int_d))
f_n, ii_d, code_n, g_n = utf8("f"), utf8("(I)I"), utf8("Code"), utf8("g")
init_n, v_d = utf8("<init>"), utf8("()V")
objinit = mref(obj, nat(init_n, v_d))

def code_attr(max_stack, max_locals, code, ex):
    body = struct.pack(">HHI", max_stack, max_locals, len(code)) + code
    body += struct.pack(">H", len(ex)) + b"".join(struct.pack(">HHHH", *e) for e in ex)
    body += struct.pack(">H", 0)
    return struct.pack(">HI", code_n, len(body)) + body

def method(flags, name, desc, attr):
    return struct.pack(">HHHH", flags, name, desc, 1) + attr

hi, lo = counter >> 8, counter & 255
f_code = bytes([
    0x03, 0x3c,              # 0 iconst_0, istore_1
    0x10, 10, 0x1a, 0x6c, 0x3c,  # 2 bipush 10, iload_0, idiv, istore_1
    0x1b, 0x3d,              # 7 iload_1, istore_2
    0xa8, 0x00, 12,          # 9 jsr 21
    0x1c, 0xac,              # 12 iload_2, ireturn
    0x4e,                    # 14 astore_3
    0xa8, 0x00, 6,           # 15 jsr 21
    0x2d, 0xbf,              # 18 aload_3, athrow
    0x00,                    # 20 nop
    0x3a, 4,                 # 21 astore 4
    0xb2, hi, lo,            # 23 getstatic counter
    0x04, 0x60,              # 26 iconst_1, iadd
    0xb3, hi, lo,            # 28 putstatic counter
    0xa9, 4,                 # 31 ret 4
])
g_code = bytes([
    0x03, 0x3c,
    0x10, 10, 0x1a, 0x6c, 0x3c,
    0x1b, 0x3d,                       # 7
    0xc9, 0, 0, 0, 15,                # 9 jsr_w 24
    0x1c, 0xac,                       # 14
    0x4e,                             # 16 astore_3
    0xc9, 0, 0, 0, 7,                 # 17 jsr_w 24
    0x2d, 0xbf,                       # 22
    0xc4, 0x3a, 0, 4,                 # 24 wide astore 4
    0xb2, hi, lo,                     # 28
    0x04, 0x60,                       # 31
    0xb3, hi, lo,                     # 33
    0xc4, 0xa9, 0, 4,                 # 36 wide ret 4
])
methods = [
    method(0x0001, init_n, v_d, code_attr(1, 1, bytes([0x2a, 0xb7, objinit >> 8, objinit & 255, 0xb1]), [])),
    method(0x0009, f_n, ii_d, code_attr(2, 5, f_code, [(2, 12, 14, 0)])),
    method(0x0009, g_n, ii_d, code_attr(2, 5, g_code, [(2, 14, 16, 0)])),
]
data = struct.pack(">IHH", 0xCAFEBABE, 3, 45)
data += struct.pack(">H", len(cp) + 1) + b"".join(cp)
data += struct.pack(">HHHH", 0x0021, this, obj, 0)
data += struct.pack(">H", 1) + struct.pack(">HHHH", 0x0009, counter_n, int_d, 0)
data += struct.pack(">H", len(methods)) + b"".join(methods)
data += struct.pack(">H", 0)
open(out, "wb").write(data)
