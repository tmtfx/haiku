/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include <KernelExport.h>
#include <string.h>
#include <KernelDebug.h>
#include <vm/vm.h>        // IS_USER_ADDRESS, copyin/copyout

#include "CryptoDevice.h"
#include "BCryptoCore.h"
#include "BCryptoEntropy.h"
#include "drivers/padlock/PadLock.h"
#include "drivers/aesni/AESNI.h"
#include "SoftCrypto.h"

//-----------------------------------------------------------
// Device hooks
//-----------------------------------------------------------
static status_t crypto_open(const char* name, uint32 flags, void** cookie);
static status_t crypto_close(void* cookie);
static status_t crypto_free(void* cookie);
static status_t crypto_control(void* cookie, uint32 op, void* data, size_t length);

static device_hooks sCryptoHooks = {
    crypto_open,
    crypto_close,
    crypto_free,
    NULL,   // read
    NULL,   // write
    crypto_control,
    NULL,
    NULL,
    NULL,
    NULL
};

//-----------------------------------------------------------
// Open / Close / Free
//-----------------------------------------------------------
static status_t crypto_open(const char* name, uint32 flags, void** cookie)
{
    *cookie = NULL;
    return B_OK;
}

static status_t crypto_close(void* cookie)
{
    return B_OK;
}

static status_t crypto_free(void* cookie)
{
    return B_OK;
}

//-----------------------------------------------------------
// ioctl / copyin/copyout sicuro
//-----------------------------------------------------------
static status_t crypto_control(void* cookie, uint32 op, void* data, size_t length)
{
    if (op != B_CRYPTO_IOCTL_SUBMIT)
        return B_BAD_VALUE;

    if (!IS_USER_ADDRESS(data))
        return B_BAD_ADDRESS;

    if (length != sizeof(BCryptoUserRequest))
        return B_BAD_VALUE;

    BCryptoUserRequest userReq;
    status_t st = memcpy_from_user(&userReq, data, sizeof(userReq));
    if (st != B_OK)
        return st;

    // Validazione vettori
    if (userReq.vectorCount == 0 || !userReq.destination)
        return B_BAD_VALUE;

    for (size_t i = 0; i < userReq.vectorCount; i++) {
        if (!IS_USER_ADDRESS(userReq.destination[i].iov_base))
            return B_BAD_ADDRESS;

        // opzionale: lunghezza multiplo AES
        if ((userReq.algorithm == B_CRYPTO_AES_CBC) &&
            (userReq.destination[i].iov_len % 16 != 0))
            return B_BAD_VALUE;
    }

    // Costruzione request kernel
    BCryptoRequest req {};
    req.operation   = userReq.operation;
    req.algorithm   = userReq.algorithm;
    req.key         = userReq.key;
    req.keyLength   = userReq.keyLength;
    req.iv          = userReq.iv;
    req.ivLength    = userReq.ivLength;
    req.source      = userReq.source;
    req.destination = userReq.destination;
    req.vectorCount = userReq.vectorCount;
    req.completionCallback = nullptr;
    req.userCookie = nullptr;

    // RNG speciale: alimenta kernel entropy pool
    if (req.algorithm == B_CRYPTO_RNG && req.operation == B_CRYPTO_DIGEST) {
        st = PadLockFeedEntropy();
        if (st != B_OK)
            return st;
    }

    // submit: il kernel seleziona automaticamente AES-NI > PadLock > SoftCrypto
    st = BSubmitCryptoRequest(&req);
    if (st != B_OK)
        return st;

    // Copia eventuale IV aggiornato in userland
    if (req.iv && req.ivLength >= 16)
        memcpy_to_user(userReq.iv, req.iv, 16);

    return B_OK;
}

//-----------------------------------------------------------
// Driver entry points
//-----------------------------------------------------------
extern "C" status_t init_hardware()
{
    return B_OK;
}

extern "C" status_t init_driver()
{
    return B_OK;
}

extern "C" void uninit_driver()
{
}

extern "C" const char** publish_devices()
{
    static const char* devices[] = {
        B_CRYPTO_DEVICE_NAME,
        NULL
    };
    return devices;
}

extern "C" device_hooks* find_device(const char* name)
{
    if (strcmp(name, B_CRYPTO_DEVICE_NAME) == 0)
        return &sCryptoHooks;

    return NULL;
}
