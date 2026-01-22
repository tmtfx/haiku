/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef _B_CRYPTO_CORE_H_
#define _B_CRYPTO_CORE_H_

#include <SupportDefs.h>
#include "BCryptoAlgorithm.h"

status_t BRegisterCryptoAlgorithm(BCryptoAlgorithm* algorithm);
status_t BUnregisterCryptoAlgorithm(BCryptoAlgorithmID algorithm);

status_t BSubmitCryptoRequest(BCryptoRequest* request);

#endif

