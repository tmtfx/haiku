/*
 * Intel/AMD Hardware SHA-NI (Accelerated Digest)
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include "x86CPUDigest.h"
#include "BCryptoCPU.h"
#include "BCryptoCore.h"
#include <crypto/BCryptoDefs.h>
#include "BCryptoAlgorithm.h"
#include <string.h>
#include <immintrin.h>
#include <arch/x86/arch_cpu.h> //
#include <arch/x86/arch_cpuasm.h>
#include <debug.h>
#include <malloc.h>

#if defined(__x86_64__) || defined(__i386__)

#pragma GCC target("sha,sse4.1,ssse3")

static const __m128i MASK_ENDIAN = _mm_set_epi8(
    12, 13, 14, 15, 
    8, 9, 10, 11, 
    4, 5, 6, 7, 
    0, 1, 2, 3
);

static const uint32 K256[64] alignas(16) = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};
/*
#define SHA256_ROUND(msg0, msg1, msg2, msg3, st0, st1, k_idx) \
    { \
        __m128i k_vec = _mm_loadu_si128((const __m128i*)&K256[k_idx]); \
        st1 = _mm_sha256rnds2_epu32(st1, st0, _mm_add_epi32(msg0, k_vec)); \
        st0 = _mm_sha256rnds2_epu32(st0, st1, _mm_add_epi32(_mm_alignr_epi8(msg0, msg0, 8), k_vec)); \
        msg0 = _mm_sha256msg1_epu32(msg0, msg1); \
        msg0 = _mm_add_epi32(msg0, _mm_sha256msg2_epu32(_mm_alignr_epi8(msg3, msg2, 4), msg3)); \
    }

static void
sha256_transform_block(__m128i& st0, __m128i& st1, const uint8* data)
{
    __m128i msg0, msg1, msg2, msg3;
    __m128i old_st0 = st0;
    __m128i old_st1 = st1;

    msg0 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(data + 0)),  MASK_ENDIAN);
    msg1 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(data + 16)), MASK_ENDIAN);
    msg2 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(data + 32)), MASK_ENDIAN);
    msg3 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(data + 48)), MASK_ENDIAN);

    SHA256_ROUND(msg0, msg1, msg2, msg3, st0, st1, 0);
    SHA256_ROUND(msg1, msg2, msg3, msg0, st0, st1, 4);
    SHA256_ROUND(msg2, msg3, msg0, msg1, st0, st1, 8);
    SHA256_ROUND(msg3, msg0, msg1, msg2, st0, st1, 12);
    SHA256_ROUND(msg0, msg1, msg2, msg3, st0, st1, 16);
    SHA256_ROUND(msg1, msg2, msg3, msg0, st0, st1, 20);
    SHA256_ROUND(msg2, msg3, msg0, msg1, st0, st1, 24);
    SHA256_ROUND(msg3, msg0, msg1, msg2, st0, st1, 28);
    SHA256_ROUND(msg0, msg1, msg2, msg3, st0, st1, 32);
    SHA256_ROUND(msg1, msg2, msg3, msg0, st0, st1, 36);
    SHA256_ROUND(msg2, msg3, msg0, msg1, st0, st1, 40);
    SHA256_ROUND(msg3, msg0, msg1, msg2, st0, st1, 44);
    SHA256_ROUND(msg0, msg1, msg2, msg3, st0, st1, 48);
    SHA256_ROUND(msg1, msg2, msg3, msg0, st0, st1, 52);
    SHA256_ROUND(msg2, msg3, msg0, msg1, st0, st1, 56);
    SHA256_ROUND(msg3, msg0, msg1, msg2, st0, st1, 60);

    st0 = _mm_add_epi32(st0, old_st0);
    st1 = _mm_add_epi32(st1, old_st1);
}
static void
sha256_transform_64bytes(__m128i& st0, __m128i& st1, const uint8* data)
{
    __m128i msg0, msg1, msg2, msg3, tmp;
    __m128i old_st0 = st0;
    __m128i old_st1 = st1;

    // 1. Caricamento e inversione endianness
    msg0 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(data + 0)),  MASK_ENDIAN);
    msg1 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(data + 16)), MASK_ENDIAN);
    msg2 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(data + 32)), MASK_ENDIAN);
    msg3 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(data + 48)), MASK_ENDIAN);

    // Round 0-3
    tmp = _mm_add_epi32(msg0, _mm_loadu_si128((const __m128i*)&K256[0]));
    st1 = _mm_sha256rnds2_epu32(st1, st0, tmp);
    st0 = _mm_sha256rnds2_epu32(st0, st1, _mm_alignr_epi8(tmp, tmp, 8));
    msg0 = _mm_sha256msg1_epu32(msg0, msg1);

    // Round 4-7
    tmp = _mm_add_epi32(msg1, _mm_loadu_si128((const __m128i*)&K256[4]));
    st1 = _mm_sha256rnds2_epu32(st1, st0, tmp);
    st0 = _mm_sha256rnds2_epu32(st0, st1, _mm_alignr_epi8(tmp, tmp, 8));
    msg1 = _mm_sha256msg1_epu32(msg1, msg2);
    msg0 = _mm_add_epi32(msg0, _mm_sha256msg2_epu32(_mm_alignr_epi8(msg3, msg2, 4), msg3));

    // Round 8-11
    tmp = _mm_add_epi32(msg2, _mm_loadu_si128((const __m128i*)&K256[8]));
    st1 = _mm_sha256rnds2_epu32(st1, st0, tmp);
    st0 = _mm_sha256rnds2_epu32(st0, st1, _mm_alignr_epi8(tmp, tmp, 8));
    msg2 = _mm_sha256msg1_epu32(msg2, msg3);
    msg1 = _mm_add_epi32(msg1, _mm_sha256msg2_epu32(_mm_alignr_epi8(msg0, msg3, 4), msg0));

    // Round 12-15
    tmp = _mm_add_epi32(msg3, _mm_loadu_si128((const __m128i*)&K256[12]));
    st1 = _mm_sha256rnds2_epu32(st1, st0, tmp);
    st0 = _mm_sha256rnds2_epu32(st0, st1, _mm_alignr_epi8(tmp, tmp, 8));
    msg3 = _mm_sha256msg1_epu32(msg3, msg0);
    msg2 = _mm_add_epi32(msg2, _mm_sha256msg2_epu32(_mm_alignr_epi8(msg1, msg0, 4), msg1));

    // Round 16-19
    tmp = _mm_add_epi32(msg0, _mm_loadu_si128((const __m128i*)&K256[16]));
    st1 = _mm_sha256rnds2_epu32(st1, st0, tmp);
    st0 = _mm_sha256rnds2_epu32(st0, st1, _mm_alignr_epi8(tmp, tmp, 8));
    msg0 = _mm_sha256msg1_epu32(msg0, msg1);
    msg3 = _mm_add_epi32(msg3, _mm_sha256msg2_epu32(_mm_alignr_epi8(msg2, msg1, 4), msg2));

    // Round 20-23
    tmp = _mm_add_epi32(msg1, _mm_loadu_si128((const __m128i*)&K256[20]));
    st1 = _mm_sha256rnds2_epu32(st1, st0, tmp);
    st0 = _mm_sha256rnds2_epu32(st0, st1, _mm_alignr_epi8(tmp, tmp, 8));
    msg1 = _mm_sha256msg1_epu32(msg1, msg2);
    msg0 = _mm_add_epi32(msg0, _mm_sha256msg2_epu32(_mm_alignr_epi8(msg3, msg2, 4), msg3));

    // Round 24-27
    tmp = _mm_add_epi32(msg2, _mm_loadu_si128((const __m128i*)&K256[24]));
    st1 = _mm_sha256rnds2_epu32(st1, st0, tmp);
    st0 = _mm_sha256rnds2_epu32(st0, st1, _mm_alignr_epi8(tmp, tmp, 8));
    msg2 = _mm_sha256msg1_epu32(msg2, msg3);
    msg1 = _mm_add_epi32(msg1, _mm_sha256msg2_epu32(_mm_alignr_epi8(msg0, msg3, 4), msg0));

    // Round 28-31
    tmp = _mm_add_epi32(msg3, _mm_loadu_si128((const __m128i*)&K256[28]));
    st1 = _mm_sha256rnds2_epu32(st1, st0, tmp);
    st0 = _mm_sha256rnds2_epu32(st0, st1, _mm_alignr_epi8(tmp, tmp, 8));
    msg3 = _mm_sha256msg1_epu32(msg3, msg0);
    msg2 = _mm_add_epi32(msg2, _mm_sha256msg2_epu32(_mm_alignr_epi8(msg1, msg0, 4), msg1));

    // Round 32-35
    tmp = _mm_add_epi32(msg0, _mm_loadu_si128((const __m128i*)&K256[32]));
    st1 = _mm_sha256rnds2_epu32(st1, st0, tmp);
    st0 = _mm_sha256rnds2_epu32(st0, st1, _mm_alignr_epi8(tmp, tmp, 8));
    msg0 = _mm_sha256msg1_epu32(msg0, msg1);
    msg3 = _mm_add_epi32(msg3, _mm_sha256msg2_epu32(_mm_alignr_epi8(msg2, msg1, 4), msg2));

    // Round 36-39
    tmp = _mm_add_epi32(msg1, _mm_loadu_si128((const __m128i*)&K256[36]));
    st1 = _mm_sha256rnds2_epu32(st1, st0, tmp);
    st0 = _mm_sha256rnds2_epu32(st0, st1, _mm_alignr_epi8(tmp, tmp, 8));
    msg1 = _mm_sha256msg1_epu32(msg1, msg2);
    msg0 = _mm_add_epi32(msg0, _mm_sha256msg2_epu32(_mm_alignr_epi8(msg3, msg2, 4), msg3));

    // Round 40-43
    tmp = _mm_add_epi32(msg2, _mm_loadu_si128((const __m128i*)&K256[40]));
    st1 = _mm_sha256rnds2_epu32(st1, st0, tmp);
    st0 = _mm_sha256rnds2_epu32(st0, st1, _mm_alignr_epi8(tmp, tmp, 8));
    msg2 = _mm_sha256msg1_epu32(msg2, msg3);
    msg1 = _mm_add_epi32(msg1, _mm_sha256msg2_epu32(_mm_alignr_epi8(msg0, msg3, 4), msg0));

    // Round 44-47
    tmp = _mm_add_epi32(msg3, _mm_loadu_si128((const __m128i*)&K256[44]));
    st1 = _mm_sha256rnds2_epu32(st1, st0, tmp);
    st0 = _mm_sha256rnds2_epu32(st0, st1, _mm_alignr_epi8(tmp, tmp, 8));
    msg3 = _mm_sha256msg1_epu32(msg3, msg0);
    msg2 = _mm_add_epi32(msg2, _mm_sha256msg2_epu32(_mm_alignr_epi8(msg1, msg0, 4), msg1));

    // Round 48-51
    tmp = _mm_add_epi32(msg0, _mm_loadu_si128((const __m128i*)&K256[48]));
    st1 = _mm_sha256rnds2_epu32(st1, st0, tmp);
    st0 = _mm_sha256rnds2_epu32(st0, st1, _mm_alignr_epi8(tmp, tmp, 8));
    msg0 = _mm_sha256msg1_epu32(msg0, msg1);
    msg3 = _mm_add_epi32(msg3, _mm_sha256msg2_epu32(_mm_alignr_epi8(msg2, msg1, 4), msg2));

    // Round 52-55
    tmp = _mm_add_epi32(msg1, _mm_loadu_si128((const __m128i*)&K256[52]));
    st1 = _mm_sha256rnds2_epu32(st1, st0, tmp);
    st0 = _mm_sha256rnds2_epu32(st0, st1, _mm_alignr_epi8(tmp, tmp, 8));
    msg1 = _mm_sha256msg1_epu32(msg1, msg2);
    msg0 = _mm_add_epi32(msg0, _mm_sha256msg2_epu32(_mm_alignr_epi8(msg3, msg2, 4), msg3));

    // Round 56-59
    tmp = _mm_add_epi32(msg2, _mm_loadu_si128((const __m128i*)&K256[56]));
    st1 = _mm_sha256rnds2_epu32(st1, st0, tmp);
    st0 = _mm_sha256rnds2_epu32(st0, st1, _mm_alignr_epi8(tmp, tmp, 8));
    msg2 = _mm_sha256msg1_epu32(msg2, msg3);
    msg1 = _mm_add_epi32(msg1, _mm_sha256msg2_epu32(_mm_alignr_epi8(msg0, msg3, 4), msg0));

    // Round 60-63
    tmp = _mm_add_epi32(msg3, _mm_loadu_si128((const __m128i*)&K256[60]));
    st1 = _mm_sha256rnds2_epu32(st1, st0, tmp);
    st0 = _mm_sha256rnds2_epu32(st0, st1, _mm_alignr_epi8(tmp, tmp, 8));
    msg3 = _mm_sha256msg1_epu32(msg3, msg0);
    msg2 = _mm_add_epi32(msg2, _mm_sha256msg2_epu32(_mm_alignr_epi8(msg1, msg0, 4), msg1));

    // SOMMA FINALE (Fondamentale!)
    st0 = _mm_add_epi32(st0, old_st0);
    st1 = _mm_add_epi32(st1, old_st1);
}

static void
extract_digest(__m128i st0, __m128i st1, uint8* out)
{
    uint32_t* out32 = (uint32_t*)out;
    out32[0] = __builtin_bswap32(_mm_extract_epi32(st0, 3)); // A
    out32[1] = __builtin_bswap32(_mm_extract_epi32(st0, 2)); // B
    out32[2] = __builtin_bswap32(_mm_extract_epi32(st0, 1)); // C
    out32[3] = __builtin_bswap32(_mm_extract_epi32(st0, 0)); // D
    out32[4] = __builtin_bswap32(_mm_extract_epi32(st1, 3)); // E
    out32[5] = __builtin_bswap32(_mm_extract_epi32(st1, 2)); // F
    out32[6] = __builtin_bswap32(_mm_extract_epi32(st1, 1)); // G
    out32[7] = __builtin_bswap32(_mm_extract_epi32(st1, 0)); // H
}
*/
// Funzione di trasformazione UNICA e OTTIMIZZATA
static void
_sha256_transform_core(__m128i& st0, __m128i& st1, const uint8* data)
{
    __m128i msg0, msg1, msg2, msg3, tmp;
    __m128i old_st0 = st0;
    __m128i old_st1 = st1;

    msg0 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(data + 0)),  MASK_ENDIAN);
    msg1 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(data + 16)), MASK_ENDIAN);
    msg2 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(data + 32)), MASK_ENDIAN);
    msg3 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(data + 48)), MASK_ENDIAN);

    // I round sono raggruppati a 4 a 4 per massimizzare il throughput della pipeline
    #define ROUNDS_4(m0, m1, m2, m3, k_idx) \
        tmp = _mm_add_epi32(m0, _mm_loadu_si128((const __m128i*)&K256[k_idx])); \
        st1 = _mm_sha256rnds2_epu32(st1, st0, tmp); \
        st0 = _mm_sha256rnds2_epu32(st0, st1, _mm_alignr_epi8(tmp, tmp, 8)); \
        m0  = _mm_sha256msg1_epu32(m0, m1); \
        m0  = _mm_add_epi32(m0, _mm_sha256msg2_epu32(_mm_alignr_epi8(m3, m2, 4), m3));

    ROUNDS_4(msg0, msg1, msg2, msg3, 0);
    ROUNDS_4(msg1, msg2, msg3, msg0, 4);
    ROUNDS_4(msg2, msg3, msg0, msg1, 8);
    ROUNDS_4(msg3, msg0, msg1, msg2, 12);
    ROUNDS_4(msg0, msg1, msg2, msg3, 16);
    ROUNDS_4(msg1, msg2, msg3, msg0, 20);
    ROUNDS_4(msg2, msg3, msg0, msg1, 24);
    ROUNDS_4(msg3, msg0, msg1, msg2, 28);
    ROUNDS_4(msg0, msg1, msg2, msg3, 32);
    ROUNDS_4(msg1, msg2, msg3, msg0, 36);
    ROUNDS_4(msg2, msg3, msg0, msg1, 40);
    ROUNDS_4(msg3, msg0, msg1, msg2, 44);
    ROUNDS_4(msg0, msg1, msg2, msg3, 48);
    ROUNDS_4(msg1, msg2, msg3, msg0, 52);
    ROUNDS_4(msg2, msg3, msg0, msg1, 56);
    ROUNDS_4(msg3, msg0, msg1, msg2, 60);

    st0 = _mm_add_epi32(st0, old_st0);
    st1 = _mm_add_epi32(st1, old_st1);
}

