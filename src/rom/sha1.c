#include "rom/sha1.h"

#include <string.h>

static uint32_t rol32(uint32_t v, int n) {
    return (v << n) | (v >> (32 - n));
}

static void sha1_block(sha1_ctx_t* ctx, const uint8_t* p) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)p[i*4] << 24) | ((uint32_t)p[i*4+1] << 16) |
               ((uint32_t)p[i*4+2] << 8) | (uint32_t)p[i*4+3];
    }
    for (int i = 16; i < 80; i++) {
        w[i] = rol32(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
    }
    uint32_t a = ctx->h[0], b = ctx->h[1], c = ctx->h[2], d = ctx->h[3], e = ctx->h[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20)      { f = (b & c) | (~b & d);          k = 0x5A827999; }
        else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
        else             { f = b ^ c ^ d;                   k = 0xCA62C1D6; }
        uint32_t t = rol32(a, 5) + f + e + k + w[i];
        e = d; d = c; c = rol32(b, 30); b = a; a = t;
    }
    ctx->h[0] += a; ctx->h[1] += b; ctx->h[2] += c; ctx->h[3] += d; ctx->h[4] += e;
}

void sha1_init(sha1_ctx_t* ctx) {
    ctx->h[0] = 0x67452301; ctx->h[1] = 0xEFCDAB89; ctx->h[2] = 0x98BADCFE;
    ctx->h[3] = 0x10325476; ctx->h[4] = 0xC3D2E1F0;
    ctx->length = 0;
    ctx->block_len = 0;
}

void sha1_update(sha1_ctx_t* ctx, const void* data, size_t len) {
    const uint8_t* p = (const uint8_t*)data;
    ctx->length += len;
    while (len > 0) {
        size_t n = 64 - ctx->block_len;
        if (n > len) n = len;
        memcpy(ctx->block + ctx->block_len, p, n);
        ctx->block_len += n; p += n; len -= n;
        if (ctx->block_len == 64) {
            sha1_block(ctx, ctx->block);
            ctx->block_len = 0;
        }
    }
}

void sha1_final(sha1_ctx_t* ctx, uint8_t digest[20]) {
    uint64_t bits = ctx->length * 8;
    uint8_t pad = 0x80;
    sha1_update(ctx, &pad, 1);
    pad = 0;
    while (ctx->block_len != 56) {
        sha1_update(ctx, &pad, 1);
    }
    uint8_t len_be[8];
    for (int i = 0; i < 8; i++) {
        len_be[i] = (uint8_t)(bits >> (56 - 8 * i));
    }
    sha1_update(ctx, len_be, 8);
    for (int i = 0; i < 5; i++) {
        digest[i*4]   = (uint8_t)(ctx->h[i] >> 24);
        digest[i*4+1] = (uint8_t)(ctx->h[i] >> 16);
        digest[i*4+2] = (uint8_t)(ctx->h[i] >> 8);
        digest[i*4+3] = (uint8_t)(ctx->h[i]);
    }
}

void sha1_hex(const void* data, size_t len, char out_hex[41]) {
    static const char hexdig[] = "0123456789abcdef";
    sha1_ctx_t ctx;
    uint8_t d[20];
    sha1_init(&ctx);
    sha1_update(&ctx, data, len);
    sha1_final(&ctx, d);
    for (int i = 0; i < 20; i++) {
        out_hex[i*2]   = hexdig[d[i] >> 4];
        out_hex[i*2+1] = hexdig[d[i] & 15];
    }
    out_hex[40] = 0;
}
