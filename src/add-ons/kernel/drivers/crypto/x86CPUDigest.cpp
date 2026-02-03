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

#pragma GCC target("sha,sse4.1")


static const uint32_t K256[64] alignas(16) = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,
    0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
    0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,
    0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,
    0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
    0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,
    0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,
    0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
    0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};
/*
static inline void
sha256_transform(__m128i& state0, __m128i& state1, const uint8_t* data)
{
    __m128i a = state0;
    __m128i b = state1;
    __m128i m0, m1, m2, m3;
    __m128i t;*/
    /*
    alignas(16) static const uint8_t kShuffleEndianBytes[16] = {
    12,13,14,15,
     8, 9,10,11,
     4, 5, 6, 7,
     0, 1, 2, 3
};*/


/*
alignas(16) static const uint8_t kShuffleEndianBytes[16] = {
    3, 2, 1, 0,  // Prima parola (indici 0-3 nel registro)
    7, 6, 5, 4,  // Seconda parola (indici 4-7)
    11, 10, 9, 8, // Terza parola (indici 8-11)
    15, 14, 13, 12 // Quarta parola (indici 12-15)
};
const __m128i SHUF_ENDIAN =
    _mm_load_si128((const __m128i*)kShuffleEndianBytes);

    m0 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(data +  0)), SHUF_ENDIAN);
    m1 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(data + 16)), SHUF_ENDIAN);
    m2 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(data + 32)), SHUF_ENDIAN);
    m3 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(data + 48)), SHUF_ENDIAN);

#define RND(m, k) \
    t = _mm_add_epi32(m, _mm_loadu_si128((const __m128i*)(k))); \
    b = _mm_sha256rnds2_epu32(b, a, t); \
    a = _mm_sha256rnds2_epu32(a, b, _mm_alignr_epi8(t, t, 8));

    // rounds 0–15
    RND(m0, K256 +  0)
    RND(m1, K256 +  4)
    RND(m2, K256 +  8)
    RND(m3, K256 + 12)*/

    // rounds 16–31
    /*m0 = _mm_sha256msg1_epu32(m0, m1);
    m1 = _mm_sha256msg1_epu32(m1, m2);
    m2 = _mm_sha256msg1_epu32(m2, m3);
    m3 = _mm_sha256msg1_epu32(m3, m0);

    m0 = _mm_add_epi32(m0, _mm_sha256msg2_epu32(m2, m3));
    m1 = _mm_add_epi32(m1, _mm_sha256msg2_epu32(m3, m0));
    m2 = _mm_add_epi32(m2, _mm_sha256msg2_epu32(m0, m1));
    m3 = _mm_add_epi32(m3, _mm_sha256msg2_epu32(m1, m2));

    RND(m0, K256 + 16)
    RND(m1, K256 + 20)
    RND(m2, K256 + 24)
    RND(m3, K256 + 28)

    // rounds 32–47
    m0 = _mm_sha256msg1_epu32(m0, m1);
    m1 = _mm_sha256msg1_epu32(m1, m2);
    m2 = _mm_sha256msg1_epu32(m2, m3);
    m3 = _mm_sha256msg1_epu32(m3, m0);

    m0 = _mm_add_epi32(m0, _mm_sha256msg2_epu32(m2, m3));
    m1 = _mm_add_epi32(m1, _mm_sha256msg2_epu32(m3, m0));
    m2 = _mm_add_epi32(m2, _mm_sha256msg2_epu32(m0, m1));
    m3 = _mm_add_epi32(m3, _mm_sha256msg2_epu32(m1, m2));

    RND(m0, K256 + 32)
    RND(m1, K256 + 36)
    RND(m2, K256 + 40)
    RND(m3, K256 + 44)

    // rounds 48–63  ⬅️ QUESTO MANCAVA
    m0 = _mm_sha256msg1_epu32(m0, m1);
    m1 = _mm_sha256msg1_epu32(m1, m2);
    m2 = _mm_sha256msg1_epu32(m2, m3);
    m3 = _mm_sha256msg1_epu32(m3, m0);

    m0 = _mm_add_epi32(m0, _mm_sha256msg2_epu32(m2, m3));
    m1 = _mm_add_epi32(m1, _mm_sha256msg2_epu32(m3, m0));
    m2 = _mm_add_epi32(m2, _mm_sha256msg2_epu32(m0, m1));
    m3 = _mm_add_epi32(m3, _mm_sha256msg2_epu32(m1, m2));

    RND(m0, K256 + 48)
    RND(m1, K256 + 52)
    RND(m2, K256 + 56)
    RND(m3, K256 + 60)*/
