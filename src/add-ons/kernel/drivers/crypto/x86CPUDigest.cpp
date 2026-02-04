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

status_t
x86_sha256_process(BCryptoRequest* request)
{
    B_PREPARE_CPU_STATE();

    alignas(16) uint32_t state_abcd[4] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a};
    alignas(16) uint32_t state_efgh[4] = {0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    
    // Messaggio "abc"
    /*alignas(16) uint32_t msg[16] = {0};
    msg[0] = 0x80636261; 
    msg[15] = 0x18000000;*/
    alignas(16) uint32_t msg[16] = {0};
    msg[0] = 0x80636261; // "abc" + stop bit (Big Endian per SHA-NI)
    msg[15] = __builtin_bswap32(0x18); // Lunghezza 24 bit (già al posto giusto per SHA-NI)

    alignas(16) uint32_t res[8];

    asm volatile (
        // 1. Caricamento stato e costanti iniziali
        "movdqu %[abcd_in], %%xmm1 \n\t" 
        "movdqu %[efgh_in], %%xmm2 \n\t"
        "movdqu 0(%[m_ptr]), %%xmm4 \n\t"  // m0
        "movdqu 16(%[m_ptr]), %%xmm5 \n\t" // m1
        "movdqu 32(%[m_ptr]), %%xmm6 \n\t" // m2
        "movdqu 48(%[m_ptr]), %%xmm7 \n\t" // m3

        // Macro manuale per 4 round (aggiorna xmm1/xmm2 usando msg in xmm_msg)
        // Usiamo xmm0 come registro temporaneo per K + MSG
        #define SHA256_4RNDS(xmm_msg, k_offset) \
            "movdqu " #k_offset "(%[k_ptr]), %%xmm0 \n\t" \
            "paddd " #xmm_msg ", %%xmm0 \n\t" \
            "sha256rnds2 %%xmm0, %%xmm1, %%xmm2 \n\t" \
            "palignr $8, %%xmm0, %%xmm0 \n\t" \
            "sha256rnds2 %%xmm0, %%xmm2, %%xmm1 \n\t"

        // Macro manuale per Message Schedule (aggiorna m_old con i nuovi dati)
        #define SHA256_MSGSTEP(m0, m1, m2, m3) \
            "sha256msg1 " #m1 ", " #m0 " \n\t" \
            "movdqu " #m3 ", %%xmm3 \n\t" \
            "palignr $4, " #m2 ", %%xmm3 \n\t" \
            "paddd %%xmm3, " #m0 " \n\t" \
            "sha256msg2 " #m3 ", " #m0 " \n\t"

        // ROUND 0-15
        SHA256_4RNDS(%%xmm4, 0)
        SHA256_4RNDS(%%xmm5, 16)
        SHA256_4RNDS(%%xmm6, 32)
        SHA256_4RNDS(%%xmm7, 48)

        // ROUND 16-31
        SHA256_MSGSTEP(%%xmm4, %%xmm5, %%xmm6, %%xmm7)
        SHA256_4RNDS(%%xmm4, 64)
        SHA256_MSGSTEP(%%xmm5, %%xmm6, %%xmm7, %%xmm4)
        SHA256_4RNDS(%%xmm5, 80)
        SHA256_MSGSTEP(%%xmm6, %%xmm7, %%xmm4, %%xmm5)
        SHA256_4RNDS(%%xmm6, 96)
        SHA256_MSGSTEP(%%xmm7, %%xmm4, %%xmm5, %%xmm6)
        SHA256_4RNDS(%%xmm7, 112)

        // ROUND 32-47
        SHA256_MSGSTEP(%%xmm4, %%xmm5, %%xmm6, %%xmm7)
        SHA256_4RNDS(%%xmm4, 128)
        SHA256_MSGSTEP(%%xmm5, %%xmm6, %%xmm7, %%xmm4)
        SHA256_4RNDS(%%xmm5, 144)
        SHA256_MSGSTEP(%%xmm6, %%xmm7, %%xmm4, %%xmm5)
        SHA256_4RNDS(%%xmm6, 160)
        SHA256_MSGSTEP(%%xmm7, %%xmm4, %%xmm5, %%xmm6)
        SHA256_4RNDS(%%xmm7, 176)

        // ROUND 48-63
        SHA256_MSGSTEP(%%xmm4, %%xmm5, %%xmm6, %%xmm7)
        SHA256_4RNDS(%%xmm4, 192)
        SHA256_MSGSTEP(%%xmm5, %%xmm6, %%xmm7, %%xmm4)
        SHA256_4RNDS(%%xmm5, 208)
        SHA256_MSGSTEP(%%xmm6, %%xmm7, %%xmm4, %%xmm5)
        SHA256_4RNDS(%%xmm6, 224)
        SHA256_MSGSTEP(%%xmm7, %%xmm4, %%xmm5, %%xmm6)
        SHA256_4RNDS(%%xmm7, 240)

        // Salvataggio finale dello stato (xmm1=abcd, xmm2=efgh)
        "movdqu %%xmm1, 0(%[out_ptr]) \n\t"
        "movdqu %%xmm2, 16(%[out_ptr]) \n\t"

        : 
        : [abcd_in] "m" (state_abcd), [efgh_in] "m" (state_efgh),
          [m_ptr] "r" (msg), [k_ptr] "r" (K256), [out_ptr] "r" (res)
        : "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7", "memory"
    );

    // 4. ACCUMULO FINALE (Fondamentale!)
    // In SHA-256 l'hash finale è lo stato iniziale + lo stato dopo i round
    uint32_t init_abcd[4] = {0xa54ff53a, 0x3c6ef372, 0xbb67ae85, 0x6a09e667};
    uint32_t init_efgh[4] = {0x5be0cd19, 0x1f83d9ab, 0x9b05688c, 0x510e527f};
    
    for(int i=0; i<4; i++) {
        res[i] += init_abcd[i];   // Accumulo nel registro ABCD (che è DCBA)
        res[i+4] += init_efgh[i]; // Accumulo nel registro EFGH (che è HGFE)
    }

    // 5. ESTRAZIONE BIG-ENDIAN
    uint32_t* out = (uint32_t*)request->destination[0].iov_base;
    
    // Ora mappiamo i risultati finali raddrizzandoli
    out[0] = __builtin_bswap32(res[3]); // A
    out[1] = __builtin_bswap32(res[2]); // B
    out[2] = __builtin_bswap32(res[1]); // C
    out[3] = __builtin_bswap32(res[0]); // D
    out[4] = __builtin_bswap32(res[7]); // E
    out[5] = __builtin_bswap32(res[6]); // F
    out[6] = __builtin_bswap32(res[5]); // G
    out[7] = __builtin_bswap32(res[4]); // H
    
    B_RESTORE_CPU_STATE();
    return B_OK;
}

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
