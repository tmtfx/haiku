/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef HYBRID_SHA_DIGEST_H
#define HYBRID_SHA_DIGEST_H

#include "BCryptoCore.h"
#include <immintrin.h>


status_t BInitHybridSHADigest();

status_t hybrid_sha_process(BCryptoRequest* request);
static status_t hybrid_SHA1_init_bridge(void** context, size_t* contextSize);
static status_t hybrid_SHA1_update_bridge(void* context, const iovec* vecs, size_t count);
static status_t hybrid_SHA1_final_bridge(void* context, uint8* outDigest);

static status_t hybrid_SHA224_init_bridge(void** context, size_t* contextSize);
static status_t hybrid_SHA256_init_bridge(void** context, size_t* contextSize);
static status_t hybrid_SHA256_update_bridge(void* context, const iovec* vecs, size_t count);
static status_t hybrid_SHA256_final_bridge(void* context, uint8* outDigest);
static status_t hybrid_SHA224_final_bridge(void* context, uint8* outDigest);

static status_t hybrid_SHA384_init_bridge(void** context, size_t* contextSize);
static status_t hybrid_SHA512_init_bridge(void** context, size_t* contextSize);
static status_t hybrid_SHA512_update_bridge(void* context, const iovec* vecs, size_t count);
static status_t hybrid_SHA512_final_bridge(void* context, uint8* outDigest);
static status_t hybrid_SHA384_final_bridge(void* context, uint8* outDigest);
#endif // HYBRID_BLAKE2_DIGEST_H

