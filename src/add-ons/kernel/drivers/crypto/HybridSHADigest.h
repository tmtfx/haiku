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

#endif // HYBRID_BLAKE2_DIGEST_H

