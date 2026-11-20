#pragma once
#include <cstddef>
#include <cstring>
// mbedtls_base64_decode on the computer (the X-Assist header).
inline int mbedtls_base64_decode(unsigned char* dst, size_t dlen, size_t* olen, const unsigned char* src, size_t slen) {
  auto v = [](unsigned char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    return c == '+' ? 62 : c == '/' ? 63 : -1;
  };
  size_t o = 0;
  unsigned acc = 0;
  int bits = 0;
  for (size_t i = 0; i < slen; i++) {
    const int x = v(src[i]);
    if (x < 0) continue;  // '=' and white space
    acc = acc << 6 | x, bits += 6;
    if (bits >= 8) {
      bits -= 8;
      if (o >= dlen) return -1;
      dst[o++] = (unsigned char)(acc >> bits);
    }
  }
  *olen = o;
  return 0;
}
