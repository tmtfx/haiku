/*
 * Software fallback for crypto operations
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef _SOFT_CRYPTO_H_
#define _SOFT_CRYPTO_H_

#include <SupportDefs.h>
#include "BCryptoCore.h"
//#include "BCryptoDefs.h"
#include <crypto/BCryptoDefs.h>
#include "soft_aes.h" // AES core

#ifdef __cplusplus
extern "C" {
#endif

/* BCryptoRequest adapter for software AES CBC */
status_t soft_aes_cbc_process(BCryptoRequest* request);

/* Register software AES fallback */
status_t BInitSoftCrypto(void);

#ifdef __cplusplus
}
#endif

#endif // _SOFT_CRYPTO_H_
