/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include <KernelExport.h>
#include <string.h>
#include <vm/vm.h>        // IS_USER_ADDRESS, copyin/copyout

#include "CryptoDevice.h"
#include "BCryptoCore.h"
#include "BCryptoEntropy.h"
#include "drivers/padlock/PadLock.h"
#include "drivers/aesni/AESNI.h"
#include "SoftCrypto.h"
#include "../BCryptoDefs.h"
//#include <user_runtime.h>
static void
secure_memzero(void* p, size_t s)
{
    if (p == NULL)
        return;
    volatile uint8* cp = (volatile uint8*)p;
    while (s--)
        *cp++ = 0;
}
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
    crypto_control,
    NULL,//crypto_read,   // read
    NULL,//crypto_write,   // write
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

status_t
crypto_control(void* cookie, uint32 op, void* arg, size_t length)
{
    switch (op) {
        case CRYPTO_IOCTL_PROCESS: {
            BCryptoUserRequest userReq;
            
            // Copia la struttura di controllo dall'utente
            if (user_memcpy(&userReq, arg, sizeof(BCryptoUserRequest)) != B_OK)
                return B_BAD_ADDRESS;

            // Prepariamo la richiesta interna al kernel (BCryptoRequest)
            BCryptoRequest req;
            req.operation = userReq.operation;
            req.algorithm = userReq.algorithm;
            req.mode = userReq.mode;
            req.flags = userReq.flags;
            
            // Sanificazione lunghezze
            if (userReq.keyLength > 64 || userReq.ivLength > 64)
                return B_BAD_VALUE;

            // Buffer temporanei nel kernel per evitare di toccare la memoria utente durante l'algoritmo
            uint8 localKey[64];
            uint8 localIV[64];

            if (user_memcpy(localKey, userReq.key, userReq.keyLength) != B_OK)
                return B_BAD_ADDRESS;
            if (user_memcpy(localIV, userReq.iv, userReq.ivLength) != B_OK)
                return B_BAD_ADDRESS;

            req.key = localKey;
            req.keyLength = userReq.keyLength;
            req.iv = localIV;
            req.ivLength = userReq.ivLength;

            // Gestione iovec: anche l'array di descrittori va copiato
            if (userReq.vectorCount > 32) return B_DEVICE_FULL;
            iovec localSrc[32], localDst[32];

            if (user_memcpy(localSrc, userReq.source, sizeof(iovec) * userReq.vectorCount) != B_OK)
                return B_BAD_ADDRESS;
            if (user_memcpy(localDst, userReq.destination, sizeof(iovec) * userReq.vectorCount) != B_OK)
                return B_BAD_ADDRESS;

            req.source = localSrc;
            req.destination = localDst;
            req.vectorCount = userReq.vectorCount;
            req.completionCallback = NULL; // Il kernel non può chiamare funzioni utente

            // Eseguiamo l'operazione
            status_t status = crypto_process_request(&req);

            // Se l'operazione ha aggiornato l'IV (tipico in CBC), lo riportiamo all'utente
            user_memcpy(userReq.iv, req.iv, userReq.ivLength);
            
            // Gestione asincrona tramite semaforo (se fornito)
            if (userReq.completionSem >= 0) {
                userReq.result = status;
                release_sem(userReq.completionSem);
            }

            return status;
        }
    }
    return B_DEV_INVALID_IOCTL;
}
//-----------------------------------------------------------
// ioctl / copyin/copyout sicuro
//-----------------------------------------------------------
/*
static status_t crypto_control(void* cookie, uint32 op, void* data, size_t length)
{
    if (op != B_CRYPTO_IOCTL_SUBMIT)
        return B_BAD_VALUE;

    if (!IS_USER_ADDRESS(data))
        return B_BAD_ADDRESS;

    if (length != sizeof(BCryptoUserRequest))
        return B_BAD_VALUE;

    BCryptoUserRequest userReq;
    status_t st = user_memcpy(&userReq, data, sizeof(userReq));
    if (st != B_OK)
        return st;

    // Validazione vettori
    if (userReq.vectorCount == 0 || !userReq.destination)
        return B_BAD_VALUE;
    
    if (userReq.vectorCount > IOV_MAX)
        return B_BAD_VALUE;

    for (size_t i = 0; i < userReq.vectorCount; i++) {
        if (!IS_USER_ADDRESS(userReq.destination[i].iov_base))
            return B_BAD_ADDRESS;
        
        if (userReq.source) {
            if (!IS_USER_ADDRESS(userReq.source[i].iov_base))
                return B_BAD_ADDRESS;
        }


        if (userReq.algorithm == B_CRYPTO_AES) {
            switch (userReq.mode) {
                case B_CRYPTO_MODE_ECB:
                case B_CRYPTO_MODE_CBC:
                    if (userReq.destination[i].iov_len % 16 != 0)
                        return B_BAD_VALUE;
                    break;

                case B_CRYPTO_MODE_CTR:
                case B_CRYPTO_MODE_GCM:
                    break;

                default:
                    return B_BAD_VALUE;
            }
        }
    }

    uint8 keyBuffer[64];
    uint8 ivBuffer[32];
    
    // Costruzione request kernel
    BCryptoRequest req {};
    req.operation   = userReq.operation;
    req.algorithm   = userReq.algorithm;
    req.mode        = userReq.mode;
    req.flags       = userReq.flags;
    

    if (userReq.key && userReq.keyLength > 0) {
    	if (userReq.keyLength > sizeof(keyBuffer))
            return B_BAD_VALUE;
        st = user_memcpy(keyBuffer, userReq.key, userReq.keyLength);
        if (st != B_OK)
            return st;
        req.key = keyBuffer;
    }
    req.keyLength   = userReq.keyLength;
    if (userReq.iv && userReq.ivLength > 0) {
        if (userReq.ivLength > sizeof(ivBuffer))
            return B_BAD_VALUE;

        st = user_memcpy(ivBuffer, userReq.iv, userReq.ivLength);
        if (st != B_OK)
            return st;
        req.iv = ivBuffer;
    }
    req.ivLength    = userReq.ivLength;
    req.source      = userReq.source;
    req.destination = userReq.destination;
    req.vectorCount = userReq.vectorCount;
    req.completionCallback = userReq.completionCallback;//nullptr;
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
    //if (req.iv && req.ivLength >= 16)
    //    user_memcpy(userReq.iv, req.iv, 16);
    if (req.iv && userReq.iv) {
        size_t out = min_c(req.ivLength, userReq.ivLength);
        user_memcpy(userReq.iv, req.iv, out);
    }

    secure_memzero(keyBuffer, sizeof(keyBuffer));
    secure_memzero(ivBuffer, sizeof(ivBuffer));

    return B_OK;
}
*/

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
