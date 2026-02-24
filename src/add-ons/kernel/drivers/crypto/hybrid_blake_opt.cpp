/*
 * Hybrid SIMD/AVX implementation for BLAKE2s and BLAKE2b
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

// --- TABELLA SIGMA (Permutazioni) ---
static const uint8 sigma[12][16] = {
    {  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15 },
    { 14, 10,  4,  8,  9, 15, 13,  6,  1, 12,  0,  2, 11,  7,  5,  3 },
    { 11,  8, 12,  0,  5,  2, 15, 13, 10, 14,  3,  6,  7,  1,  9,  4 },
    {  7,  9,  3,  1, 13, 12, 11, 14,  2,  6,  5, 10,  4,  0, 15,  8 },
    {  9,  0,  5,  7,  2,  4, 10, 15, 14,  1, 11, 12,  6,  8,  3, 13 },
    {  2, 12,  6, 10,  0, 11,  8,  3,  4, 13,  7,  5, 15, 14,  1,  9 },
    { 12,  5,  1, 15, 14, 13,  4, 10,  0,  7,  6,  3,  9,  2,  8, 11 },
    { 13, 11,  7, 14, 12,  1,  3,  9,  5,  0, 15,  4,  8,  6,  2, 10 },
    {  6, 15, 14,  9, 11,  3,  0,  8, 12,  2, 13,  7,  1,  4, 10,  5 },
    { 10,  2,  8,  4,  7,  6,  1,  5, 15, 11,  9, 14,  3, 12, 13,  0 },
    {  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15 }, // Per B (12 round)
    { 14, 10,  4,  8,  9, 15, 13,  6,  1, 12,  0,  2, 11,  7,  5,  3 }  // Per B (12 round)
};

// --- LOGICA BLAKE2s (32-bit, SSE4.1) ---
#define ROT_S(v, n) ( \
    (n == 16) ? _mm_shuffle_epi8(v, _mm_set_epi8(13,12,15,14, 9,8,11,10, 5,4,7,6, 1,0,3,2)) : \
    (n == 8)  ? _mm_shuffle_epi8(v, _mm_set_epi8(12,15,14,13, 8,11,10,9, 4,7,6,5, 0,3,2,1)) : \
    _mm_or_si128(_mm_srli_epi32(v, n), _mm_slli_epi32(v, 32 - n)) \
)

#define G_S(v0, v1, v2, v3, m0, m1) \
    v0 = _mm_add_epi32(_mm_add_epi32(v0, v1), m0); \
    v3 = _mm_xor_si128(v3, v0); v3 = ROT_S(v3, 16); \
    v2 = _mm_add_epi32(v2, v3); \
    v1 = _mm_xor_si128(v1, v2); v1 = ROT_S(v1, 12); \
    v0 = _mm_add_epi32(_mm_add_epi32(v0, v1), m1); \
    v3 = _mm_xor_si128(v3, v0); v3 = ROT_S(v3, 8); \
    v2 = _mm_add_epi32(v2, v3); \
    v1 = _mm_xor_si128(v1, v2); v1 = ROT_S(v1, 7);

__attribute__((target("sse4.1")))
static void blake2s_compress_sse(SoftBlake2sContext* ctx, const uint8 block[64]) {

    const uint32* m = (const uint32*)block;
    __m128i v0 = _mm_loadu_si128((__m128i*)&ctx->h[0]);
    __m128i v1 = _mm_loadu_si128((__m128i*)&ctx->h[4]);
    __m128i v2 = _mm_setr_epi32(0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A);
    __m128i v3 = _mm_setr_epi32(0x510E527F ^ ctx->t[0], 0x9B05688C ^ ctx->t[1], 
                                0x1F83D9AB ^ ctx->f[0], // Se f[0] è 0xFFFFFFFF, lo XOR inverte tutto correttamente
                                0x5BE0CD19 ^ ctx->f[1]);

    for(int r=0; r<10; r++) {
        // Round parte 1: Colonne
        __m128i m0 = _mm_setr_epi32(m[sigma[r][0]], m[sigma[r][2]], m[sigma[r][4]], m[sigma[r][6]]);
        __m128i m1 = _mm_setr_epi32(m[sigma[r][1]], m[sigma[r][3]], m[sigma[r][5]], m[sigma[r][7]]);
        G_S(v0, v1, v2, v3, m0, m1);
        
        // Diagonali
        v1 = _mm_shuffle_epi32(v1, _MM_SHUFFLE(0,3,2,1));
        v2 = _mm_shuffle_epi32(v2, _MM_SHUFFLE(1,0,3,2));
        v3 = _mm_shuffle_epi32(v3, _MM_SHUFFLE(2,1,0,3));
        
        __m128i m2 = _mm_setr_epi32(m[sigma[r][8]], m[sigma[r][10]], m[sigma[r][12]], m[sigma[r][14]]);
        __m128i m3 = _mm_setr_epi32(m[sigma[r][9]], m[sigma[r][11]], m[sigma[r][13]], m[sigma[r][15]]);
        G_S(v0, v1, v2, v3, m2, m3);
        
        v1 = _mm_shuffle_epi32(v1, _MM_SHUFFLE(2,1,0,3));
        v2 = _mm_shuffle_epi32(v2, _MM_SHUFFLE(1,0,3,2));
        v3 = _mm_shuffle_epi32(v3, _MM_SHUFFLE(0,3,2,1));
    }

    __m128i h0 = _mm_loadu_si128((__m128i*)&ctx->h[0]);
    __m128i h1 = _mm_loadu_si128((__m128i*)&ctx->h[4]);
    _mm_storeu_si128((__m128i*)&ctx->h[0], _mm_xor_si128(h0, _mm_xor_si128(v0, v2)));
    _mm_storeu_si128((__m128i*)&ctx->h[4], _mm_xor_si128(h1, _mm_xor_si128(v1, v3)));
    // soft_blake2s_compress(ctx, block); for test purposes
}

// --- BLAKE2b AVX2 ---
__attribute__((target("avx2")))
static void blake2b_compress_avx2(SoftBlake2bContext* ctx, const uint8 block[128]) {
    const uint64* m = (const uint64*)block;
    __m256i v0 = _mm256_loadu_si256((__m256i*)&ctx->h[0]);
    __m256i v1 = _mm256_loadu_si256((__m256i*)&ctx->h[4]);
    __m256i v2 = _mm256_setr_epi64x(0x6A09E667F3BCC908ULL, 0xBB67AE8584CAA73BULL, 0x3C6EF372FE94F82BULL, 0xA54FF53A5F1D36F1ULL);
    __m256i v3 = _mm256_setr_epi64x(0x510E527FADE682D1ULL ^ ctx->t[0], 0x9B05688C2B3E6C1FULL ^ ctx->t[1], 
                                    0x1F83D9ABFB41BD6BULL ^ ctx->f[0], 0x5BE0CD19137E2179ULL ^ ctx->f[1]);

    for(int r=0; r<12; r++) {
        __m256i m0 = _mm256_setr_epi64x(m[sigma[r][0]], m[sigma[r][2]], m[sigma[r][4]], m[sigma[r][6]]);
        __m256i m1 = _mm256_setr_epi64x(m[sigma[r][1]], m[sigma[r][3]], m[sigma[r][5]], m[sigma[r][7]]);

        // Round parte 1 (Colonne)
        v0 = _mm256_add_epi64(_mm256_add_epi64(v0, v1), m0); // Somma m0
        v3 = _mm256_xor_si256(v3, v0); 
        v3 = _mm256_or_si256(_mm256_srli_epi64(v3, 32), _mm256_slli_epi64(v3, 32));
        v2 = _mm256_add_epi64(v2, v3);
        v1 = _mm256_xor_si256(v1, v2); 
        v1 = _mm256_or_si256(_mm256_srli_epi64(v1, 24), _mm256_slli_epi64(v1, 40));
        
        // Aggiungiamo la somma del secondo set di messaggi qui!
        v0 = _mm256_add_epi64(v0, m1); 

        v1 = _mm256_permute4x64_epi64(v1, 0x39); 
        v2 = _mm256_permute4x64_epi64(v2, 0x4E); 
        v3 = _mm256_permute4x64_epi64(v3, 0x93);

        // Round parte 2 (Diagonali)
        __m256i m2 = _mm256_setr_epi64x(m[sigma[r][8]], m[sigma[r][10]], m[sigma[r][12]], m[sigma[r][14]]);
        __m256i m3 = _mm256_setr_epi64x(m[sigma[r][9]], m[sigma[r][11]], m[sigma[r][13]], m[sigma[r][15]]);
        
        v0 = _mm256_add_epi64(_mm256_add_epi64(v0, v1), m2); // Somma m2
        v3 = _mm256_xor_si256(v3, v0); 
        v3 = _mm256_or_si256(_mm256_srli_epi64(v3, 16), _mm256_slli_epi64(v3, 48));
        v2 = _mm256_add_epi64(v2, v3);
        v1 = _mm256_xor_si256(v1, v2); 
        v1 = _mm256_or_si256(_mm256_srli_epi64(v1, 63), _mm256_slli_epi64(v1, 1));
        
        // Aggiungiamo la somma del quarto set di messaggi qui!
        v0 = _mm256_add_epi64(v0, m3); 

        v1 = _mm256_permute4x64_epi64(v1, 0x93); 
        v2 = _mm256_permute4x64_epi64(v2, 0x4E); 
        v3 = _mm256_permute4x64_epi64(v3, 0x39);
    }
}

static const uint64 blake2b_iv[8] = {
	0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL,
	0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
	0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
	0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL
};
#define G_SSE41(va, vb, vc, vd, m_low, m_high) \
    do { \
        va = _mm_add_epi64(va, vb); \
        va = _mm_add_epi64(va, m_low); \
        vd = _mm_xor_si128(vd, va); \
        /* Rotazione 32: shuffle 32-bit (scambia hi/lo di ogni 64-bit) */ \
        vd = _mm_shuffle_epi32(vd, _MM_SHUFFLE(2, 3, 0, 1)); \
        vc = _mm_add_epi64(vc, vd); \
        vb = _mm_xor_si128(vb, vc); \
        /* Rotazione 24: shift 64-bit */ \
        vb = _mm_or_si128(_mm_srli_epi64(vb, 24), _mm_slli_epi64(vb, 40)); \
        va = _mm_add_epi64(va, vb); \
        va = _mm_add_epi64(va, m_high); \
        vd = _mm_xor_si128(vd, va); \
        /* Rotazione 16: shift 64-bit */ \
        vd = _mm_or_si128(_mm_srli_epi64(vd, 16), _mm_slli_epi64(vd, 48)); \
        vc = _mm_add_epi64(vc, vd); \
        vb = _mm_xor_si128(vb, vc); \
        /* Rotazione 63: shift 64-bit */ \
        vb = _mm_or_si128(_mm_srli_epi64(vb, 63), _mm_slli_epi64(vb, 1)); \
    } while (0)
