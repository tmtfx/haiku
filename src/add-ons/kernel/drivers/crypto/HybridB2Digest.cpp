/*
 * Blake2 Hybrid Optimized Digest for Haiku kernel crypto
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include "soft_blake.h"         
#include "hybrid_blake_opt.h"   
#include "BCryptoAlgorithm.h"
#include "BCryptoCore.h"
#include "BCryptoCPU.h"
#include <crypto/BCryptoDefs.h>
#include <arch/x86/arch_cpu.h>
#include <string.h>
#include <stdlib.h>

// Helper interno per il processo one-shot
static status_t
hybrid_blake2_process(BCryptoRequest* request)
{
    if (request == NULL || !request->source || !request->destination)
        return B_BAD_VALUE;

    size_t destLen = request->destination[0].iov_len;
    uint8 digest[64]; // Buffer per il risultato finale
    
    switch(request->algorithm) {
        case B_CRYPTO_BLAKE2B: {
            if (destLen < 64) return B_BAD_VALUE;
            SoftBlake2bContext* ctx = (SoftBlake2bContext*)malloc(sizeof(SoftBlake2bContext));
            if (ctx == NULL) return B_NO_MEMORY;

            hybrid_blake2b_init(ctx, 64);
            for (size_t i = 0; i < request->vectorCount; i++) {
                if (request->source[i].iov_base && request->source[i].iov_len > 0)
                    hybrid_blake2b_update(ctx, (const uint8*)request->source[i].iov_base, request->source[i].iov_len);
            }
            hybrid_blake2b_finalize(ctx, digest);
            memcpy(request->destination[0].iov_base, digest, 64);
            
            memset(ctx, 0, sizeof(SoftBlake2bContext));
            free(ctx);
            break;
        }

        case B_CRYPTO_BLAKE2S: {
            if (destLen < 32) return B_BAD_VALUE;
            SoftBlake2sContext* ctx = (SoftBlake2sContext*)malloc(sizeof(SoftBlake2sContext));
            if (ctx == NULL) return B_NO_MEMORY;

            hybrid_blake2s_init(ctx, 32);
            for (size_t i = 0; i < request->vectorCount; i++) {
                if (request->source[i].iov_base && request->source[i].iov_len > 0)
                    hybrid_blake2s_update(ctx, (const uint8*)request->source[i].iov_base, request->source[i].iov_len);
            }
            hybrid_blake2s_finalize(ctx, digest);
            memcpy(request->destination[0].iov_base, digest, 32);

            memset(ctx, 0, sizeof(SoftBlake2sContext));
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

/* ---------------------------------------------------------------- */
/* Bridges for Multi-part Hash (Init/Update/Final)                  */
/* ---------------------------------------------------------------- */

// --- BRIDGE PER BLAKE2B ---
static status_t
hybrid_blake2b_init_bridge(void** context, size_t* contextSize)
{
    if (context == NULL || contextSize == NULL) return B_BAD_VALUE;
    *contextSize = sizeof(SoftBlake2bContext);
    *context = malloc(*contextSize);
    if (*context == NULL) return B_NO_MEMORY;

    hybrid_blake2b_init((SoftBlake2bContext*)*context, 64);
    return B_OK;
}

static status_t
hybrid_blake2b_update_bridge(void* context, const iovec* vecs, size_t count)
{
    if (context == NULL) return B_BAD_VALUE;
    SoftBlake2bContext* ctx = (SoftBlake2bContext*)context;
    for (size_t i = 0; i < count; i++) {
        if (vecs[i].iov_base && vecs[i].iov_len > 0)
            hybrid_blake2b_update(ctx, (const uint8*)vecs[i].iov_base, vecs[i].iov_len);
    }
    return B_OK;
}

static status_t
hybrid_blake2b_final_bridge(void* context, uint8* outDigest)
{
    if (context == NULL || outDigest == NULL) return B_BAD_VALUE;
    SoftBlake2bContext* ctx = (SoftBlake2bContext*)context;
    hybrid_blake2b_finalize(ctx, outDigest);
    memset(ctx, 0, sizeof(SoftBlake2bContext));
    return B_OK;
}

