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
#include <immintrin.h> // Per gli intrinsechi SHA-NI

#if defined(__x86_64__) || defined(__i386__)

#pragma GCC target("sha,sse4.1")
static const __m128i MASK_ENDIAN = _mm_set_epi8(12,13,14,15, 8,9,10,11, 4,5,6,7, 0,1,2,3);
static const __m128i MASK_SHUFFLE_STATE = _mm_set_epi8(3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12);

static const uint32 K256[64] __attribute__((aligned(16))) = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};
#define SHA256_ROUND(msg0, msg1, msg2, msg3, st0, st1, k_idx) \
    { \
        __m128i k_vec = _mm_loadu_si128((const __m128i*)&K256[k_idx]); \
        st1 = _mm_sha256rnds2_epu32(st1, st0, _mm_add_epi32(msg0, k_vec)); \
        st0 = _mm_sha256rnds2_epu32(st0, st1, _mm_add_epi32(_mm_alignr_epi8(msg0, msg0, 8), k_vec)); \
        msg0 = _mm_sha256msg1_epu32(msg0, msg1); \
        msg0 = _mm_add_epi32(msg0, _mm_sha256msg2_epu32(_mm_alignr_epi8(msg3, msg2, 4), msg3)); \
    }
/*
#define SHA256_ROUND(msg0, msg1, msg2, msg3, st0, st1, k_idx) \
    { \
        __m128i k_vec = _mm_loadu_si128((const __m128i*)&K256[k_idx]); \
        k_vec = _mm_shuffle_epi8(k_vec, MASK_ENDIAN); \
        st1 = _mm_sha256rnds2_epu32(st1, st0, k_vec); \
        st0 = _mm_sha256rnds2_epu32(st0, st1, _mm_alignr_epi8(k_vec, k_vec, 8)); \
        msg0 = _mm_sha256msg1_epu32(msg0, msg1); \
        msg0 = _mm_add_epi32(msg0, _mm_sha256msg2_epu32(_mm_alignr_epi8(msg3, msg2, 4), msg3)); \
    }
    */
