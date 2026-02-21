/*
 * Software Digest for Haiku kernel crypto
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include "soft_sha.h"
#include "soft_blake.h"
#include "BCryptoAlgorithm.h"
#include "BCryptoCore.h"
#include <crypto/BCryptoDefs.h>
#include <string.h>
//#include <Errors.h>
//#include <malloc.h>

/* ---------------------------------------------------------------- */
/* Public BCryptoRequest wrapper for Digest                        */
/* ---------------------------------------------------------------- */

////////   One-shot function    /////////
status_t
soft_digest_process(BCryptoRequest* request)
{
    if (!request || !request->source || !request->destination)
        return B_BAD_VALUE;
    
    size_t destLen = request->destination[0].iov_len;
    uint8 digest[64]; // Buffer temporaneo per contenere il digest massimo (BLAKE2b)
    memset(digest, 0, sizeof(digest));

    //if (request->algorithm != B_CRYPTO_SHA256)
    //    return B_NOT_SUPPORTED;
    switch (request->algorithm) {
        case B_CRYPTO_SHA256: {
        	if (destLen < 32) return B_BAD_VALUE;
        	
            SoftSHA256Context ctx;
            soft_sha256_init(&ctx);
            // Iteriamo sui vettori sorgente (Zero-Copy)
            // Non serve SMAP qui perché BCryptoCore ha già mappato/verificato la memoria
            /*
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
            memset(&ctx, 0, sizeof(ctx));
            break;
            */
            for (size_t i = 0; i < request->vectorCount; i++) {
                if (request->source[i].iov_base && request->source[i].iov_len > 0)
                    soft_sha256_update(&ctx, (const uint8*)request->source[i].iov_base, request->source[i].iov_len);
            }
            soft_sha256_finalize(&ctx, digest);
            memcpy(request->destination[0].iov_base, digest, 32);
            memset(&ctx, 0, sizeof(SoftSHA256Context));
            break;
        }
        case B_CRYPTO_BLAKE2B: {
        	if (destLen < 64) return B_BAD_VALUE;
            SoftBlake2bContext ctx;
            soft_blake2b_init(&ctx, 64);
            for (size_t i = 0; i < request->vectorCount; i++) {
                if (request->source[i].iov_base && request->source[i].iov_len > 0)
                    soft_blake2b_update(&ctx, (const uint8*)request->source[i].iov_base, request->source[i].iov_len);
            }
            soft_blake2b_finalize(&ctx, digest);
            memcpy(request->destination[0].iov_base, digest, 64);
            memset(&ctx, 0, sizeof(SoftBlake2bContext));
            break;
        }
        case B_CRYPTO_BLAKE2S: {
        	if (destLen < 32) return B_BAD_VALUE;
        	SoftBlake2sContext ctx;
            soft_blake2s_init(&ctx, 32);
            for (size_t i = 0; i < request->vectorCount; i++) {
                if (request->source[i].iov_base && request->source[i].iov_len > 0)
                    soft_blake2s_update(&ctx, (const uint8*)request->source[i].iov_base, request->source[i].iov_len);
            }
            soft_blake2s_finalize(&ctx, digest);
            memcpy(request->destination[0].iov_base, digest, 32);
            memset(&ctx, 0, sizeof(SoftBlake2sContext));
            break;
        }
        
        case B_CRYPTO_BLAKE3: {
        	if (destLen < 32) return B_BAD_VALUE;
            SoftBlake3Context ctx;
            soft_blake3_init(&ctx);
            for (size_t i = 0; i < request->vectorCount; i++) {
                if (request->source[i].iov_base && request->source[i].iov_len > 0)
                    soft_blake3_update(&ctx, (const uint8*)request->source[i].iov_base, request->source[i].iov_len);
            }
            soft_blake3_finalize(&ctx, digest, 32);
            memcpy(request->destination[0].iov_base, digest, 32);
            memset(&ctx, 0, sizeof(SoftBlake3Context));
            break;
        }
        
        
        default:
            return B_NOT_SUPPORTED;
    }
    
    memset(digest, 0, sizeof(digest));

    // Gestione asincrona (opzionale, come in SoftCrypto)
    if (request->completionCallback) {
        request->completionCallback(request, B_OK);
        return B_OK; 
    }

    return B_OK;
}

