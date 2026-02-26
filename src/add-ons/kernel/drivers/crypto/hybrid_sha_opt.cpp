/*
 * Hybrid SIMD/AVX implementation for SHA
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * Optimized for Haiku Kernel. MIT License.
 */
#include "soft_blake.h"
#include "hybrid_blake_opt.h"
#include <smmintrin.h> // SSE4.1
#include <immintrin.h> // AVX2
#include <string.h>
#include <ByteOrder.h>
#include <debug.h>

static bool sUseAVX2 = false;

void hybrid_set_use_avx2(bool enable) {
    sUseAVX2 = enable;
}

#include <tmmintrin.h> // SSSE3 per _mm_shuffle_epi8
#include <smmintrin.h> // SSE4.1

// Helper per emulare le rotazioni a destra di SHA-256 in SSE
#define ROR32(x, n) _mm_or_si128(_mm_srli_epi32(x, n), _mm_slli_epi32(x, 32 - n))

// s0(x) = (ROTR(x, 7) ^ ROTR(x, 18) ^ (x >> 3))
#define SCHED_s0(x) _mm_xor_si128(_mm_xor_si128(ROR32(x, 7), ROR32(x, 18)), _mm_srli_epi32(x, 3))

// s1(x) = (ROTR(x, 17) ^ ROTR(x, 19) ^ (x >> 10))
#define SCHED_s1(x) _mm_xor_si128(_mm_xor_si128(ROR32(x, 17), ROR32(x, 19)), _mm_srli_epi32(x, 10))

__attribute__((target("sse4.1")))
void hybrid_sha256_transform_sse(SoftSHA256Context* ctx, const uint8* data) {
    uint32 W[64] __attribute__((aligned(16)));
    uint32 a, b, c, d, e, f, g, h;

    // 1. Caricamento e Byte-Swap (Big Endian -> Little Endian CPU)
    // Usiamo una maschera fissa per invertire l'ordine dei byte in ogni uint32
    __m128i mask = _mm_set_epi8(12,13,14,15, 8,9,10,11, 4,5,6,7, 0,1,2,3);
    for (int i = 0; i < 4; i++) {
        __m128i raw = _mm_loadu_si128((__m128i*)(data + i * 16));
        _mm_store_si128((__m128i*)&W[i * 4], _mm_shuffle_epi8(raw, mask));
    }

    // 2. Espansione Messaggio (Scheduling)
    // Calcoliamo 4 parole W alla volta parallelamente
    for (int i = 16; i < 64; i += 4) {
        __m128i w_15 = _mm_loadu_si128((__m128i*)&W[i - 15]);
        __m128i w_2  = _mm_loadu_si128((__m128i*)&W[i - 2]);
        __m128i w_16 = _mm_load_si128((__m128i*)&W[i - 16]);
        __m128i w_7  = _mm_loadu_si128((__m128i*)&W[i - 7]);

        __m128i s0_v = SCHED_s0(w_15);
        __m128i s1_v = SCHED_s1(w_2);

        __m128i res = _mm_add_epi32(_mm_add_epi32(w_16, w_7), _mm_add_epi32(s0_v, s1_v));
        _mm_store_si128((__m128i*)&W[i], res);
    }

    // 3. Round di compressione (Seriali, usano le tue macro standard)
    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];

    for (int i = 0; i < 64; i++) {
        uint32 t1 = h + S1(e) + Ch(e, f, g) + K[i] + W[i];
        uint32 t2 = S0(a) + Maj(a, b, c);
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }

    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}
/*
// --- SHA-256 Ottimizzato SSE4.1 ---
__attribute__((target("sse4.1")))
void hybrid_sha256_transform_sse(SoftSHA256Context* ctx, const uint8* data) {
    uint32 W[64] __attribute__((aligned(16)));
    
    // Byte swap veloce via SSSE3
    __m128i mask = _mm_set_epi8(12,13,14,15, 8,9,10,11, 4,5,6,7, 0,1,2,3);
    for (int i = 0; i < 4; i++) {
        __m128i w = _mm_loadu_si128((__m128i*)(data + i * 16));
        _mm_store_si128((__m128i*)&W[i * 4], _mm_shuffle_epi8(w, mask));
    }

    // Espansione W (Hybrid SSE)
    // s0(x) = (ROTR(x, 7) ^ ROTR(x, 18) ^ (x >> 3))
    // s1(x) = (ROTR(x, 17) ^ ROTR(x, 19) ^ (x >> 10))
    for (int i = 16; i < 64; i += 4) {
        __m128i w_15 = _mm_loadu_si128((__m128i*)&W[i-15]);
        __m128i w_2  = _mm_loadu_si128((__m128i*)&W[i-2]);
        
        // Esecuzione parallela delle funzioni sigma
        // (Semplificato per brevità, usa le macro definite precedentemente)
        // ... logica s0 e s1 ...
        
        __m128i w_16 = _mm_load_si128((__m128i*)&W[i-16]);
        __m128i w_7  = _mm_loadu_si128((__m128i*)&W[i-7]);
        _mm_store_si128((__m128i*)&W[i], _mm_add_epi32(_mm_add_epi32(w_16, w_7), /* sigma res */));
    }

    // Round di compressione standard (C puro per ora)
    // ... stessa logica di soft_sha.cpp ...
}*/