/*#define SHA256_RND(msg, k_ptr) \
    t = _mm_add_epi32(msg, _mm_loadu_si128((const __m128i*)(k_ptr))); \
    state1 = _mm_sha256rnds2_epu32(state1, state0, t); \
    state0 = _mm_sha256rnds2_epu32(state0, state1, _mm_alignr_epi8(t, t, 8));
    // Rounds 16-31
    m0 = _mm_sha256msg1_epu32(m0, m1);
    m0 = _mm_add_epi32(m0, _mm_sha256msg2_epu32(_mm_alignr_epi8(m3, m2, 4), m3));
    SHA256_RND(m0, K256 + 16)
    // ... ripeti lo schema per m1, m2, m3 ...
    m1 = _mm_sha256msg1_epu32(m1, m2);
    m1 = _mm_add_epi32(m1, _mm_sha256msg2_epu32(_mm_alignr_epi8(m0, m3, 4), m0));
    SHA256_RND(m1, K256 + 20)

    m2 = _mm_sha256msg1_epu32(m2, m3);
    m2 = _mm_add_epi32(m2, _mm_sha256msg2_epu32(_mm_alignr_epi8(m1, m0, 4), m1));
    SHA256_RND(m2, K256 + 24)

    m3 = _mm_sha256msg1_epu32(m3, m0);
    m3 = _mm_add_epi32(m3, _mm_sha256msg2_epu32(_mm_alignr_epi8(m2, m1, 4), m2));
    SHA256_RND(m3, K256 + 28)

    // Rounds 32-47
    m0 = _mm_sha256msg1_epu32(m0, m1);
    m0 = _mm_add_epi32(m0, _mm_sha256msg2_epu32(_mm_alignr_epi8(m3, m2, 4), m3));
    SHA256_RND(m0, K256 + 32)
    m1 = _mm_sha256msg1_epu32(m1, m2);
    m1 = _mm_add_epi32(m1, _mm_sha256msg2_epu32(_mm_alignr_epi8(m0, m3, 4), m0));
    SHA256_RND(m1, K256 + 36)
    m2 = _mm_sha256msg1_epu32(m2, m3);
    m2 = _mm_add_epi32(m2, _mm_sha256msg2_epu32(_mm_alignr_epi8(m1, m0, 4), m1));
    SHA256_RND(m2, K256 + 40)
    m3 = _mm_sha256msg1_epu32(m3, m0);
    m3 = _mm_add_epi32(m3, _mm_sha256msg2_epu32(_mm_alignr_epi8(m2, m1, 4), m2));
    SHA256_RND(m3, K256 + 44)

    // Rounds 48-63
    m0 = _mm_sha256msg1_epu32(m0, m1);
    m0 = _mm_add_epi32(m0, _mm_sha256msg2_epu32(_mm_alignr_epi8(m3, m2, 4), m3));
    SHA256_RND(m0, K256 + 48)
    m1 = _mm_sha256msg1_epu32(m1, m2);
    m1 = _mm_add_epi32(m1, _mm_sha256msg2_epu32(_mm_alignr_epi8(m0, m3, 4), m0));
    SHA256_RND(m1, K256 + 52)
    m2 = _mm_sha256msg1_epu32(m2, m3);
    m2 = _mm_add_epi32(m2, _mm_sha256msg2_epu32(_mm_alignr_epi8(m1, m0, 4), m1));
    SHA256_RND(m2, K256 + 56)
    m3 = _mm_sha256msg1_epu32(m3, m0);
    m3 = _mm_add_epi32(m3, _mm_sha256msg2_epu32(_mm_alignr_epi8(m2, m1, 4), m2));
    SHA256_RND(m3, K256 + 60)

#undef RND

    //state0 = _mm_add_epi32(state0, a);
    //state1 = _mm_add_epi32(state1, b);
    state0 = _mm_add_epi32(a, state0);
    state1 = _mm_add_epi32(b, state1);
}*/
/* waaaaaa
static inline void
sha256_transform(__m128i& st0, __m128i& st1, const uint8_t* data)
{
    // 1. SALVA lo stato iniziale (per l'accumulo finale)
    __m128i old_st0 = st0;
    __m128i old_st1 = st1;
    
    __m128i m0, m1, m2, m3, t;

    // 2. CARICA i dati (Usa la maschera che inverte solo le word: 3,2,1,0...)
    alignas(16) static const uint8_t kShuf[16] = {12,13,14,15,
     8, 9,10,11,
     4, 5, 6, 7,
     0, 1, 2, 3
    };//3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12};
    const __m128i mask = _mm_load_si128((const __m128i*)kShuf);

    m0 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(data + 0)),  mask);
    m1 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(data + 16)), mask);
    m2 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(data + 32)), mask);
    m3 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(data + 48)), mask);

    // 3. DEFINISCI LA MACRO (Qui stiamo usando st0 e st1 direttamente)
    // Nota l'ordine: st1 (EFGH) viene aggiornato per primo usando st0 (ABCD)
#define RND4(msg, k_idx) \
    t = _mm_add_epi32(msg, _mm_loadu_si128((const __m128i*)&K256[k_idx])); \
    st1 = _mm_sha256rnds2_epu32(st1, st0, t); \
    st0 = _mm_sha256rnds2_epu32(st0, st1, _mm_alignr_epi8(t, t, 8));

    // Round 0-15
    RND4(m0, 0); RND4(m1, 4); RND4(m2, 8); RND4(m3, 12);

    // Round 16-63 (Message Schedule + RND)
    for (int j = 16; j < 64; j += 16) {
        m0 = _mm_add_epi32(_mm_sha256msg1_epu32(m0, m1), _mm_sha256msg2_epu32(_mm_alignr_epi8(m3, m2, 4), m3));
        RND4(m0, j);
        m1 = _mm_add_epi32(_mm_sha256msg1_epu32(m1, m2), _mm_sha256msg2_epu32(_mm_alignr_epi8(m0, m3, 4), m0));
        RND4(m1, j+4);
        m2 = _mm_add_epi32(_mm_sha256msg1_epu32(m2, m3), _mm_sha256msg2_epu32(_mm_alignr_epi8(m1, m0, 4), m1));
        RND4(m2, j+8);
        m3 = _mm_add_epi32(_mm_sha256msg1_epu32(m3, m0), _mm_sha256msg2_epu32(_mm_alignr_epi8(m2, m1, 4), m2));
        RND4(m3, j+12);
    }

    // 4. ACCUMULO FINALE (Somma lo stato iniziale a quello calcolato)
    st0 = _mm_add_epi32(st0, old_st0);
    st1 = _mm_add_epi32(st1, old_st1);
}*/
static const __m128i SHUF_MASK = _mm_set_epi8(
    12, 13, 14, 15,
    8, 9, 10, 11,
    4, 5, 6, 7,
    0, 1, 2, 3
);
static void
sha256_transform(__m128i& state0, __m128i& state1, const uint8* block)
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
static void
sha256_debug_transform(__m128i& st0, __m128i& st1, const uint8_t* data)
{
	/*
    __m128i m0, t;
    const __m128i mask = _mm_set_epi8(3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12);

    // Carichiamo solo il primo blocco (4 word)
    m0 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)data), mask);

    // Eseguiamo SOLO i primi 4 round
    t = _mm_add_epi32(m0, _mm_loadu_si128((const __m128i*)&K256[0]));
    st1 = _mm_sha256rnds2_epu32(st1, st0, t);
    st0 = _mm_sha256rnds2_epu32(st0, st1, _mm_alignr_epi8(t, t, 8));
    
    // Niente accumulo finale per ora, vogliamo vedere lo stato puro dopo 4 round
    */
    st0 = _mm_setzero_si128();
    st1 = _mm_setzero_si128();

    // Usiamo un messaggio "finto" tutto zero
    __m128i m_test = _mm_setzero_si128();
    
    // Usiamo una costante finta (0x01010101...) invece di K256
    __m128i k_test = _mm_set1_epi32(0x01010101);

    // Eseguiamo i round
    __m128i t = _mm_add_epi32(m_test, k_test);
    st1 = _mm_sha256rnds2_epu32(st1, st0, t);
    st0 = _mm_sha256rnds2_epu32(st0, st1, _mm_alignr_epi8(t, t, 8));
}
static inline void
sha256_transform_orig(__m128i& st0, __m128i& st1, const uint8_t* data)
{
    __m128i old_st0 = st0;
    __m128i old_st1 = st1;
    __m128i m0, m1, m2, m3, t;

    // 1. CARICAMENTO DATI
    // Maschera corretta per SHA-NI (inverte l'endianness di ogni uint32)
    const __m128i mask = _mm_set_epi8(3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12);
    
    m0 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(data + 0)),  mask);
    m1 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(data + 16)), mask);
    m2 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(data + 32)), mask);
    m3 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(data + 48)), mask);

    // 2. MACRO DEI ROUND
    // Ogni chiamata processa 4 round SHA-256
