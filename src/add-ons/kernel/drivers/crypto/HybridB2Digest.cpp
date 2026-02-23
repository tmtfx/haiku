/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * Hybrid Optimized Digest for Haiku kernel
 */

#include "soft_blake.h"         // Manterremo i contesti esistenti
#include "hybrid_blake_opt.h"   // Qui dichiareremo le versioni SSE/AVX
#include "BCryptoAlgorithm.h"
#include <crypto/BCryptoDefs.h>
#include <arch/x86/arch_cpu.h>

// I bridge richiameranno le versioni ottimizzate invece di quelle "soft" standard
static status_t
hybrid_blake2_process(BCryptoRequest* request)
{
	if (request == NULL)
        return B_BAD_VALUE;
	switch(request->algorithm){
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
            if (request->destination[0].iov_base != NULL) {
                memcpy(request->destination[0].iov_base, digest, 64);
            } else {
                free(ctx); // Non dimenticare di liberare se fallisci qui!
                return B_BAD_VALUE;
            }
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
            if (request->destination[0].iov_base != NULL) {
                memcpy(request->destination[0].iov_base, digest, 32);
            } else {
                free(ctx);
                return B_BAD_VALUE;
            }
            memset(ctx, 0, sizeof(SoftBlake2sContext));
            free(ctx);
            break;
        }
        default:
            return B_BAD_VALUE;
	}
}

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
    
    memset(ctx, 0, sizeof(SoftBlake2bContext)); // Cancella tracce sensibili
    // La free(context) verrà eseguita dal chiamante (BCryptoCore)
    return B_OK;
}

// --- BRIDGE PER BLAKE2S (32-bit) ---
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
    
    memset(ctx, 0, sizeof(SoftBlake2sContext)); // Cancella tracce sensibili
    // La free(context) verrà eseguita dal chiamante (BCryptoCore)
    return B_OK;
}


status_t 
BInitHybridDigest() 
{
#if defined(__SSE4_1__) || defined(__AVX2__)
	cpuid_info info;
    get_cpuid(&info, 1, 0);
    bool hasSSE41 = (info.eax_1.ecx & (1 << 19)) != 0;
    if (!hasSSE41) {
        return B_NOT_SUPPORTED; 
    }
    static BCryptoAlgorithm sHybridAlgos[] = {
        {
            .algorithm = B_CRYPTO_BLAKE2S,
            .flags     = B_CRYPTO_ALG_SOFTWARE, // Resta software, ma ottimizzato
            .name      = "BLAKE2s (Hybrid/SIMD)",
            .priority  = 20, 
            .Process   = hybrid_blake2_process,
            .HashInit   = hybrid_blake2s_init_bridge,
            .HashUpdate = hybrid_blake2s_update_bridge,
            .HashFinal  = hybrid_blake2s_final_bridge
        },
        {
            .algorithm = B_CRYPTO_BLAKE2B,
            .flags     = B_CRYPTO_ALG_SOFTWARE,
            .name      = "BLAKE2b (Hybrid/SIMD)",
            .priority  = 20,
            .Process   = hybrid_blake2_process,
            .HashInit   = hybrid_blake2b_init_bridge,
            .HashUpdate = hybrid_blake2b_update_bridge,
            .HashFinal  = hybrid_blake2b_final_bridge
        }
    };
    status_t status = B_OK;
    for (size_t i = 0; i < sizeof(sHybridAlgos)/sizeof(sHybridAlgos[0]); i++) {
        status = BRegisterCryptoAlgorithm(&sHybridAlgos[i]);
        if (status != B_OK) break;
    }
    return status;
#else
    return B_NOT_SUPPORTED;
#endif
}
