/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef _B_CRYPTO_REQUEST_H_
#define _B_CRYPTO_REQUEST_H_

#include <OS.h>
#include <SupportDefs.h>
#include <iovec.h>
#include <crypto/BCryptoDefs.h>

typedef struct {
    BCryptoOperation    operation;
    BCryptoAlgorithmID  algorithm;
    BCryptoMode         mode;
    uint32              flags;

    void* key;
    size_t              keyLength;
    void* iv;
    size_t              ivLength;

    const iovec*        source;
    iovec*              destination;
    size_t              vectorCount;

    // Sostituiamo il puntatore a funzione con un semaforo
    sem_id              completionSem; 
    status_t            result;
} BCryptoUserRequest;

struct BCryptoRandomRequest {
    void* buffer;
    size_t length;
    status_t result;
};

#endif // _B_CRYPTO_REQUEST_H_

