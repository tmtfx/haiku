/*
 * Software Digest (SHA-256) for Haiku kernel crypto
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef _SOFT_DIGEST_H_
#define _SOFT_DIGEST_H_

#include <SupportDefs.h>
#include <crypto/BCryptoDefs.h>

/* Inizializza e registra l'algoritmo di digest software nel core */
status_t BInitSoftDigest();

/* Funzione di processing (esposta per BCryptoAlgorithm) */
status_t soft_digest_process(BCryptoRequest* request);

#endif // _SOFT_DIGEST_H_
