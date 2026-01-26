/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef _B_CRYPTO_KERNEL_INTERNAL_H_
#define _B_CRYPTO_KERNEL_INTERNAL_H_

#include <OS.h>
#include <SupportDefs.h>
#include <iovec.h>
#include <crypto/BCryptoDefs.h>

struct BCryptoRequest {
    BCryptoOperation		operation;
    BCryptoAlgorithmID		algorithm;
    BCryptoMode				mode;
    uint32					flags; 
    const void*				key;
    size_t					keyLength;
    void*					iv;
    size_t					ivLength;
    const iovec*			source;
    iovec*					destination;
    size_t					vectorCount;
    status_t				(*completionCallback)(BCryptoRequest*, status_t);
    void*					userCookie;
};
#endif // _B_CRYPTO_KERNEL_INTERNAL_H_
