// Raw deflate decoding (vm_inflate.h). Canonical Huffman codes as zlib's "puff" builds them (count[len] codes of
// each length, symbols in code order), plus a 512-entry table that decodes codes of up to 9 bits in one step (most
// literals); longer codes go bit by bit. The tables live in PSRAM for the call (no static or big stack buffers).
#include "vm_inflate.h"

#include <algorithm>

#include "vm_internal.h"

namespace j2me {

namespace {

struct Huff {
  uint16_t count[16];
  uint16_t symbol[320];
  uint16_t fast[512];  // the next 9 input bits -> (length << 9) | symbol; 0: a longer code
};

struct Tables {
  Huff lit, dist;
  uint16_t lengths[320];
};

struct Inflater {
  const uint8_t* in;
  size_t inLen, inPos = 0;
  uint32_t bitbuf = 0;
  int bitcnt = 0;
  uint8_t* out = nullptr;
  size_t outLen = 0, outPos = 0;
  void* sink = nullptr;  // growable output: grow(sink, want) resizes it and returns its data
  uint8_t* (*grow)(void*, size_t) = nullptr;
  Tables* tab = nullptr;
  bool bad = false;

  bool room(size_t n) {
    if (outPos + n <= outLen) return true;
    if (!grow) return false;
    size_t want = std::max(outPos + n, outLen * 2 + 4096);
    out = grow(sink, want);
    if (!out) return false;
    outLen = want;
    return true;
  }

  void need(int n) {
    while (bitcnt < n && inPos < inLen) {
      bitbuf |= (uint32_t)in[inPos++] << bitcnt;
      bitcnt += 8;
    }
  }

  int bits(int n) {  // n <= 16
    if (n == 0) return 0;
    need(n);
    if (bitcnt < n) {
      bad = true;
      return 0;
    }
    int v = (int)(bitbuf & ((1u << n) - 1));
    bitbuf >>= n;
    bitcnt -= n;
    return v;
  }

  int decode(const Huff& h) {
    need(9);
    uint16_t e = h.fast[bitbuf & 511];
    if (e && (e >> 9) <= bitcnt) {
      bitbuf >>= e >> 9;
      bitcnt -= e >> 9;
      return e & 511;
    }
    int code = 0, first = 0, index = 0;  // a code longer than 9 bits (or the input's very end): bit by bit
    for (int len = 1; len <= 15; len++) {
      if (bitcnt == 0) {
        need(1);
        if (bitcnt == 0) {
          bad = true;
          return -1;
        }
      }
      code |= (int)(bitbuf & 1);
      bitbuf >>= 1;
      bitcnt--;
      int count = h.count[len];
      if (code - count < first) return h.symbol[index + (code - first)];
      index += count;
      first += count;
      first <<= 1;
      code <<= 1;
    }
    bad = true;
    return -1;
  }

  // <0: over-subscribed; 0: complete; >0: incomplete
  static int construct(Huff& h, const uint16_t* length, int n) {
    for (int len = 0; len < 16; len++) h.count[len] = 0;
    for (int s = 0; s < n; s++) h.count[length[s]]++;
    std::memset(h.fast, 0, sizeof h.fast);
    if (h.count[0] == n) return 0;
    int left = 1;
    for (int len = 1; len < 16; len++) {
      left <<= 1;
      left -= h.count[len];
      if (left < 0) return left;
    }
    uint16_t offs[16];
    offs[1] = 0;
    for (int len = 1; len < 15; len++) offs[len + 1] = (uint16_t)(offs[len] + h.count[len]);
    for (int s = 0; s < n; s++)
      if (length[s] != 0) h.symbol[offs[length[s]]++] = (uint16_t)s;
    // the one-step table: each code of up to 9 bits, bit-reversed (deflate sends codes from their top bit)
    int code = 0, idx = 0;
    for (int len = 1; len <= 9; len++) {
      for (int k = 0; k < h.count[len]; k++, idx++, code++) {
        int rev = 0;
        for (int b = 0; b < len; b++) rev |= ((code >> b) & 1) << (len - 1 - b);
        for (int r = rev; r < 512; r += 1 << len) h.fast[r] = (uint16_t)((len << 9) | h.symbol[idx]);
      }
      code <<= 1;
    }
    return left;
  }

  bool stored() {
    // to the byte boundary, and give back the whole bytes already in the bit buffer
    bitbuf >>= bitcnt & 7;
    bitcnt -= bitcnt & 7;
    inPos -= (size_t)(bitcnt / 8);
    bitbuf = 0;
    bitcnt = 0;
    if (inPos + 4 > inLen) return false;
    unsigned len = in[inPos] | (in[inPos + 1] << 8);
    unsigned nlen = in[inPos + 2] | (in[inPos + 3] << 8);
    inPos += 4;
    if (len != (~nlen & 0xffffu) || inPos + len > inLen || !room(len)) return false;
    if (len) std::memcpy(out + outPos, in + inPos, len);
    inPos += len;
    outPos += len;
    return true;
  }

