/*
 * Hardware-accelerated AES using Intel/AMD AES-NI instructions
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef _AES_NI_H_
#define _AES_NI_H_

#include <SupportDefs.h>
#include "BCryptoDefs.h"
#include "BCryptoCore.h"

#include <immintrin.h> // Necessario per __m128i

struct AESNIContext {
    __m128i encRoundKeys[15]; // Massimo 14 round per AES-256 + 1
    __m128i decRoundKeys[15];
    int     rounds;
    uint8   iv[16];
} __attribute__((aligned(16))); // L'allineamento a 16 byte è vitale per SSE

// AGGIUNTA QUI:
#ifdef __cplusplus
extern "C" {
#endif

status_t BInitAESNICrypto();

#ifdef __cplusplus
}
#endif

#endif

/*struct AESNIContext {
    uint8 roundKeys[240];  // max 14 rounds * 16 bytes
    size_t nr;             // number of rounds
    uint8 iv[16];          // IV for CBC
};*/
/*
status_t aesni_process_request(BCryptoRequest* request);

status_t BInitAESNICrypto();

#endif // _AES_NI_H_*/
