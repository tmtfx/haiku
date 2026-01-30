/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef _SOFT_SHA_H_
#define _SOFT_SHA_H_

#include <SupportDefs.h>

struct SoftSHA256Context {
    uint32 state[8];
    uint64 count;
    uint8  buffer[64];
};

void soft_sha256_init(SoftSHA256Context* ctx);
void soft_sha256_update(SoftSHA256Context* ctx, const uint8* input, size_t len);
void soft_sha256_finalize(SoftSHA256Context* ctx, uint8 digest[32]);

#endif
