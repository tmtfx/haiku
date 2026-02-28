/*
 * SHA Hybrid Optimized Digest for Haiku kernel crypto
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include "soft_sha.h"     
#include "hybrid_sha_opt.h"
#include "HybridSHADigest.h"
#include "BCryptoAlgorithm.h"
#include "BCryptoCore.h"
#include "BCryptoCPU.h"
#include <crypto/BCryptoDefs.h>
#include <arch/x86/arch_cpu.h>
#include <string.h>
#include <malloc.h>
#include <stdlib.h>
#include <cstdio>


static void
_free_context(void* ctx, size_t size)
{
    if (ctx) {
        memset(ctx, 0, size);
        free(ctx);
    }
}

/*
static status_t
hybrid_sha_process(BCryptoRequest* request)
{
	if (request == NULL || !request->source || !request->destination)
        return B_BAD_VALUE;

    size_t destLen = request->destination[0].iov_len;
    uint8 digest[64]; // Buffer per il risultato finale
    BCryptoAlgorithmID algo = request->algorithm;
    int algolen = decode_hash_length(algo);
    
    //switch(request->algorithm) {
    switch(algo) {
    	case B_CRYPTO_SHA1: {
    		if (destLen < 20) return B_BAD_VALUE;
            SoftSHA1Context* ctx = (SoftSHA1Context*)malloc(sizeof(SoftSHA1Context));
            if (ctx == NULL) return B_NO_MEMORY;

            hybrid_SHA1_init_bridge(ctx, 20);
            for (size_t i = 0; i < request->vectorCount; i++) {
                if (request->source[i].iov_base && request->source[i].iov_len > 0)
                    hybrid_SHA1_update_bridge(ctx, (const uint8*)request->source[i].iov_base, request->source[i].iov_len);
            }
            hybrid_SHA1_final_bridge(ctx, digest);
            memcpy(request->destination[0].iov_base, digest, 20);

            memset(ctx, 0, sizeof(SoftSHA1Context));
            free(ctx);
    		break;
    	}
    	case B_CRYPTO_SHA224: {
    		if (destLen < 28) return B_BAD_VALUE;
            SoftSHA256Context* ctx = (SoftSHA256Context*)malloc(sizeof(SoftSHA256Context));
            if (ctx == NULL) return B_NO_MEMORY;

            hybrid_SHA224_init_bridge(ctx, 28);
            for (size_t i = 0; i < request->vectorCount; i++) {
                if (request->source[i].iov_base && request->source[i].iov_len > 0)
                    hybrid_SHA224_update_bridge(ctx, (const uint8*)request->source[i].iov_base, request->source[i].iov_len);
            }
            hybrid_SHA224_final_bridge(ctx, digest);
            memcpy(request->destination[0].iov_base, digest, 28);

            memset(ctx, 0, sizeof(SoftSHA256Context));
            free(ctx);
    		break;
    	}
    	case B_CRYPTO_SHA256: {
    		if (destLen < 32) return B_BAD_VALUE;
            SoftSHA256Context* ctx = (SoftSHA256Context*)malloc(sizeof(SoftSHA256Context));
            if (ctx == NULL) return B_NO_MEMORY;

            hybrid_SHA256_init_bridge(ctx, 32);
            for (size_t i = 0; i < request->vectorCount; i++) {
                if (request->source[i].iov_base && request->source[i].iov_len > 0)
                    hybrid_SHA256_update_bridge(ctx, (const uint8*)request->source[i].iov_base, request->source[i].iov_len);
            }
            hybrid_SHA256_final_bridge(ctx, digest);
            memcpy(request->destination[0].iov_base, digest, 32);

            memset(ctx, 0, sizeof(SoftSHA256Context));
            free(ctx);
    		//hybrid_SHA256_init_bridge(&ctx, &ctxSize);
            //hybrid_SHA256_update_bridge(ctx, request->source, request->sourceCount);
            //hybrid_SHA256_final_bridge(ctx, request->destination[0].iov_base);
    		break;
    	}
    	case B_CRYPTO_SHA384: {
    		if (destLen < 48) return B_BAD_VALUE;
            SoftSHA512Context* ctx = (SoftSHA512Context*)malloc(sizeof(SoftSHA512Context));
            if (ctx == NULL) return B_NO_MEMORY;

            hybrid_SHA384_init_bridge(ctx, 48);
            for (size_t i = 0; i < request->vectorCount; i++) {
                if (request->source[i].iov_base && request->source[i].iov_len > 0)
                    hybrid_SHA384_update_bridge(ctx, (const uint8*)request->source[i].iov_base, request->source[i].iov_len);
            }
            hybrid_SHA384_final_bridge(ctx, digest);
            memcpy(request->destination[0].iov_base, digest, 48);

            memset(ctx, 0, sizeof(SoftSHA512Context));
            free(ctx);
    		break;
    	}
    	case B_CRYPTO_SHA3_512: {
    		if (destLen < 64) return B_BAD_VALUE;
            SoftSHA512Context* ctx = (SoftSHA512Context*)malloc(sizeof(SoftSHA512Context));
            if (ctx == NULL) return B_NO_MEMORY;

            hybrid_SHA512_init_bridge(ctx, 64);
            for (size_t i = 0; i < request->vectorCount; i++) {
                if (request->source[i].iov_base && request->source[i].iov_len > 0)
                    hybrid_SHA512_update_bridge(ctx, (const uint8*)request->source[i].iov_base, request->source[i].iov_len);
            }
            hybrid_SHA512_final_bridge(ctx, digest);
            memcpy(request->destination[0].iov_base, digest, 64);

            memset(ctx, 0, sizeof(SoftSHA512Context));
            free(ctx);
    		break;
    	}
    	default:
            return B_NOT_SUPPORTED;
    }
    if (request->completionCallback)
        request->completionCallback(request, B_OK);

    return B_OK;
}
*/
status_t
hybrid_sha_process(BCryptoRequest* request)
{
    if (request == NULL || !request->source || !request->destination)
        return B_BAD_VALUE;

    void* ctx = NULL;
    size_t ctxSize = 0;
    uint8 digest[64];
    status_t st = B_OK;

    // 1. Init (Alloca tramite bridge)
    switch (request->algorithm) {
        case B_CRYPTO_SHA1:   st = hybrid_SHA1_init_bridge(&ctx, &ctxSize); break;
        case B_CRYPTO_SHA224: st = hybrid_SHA224_init_bridge(&ctx, &ctxSize); break;
        case B_CRYPTO_SHA256: st = hybrid_SHA256_init_bridge(&ctx, &ctxSize); break;
        case B_CRYPTO_SHA384: st = hybrid_SHA384_init_bridge(&ctx, &ctxSize); break;
        case B_CRYPTO_SHA512: st = hybrid_SHA512_init_bridge(&ctx, &ctxSize); break;
        default: return B_NOT_SUPPORTED;
    }

    if (st != B_OK) return st;

    // 2. Update
    switch (request->algorithm) {
        case B_CRYPTO_SHA1:   st = hybrid_SHA1_update_bridge(ctx, request->source, request->vectorCount); break;
        case B_CRYPTO_SHA224: st = hybrid_SHA256_update_bridge(ctx, request->source, request->vectorCount); break;
        case B_CRYPTO_SHA256: st = hybrid_SHA256_update_bridge(ctx, request->source, request->vectorCount); break;
        case B_CRYPTO_SHA384: st = hybrid_SHA512_update_bridge(ctx, request->source, request->vectorCount); break;
        case B_CRYPTO_SHA512: st = hybrid_SHA512_update_bridge(ctx, request->source, request->vectorCount); break;
        default: return B_NOT_SUPPORTED;
    }

    // 3. Final
    if (st == B_OK) {
        // Chiamiamo i bridge final. NOTA: i bridge ora liberano la memoria internamente!
        switch (request->algorithm) {
            case B_CRYPTO_SHA1:   st = hybrid_SHA1_final_bridge(ctx, digest); break;
            case B_CRYPTO_SHA224: st = hybrid_SHA224_final_bridge(ctx, digest); break;
            case B_CRYPTO_SHA256: st = hybrid_SHA256_final_bridge(ctx, digest); break;
            case B_CRYPTO_SHA384: st = hybrid_SHA384_final_bridge(ctx, digest); break;
            case B_CRYPTO_SHA512: st = hybrid_SHA512_final_bridge(ctx, digest); break;
            default: return B_NOT_SUPPORTED;
        }
        
        if (st == B_OK) {
            size_t outLen = decode_hash_length(request->algorithm);
            memcpy(request->destination[0].iov_base, digest, outLen);
        }
        // Dato che i Final bridge hanno già fatto free(ctx), mettiamo a NULL per sicurezza
        ctx = NULL;
    }

    // Se qualcosa è fallito prima del Final, liberiamo qui
    if (ctx != NULL)
        _free_context(ctx, ctxSize);

    if (request->completionCallback)
        request->completionCallback(request, st);

    return st;
}
/*
// --- BRIDGE PER SHA1 ---
static status_t
hybrid_SHA1_init_bridge(void** context, size_t* contextSize)
{
    if (context == NULL || contextSize == NULL) return B_BAD_VALUE;
    *contextSize = sizeof(SoftSHA1Context); // SoftSHA1Context da implementare in soft_sha.h
    *context = malloc(*contextSize);
    if (*context == NULL) return B_NO_MEMORY;

    hybrid_SHA1_init((SoftSHA1Context*)*context, 64); //hybrid_SHA1_init in hybrid_sha_opt.cpp
    return B_OK;
}

static status_t
hybrid_SHA1_update_bridge(void* context, const iovec* vecs, size_t count)
{
    if (context == NULL) return B_BAD_VALUE;
    SoftSHA1Context* ctx = (SoftSHA1Context*)context;
    for (size_t i = 0; i < count; i++) {
        if (vecs[i].iov_base && vecs[i].iov_len > 0)
            hybrid_SHA1_update(ctx, (const uint8*)vecs[i].iov_base, vecs[i].iov_len);
    }
    return B_OK;
}

static status_t
hybrid_SHA1_final_bridge(void* context, uint8* outDigest)
{
    if (context == NULL || outDigest == NULL) return B_BAD_VALUE;
    SoftSHA1Context* ctx = (SoftSHA1Context*)context;
    hybrid_SHA1_finalize(ctx, outDigest);
    memset(ctx, 0, sizeof(SoftSHA1Context));
    return B_OK;
}

// --- BRIDGE PER SHA224 ---
static status_t
hybrid_SHA224_init_bridge(void** context, size_t* contextSize)
{
    if (context == NULL || contextSize == NULL) return B_BAD_VALUE;
    *contextSize = sizeof(SoftSHA256Context);
    *context = malloc(*contextSize);
    if (*context == NULL) return B_NO_MEMORY;

    hybrid_SHA224_init((SoftSHA256Context*)*context, 32);
    return B_OK;
}

static status_t
hybrid_SHA224_update_bridge(void* context, const iovec* vecs, size_t count)
{
    if (context == NULL) return B_BAD_VALUE;
    SoftSHA256Context* ctx = (SoftSHA256Context*)context;
    for (size_t i = 0; i < count; i++) {
        if (vecs[i].iov_base && vecs[i].iov_len > 0)
            hybrid_SHA224_update(ctx, (const uint8*)vecs[i].iov_base, vecs[i].iov_len);
    }
    return B_OK;
}

static status_t
hybrid_SHA224_final_bridge(void* context, uint8* outDigest)
{
    if (context == NULL || outDigest == NULL) return B_BAD_VALUE;
    SoftSHA256Context* ctx = (SoftSHA256Context*)context;
    hybrid_SHA224_finalize(ctx, outDigest);
    memset(ctx, 0, sizeof(SoftSHA256Context));
    return B_OK;
}

// --- BRIDGE PER SHA256 ---
static status_t
hybrid_SHA256_init_bridge(void** context, size_t* contextSize)
{
    if (context == NULL || contextSize == NULL) return B_BAD_VALUE;
    *contextSize = sizeof(SoftSHA256Context);
    *context = malloc(*contextSize);
    if (*context == NULL) return B_NO_MEMORY;

    hybrid_SHA256_init((SoftSHA256Context*)*context, 32);
    return B_OK;
}

static status_t
hybrid_SHA256_update_bridge(void* context, const iovec* vecs, size_t count)
{
    if (context == NULL) return B_BAD_VALUE;
    SoftSHA256Context* ctx = (SoftSHA256Context*)context;
    for (size_t i = 0; i < count; i++) {
        if (vecs[i].iov_base && vecs[i].iov_len > 0)
            hybrid_SHA256_update(ctx, (const uint8*)vecs[i].iov_base, vecs[i].iov_len);
    }
    return B_OK;
}

static status_t
hybrid_SHA256_final_bridge(void* context, uint8* outDigest)
{
    if (context == NULL || outDigest == NULL) return B_BAD_VALUE;
    SoftSHA256Context* ctx = (SoftSHA256Context*)context;
    hybrid_SHA256_finalize(ctx, outDigest);
    memset(ctx, 0, sizeof(SoftSHA256Context));
    return B_OK;
}
// --- BRIDGE PER SHA384 ---
static status_t
hybrid_SHA384_init_bridge(void** context, size_t* contextSize)
{
    if (context == NULL || contextSize == NULL) return B_BAD_VALUE;
    *contextSize = sizeof(SoftSHA512Context);
    *context = malloc(*contextSize);
    if (*context == NULL) return B_NO_MEMORY;

    hybrid_SHA384_init((SoftSHA512Context*)*context, 32);
    return B_OK;
}

static status_t
hybrid_SHA384_update_bridge(void* context, const iovec* vecs, size_t count)
{
    if (context == NULL) return B_BAD_VALUE;
    SoftSHA512Context* ctx = (SoftSHA512Context*)context;
    for (size_t i = 0; i < count; i++) {
        if (vecs[i].iov_base && vecs[i].iov_len > 0)
            hybrid_SHA384_update(ctx, (const uint8*)vecs[i].iov_base, vecs[i].iov_len);
    }
    return B_OK;
}

static status_t
hybrid_SHA384_final_bridge(void* context, uint8* outDigest)
{
    if (context == NULL || outDigest == NULL) return B_BAD_VALUE;
    SoftSHA512Context* ctx = (SoftSHA512Context*)context;
    hybrid_SHA384_finalize(ctx, outDigest);
    memset(ctx, 0, sizeof(SoftSHA512Context));
    return B_OK;
}
// --- BRIDGE PER SHA512 ---*/
/*static status_t
hybrid_SHA512_init_bridge(void** context, size_t* contextSize)
{
    if (context == NULL || contextSize == NULL) return B_BAD_VALUE;
    *contextSize = sizeof(SoftSHA512Context);
    *context = malloc(*contextSize);
    if (*context == NULL) return B_NO_MEMORY;

    hybrid_SHA512_init((SoftSHA512Context*)*context, 32);
    return B_OK;
}*/
/* --- BRIDGE PER SHA1 --- */

