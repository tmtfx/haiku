/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef _SOFT_SHA_H_
#define _SOFT_SHA_H_

#include <SupportDefs.h>
/*
struct SoftSHA256Context {
    uint32 state[8];
    uint64 count;
    uint8  buffer[64];
};*/
struct SoftSHA1Context {
    uint32 state[5] __attribute__((aligned(16)));
    uint64 count;
    uint8  buffer[64] __attribute__((aligned(16)));
};

// SHA-256 (e 224) Context
struct SoftSHA256Context {
    uint32 state[8] __attribute__((aligned(16)));
    uint64 count;
    uint8  buffer[64] __attribute__((aligned(16)));
};
typedef struct SoftSHA256Context SoftSHA224Context;

// SHA-512 (e 384) Context
// Qui usiamo uint64 e allineamento a 32 per AVX2
struct SoftSHA512Context {
    uint64 state[8] __attribute__((aligned(32)));
    uint64 count[2];
    uint8  buffer[128] __attribute__((aligned(32)));
};
typedef struct SoftSHA512Context SoftSHA384Context;

void soft_sha256_init(SoftSHA256Context* ctx);
void soft_sha256_update(SoftSHA256Context* ctx, const uint8* input, size_t len);
void soft_sha256_finalize(SoftSHA256Context* ctx, uint8 digest[32]);

#endif