/*
static void
sha256_transform_block(__m128i& state0, __m128i& state1, const uint8* data)
{
    __m128i msg0, msg1, msg2, msg3;
    __m128i abef_save = state0;
    __m128i cdgh_save = state1;

    msg0 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(data + 0)), MASK_ENDIAN);
    msg1 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(data + 16)), MASK_ENDIAN);
    msg2 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(data + 32)), MASK_ENDIAN);
    msg3 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(data + 48)), MASK_ENDIAN);

    SHA256_ROUND(msg0, msg1, msg2, msg3, state0, state1, 0);
    SHA256_ROUND(msg1, msg2, msg3, msg0, state0, state1, 4);
    SHA256_ROUND(msg2, msg3, msg0, msg1, state0, state1, 8);
    SHA256_ROUND(msg3, msg0, msg1, msg2, state0, state1, 12);
    SHA256_ROUND(msg0, msg1, msg2, msg3, state0, state1, 16);
    SHA256_ROUND(msg1, msg2, msg3, msg0, state0, state1, 20);
    SHA256_ROUND(msg2, msg3, msg0, msg1, state0, state1, 24);
    SHA256_ROUND(msg3, msg0, msg1, msg2, state0, state1, 28);
    SHA256_ROUND(msg0, msg1, msg2, msg3, state0, state1, 32);
    SHA256_ROUND(msg1, msg2, msg3, msg0, state0, state1, 36);
    SHA256_ROUND(msg2, msg3, msg0, msg1, state0, state1, 40);
    SHA256_ROUND(msg3, msg0, msg1, msg2, state0, state1, 44);
    SHA256_ROUND(msg0, msg1, msg2, msg3, state0, state1, 48);
    SHA256_ROUND(msg1, msg2, msg3, msg0, state0, state1, 52);
    SHA256_ROUND(msg2, msg3, msg0, msg1, state0, state1, 56);
    SHA256_ROUND(msg3, msg0, msg1, msg2, state0, state1, 60);

    state0 = _mm_add_epi32(state0, abef_save);
    state1 = _mm_add_epi32(state1, cdgh_save);
}
*/
static void
sha256_transform_block(__m128i& st0, __m128i& st1, const uint8* data)
{
    __m128i msg0, msg1, msg2, msg3;
    __m128i old_st0 = st0;
    __m128i old_st1 = st1;

    // Caricamento messaggio con inversione endianness per SHA-NI
    msg0 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(data + 0)),  MASK_ENDIAN);
    msg1 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(data + 16)), MASK_ENDIAN);
    msg2 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(data + 32)), MASK_ENDIAN);
    msg3 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(data + 48)), MASK_ENDIAN);

    // 64 Round (4 per macro = 16 chiamate)
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
extract_digest(__m128i st0, __m128i st1, uint8* out)
{
    uint32_t raw[8];
    _mm_storeu_si128((__m128i*)&raw[0], st0);
    _mm_storeu_si128((__m128i*)&raw[4], st1);

    uint32_t* out32 = (uint32_t*)out;
    // Mappatura Intel SHA-NI a SHA-256 A,B,C,D,E,F,G,H
    out32[0] = __builtin_bswap32(raw[3]); // A
    out32[1] = __builtin_bswap32(raw[2]); // B
    out32[2] = __builtin_bswap32(raw[1]); // C
    out32[3] = __builtin_bswap32(raw[0]); // D
    out32[4] = __builtin_bswap32(raw[7]); // E
    out32[5] = __builtin_bswap32(raw[6]); // F
    out32[6] = __builtin_bswap32(raw[5]); // G
    out32[7] = __builtin_bswap32(raw[4]); // H
}
/*
status_t
x86_sha256_process(BCryptoRequest* request)
{
    if (request->algorithm != B_CRYPTO_SHA256)
        return B_BAD_VALUE;

    // Inizializzazione stato (Ordine corretto per SHA-NI: DCBA e HGFE)
    __m128i state0 = _mm_set_epi32(0xa54ff53a, 0x3c6ef372, 0xbb67ae85, 0x6a09e667); // DCBA
    __m128i state1 = _mm_set_epi32(0x5be0cd19, 0x1f83d9ab, 0x9b05688c, 0x510e527f); // HGFE

    uint64 totalByteCount = 0;
    uint8 pendingBuffer[128];
    size_t pendingLen = 0;

    for (size_t i = 0; i < request->vectorCount; i++) {
        const uint8* data = (const uint8*)request->source[i].iov_base;
        size_t len = request->source[i].iov_len;
        totalByteCount += len;

        // Se abbiamo dati pendenti dal vettore precedente
        if (pendingLen > 0) {
            size_t toCopy = 64 - pendingLen;
            if (len >= toCopy) {
                memcpy(pendingBuffer + pendingLen, data, toCopy);
                sha256_transform_block(state0, state1, pendingBuffer);
                data += toCopy;
                len -= toCopy;
                pendingLen = 0;
            }
        }

        // Processa i blocchi pieni da 64 byte
        while (len >= 64) {
            sha256_transform_block(state0, state1, data);
            data += 64;
            len -= 64;
        }

        // Salva i rimasugli per il padding finale
        if (len > 0) {
            memcpy(pendingBuffer, data, len);
            pendingLen = len;
        }
    }

    // --- LOGICA DI PADDING FINALE ---
    // 1. Aggiungi il bit 0x80
    pendingBuffer[pendingLen++] = 0x80;

    // 2. Se non c'è spazio per la lunghezza (8 byte), chiudi questo blocco e fanne un altro
    if (pendingLen > 56) {
        memset(pendingBuffer + pendingLen, 0, 64 - pendingLen);
        sha256_transform_block(state0, state1, pendingBuffer);
        pendingLen = 0;
    }

    // 3. Riempi di zeri fino all'offset 56
    memset(pendingBuffer + pendingLen, 0, 56 - pendingLen);

    // 4. Inserisci la lunghezza totale in BIT (Big Endian)
    uint64 totalBits = __builtin_bswap64(totalByteCount * 8);
    memcpy(pendingBuffer + 56, &totalBits, 8);
    sha256_transform_block(state0, state1, pendingBuffer);

    // Usiamo un array d'appoggio per non impazzire con gli shuffle XMM
    uint32 hash[8];
    
    // SHA-NI memorizza state1 come (H,G,F,E) e state0 come (D,C,B,A)
    // Ma attenzione: all'interno di ogni registro l'ordine è invertito per la logica hardware
    _mm_storeu_si128((__m128i*)&hash[0], state1); // Carica DCBA
    _mm_storeu_si128((__m128i*)&hash[4], state0); // Carica HGFE

    if (request->destination[0].iov_base) {
        uint8* out = (uint8*)request->destination[0].iov_base;
        for (int i = 0; i < 8; i++) {
            // SHA-256 standard vuole Big Endian. 
            // Dobbiamo invertire i byte di ogni word a 32-bit.
            ((uint32*)out)[i] = __builtin_bswap32(hash[7-i]);
        }
    }

    return B_OK;
}
*/
/*
status_t
x86_sha256_process(BCryptoRequest* request)
{
    if (request->algorithm != B_CRYPTO_SHA256)
        return B_BAD_VALUE;

    // 1. Inizializzazione Stato (Ordine corretto per SHA-NI)
    __m128i st0 = _mm_set_epi32(0xa54ff53a, 0x3c6ef372, 0xbb67ae85, 0x6a09e667); // DCBA
    __m128i st1 = _mm_set_epi32(0x5be0cd19, 0x1f83d9ab, 0x9b05688c, 0x510e527f); // HGFE

    uint64 totalByteCount = 0;
    
    for (size_t i = 0; i < request->vectorCount; i++) {
        const uint8* data = (const uint8*)request->source[i].iov_base;
        size_t len = request->source[i].iov_len;
        totalByteCount += len;

        while (len >= 64) {
            sha256_transform_block(st0, st1, data);
            data += 64;
            len -= 64;
        }

        // Padding finale (solo sull'ultimo vettore)
        if (i == request->vectorCount - 1) {
            uint8 pad[128];
            memset(pad, 0, 128);
            memcpy(pad, data, len);
            pad[len] = 0x80;
            
            size_t padLen = (len < 56) ? 64 : 128;
            uint64 bits = __builtin_bswap64(totalByteCount * 8);
            memcpy(pad + padLen - 8, &bits, 8);
            
            sha256_transform_block(st0, st1, pad);
            if (padLen == 128)
                sha256_transform_block(st0, st1, pad + 64);
        }
    }

    // 2. ESTRAZIONE MANUALE (Per evitare i pattern ripetitivi)
    // Scarichiamo i registri in array temporanei per rimetterli in ordine
    uint32_t raw_st0[4];
    uint32_t raw_st1[4];
    _mm_storeu_si128((__m128i*)raw_st0, st0);
    _mm_storeu_si128((__m128i*)raw_st1, st1);

    if (request->destination[0].iov_base) {
        uint32_t* out32 = (uint32_t*)request->destination[0].iov_base;
        
        // Mappatura Intel SHA-NI -> SHA-256 Standard:
        // raw_st0 contiene {FE, HG, DC, BA} (a seconda di come lo vede la CPU)
        // La logica corretta per rimetterli in A,B,C,D,E,F,G,H è:
        out32[0] = __builtin_bswap32(raw_st0[3]); // A
        out32[1] = __builtin_bswap32(raw_st0[2]); // B
        out32[2] = __builtin_bswap32(raw_st0[1]); // C
        out32[3] = __builtin_bswap32(raw_st0[0]); // D
        out32[4] = __builtin_bswap32(raw_st1[3]); // E
        out32[5] = __builtin_bswap32(raw_st1[2]); // F
        out32[6] = __builtin_bswap32(raw_st1[1]); // G
        out32[7] = __builtin_bswap32(raw_st1[0]); // H
    }

    return B_OK;
}
*/
status_t
x86_sha256_process(BCryptoRequest* request)
{
    if (request->algorithm != B_CRYPTO_SHA256) return B_BAD_VALUE;

    __m128i st0 = _mm_set_epi32(0xa54ff53a, 0x3c6ef372, 0xbb67ae85, 0x6a09e667);
    __m128i st1 = _mm_set_epi32(0x5be0cd19, 0x1f83d9ab, 0x9b05688c, 0x510e527f);
    uint64 totalLen = 0;

    for (size_t i = 0; i < request->vectorCount; i++) {
        const uint8* data = (const uint8*)request->source[i].iov_base;
        size_t len = request->source[i].iov_len;
        totalLen += len;

        while (len >= 64) {
            sha256_transform_block(st0, st1, data);
            data += 64; len -= 64;
        }

        if (i == request->vectorCount - 1) {
            uint8 pad[128];
            memset(pad, 0, 128);
            memcpy(pad, data, len);
            pad[len] = 0x80;
            size_t padLen = (len < 56) ? 64 : 128;
            uint64_t bits = __builtin_bswap64(totalLen * 8);
            memcpy(pad + padLen - 8, &bits, 8);
            sha256_transform_block(st0, st1, pad);
            if (padLen == 128) sha256_transform_block(st0, st1, pad + 64);
        }
    }

    if (request->destination[0].iov_base)
        extract_digest(st0, st1, (uint8*)request->destination[0].iov_base);

    return B_OK;
}
/*
static status_t
x86_sha256_init_bridge(void** context, size_t* contextSize)
{
    *contextSize = sizeof(x86_sha256_context);
    x86_sha256_context* ctx = (x86_sha256_context*)malloc(sizeof(x86_sha256_context));
    if (!ctx) return B_NO_MEMORY;

    // Stato iniziale SHA-256 ruotato per SHA-NI
    ctx->state0 = _mm_set_epi32(0xa54ff53a, 0x3c6ef372, 0xbb67ae85, 0x6a09e667);
    ctx->state1 = _mm_set_epi32(0x5be0cd19, 0x1f83d9ab, 0x9b05688c, 0x510e527f);
    ctx->buffer_len = 0;
    ctx->total_len = 0;
    *context = ctx;
    return B_OK;
}*/
status_t x86_sha256_init_bridge(void** ctx, size_t* size) {
    *size = sizeof(x86_sha256_context);
    *ctx = malloc(*size);
    if (!*ctx) return B_NO_MEMORY;
    x86_sha256_context* s = (x86_sha256_context*)*ctx;
    s->state0 = _mm_set_epi32(0xa54ff53a, 0x3c6ef372, 0xbb67ae85, 0x6a09e667);
    s->state1 = _mm_set_epi32(0x5be0cd19, 0x1f83d9ab, 0x9b05688c, 0x510e527f);
    s->buffer_len = 0; s->total_len = 0;
    return B_OK;
}
/*
static status_t
x86_sha256_final_bridge(void* context, uint8* outDigest)
{
    x86_sha256_context* ctx = (x86_sha256_context*)context;
    uint8 pad[128];
    size_t pad_len = ctx->buffer_len;
    
    memcpy(pad, ctx->buffer, pad_len);
    pad[pad_len++] = 0x80;

    size_t target_pad = (pad_len > 56) ? 128 : 64;
    memset(pad + pad_len, 0, target_pad - pad_len);
    
    uint64 bits = __builtin_bswap64(ctx->total_len * 8);
    memcpy(pad + target_pad - 8, &bits, 8);

    sha256_transform_block(ctx->state0, ctx->state1, pad);
    if (target_pad == 128)
        sha256_transform_block(ctx->state0, ctx->state1, pad + 64);

    // ESTRAZIONE CORRETTA: SHA-NI usa Little Endian nei registri, 
    // lo standard SHA-256 vuole Big Endian.
    __m128i dcbA = _mm_shuffle_epi8(ctx->state0, MASK_ENDIAN);
    __m128i hgfE = _mm_shuffle_epi8(ctx->state1, MASK_ENDIAN);

    _mm_storeu_si128((__m128i*)outDigest, dcbA);
    _mm_storeu_si128((__m128i*)(outDigest + 16), hgfE);

    return B_OK;
}*/
/*
static status_t
x86_sha256_final_bridge(void* context, uint8* outDigest)
{
    x86_sha256_context* ctx = (x86_sha256_context*)context;
    if (ctx == NULL || outDigest == NULL)
        return B_BAD_VALUE;

    uint8 pad[128];
    size_t pad_len = ctx->buffer_len;
    
    // 1. Prepariamo il padding nel buffer locale
    memcpy(pad, ctx->buffer, pad_len);
    pad[pad_len++] = 0x80;

    size_t target_pad = (pad_len > 56) ? 128 : 64;
    memset(pad + pad_len, 0, target_pad - pad_len);
    
    // Lunghezza totale in bit (Big Endian)
    uint64 bits = __builtin_bswap64(ctx->total_len * 8);
    memcpy(pad + target_pad - 8, &bits, 8);

    // 2. PROCESSIAMO IL PADDING (Il pezzo che mancava!)
    sha256_transform_block(ctx->state0, ctx->state1, pad);
    if (target_pad == 128)
        sha256_transform_block(ctx->state0, ctx->state1, pad + 64);

    // 3. TRASFORMAZIONE FINALE REGISTRI
    // SHA-NI: state0 = (B, A, D, C), state1 = (F, E, H, G)
    // Riportiamo in ordine A, B, C, D e E, F, G, H
    __m128i abcd = _mm_shuffle_epi32(ctx->state0, _MM_SHUFFLE(1, 0, 3, 2));
    __m128i efgh = _mm_shuffle_epi32(ctx->state1, _MM_SHUFFLE(1, 0, 3, 2));

    // Invertiamo l'endianness di ogni word (Little -> Big Endian)
    abcd = _mm_shuffle_epi8(abcd, MASK_ENDIAN);
    efgh = _mm_shuffle_epi8(efgh, MASK_ENDIAN);

    // 4. SALVATAGGIO
    _mm_storeu_si128((__m128i*)outDigest, abcd);
    _mm_storeu_si128((__m128i*)(outDigest + 16), efgh);

    // La free del contesto la gestisce il BCryptoCore dopo questa chiamata
    return B_OK;
}
*/
status_t x86_sha256_final_bridge(void* ctx, uint8* out) {
    x86_sha256_context* s = (x86_sha256_context*)ctx;
    uint8 pad[128];
    memcpy(pad, s->buffer, s->buffer_len);
    pad[s->buffer_len] = 0x80;
    size_t padLen = (s->buffer_len < 56) ? 64 : 128;
    memset(pad + s->buffer_len + 1, 0, padLen - s->buffer_len - 1);
    uint64_t bits = __builtin_bswap64(s->total_len * 8);
    memcpy(pad + padLen - 8, &bits, 8);
    sha256_transform_block(s->state0, s->state1, pad);
    if (padLen == 128) sha256_transform_block(s->state0, s->state1, pad + 64);
    extract_digest(s->state0, s->state1, out);
    free(ctx);
    return B_OK;
}
/*
static status_t
x86_sha256_update_bridge(void* context, const iovec* vecs, size_t count)
{
    x86_sha256_context* ctx = (x86_sha256_context*)context;
    for (size_t i = 0; i < count; i++) {
        const uint8* data = (const uint8*)vecs[i].iov_base;
        size_t len = vecs[i].iov_len;
        ctx->total_len += len;

        // Gestione buffer parziale
        if (ctx->buffer_len > 0) {
            size_t partial = 64 - ctx->buffer_len;
            if (len >= partial) {
                memcpy(ctx->buffer + ctx->buffer_len, data, partial);
                sha256_transform_block(ctx->state0, ctx->state1, ctx->buffer);
                data += partial;
                len -= partial;
                ctx->buffer_len = 0;
            }
        }

        while (len >= 64) {
            sha256_transform_block(ctx->state0, ctx->state1, data);
            data += 64;
            len -= 64;
        }

        if (len > 0) {
            memcpy(ctx->buffer + ctx->buffer_len, data, len);
            ctx->buffer_len += len;
        }
    }
    return B_OK;
}*/
status_t x86_sha256_update_bridge(void* ctx, const iovec* vecs, size_t count) {
    x86_sha256_context* s = (x86_sha256_context*)ctx;
    for (size_t i = 0; i < count; i++) {
        const uint8* data = (const uint8*)vecs[i].iov_base;
        size_t len = vecs[i].iov_len;
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
    }
    return B_OK;
}

status_t
BInitx86CPUDigest()
{
    // Verifichiamo se il Core ha rilevato SHA-NI durante il boot
    if (!(BGetStoredCryptoCapabilities() & B_CRYPTO_HW_SHA_NI))
        return B_UNSUPPORTED;

    static BCryptoAlgorithm sSHA256_HW = {
        .algorithm = B_CRYPTO_SHA256,
        .mode      = B_CRYPTO_MODE_ANY,
        .flags     = B_CRYPTO_ALG_HW_ACCEL,
        .priority  = 90, // Vince su SoftDigest
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
