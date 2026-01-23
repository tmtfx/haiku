/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef _B_CRYPTO_DEFS_H_
#define _B_CRYPTO_DEFS_H_

#include <Drivers.h>
#include <SupportDefs.h>
#include <Errors.h>
#ifndef B_PENDING
#   define B_PENDING B_DEV_PENDING
#endif

enum BCryptoAlgorithmID {
    // Cifrari simmetrici
    B_CRYPTO_AES        = 0x0001,

    // Hash
    B_CRYPTO_SHA1       = 0x0010,
    B_CRYPTO_SHA256     = 0x0011,
    B_CRYPTO_SHA512     = 0x0012,

    // RNG
    B_CRYPTO_RNG        = 0x0020
};

enum BCryptoMode {
    B_CRYPTO_MODE_NONE = 0x00,

    // AES modes
    B_CRYPTO_MODE_ECB  = 0x01,
    B_CRYPTO_MODE_CBC  = 0x02,
    B_CRYPTO_MODE_CTR  = 0x03,
    B_CRYPTO_MODE_GCM  = 0x04
};

enum BCryptoOperation {
    B_CRYPTO_ENCRYPT = 1,
    B_CRYPTO_DECRYPT,
    B_CRYPTO_DIGEST
};

enum {
    B_CRYPTO_ALG_HW_ACCEL    = 0x01,
    B_CRYPTO_ALG_SOFTWARE    = 0x02,
    B_CRYPTO_ALG_ASYNC       = 0x04
};

enum {
    B_CRYPTO_IOCTL_BASE = B_DEVICE_OP_CODES_END + 100,
    
    // Questo comando invia una richiesta e riceve un risultato
    B_CRYPTO_IOCTL_PROCESS = B_CRYPTO_IOCTL_BASE
};

#endif
