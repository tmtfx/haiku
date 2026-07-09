/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef SOFT_CRYPTO_ENGINES_H
#define SOFT_CRYPTO_ENGINES_H

#include "SoftCryptoPriv.h"

// Motore GCM
void soft_ghash_update(GCMState* state, const uint8* data, size_t len);
void soft_ghash_multiply(uint8* x, const uint8* h);

// Motore AES
void soft_aes_ctr_update(SoftAESContext* ctx, uint8* counter, const uint8* in, uint8* out, size_t len);

#endif
