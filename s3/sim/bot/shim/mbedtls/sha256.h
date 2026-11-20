#pragma once
// mbedTLS's SHA-256 on the computer: CommonCrypto.
#include <CommonCrypto/CommonDigest.h>
#include <cstddef>
typedef CC_SHA256_CTX mbedtls_sha256_context;
inline int mbedtls_sha256(const unsigned char* in, size_t n, unsigned char out[32], int) {
  CC_SHA256(in, (CC_LONG)n, out);
  return 0;
}
inline void mbedtls_sha256_init(mbedtls_sha256_context*) {}
inline void mbedtls_sha256_free(mbedtls_sha256_context*) {}
inline int mbedtls_sha256_starts(mbedtls_sha256_context* c, int) { return CC_SHA256_Init(c), 0; }
inline int mbedtls_sha256_update(mbedtls_sha256_context* c, const unsigned char* p, size_t n) {
  return CC_SHA256_Update(c, p, (CC_LONG)n), 0;
}
inline int mbedtls_sha256_finish(mbedtls_sha256_context* c, unsigned char out[32]) { return CC_SHA256_Final(out, c), 0; }
