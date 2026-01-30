/*
 * Software fallback for crypto operations
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef _SOFT_CRYPTO_H_
#define _SOFT_CRYPTO_H_

#include <SupportDefs.h>
#include "BCryptoCore.h"
#include <crypto/BCryptoDefs.h>

/* BCryptoRequest adapter for software AES CBC */
status_t soft_aes_cbc_process(BCryptoRequest* request);

/* Register software AES fallback */
status_t BInitSoftCrypto(void);

#endif // _SOFT_CRYPTO_H_
