/*
 * Software Digest (SHA-256) for Haiku kernel crypto
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include "soft_sha.h"
#include "BCryptoAlgorithm.h"
#include "BCryptoCore.h"
#include <crypto/BCryptoDefs.h>
#include <string.h>
//#include <Errors.h>

/* ---------------------------------------------------------------- */
/* Public BCryptoRequest wrapper for Digest                        */
/* ---------------------------------------------------------------- */
status_t
soft_digest_process(BCryptoRequest* request)
{
    if (!request || !request->source || !request->destination)
        return B_BAD_VALUE;

    if (request->algorithm != B_CRYPTO_SHA256)
        return B_NOT_SUPPORTED;

    SoftSHA256Context ctx;
    soft_sha256_init(&ctx);

    // Iteriamo sui vettori sorgente (Zero-Copy)
    // Non serve SMAP qui perché BCryptoCore ha già mappato/verificato la memoria
    for (size_t i = 0; i < request->vectorCount; i++) {
        const uint8* data = (const uint8*)request->source[i].iov_base;
        size_t len = request->source[i].iov_len;
        
        if (len > 0)
            soft_sha256_update(&ctx, data, len);
    }

    uint8 digest[32];
    soft_sha256_finalize(&ctx, digest);

    // Scriviamo il risultato nel primo iovec di destinazione dell'utente
    // Nota: SHA256 produce sempre 32 byte.
    if (request->destination[0].iov_len >= 32) {
        memcpy(request->destination[0].iov_base, digest, 32);
    } else {
        return B_BAD_VALUE; // Buffer di destinazione troppo piccolo
    }

    // Gestione asincrona (opzionale, come in SoftCrypto)
    if (request->completionCallback) {
        request->completionCallback(request, B_OK);
        return B_OK; 
    }

    return B_OK;
}

/* ---------------------------------------------------------------- */
/* Register software Digest                                         */
/* ---------------------------------------------------------------- */
status_t BInitSoftDigest()
{
    static BCryptoAlgorithm sSoftSHA256 = {
        .algorithm = B_CRYPTO_SHA256,
        .mode      = B_CRYPTO_MODE_ANY,
        .flags     = B_CRYPTO_ALG_SOFTWARE,
        .priority  = 10,             // Priorità software (bassa)
        .Process   = soft_digest_process
    };

    return BRegisterCryptoAlgorithm(&sSoftSHA256);
}
