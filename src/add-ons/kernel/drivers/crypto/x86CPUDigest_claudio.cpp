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

#if defined(__x86_64__) || defined(__i386__)

#pragma GCC target("sha,sse4.1,ssse3")

// Costanti SHA-256
static const uint32 K256[64] __attribute__((aligned(16))) = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

// Maschera per invertire l'endianness dei messaggi (da little a big endian)
static const __m128i SHUF_MASK = _mm_set_epi8(
    12, 13, 14, 15,
    8, 9, 10, 11,
    4, 5, 6, 7,
    0, 1, 2, 3
);

/*
 * Funzione di trasformazione per un blocco da 64 byte
 * 
 * Intel SHA-NI memorizza lo stato in modo particolare:
 * - state0 contiene: ABCD (dove A è nell'elemento più alto)
 * - state1 contiene: EFGH (dove E è nell'elemento più alto)
 */
static void
sha256_transform_block(__m128i& state0, __m128i& state1, const uint8* block)
{
    __m128i msg0, msg1, msg2, msg3;
    __m128i tmp;
    __m128i abcd_save, efgh_save;

    // Salva lo stato iniziale per l'addizione finale
    abcd_save = state0;
    efgh_save = state1;

    // Carica i 4 blocchi di messaggio da 16 byte e inverti l'endianness
    msg0 = _mm_loadu_si128((const __m128i*)(block + 0));
    msg0 = _mm_shuffle_epi8(msg0, SHUF_MASK);

    msg1 = _mm_loadu_si128((const __m128i*)(block + 16));
    msg1 = _mm_shuffle_epi8(msg1, SHUF_MASK);

    msg2 = _mm_loadu_si128((const __m128i*)(block + 32));
    msg2 = _mm_shuffle_epi8(msg2, SHUF_MASK);

    msg3 = _mm_loadu_si128((const __m128i*)(block + 48));
    msg3 = _mm_shuffle_epi8(msg3, SHUF_MASK);

    // Rounds 0-3
    tmp = _mm_add_epi32(msg0, _mm_loadu_si128((const __m128i*)&K256[0]));
    state1 = _mm_sha256rnds2_epu32(state1, state0, tmp);
    tmp = _mm_shuffle_epi32(tmp, 0x0E);
    state0 = _mm_sha256rnds2_epu32(state0, state1, tmp);

    // Rounds 4-7
    tmp = _mm_add_epi32(msg1, _mm_loadu_si128((const __m128i*)&K256[4]));
    state1 = _mm_sha256rnds2_epu32(state1, state0, tmp);
    tmp = _mm_shuffle_epi32(tmp, 0x0E);
    state0 = _mm_sha256rnds2_epu32(state0, state1, tmp);
    msg0 = _mm_sha256msg1_epu32(msg0, msg1);

    // Rounds 8-11
    tmp = _mm_add_epi32(msg2, _mm_loadu_si128((const __m128i*)&K256[8]));
    state1 = _mm_sha256rnds2_epu32(state1, state0, tmp);
    tmp = _mm_shuffle_epi32(tmp, 0x0E);
    state0 = _mm_sha256rnds2_epu32(state0, state1, tmp);
    msg1 = _mm_sha256msg1_epu32(msg1, msg2);

    // Rounds 12-15
    tmp = _mm_add_epi32(msg3, _mm_loadu_si128((const __m128i*)&K256[12]));
    state1 = _mm_sha256rnds2_epu32(state1, state0, tmp);
    msg0 = _mm_add_epi32(msg0, _mm_alignr_epi8(msg3, msg2, 4));
    msg0 = _mm_sha256msg2_epu32(msg0, msg3);
    tmp = _mm_shuffle_epi32(tmp, 0x0E);
    state0 = _mm_sha256rnds2_epu32(state0, state1, tmp);
    msg2 = _mm_sha256msg1_epu32(msg2, msg3);

    // Rounds 16-19
    tmp = _mm_add_epi32(msg0, _mm_loadu_si128((const __m128i*)&K256[16]));
    state1 = _mm_sha256rnds2_epu32(state1, state0, tmp);
    msg1 = _mm_add_epi32(msg1, _mm_alignr_epi8(msg0, msg3, 4));
    msg1 = _mm_sha256msg2_epu32(msg1, msg0);
    tmp = _mm_shuffle_epi32(tmp, 0x0E);
    state0 = _mm_sha256rnds2_epu32(state0, state1, tmp);
    msg3 = _mm_sha256msg1_epu32(msg3, msg0);

    // Rounds 20-23
    tmp = _mm_add_epi32(msg1, _mm_loadu_si128((const __m128i*)&K256[20]));
    state1 = _mm_sha256rnds2_epu32(state1, state0, tmp);
    msg2 = _mm_add_epi32(msg2, _mm_alignr_epi8(msg1, msg0, 4));
    msg2 = _mm_sha256msg2_epu32(msg2, msg1);
    tmp = _mm_shuffle_epi32(tmp, 0x0E);
    state0 = _mm_sha256rnds2_epu32(state0, state1, tmp);
    msg0 = _mm_sha256msg1_epu32(msg0, msg1);

    // Rounds 24-27
    tmp = _mm_add_epi32(msg2, _mm_loadu_si128((const __m128i*)&K256[24]));
    state1 = _mm_sha256rnds2_epu32(state1, state0, tmp);
    msg3 = _mm_add_epi32(msg3, _mm_alignr_epi8(msg2, msg1, 4));
    msg3 = _mm_sha256msg2_epu32(msg3, msg2);
    tmp = _mm_shuffle_epi32(tmp, 0x0E);
    state0 = _mm_sha256rnds2_epu32(state0, state1, tmp);
    msg1 = _mm_sha256msg1_epu32(msg1, msg2);

    // Rounds 28-31
    tmp = _mm_add_epi32(msg3, _mm_loadu_si128((const __m128i*)&K256[28]));
    state1 = _mm_sha256rnds2_epu32(state1, state0, tmp);
    msg0 = _mm_add_epi32(msg0, _mm_alignr_epi8(msg3, msg2, 4));
    msg0 = _mm_sha256msg2_epu32(msg0, msg3);
    tmp = _mm_shuffle_epi32(tmp, 0x0E);
    state0 = _mm_sha256rnds2_epu32(state0, state1, tmp);
    msg2 = _mm_sha256msg1_epu32(msg2, msg3);

    // Rounds 32-35
    tmp = _mm_add_epi32(msg0, _mm_loadu_si128((const __m128i*)&K256[32]));
    state1 = _mm_sha256rnds2_epu32(state1, state0, tmp);
    msg1 = _mm_add_epi32(msg1, _mm_alignr_epi8(msg0, msg3, 4));
    msg1 = _mm_sha256msg2_epu32(msg1, msg0);
    tmp = _mm_shuffle_epi32(tmp, 0x0E);
    state0 = _mm_sha256rnds2_epu32(state0, state1, tmp);
    msg3 = _mm_sha256msg1_epu32(msg3, msg0);

    // Rounds 36-39
    tmp = _mm_add_epi32(msg1, _mm_loadu_si128((const __m128i*)&K256[36]));
    state1 = _mm_sha256rnds2_epu32(state1, state0, tmp);
    msg2 = _mm_add_epi32(msg2, _mm_alignr_epi8(msg1, msg0, 4));
    msg2 = _mm_sha256msg2_epu32(msg2, msg1);
    tmp = _mm_shuffle_epi32(tmp, 0x0E);
    state0 = _mm_sha256rnds2_epu32(state0, state1, tmp);
    msg0 = _mm_sha256msg1_epu32(msg0, msg1);

    // Rounds 40-43
    tmp = _mm_add_epi32(msg2, _mm_loadu_si128((const __m128i*)&K256[40]));
    state1 = _mm_sha256rnds2_epu32(state1, state0, tmp);
    msg3 = _mm_add_epi32(msg3, _mm_alignr_epi8(msg2, msg1, 4));
    msg3 = _mm_sha256msg2_epu32(msg3, msg2);
    tmp = _mm_shuffle_epi32(tmp, 0x0E);
    state0 = _mm_sha256rnds2_epu32(state0, state1, tmp);
    msg1 = _mm_sha256msg1_epu32(msg1, msg2);

    // Rounds 44-47
    tmp = _mm_add_epi32(msg3, _mm_loadu_si128((const __m128i*)&K256[44]));
    state1 = _mm_sha256rnds2_epu32(state1, state0, tmp);
    msg0 = _mm_add_epi32(msg0, _mm_alignr_epi8(msg3, msg2, 4));
    msg0 = _mm_sha256msg2_epu32(msg0, msg3);
    tmp = _mm_shuffle_epi32(tmp, 0x0E);
    state0 = _mm_sha256rnds2_epu32(state0, state1, tmp);
    msg2 = _mm_sha256msg1_epu32(msg2, msg3);

    // Rounds 48-51
    tmp = _mm_add_epi32(msg0, _mm_loadu_si128((const __m128i*)&K256[48]));
    state1 = _mm_sha256rnds2_epu32(state1, state0, tmp);
    msg1 = _mm_add_epi32(msg1, _mm_alignr_epi8(msg0, msg3, 4));
    msg1 = _mm_sha256msg2_epu32(msg1, msg0);
    tmp = _mm_shuffle_epi32(tmp, 0x0E);
    state0 = _mm_sha256rnds2_epu32(state0, state1, tmp);
    msg3 = _mm_sha256msg1_epu32(msg3, msg0);

    // Rounds 52-55
    tmp = _mm_add_epi32(msg1, _mm_loadu_si128((const __m128i*)&K256[52]));
    state1 = _mm_sha256rnds2_epu32(state1, state0, tmp);
    msg2 = _mm_add_epi32(msg2, _mm_alignr_epi8(msg1, msg0, 4));
    msg2 = _mm_sha256msg2_epu32(msg2, msg1);
    tmp = _mm_shuffle_epi32(tmp, 0x0E);
    state0 = _mm_sha256rnds2_epu32(state0, state1, tmp);

    // Rounds 56-59
    tmp = _mm_add_epi32(msg2, _mm_loadu_si128((const __m128i*)&K256[56]));
    state1 = _mm_sha256rnds2_epu32(state1, state0, tmp);
    msg3 = _mm_add_epi32(msg3, _mm_alignr_epi8(msg2, msg1, 4));
    msg3 = _mm_sha256msg2_epu32(msg3, msg2);
    tmp = _mm_shuffle_epi32(tmp, 0x0E);
    state0 = _mm_sha256rnds2_epu32(state0, state1, tmp);

    // Rounds 60-63
    tmp = _mm_add_epi32(msg3, _mm_loadu_si128((const __m128i*)&K256[60]));
    state1 = _mm_sha256rnds2_epu32(state1, state0, tmp);
    tmp = _mm_shuffle_epi32(tmp, 0x0E);
    state0 = _mm_sha256rnds2_epu32(state0, state1, tmp);

    // Aggiungi lo stato salvato
    state0 = _mm_add_epi32(state0, abcd_save);
    state1 = _mm_add_epi32(state1, efgh_save);
}

