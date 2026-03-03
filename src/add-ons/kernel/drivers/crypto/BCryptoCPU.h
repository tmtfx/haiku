/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef _B_CRYPTO_CPU_H_
#define _B_CRYPTO_CPU_H_

#include <crypto/BCryptoDefs.h>
#include <crypto/BCryptoKernelInternal.h>
#include <x86gprintrin.h>
//#include <KernelExport.h>

extern bool gHasXsave;
extern uint64 gXsaveMask;

typedef struct alignas(64) {
    uint8_t buffer[1024]; // 512 minimi, 1024 per sicurezza con AVX/XSAVE
} fpu_state_t;

/*
 * Rileva le capacità crittografiche della CPU (AES-NI, SHA, VIA PadLock)
 * e riempie la struttura crypto_device_info fornita.
 */
status_t BGetCPUCryptoInfo(crypto_device_info* info);

#endif // _B_CRYPTO_CPU_H_