//extern "C" void soft_blake2b_compress(SoftBlake2bContext* ctx, const uint8 block[128]);
// --- BLAKE2b SSE4.1 FALLBACK ---
__attribute__((target("ssse3,sse4.1")))
static void blake2b_compress_sse(SoftBlake2bContext* ctx, const uint8 block[128]) {
	if (ctx == NULL || block == NULL) {
    	dprintf("BLAKE2b Error: NULL pointer!\n");
    	return;
	}
	uint64 v[16];
    uint64 m[16];

    // Caricamento dati (manteniamo il tuo memcpy per sicurezza LE)
    for (int i = 0; i < 16; ++i)
        memcpy(&m[i], block + i * 8, 8);

    // Inizializzazione v (Identica alla tua soft_blake)
    for (int i = 0; i < 8; ++i) v[i] = ctx->h[i];
    for (int i = 0; i < 8; ++i) v[i + 8] = blake2b_iv[i];

    v[12] ^= ctx->t[0];
    v[13] ^= ctx->t[1];
    v[14] ^= ctx->f[0];
    v[15] ^= ctx->f[1];

    for (int r = 0; r < 12; r++) {
        // --- COLONNE (G0+G1 e G2+G3) ---
        __m128i v01 = _mm_loadu_si128((__m128i*)&v[0]);
        __m128i v23 = _mm_loadu_si128((__m128i*)&v[2]);
        __m128i v45 = _mm_loadu_si128((__m128i*)&v[4]);
        __m128i v67 = _mm_loadu_si128((__m128i*)&v[6]);
        __m128i v89 = _mm_loadu_si128((__m128i*)&v[8]);
        __m128i vAB = _mm_loadu_si128((__m128i*)&v[10]);
        __m128i vCD = _mm_loadu_si128((__m128i*)&v[12]);
        __m128i vEF = _mm_loadu_si128((__m128i*)&v[14]);

        // G0 e G1 in parallelo
        G_SSE41(v01, v45, v89, vCD, 
                _mm_set_epi64x(m[sigma[r][2]], m[sigma[r][0]]),   // m[2*0], m[2*1]
                _mm_set_epi64x(m[sigma[r][3]], m[sigma[r][1]]));  // m[2*0+1], m[2*1+1]
        
        // G2 e G3 in parallelo
        G_SSE41(v23, v67, vAB, vEF, 
                _mm_set_epi64x(m[sigma[r][6]], m[sigma[r][4]]), 
                _mm_set_epi64x(m[sigma[r][7]], m[sigma[r][5]]));

        // Store temporaneo per riorganizzare le diagonali
        _mm_storeu_si128((__m128i*)&v[0], v01);  _mm_storeu_si128((__m128i*)&v[2], v23);
        _mm_storeu_si128((__m128i*)&v[4], v45);  _mm_storeu_si128((__m128i*)&v[6], v67);
        _mm_storeu_si128((__m128i*)&v[8], v89);  _mm_storeu_si128((__m128i*)&v[10], vAB);
        _mm_storeu_si128((__m128i*)&v[12], vCD); _mm_storeu_si128((__m128i*)&v[14], vEF);

        // --- DIAGONALI (G4+G5 e G6+G7) ---
        // Caricamento manuale per rispettare gli indici: v0+v1, v5+v6, v10+v11, v15+v12 ecc.
        __m128i d4_v051015_a = _mm_set_epi64x(v[1],  v[0]);  // va per G4, G5
        __m128i d4_v051015_b = _mm_set_epi64x(v[6],  v[5]);  // vb
        __m128i d4_v051015_c = _mm_set_epi64x(v[11], v[10]); // vc
        __m128i d4_v051015_d = _mm_set_epi64x(v[12], v[15]); // vd

        G_SSE41(d4_v051015_a, d4_v051015_b, d4_v051015_c, d4_v051015_d,
                _mm_set_epi64x(m[sigma[r][10]], m[sigma[r][8]]),
                _mm_set_epi64x(m[sigma[r][11]], m[sigma[r][9]]));

        __m128i d6_v27813_a = _mm_set_epi64x(v[3],  v[2]);  // va per G6, G7
        __m128i d6_v27813_b = _mm_set_epi64x(v[4],  v[7]);  // vb
        __m128i d6_v27813_c = _mm_set_epi64x(v[9],  v[8]);  // vc
        __m128i d6_v27813_d = _mm_set_epi64x(v[14], v[13]); // vd

        G_SSE41(d6_v27813_a, d6_v27813_b, d6_v27813_c, d6_v27813_d,
                _mm_set_epi64x(m[sigma[r][14]], m[sigma[r][12]]),
                _mm_set_epi64x(m[sigma[r][15]], m[sigma[r][13]]));

        // Un-diagonalize (Scrittura finale del round)
        v[0] = _mm_extract_epi64(d4_v051015_a, 0); v[1] = _mm_extract_epi64(d4_v051015_a, 1);
        v[5] = _mm_extract_epi64(d4_v051015_b, 0); v[6] = _mm_extract_epi64(d4_v051015_b, 1);
        v[10]= _mm_extract_epi64(d4_v051015_c, 0); v[11]= _mm_extract_epi64(d4_v051015_c, 1);
        v[15]= _mm_extract_epi64(d4_v051015_d, 0); v[12]= _mm_extract_epi64(d4_v051015_d, 1);

        v[2] = _mm_extract_epi64(d6_v27813_a, 0);  v[3] = _mm_extract_epi64(d6_v27813_a, 1);
        v[7] = _mm_extract_epi64(d6_v27813_b, 0);  v[4] = _mm_extract_epi64(d6_v27813_b, 1);
        v[8] = _mm_extract_epi64(d6_v27813_c, 0);  v[9] = _mm_extract_epi64(d6_v27813_c, 1);
        v[13]= _mm_extract_epi64(d6_v27813_d, 0);  v[14]= _mm_extract_epi64(d6_v27813_d, 1);
    }

    for (int i = 0; i < 8; i++)
        ctx->h[i] ^= v[i] ^ v[i + 8];

}