#define SHA256_RND4(msg, k_idx) \
    t = _mm_add_epi32(msg, _mm_loadu_si128((const __m128i*)&K256[k_idx])); \
    st1 = _mm_sha256rnds2_epu32(st1, st0, t); \
    st0 = _mm_sha256rnds2_epu32(st0, st1, _mm_alignr_epi8(t, t, 8));

    // 3. MACRO MESSAGE SCHEDULE (Intel Pipeline)
#define MSG_STEP(curr, next, plus2, plus3) \
    curr = _mm_sha256msg1_epu32(curr, next); \
    curr = _mm_add_epi32(curr, _mm_sha256msg2_epu32(_mm_alignr_epi8(plus3, plus2, 4), plus3));

    // --- ROUNDS 0-15 ---
    SHA256_RND4(m0, 0);
    SHA256_RND4(m1, 4);
    SHA256_RND4(m2, 8);
    SHA256_RND4(m3, 12);

    // --- ROUNDS 16-31 ---
    MSG_STEP(m0, m1, m2, m3); SHA256_RND4(m0, 16);
    MSG_STEP(m1, m2, m3, m0); SHA256_RND4(m1, 20);
    MSG_STEP(m2, m3, m0, m1); SHA256_RND4(m2, 24);
    MSG_STEP(m3, m0, m1, m2); SHA256_RND4(m3, 28);

    // --- ROUNDS 32-47 ---
    MSG_STEP(m0, m1, m2, m3); SHA256_RND4(m0, 32);
    MSG_STEP(m1, m2, m3, m0); SHA256_RND4(m1, 36);
    MSG_STEP(m2, m3, m0, m1); SHA256_RND4(m2, 40);
    MSG_STEP(m3, m0, m1, m2); SHA256_RND4(m3, 44);

    // --- ROUNDS 48-63 ---
    MSG_STEP(m0, m1, m2, m3); SHA256_RND4(m0, 48);
    MSG_STEP(m1, m2, m3, m0); SHA256_RND4(m1, 52);
    MSG_STEP(m2, m3, m0, m1); SHA256_RND4(m2, 56);
    MSG_STEP(m3, m0, m1, m2); SHA256_RND4(m3, 60);

    // 4. ACCUMULO FINALE
    st0 = _mm_add_epi32(st0, old_st0);
    st1 = _mm_add_epi32(st1, old_st1);

#undef SHA256_RND4
#undef MSG_STEP
}
//static const __m128i SHUF_ENDIAN = { 0x0c0d0e0f0,0x08090a0b,0x04050607,0x00010203 };