static void
extract_digest(__m128i st0, __m128i st1, uint8* out)
{
    uint32* out32 = (uint32*)out;
    // Ordine corretto per extract_epi32 coerente con set_epi32(A,B,C,D)
    out32[0] = __builtin_bswap32(_mm_extract_epi32(st0, 3)); // A
    out32[1] = __builtin_bswap32(_mm_extract_epi32(st0, 2)); // B
    out32[2] = __builtin_bswap32(_mm_extract_epi32(st0, 1)); // C
    out32[3] = __builtin_bswap32(_mm_extract_epi32(st0, 0)); // D
    out32[4] = __builtin_bswap32(_mm_extract_epi32(st1, 3)); // E
    out32[5] = __builtin_bswap32(_mm_extract_epi32(st1, 2)); // F
    out32[6] = __builtin_bswap32(_mm_extract_epi32(st1, 1)); // G
    out32[7] = __builtin_bswap32(_mm_extract_epi32(st1, 0)); // H
}

// --- LOGICA DI AGGIORNAMENTO DATI (FIX RESIDUI) ---
static void
_sha256_update_logic(x86_sha256_context* ctx, const uint8* data, size_t len)
{
    ctx->total_len += len;
    
    // Se c'è roba nel buffer, prova a completare il blocco
    if (ctx->buffer_len > 0) {
        size_t fill = 64 - ctx->buffer_len;
        if (len >= fill) {
            memcpy(ctx->buffer + ctx->buffer_len, data, fill);
            _sha256_transform_core(ctx->state0, ctx->state1, ctx->buffer);
            data += fill;
            len -= fill;
            ctx->buffer_len = 0;
        }
    }

    // Processa i blocchi pieni direttamente dalla sorgente
    while (len >= 64) {
        _sha256_transform_core(ctx->state0, ctx->state1, data);
        data += 64;
        len -= 64;
    }

    // Metti l'avanzo nel buffer (i tuoi famosi 6 byte del commento)
    if (len > 0) {
        memcpy(ctx->buffer + ctx->buffer_len, data, len);
        ctx->buffer_len += len;
    }
}
// --- ONE-SHOT PROCESS ---
status_t
x86_sha256_process(BCryptoRequest* request)
{
    if (request == NULL || request->algorithm != B_CRYPTO_SHA256) return B_BAD_VALUE;
    if (request->vectorCount == 0 || request->source == NULL) return B_OK;
    if (request->destination == NULL || request->destination[0].iov_base == NULL) return B_BAD_VALUE;
    
    x86_sha256_context* ctx = (x86_sha256_context*)memalign(64, sizeof(x86_sha256_context));
    if (ctx == NULL) return B_NO_MEMORY;
    memset(ctx, 0, sizeof(x86_sha256_context));
    
    B_PREPARE_CPU_STATE();
    
    ctx->state0 = _mm_set_epi32(0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a);
    ctx->state1 = _mm_set_epi32(0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19);
    ctx->total_len = 0;
    ctx->buffer_len = 0;
    /*
    uint64 totalBytes = 0;
    // Calcoliamo prima la lunghezza totale per il padding finale
    for (size_t i = 0; i < request->vectorCount; i++)
        totalBytes += request->source[i].iov_len;

    for (size_t i = 0; i < request->vectorCount; i++) {
        const uint8* data = (const uint8*)request->source[i].iov_base;
        size_t len = request->source[i].iov_len;
        
        if (data == NULL || len == 0) continue;

        // Processa i blocchi pieni
        while (len >= 64) {
            sha256_transform_64bytes(ctx->state0, ctx->state1, data);
            data += 64; 
            len -= 64;
        }

        // Gestione dell'ultimo chunk (Padding)
        if (i == request->vectorCount - 1) {
            alignas(16) uint8 pad[128];
            memset(pad, 0, 128);
            if (len > 0) memcpy(pad, data, len); // Copia il residuo (< 64 bytes)
            
            pad[len] = 0x80; // Bit di stop
            
            // Se non c'è spazio per i 64 bit di lunghezza, usiamo due blocchi
            size_t padLen = (len < 56) ? 64 : 128;
            uint64_t bitLen = __builtin_bswap64(totalBytes * 8);
            memcpy(pad + padLen - 8, &bitLen, 8);
            
            // USA SEMPRE LA STESSA FUNZIONE!
            sha256_transform_64bytes(ctx->state0, ctx->state1, pad);
            if (padLen == 128) 
                sha256_transform_64bytes(ctx->state0, ctx->state1, pad + 64);
        }
    }*/
    for (size_t i = 0; i < request->vectorCount; i++) {
        _sha256_update_logic(ctx, (uint8*)request->source[i].iov_base, request->source[i].iov_len);
    }
    alignas(16) uint8 pad[128];
    memcpy(pad, ctx->buffer, ctx->buffer_len);
    pad[ctx->buffer_len] = 0x80;
    size_t padLen = (ctx->buffer_len < 56) ? 64 : 128;
    memset(pad + ctx->buffer_len + 1, 0, padLen - ctx->buffer_len - 1);
    
    uint64 bits = __builtin_bswap64(ctx->total_len * 8);
    memcpy(pad + padLen - 8, &bits, 8);
    
    _sha256_transform_core(ctx->state0, ctx->state1, pad);
    if (padLen == 128) _sha256_transform_core(ctx->state0, ctx->state1, pad + 64);

    extract_digest(ctx->state0, ctx->state1, (uint8*)request->destination[0].iov_base);
    
    B_RESTORE_CPU_STATE()
    free(ctx);

    return B_OK;
}
// --- BRIDGE FUNCTIONS (STREAMING) ---

