/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef _B_PADLOCK_RNG_H_
#define _B_PADLOCK_RNG_H_

#include <SupportDefs.h>
#include "../../BCryptoCore.h"
#include "../../BCryptoDefs.h"

// Registrazione RNG PadLock
status_t BInitPadLockRNG();

// Funzione interna per generare RNG (non esposta in header pubblico)
//static status_t padlock_rng_process(BCryptoRequest* request);

#endif
