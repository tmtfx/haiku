/*
 * Hybrid SIMD/AVX implementation for SHA
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * Optimized for Haiku Kernel. MIT License.
 */
#include "soft_sha.h"
#include "hybrid_sha_opt.h"
#include "BCryptoCPU.h"
#include "BCryptoCore.h"
#include <smmintrin.h> // SSE4.1
#include <immintrin.h> // AVX2
#include <string.h>
#include <ByteOrder.h>
#include <debug.h>
#include <KernelExport.h>


#include <tmmintrin.h> // SSSE3 per _mm_shuffle_epi8
#include <smmintrin.h> // SSE4.1


//static spinlock sSHALock = B_SPINLOCK_INITIALIZER;
static uint32 sCaps = 0;
//static uint32 sW_SSE[64] __attribute__((aligned(16)));
//static uint32 sW_AVX2[64] __attribute__((aligned(32)));
//static fpu_state_t global_fpu_save __attribute__((aligned(16)));

/* ----- SHA1 ---- */

// Macro per SHA-1
#define ROL32(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

#define F1(b,c,d) ((b & c) | (~b & d))
#define F2(b,c,d) (b ^ c ^ d)
#define F3(b,c,d) ((b & c) | (b & d) | (c & d))
#define F4(b,c,d) (b ^ c ^ d)

__attribute__((target("sse4.1")))
void hybrid_sha1_transform_sse(SoftSHA1Context* ctx, const uint8* data) {
    uint32 W[80] __attribute__((aligned(16)));
    uint32 a, b, c, d, e;

    const __m128i mask = _mm_set_epi8(12,13,14,15, 8,9,10,11, 4,5,6,7, 0,1,2,3);

    // 1. Caricamento prime 16 parole con SSE
    for (int i = 0; i < 4; i++) {
        __m128i raw = _mm_loadu_si128((const __m128i*)(data + i * 16));
        _mm_store_si128((__m128i*)&W[i * 4], _mm_shuffle_epi8(raw, mask));
    }

    // 2. Scheduling SSE (W[i] = ROL32(W[i-3] ^ W[i-8] ^ W[i-14] ^ W[i-16], 1))
    for (int i = 16; i < 80; i += 4) {
        __m128i v_m3  = _mm_loadu_si128((__m128i*)&W[i-3]);
        __m128i v_m8  = _mm_loadu_si128((__m128i*)&W[i-8]);
        __m128i v_m14 = _mm_loadu_si128((__m128i*)&W[i-14]);
        __m128i v_m16 = _mm_load_si128((__m128i*)&W[i-16]);

        __m128i res = _mm_xor_si128(_mm_xor_si128(v_m3, v_m8), _mm_xor_si128(v_m14, v_m16));
        res = _mm_or_si128(_mm_slli_epi32(res, 1), _mm_srli_epi32(res, 31));
        _mm_store_si128((__m128i*)&W[i], res);
    }

    // 3. Round di compressione
    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3]; e = ctx->state[4];

    // Round 0-19
    for (int i = 0; i < 20; i++) {
        uint32 temp = ROL32(a, 5) + F1(b, c, d) + e + 0x5A827999 + W[i];
        e = d; d = c; c = ROL32(b, 30); b = a; a = temp;
    }
    // Round 20-39
    for (int i = 20; i < 40; i++) {
        uint32 temp = ROL32(a, 5) + F2(b, c, d) + e + 0x6ED9EBA1 + W[i];
        e = d; d = c; c = ROL32(b, 30); b = a; a = temp;
    }
    // Round 40-59
    for (int i = 40; i < 60; i++) {
        uint32 temp = ROL32(a, 5) + F3(b, c, d) + e + 0x8F1BBCDC + W[i];
        e = d; d = c; c = ROL32(b, 30); b = a; a = temp;
    }
    // Round 60-79
    for (int i = 60; i < 80; i++) {
        uint32 temp = ROL32(a, 5) + F4(b, c, d) + e + 0xCA62C1D6 + W[i];
        e = d; d = c; c = ROL32(b, 30); b = a; a = temp;
    }

    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; 
    ctx->state[3] += d; ctx->state[4] += e;
}

