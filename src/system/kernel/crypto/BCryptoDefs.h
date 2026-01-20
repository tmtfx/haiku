/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef _B_CRYPTO_DEFS_H_
#define _B_CRYPTO_DEFS_H_


#include <SupportDefs.h>

enum BCryptoAlgorithmID {
    B_CRYPTO_AES_CBC      = 0x0001,
    B_CRYPTO_AES_GCM      = 0x0002,
    B_CRYPTO_SHA1         = 0x0010,
    B_CRYPTO_SHA256       = 0x0011,
    B_CRYPTO_SHA512       = 0x0012,
    B_CRYPTO_RNG          = 0x0020
};

enum BCryptoOperation {
    B_CRYPTO_ENCRYPT = 1,
    B_CRYPTO_DECRYPT,
    B_CRYPTO_DIGEST
};

enum {
    B_CRYPTO_HW_ACCEL    = 0x01,
    B_CRYPTO_SOFTWARE    = 0x02,
    B_CRYPTO_ASYNC       = 0x04
};


#endif
