/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef _B_CRYPTO_CORE_H_
#define _B_CRYPTO_CORE_H_

#include <crypto/BCryptoDefs.h>
#include "BCryptoAlgorithm.h"

status_t crypto_init_core();
void     crypto_uninit_core();
#ifndef BCRYPTO_SECURE_MEMZERO
#define BCRYPTO_SECURE_MEMZERO

static inline void
bcrypto_secure_memzero(void* p, size_t s)
{
    if (p == NULL)
        return;
    volatile uint8* cp = (volatile uint8*)p;
    while (s--)
        *cp++ = 0;
    
    // Aggiungiamo il barrier per essere sicuri al 100% 
    // che il compilatore non ottimizzi via il loop
    __asm__ volatile("" : : "r"(p) : "memory");
}

#endif
uint32   BGetStoredCryptoCapabilities();

status_t BRegisterCryptoAlgorithm(BCryptoAlgorithm* algorithm);
status_t BUnregisterCryptoAlgorithm(BCryptoAlgorithmID algorithm);
status_t BSubmitCryptoRequest(BCryptoRequest* request);
status_t BFillBufferWithRandom(void* buffer, size_t length);
status_t BGetAlgorithmInfo(BCryptoAlgorithmInfo* info);
status_t BCheckAlgorithmAvailability(BCryptoAlgorithmID id);
extern status_t BHashInit(crypto_session* session);
extern status_t BHashUpdate(crypto_session* session, BCryptoUserRequest* request);
extern status_t BHashFinal(crypto_session* session, BCryptoUserRequest* request);
extern status_t BStreamInit(crypto_session* session, BCryptoRequest* req);
extern status_t BStreamUpdate(crypto_session* session, BCryptoRequest* req);
extern status_t BStreamFinal(crypto_session* session, BCryptoRequest* req);
#endif // _B_CRYPTO_CORE_H_
