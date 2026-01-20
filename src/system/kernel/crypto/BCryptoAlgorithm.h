/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef _B_CRYPTO_ALGORITHM_H_
#define _B_CRYPTO_ALGORITHM_H_


#include <SupportDefs.h>
#include "BCryptoRequest.h"

struct BCryptoAlgorithm {
    BCryptoAlgorithmID  algorithm;
    uint32  flags;
    int32   priority;
    status_t (*Process)(BCryptoRequest* request);
};

#endif