static status_t
hybrid_SHA1_init_bridge(void** context, size_t* contextSize)
{
    if (context == NULL || contextSize == NULL) return B_BAD_VALUE;
    *contextSize = sizeof(SoftSHA1Context);
    *context = malloc(*contextSize);
    if (*context == NULL) return B_NO_MEMORY;

    hybrid_SHA1_init((SoftSHA1Context*)*context, 20);
    return B_OK;
}

static status_t
hybrid_SHA1_update_bridge(void* context, const iovec* vecs, size_t count)
{
    if (context == NULL) return B_BAD_VALUE;
    SoftSHA1Context* ctx = (SoftSHA1Context*)context;
    for (size_t i = 0; i < count; i++) {
        if (vecs[i].iov_base && vecs[i].iov_len > 0)
            hybrid_SHA1_update(ctx, (const uint8*)vecs[i].iov_base, vecs[i].iov_len);
    }
    return B_OK;
}

static status_t
hybrid_SHA1_final_bridge(void* context, uint8* outDigest)
{
    if (context == NULL || outDigest == NULL) return B_BAD_VALUE;
    SoftSHA1Context* ctx = (SoftSHA1Context*)context;
    hybrid_SHA1_finalize(ctx, outDigest);
    _free_context(ctx, sizeof(SoftSHA1Context));
    return B_OK;
}

