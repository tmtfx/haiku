/*
 * Hybrid SIMD/AVX implementation for SHA
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * Optimized for Haiku Kernel. MIT License.
 */
#include "soft_sha.h"
#include "hybrid_sha_opt.h"
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

// Costanti K per SHA-256
static const uint32 K256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, ... 
};

// IV per SHA-256
static const uint32 IV256[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
};

// IV per SHA-224
static const uint32 IV224[8] = {
    0xc1059ed8, 0x367cd507, 0x3070dd17, 0xf70e5939,
    0xffc00b31, 0x68581511, 0x64f98fa7, 0xbefa4fa4
};

/* -------- SCHEDULER SCALARE ------ */
// Macro per i round scalari
#define Ch(x,y,z)  (((x) & (y)) ^ (~(x) & (z)))
#define Maj(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define S0(x)      (ROR32(x, 2) ^ ROR32(x, 13) ^ ROR32(x, 22))
#define S1(x)      (ROR32(x, 6) ^ ROR32(x, 11) ^ ROR32(x, 25))
#define s0(x)      (ROR32(x, 7) ^ ROR32(x, 18) ^ ((x) >> 3))
#define s1(x)      (ROR32(x, 17) ^ ROR32(x, 19) ^ ((x) >> 10))

__attribute__((target("sse4.1")))
void hybrid_sha256_transform_sse(SoftSHA256Context* ctx, const uint8* data) {
    uint32 W[64];
    uint32 a, b, c, d, e, f, g, h;

    // 1. Caricamento scalare con byte swap
    for (int i = 0; i < 16; i++) {
        W[i] = B_BENDIAN_TO_HOST_INT32(((uint32*)data)[i]);
    }

    // 2. Scheduling Scalare
    for (int i = 16; i < 64; i++) {
        W[i] = s1(W[i-2]) + W[i-7] + s0(W[i-15]) + W[i-16];
    }

    // 3. Round di compressione
    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];

    for (int i = 0; i < 64; i++) {
        uint32 t1 = h + S1(e) + Ch(e, f, g) + K256[i] + W[i];
        uint32 t2 = S0(a) + Maj(a, b, c);
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }

    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}
/*- --------------------- SCHEDULER FULL SSE --------------------------------- -*/
__attribute__((target("sse4.1")))
void hybrid_sha256_transform_sse_full(SoftSHA256Context* ctx, const uint8* data) {
    uint32 W[64] __attribute__((aligned(16)));
    uint32 a, b, c, d, e, f, g, h;

    // Maschera per il byte-swap (Big Endian -> Little Endian)
    const __m128i mask = _mm_set_epi8(12,13,14,15, 8,9,10,11, 4,5,6,7, 0,1,2,3);

    // 1. Caricamento prime 16 parole (0-15)
    for (int i = 0; i < 4; i++) {
        __m128i raw = _mm_loadu_si128((const __m128i*)(data + i * 16));
        _mm_store_si128((__m128i*)&W[i * 4], _mm_shuffle_epi8(raw, mask));
    }

    // 2. Scheduling SSE (16-63)
    // Usiamo registri per minimizzare gli accessi alla stack
    for (int i = 16; i < 64; i += 4) {
        __m128i v0 = _mm_load_si128((__m128i*)&W[i - 16]);
        __m128i v1 = _mm_load_si128((__m128i*)&W[i - 12]);
        __m128i v2 = _mm_load_si128((__m128i*)&W[i - 8]);
        __m128i v3 = _mm_load_si128((__m128i*)&W[i - 4]);

        // s0 = sigma0(W[i-15])
        // w_i_15 = {W[i-15], W[i-14], W[i-13], W[i-12]}
        __m128i w_i_15 = _mm_alignr_epi8(v1, v0, 4);
        __m128i s0_v = SCHED_s0(w_i_15);

        // w_i_7 = {W[i-7], W[i-6], W[i-5], W[i-4]}
        __m128i w_i_7 = _mm_alignr_epi8(v3, v2, 4);

        // Calcolo parziale: W[i-16] + s0 + W[i-7]
        __m128i res = _mm_add_epi32(_mm_add_epi32(v0, s0_v), w_i_7);

        // Gestione di s1 = sigma1(W[i-2]) - QUI STA IL TRUCCO
        // W[i] e W[i+1] dipendono da v3 (già calcolato)
        // W[i+2] e W[i+3] dipendono da W[i] e W[i+1] (calcolati ora)
        
        // Fase 1: Calcoliamo i primi due elementi del registro
        __m128i s1_part1 = SCHED_s1(_mm_alignr_epi8(res, v3, 8)); // W[i-2], W[i-1]
        res = _mm_add_epi32(res, s1_part1);

        // Fase 2: Usiamo i risultati appena ottenuti per calcolare gli ultimi due
        __m128i s1_part2 = SCHED_s1(_mm_alignr_epi8(res, res, 8)); // W[i], W[i+1]
        // Filtriamo solo gli ultimi due con uno shuffle/mask
        s1_part2 = _mm_and_si128(s1_part2, _mm_set_epi32(-1, -1, 0, 0));
        res = _mm_add_epi32(res, s1_part2);

        _mm_store_si128((__m128i*)&W[i], res);
    }

    // 3. Round di compressione
    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];

    // Srotoliamo i round per evitare l'overhead del loop o carichiamo K256
    for (int i = 0; i < 64; i++) {
        uint32 t1 = h + S1(e) + Ch(e, f, g) + K256[i] + W[i];
        uint32 t2 = S0(a) + Maj(a, b, c);
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }

    // Aggiornamento stato
    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

