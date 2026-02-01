/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include <KernelExport.h>
#include <Drivers.h>
#include <string.h>
#include <vm/vm.h>        // IS_USER_ADDRESS, copyin/copyout
#include <device_manager.h>
#include "CryptoDevice.h"
#include "BCryptoCore.h"
#include <crypto/BCryptoDefs.h>
#include <crypto/BCryptoKernelInternal.h>
#include "BCryptoEntropy.h"

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

static status_t
crypto_open_modern(void* device_cookie, const char* name, int flags, void** cookie)
{
    //*cookie = NULL;
    crypto_session* session = (crypto_session*)malloc(sizeof(crypto_session));
    if (session == NULL)
        return B_NO_MEMORY;

    memset(session, 0, sizeof(crypto_session));
    session->is_active = false;
    
    *cookie = session;

    return B_OK;
}

static status_t
crypto_open_legacy(const char* name, uint32 flags, void** cookie)
{
    return crypto_open_modern(NULL, name, (int)flags, cookie);
}

static status_t
crypto_close(void* cookie)
{
	crypto_session* session = (crypto_session*)cookie;
    if (session) {
        if (session->algorithm_state) {
            secure_memzero(session->algorithm_state, session->state_size);
            free(session->algorithm_state);
        }
        secure_memzero(session, sizeof(crypto_session));
        free(session);
    }
    return B_OK;
}

static status_t
crypto_free(void* cookie)
{
    return B_OK;
}