/* --- BRIDGE PER SHA224 --- */
static status_t
hybrid_SHA224_init_bridge(void** context, size_t* contextSize)
{
    if (context == NULL || contextSize == NULL) return B_BAD_VALUE;
    *contextSize = sizeof(SoftSHA256Context);
    *context = malloc(*contextSize);
    if (*context == NULL) return B_NO_MEMORY;

    hybrid_SHA224_init((SoftSHA256Context*)*context, 28);
    return B_OK;
}

static status_t
hybrid_SHA224_update_bridge(void* context, const iovec* vecs, size_t count)
{
	if (context == NULL) return B_BAD_VALUE;
    SoftSHA256Context* ctx = (SoftSHA256Context*)context;
    for (size_t i = 0; i < count; i++) {
        if (vecs[i].iov_base && vecs[i].iov_len > 0) {
            // Chiamiamo la funzione di update core (che è la stessa per 224 e 256)
            hybrid_SHA256_update(ctx, (const uint8*)vecs[i].iov_base, vecs[i].iov_len); //stessa logica di compressione usiamo sha256
        }
    }
    return B_OK;
}

static status_t
hybrid_SHA224_final_bridge(void* context, uint8* outDigest)
{
    if (context == NULL || outDigest == NULL) return B_BAD_VALUE;
    SoftSHA256Context* ctx = (SoftSHA256Context*)context;
    hybrid_SHA224_finalize(ctx, outDigest);
    _free_context(ctx, sizeof(SoftSHA256Context));
    return B_OK;
}