static const __m128i MASK_ENDIAN = _mm_set_epi8(12,13,14,15, 8,9,10,11, 4,5,6,7, 0,1,2,3);
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
/*
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
}*/
/*
static void
extract_digest(__m128i state0, __m128i state1, uint8* digest)
{
    // Salva temporaneamente i registri
    alignas(16) uint32 temp[8];
    _mm_storeu_si128((__m128i*)&temp[0], state0);
    _mm_storeu_si128((__m128i*)&temp[4], state1);
    
    // Intel SHA-NI memorizza: state0={C,D,A,B}, state1={G,H,E,F}
    // quindi temp[] = {C, D, A, B, G, H, E, F}
    // Dobbiamo riordinare in: {A, B, C, D, E, F, G, H}
    
    uint32* out32 = (uint32*)digest;
    
    // Mappa corretta basata sul layout Intel SHA-NI
    out32[0] = __builtin_bswap32(temp[2]); // A
    out32[1] = __builtin_bswap32(temp[3]); // B  
    out32[2] = __builtin_bswap32(temp[0]); // C
    out32[3] = __builtin_bswap32(temp[1]); // D
    out32[4] = __builtin_bswap32(temp[6]); // E
    out32[5] = __builtin_bswap32(temp[7]); // F
    out32[6] = __builtin_bswap32(temp[4]); // G
    out32[7] = __builtin_bswap32(temp[5]); // H
}*/
static inline void
extract_digest(__m128i st0, __m128i st1, uint8_t* o)
{
//    uint32_t* o = (uint32_t*)out;
uint32_t* out = (uint32_t*)o;//request->destination[0].iov_base;

// A, B, C, D sono negli slot 0, 1, 2, 3 di st0
out[0] = __builtin_bswap32(_mm_extract_epi32(st0, 0)); 
out[1] = __builtin_bswap32(_mm_extract_epi32(st0, 1)); 
out[2] = __builtin_bswap32(_mm_extract_epi32(st0, 2)); 
out[3] = __builtin_bswap32(_mm_extract_epi32(st0, 3)); 

// E, F, G, H sono negli slot 0, 1, 2, 3 di st1
out[4] = __builtin_bswap32(_mm_extract_epi32(st1, 0)); 
out[5] = __builtin_bswap32(_mm_extract_epi32(st1, 1)); 
out[6] = __builtin_bswap32(_mm_extract_epi32(st1, 2)); 
out[7] = __builtin_bswap32(_mm_extract_epi32(st1, 3));
    //o[0] = __builtin_bswap32(_mm_extract_epi32(st0, 3));
    //o[1] = __builtin_bswap32(_mm_extract_epi32(st0, 2));
    //o[2] = __builtin_bswap32(_mm_extract_epi32(st0, 1));
    //o[3] = __builtin_bswap32(_mm_extract_epi32(st0, 0));
    //o[4] = __builtin_bswap32(_mm_extract_epi32(st1, 3));
    //o[5] = __builtin_bswap32(_mm_extract_epi32(st1, 2));
    //o[6] = __builtin_bswap32(_mm_extract_epi32(st1, 1));
    //o[7] = __builtin_bswap32(_mm_extract_epi32(st1, 0));*/

//    o[0] = _mm_extract_epi32(st0, 3);
//    o[1] = _mm_extract_epi32(st0, 2);
//    o[2] = _mm_extract_epi32(st0, 1);
//    o[3] = _mm_extract_epi32(st0, 0);
//    o[4] = _mm_extract_epi32(st1, 3);
//    o[5] = _mm_extract_epi32(st1, 2);
//    o[6] = _mm_extract_epi32(st1, 1);
//    o[7] = _mm_extract_epi32(st1, 0);

    
//    o[0] = __builtin_bswap32(_mm_extract_epi32(st0, 0)); // A
//    o[1] = __builtin_bswap32(_mm_extract_epi32(st0, 1)); // B
//    o[2] = __builtin_bswap32(_mm_extract_epi32(st0, 2)); // C
//    o[3] = __builtin_bswap32(_mm_extract_epi32(st0, 3)); // D
//    o[4] = __builtin_bswap32(_mm_extract_epi32(st1, 0)); // E
//    o[5] = __builtin_bswap32(_mm_extract_epi32(st1, 1)); // F
//    o[6] = __builtin_bswap32(_mm_extract_epi32(st1, 2)); // G
//    o[7] = __builtin_bswap32(_mm_extract_epi32(st1, 3)); // H

}
/* da provare : 
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
}*/
status_t
x86_sha256_process(BCryptoRequest* request)
{
    B_PREPARE_CPU_STATE();
    
    //__m128i m0 = _mm_setr_epi32(0x80636261, 0, 0, 0);
    //__m128i m3 = _mm_setr_epi32(0, 0, 0, 0x18000000);
    __m128i abcd = _mm_setr_epi32(0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a); 
__m128i efgh = _mm_setr_epi32(0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19);
__m128i old_abcd = abcd;
__m128i old_efgh = efgh;

// 2. MESSAGGIO "abc"
__m128i m0 = _mm_setr_epi32(0x80636261, 0, 0, 0); 
__m128i m1 = _mm_setzero_si128();
__m128i m2 = _mm_setzero_si128();
__m128i m3 = _mm_setr_epi32(0, 0, 0, 0x18000000); 

// 3. ROUND 0-63 (Usa le macro per brevità, ora che sappiamo che il motore è stabile)
#define RND4(msg, k_idx) \
    tmp = _mm_add_epi32(msg, _mm_loadu_si128((__m128i*)&K256[k_idx])); \
    efgh = _mm_sha256rnds2_epu32(efgh, abcd, tmp); \
    abcd = _mm_sha256rnds2_epu32(abcd, efgh, _mm_alignr_epi8(tmp, tmp, 8));

#define MSGSTEP(m0, m1, m2, m3) \
    m0 = _mm_sha256msg1_epu32(m0, m1); \
    m0 = _mm_add_epi32(m0, _mm_sha256msg2_epu32(_mm_alignr_epi8(m3, m2, 4), m3));

__m128i tmp;
RND4(m0, 0); RND4(m1, 4); RND4(m2, 8); RND4(m3, 12);

MSGSTEP(m0, m1, m2, m3); RND4(m0, 16);
MSGSTEP(m1, m2, m3, m0); RND4(m1, 20);
MSGSTEP(m2, m3, m0, m1); RND4(m2, 24);
MSGSTEP(m3, m0, m1, m2); RND4(m3, 28);

MSGSTEP(m0, m1, m2, m3); RND4(m0, 32);
MSGSTEP(m1, m2, m3, m0); RND4(m1, 36);
MSGSTEP(m2, m3, m0, m1); RND4(m2, 40);
MSGSTEP(m3, m0, m1, m2); RND4(m3, 44);

MSGSTEP(m0, m1, m2, m3); RND4(m0, 48);
MSGSTEP(m1, m2, m3, m0); RND4(m1, 52);
MSGSTEP(m2, m3, m0, m1); RND4(m2, 56);
MSGSTEP(m3, m0, m1, m2); RND4(m3, 60);

// 4. ACCUMULO FINALE
abcd = _mm_add_epi32(abcd, old_abcd);
efgh = _mm_add_epi32(efgh, old_efgh);

// 5. ESTRAZIONE FINALE (Dopo 64 round la mappatura torna standard)
alignas(16) uint32_t res[8];
_mm_storeu_si128((__m128i*)&res[0], abcd);
_mm_storeu_si128((__m128i*)&res[4], efgh);

uint32_t* out = (uint32_t*)request->destination[0].iov_base;

// Registro ABCD: Intel mette D in res[0], C in res[1], B in res[2], A in res[3]
out[0] = __builtin_bswap32(res[3]); // A
out[1] = __builtin_bswap32(res[2]); // B
out[2] = __builtin_bswap32(res[1]); // C
out[3] = __builtin_bswap32(res[0]); // D

// Registro EFGH: Intel mette H in res[4], G in res[5], F in res[6], E in res[7]
out[4] = __builtin_bswap32(res[7]); // E
out[5] = __builtin_bswap32(res[6]); // F
out[6] = __builtin_bswap32(res[5]); // G
out[7] = __builtin_bswap32(res[4]); // H

    
    
/*stampa numeri casuali in uscita sulla seconda parte dell'hash!!!! ATTENZIONE non stiamo bloccando tutto il necessario    
    // 1. Reset stato (Usiamo setr per ordine A,B,C,D lineare)
__m128i abcd = _mm_set_epi32(0xa54ff53a, 0x3c6ef372, 0xbb67ae85, 0x6a09e667); 
__m128i efgh = _mm_set_epi32(0x5be0cd19, 0x1f83d9ab, 0x9b05688c, 0x510e527f);

// 2. MESSAGGIO "abc" (Il Big-Endian dello SHA-256 in un registro Little-Endian)
// La prima Word deve essere 0x61626380.
__m128i m0 = _mm_set_epi32(0, 0, 0, 0x80636261); 
__m128i m1 = _mm_setzero_si128();
__m128i m2 = _mm_setzero_si128();
__m128i m3 = _mm_set_epi32(0x18000000, 0, 0, 0);
__m128i k0 = _mm_add_epi32(m0, _mm_set_epi32(0xe9b5dba5, 0xb5c0fbcf, 0x71374491, 0x428a2f98));
__m128i k1 = _mm_add_epi32(m1, _mm_set_epi32(0xab1c5ed5, 0x923f82a4, 0x59f111f1, 0x3956c25b));
__m128i k2 = _mm_add_epi32(m2, _mm_set_epi32(0x550c7dc3, 0x243185be, 0x12835b01, 0xd807aa98));
__m128i k3 = _mm_add_epi32(m3, _mm_set_epi32(0xc19bf174, 0x9bdc06a7, 0x80deb1fe, 0x72be5d74));

efgh = _mm_sha256rnds2_epu32(efgh, abcd, k0);
abcd = _mm_sha256rnds2_epu32(abcd, efgh, _mm_alignr_epi8(k0, k0, 8));

efgh = _mm_sha256rnds2_epu32(efgh, abcd, k1);
abcd = _mm_sha256rnds2_epu32(abcd, efgh, _mm_alignr_epi8(k1, k1, 8));

efgh = _mm_sha256rnds2_epu32(efgh, abcd, k2);
abcd = _mm_sha256rnds2_epu32(abcd, efgh, _mm_alignr_epi8(k2, k2, 8));

efgh = _mm_sha256rnds2_epu32(efgh, abcd, k3);
abcd = _mm_sha256rnds2_epu32(abcd, efgh, _mm_alignr_epi8(k3, k3, 8));

// 4. ESTRAZIONE SENZA BSWAP
uint32_t* out = (uint32_t*)request->destination[0].iov_base;
out[0] = _mm_extract_epi32(abcd, 0); 
out[1] = _mm_extract_epi32(abcd, 1);
out[2] = _mm_extract_epi32(abcd, 2);
out[3] = _mm_extract_epi32(abcd, 3);
    // 1. STATO INIZIALE
//__m128i abcd = _mm_set_epi32(0xa54ff53a, 0x3c6ef372, 0xbb67ae85, 0x6a09e667); 
//__m128i efgh = _mm_set_epi32(0x5be0cd19, 0x1f83d9ab, 0x9b05688c, 0x510e527f);
//__m128i abcd = _mm_setr_epi32(0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a); 
//__m128i efgh = _mm_setr_epi32(0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19);
*/

/*
// 2. SALVA PER ACCUMULO
__m128i old_abcd = abcd;
__m128i old_efgh = efgh;

// 2. MESSAGGIO "abc" (Formato Intel: Big-Endian nelle Word, caricate come Little-Endian)
// Questo è il modo in cui "abc" deve apparire in memoria per SHA-NI
alignas(16) uint32_t msg_words[16] = {0};
msg_words[0] = __builtin_bswap32(0x61626380); // 'a','b','c',0x80
msg_words[15] = __builtin_bswap32(0x00000018); // Lunghezza 24 bit
// Carichiamo i registri senza shuffle
__m128i m0 = _mm_loadu_si128((__m128i*)&msg_words[0]);
__m128i m1 = _mm_loadu_si128((__m128i*)&msg_words[4]);
__m128i m2 = _mm_loadu_si128((__m128i*)&msg_words[8]);
__m128i m3 = _mm_loadu_si128((__m128i*)&msg_words[12]);


__m128i tmp;

// Round 0-3*/
/*tmp = _mm_add_epi32(m0, _mm_loadu_si128((__m128i*)&K256[0]));
efgh = _mm_sha256rnds2_epu32(efgh, abcd, tmp);
abcd = _mm_sha256rnds2_epu32(abcd, efgh, _mm_alignr_epi8(tmp, tmp, 8));
tmp = _mm_add_epi32(m0, _mm_setr_epi32(0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5));
efgh = _mm_sha256rnds2_epu32(efgh, abcd, tmp);
abcd = _mm_sha256rnds2_epu32(abcd, efgh, _mm_alignr_epi8(tmp, tmp, 8));

__asm__ ("" : : : "memory");*/
// Round 4-7
/*
tmp = _mm_add_epi32(m1, _mm_loadu_si128((__m128i*)&K256[4]));
efgh = _mm_sha256rnds2_epu32(efgh, abcd, tmp);
abcd = _mm_sha256rnds2_epu32(abcd, efgh, _mm_alignr_epi8(tmp, tmp, 8));
tmp = _mm_add_epi32(m1, _mm_setr_epi32(0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5));
efgh = _mm_sha256rnds2_epu32(efgh, abcd, tmp);
abcd = _mm_sha256rnds2_epu32(abcd, efgh, _mm_alignr_epi8(tmp, tmp, 8));*/

// Round 8-11
/*
tmp = _mm_add_epi32(m2, _mm_loadu_si128((__m128i*)&K256[8]));
efgh = _mm_sha256rnds2_epu32(efgh, abcd, tmp);
abcd = _mm_sha256rnds2_epu32(abcd, efgh, _mm_alignr_epi8(tmp, tmp, 8));
tmp = _mm_add_epi32(m2, _mm_setr_epi32(0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3));
efgh = _mm_sha256rnds2_epu32(efgh, abcd, tmp);
abcd = _mm_sha256rnds2_epu32(abcd, efgh, _mm_alignr_epi8(tmp, tmp, 8));*/

// Round 12-15
/*
tmp = _mm_add_epi32(m3, _mm_loadu_si128((__m128i*)&K256[12]));
efgh = _mm_sha256rnds2_epu32(efgh, abcd, tmp);
abcd = _mm_sha256rnds2_epu32(abcd, efgh, _mm_alignr_epi8(tmp, tmp, 8));
tmp = _mm_add_epi32(m3, _mm_setr_epi32(0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174));
efgh = _mm_sha256rnds2_epu32(efgh, abcd, tmp);
abcd = _mm_sha256rnds2_epu32(abcd, efgh, _mm_alignr_epi8(tmp, tmp, 8));*/
/*
// 3. TRASFORMAZIONE (Macro standard Intel)
#define RND4(msg, k_idx) \
    tmp = _mm_add_epi32(msg, _mm_loadu_si128((__m128i*)&K256[k_idx])); \
    efgh = _mm_sha256rnds2_epu32(efgh, abcd, tmp); \
    abcd = _mm_sha256rnds2_epu32(abcd, efgh, _mm_alignr_epi8(tmp, tmp, 8));

__m128i tmp;
RND4(m0, 0); RND4(m1, 4); RND4(m2, 8); RND4(m3, 12);

#define MSGSTEP(m0, m1, m2, m3) \
    m0 = _mm_sha256msg1_epu32(m0, m1); \
    m0 = _mm_add_epi32(m0, _mm_sha256msg2_epu32(_mm_alignr_epi8(m3, m2, 4), m3));

// Round 16-31
MSGSTEP(m0, m1, m2, m3); RND4(m0, 16);
MSGSTEP(m1, m2, m3, m0); RND4(m1, 20);
MSGSTEP(m2, m3, m0, m1); RND4(m2, 24);
MSGSTEP(m3, m0, m1, m2); RND4(m3, 28);

// Round 32-47
MSGSTEP(m0, m1, m2, m3); RND4(m0, 32);
MSGSTEP(m1, m2, m3, m0); RND4(m1, 36);
MSGSTEP(m2, m3, m0, m1); RND4(m2, 40);
MSGSTEP(m3, m0, m1, m2); RND4(m3, 44);

// Round 48-63
MSGSTEP(m0, m1, m2, m3); RND4(m0, 48);
MSGSTEP(m1, m2, m3, m0); RND4(m1, 52);
MSGSTEP(m2, m3, m0, m1); RND4(m2, 56);
MSGSTEP(m3, m0, m1, m2); RND4(m3, 60);*/

// ... (Incolla qui i restanti round 16-63 con MSG_STEP e RND4) ...
/*
// 4. ACCUMULO
abcd = _mm_add_epi32(abcd, old_abcd);
efgh = _mm_add_epi32(efgh, old_efgh);

// 5. ESTRAZIONE (L'unica che rimette A in out[0] dopo 64 round)
uint32_t* out = (uint32_t*)request->destination[0].iov_base;
out[0] = __builtin_bswap32(_mm_extract_epi32(abcd, 0)); 
out[1] = __builtin_bswap32(_mm_extract_epi32(abcd, 1)); 
out[2] = __builtin_bswap32(_mm_extract_epi32(abcd, 2)); 
out[3] = __builtin_bswap32(_mm_extract_epi32(abcd, 3)); 
out[4] = __builtin_bswap32(_mm_extract_epi32(efgh, 0)); 
out[5] = __builtin_bswap32(_mm_extract_epi32(efgh, 1)); 
out[6] = __builtin_bswap32(_mm_extract_epi32(efgh, 2)); 
out[7] = __builtin_bswap32(_mm_extract_epi32(efgh, 3));
*/





/*
alignas(16) uint8_t abc_pad[64] = {0};
    abc_pad[0] = 'a'; abc_pad[1] = 'b'; abc_pad[2] = 'c';
    abc_pad[3] = 0x80;
    // La lunghezza in bit per "abc" è 24 (0x18). Va in Big-Endian negli ultimi 8 byte.
    abc_pad[63] = 0x18; 

// 3. CARICA MESSAGGIO (abc con padding già pronto nel tuo buffer abc_pad)
const __m128i mask = _mm_set_epi8(3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12);
__m128i m0 = _mm_shuffle_epi8(_mm_loadu_si128((__m128i*)(abc_pad + 0)), mask);
__m128i m1 = _mm_shuffle_epi8(_mm_loadu_si128((__m128i*)(abc_pad + 16)), mask);
__m128i m2 = _mm_shuffle_epi8(_mm_loadu_si128((__m128i*)(abc_pad + 32)), mask);
__m128i m3 = _mm_shuffle_epi8(_mm_loadu_si128((__m128i*)(abc_pad + 48)), mask);
*/
/* tentativo beccato 1 errore (abc_pad passava degli 0 non abc)
//__m128i m0 = _mm_setr_epi32(0x61626380, 0x00000000, 0x00000000, 0x00000000); 
//__m128i m1 = _mm_setzero_si128();
//__m128i m2 = _mm_setzero_si128();
//__m128i m3 = _mm_setr_epi32(0x00000000, 0x00000000, 0x00000000, 0x00000018);

//__m128i m0 = _mm_setr_epi32(0x80636261, 0x00000000, 0x00000000, 0x00000000); 
//__m128i m1 = _mm_setzero_si128();
//__m128i m2 = _mm_setzero_si128();
//__m128i m3 = _mm_setr_epi32(0x00000000, 0x00000000, 0x00000000, 0x18000000);
__m128i m0 = _mm_set_epi32(0, 0, 0, 0x80636261); 
__m128i m1 = _mm_setzero_si128();
__m128i m2 = _mm_setzero_si128();
__m128i m3 = _mm_set_epi32(0x18000000, 0, 0, 0);


// 4. ROUNDS 0-15 (Usiamo variabili t0, t1 per non confonderci)
__m128i tmp;
#define RNDSTEP(msg, k_idx) \
    tmp = _mm_add_epi32(msg, _mm_loadu_si128((__m128i*)&K256[k_idx])); \
    efgh = _mm_sha256rnds2_epu32(efgh, abcd, tmp); \
    abcd = _mm_sha256rnds2_epu32(abcd, efgh, _mm_alignr_epi8(tmp, tmp, 8));

RNDSTEP(m0, 0); RNDSTEP(m1, 4); RNDSTEP(m2, 8); RNDSTEP(m3, 12);

// 5. MESSAGE SCHEDULE + ROUNDS 16-63
#define MSGSTEP(curr, next, p2, p3) \
    curr = _mm_sha256msg1_epu32(curr, next); \
    curr = _mm_add_epi32(curr, _mm_sha256msg2_epu32(_mm_alignr_epi8(p3, p2, 4), p3));

for (int j = 16; j < 64; j += 16) {
    MSGSTEP(m0, m1, m2, m3); RNDSTEP(m0, j);
    MSGSTEP(m1, m2, m3, m0); RNDSTEP(m1, j+4);
    MSGSTEP(m2, m3, m0, m1); RNDSTEP(m2, j+8);
    MSGSTEP(m3, m0, m1, m2); RNDSTEP(m3, j+12);
}

// 6. ACCUMULO FINALE
abcd = _mm_add_epi32(abcd, old_abcd);
efgh = _mm_add_epi32(efgh, old_efgh);

// 7. ESTRAZIONE DIRETTA
uint32_t* out = (uint32_t*)request->destination[0].iov_base;
//out[0] = _mm_extract_epi32(abcd, 3); // Proviamo a leggere dall'alto
//out[1] = _mm_extract_epi32(abcd, 2);
//out[2] = _mm_extract_epi32(abcd, 1);
//out[3] = _mm_extract_epi32(abcd, 0);
//out[4] = _mm_extract_epi32(efgh, 3); // Proviamo a leggere dall'alto
//out[5] = _mm_extract_epi32(efgh, 2);
//out[6] = _mm_extract_epi32(efgh, 1);
//out[7] = _mm_extract_epi32(efgh, 0);
//out[0] = __builtin_bswap32(_mm_extract_epi32(abcd, 3)); // A
//out[1] = __builtin_bswap32(_mm_extract_epi32(abcd, 2)); // B
//out[2] = __builtin_bswap32(_mm_extract_epi32(abcd, 1)); // C
//out[3] = __builtin_bswap32(_mm_extract_epi32(abcd, 0)); // D
//out[4] = __builtin_bswap32(_mm_extract_epi32(efgh, 3)); // E
//out[5] = __builtin_bswap32(_mm_extract_epi32(efgh, 2)); // F
//out[6] = __builtin_bswap32(_mm_extract_epi32(efgh, 1)); // G
//out[7] = __builtin_bswap32(_mm_extract_epi32(efgh, 0)); // H

out[0] = __builtin_bswap32(_mm_extract_epi32(abcd, 2)); // A
out[1] = __builtin_bswap32(_mm_extract_epi32(abcd, 3)); // B
out[2] = __builtin_bswap32(_mm_extract_epi32(abcd, 0)); // C
out[3] = __builtin_bswap32(_mm_extract_epi32(abcd, 1)); // D
out[4] = __builtin_bswap32(_mm_extract_epi32(efgh, 2)); // E
out[5] = __builtin_bswap32(_mm_extract_epi32(efgh, 3)); // F
out[6] = __builtin_bswap32(_mm_extract_epi32(efgh, 0)); // G
out[7] = __builtin_bswap32(_mm_extract_epi32(efgh, 1)); // H

//out[0] = _mm_extract_epi32(abcd, 2); // A
//out[1] = _mm_extract_epi32(abcd, 3); // B
//out[2] = _mm_extract_epi32(abcd, 0); // C
//out[3] = _mm_extract_epi32(abcd, 1); // D
//out[4] = _mm_extract_epi32(efgh, 2); // E
//out[5] = _mm_extract_epi32(efgh, 3); // F
//out[6] = _mm_extract_epi32(efgh, 0); // G
//out[7] = _mm_extract_epi32(efgh, 1); // H

//out[0] = __builtin_bswap32(_mm_extract_epi32(abcd, 0));
//out[1] = __builtin_bswap32(_mm_extract_epi32(abcd, 1));
//out[2] = __builtin_bswap32(_mm_extract_epi32(abcd, 2));
//out[3] = __builtin_bswap32(_mm_extract_epi32(abcd, 3));
//out[4] = __builtin_bswap32(_mm_extract_epi32(efgh, 0));
//out[5] = __builtin_bswap32(_mm_extract_epi32(efgh, 1));
//out[6] = __builtin_bswap32(_mm_extract_epi32(efgh, 2));
//out[7] = __builtin_bswap32(_mm_extract_epi32(efgh, 3));
*/
/*
    // 1. STATO INIZIALE (Standard SHA-256)
    // Usiamo l'ordine richiesto dalle istruzioni Intel: (D, C, B, A) e (H, G, F, E)
    __m128i st0 = _mm_set_epi32(0xa54ff53a, 0x3c6ef372, 0xbb67ae85, 0x6a09e667); 
    __m128i st1 = _mm_set_epi32(0x5be0cd19, 0x1f83d9ab, 0x9b05688c, 0x510e527f);
    
    // Inizializza lo stato SHA-256
    // Nota: questi valori devono essere in little-endian e nell'ordine DCBA/HGFE
    //__m128i st0 = _mm_setr_epi32(0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a);
    //__m128i st1 = _mm_setr_epi32(0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19);

    // 2. BLOCCO DI PADDING MANUALE PER "abc"
    // 64 byte totali: 'a','b','c', 0x80, 00... (52 volte), 00 00 00 00 00 00 00 18
    alignas(16) uint8_t abc_pad[64] = {0};
    abc_pad[0] = 'a'; abc_pad[1] = 'b'; abc_pad[2] = 'c';
    abc_pad[3] = 0x80;
    // La lunghezza in bit per "abc" è 24 (0x18). Va in Big-Endian negli ultimi 8 byte.
    abc_pad[63] = 0x18; 

    // 3. TRASFORMAZIONE (Usa la tua funzione sha256_transform)
    sha256_debug_transform(st0, st1, abc_pad);

    // 4. ESTRAZIONE
    // Se lo stato è (D,C,B,A), l'indice 0 è A.
    uint32_t* out = (uint32_t*)request->destination[0].iov_base;
    //out[0] = __builtin_bswap32(_mm_extract_epi32(st0, 0)); // A
    //out[1] = __builtin_bswap32(_mm_extract_epi32(st0, 1)); // B
    //out[2] = __builtin_bswap32(_mm_extract_epi32(st0, 2)); // C
    //out[3] = __builtin_bswap32(_mm_extract_epi32(st0, 3)); // D
    //out[4] = __builtin_bswap32(_mm_extract_epi32(st1, 0)); // E
    //out[5] = __builtin_bswap32(_mm_extract_epi32(st1, 1)); // F
    //out[6] = __builtin_bswap32(_mm_extract_epi32(st1, 2)); // G
    //out[7] = __builtin_bswap32(_mm_extract_epi32(st1, 3)); // H
    //extract_digest(st0,st1, (uint8*)request->destination[0].iov_base);
    out[0] = __builtin_bswap32(_mm_extract_epi32(st0, 0)); 
    out[1] = __builtin_bswap32(_mm_extract_epi32(st0, 1)); 
    out[2] = __builtin_bswap32(_mm_extract_epi32(st0, 2)); 
    out[3] = __builtin_bswap32(_mm_extract_epi32(st0, 3)); 

    // E, F, G, H sono negli slot 0, 1, 2, 3 di st1
    out[4] = __builtin_bswap32(_mm_extract_epi32(st1, 0)); 
    out[5] = __builtin_bswap32(_mm_extract_epi32(st1, 1)); 
    out[6] = __builtin_bswap32(_mm_extract_epi32(st1, 2)); 
    out[7] = __builtin_bswap32(_mm_extract_epi32(st1, 3));*/

    B_RESTORE_CPU_STATE();
    return B_OK;
}
// --- ONE-SHOT PROCESS ---
/*
status_t
x86_sha256_process(BCryptoRequest* request)
{
    if (request == NULL || request->algorithm != B_CRYPTO_SHA256) return B_BAD_VALUE;
    if (request->vectorCount == 0 || request->source == NULL) return B_OK;
    if (request->destination == NULL || request->destination[0].iov_base == NULL) return B_BAD_VALUE;

    B_PREPARE_CPU_STATE();

    __m128i st0 = _mm_set_epi32(0xa54ff53a, 0x3c6ef372, 0xbb67ae85, 0x6a09e667); // A, B, C, D
    __m128i st1 = _mm_set_epi32(0x5be0cd19, 0x1f83d9ab, 0x9b05688c, 0x510e527f); // E, F, G, H
    //__m128i st0 = _mm_set_epi32(0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a); // D, C, B, A
    //__m128i st1 = _mm_set_epi32(0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19); // H, G, F, E
    //__m128i st0 = _mm_set_epi32(0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a); // (D,C,B,A)
    //__m128i st1 = _mm_set_epi32(0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19); // (H,G,F,E)
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
            //sha256_transform_64bytes(st0, st1, data);
            sha256_transform(st0, st1, data);
            data += 64; 
            len -= 64;
        }

        // Gestione dell'ultimo chunk (Padding)
        if (i == request->vectorCount - 1) {
            alignas(16) uint8 pad[128];
            memset(pad, 0, 128);
            memcpy(pad, data, len); // Copia il residuo (< 64 bytes)
            
            pad[len] = 0x80; // Bit di stop
            
            // Se non c'è spazio per i 64 bit di lunghezza, usiamo due blocchi
            size_t padLen = (len < 56) ? 64 : 128;
            uint64_t bitLen = __builtin_bswap64((uint64_t)totalBytes * 8);
            memcpy(pad + padLen - 8, &bitLen, 8);
            
            // USA SEMPRE LA STESSA FUNZIONE!
            //sha256_transform_64bytes(st0, st1, pad);
            sha256_transform(st0, st1, pad);
            if (padLen == 128) 
                sha256_transform(st0, st1, pad + 64);
        }
    }

    extract_digest(st0, st1, (uint8*)request->destination[0].iov_base);
    B_RESTORE_CPU_STATE();
    return B_OK;
}*/

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

