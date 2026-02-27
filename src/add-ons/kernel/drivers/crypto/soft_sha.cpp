/*
 * Software SHA-256 core for Haiku kernel crypto
 * Copyright 2026, Fabio Tomat
 * MIT License
 */

#include "soft_sha.h"
#include <string.h>

// Macros standard per SHA-256
#define ROTR(x, n) ((x >> n) | (x << (32 - n)))
#define Ch(x, y, z)  ((x & y) ^ (~x & z))
#define Maj(x, y, z) ((x & y) ^ (x & z) ^ (y & z))
#define S0(x) (ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define S1(x) (ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define s0(x) (ROTR(x, 7) ^ ROTR(x, 18) ^ (x >> 3))
#define s1(x) (ROTR(x, 17) ^ ROTR(x, 19) ^ (x >> 10))

static const uint32 K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static void sha256_transform(SoftSHA256Context* ctx, const uint8* data) {
    uint32 a, b, c, d, e, f, g, h, i, t1, t2, W[64];

    for (i = 0; i < 16; i++) {
        W[i] = (data[i*4] << 24) | (data[i*4+1] << 16) | (data[i*4+2] << 8) | (data[i*4+3]);
    }
    for (i = 16; i < 64; i++) {
        W[i] = s1(W[i-2]) + W[i-7] + s0(W[i-15]) + W[i-16];
    }

    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];

    for (i = 0; i < 64; i++) {
        t1 = h + S1(e) + Ch(e, f, g) + K[i] + W[i];
        t2 = S0(a) + Maj(a, b, c);
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }

    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

void soft_sha256_init(SoftSHA256Context* ctx) {
    ctx->state[0] = 0x6a09e667; ctx->state[1] = 0xbb67ae85; ctx->state[2] = 0x3c6ef372; ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f; ctx->state[5] = 0x9b05688c; ctx->state[6] = 0x1f83d9ab; ctx->state[7] = 0x5be0cd19;
    ctx->count = 0;
}

void soft_sha256_update(SoftSHA256Context* ctx, const uint8* input, size_t len) {
    size_t i, index, partLen;
    index = (size_t)((ctx->count >> 3) & 0x3F);
    ctx->count += (uint64)len << 3;
    partLen = 64 - index;

    if (len >= partLen) {
        memcpy(&ctx->buffer[index], input, partLen);
        sha256_transform(ctx, ctx->buffer);
        for (i = partLen; i + 63 < len; i += 64)
            sha256_transform(ctx, &input[i]);
        index = 0;
    } else i = 0;
    
    memcpy(&ctx->buffer[index], &input[i], len - i);
}

void soft_sha256_finalize(SoftSHA256Context* ctx, uint8 digest[32]) {
    uint8 bits[8];
    uint32 index, padLen;
    static uint8 padding[64] = { 0x80, 0 /* , ... */ };

    for (int i = 0; i < 8; i++) bits[i] = (uint8)(ctx->count >> (56 - i * 8));
    
    index = (uint32)((ctx->count >> 3) & 0x3f);
    padLen = (index < 56) ? (56 - index) : (120 - index);
    soft_sha256_update(ctx, padding, padLen);
    soft_sha256_update(ctx, bits, 8);

    for (int i = 0; i < 8; i++) {
        digest[i*4]   = (uint8)(ctx->state[i] >> 24);
        digest[i*4+1] = (uint8)(ctx->state[i] >> 16);
        digest[i*4+2] = (uint8)(ctx->state[i] >> 8);
        digest[i*4+3] = (uint8)(ctx->state[i]);
    }
}



/* --- SHA-1 --- */
void soft_sha1_init(SoftSHA1Context* ctx) {
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xEFCDAB89;
    ctx->state[2] = 0x98BADCFE;
    ctx->state[3] = 0x10325476;
    ctx->state[4] = 0xC3D2E1F0;
    ctx->count = 0;
}


void soft_sha224_init(SoftSHA256Context* ctx) {
    // SHA-224 usa IV diversi da SHA-256
    ctx->state[0] = 0xc1059ed8; ctx->state[1] = 0x367cd507;
    ctx->state[2] = 0x3070dd17; ctx->state[3] = 0xf70e5939;
    ctx->state[4] = 0xffc00b31; ctx->state[5] = 0x68581511;
    ctx->state[6] = 0x64f98fa7; ctx->state[7] = 0xbefa4fa4;
    ctx->count = 0;
}
/* --- SHA-512 --- */
void soft_sha512_init(SoftSHA512Context* ctx) {
    ctx->state[0] = 0x6a09e667f3bcc908ULL;
    ctx->state[1] = 0xbb67ae8584caa73bULL;
    ctx->state[2] = 0x3c6ef372fe94f82bULL;
    ctx->state[3] = 0xa54ff53a5f1d36f1ULL;
    ctx->state[4] = 0x510e527fad6849adbULL;
    ctx->state[5] = 0x9b05688c2b3e6c1fULL;
    ctx->state[6] = 0x1f83d9abfb41bd6bULL;
    ctx->state[7] = 0x5be0cd19137e2179ULL;
    ctx->count[0] = 0; // SHA-512 usa 128 bit per il conteggio
    ctx->count[1] = 0;
}

/* --- SHA-384 --- */
void soft_sha384_init(SoftSHA512Context* ctx) {
    // SHA-384 usa la stessa struttura di SHA-512 ma IV diversi
    ctx->state[0] = 0xcbbb9d5dc1059ed8ULL;
    ctx->state[1] = 0x629a292a367cd507ULL;
    ctx->state[2] = 0x9159015a3070dd17ULL;
    ctx->state[3] = 0x152fecd8f70e5939ULL;
    ctx->state[4] = 0x67332667ffc00b31ULL;
    ctx->state[5] = 0x8eb44a8768581511ULL;
    ctx->state[6] = 0xdb0c2e0d64f98fa7ULL;
    ctx->state[7] = 0x47b5481dbefa4fa4ULL;
    ctx->count[0] = 0;
    ctx->count[1] = 0;
}
