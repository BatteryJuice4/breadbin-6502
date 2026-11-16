#pragma once
// Minimal SHA-1 (FIPS 180-4). Used for ROM identification only, not for security.
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t h[5];
    uint64_t length;        // total bytes
    uint8_t block[64];
    size_t block_len;
} sha1_ctx_t;

void sha1_init(sha1_ctx_t* ctx);
void sha1_update(sha1_ctx_t* ctx, const void* data, size_t len);
void sha1_final(sha1_ctx_t* ctx, uint8_t digest[20]);

// Convenience: hash a buffer into a lowercase 40-char hex string (+ NUL).
void sha1_hex(const void* data, size_t len, char out_hex[41]);