__attribute__((target("avx2")))
void hybrid_sha1_transform_avx2(SoftSHA1Context* ctx, const uint8* data) {
    uint32 W[80] __attribute__((aligned(32)));
    uint32 a, b, c, d, e;

    // Maschera 256-bit per byte-swap (ripetuta sui due lane)
    const __m256i mask = _mm256_set_epi8(
        12,13,14,15, 8,9,10,11, 4,5,6,7, 0,1,2,3,
        12,13,14,15, 8,9,10,11, 4,5,6,7, 0,1,2,3
    );

    // 1. Caricamento prime 16 parole (2 registri da 256 bit)
    for (int i = 0; i < 2; i++) {
        __m256i raw = _mm256_loadu_si256((const __m256i*)(data + i * 32));
        _mm256_store_si256((__m256i*)&W[i * 8], _mm256_shuffle_epi8(raw, mask));
    }

    // 2. Scheduling AVX2 (Calcoliamo 8 parole alla volta)
    // W[i] = ROL32(W[i-3] ^ W[i-8] ^ W[i-14] ^ W[i-16], 1)
    for (int i = 16; i < 80; i += 8) {
        __m256i v_m3  = _mm256_loadu_si256((__m256i*)&W[i-3]);
        __m256i v_m8  = _mm256_loadu_si256((__m256i*)&W[i-8]);
        __m256i v_m14 = _mm256_loadu_si256((__m256i*)&W[i-14]);
        __m256i v_m16 = _mm256_load_si256((__m256i*)&W[i-16]);

        __m256i res = _mm256_xor_si256(_mm256_xor_si256(v_m3, v_m8), 
                                       _mm256_xor_si256(v_m14, v_m16));
        
        // Rotazione SIMD (ROL32)
        res = _mm256_or_si256(_mm256_slli_epi32(res, 1), _mm256_srli_epi32(res, 31));
        
        _mm256_store_si256((__m256i*)&W[i], res);
    }

    // 3. Round di compressione (Scalari)
    // Usiamo lo stesso codice della versione SSE, 
    // ma ora W è stato pre-calcolato molto più velocemente.
    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3]; e = ctx->state[4];

    for (int i = 0; i < 20; i++) {
        uint32 t = ROL32(a, 5) + F1(b, c, d) + e + 0x5A827999 + W[i];
        e = d; d = c; c = ROL32(b, 30); b = a; a = t;
    }
    for (int i = 20; i < 40; i++) {
        uint32 t = ROL32(a, 5) + F2(b, c, d) + e + 0x6ED9EBA1 + W[i];
        e = d; d = c; c = ROL32(b, 30); b = a; a = t;
    }
    for (int i = 40; i < 60; i++) {
        uint32 t = ROL32(a, 5) + F3(b, c, d) + e + 0x8F1BBCDC + W[i];
        e = d; d = c; c = ROL32(b, 30); b = a; a = t;
    }
    for (int i = 60; i < 80; i++) {
        uint32 t = ROL32(a, 5) + F4(b, c, d) + e + 0xCA62C1D6 + W[i];
        e = d; d = c; c = ROL32(b, 30); b = a; a = t;
    }

    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; 
    ctx->state[3] += d; ctx->state[4] += e;
}

/* --- SHA 256 e affini ---- */

/* ---- IV vengono usati nella inizializzazione già fatta però in soft_sha.cpp
// IV per SHA-256
static const uint32 IV256[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
};

// IV per SHA-224
static const uint32 IV224[8] = {
    0xc1059ed8, 0x367cd507, 0x3070dd17, 0xf70e5939,
    0xffc00b31, 0x68581511, 0x64f98fa7, 0xbefa4fa4
};*/

/* -------- SCHEDULER SHA256 SCALARE ------ */
// Macro per i round scalari
#define ROR32(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

#define Ch(x,y,z)  (((x) & (y)) ^ (~(x) & (z)))
#define Maj(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define S0(x)      (ROR32(x, 2) ^ ROR32(x, 13) ^ ROR32(x, 22))
#define S1(x)      (ROR32(x, 6) ^ ROR32(x, 11) ^ ROR32(x, 25))
#define s0(x)      (ROR32(x, 7) ^ ROR32(x, 18) ^ ((x) >> 3))
#define s1(x)      (ROR32(x, 17) ^ ROR32(x, 19) ^ ((x) >> 10))

