/*
 * Hardware-accelerated AES using Intel/AMD AES-NI instructions
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef _AES_NI_H_
#define _AES_NI_H_

#include <SupportDefs.h>
#include <crypto/BCryptoDefs.h>
#include <crypto/BCryptoKernelInternal.h>
#include "BCryptoCore.h"

#include <immintrin.h> // Necessario per __m128i

struct AESNIContext {
    alignas(16) __m128i encRoundKeys[15]; // Massimo 14 round per AES-256 + 1
    alignas(16) __m128i decRoundKeys[15];
    alignas(16) BCryptoFPUContext fpu_save;
    int     rounds;
    uint8   iv[16];
} __attribute__((aligned(16))); // L'allineamento a 16 byte è vitale per SSE

status_t BInitAESNICrypto();

#endif // _AES_NI_H_