/* ---------------------------------------------------------------- */
/* Bridges for Multi-part Hash (Init/Update/Final)                  */
/* ---------------------------------------------------------------- */

// --- SHA256 BRIDGE ---

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
    memset(&ctx, 0, sizeof(SoftSHA256Context));
    return B_OK;
}

// --- BRIDGE PER BLAKE2B ---
static status_t
soft_blake2b_init_bridge(void** context, size_t* contextSize)
{
    if (context == NULL || contextSize == NULL) return B_BAD_VALUE;
    *contextSize = sizeof(SoftBlake2bContext);
    *context = malloc(*contextSize);
    if (*context == NULL) return B_NO_MEMORY;

    soft_blake2b_init((SoftBlake2bContext*)*context, 64);
    return B_OK;
}

static status_t
soft_blake2b_update_bridge(void* context, const iovec* vecs, size_t count)
{
	if (context == NULL) return B_BAD_VALUE;
    SoftBlake2bContext* ctx = (SoftBlake2bContext*)context;
    for (size_t i = 0; i < count; i++) {
        if (vecs[i].iov_base && vecs[i].iov_len > 0)
            soft_blake2b_update(ctx, (const uint8*)vecs[i].iov_base, vecs[i].iov_len);
    }
    return B_OK;
}

static status_t
soft_blake2b_final_bridge(void* context, uint8* outDigest)
{
	/*
    soft_blake2b_finalize((SoftBlake2bContext*)context, outDigest);
    return B_OK;
    */
    if (context == NULL || outDigest == NULL) return B_BAD_VALUE;
    SoftBlake2bContext* ctx = (SoftBlake2bContext*)context;
    soft_blake2b_finalize(ctx, outDigest);
    
    memset(ctx, 0, sizeof(SoftBlake2bContext)); // Cancella tracce sensibili
    // La free(context) verrà eseguita dal chiamante (BCryptoCore)
    return B_OK;
}

// --- BRIDGE PER BLAKE2S (32-bit) ---
static status_t
soft_blake2s_init_bridge(void** context, size_t* contextSize)
{
    if (context == NULL || contextSize == NULL) return B_BAD_VALUE;
    *contextSize = sizeof(SoftBlake2sContext);
    *context = malloc(*contextSize);
    if (*context == NULL) return B_NO_MEMORY;

    soft_blake2s_init((SoftBlake2sContext*)*context, 32);
    return B_OK;
}

static status_t
soft_blake2s_update_bridge(void* context, const iovec* vecs, size_t count)
{
	if (context == NULL) return B_BAD_VALUE;
    SoftBlake2sContext* ctx = (SoftBlake2sContext*)context;
    for (size_t i = 0; i < count; i++) {
        if (vecs[i].iov_base && vecs[i].iov_len > 0)
            soft_blake2s_update(ctx, (const uint8*)vecs[i].iov_base, vecs[i].iov_len);
    }
    return B_OK;
}

static status_t
soft_blake2s_final_bridge(void* context, uint8* outDigest)
{
	/*
    soft_blake2s_finalize((SoftBlake2sContext*)context, outDigest);
    return B_OK;
    */
    if (context == NULL || outDigest == NULL) return B_BAD_VALUE;
    SoftBlake2sContext* ctx = (SoftBlake2sContext*)context;
    soft_blake2s_finalize(ctx, outDigest);
    
    memset(ctx, 0, sizeof(SoftBlake2sContext)); // Cancella tracce sensibili
    // La free(context) verrà eseguita dal chiamante (BCryptoCore)
    return B_OK;
}

