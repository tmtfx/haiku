/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef _B_CRYPTO_DEVICE_H_
#define _B_CRYPTO_DEVICE_H_

#include <Drivers.h>
#include <iovec.h>
#include "../../BCryptoDefs.h"

#define B_CRYPTO_DEVICE_NAME "crypto"

enum {
    B_CRYPTO_IOCTL_SUBMIT = B_DEVICE_OP_CODES_END + 1
};

struct BCryptoUserRequest {
    BCryptoOperation    operation;
    BCryptoAlgorithmID  algorithm;
	BCryptoMode         mode;
	uint32              flags;
	
    void* key;
    size_t keyLength;

    void* iv;
    size_t ivLength;

    const iovec* source;
    iovec* destination;
    size_t vectorCount;
    status_t (*completionCallback)(BCryptoRequest*, status_t);
};

#endif
