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

////////   One-shot function    /////////
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

static status_t
soft_sha256_init_bridge(void** context, size_t* contextSize)
{
    if (context == NULL || contextSize == NULL)
        return B_BAD_VALUE;

    *contextSize = sizeof(SoftSHA256Context);
    *context = malloc(*contextSize);
    if (*context == NULL)
        return B_NO_MEMORY;

    soft_sha256_init((SoftSHA256Context*)*context);
    return B_OK;
}

static status_t
soft_sha256_update_bridge(void* context, const iovec* vecs, size_t count)
{
    if (context == NULL || (vecs == NULL && count > 0))
        return B_BAD_VALUE;

    SoftSHA256Context* ctx = (SoftSHA256Context*)context;

    for (size_t i = 0; i < count; i++) {
        if (vecs[i].iov_base != NULL && vecs[i].iov_len > 0)
            soft_sha256_update(ctx, (const uint8*)vecs[i].iov_base, vecs[i].iov_len);
    }

    return B_OK;
}

static status_t
soft_sha256_final_bridge(void* context, uint8* outDigest)
{
    if (context == NULL || outDigest == NULL)
        return B_BAD_VALUE;

    SoftSHA256Context* ctx = (SoftSHA256Context*)context;
    soft_sha256_finalize(ctx, outDigest);

    // Nota: la free(context) la farà il Core dopo aver chiamato Final
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
        .Process   = soft_digest_process,
        
        .HashInit   = soft_sha256_init_bridge,
        .HashUpdate = soft_sha256_update_bridge,
        .HashFinal  = soft_sha256_final_bridge
    };

    return BRegisterCryptoAlgorithm(&sSoftSHA256);
}
