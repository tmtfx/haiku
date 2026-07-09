/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef _B_CRYPTO_ENTROPY_H_
#define _B_CRYPTO_ENTROPY_H_

#include <crypto/BCryptoDefs.h>

status_t EntropyManager();
status_t BStartEntropyFeeder(); // Avvia il thread in background
void     BStopEntropyFeeder();  // Ferma il thread
status_t EntropyManager();      // Esegue un singolo versamento (opzionale)

#endif