// --- INTERFACCIA PUBBLICA ---

void hybrid_blake2b_init(SoftBlake2bContext* ctx, size_t outLen) { soft_blake2b_init(ctx, outLen); }

void hybrid_blake2b_update(SoftBlake2bContext* ctx, const uint8* in, size_t inLen) {
    while (inLen > 0) {
        size_t left = 128 - ctx->buflen;
        size_t fill = (inLen > left) ? left : inLen;
        memcpy(ctx->buf + ctx->buflen, in, fill);
        ctx->buflen += fill; in += fill; inLen -= fill;
        if (ctx->buflen == 128 && inLen > 0) {
            ctx->t[0] += 128; if (ctx->t[0] < 128) ctx->t[1]++;
            //if (sUseAVX2) blake2b_compress_avx2(ctx, ctx->buf);
            //else blake2b_compress_sse(ctx, ctx->buf);
            blake2b_compress_sse(ctx, ctx->buf); //ONLY For test purposes
            //soft_blake2b_compress(ctx, ctx->buf); //ONLY For test purposes
            ctx->buflen = 0;
        }
    }
}

void hybrid_blake2b_finalize(SoftBlake2bContext* ctx, uint8* out) {
    ctx->t[0] += ctx->buflen;
    if (ctx->t[0] < ctx->buflen) ctx->t[1]++;
    //ctx->f[0] = true;
    ctx->f[0] = 0xFFFFFFFFFFFFFFFF;
    memset(ctx->buf + ctx->buflen, 0, 128 - ctx->buflen);
    //if (sUseAVX2) blake2b_compress_avx2(ctx, ctx->buf);
    //else blake2b_compress_sse(ctx, ctx->buf); 
    blake2b_compress_sse(ctx, ctx->buf); //ONLY For test purposes
    //soft_blake2b_compress(ctx, ctx->buf); //ONLY For test purposes
    uint8* p = out;
    for (size_t i = 0; i < (ctx->outlen / 8); i++) {
        uint64_t v = B_HOST_TO_LENDIAN_INT64(ctx->h[i]);
        memcpy(p, &v, 8);
        p += 8;
    }
    
    // Gestione di eventuali byte rimanenti (se outlen non è multiplo di 8)
    size_t remainder = ctx->outlen % 8;
    if (remainder > 0) {
        uint64_t v = B_HOST_TO_LENDIAN_INT64(ctx->h[ctx->outlen / 8]);
        memcpy(p, &v, remainder);
    }
}