/*
 * Estrae il digest finale dai registri SIMD
 */
static void
extract_digest(__m128i state0, __m128i state1, uint8* digest)
{
    // Prima riordiniamo: da {C,D,A,B} a {A,B,C,D} e da {G,H,E,F} a {E,F,G,H}
    state0 = _mm_shuffle_epi32(state0, 0xB1); // 10-11-00-01 -> swap pairs
    state1 = _mm_shuffle_epi32(state1, 0xB1);
    
    // Poi scambiamo le coppie
    __m128i tmp0 = state0;
    state0 = _mm_blend_epi16(state0, state1, 0xF0); // prendi high da state1
    state1 = _mm_blend_epi16(state1, tmp0, 0xF0);   // prendi high da state0
    
    // Ora abbiamo A,B,E,F in state0 e C,D,G,H in state1
    // Dobbiamo fare un altro riordino
    tmp0 = _mm_unpacklo_epi64(state0, state1);  // A,B,C,D
    state1 = _mm_unpackhi_epi64(state0, state1); // E,F,G,H
    state0 = tmp0;
    
    // Converti in big-endian
    state0 = _mm_shuffle_epi8(state0, SHUF_MASK);
    state1 = _mm_shuffle_epi8(state1, SHUF_MASK);
    
    // Scrivi il risultato
    _mm_storeu_si128((__m128i*)digest, state0);
    _mm_storeu_si128((__m128i*)(digest + 16), state1);
}
 /*
static void
extract_digest(__m128i state0, __m128i state1, uint8* digest)
{
    // state0 contiene ABCD, state1 contiene EFGH
    // Dobbiamo riordinarli e convertirli in big-endian
    
    // Prima invertiamo l'ordine degli elementi in ciascun registro
    state0 = _mm_shuffle_epi32(state0, 0x1B); // 0x1B = 00-01-10-11 invertito
    state1 = _mm_shuffle_epi32(state1, 0x1B);
    
    // Poi convertiamo in big-endian
    state0 = _mm_shuffle_epi8(state0, SHUF_MASK);
    state1 = _mm_shuffle_epi8(state1, SHUF_MASK);
    
    // Scrivi il risultato
    _mm_storeu_si128((__m128i*)digest, state0);
    _mm_storeu_si128((__m128i*)(digest + 16), state1);
}*/