status_t x86_sha256_update_bridge(void* ctx, const iovec* vecs, size_t count) {
    x86_sha256_context* s = (x86_sha256_context*)ctx;
    if (s == NULL) return B_BAD_VALUE;

    B_PREPARE_CPU_STATE(); // SALVA REGISTRI PER UPDATE

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
    }

    B_RESTORE_CPU_STATE(); // RIPRISTINA REGISTRI
    return B_OK;
}

status_t x86_sha256_final_bridge(void* ctx, uint8* out) {
    x86_sha256_context* s = (x86_sha256_context*)ctx;
    if (s == NULL || out == NULL) return B_BAD_VALUE;

    B_PREPARE_CPU_STATE(); // SALVA REGISTRI PER FINAL

    alignas(16) uint8 pad[128];
    memcpy(pad, s->buffer, s->buffer_len);
    pad[s->buffer_len] = 0x80;
    size_t padLen = (s->buffer_len < 56) ? 64 : 128;
    memset(pad + s->buffer_len + 1, 0, padLen - s->buffer_len - 1);
    
    uint64_t bits = __builtin_bswap64(s->total_len * 8);
    memcpy(pad + padLen - 8, &bits, 8);
    
    sha256_transform_block(s->state0, s->state1, pad);
    if (padLen == 128) sha256_transform_block(s->state0, s->state1, pad + 64);
    
    extract_digest(s->state0, s->state1, out);

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
