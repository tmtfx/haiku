/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include "BCryptoCapabilities.h"

#if ARCH_X86
#include <arch/x86/arch_cpu.h>
#endif

uint32
BGetCryptoCapabilities()
{
    uint32 caps = 0;

#if ARCH_X86
    if (x86_check_feature(IA32_FEATURE_AES))
        caps |= B_CPU_CRYPTO_AESNI;

    if (x86_check_feature(IA32_FEATURE_SHA))
        caps |= B_CPU_CRYPTO_SHAEXT;

    if (x86_check_feature(IA32_FEATURE_PADLOCK))
        caps |= B_CPU_CRYPTO_PADLOCK;

    if (x86_check_feature(IA32_FEATURE_PADLOCK_RNG))
        caps |= B_CPU_CRYPTO_PADLOCK_RNG;
#endif

    return caps;
}

