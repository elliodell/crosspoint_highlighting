#pragma once
#include <cstddef>

// Use host crypto for the same digest bytes as the device's mbedTLS library.
#if defined(__APPLE__)
#include <CommonCrypto/CommonDigest.h>
using mbedtls_sha256_context = CC_SHA256_CTX;
inline void mbedtls_sha256_init(mbedtls_sha256_context*) {}
inline void mbedtls_sha256_free(mbedtls_sha256_context*) {}
inline int mbedtls_sha256_starts(mbedtls_sha256_context* ctx, int) { return CC_SHA256_Init(ctx) == 1 ? 0 : -1; }
inline int mbedtls_sha256_update(mbedtls_sha256_context* ctx, const unsigned char* bytes, size_t size) {
  return CC_SHA256_Update(ctx, bytes, static_cast<CC_LONG>(size)) == 1 ? 0 : -1;
}
inline int mbedtls_sha256_finish(mbedtls_sha256_context* ctx, unsigned char* output) {
  return CC_SHA256_Final(output, ctx) == 1 ? 0 : -1;
}
#else
#include <openssl/sha.h>
using mbedtls_sha256_context = SHA256_CTX;
inline void mbedtls_sha256_init(mbedtls_sha256_context*) {}
inline void mbedtls_sha256_free(mbedtls_sha256_context*) {}
inline int mbedtls_sha256_starts(mbedtls_sha256_context* ctx, int) { return SHA256_Init(ctx) == 1 ? 0 : -1; }
inline int mbedtls_sha256_update(mbedtls_sha256_context* ctx, const unsigned char* bytes, size_t size) {
  return SHA256_Update(ctx, bytes, size) == 1 ? 0 : -1;
}
inline int mbedtls_sha256_finish(mbedtls_sha256_context* ctx, unsigned char* output) {
  return SHA256_Final(output, ctx) == 1 ? 0 : -1;
}
#endif