/* --- BRIDGE PER SHA256 --- */
static status_t
hybrid_SHA256_init_bridge(void** context, size_t* contextSize)
{
    if (context == NULL || contextSize == NULL) return B_BAD_VALUE;
    *contextSize = sizeof(SoftSHA256Context);
    *context = malloc(*contextSize);
    if (*context == NULL) return B_NO_MEMORY;

    hybrid_SHA256_init((SoftSHA256Context*)*context, 32);
    return B_OK;
}

static status_t
hybrid_SHA256_update_bridge(void* context, const iovec* vecs, size_t count)
{
    if (context == NULL) return B_BAD_VALUE;
    SoftSHA256Context* ctx = (SoftSHA256Context*)context;
    for (size_t i = 0; i < count; i++) {
        if (vecs[i].iov_base && vecs[i].iov_len > 0)
            hybrid_SHA256_update(ctx, (const uint8*)vecs[i].iov_base, vecs[i].iov_len);
    }
    return B_OK;
}

static status_t
hybrid_SHA256_final_bridge(void* context, uint8* outDigest)
{
    if (context == NULL || outDigest == NULL) return B_BAD_VALUE;
    SoftSHA256Context* ctx = (SoftSHA256Context*)context;
    hybrid_SHA256_finalize(ctx, outDigest);
    _free_context(ctx, sizeof(SoftSHA256Context));
    return B_OK;
}

