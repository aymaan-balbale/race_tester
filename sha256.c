/*
 * sha256.c — Compact SHA-256 (FIPS 180-4 style padding)
 */

#include "sha256.h"

#include <stdint.h>
#include <string.h>

static uint32_t rotr(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32 - n));
}

static void sha256_transform(uint32_t state[8], const uint8_t block[64]) {
    static const uint32_t K[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
        0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
        0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
        0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
        0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
        0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
        0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
        0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
        0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
        0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
        0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
        0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
    };

    uint32_t W[64];
    uint32_t a, b, c, d, e, f, g, h;
    uint32_t T1, T2;

    for (int i = 0; i < 16; i++) {
        W[i] = ((uint32_t)block[i * 4 + 0] << 24) |
               ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] << 8) |
               ((uint32_t)block[i * 4 + 3]);
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotr(W[i - 15], 7) ^ rotr(W[i - 15], 18) ^ (W[i - 15] >> 3);
        uint32_t s1 = rotr(W[i - 2], 17) ^ rotr(W[i - 2], 19) ^ (W[i - 2] >> 10);
        W[i] = W[i - 16] + s0 + W[i - 7] + s1;
    }

    a = state[0]; b = state[1]; c = state[2]; d = state[3];
    e = state[4]; f = state[5]; g = state[6]; h = state[7];

    for (int i = 0; i < 64; i++) {
        uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        T1 = h + S1 + ch + K[i] + W[i];
        uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        T2 = S0 + maj;
        h = g; g = f; f = e; e = d + T1;
        d = c; c = b; b = a; a = T1 + T2;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

typedef struct {
    uint32_t state[8];
    uint64_t n_bytes;
    uint8_t  buf[64];
    size_t   buflen;
} sha256_ctx;

static void sha256_init(sha256_ctx *ctx) {
    ctx->n_bytes = 0;
    ctx->buflen  = 0;
    ctx->state[0] = 0x6a09e667u;
    ctx->state[1] = 0xbb67ae85u;
    ctx->state[2] = 0x3c6ef372u;
    ctx->state[3] = 0xa54ff53au;
    ctx->state[4] = 0x510e527fu;
    ctx->state[5] = 0x9b05688cu;
    ctx->state[6] = 0x1f83d9abu;
    ctx->state[7] = 0x5be0cd19u;
}

static void sha256_update(sha256_ctx *ctx, const uint8_t *data, size_t len) {
    ctx->n_bytes += (uint64_t)len;

    while (len > 0) {
        size_t space = 64 - ctx->buflen;
        size_t copy = len < space ? len : space;
        memcpy(ctx->buf + ctx->buflen, data, copy);
        ctx->buflen += copy;
        data += copy;
        len -= copy;
        if (ctx->buflen == 64) {
            sha256_transform(ctx->state, ctx->buf);
            ctx->buflen = 0;
        }
    }
}

void sha256_hex_digest(const unsigned char *data, size_t len, char out_hex[65]) {
    sha256_ctx ctx;
    uint8_t    digest[32];

    sha256_init(&ctx);
    sha256_update(&ctx, data, len);

    uint64_t bitlen = ctx.n_bytes * 8;

    /* Padding: 0x80 then zeros until the block ends 8 bytes before full */
    ctx.buf[ctx.buflen++] = 0x80;
    if (ctx.buflen == 64) {
        sha256_transform(ctx.state, ctx.buf);
        ctx.buflen = 0;
    }
    while (ctx.buflen != 56) {
        if (ctx.buflen == 64) {
            sha256_transform(ctx.state, ctx.buf);
            ctx.buflen = 0;
        }
        ctx.buf[ctx.buflen++] = 0;
    }

    for (int i = 0; i < 8; i++)
        ctx.buf[56 + i] = (uint8_t)(bitlen >> (56 - i * 8));

    sha256_transform(ctx.state, ctx.buf);

    for (int i = 0; i < 8; i++) {
        digest[i * 4 + 0] = (uint8_t)(ctx.state[i] >> 24);
        digest[i * 4 + 1] = (uint8_t)(ctx.state[i] >> 16);
        digest[i * 4 + 2] = (uint8_t)(ctx.state[i] >> 8);
        digest[i * 4 + 3] = (uint8_t)(ctx.state[i]);
    }

    static const char *hex = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        out_hex[i * 2 + 0] = hex[digest[i] >> 4];
        out_hex[i * 2 + 1] = hex[digest[i] & 0xf];
    }
    out_hex[64] = '\0';
}
