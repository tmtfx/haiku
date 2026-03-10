/*
 * Hardware-accelerated AES using Intel/AMD AES-NI instructions
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef _AES_NI_H_
#define _AES_NI_H_

#include <SupportDefs.h>
//#include <crypto/BCryptoDefs.h>
//#include <crypto/BCryptoKernelInternal.h>
//#include "BCryptoCore.h"

//#include <immintrin.h> // Necessario per __m128i
/*
struct AESNIContext {
	alignas(64) BCryptoFPUContext fpu_save;
    alignas(16) __m128i encRoundKeys[15]; // Massimo 14 round per AES-256 + 1
    alignas(16) __m128i decRoundKeys[15];
    int     rounds;
    uint8   iv[16];
    
    //uint8 h_key[16];      // Costante H per GHASH
    //uint8 tag_acc[16];    // Accumulatore parziale del Tag
    //uint8 counter[16];    // Stato attuale del contatore CTR
    //uint64 total_len;     // Bit totali processati (per il blocco finale)
    //bool is_encrypting;   // Stato per sapere se stiamo cifrando o decifrando
} __attribute__((aligned(64))); // L'allineamento a 16 byte è vitale per SSE*/
typedef void (*ghash_multiply_func)(uint8* x, const uint8* h);

status_t BInitAESNICrypto();

#endif // _AES_NI_H_
