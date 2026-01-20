/*
 * Hardware-accelerated AES using Intel/AMD AES-NI instructions
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef _AES_NI_H_
#define _AES_NI_H_

#include <SupportDefs.h>
#include "../../BCryptoDefs.h"
#include "../../BCryptoCore.h"

#if ARCH_X86
#include <emmintrin.h>
#endif

struct AESNIContext {
#if ARCH_X86
    __m128i encRoundKeys[15];
    __m128i decRoundKeys[15];
#endif
    size_t rounds;
    uint8  iv[16];
};

status_t BInitAESNICrypto();

#endif /* _AES_NI_H_ */

/*struct AESNIContext {
    uint8 roundKeys[240];  // max 14 rounds * 16 bytes
    size_t nr;             // number of rounds
    uint8 iv[16];          // IV for CBC
};*/
/*
status_t aesni_process_request(BCryptoRequest* request);

status_t BInitAESNICrypto();

#endif // _AES_NI_H_*/