status_t x86_sha256_init_bridge(void** ctx, size_t* size) {
    if (ctx == NULL || size == NULL) return B_BAD_VALUE;
    *size = sizeof(x86_sha256_context);
    *ctx = malloc(*size);
    if (!*ctx) return B_NO_MEMORY;
    
    x86_sha256_context* s = (x86_sha256_context*)*ctx;
    memset(s, 0, sizeof(x86_sha256_context));
    s->state0 = _mm_set_epi32(0xa54ff53a, 0x3c6ef372, 0xbb67ae85, 0x6a09e667);
    s->state1 = _mm_set_epi32(0x5be0cd19, 0x1f83d9ab, 0x9b05688c, 0x510e527f);
    return B_OK;
}

status_t x86_sha256_update_bridge(void* ctx_void, const iovec* vecs, size_t count) {
    x86_sha256_context* ctx = (x86_sha256_context*)ctx_void;
    if (ctx == NULL) return B_BAD_VALUE;

    B_PREPARE_CPU_STATE(); // SALVA REGISTRI PER UPDATE
/*
    for (size_t i = 0; i < count; i++) {
        const uint8* data = (const uint8*)vecs[i].iov_base;
        size_t len = vecs[i].iov_len;
        if (data == NULL || len == 0) continue;
        s->total_len += len;

        while (len > 0) {
            size_t copy = (64 - s->buffer_len < len) ? 64 - s->buffer_len : len;
            memcpy(s->buffer + s->buffer_len, data, copy);
            s->buffer_len += copy; data += copy; len -= copy;
            if (s->buffer_len == 64) {
                sha256_transform_block(s->state0, s->state1, s->buffer);
                s->buffer_len = 0;
            }
        }
    }*/
    for (size_t i = 0; i < count; i++) {
        _sha256_update_logic(ctx, (const uint8*)vecs[i].iov_base, vecs[i].iov_len);
    }

    B_RESTORE_CPU_STATE(); // RIPRISTINA REGISTRI
    return B_OK;
}