/* --- BRIDGE PER SHA384 --- */
static status_t
hybrid_SHA384_init_bridge(void** context, size_t* contextSize)
{
    if (context == NULL || contextSize == NULL) return B_BAD_VALUE;
    *contextSize = sizeof(SoftSHA512Context);
    *context = memalign(32, *contextSize);
    if (*context == NULL) return B_NO_MEMORY;

    hybrid_SHA384_init((SoftSHA512Context*)*context, 48);
    return B_OK;
}

static status_t
hybrid_SHA384_update_bridge(void* context, const iovec* vecs, size_t count)
{
    if (context == NULL || vecs == NULL) return B_BAD_VALUE;
    
    // SHA-384 usa il contesto SHA-512
    SoftSHA512Context* ctx = (SoftSHA512Context*)context;
    
    for (size_t i = 0; i < count; i++) {
        if (vecs[i].iov_base && vecs[i].iov_len > 0) {
            // Chiamiamo la funzione di update core (comune a 384 e 512)
            hybrid_SHA512_update(ctx, (const uint8*)vecs[i].iov_base, vecs[i].iov_len); //stessa logica di compressione usiamo sha512
        }
    }
    return B_OK;
}

static status_t
hybrid_SHA384_final_bridge(void* context, uint8* outDigest)
{
    if (context == NULL || outDigest == NULL) return B_BAD_VALUE;
    SoftSHA512Context* ctx = (SoftSHA512Context*)context;
    hybrid_SHA384_finalize(ctx, outDigest);
    _free_context(ctx, sizeof(SoftSHA512Context));
    return B_OK;
}

