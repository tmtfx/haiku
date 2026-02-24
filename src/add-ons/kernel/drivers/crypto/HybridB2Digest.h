/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef HYBRID_BLAKE2_DIGEST_H
#define HYBRID_BLAKE2_DIGEST_H

#include "BCryptoCore.h"
#include <immintrin.h>


status_t BInitHybridDigest();

status_t hybrid_blake2_process(BCryptoRequest* request);
/*void soft_sha256_init(SoftSHA256Context* ctx);
void soft_sha256_update(SoftSHA256Context* ctx, const uint8* input, size_t len);
void soft_sha256_finalize(SoftSHA256Context* ctx, uint8 digest[32]);
*/
#endif // HYBRID_BLAKE2_DIGEST_H