__attribute__((target("sse4.1")))
void hybrid_sha256_transform_sse(SoftSHA256Context* ctx, const uint8* data) {
    uint32 W[64] __attribute__((aligned(16)));
    //uint32* W = sW_SSE;
    uint32 a, b, c, d, e, f, g, h;

    // 1. Caricamento scalare con byte swap
    for (int i = 0; i < 16; i++) {
    	uint32 tmp;
        memcpy(&tmp, data + (i * 4), 4);
        W[i] = B_BENDIAN_TO_HOST_INT32(tmp);
        //W[i] = B_BENDIAN_TO_HOST_INT32(((uint32*)data)[i]);
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

/*- --------------------- SCHEDULER SHA256 FULL SSE --------------------------- -*/
// sigma0(x) = ROR(x, 7) ^ ROR(x, 18) ^ (x >> 3)
// Versione vettoriale SSE
static inline __m128i SCHED_s0(__m128i x) {
    __m128i t1 = _mm_or_si128(_mm_srli_epi32(x, 7),  _mm_slli_epi32(x, 32 - 7));
    __m128i t2 = _mm_or_si128(_mm_srli_epi32(x, 18), _mm_slli_epi32(x, 32 - 18));
    __m128i t3 = _mm_srli_epi32(x, 3);
    return _mm_xor_si128(_mm_xor_si128(t1, t2), t3);
}

// sigma1(x) = ROR(x, 17) ^ ROR(x, 19) ^ (x >> 10)
// Versione vettoriale SSE
static inline __m128i SCHED_s1(__m128i x) {
    __m128i t1 = _mm_or_si128(_mm_srli_epi32(x, 17), _mm_slli_epi32(x, 32 - 17));
    __m128i t2 = _mm_or_si128(_mm_srli_epi32(x, 19), _mm_slli_epi32(x, 32 - 19));
    __m128i t3 = _mm_srli_epi32(x, 10);
    return _mm_xor_si128(_mm_xor_si128(t1, t2), t3);
}

__attribute__((target("sse4.1")))
void hybrid_sha256_transform_sse_full(SoftSHA256Context* ctx, const uint8* data) {
    static uint32 W[64] __attribute__((aligned(16)));
    //uint32* W = sW_SSE;
    uint32 a, b, c, d, e, f, g, h;

    const __m128i mask = _mm_set_epi8(12,13,14,15, 8,9,10,11, 4,5,6,7, 0,1,2,3);

    // 1. Caricamento prime 16 parole
    for (int i = 0; i < 4; i++) {
        __m128i raw = _mm_loadu_si128((const __m128i*)(data + i * 16));
        _mm_storeu_si128((__m128i*)&W[i * 4], _mm_shuffle_epi8(raw, mask));
    }

    // 2. Scheduling SSE (16-63)
    for (int i = 16; i < 64; i += 4) {
        __m128i v0 = _mm_loadu_si128((__m128i*)&W[i - 16]);
        __m128i v1 = _mm_loadu_si128((__m128i*)&W[i - 12]);
        __m128i v2 = _mm_loadu_si128((__m128i*)&W[i - 8]);
        __m128i v3 = _mm_loadu_si128((__m128i*)&W[i - 4]);

        // s0 = sigma0(W[i-15...i-12])
        __m128i w_i_15 = _mm_alignr_epi8(v1, v0, 4);
        __m128i s0_v = SCHED_s0(w_i_15);

        // w_i_7 = W[i-7...i-4]
        __m128i w_i_7 = _mm_alignr_epi8(v3, v2, 4);

        // Calcolo base: W[i-16] + sigma0(W[i-15]) + W[i-7]
        __m128i res = _mm_add_epi32(_mm_add_epi32(v0, s0_v), w_i_7);

        // --- GESTIONE RICORSIVA SIGMA1 ---
        // I primi due elementi di res (W_i, W_i+1) hanno bisogno di sigma1(W_i-2, W_i-1)
        __m128i s1_lo = SCHED_s1(_mm_alignr_epi8(res, v3, 8)); // Usa W[i-2], W[i-1] dal registro precedente
        // Azzeriamo la parte alta perché s1_lo è valido solo per i primi due
        s1_lo = _mm_and_si128(s1_lo, _mm_set_epi32(0, 0, -1, -1));
        res = _mm_add_epi32(res, s1_lo);

        // Ora che abbiamo calcolato W_i e W_i+1, possiamo calcolare W_i+2 e W_i+3
        __m128i s1_hi = SCHED_s1(_mm_alignr_epi8(res, res, 8)); // Usa i W[i], W[i+1] appena calcolati
        s1_hi = _mm_and_si128(s1_hi, _mm_set_epi32(-1, -1, 0, 0));
        res = _mm_add_epi32(res, s1_hi);

        _mm_storeu_si128((__m128i*)&W[i], res);
    }

    // 3. Round di compressione (Scalari)
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

/* --------------- SCHEDULER SHA256 AVX2 ------------------------*/
#define SIG0_256_AVX2(x) \
    _mm256_xor_si256(_mm256_xor_si256( \
        _mm256_or_si256(_mm256_srli_epi32(x, 7),  _mm256_slli_epi32(x, 25)), \
        _mm256_or_si256(_mm256_srli_epi32(x, 18), _mm256_slli_epi32(x, 14))), \
        _mm256_srli_epi32(x, 3))

#define SIG1_256_AVX2(x) \
    _mm256_xor_si256(_mm256_xor_si256( \
        _mm256_or_si256(_mm256_srli_epi32(x, 17), _mm256_slli_epi32(x, 15)), \
        _mm256_or_si256(_mm256_srli_epi32(x, 19), _mm256_slli_epi32(x, 13))), \
        _mm256_srli_epi32(x, 10))
/* nope, copilot gpt-5.2 dice che la logica dei W è sbagliata usata così
__attribute__((target("avx2")))
void hybrid_sha256_transform_avx2(SoftSHA256Context* ctx, const uint8* data) {
    // Allineamento a 32 byte per massime prestazioni AVX2
    //static uint32 W[64] __attribute__((aligned(32)));
    //uint32* W = sW_AVX2;
    uint32 W[64] __attribute__((aligned(32)));
    alignas(32) uint32 a, b, c, d, e, f, g, h;

    // Maschera per il byte-swap (Big Endian -> Host)
    const __m256i mask = _mm256_set_epi8(
        12,13,14,15, 8,9,10,11, 4,5,6,7, 0,1,2,3,
        12,13,14,15, 8,9,10,11, 4,5,6,7, 0,1,2,3
    );

    // 1. Caricamento prime 16 parole (0-15)
    for (int i = 0; i < 2; i++) {
        __m256i raw = _mm256_loadu_si256((const __m256i*)(data + i * 32));
        _mm256_storeu_si256((__m256i*)&W[i * 8], _mm256_shuffle_epi8(raw, mask));
    }

    // 2. Scheduling AVX2 (16-63)
    // Dividiamo il calcolo in due metà per rispettare la dipendenza W[i-2]
    for (int i = 16; i < 64; i += 8) {
        // --- PARTE A: Calcola W[i...i+3] ---
        __m128i v16_a = _mm_loadu_si128((__m128i*)&W[i - 16]);
        __m128i v15_a = _mm_loadu_si128((__m128i*)&W[i - 15]);
        __m128i v7_a  = _mm_loadu_si128((__m128i*)&W[i - 7]);
        __m128i v2_a  = _mm_loadu_si128((__m128i*)&W[i - 2]);

        // Usiamo i registri a 128-bit (XMM) per questa fase intermedia
        __m128i s0_a = _mm_xor_si128(_mm_xor_si128(
            _mm_or_si128(_mm_srli_epi32(v15_a, 7),  _mm_slli_epi32(v15_a, 25)),
            _mm_or_si128(_mm_srli_epi32(v15_a, 18), _mm_slli_epi32(v15_a, 14))),
            _mm_srli_epi32(v15_a, 3));

        __m128i s1_a = _mm_xor_si128(_mm_xor_si128(
            _mm_or_si128(_mm_srli_epi32(v2_a, 17), _mm_slli_epi32(v2_a, 15)),
            _mm_or_si128(_mm_srli_epi32(v2_a, 19), _mm_slli_epi32(v2_a, 13))),
            _mm_srli_epi32(v2_a, 10));

        __m128i res_a = _mm_add_epi32(_mm_add_epi32(v16_a, s0_a), _mm_add_epi32(v7_a, s1_a));
        _mm_storeu_si128((__m128i*)&W[i], res_a);

        // --- PARTE B: Calcola W[i+4...i+7] ---
        // Ora W[i] e W[i+1] sono pronti, quindi v2_b (W[i+2]) sarà corretto
        __m128i v16_b = _mm_loadu_si128((__m128i*)&W[i - 12]);
        __m128i v15_b = _mm_loadu_si128((__m128i*)&W[i - 11]);
        __m128i v7_b  = _mm_loadu_si128((__m128i*)&W[i - 3]);
        __m128i v2_b  = _mm_loadu_si128((__m128i*)&W[i + 2]); // Dipende da res_a

        __m128i s0_b = _mm_xor_si128(_mm_xor_si128(
            _mm_or_si128(_mm_srli_epi32(v15_b, 7),  _mm_slli_epi32(v15_b, 25)),
            _mm_or_si128(_mm_srli_epi32(v15_b, 18), _mm_slli_epi32(v15_b, 14))),
            _mm_srli_epi32(v15_b, 3));

        __m128i s1_b = _mm_xor_si128(_mm_xor_si128(
            _mm_or_si128(_mm_srli_epi32(v2_b, 17), _mm_slli_epi32(v2_b, 15)),
            _mm_or_si128(_mm_srli_epi32(v2_b, 19), _mm_slli_epi32(v2_b, 13))),
            _mm_srli_epi32(v2_b, 10));

        __m128i res_b = _mm_add_epi32(_mm_add_epi32(v16_b, s0_b), _mm_add_epi32(v7_b, s1_b));
        _mm_storeu_si128((__m128i*)&W[i + 4], res_b);
    }

    // 3. Round di compressione
    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];

    for (int i = 0; i < 64; i++) {
        uint32 t1 = h + S1(e) + Ch(e, f, g) + K256[i] + W[i];
        uint32 t2 = S0(a) + Maj(a, b, c);
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }

    // Aggiornamento stato finale
    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
    
    _mm256_zeroupper();
}*/
__attribute__((target("avx2")))
void hybrid_sha256_transform_avx2(SoftSHA256Context* ctx, const uint8* data)
{
    // W locale: niente static, niente race, deterministico
    uint32 W[64] __attribute__((aligned(32)));
    uint32 a, b, c, d, e, f, g, h;

    // Maschera per byteswap su 32-bit words (ripetuta sui due lane 128-bit)
    const __m256i bswap_mask = _mm256_set_epi8(
        12,13,14,15,  8, 9,10,11,  4, 5, 6, 7,  0, 1, 2, 3,
        12,13,14,15,  8, 9,10,11,  4, 5, 6, 7,  0, 1, 2, 3
    );

    // 1) Load + byte-swap di W[0..15] usando AVX2 (64 byte totali)
    // Ogni load prende 32 byte = 8 word.
    for (int i = 0; i < 2; i++) {
        __m256i raw = _mm256_loadu_si256((const __m256i*)(data + i * 32));
        __m256i swp = _mm256_shuffle_epi8(raw, bswap_mask);
        _mm256_store_si256((__m256i*)&W[i * 8], swp);
    }

    // 2) Schedule scalare W[16..63] (corretto per definizione)
    for (int i = 16; i < 64; i++) {
        W[i] = s1(W[i - 2]) + W[i - 7] + s0(W[i - 15]) + W[i - 16];
    }

    // 3) Round di compressione scalari (come la tua versione funzionante)
    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];

    for (int i = 0; i < 64; i++) {
        uint32 t1 = h + S1(e) + Ch(e, f, g) + K256[i] + W[i];
        uint32 t2 = S0(a) + Maj(a, b, c);
        h = g; g = f; f = e;
        e = d + t1;
        d = c; c = b; b = a;
        a = t1 + t2;
    }

    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;

    // Buona pratica quando usi AVX in codice che può poi chiamare SSE “legacy”
    _mm256_zeroupper();
}

/* --- IV vengono usati nell'inizializzazione gia fatta in soft_sha.cpp ----- 
// IV per SHA-512
static const uint64_t IV512[8] = {
    0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL,
    0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
    0x510e527fad6849adbULL, 0x9b05688c2b3e6c1fULL,
    0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL
};

// IV per SHA-384
static const uint64_t IV384[8] = {
    0xcbbb9d5dc1059ed8ULL, 0x629a292a367cd507ULL,
    0x9159015a3070dd17ULL, 0x152fecd8f70e5939ULL,
    0x67332667ffc00b31ULL, 0x8eb44a8768581511ULL,
    0xdb0c2e0d64f98fa7ULL, 0x47b5481dbefa4fa4ULL
};*/
/* ------------- SCHEDULER SHA512 AVX2 --------------------- */
// --- Macro Scalari (per round di compressione) ---
#define ROR64(x, n) (((x) >> (n)) | ((x) << (64 - (n))))

#define SHA512_S0(x) (ROR64(x, 28) ^ ROR64(x, 34) ^ ROR64(x, 39))
#define SHA512_S1(x) (ROR64(x, 14) ^ ROR64(x, 18) ^ ROR64(x, 41))
#define SHA512_s0(x) (ROR64(x, 1)  ^ ROR64(x, 8)  ^ ((x) >> 7))
#define SHA512_s1(x) (ROR64(x, 19) ^ ROR64(x, 61) ^ ((x) >> 6))

// --- Funzioni Helper AVX2 (per scheduling) ---
// Emulazione ROR64 su registri a 256-bit (4 parole x 64-bit)
// Usiamo un nome personalizzato per evitare conflitti con le intrinseche di sistema
__attribute__((target("avx2")))
static inline __m256i avx2_ror64(__m256i x, int n) {
    return _mm256_or_si256(
        _mm256_srli_epi64(x, n), 
        _mm256_slli_epi64(x, 64 - n)
    );
}

__attribute__((target("avx2")))
static inline __m256i sha512_sched_s0_avx2(__m256i x) {
    // sigma0(x) = ROR(x, 1) ^ ROR(x, 8) ^ (x >> 7)
    return _mm256_xor_si256(
        _mm256_xor_si256(avx2_ror64(x, 1), avx2_ror64(x, 8)), 
        _mm256_srli_epi64(x, 7)
    );
}

__attribute__((target("avx2")))
static inline __m256i sha512_sched_s1_avx2(__m256i x) {
    // sigma1(x) = ROR(x, 19) ^ ROR(x, 61) ^ (x >> 6)
    return _mm256_xor_si256(
        _mm256_xor_si256(avx2_ror64(x, 19), avx2_ror64(x, 61)), 
        _mm256_srli_epi64(x, 6)
    );
}

__attribute__((target("avx2")))
void hybrid_sha512_transform_avx2(SoftSHA512Context* ctx, const uint8* data) {
    uint64_t W[80] __attribute__((aligned(32)));
    uint64_t a, b, c, d, e, f, g, h;

    const __m256i mask = _mm256_set_epi8(
        8,9,10,11,12,13,14,15, 0,1,2,3,4,5,6,7,
        8,9,10,11,12,13,14,15, 0,1,2,3,4,5,6,7
    );

    // 1. Caricamento e Byte-swap (16 parole, 128 byte totali)
    for (int i = 0; i < 4; i++) {
        __m256i raw = _mm256_loadu_si256((const __m256i*)(data + i * 32));
        _mm256_store_si256((__m256i*)&W[i * 4], _mm256_shuffle_epi8(raw, mask));
    }

    // 2. Scheduling AVX2 (Processa 4 parole alla volta)
    for (int i = 16; i < 80; i += 4) {
        __m256i v_m16 = _mm256_load_si256((__m256i*)&W[i-16]);
        __m256i v_m15 = _mm256_loadu_si256((__m256i*)&W[i-15]);
        __m256i v_m7  = _mm256_loadu_si256((__m256i*)&W[i-7]);
        __m256i v_m2  = _mm256_loadu_si256((__m256i*)&W[i-2]);

        __m256i s0 = sha512_sched_s0_avx2(v_m15);
        __m256i s1 = sha512_sched_s1_avx2(v_m2);

        __m256i res = _mm256_add_epi64(_mm256_add_epi64(v_m16, s0),
                                       _mm256_add_epi64(v_m7, s1));
        _mm256_store_si256((__m256i*)&W[i], res);
    }

    // 3. Round di compressione (Scalari)
    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];

    for (int i = 0; i < 80; i++) {
        uint64_t t1 = h + SHA512_S1(e) + ((e & f) ^ (~e & g)) + K512[i] + W[i];
        uint64_t t2 = SHA512_S0(a) + ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }

    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

__attribute__((target("sse4.1")))
void hybrid_sha512_transform_sse(SoftSHA512Context* ctx, const uint8* data) {
    uint64_t W[80] __attribute__((aligned(16)));
    uint64_t a, b, c, d, e, f, g, h;

    const __m128i mask = _mm_set_epi8(8,9,10,11,12,13,14,15, 0,1,2,3,4,5,6,7);

    // 1. Caricamento SSE (8 registri da 16 byte)
    for (int i = 0; i < 8; i++) {
        __m128i raw = _mm_loadu_si128((const __m128i*)(data + i * 16));
        _mm_store_si128((__m128i*)&W[i * 2], _mm_shuffle_epi8(raw, mask));
    }

    // 2. Scheduling SSE (2 parole alla volta)
    for (int i = 16; i < 80; i += 2) {
        __m128i v_m16 = _mm_load_si128((__m128i*)&W[i-16]);
        __m128i v_m15 = _mm_loadu_si128((__m128i*)&W[i-15]);
        __m128i v_m7  = _mm_loadu_si128((__m128i*)&W[i-7]);
        __m128i v_m2  = _mm_loadu_si128((__m128i*)&W[i-2]);

        // Sigma emulato su __m128i
        auto s_ror = [](__m128i x, int n) { 
            return _mm_or_si128(_mm_srli_epi64(x, n), _mm_slli_epi64(x, 64 - n)); 
        };
        
        __m128i s0 = _mm_xor_si128(_mm_xor_si128(s_ror(v_m15, 1), s_ror(v_m15, 8)), _mm_srli_epi64(v_m15, 7));
        __m128i s1 = _mm_xor_si128(_mm_xor_si128(s_ror(v_m2, 19), s_ror(v_m2, 61)), _mm_srli_epi64(v_m2, 6));

        __m128i res = _mm_add_epi64(_mm_add_epi64(v_m16, s0), _mm_add_epi64(v_m7, s1));
        _mm_store_si128((__m128i*)&W[i], res);
    }

    // 3. Round di compressione (Scalari) - Uguali alla versione AVX2
    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];

    for (int i = 0; i < 80; i++) {
        uint64_t t1 = h + SHA512_S1(e) + ((e & f) ^ (~e & g)) + K512[i] + W[i];
        uint64_t t2 = SHA512_S0(a) + ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }

    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

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
void hybrid_SHA1_update(SoftSHA1Context* ctx, const uint8* in, size_t inLen) {
	sCaps = BGetStoredCryptoCapabilities();
    while (inLen > 0) {
        size_t left = 64 - ctx->buflen;
        size_t fill = (inLen > left) ? left : inLen;
        
        memcpy(ctx->buffer + ctx->buflen, in, fill);
        ctx->buflen += fill;
        in += fill;
        inLen -= fill;

        if (ctx->buflen == 64) {
        	//fpu_state_t fpu_save __attribute__((aligned(16)));
        	//BCryptoFPUContext fpu_save;
            
            //_fxsave(&fpu_save);
            //bcrypto_save_regs(&fpu_save);
            
            //cpu_status cpu = disable_interrupts();
            //bcrypto_save_regs(&ctx->fpu_save);
            cpu_status cpu_state = disable_interrupts();
            bcrypto_save_regs(&ctx->fpu_save);
                   
            ctx->count += 512;
            //if (gHasAVX2) 
            if (sCaps & B_CRYPTO_HW_AVX2)
                hybrid_sha1_transform_avx2(ctx, ctx->buffer);
            else 
                hybrid_sha1_transform_sse(ctx, ctx->buffer);
            
            //_fxrstor(&fpu_save);
            //bcrypto_restore_regs(&fpu_save);
            
            //bcrypto_restore_regs(&ctx->fpu_save);
            //restore_interrupts(cpu);
            bcrypto_restore_regs(&ctx->fpu_save);
            restore_interrupts(cpu_state);
            
            ctx->buflen = 0;
        }
    }
}

void hybrid_SHA256_update(SoftSHA256Context* ctx, const uint8* in, size_t inLen) {
	sCaps = BGetStoredCryptoCapabilities();
    while (inLen > 0) {
        size_t left = 64 - ctx->buflen; // SHA256 block size = 64
        size_t fill = (inLen > left) ? left : inLen;
        
        memcpy(ctx->buffer + ctx->buflen, in, fill);
        ctx->buflen += fill;
        in += fill;
        inLen -= fill;

        if (ctx->buflen == 64) {
        	// --- PROTEZIONE KERNEL ---
        	//fpu_state_t fpu_save __attribute__((aligned(16)));
        	//BCryptoFPUContext fpu_save;

            //acquire_spinlock(&sSHALock);
            //_fxsave(&fpu_save);
            //_fxsave(&global_fpu_save);
            //bcrypto_save_regs(&fpu_save);
                    	
            //cpu_status cpu = disable_interrupts();
            //bcrypto_save_regs(&ctx->fpu_save);
            //B_PREPARE_CPU_STATE();
            cpu_status cpu_state = disable_interrupts();
            bcrypto_save_regs(&ctx->fpu_save);
            
            //ctx->count += 512; // conta i bit (64 bytes * 8)
            
            //if (gHasAVX2)
            if (sCaps & B_CRYPTO_HW_AVX2)
                hybrid_sha256_transform_avx2(ctx, ctx->buffer);
            else 
                //hybrid_sha256_transform_sse_full(ctx, ctx->buffer);
                hybrid_sha256_transform_sse(ctx, ctx->buffer);
            
            //_fxrstor(&fpu_save);
            //_fxrstor(&global_fpu_save);
            //bcrypto_restore_regs(&fpu_save);
            
            //release_spinlock(&sSHALock);
            
            //bcrypto_restore_regs(&ctx->fpu_save);
            //restore_interrupts(cpu);
            
            bcrypto_restore_regs(&ctx->fpu_save);
            restore_interrupts(cpu_state);
            
            ctx->count += 512;
            ctx->buflen = 0;
        }
    }
}

void hybrid_SHA512_update(SoftSHA512Context* ctx, const uint8* in, size_t inLen) {
	sCaps = BGetStoredCryptoCapabilities();
    while (inLen > 0) {
        size_t left = 128 - ctx->buflen; // SHA-512 usa blocchi da 128 byte
        size_t fill = (inLen > left) ? left : inLen;
        
        memcpy(ctx->buffer + ctx->buflen, in, fill);
        ctx->buflen += fill;
        in += fill;
        inLen -= fill;

        if (ctx->buflen == 128) {
        	//fpu_state_t fpu_save __attribute__((aligned(32)));
        	//BCryptoFPUContext fpu_save;
            
            //_fxsave(&fpu_save);
            //bcrypto_save_regs(&fpu_save);
            //cpu_status cpu = disable_interrupts();
            //bcrypto_save_regs(&ctx->fpu_save);
            cpu_status cpu_state = disable_interrupts();
            bcrypto_save_regs(&ctx->fpu_save);
            // Aggiorniamo il contatore a 128 bit (count[0] low, count[1] high)
            uint64_t old_low = ctx->count[0];
            ctx->count[0] += 1024; // 128 bytes * 8 bits
            if (ctx->count[0] < old_low) ctx->count[1]++;

            //if (gHasAVX2) 
            if (sCaps & B_CRYPTO_HW_AVX2)
                hybrid_sha512_transform_avx2(ctx, ctx->buffer);
            else 
                hybrid_sha512_transform_sse(ctx, ctx->buffer);
            
            //_fxrstor(&fpu_save);
            //bcrypto_restore_regs(&fpu_save);
            //bcrypto_restore_regs(&ctx->fpu_save);
            //restore_interrupts(cpu);
            bcrypto_restore_regs(&ctx->fpu_save);
            restore_interrupts(cpu_state);
            
            ctx->buflen = 0;
        }
    }
}

/*  ------------- FUNZIONI FINALIZE ------- */
void hybrid_SHA1_finalize(SoftSHA1Context* ctx, uint8* out) {
    uint64 total_bits = ctx->count + (ctx->buflen * 8);
    uint8 pad = 0x80;
    hybrid_SHA1_update(ctx, &pad, 1);

    uint8 zero = 0x00;
    while (ctx->buflen != 56) {
        hybrid_SHA1_update(ctx, &zero, 1);
    }

    uint64 be_bits = B_HOST_TO_BENDIAN_INT64(total_bits);
    hybrid_SHA1_update(ctx, (uint8*)&be_bits, 8);

    // SHA-1 produce 160 bit (20 byte = 5 parole da 32 bit)
    for (int i = 0; i < 5; i++) {
        uint32 v = B_HOST_TO_BENDIAN_INT32(ctx->state[i]);
        memcpy(out + (i * 4), &v, 4);
    }
}

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

void hybrid_SHA224_finalize(SoftSHA256Context* ctx, uint8* out) {
    hybrid_SHA256_finalize(ctx, out);
}

void hybrid_SHA512_finalize(SoftSHA512Context* ctx, uint8* out) {
    // Salviamo la lunghezza totale in bit PRIMA di iniziare il padding
    // count[0] è la parte bassa (bit), count[1] è la parte alta.
    uint64_t low_bits = ctx->count[0] + (ctx->buflen * 8);
    uint64_t high_bits = ctx->count[1];
    if (low_bits < ctx->count[0]) high_bits++; // Carry se superiamo i 64 bit

    // 1. Padding '1'
    uint8 pad = 0x80;
    hybrid_SHA512_update(ctx, &pad, 1);

    // 2. Padding '0' fino a 112 byte
    uint8 zero = 0x00;
    while (ctx->buflen != 112) {
        hybrid_SHA512_update(ctx, &zero, 1);
    }

    // 3. Lunghezza in bit (Big-Endian) - 128 bit totali
    uint64_t hi_be = B_HOST_TO_BENDIAN_INT64(high_bits);
    uint64_t lo_be = B_HOST_TO_BENDIAN_INT64(low_bits);
    
    // Lo standard vuole prima la parte alta, poi la bassa
    hybrid_SHA512_update(ctx, (uint8*)&hi_be, 8);
    hybrid_SHA512_update(ctx, (uint8*)&lo_be, 8);

    // 4. Output basato su outlen (48 per SHA-384, 64 per SHA-512)
    for (size_t i = 0; i < (ctx->outlen / 8); i++) {
        uint64_t v = B_HOST_TO_BENDIAN_INT64(ctx->state[i]);
        memcpy(out + (i * 8), &v, 8);
    }
}

void hybrid_SHA384_finalize(SoftSHA512Context* ctx, uint8* out) {
    hybrid_SHA512_finalize(ctx, out);
}