// --- BRIDGE PER BLAKE2S ---
static status_t
hybrid_blake2s_init_bridge(void** context, size_t* contextSize)
{
    if (context == NULL || contextSize == NULL) return B_BAD_VALUE;
    *contextSize = sizeof(SoftBlake2sContext);
    *context = malloc(*contextSize);
    if (*context == NULL) return B_NO_MEMORY;

    hybrid_blake2s_init((SoftBlake2sContext*)*context, 32);
    return B_OK;
}

static status_t
hybrid_blake2s_update_bridge(void* context, const iovec* vecs, size_t count)
{
    if (context == NULL) return B_BAD_VALUE;
    SoftBlake2sContext* ctx = (SoftBlake2sContext*)context;
    for (size_t i = 0; i < count; i++) {
        if (vecs[i].iov_base && vecs[i].iov_len > 0)
            hybrid_blake2s_update(ctx, (const uint8*)vecs[i].iov_base, vecs[i].iov_len);
    }
    return B_OK;
}

static status_t
hybrid_blake2s_final_bridge(void* context, uint8* outDigest)
{
    if (context == NULL || outDigest == NULL) return B_BAD_VALUE;
    SoftBlake2sContext* ctx = (SoftBlake2sContext*)context;
    hybrid_blake2s_finalize(ctx, outDigest);
    memset(ctx, 0, sizeof(SoftBlake2sContext));
    return B_OK;
}

/* ---------------------------------------------------------------- */
/* Registration                                                     */
/* ---------------------------------------------------------------- */
static BCryptoAlgorithm sBlake2sSSE;
static BCryptoAlgorithm sBlake2b;

status_t 
BInitHybridB2Digest() 
{
#if defined(__x86_64__) || defined(__i386__)
    /*
    //cpuid_info info;
    //get_cpuid(&info, 1, 0);
    //bool hasSSE41 = (info.eax_1.ecx & (1 << 19)) != 0;
    bool hasSSE41 = x86_check_feature(IA32_FEATURE_EXT_SSE4_1, FEATURE_EXT);

    //get_cpuid(&info, 7, 0);
    //bool hasAVX2 = (info.eax_7.ebx & (1 << 5)) != 0;
    bool hasAVX2 = x86_check_feature(IA32_FEATURE_AVX2, FEATURE_7_EBX);

    if (!hasSSE41) return B_NOT_SUPPORTED;
    
    hybrid_set_use_avx2(hasAVX2);*/
    if (!gHasSSE41) return B_NOT_SUPPORTED;

    // --- BLAKE2s: SSE4.1 è sempre presente se siamo qui ---
    sBlake2sSSE = {
        .algorithm = B_CRYPTO_BLAKE2S,
        .flags     = B_CRYPTO_ALG_SOFTWARE,
        .name      = "BLAKE2s (Hybrid/SSE4.1)",
        .priority  = 25,
        .Process   = hybrid_blake2_process,
        .HashInit  = hybrid_blake2s_init_bridge,
        .HashUpdate = hybrid_blake2s_update_bridge,
        .HashFinal = hybrid_blake2s_final_bridge
    };
    BRegisterCryptoAlgorithm(&sBlake2sSSE);

    // --- BLAKE2b: Registrazione basata su capacità CPU ---
    const char* b2bName = gHasAVX2 ? "BLAKE2b (Hybrid/AVX2)" : "BLAKE2b (Hybrid/SSE4.1)";
    sBlake2b = {
        .algorithm = B_CRYPTO_BLAKE2B,
        .flags     = B_CRYPTO_ALG_SOFTWARE,
        //.name      = b2bName,
        .priority  = gHasAVX2 ? 25 : 20,
        .Process   = hybrid_blake2_process,
        .HashInit  = hybrid_blake2b_init_bridge,
        .HashUpdate = hybrid_blake2b_update_bridge,
        .HashFinal = hybrid_blake2b_final_bridge
    };
    strlcpy(sBlake2b.name, b2bName, sizeof(sBlake2b.name));
    BRegisterCryptoAlgorithm(&sBlake2b);

    return B_OK;
#else
    return B_NOT_SUPPORTED;
#endif
}