__attribute__((target("avx2")))
void hybrid_sha256_transform_avx2(SoftSHA256Context* ctx, const uint8* data) {
    // Allineamento a 32 byte obbligatorio per AVX2
    uint32 W[64] __attribute__((aligned(32)));
    uint32 a, b, c, d, e, f, g, h;

    // Maschera 256-bit per byte-swap (ripetuta sui due lane da 128)
    const __m256i mask = _mm256_set_epi8(
        12,13,14,15, 8,9,10,11, 4,5,6,7, 0,1,2,3,
        12,13,14,15, 8,9,10,11, 4,5,6,7, 0,1,2,3
    );

    // 1. Caricamento prime 16 parole (due caricate a 256-bit alla volta)
    for (int i = 0; i < 2; i++) {
        __m256i raw = _mm256_loadu_si256((const __m256i*)(data + i * 32));
        _mm256_store_si256((__m256i*)&W[i * 8], _mm256_shuffle_epi8(raw, mask));
    }

    // 2. Scheduling AVX2 (8 parole per iterazione)
    // Calcoliamo W[i...i+7]
    for (int i = 16; i < 64; i += 8) {
        // Carichiamo i pezzi necessari dai blocchi precedenti
        __m256i v_minus_16 = _mm256_load_si256((__m256i*)&W[i - 16]);
        __m256i v_minus_8  = _mm256_load_si256((__m256i*)&W[i - 8]);
        
        // Per W[i-15] e W[i-2] servono cross-load
        // In AVX2 è più efficiente caricare con offset se la memoria è allineata
        __m256i v_minus_15 = _mm256_loadu_si256((__m256i*)&W[i - 15]);
        __m256i v_minus_7  = _mm256_loadu_si256((__m256i*)&W[i - 7]);
        __m256i v_minus_2  = _mm256_loadu_si256((__m256i*)&W[i - 2]);

        // s0 = sigma0(W[i-15])
        __m256i s0_v = _mm256_xor_si256(
            _mm256_xor_si256(
                _mm256_or_si256(_mm256_srli_epi32(v_minus_15, 7),  _mm256_slli_epi32(v_minus_15, 25)),
                _mm256_or_si256(_mm256_srli_epi32(v_minus_15, 18), _mm256_slli_epi32(v_minus_15, 14))
            ),
            _mm256_srli_epi32(v_minus_15, 3)
        );

        // s1 = sigma1(W[i-2]) 
        // Nota: Qui non abbiamo la dipendenza ricorsiva immediata perché 
        // carichiamo v_minus_2 che è già stato calcolato nel loop precedente!
        __m256i s1_v = _mm256_xor_si256(
            _mm256_xor_si256(
                _mm256_or_si256(_mm256_srli_epi32(v_minus_2, 17), _mm256_slli_epi32(v_minus_2, 15)),
                _mm256_or_si256(_mm256_srli_epi32(v_minus_2, 19), _mm256_slli_epi32(v_minus_2, 13))
            ),
            _mm256_srli_epi32(v_minus_2, 10)
        );

        // W[i] = s1 + W[i-7] + s0 + W[i-16]
        __m256i res = _mm256_add_epi32(_mm256_add_epi32(v_minus_16, s0_v), 
                                       _mm256_add_epi32(v_minus_7, s1_v));

        _mm256_store_si256((__m256i*)&W[i], res);
    }

    // 3. Round di compressione (Scalari, sono il collo di bottiglia seriale)
    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];

    for (int i = 0; i < 64; i++) {
        uint32 t1 = h + S1(e) + Ch(e, f, g) + K256[i] + W[i];
        uint32 t2 = S0(a) + Maj(a, b, c);
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }

    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

