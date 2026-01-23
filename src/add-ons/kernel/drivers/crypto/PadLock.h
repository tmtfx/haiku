/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef _B_PADLOCK_CRYPTO_H_
#define _B_PADLOCK_CRYPTO_H_

#include "BCryptoAlgorithm.h"
#include "BCryptoDefs.h"

struct PadLockAESContext {
    uint8 key[32];       // max 256-bit key
    size_t keyLength;
    uint8 iv[16];
} __attribute__((aligned(16)));

status_t BInitPadLockCrypto();

#endif