/*
 * One-shot SHA-256 (processa tutto in una volta)
 */
status_t
x86_sha256_process(BCryptoRequest* request)
{
    if (request == NULL || request->algorithm != B_CRYPTO_SHA256)
        return B_BAD_VALUE;
    
    if (request->vectorCount == 0 || request->source == NULL)
        return B_OK;
    
    if (request->destination == NULL || request->destination[0].iov_base == NULL)
        return B_BAD_VALUE;

    B_PREPARE_CPU_STATE();

    // Inizializza lo stato SHA-256
    // Nota: questi valori devono essere in little-endian e nell'ordine DCBA/HGFE
    __m128i state0 = _mm_setr_epi32(0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a);
    __m128i state1 = _mm_setr_epi32(0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19);

    uint64 totalBytes = 0;

    // Calcola la lunghezza totale
    for (size_t i = 0; i < request->vectorCount; i++)
        totalBytes += request->source[i].iov_len;

    // Processa i dati
    for (size_t i = 0; i < request->vectorCount; i++) {
        const uint8* data = (const uint8*)request->source[i].iov_base;
        size_t len = request->source[i].iov_len;

        if (data == NULL || len == 0)
            continue;

        // Processa blocchi completi da 64 byte
        while (len >= 64) {
            sha256_transform_block(state0, state1, data);
            data += 64;
            len -= 64;
        }

        // Gestione padding solo sull'ultimo vettore
        if (i == request->vectorCount - 1) {
            alignas(16) uint8 pad[128];
            memset(pad, 0, 128);
            memcpy(pad, data, len);
            
            // Aggiungi il bit 1 seguito da zeri
            pad[len] = 0x80;
            
            // Determina se serve uno o due blocchi
            size_t padLen = (len < 56) ? 64 : 128;
            
            // Aggiungi la lunghezza in bit (big-endian) negli ultimi 8 byte
            uint64 bitLen = __builtin_bswap64(totalBytes * 8);
            memcpy(pad + padLen - 8, &bitLen, 8);
            
            // Processa il/i blocco/i di padding
            sha256_transform_block(state0, state1, pad);
            if (padLen == 128)
                sha256_transform_block(state0, state1, pad + 64);
        }
    }

    // Estrai il digest finale
    extract_digest(state0, state1, (uint8*)request->destination[0].iov_base);

    B_RESTORE_CPU_STATE();
    return B_OK;
}