/* --- BRIDGE PER SHA512 --- */
static status_t
hybrid_SHA512_init_bridge(void** context, size_t* contextSize)
{
    if (context == NULL || contextSize == NULL) return B_BAD_VALUE;
    *contextSize = sizeof(SoftSHA512Context);
    *context = memalign(32, *contextSize);
    if (*context == NULL) return B_NO_MEMORY;

    hybrid_SHA512_init((SoftSHA512Context*)*context, 64);
    return B_OK;
}

static status_t
hybrid_SHA512_update_bridge(void* context, const iovec* vecs, size_t count)
{
    if (context == NULL) return B_BAD_VALUE;
    SoftSHA512Context* ctx = (SoftSHA512Context*)context;
    for (size_t i = 0; i < count; i++) {
        if (vecs[i].iov_base && vecs[i].iov_len > 0)
            hybrid_SHA512_update(ctx, (const uint8*)vecs[i].iov_base, vecs[i].iov_len);
    }
    return B_OK;
}

static status_t
hybrid_SHA512_final_bridge(void* context, uint8* outDigest)
{
    if (context == NULL || outDigest == NULL) return B_BAD_VALUE;
    SoftSHA512Context* ctx = (SoftSHA512Context*)context;
    hybrid_SHA512_finalize(ctx, outDigest);
    _free_context(ctx, sizeof(SoftSHA512Context));
    return B_OK;
}

/* ---------------------------------------------------------------- */
/* Registration                                                     */
/* ---------------------------------------------------------------- */
static BCryptoAlgorithm sSHA1;
static BCryptoAlgorithm sSHA224;
static BCryptoAlgorithm sSHA256;
static BCryptoAlgorithm sSHA384;
static BCryptoAlgorithm sSHA512;

