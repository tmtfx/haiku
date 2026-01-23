/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

/*
static status_t
padlock_rng_generate(uint8* buffer, size_t length)
{
#if !ARCH_X86
    return B_NOT_SUPPORTED;
#else
    size_t generated = 0;

    while (generated < length) {
        uint32 value;
        uint32 flags;

        //
        // xstore:
        // EAX = buffer
        // ECX = length
        // EDX = flags (0 = random)
        //
        asm volatile (
            "movl $0, %%edx\n\t"
            "xstore %%eax, %%ecx\n\t"
            "setc %%dl\n\t"
            : "=a"(value), "=c"(flags)
            :
            : "edx", "memory"
        );

        if (flags)
            return B_ERROR;

        size_t copy = min_c(sizeof(value), length - generated);
        memcpy(buffer + generated, &value, copy);
        generated += copy;
    }

    return B_OK;
#endif
}

static status_t
padlock_rng_process(BCryptoRequest* request)
{
	if (!request)
        return B_BAD_VALUE;
    
    if (request->algorithm != B_CRYPTO_RNG)
        return B_NOT_SUPPORTED;

    if (request->operation != B_CRYPTO_DIGEST)
        return B_BAD_VALUE;

    if (request->vectorCount == 0)
        return B_BAD_VALUE;
    
    status_t st = B_OK;

    for (size_t i = 0; i < request->vectorCount; i++) {
        iovec& dst = request->destination[i];

        st = padlock_rng_generate(
            (uint8*)dst.iov_base,
            dst.iov_len
        );

        if (st != B_OK)
            break;
    }

    if (request->completionCallback) {
        request->completionCallback(request, st);
        return B_OK; // async sempre OK
    }

    return st;
}*/

/*
 * VIA PadLock Hardware RNG
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 */
#include <string.h>         // Per memcpy
#include <KernelExport.h>   // Per utilità kernel generali
#include <SupportDefs.h>    // Per i tipi come status_t, uint8, ecc.
#include "PadLockRNG.h"
#include "BCryptoCPU.h"
#include "BCryptoCore.h"
#include "BCryptoDevice.h"
#include <cstring>

#if defined(__x86_64__) || defined(__i386__)

static status_t
padlock_rng_generate(uint8* buffer, size_t length)
{
    size_t generated = 0;
    
    while (generated < length) {
        uint32 value[2]; // Usiamo 8 byte per sicurezza, xstore è avido
        uint32 statusFlag;

        /*
         * xstore (VIA PadLock RNG):
         * EDI = puntatore alla destinazione
         * EDX = 0 (qualità massima/nessun filtraggio hardware extra)
         * Risultato:
         * EAX = numero di byte generati (di solito 0 o 8)
         * Carry Flag = 0 se successo, 1 se errore/dati non pronti
         */
        asm volatile (
            "movl $0, %%edx\n\t"        // Qualità 0
            "xstore\n\t"                // Scrive in (EDI)
            "setc %%al\n\t"             // AL = 1 se Carry è settato (errore)
            "movzbl %%al, %1\n\t"       // statusFlag = AL
            : "=D"(value[0]), "=r"(statusFlag)
            : "D"(&value[0])            // EDI punta al nostro buffer locale
            : "eax", "edx", "memory", "cc"
        );

        if (statusFlag != 0) {
            // Se il chip non è pronto, facciamo un brevissimo relax e riproviamo
            // o ritorniamo errore se preferisci non bloccare
            continue; 
        }

        // xstore scrive solitamente 8 byte, ma noi ne copiamo solo quelli necessari
        size_t available = 8; 
        size_t toCopy = (length - generated < available) ? (length - generated) : available;
        
        memcpy(buffer + generated, value, toCopy);
        generated += toCopy;
    }
    
    return B_OK;
}

static status_t
padlock_rng_process(BCryptoRequest* request)
{
    // Il Core passa per BCryptoRequest, noi estraiamo i buffer
    for (size_t i = 0; i < request->vectorCount; i++) {
        status_t st = padlock_rng_generate(
            (uint8*)request->destination[i].iov_base, 
            request->destination[i].iov_len
        );
        if (st != B_OK) return st;
    }
    return B_OK;
}

status_t
BInitPadLockRNG()
{
    if (!(BGetStoredCryptoCapabilities() & B_CRYPTO_HW_RNG))
        return B_UNSUPPORTED;

    static BCryptoAlgorithm sPadLockRNG = {
        B_CRYPTO_RNG,
        B_CRYPTO_MODE_ANY,
        B_CRYPTO_ALG_HW_ACCEL,
        100, // Massima priorità per l'hardware RNG
        padlock_rng_process
    };

    return BRegisterCryptoAlgorithm(&sPadLockRNG);
}

#endif
