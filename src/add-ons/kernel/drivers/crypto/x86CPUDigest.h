/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef X86_CPU_DIGEST_H
#define X86_CPU_DIGEST_H

#include "BCryptoCore.h"
#include <immintrin.h>

struct x86_sha256_context {
    alignas(16) __m128i state0;     // Contiene DCBA
    alignas(16) __m128i state1;     // Contiene HGFE
    alignas(16) BCryptoFPUContext fpu_save;
    uint8   buffer[64];
    size_t  buffer_len;
    uint64  total_len;
} __attribute__((aligned(16)));

status_t BInitx86CPUDigest();

status_t x86_sha256_process(BCryptoRequest* request);
/*void soft_sha256_init(SoftSHA256Context* ctx);
void soft_sha256_update(SoftSHA256Context* ctx, const uint8* input, size_t len);
void soft_sha256_finalize(SoftSHA256Context* ctx, uint8 digest[32]);
*/
#endif // X86_CPU_DIGEST_H