status_t 
BInitHybridSHADigest() 
{
#if defined(__x86_64__) || defined(__i386__)
    //bool hasSSE41 = x86_check_feature(IA32_FEATURE_EXT_SSE4_1, FEATURE_EXT);

    //bool hasAVX2 = x86_check_feature(IA32_FEATURE_AVX2, FEATURE_7_EBX);

    //if (!hasSSE41) return B_NOT_SUPPORTED;
    
    //hybrid_set_use_avx2(hasAVX2);
    
    //uint32 priority = hasAVX2 ? 30 : 20;
    //const char* suffix = hasAVX2 ? "(Hybrid/AVX2)" : "(Hybrid/SSE4.1)";
    if (!gHasSSE41) return B_NOT_SUPPORTED;

    int priority = gHasAVX2 ? 30 : 20;
    const char* suffix = gHasAVX2 ? "(Hybrid/AVX2)" : "(Hybrid/SSE4.1)";

    // --- SHA1: SSE4.1 è sempre presente se siamo qui ---
    sSHA1 = {
        .algorithm = B_CRYPTO_SHA1,
        .flags     = B_CRYPTO_ALG_SOFTWARE,
        .priority  = priority,
        .Process   = hybrid_sha_process,
        .HashInit  = hybrid_SHA1_init_bridge,
        .HashUpdate = hybrid_SHA1_update_bridge,
        .HashFinal = hybrid_SHA1_final_bridge
    };
    snprintf(sSHA1.name, sizeof(sSHA1.name), "SHA1 %s", suffix);
    BRegisterCryptoAlgorithm(&sSHA1);
    
    // --- SHA224 ---
    sSHA224 = {
        .algorithm = B_CRYPTO_SHA224,
        .flags     = B_CRYPTO_ALG_SOFTWARE,
        .priority  = priority,
        .Process   = hybrid_sha_process,
        .HashInit  = hybrid_SHA224_init_bridge,
        .HashUpdate = hybrid_SHA224_update_bridge,
        .HashFinal = hybrid_SHA224_final_bridge
    };
    snprintf(sSHA224.name, sizeof(sSHA224.name), "SHA224 %s", suffix);
    BRegisterCryptoAlgorithm(&sSHA224);
    
    // --- SHA256 ---
    sSHA256 = {
        .algorithm = B_CRYPTO_SHA256,
        .flags     = B_CRYPTO_ALG_SOFTWARE,
        .priority  = priority,
        .Process   = hybrid_sha_process,
        .HashInit  = hybrid_SHA256_init_bridge,
        .HashUpdate = hybrid_SHA256_update_bridge,
        .HashFinal = hybrid_SHA256_final_bridge
    };
    snprintf(sSHA256.name, sizeof(sSHA256.name), "SHA256 %s", suffix);
    BRegisterCryptoAlgorithm(&sSHA256);

    // --- SHA384 ---
    sSHA384 = {
        .algorithm = B_CRYPTO_SHA384,
        .flags     = B_CRYPTO_ALG_SOFTWARE,
        .priority  = priority,
        .Process   = hybrid_sha_process,
        .HashInit  = hybrid_SHA384_init_bridge,
        .HashUpdate = hybrid_SHA384_update_bridge,
        .HashFinal = hybrid_SHA384_final_bridge
    };
    snprintf(sSHA384.name, sizeof(sSHA384.name), "SHA384 %s", suffix);
    BRegisterCryptoAlgorithm(&sSHA384);

    // --- SHA512 ---
    sSHA512 = {
        .algorithm = B_CRYPTO_SHA512,
        .flags     = B_CRYPTO_ALG_SOFTWARE,
        .priority  = priority,
        .Process   = hybrid_sha_process,
        .HashInit  = hybrid_SHA512_init_bridge,
        .HashUpdate = hybrid_SHA512_update_bridge,
        .HashFinal = hybrid_SHA512_final_bridge
    };
    snprintf(sSHA512.name, sizeof(sSHA512.name), "SHA512 %s", suffix);
    BRegisterCryptoAlgorithm(&sSHA512);
    
    return B_OK;
#else
    return B_NOT_SUPPORTED;
#endif
}
