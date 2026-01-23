/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include "PadLockRNG.h"

#include "BCryptoCore.h"
#include "BCryptoCapabilities.h"
#include "BCryptoDefs.h"

#include <string.h>

#if ARCH_X86
#include <arch/x86/arch_cpu.h>
#endif

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

        /*
         * xstore:
         * EAX = buffer
         * ECX = length
         * EDX = flags (0 = random)
         */
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
}

status_t
BInitPadLockRNG()
{
    uint32 caps = BGetCryptoCapabilities();

    if (!(caps & B_CPU_CRYPTO_PADLOCK_RNG))
        return B_UNSUPPORTED;

    static BCryptoAlgorithm sPadLockRNG = {
        .algorithm = B_CRYPTO_RNG,
        .mode      = B_CRYPTO_MODE_ANY,  // per RNG non c’è modalità
        .flags     = B_CRYPTO_ALG_HW_ACCEL,
        .priority  = 100, // massimo, è hardware puro
        .Process   = padlock_rng_process
    };

    return BRegisterCryptoAlgorithm(&sPadLockRNG);
}