/*   ----  roba vecchia e bagliata ---------

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
}*/


/* -------- FUNZIONI INIT ---------- */
void hybrid_SHA1_init(SoftSHA1Context* ctx, size_t outLen) {
    soft_sha1_init(ctx);
    ctx->outlen = outLen; // Solitamente 20
}

void hybrid_SHA224_init(SoftSHA256Context* ctx, size_t outLen) {
    soft_sha224_init(ctx);
    ctx->outlen = outLen; // Solitamente 28
}

void hybrid_SHA256_init(SoftSHA256Context* ctx, size_t outLen) {
    //if (sUseAVX2) dprintf("BCrypto: SHA256 with AVX2\n");
    soft_sha256_init(ctx);
    ctx->outlen = outLen; // Solitamente 32
}

void hybrid_SHA384_init(SoftSHA512Context* ctx, size_t outLen) {
    //if (sUseAVX2) dprintf("BCrypto: SHA256 with AVX2\n");
    soft_sha384_init(ctx);
    ctx->outlen = outLen; // Solitamente 48
}

void hybrid_SHA512_init(SoftSHA512Context* ctx, size_t outLen) {
    //if (sUseAVX2) dprintf("BCrypto: SHA256 with AVX2\n");
    soft_sha512_init(ctx);
    ctx->outlen = outLen; // Solitamente 64
}

/* ---------- FUNZIONI UPDATE --------- */
void hybrid_SHA256_update(SoftSHA256Context* ctx, const uint8* in, size_t inLen) {
    while (inLen > 0) {
        size_t left = 64 - ctx->buflen; // SHA256 block size = 64
        size_t fill = (inLen > left) ? left : inLen;
        
        memcpy(ctx->buf + ctx->buflen, in, fill);
        ctx->buflen += fill;
        in += fill;
        inLen -= fill;

        if (ctx->buflen == 64) {
            ctx->count += 512; // conta i bit (64 bytes * 8)
            
            if (sUseAVX2) 
                hybrid_sha256_transform_avx2(ctx, ctx->buf);
            else 
                hybrid_sha256_transform_sse_full(ctx, ctx->buf);
            
            ctx->buflen = 0;
        }
    }
}

/*  ------------- FUNZIONI FINALIZE ------- */
void hybrid_SHA256_finalize(SoftSHA256Context* ctx, uint8* out) {
    // 1. Aggiungiamo il bit di padding '1' (0x80)
    uint64 total_bits = ctx->count + (ctx->buflen * 8);
    uint8 pad = 0x80;
    hybrid_SHA256_update(ctx, &pad, 1);

    // 2. Padding con zeri fino a 56 byte (lasciando 8 byte per la lunghezza)
    uint8 zero = 0x00;
    while (ctx->buflen != 56) {
        hybrid_SHA256_update(ctx, &zero, 1);
    }

    // 3. Aggiungiamo la lunghezza del messaggio in Big-Endian
    uint64 be_bits = B_HOST_TO_BENDIAN_INT64(total_bits);
    hybrid_SHA256_update(ctx, (uint8*)&be_bits, 8);

    // 4. Copiamo il digest finale (gestendo il troncamento per SHA-224)
    for (size_t i = 0; i < (ctx->outlen / 4); i++) {
        uint32 v = B_HOST_TO_BENDIAN_INT32(ctx->state[i]);
        memcpy(out + (i * 4), &v, 4);
    }
}
