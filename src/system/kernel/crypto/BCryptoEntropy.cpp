/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include "BCryptoCore.h"
#include "BCryptoDefs.h"

// funzione kernel che aggiunge entropia
extern void add_entropy(const void* data, size_t size);

status_t
PadLockFeedEntropy()
{
    uint8 buffer[64];
    BCryptoRequest req{};
    iovec vec = { buffer, sizeof(buffer) };

    req.operation = B_CRYPTO_DIGEST;
    req.algorithm = B_CRYPTO_RNG;
    req.destination = &vec;
    req.vectorCount = 1;

    status_t st = BSubmitCryptoRequest(&req);
    if (st != B_OK)
        return st;

    add_entropy(buffer, sizeof(buffer));
    return B_OK;
}
