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
//#include "BCryptoDevice.h"

#include "BCryptoCore.h"
#include <crypto/BCryptoDefs.h>
#include <crypto/BCryptoKernelInternal.h>
#include "BCryptoEntropy.h"

//#define B_CRYPTO_DEVICE_NAME "crypto/v1"

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
/*
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
}*/
static status_t
crypto_close(void* cookie)
{
    crypto_session* session = (crypto_session*)cookie;
    if (session == NULL)
        return B_OK;

    // Se l'utente chiude il file descriptor senza aver chiamato HASH_FINAL,
    // lo stato dell'algoritmo sarebbe rimasto allocato e "lockato" in RAM.
    if (session->algorithm_state != NULL) {
        if (session->mode == B_CRYPTO_MODE_GCM) 
            BStreamUninit(session);
        else
            BHashFinal(session, NULL);
    }

    // Ora che lo stato interno è certamente pulito, cancelliamo la sessione.
    secure_memzero(session, sizeof(crypto_session));
    free(session);

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
            
            // Se c'era già uno stato attivo, lo chiudiamo formalmente tramite il Core
            // prima di inizializzarne uno nuovo. Questo garantisce unlock + free.
            if (session->algorithm_state != NULL) {
                BHashFinal(session, NULL);
            }
    
            session->algorithm = algo;
            session->mode = B_CRYPTO_MODE_ANY;
            session->algorithm_state = NULL; 
            
            return BHashInit(session); // Il Core alloca e fa lock_memory
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
            // 1. Copiamo la richiesta dall'utente (contiene il puntatore alla destinazione)
            if (user_memcpy(&userReq, arg, sizeof(BCryptoUserRequest)) != B_OK)
                return B_BAD_ADDRESS;

            crypto_session* session = (crypto_session*)cookie;
            // 2. Controllo validità sessione
            if (!session || !session->is_active) 
                return B_BAD_VALUE;

            // 3. Mappiamo l'iovec di destinazione nello spazio kernel
            iovec localDst[1]; 
            if (user_memcpy(localDst, userReq.destination, sizeof(iovec)) != B_OK)
                return B_BAD_ADDRESS;

            // 4. Verifica che il buffer sia sufficiente per l'hash richiesto
            if (localDst[0].iov_len < decode_hash_length(session->algorithm))
                return B_BAD_VALUE;
        
            // 5. Prepariamo la richiesta per il Core
            userReq.destination = localDst;
            userReq.vectorCount = 1;

            // 6. CHIAMATA AL CORE
            // BHashFinal ora si occupa di:
            // - Calcolare l'hash finale
            // - Copiarlo in localDst[0].iov_base (gestendo lock/unlock/SMAP)
            // - Eseguire unlock_memory sullo stato della sessione
            // - Eseguire la free() dello stato
            // - Mettere a NULL session->algorithm_state
            return BHashFinal(session, &userReq);
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
        
        case B_CRYPTO_IOCTL_GET_NEXT_ALGO: {
            BCryptoAlgorithmInfo info;
            if (user_memcpy(&info, arg, sizeof(BCryptoAlgorithmInfo)) != B_OK)
                return B_BAD_ADDRESS;

            status_t status = BGetAlgorithmInfo(&info);
            if (status != B_OK)
                return status;

            return user_memcpy(arg, &info, sizeof(BCryptoAlgorithmInfo));
        }
       
        case B_CRYPTO_IOCTL_CHECK_ALGO: {
            BCryptoAlgorithmID algo;
            if (user_memcpy(&algo, arg, sizeof(BCryptoAlgorithmID)) != B_OK)
                return B_BAD_ADDRESS;

            return BCheckAlgorithmAvailability(algo);
        }
       
        /*--- STREAMING AEAD (GCM/CCM) ---*/
        case B_CRYPTO_IOCTL_STREAM_INIT: {
            BCryptoUserRequest userReq;
            if (user_memcpy(&userReq, arg, sizeof(BCryptoUserRequest)) != B_OK)
                return B_BAD_ADDRESS;

            crypto_session* session = (crypto_session*)cookie;
            
            // Se c'è uno stato attivo (magari un hash o un altro stream), pulizia.
            if (session->algorithm_state != NULL) {
                //BStreamFinal(session, NULL); 
                BStreamUninit(session);
            }

            if (userReq.keyLength > 64 || userReq.ivLength > 64)
                return B_BAD_VALUE;

            uint8 localKey[64];
            uint8 localIV[64];

            if (user_memcpy(localKey, userReq.key, userReq.keyLength) != B_OK) 
                return B_BAD_ADDRESS;
            if (user_memcpy(localIV, userReq.iv, userReq.ivLength) != B_OK) 
                return B_BAD_ADDRESS;

            session->algorithm = userReq.algorithm;
            session->op = userReq.operation;
            session->mode = userReq.mode;
            session->algorithm_state = NULL; 

            // Prepariamo la richiesta kernel-side per l'inizializzazione
            BCryptoRequest req;
            req.algorithm = userReq.algorithm;
            req.operation = userReq.operation;
            req.mode = userReq.mode;
            req.key = localKey;
            req.keyLength = userReq.keyLength;
            req.iv = localIV;
            req.ivLength = userReq.ivLength;

            status_t status = BStreamInit(session, &req);
            
            secure_memzero(localKey, sizeof(localKey));
            secure_memzero(localIV, sizeof(localIV));
            return status;
        }

        case B_CRYPTO_IOCTL_STREAM_UPDATE: {
            BCryptoUserRequest userReq;
            if (user_memcpy(&userReq, arg, sizeof(BCryptoUserRequest)) != B_OK)
                return B_BAD_ADDRESS;

            crypto_session* session = (crypto_session*)cookie;
            if (!session->is_active || session->algorithm_state == NULL) 
                return B_BAD_VALUE;

            if (userReq.vectorCount > 32) return B_DEVICE_FULL;

            iovec localSrc[32], localDst[32];
            if (user_memcpy(localSrc, userReq.source, sizeof(iovec) * userReq.vectorCount) != B_OK)
                return B_BAD_ADDRESS;
            if (user_memcpy(localDst, userReq.destination, sizeof(iovec) * userReq.vectorCount) != B_OK)
                return B_BAD_ADDRESS;

            BCryptoRequest req;
            req.source = localSrc;
            req.destination = localDst;
            req.vectorCount = userReq.vectorCount;

            return BStreamUpdate(session, &req);
        }

        case B_CRYPTO_IOCTL_STREAM_FINAL: {
            BCryptoUserRequest userReq;
            if (user_memcpy(&userReq, arg, sizeof(BCryptoUserRequest)) != B_OK)
                return B_BAD_ADDRESS;

            crypto_session* session = (crypto_session*)cookie;
            if (!session->is_active || session->algorithm_state == NULL) 
                return B_BAD_VALUE;

            // Il tag viene restituito tramite il primo vettore di destination
            //iovec localTagVec[1];
            iovec localTagVec;
            //if (user_memcpy(localTagVec, userReq.destination, sizeof(iovec)) != B_OK) return B_BAD_ADDRESS;
            if (user_memcpy(&localTagVec, userReq.destination, sizeof(iovec)) != B_OK)
                return B_BAD_ADDRESS;

            if (localTagVec.iov_len < 16) return B_BAD_VALUE;

            // 2. Buffer temporaneo KERNEL per il tag
            uint8 kernelTag[16];
            iovec tagIov = { kernelTag, 16 };
            
            BCryptoRequest kernelReq;
            memset(&kernelReq, 0, sizeof(kernelReq));
            kernelReq.destination = &tagIov;
            kernelReq.vectorCount = 1;

            //return BStreamFinal(session, &req);
            status_t status = BStreamFinal(session, &kernelReq);
            if (status == B_OK) {
                // 4. Copia finale verso l'utente
                // Recuperiamo dove l'utente voleva il tag
                iovec userTagIov;
                if (user_memcpy(&userTagIov, userReq.destination, sizeof(iovec)) == B_OK) {
                    user_memcpy(userTagIov.iov_base, kernelTag, 16);
                }
            }
            
            BStreamUninit(session);

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
