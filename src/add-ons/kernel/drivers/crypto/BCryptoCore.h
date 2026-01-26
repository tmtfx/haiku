/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
 /*
#ifndef _B_CRYPTO_CORE_H_
#define _B_CRYPTO_CORE_H_

#include <SupportDefs.h>
#include "BCryptoAlgorithm.h"

status_t BRegisterCryptoAlgorithm(BCryptoAlgorithm* algorithm);
status_t BUnregisterCryptoAlgorithm(BCryptoAlgorithmID algorithm);

status_t BSubmitCryptoRequest(BCryptoRequest* request);

#endif

*/
#ifndef _B_CRYPTO_CORE_H_
#define _B_CRYPTO_CORE_H_

//#include "BCryptoDefs.h"
#include <crypto/BCryptoDefs.h>
#include "BCryptoAlgorithm.h"

#ifdef __cplusplus
extern "C" {
#endif

status_t crypto_init_core();
void     crypto_uninit_core();

// Questa è la riga incriminata: controlla che il nome sia identico
uint32   BGetStoredCryptoCapabilities();

status_t BRegisterCryptoAlgorithm(BCryptoAlgorithm* algorithm);
status_t BUnregisterCryptoAlgorithm(BCryptoAlgorithmID algorithm);
status_t BSubmitCryptoRequest(BCryptoRequest* request);

#ifdef __cplusplus
}
#endif

#endif // _B_CRYPTO_CORE_H_