// --- STREAMING API (Init/Update/Final) ---

status_t
x86_sha256_init_bridge(void** ctx, size_t* size)
{
    if (ctx == NULL || size == NULL)
        return B_BAD_VALUE;
    
    *size = sizeof(x86_sha256_context);
    *ctx = malloc(*size);
    if (!*ctx)
        return B_NO_MEMORY;
    
    x86_sha256_context* s = (x86_sha256_context*)*ctx;
    memset(s, 0, sizeof(x86_sha256_context));
    
    // Inizializza lo stato
    s->state0 = _mm_setr_epi32(0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a);
    s->state1 = _mm_setr_epi32(0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19);
    s->buffer_len = 0;
    s->total_len = 0;
    
    return B_OK;
}

status_t
x86_sha256_update_bridge(void* ctx, const iovec* vecs, size_t count)
{
    x86_sha256_context* s = (x86_sha256_context*)ctx;
    if (s == NULL)
        return B_BAD_VALUE;

    B_PREPARE_CPU_STATE();

    for (size_t i = 0; i < count; i++) {
        const uint8* data = (const uint8*)vecs[i].iov_base;
        size_t len = vecs[i].iov_len;
        
        if (data == NULL || len == 0)
            continue;
        
        s->total_len += len;

        // Riempi il buffer parziale
        while (len > 0) {
            size_t copy = (64 - s->buffer_len < len) ? 64 - s->buffer_len : len;
            memcpy(s->buffer + s->buffer_len, data, copy);
            s->buffer_len += copy;
            data += copy;
            len -= copy;

            // Se il buffer è pieno, processalo
            if (s->buffer_len == 64) {
                sha256_transform_block(s->state0, s->state1, s->buffer);
                s->buffer_len = 0;
            }
        }
    }

    B_RESTORE_CPU_STATE();
    return B_OK;
}

status_t
x86_sha256_final_bridge(void* ctx, uint8* out)
{
    x86_sha256_context* s = (x86_sha256_context*)ctx;
    if (s == NULL || out == NULL)
        return B_BAD_VALUE;

    B_PREPARE_CPU_STATE();

    alignas(16) uint8 pad[128];
    memcpy(pad, s->buffer, s->buffer_len);
    pad[s->buffer_len] = 0x80;
    
    size_t padLen = (s->buffer_len < 56) ? 64 : 128;
    memset(pad + s->buffer_len + 1, 0, padLen - s->buffer_len - 1);
    
    uint64 bits = __builtin_bswap64(s->total_len * 8);
    memcpy(pad + padLen - 8, &bits, 8);
    
    sha256_transform_block(s->state0, s->state1, pad);
    if (padLen == 128)
        sha256_transform_block(s->state0, s->state1, pad + 64);
    
    extract_digest(s->state0, s->state1, out);

    B_RESTORE_CPU_STATE();
    
    free(ctx);
    return B_OK;
}

status_t
BInitx86CPUDigest()
{
    if (!(BGetStoredCryptoCapabilities() & B_CRYPTO_HW_SHA_NI))
        return B_UNSUPPORTED;

    static BCryptoAlgorithm sSHA256_HW = {
        .algorithm = B_CRYPTO_SHA256,
        .mode      = B_CRYPTO_MODE_ANY,
        .flags     = B_CRYPTO_ALG_HW_ACCEL,
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
