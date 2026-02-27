/*
 * SHA Hybrid Optimized Digest for Haiku kernel crypto
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include "soft_sha.h"         
#include "hybrid_sha_opt.h"   
#include "BCryptoAlgorithm.h"
#include "BCryptoCore.h"
#include <crypto/BCryptoDefs.h>
#include <arch/x86/arch_cpu.h>
#include <string.h>
#include <stdlib.h>

static status_t
hybrid_sha_process(BCryptoRequest* request)
{
	if (request == NULL || !request->source || !request->destination)
        return B_BAD_VALUE;

    size_t destLen = request->destination[0].iov_len;
    uint8 digest[64]; // Buffer per il risultato finale
    
    switch(request->algorithm) {
    	case B_CRYPTO_SHA1: {
    		break;
    	}
    	case B_CRYPTO_SHA224: {
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
    		break;
    	}
    	case B_CRYPTO_SHA3_512: {
    		break;
    	}
    	default:
            return B_NOT_SUPPORTED;
    }
    if (request->completionCallback)
        request->completionCallback(request, B_OK);

    return B_OK;
}


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
    *contextSize = sizeof(SoftSHA224Context);
    *context = malloc(*contextSize);
    if (*context == NULL) return B_NO_MEMORY;

    hybrid_SHA224_init((SoftSHA224Context*)*context, 32);
    return B_OK;
}

static status_t
hybrid_SHA224_update_bridge(void* context, const iovec* vecs, size_t count)
{
    if (context == NULL) return B_BAD_VALUE;
    SoftSHA224Context* ctx = (SoftSHA224Context*)context;
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
    SoftSHA224Context* ctx = (SoftSHA224Context*)context;
    hybrid_SHA224_finalize(ctx, outDigest);
    memset(ctx, 0, sizeof(SoftSHA224Context));
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
// --- BRIDGE PER SHA512 ---

/* ---------------------------------------------------------------- */
/* Registration                                                     */
/* ---------------------------------------------------------------- */
static BCryptoAlgorithm sSHA1SSE;
static BCryptoAlgorithm sSHA512;

status_t 
BInitHybridSHADigest() 
{
#if defined(__x86_64__) || defined(__i386__)
    //cpuid_info info;
    //get_cpuid(&info, 1, 0);
    //bool hasSSE41 = (info.eax_1.ecx & (1 << 19)) != 0;
    bool hasSSE41 = x86_check_feature(IA32_FEATURE_EXT_SSE4_1, FEATURE_EXT);

    //get_cpuid(&info, 7, 0);
    //bool hasAVX2 = (info.eax_7.ebx & (1 << 5)) != 0;
    bool hasAVX2 = x86_check_feature(IA32_FEATURE_AVX2, FEATURE_7_EBX);

    if (!hasSSE41) return B_NOT_SUPPORTED;
    
    hybrid_set_use_avx2(hasAVX2);

    // --- SHA1: SSE4.1 è sempre presente se siamo qui ---
    sSHA1SSE = {
        .algorithm = B_CRYPTO_SHA1,
        .flags     = B_CRYPTO_ALG_SOFTWARE,
        .name      = "SHA1 (Hybrid/SSE4.1)",
        .priority  = 25,
        .Process   = hybrid_sha_process,
        .HashInit  = hybrid_SHA1_init_bridge,
        .HashUpdate = hybrid_SHA1_update_bridge,
        .HashFinal = hybrid_SHA1_final_bridge
    };
    BRegisterCryptoAlgorithm(&sSHA1SSE);
    //

    // --- SHA512: Registrazione basata su capacità CPU ---
    const char* b2bName = hasAVX2 ? "SHA512 (Hybrid/AVX2)" : "SHA512 (Hybrid/SSE4.1)";
    sSHA512 = {
        .algorithm = B_CRYPTO_SHA512,
        .flags     = B_CRYPTO_ALG_SOFTWARE,
        //.name      = b2bName,
        .priority  = hasAVX2 ? 25 : 20,
        .Process   = hybrid_sha_process,
        .HashInit  = hybrid_SHA512_init_bridge,
        .HashUpdate = hybrid_SHA512_update_bridge,
        .HashFinal = hybrid_SHA512_final_bridge
    };
    strlcpy(sSHA512.name, b2bName, sizeof(sSHA512.name));
    BRegisterCryptoAlgorithm(&sSHA512);

    return B_OK;
#else
    return B_NOT_SUPPORTED;
#endif
}
