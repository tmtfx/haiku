/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef _B_CRYPTO_CPU_H_
#define _B_CRYPTO_CPU_H_

#include <crypto/BCryptoDefs.h>

/*
 * Rileva le capacità crittografiche della CPU (AES-NI, SHA, VIA PadLock)
 * e riempie la struttura crypto_device_info fornita.
 */
status_t BGetCPUCryptoInfo(crypto_device_info* info);

#endif // _B_CRYPTO_CPU_H_