status_t x86_sha256_final_bridge(void* ctx_void, uint8* out) {
    x86_sha256_context* ctx = (x86_sha256_context*)ctx_void;
    if (ctx == NULL || out == NULL) return B_BAD_VALUE;

    B_PREPARE_CPU_STATE(); // SALVA REGISTRI PER FINAL

    alignas(16) uint8 pad[128];
    memcpy(pad, ctx->buffer, ctx->buffer_len);
    pad[ctx->buffer_len] = 0x80;
    size_t padLen = (ctx->buffer_len < 56) ? 64 : 128;
    memset(pad + ctx->buffer_len + 1, 0, padLen - ctx->buffer_len - 1);
    
    uint64_t bits = __builtin_bswap64(ctx->total_len * 8);
    memcpy(pad + padLen - 8, &bits, 8);
    
    _sha256_transform_core(ctx->state0, ctx->state1, pad);
    if (padLen == 128) _sha256_transform_core(ctx->state0, ctx->state1, pad + 64);
    
    extract_digest(ctx->state0, ctx->state1, out);

    B_RESTORE_CPU_STATE(); // RIPRISTINA REGISTRI
    
    free(ctx);
    return B_OK;
}

status_t BInitx86CPUDigest() {
    if (!(BGetStoredCryptoCapabilities() & B_CRYPTO_HW_SHA_NI))
        return B_UNSUPPORTED;

    static BCryptoAlgorithm sSHA256_HW = {
        .algorithm = B_CRYPTO_SHA256,
        .mode      = B_CRYPTO_MODE_ANY,
        .flags     = B_CRYPTO_ALG_HW_ACCEL,
        .name      = "SHA256 (Hardware)",
        .priority  = 95, 
        .Process   = x86_sha256_process,
        .HashInit   = x86_sha256_init_bridge,
        .HashUpdate = x86_sha256_update_bridge,
        .HashFinal  = x86_sha256_final_bridge
    };

    return BRegisterCryptoAlgorithm(&sSHA256_HW);
}
#else
status_t BInitx86CPUDigest() { return B_NOT_SUPPORTED; }
#endif

