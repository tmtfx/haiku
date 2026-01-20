/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef _B_CRYPTO_CAPABILITIES_H_
#define _B_CRYPTO_CAPABILITIES_H_


#include <SupportDefs.h>


enum BCryptoCpuFeature {
    B_CPU_CRYPTO_AESNI      = 0x0001,
    B_CPU_CRYPTO_SHAEXT     = 0x0002,
    B_CPU_CRYPTO_PADLOCK    = 0x0004,
    B_CPU_CRYPTO_PADLOCK_RNG= 0x0008
};

uint32 BGetCryptoCapabilities();


#endif