// --- BRIDGE PER BLAKE3 ---
static status_t
soft_blake3_init_bridge(void** context, size_t* contextSize)
{
    if (context == NULL || contextSize == NULL) return B_BAD_VALUE;
    *contextSize = sizeof(SoftBlake3Context);
    *context = malloc(*contextSize);
    if (*context == NULL) return B_NO_MEMORY;

    soft_blake3_init((SoftBlake3Context*)*context);
    return B_OK;
}

static status_t
soft_blake3_update_bridge(void* context, const iovec* vecs, size_t count)
{
	if (context == NULL) return B_BAD_VALUE;
    SoftBlake3Context* ctx = (SoftBlake3Context*)context;
    for (size_t i = 0; i < count; i++) {
        if (vecs[i].iov_base && vecs[i].iov_len > 0)
            soft_blake3_update(ctx, (const uint8*)vecs[i].iov_base, vecs[i].iov_len);
    }
    return B_OK;
}

static status_t
soft_blake3_final_bridge(void* context, uint8* outDigest)
{
	/*
    // Usiamo lo standard 32 byte per BLAKE3
    soft_blake3_finalize((SoftBlake3Context*)context, outDigest, 32);
    return B_OK;
    */
    if (context == NULL || outDigest == NULL) return B_BAD_VALUE;
    SoftBlake3Context* ctx = (SoftBlake3Context*)context;
    soft_blake3_finalize(ctx, outDigest, 32);
    
    memset(ctx, 0, sizeof(SoftBlake3Context)); // Cancella tracce sensibili
    // La free(context) verrà eseguita dal chiamante (BCryptoCore)
    return B_OK;
}

/* ---------------------------------------------------------------- */
/* Register software Digest                                         */
/* ---------------------------------------------------------------- */
status_t BInitSoftDigest()
{
    // Definiamo gli algoritmi in un array per pulizia
    static BCryptoAlgorithm sAlgos[] = {
        {
            .algorithm = B_CRYPTO_SHA256,
            .flags     = B_CRYPTO_ALG_SOFTWARE,
            .name      = "SHA256 (Software)",
            .priority  = 10,
            .Process   = soft_digest_process,
            .HashInit  = soft_sha256_init_bridge,
            .HashUpdate = soft_sha256_update_bridge,
            .HashFinal = soft_sha256_final_bridge
        },
        {
            .algorithm = B_CRYPTO_BLAKE2B,
            .flags     = B_CRYPTO_ALG_SOFTWARE,
            .name      = "BLAKE2b (Software)",
            .priority  = 10,
            .Process   = soft_digest_process,
            .HashInit  = soft_blake2b_init_bridge,
            .HashUpdate = soft_blake2b_update_bridge,
            .HashFinal = soft_blake2b_final_bridge
        },
        {
            .algorithm = B_CRYPTO_BLAKE2S,
            .flags     = B_CRYPTO_ALG_SOFTWARE,
            .name      = "BLAKE2s (Software)",
            .priority  = 8, // Leggermente meno prioritario su 64-bit
            .Process   = soft_digest_process,
            .HashInit  = soft_blake2s_init_bridge,
            .HashUpdate = soft_blake2s_update_bridge,
            .HashFinal = soft_blake2s_final_bridge
        },
        {
            .algorithm = B_CRYPTO_BLAKE3,
            .flags     = B_CRYPTO_ALG_SOFTWARE,
            .name      = "BLAKE3 (Software)",
            .priority  = 15,
            .Process   = soft_digest_process,
            .HashInit  = soft_blake3_init_bridge,
            .HashUpdate = soft_blake3_update_bridge,
            .HashFinal = soft_blake3_final_bridge
        }
    };

    status_t status = B_OK;
    for (size_t i = 0; i < sizeof(sAlgos)/sizeof(sAlgos[0]); i++) {
        status = BRegisterCryptoAlgorithm(&sAlgos[i]);
        if (status != B_OK) break;
    }

    return status;
}