static status_t
crypto_control(void* cookie, uint32 op, void* arg, size_t length)
{
    switch (op) {
        case B_CRYPTO_IOCTL_PROCESS: {
            BCryptoUserRequest userReq;
            
            if (user_memcpy(&userReq, arg, sizeof(BCryptoUserRequest)) != B_OK) return B_BAD_ADDRESS;

            BCryptoRequest req;
            req.operation = userReq.operation;
            req.algorithm = userReq.algorithm;
            req.mode = userReq.mode;
            req.flags = userReq.flags;
            
            if (userReq.keyLength > 64 || userReq.ivLength > 64)
                return B_BAD_VALUE;
            

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

            if (userReq.vectorCount > 32) 
                return B_DEVICE_FULL;
            

            iovec localSrc[32], localDst[32];

            if (user_memcpy(localSrc, userReq.source, sizeof(iovec) * userReq.vectorCount) != B_OK) 
                return B_BAD_ADDRESS;
            
            if (user_memcpy(localDst, userReq.destination, sizeof(iovec) * userReq.vectorCount) != B_OK) 
                return B_BAD_ADDRESS;

            req.source = localSrc;
            req.destination = localDst;
            req.vectorCount = userReq.vectorCount;
            req.completionCallback = NULL;

            // Chiamata al core del framework
            status_t status = BSubmitCryptoRequest(&req);

            // Riporta l'IV aggiornato all'utente
            if (status == B_OK && userReq.iv != NULL) {
                if (user_memcpy(userReq.iv, localIV, userReq.ivLength) != B_OK) {
                    // Se fallisce qui, il dato è cifrato ma l'utente non ha l'IV nuovo
                    status = B_BAD_ADDRESS;
                }
            }
            
            //if (userReq.completionSem >= 0) {
            //    userReq.result = status;
            //    user_memcpy(arg, &userReq, sizeof(BCryptoUserRequest));
            //    release_sem(userReq.completionSem);
            //}
            secure_memzero(localKey, sizeof(localKey));
            secure_memzero(localIV, sizeof(localIV));

            return status;
        }
        
        case B_CRYPTO_IOCTL_HASH_INIT: {
            BCryptoAlgorithmID algo;
            if (user_memcpy(&algo, arg, sizeof(BCryptoAlgorithmID)) != B_OK)
                return B_BAD_ADDRESS;

            crypto_session* session = (crypto_session*)cookie;
            
            // 1. Salva temporaneamente il vecchio stato
            void* oldState = session->algorithm_state;
            size_t oldSize = session->state_size;
            BCryptoAlgorithmID oldAlgo = session->algorithm;
            
            // 2. Tenta l'inizializzazione sul nuovo
            session->algorithm = algo;
            session->algorithm_state = NULL; // BHashInit allocherà il nuovo
            
            status_t st = BHashInit(session);
            if (st == B_OK) {
                // Successo: ora possiamo liberare il vecchio
                if (oldState) {
                    secure_memzero(oldState, oldSize);
                    free(oldState);
                }
            } else {
                // Fallimento: ripristina lo stato precedente
                session->algorithm = oldAlgo;
                session->algorithm_state = oldState;
            }
            return st;
        }
        case B_CRYPTO_IOCTL_HASH_UPDATE: {
            BCryptoUserRequest userReq;
            if (user_memcpy(&userReq, arg, sizeof(BCryptoUserRequest)) != B_OK)
                return B_BAD_ADDRESS;

            crypto_session* session = (crypto_session*)cookie;
            if (!session->is_active) return B_BAD_VALUE;

            // Trasformiamo i puntatori utente in iovec locali (come in PROCESS)
            iovec localSrc[32];
            if (userReq.vectorCount > 32) return B_DEVICE_FULL;
            
            if (user_memcpy(localSrc, userReq.source, sizeof(iovec) * userReq.vectorCount) != B_OK)
                return B_BAD_ADDRESS;

            // Temporaneamente sovrascriviamo il puntatore della struct locale con quello kernel
            userReq.source = localSrc;

            return BHashUpdate(session, &userReq);
        }
        case B_CRYPTO_IOCTL_HASH_FINAL: {
            BCryptoUserRequest userReq;
            if (user_memcpy(&userReq, arg, sizeof(BCryptoUserRequest)) != B_OK)
                return B_BAD_ADDRESS;

            crypto_session* session = (crypto_session*)cookie;
            if (!session || !session->is_active) 
                return B_BAD_VALUE;

            // Per il Final, dobbiamo mappare il buffer di destinazione
            // Di solito il digest sta in un singolo buffer, quindi prendiamo solo il primo iovec
            iovec localDst[1]; 
            if (user_memcpy(localDst, userReq.destination, sizeof(iovec)) != B_OK)
                return B_BAD_ADDRESS;

            // Verifichiamo che il buffer sia grande abbastanza per l'algoritmo corrente
            if (localDst[0].iov_len < decode_hash_length(session->algorithm))
                return B_BAD_VALUE;

            // Passiamo la struct aggiornata al Core
            userReq.destination = localDst;
            userReq.vectorCount = 1; // Per la finalizzazione standard basta un vettore

            status_t st = BHashFinal(session, &userReq);
            
            // La sessione è conclusa, ma NON liberiamo qui algorithm_state.
            // Lo stato verrà liberato al prossimo HASH_INIT o alla crypto_close.
            session->is_active = false; 
            
            return st;
        }
        
        case B_CRYPTO_IOCTL_GET_RANDOM: {
            BCryptoRandomRequest randomReq;

            // 1. Copia la richiesta dallo spazio utente
            if (user_memcpy(&randomReq, arg, sizeof(BCryptoRandomRequest)) != B_OK)
                return B_BAD_ADDRESS;

            if (randomReq.buffer == NULL || randomReq.length == 0)
                return B_BAD_VALUE;

            // Limite di sicurezza per singola richiesta (es. 1MB)
            if (randomReq.length > 1024 * 1024)
                return B_BAD_VALUE;

            // 2. Small Buffer Optimization (SBO)
            uint8 stackBuffer[512];
            uint8* kBuffer = NULL;
            bool usedHeap = false;

            if (randomReq.length <= sizeof(stackBuffer)) {
                kBuffer = stackBuffer;
            } else {
                kBuffer = (uint8*)malloc(randomReq.length);
                if (kBuffer == NULL) return B_NO_MEMORY;
                usedHeap = true;
            }

            // 3. Chiamata al generatore hardware (che vive nel Core o nel modulo CPU)
            // Nota: BFillBufferWithRandom è una funzione che dovrai esportare dal Core
            status_t status = BFillBufferWithRandom(kBuffer, randomReq.length);

            // 4. Copia i dati generati all'utente
            if (status == B_OK) {
                if (user_memcpy(randomReq.buffer, kBuffer, randomReq.length) != B_OK)
                    status = B_BAD_ADDRESS;
            }

            // 5. Pulizia e rilascio
            secure_memzero(kBuffer, randomReq.length);
            if (usedHeap) free(kBuffer);

            // Riporta il risultato nella struct originale se necessario
            randomReq.result = status;
            user_memcpy(arg, &randomReq, sizeof(BCryptoRandomRequest));

            return status;
        }
    }
    return B_DEV_INVALID_IOCTL;
}


device_hooks sCryptoHooks = {
    crypto_open_legacy,
    crypto_close,
    crypto_free,
    crypto_control,
    NULL,//crypto_read,   // read
    NULL,//crypto_write,   // write
    NULL,
    NULL
};

struct device_module_info sCryptoDeviceModule = {
    {
        "drivers/crypto/api/v1",
        0,
        NULL
    },
    NULL, // init_device
    NULL, // uninit_device
    NULL, // remove_device
    
    crypto_open_modern,
    crypto_close,
    crypto_free,
    NULL, // read
    NULL, // write
    NULL, // io
    crypto_control,
    
    NULL, // select
    NULL  // deselect
};
//--------------------------
//-----------------------------------------------------------
// Driver entry points
//-----------------------------------------------------------
extern "C" status_t crypto_std_ops(int op, ...);

extern "C" status_t init_hardware()
{
	dprintf("crypto: init_hardware\n");
    return B_OK;
}

extern "C" status_t init_driver()
{
	dprintf("crypto: init_driver\n");
    return crypto_std_ops(B_MODULE_INIT);
}

extern "C" void uninit_driver()
{
	dprintf("crypto: uninit_driver\n");
    crypto_std_ops(B_MODULE_UNINIT);
}

extern "C" int32 api_version;
int32 api_version = B_CUR_DRIVER_API_VERSION;

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