// --- FUNZIONI IBRIDE (Interfaccia) ---

void hybrid_blake2s_init(SoftBlake2sContext* ctx, size_t outLen) { soft_blake2s_init(ctx, outLen); }

void hybrid_blake2s_update(SoftBlake2sContext* ctx, const uint8* in, size_t inLen) {
    while (inLen > 0) {
        size_t left = 64 - ctx->buflen;
        size_t fill = (inLen > left) ? left : inLen;
        memcpy(ctx->buf + ctx->buflen, in, fill);
        ctx->buflen += fill; in += fill; inLen -= fill;
        if (ctx->buflen == 64 && inLen > 0) {
            ctx->t[0] += 64; if (ctx->t[0] < 64) ctx->t[1]++;
            blake2s_compress_sse(ctx, ctx->buf);
            ctx->buflen = 0;
        }
    }
}

void hybrid_blake2s_finalize(SoftBlake2sContext* ctx, uint8* out) {
    ctx->t[0] += ctx->buflen;
    if (ctx->t[0] < ctx->buflen) ctx->t[1]++;
    ctx->f[0] = 0xFFFFFFFF;
    memset(ctx->buf + ctx->buflen, 0, 64 - ctx->buflen);
    blake2s_compress_sse(ctx, ctx->buf);
    for (int i = 0; i < 8; i++) {
        ((uint32*)out)[i] = B_HOST_TO_LENDIAN_INT32(ctx->h[i]);
    }
}