  bool codes(const Huff& lit, const Huff& dist) {
    static const uint16_t lbase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                       31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const uint8_t lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const uint16_t dbase[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                       193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    static const uint8_t dext[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    for (;;) {
      int sym = decode(lit);
      if (sym < 0) return false;
      if (sym < 256) {
        if (!room(1)) return false;
        out[outPos++] = (uint8_t)sym;
      } else if (sym == 256) {
        return true;
      } else {
        sym -= 257;
        if (sym >= 29) return false;
        size_t len = lbase[sym] + (size_t)bits(lext[sym]);
        int ds = decode(dist);
        if (bad || ds < 0 || ds >= 30) return false;
        size_t d = dbase[ds] + (size_t)bits(dext[ds]);
        if (bad || d > outPos || !room(len)) return false;
        uint8_t* o = out + outPos;
        const uint8_t* s = o - d;
        for (size_t i = 0; i < len; i++) o[i] = s[i];
        outPos += len;
      }
    }
  }

  bool fixed() {
    uint16_t* l = tab->lengths;
    int s = 0;
    for (; s < 144; s++) l[s] = 8;
    for (; s < 256; s++) l[s] = 9;
    for (; s < 280; s++) l[s] = 7;
    for (; s < 288; s++) l[s] = 8;
    construct(tab->lit, l, 288);
    for (s = 0; s < 30; s++) l[s] = 5;
    construct(tab->dist, l, 30);
    return codes(tab->lit, tab->dist);
  }

  bool dynamic() {
    static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    uint16_t* lengths = tab->lengths;
    int nlen = bits(5) + 257, ndist = bits(5) + 1, ncode = bits(4) + 4;
    if (bad || nlen > 286 || ndist > 30) return false;
    int index = 0;
    for (; index < ncode; index++) lengths[order[index]] = (uint16_t)bits(3);
    for (; index < 19; index++) lengths[order[index]] = 0;
    if (bad || construct(tab->lit, lengths, 19) != 0) return false;  // the code-length code (must be complete)
    index = 0;
    while (index < nlen + ndist) {
      int sym = decode(tab->lit);
      if (sym < 0) return false;
      if (sym < 16) {
        lengths[index++] = (uint16_t)sym;
      } else {
        uint16_t len = 0;
        if (sym == 16) {
          if (index == 0) return false;
          len = lengths[index - 1];
          sym = 3 + bits(2);
        } else if (sym == 17) {
          sym = 3 + bits(3);
        } else {
          sym = 11 + bits(7);
        }
        if (bad || index + sym > nlen + ndist) return false;
        while (sym--) lengths[index++] = len;
      }
    }
    if (lengths[256] == 0) return false;
    int err = construct(tab->lit, lengths, nlen);
    if (err < 0 || (err > 0 && nlen - tab->lit.count[0] != 1)) return false;
    err = construct(tab->dist, lengths + nlen, ndist);
    if (err < 0 || (err > 0 && ndist - tab->dist.count[0] != 1)) return false;
    return codes(tab->lit, tab->dist);
  }

  bool run() {
    tab = static_cast<Tables*>(vmMalloc(sizeof(Tables)));
    if (!tab) return false;
    bool ok = true;
    int last;
    do {
      last = bits(1);
      int type = bits(2);
      if (bad) {
        ok = false;
        break;
      }
      ok = type == 0 ? stored() : type == 1 ? fixed() : type == 2 ? dynamic() : false;
      if (!ok || bad) {
        ok = false;
        break;
      }
    } while (!last);
    vmFree(tab);
    return ok;
  }
};

template <class V>
bool inflateInto(const uint8_t* src, size_t len, V& out, size_t sizeHint) {
  Inflater f;
  f.in = src;
  f.inLen = len;
  out.clear();
  out.resize(sizeHint ? sizeHint : len * 4 + 64);
  f.out = out.data();
  f.outLen = out.size();
  f.sink = &out;
  f.grow = [](void* s, size_t want) -> uint8_t* {
    V* v = static_cast<V*>(s);
    v->resize(want);
    return v->data();
  };
  bool ok = f.run();
  out.resize(ok ? f.outPos : 0);
  return ok;
}

}  // namespace

bool inflateRaw(const uint8_t* src, size_t len, uint8_t* out, size_t outLen) {
  Inflater f;
  f.in = src;
  f.inLen = len;
  f.out = out;
  f.outLen = outLen;
  return f.run() && f.outPos == outLen;
}

bool inflateRaw(const uint8_t* src, size_t len, std::vector<uint8_t>& out, size_t sizeHint) {
  return inflateInto(src, len, out, sizeHint);
}

bool inflateRaw(const uint8_t* src, size_t len, PVec<uint8_t>& out, size_t sizeHint) {
  return inflateInto(src, len, out, sizeHint);
}

}  // namespace j2me
