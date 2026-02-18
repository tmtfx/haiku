/*
 * Intel/AMD Hardware RNG (RDRAND)
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 */

#include "BCryptoCPU.h"
#include "BCryptoCore.h"
#include <crypto/BCryptoDefs.h>
#include "BCryptoAlgorithm.h"
#include <string.h>

#if defined(__x86_64__) || defined(__i386__)

static status_t
cpu_rng_generate(uint8* buffer, size_t length)
{
    size_t generated = 0;
    
    while (generated < length) {
        #ifdef __x86_64__
            uint64 val;
            size_t step = 8;
        #else
            uint32 val;
            size_t step = 4;
        #endif
        uint8 ok;

        // rdrand ritorna il successo nel Carry Flag (CF)
        asm volatile (
            "rdrand %0; "
            "setc %1"
            : "=r"(val), "=qm"(ok)
            :
            : "cc" 
        );
/*#ifdef __x86_64__
        uint64 val; // Usiamo uint64 per chiarezza
        uint8 ok;
        asm volatile (
            "rdrand %0; "
            "setc %1"
            : "=r"(val), "=qm"(ok)
            :
            : "cc" // Notifica al compilatore che modifichiamo i condition codes (il Carry Flag)
        );
        size_t step = 8;
#else
        uint32 val;
        uint8 ok;
        asm volatile (
            "rdrand %0; "
            "setc %1"
            : "=r"(val), "=qm"(ok)
            :
            : "cc"
        );
        size_t step = 4;
#endif*/

        if (!ok) {
            // Il generatore hardware può "svuotarsi" se chiamato troppo velocemente.
            // Facciamo un breve "pause" e riproviamo.
            asm volatile ("pause");
            continue;
        }

        size_t toCopy = (length - generated < step) ? (length - generated) : step;
        memcpy(buffer + generated, &val, toCopy);
        generated += toCopy;
    }
    
    return B_OK;
}

static status_t
cpu_rng_process(BCryptoRequest* request)
{
    for (size_t i = 0; i < request->vectorCount; i++) {
        status_t st = cpu_rng_generate(
            (uint8*)request->destination[i].iov_base, 
            request->destination[i].iov_len
        );
        if (st != B_OK) return st;
    }
    return B_OK;
}

status_t
BInitx86CPURNG()
{
    // Verifichiamo il supporto RDRAND (solitamente bit 30 di ECX in CPUID leaf 1)
    // Assumo che BGetStoredCryptoCapabilities() lo abbia già rilevato
    if (!(BGetStoredCryptoCapabilities() & B_CRYPTO_HW_RDRAND))
        return B_UNSUPPORTED;

    static BCryptoAlgorithm sCPURNG = {
        .algorithm = B_CRYPTO_RNG,
        .mode = B_CRYPTO_MODE_ANY,
        .flags = B_CRYPTO_ALG_HW_ACCEL,
        .name      = "RNG (Hardware)",
        .priority = 90, // Un pelino meno di PadLock se presenti entrambi, o viceversa
        .Process = cpu_rng_process
    };

    return BRegisterCryptoAlgorithm(&sCPURNG);
}
#else
status_t BInitx86CPURNG() { return B_NOT_SUPPORTED; }
#endif
