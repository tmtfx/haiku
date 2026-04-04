/*
 * Hardware-accelerated AES using Intel/AMD AES-NI instructions
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include "AESNI.h"
#include "SoftCryptoPriv.h"
#include "BCryptoCore.h"
#include <crypto/BCryptoDefs.h>
#include <string.h>
#include "BCryptoAlgorithm.h"
#include "SoftCryptoEngines.h"

#if defined(__x86_64__) || defined(__i386__)
#include "BCryptoCPU.h"
#include <arch/x86/arch_cpu.h>
#include <wmmintrin.h>
#include <tmmintrin.h>
#include <immintrin.h>
#include <malloc.h>
#include <debug.h>

#pragma GCC target("aes,sse4.1,pclmul")

/* --- Funzioni di Supporto --- */

static void
secure_memzero(void* p, size_t s)
{
    if (p == NULL) return;
    volatile uint8* cp = (volatile uint8*)p;
    while (s--) *cp++ = 0;
}
//static ghash_multiply_func sGCMHashFunc = NULL;
static bool ghash_accel=false;

static inline void
aesni_increment_gcm_ctr(uint8 counter[16])
{
    // GCM incrementa solo gli ultimi 4 byte (32-bit) in Big Endian
    for (int i = 15; i >= 12; i--) {
        if (++counter[i] != 0)
            break;
    }
}

/* --- Key Expansion --- */
static void
aesni_expand_key_128(AESNIContext& ctx, const uint8* key)
{
    __m128i k = _mm_loadu_si128((const __m128i*)key);
    ctx.encRoundKeys[0] = k;

    auto expand = [&](__m128i current, int rcon) {
        __m128i t = _mm_aeskeygenassist_si128(current, rcon);
        t = _mm_shuffle_epi32(t, 0xff);
        current = _mm_xor_si128(current, _mm_slli_si128(current, 4));
        current = _mm_xor_si128(current, _mm_slli_si128(current, 4));
        current = _mm_xor_si128(current, _mm_slli_si128(current, 4));
        return _mm_xor_si128(current, t);
    };

    ctx.encRoundKeys[1] = k = expand(k, 0x01);
    ctx.encRoundKeys[2] = k = expand(k, 0x02);
    ctx.encRoundKeys[3] = k = expand(k, 0x04);
    ctx.encRoundKeys[4] = k = expand(k, 0x08);
    ctx.encRoundKeys[5] = k = expand(k, 0x10);
    ctx.encRoundKeys[6] = k = expand(k, 0x20);
    ctx.encRoundKeys[7] = k = expand(k, 0x40);
    ctx.encRoundKeys[8] = k = expand(k, 0x80);
    ctx.encRoundKeys[9] = k = expand(k, 0x1b);
    ctx.encRoundKeys[10] = k = expand(k, 0x36);
    ctx.rounds = 10;
}
static inline void
aesni_192_assist(__m128i* t1, __m128i* t2, __m128i t3)
{
    t3 = _mm_shuffle_epi32(t3, 0x55);
    *t1 = _mm_xor_si128(*t1, _mm_slli_si128(*t1, 4));
    *t1 = _mm_xor_si128(*t1, _mm_slli_si128(*t1, 4));
    *t1 = _mm_xor_si128(*t1, _mm_slli_si128(*t1, 4));
    *t1 = _mm_xor_si128(*t1, t3);

    __m128i tmp = _mm_shuffle_epi32(*t1, 0xff);
    *t2 = _mm_xor_si128(*t2, _mm_slli_si128(*t2, 4));
    *t2 = _mm_xor_si128(*t2, tmp);
}

static void
aesni_expand_key_192(AESNIContext& ctx, const uint8* key)
{
    __m128i t1, t2, t3;
    ctx.rounds = 12;

    t1 = _mm_loadu_si128((const __m128i*)key);
    t2 = _mm_loadl_epi64((const __m128i*)(key + 16));

    ctx.encRoundKeys[0] = t1;

    // RCON 0x01
    t3 = _mm_aeskeygenassist_si128(t2, 0x01);
    aesni_192_assist(&t1, &t2, t3);
    ctx.encRoundKeys[1] = _mm_castpd_si128(_mm_move_sd(_mm_castsi128_pd(t2), _mm_castsi128_pd(t1)));
    ctx.encRoundKeys[2] = _mm_shuffle_epi32(t1, 0x44);
    ctx.encRoundKeys[2] = _mm_castpd_si128(_mm_move_sd(_mm_castsi128_pd(ctx.encRoundKeys[2]), _mm_castsi128_pd(t2)));

    // RCON 0x02
    t3 = _mm_aeskeygenassist_si128(t2, 0x02);
    aesni_192_assist(&t1, &t2, t3);
    ctx.encRoundKeys[3] = t1;
    ctx.encRoundKeys[4] = t2;

    // RCON 0x04
    t3 = _mm_aeskeygenassist_si128(t2, 0x04);
    aesni_192_assist(&t1, &t2, t3);
    ctx.encRoundKeys[4] = _mm_castpd_si128(_mm_move_sd(_mm_castsi128_pd(t2), _mm_castsi128_pd(t1)));
    ctx.encRoundKeys[5] = _mm_shuffle_epi32(t1, 0x44);
    ctx.encRoundKeys[5] = _mm_castpd_si128(_mm_move_sd(_mm_castsi128_pd(ctx.encRoundKeys[5]), _mm_castsi128_pd(t2)));

    // RCON 0x08
    t3 = _mm_aeskeygenassist_si128(t2, 0x08);
    aesni_192_assist(&t1, &t2, t3);
    ctx.encRoundKeys[6] = t1;
    ctx.encRoundKeys[7] = t2;

    // RCON 0x10
    t3 = _mm_aeskeygenassist_si128(t2, 0x10);
    aesni_192_assist(&t1, &t2, t3);
    ctx.encRoundKeys[7] = _mm_castpd_si128(_mm_move_sd(_mm_castsi128_pd(t2), _mm_castsi128_pd(t1)));
    ctx.encRoundKeys[8] = _mm_shuffle_epi32(t1, 0x44);
    ctx.encRoundKeys[8] = _mm_castpd_si128(_mm_move_sd(_mm_castsi128_pd(ctx.encRoundKeys[8]), _mm_castsi128_pd(t2)));

    // RCON 0x20
    t3 = _mm_aeskeygenassist_si128(t2, 0x20);
    aesni_192_assist(&t1, &t2, t3);
    ctx.encRoundKeys[9] = t1;
    ctx.encRoundKeys[10] = t2;

    // RCON 0x40
    t3 = _mm_aeskeygenassist_si128(t2, 0x40);
    aesni_192_assist(&t1, &t2, t3);
    ctx.encRoundKeys[10] = _mm_castpd_si128(_mm_move_sd(_mm_castsi128_pd(t2), _mm_castsi128_pd(t1)));
    ctx.encRoundKeys[11] = _mm_shuffle_epi32(t1, 0x44);
    ctx.encRoundKeys[11] = _mm_castpd_si128(_mm_move_sd(_mm_castsi128_pd(ctx.encRoundKeys[11]), _mm_castsi128_pd(t2)));

    // RCON 0x80
    t3 = _mm_aeskeygenassist_si128(t2, 0x80);
    aesni_192_assist(&t1, &t2, t3);
    ctx.encRoundKeys[12] = t1;
}

static inline __m128i 
aesni_256_assist(__m128i t1, __m128i t2) {
    __m128i t3;
    t2 = _mm_shuffle_epi32(t2, 0xff);
    t3 = _mm_slli_si128(t1, 0x04);
    t1 = _mm_xor_si128(t1, t3);
    t3 = _mm_slli_si128(t3, 0x04);
    t1 = _mm_xor_si128(t1, t3);
    t3 = _mm_slli_si128(t3, 0x04);
    t1 = _mm_xor_si128(t1, t3);
    t1 = _mm_xor_si128(t1, t2);
    return t1;
}

static inline __m128i
aesni_256_assist_middle(__m128i t1, __m128i t3) {
    __m128i t2 = _mm_aeskeygenassist_si128(t3, 0x00);
    t2 = _mm_shuffle_epi32(t2, 0xaa);
    __m128i t4 = _mm_slli_si128(t1, 0x04);
    t1 = _mm_xor_si128(t1, t4);
    t4 = _mm_slli_si128(t4, 0x04);
    t1 = _mm_xor_si128(t1, t4);
    t4 = _mm_slli_si128(t4, 0x04);
    t1 = _mm_xor_si128(t1, t4);
    t1 = _mm_xor_si128(t1, t2);
    return t1;
}

void
aesni_expand_key_256(AESNIContext& ctx, const uint8* key)
{
    __m128i t1, t2, t3;
    
    // Round 0 e 1 (Caricamento chiave originale 32 byte)
    ctx.encRoundKeys[0] = t1 = _mm_loadu_si128((const __m128i*)key);
    ctx.encRoundKeys[1] = t3 = _mm_loadu_si128((const __m128i*)(key + 16));

    // Round 2 e 3
    t2 = _mm_aeskeygenassist_si128(t3, 0x01);
    ctx.encRoundKeys[2] = t1 = aesni_256_assist(t1, t2);
    ctx.encRoundKeys[3] = t3 = aesni_256_assist_middle(t3, t1);

    // Round 4 e 5
    t2 = _mm_aeskeygenassist_si128(t3, 0x02);
    ctx.encRoundKeys[4] = t1 = aesni_256_assist(t1, t2);
    ctx.encRoundKeys[5] = t3 = aesni_256_assist_middle(t3, t1);

    // Round 6 e 7
    t2 = _mm_aeskeygenassist_si128(t3, 0x04);
    ctx.encRoundKeys[6] = t1 = aesni_256_assist(t1, t2);
    ctx.encRoundKeys[7] = t3 = aesni_256_assist_middle(t3, t1);

    // Round 8 e 9
    t2 = _mm_aeskeygenassist_si128(t3, 0x08);
    ctx.encRoundKeys[8] = t1 = aesni_256_assist(t1, t2);
    ctx.encRoundKeys[9] = t3 = aesni_256_assist_middle(t3, t1);

    // Round 10 e 11
    t2 = _mm_aeskeygenassist_si128(t3, 0x10);
    ctx.encRoundKeys[10] = t1 = aesni_256_assist(t1, t2);
    ctx.encRoundKeys[11] = t3 = aesni_256_assist_middle(t3, t1);

    // Round 12 e 13
    t2 = _mm_aeskeygenassist_si128(t3, 0x20);
    ctx.encRoundKeys[12] = t1 = aesni_256_assist(t1, t2);
    ctx.encRoundKeys[13] = t3 = aesni_256_assist_middle(t3, t1);

    // Round 14 (Finale per AES-256)
    t2 = _mm_aeskeygenassist_si128(t3, 0x40);
    ctx.encRoundKeys[14] = t1 = aesni_256_assist(t1, t2);
    
    ctx.rounds = 14;
}

static status_t
aesni_expand_key(AESNIContext& ctx, const uint8* key, size_t keyLength)
{
    if (keyLength == 16)
        aesni_expand_key_128(ctx, key);
    else if (keyLength == 24)
        aesni_expand_key_192(ctx, key);
    else if (keyLength == 32)
        aesni_expand_key_256(ctx, key);
    else
        return B_BAD_VALUE;

    /* build decrypt round keys */
    ctx.decRoundKeys[0] = ctx.encRoundKeys[ctx.rounds];
    for (int i = 1; i < (int)ctx.rounds; i++)
        ctx.decRoundKeys[i] = _mm_aesimc_si128(ctx.encRoundKeys[ctx.rounds - i]);
    ctx.decRoundKeys[ctx.rounds] = ctx.encRoundKeys[0];

    return B_OK;
}

/* --- Core AES Engine --- */
/* ------------------------------------------------------------- */
/* AES-NI CBC processing                                         */
/* ------------------------------------------------------------- */
static status_t
aesni_process_cbc(bool encrypt, AESNIContext* ctx, const uint8* in, uint8* out, size_t length)
{
    if (length % 16 != 0) return B_BAD_VALUE;
    size_t blocks = length / 16;

    __m128i iv = _mm_loadu_si128((const __m128i*)ctx->iv);

    for (size_t i = 0; i < blocks; i++) {
        __m128i block = _mm_loadu_si128((const __m128i*)(in + i * 16));
        
        if (encrypt) {
            block = _mm_xor_si128(block, iv);
            block = _mm_xor_si128(block, ctx->encRoundKeys[0]);
            for (int r = 1; r < (int)ctx->rounds; r++)
                block = _mm_aesenc_si128(block, ctx->encRoundKeys[r]);
            block = _mm_aesenclast_si128(block, ctx->encRoundKeys[ctx->rounds]);
            iv = block;
        } else {
            __m128i tmp = block;
            block = _mm_xor_si128(block, ctx->decRoundKeys[0]);
            for (int r = 1; r < (int)ctx->rounds; r++)
                block = _mm_aesdec_si128(block, ctx->decRoundKeys[r]);
            block = _mm_aesdeclast_si128(block, ctx->decRoundKeys[ctx->rounds]);
            block = _mm_xor_si128(block, iv);
            iv = tmp;
        }
        _mm_storeu_si128((__m128i*)(out + i * 16), block);
    }
    _mm_storeu_si128((__m128i*)ctx->iv, iv);
    return B_OK;
}

static status_t
aesni_process_ecb(bool encrypt,
                  AESNIContext* ctx,
                  const uint8* in,
                  uint8* out,
                  size_t length)
{
    if (length % 16 != 0)
        return B_BAD_VALUE;

    size_t blocks = length / 16;

    for (size_t i = 0; i < blocks; i++) {
        __m128i block = _mm_loadu_si128((const __m128i*)(in + i * 16));

        if (encrypt) {
            block = _mm_xor_si128(block, ctx->encRoundKeys[0]);
            for (int r = 1; r < (int)ctx->rounds; r++)
                block = _mm_aesenc_si128(block, ctx->encRoundKeys[r]);
            block = _mm_aesenclast_si128(block, ctx->encRoundKeys[ctx->rounds]);
        } else {
            block = _mm_xor_si128(block, ctx->decRoundKeys[0]);
            for (int r = 1; r < (int)ctx->rounds; r++)
                block = _mm_aesdec_si128(block, ctx->decRoundKeys[r]);
            block = _mm_aesdeclast_si128(block, ctx->decRoundKeys[ctx->rounds]);
        }

        _mm_storeu_si128((__m128i*)(out + i * 16), block);
    }

    return B_OK;
}


static inline void
aesni_increment_ctr(__m128i& ctr)
{
    const __m128i one = _mm_set_epi64x(0, 1);
    ctr = _mm_add_epi64(ctr, one);
}


static status_t
aesni_process_ctr(AESNIContext* ctx,
                  const uint8* in,
                  uint8* out,
                  size_t length)
{
    __m128i ctr = _mm_loadu_si128((const __m128i*)ctx->iv);

    for (size_t offset = 0; offset < length; offset += 16) {
        __m128i stream = ctr;

        stream = _mm_xor_si128(stream, ctx->encRoundKeys[0]);
        for (int r = 1; r < (int)ctx->rounds; r++)
            stream = _mm_aesenc_si128(stream, ctx->encRoundKeys[r]);
        stream = _mm_aesenclast_si128(stream, ctx->encRoundKeys[ctx->rounds]);

        __m128i data = _mm_loadu_si128((const __m128i*)(in + offset));
        data = _mm_xor_si128(data, stream);

        _mm_storeu_si128((__m128i*)(out + offset), data);

        aesni_increment_ctr(ctr);
    }

    _mm_storeu_si128((__m128i*)ctx->iv, ctr);
    return B_OK;
}

static inline void
aesni_encrypt_block_xmm(const AESNIContext* ctx, const uint8* in, uint8* out)
{
    __m128i block = _mm_loadu_si128((const __m128i*)in);
    block = _mm_xor_si128(block, ctx->encRoundKeys[0]);
    for (int i = 1; i < ctx->rounds; i++)
        block = _mm_aesenc_si128(block, ctx->encRoundKeys[i]);
    block = _mm_aesenclast_si128(block, ctx->encRoundKeys[ctx->rounds]);
    _mm_storeu_si128((__m128i*)out, block);
}

/* Update CTR: cifra un chunk (max 16 byte) e incrementa il contatore */
static void
aesni_ctr_process_chunk(AESNIContext* ctx, uint8 counter[16], const uint8* src, uint8* dst, size_t len)
{
    alignas(16) uint8 keystream[16];
    aesni_encrypt_block_xmm(ctx, counter, keystream);

    for (size_t i = 0; i < len; i++) {
        dst[i] = src[i] ^ keystream[i];
    }
    
    aesni_increment_gcm_ctr(counter);
}

/* --- GCM Logic --- */
static void
ghash_update_internal(uint8 tag_acc[16], const uint8 h_key[16], const uint8* data, size_t len)
{
    size_t pos = 0;
    while (pos < len) {
        size_t chunk = (len - pos < 16) ? len - pos : 16;
        uint8 partial[16] = {0};
        memcpy(partial, data + pos, chunk);

        for (int j = 0; j < 16; j++)
            tag_acc[j] ^= partial[j];

        // Qui usiamo la TUA soft_ghash_multiply che funziona!
        //sGCMHashFunc(tag_acc, h_key);
        soft_ghash_multiply(tag_acc, h_key);
        
        pos += chunk;
    }
}
static void
aesni_ctr_update_xmm(AESNIContext* ctx, uint8 counter[16], const uint8* src, uint8* dst, size_t len)
{
    size_t pos = 0;
    while (pos < len) {
        __m128i ctr_block = _mm_loadu_si128((const __m128i*)counter);
        
        // Cifratura del contatore
        ctr_block = _mm_xor_si128(ctr_block, ctx->encRoundKeys[0]);
        for (int i = 1; i < ctx->rounds; i++)
            ctr_block = _mm_aesenc_si128(ctr_block, ctx->encRoundKeys[i]);
        ctr_block = _mm_aesenclast_si128(ctr_block, ctx->encRoundKeys[ctx->rounds]);

        // XOR con i dati
        size_t chunk = (len - pos < 16) ? len - pos : 16;
        uint8 tmpIn[16] = {0};
        uint8 tmpOut[16];
        memcpy(tmpIn, src + pos, chunk);
        
        __m128i res = _mm_xor_si128(_mm_loadu_si128((__m128i*)tmpIn), ctr_block);
        _mm_storeu_si128((__m128i*)tmpOut, res);
        memcpy(dst + pos, tmpOut, chunk);

        // Incremento contatore (GCM standard: ultimi 4 byte BE)
        for (int j = 15; j >= 12; j--) {
            if (++counter[j] != 0) break;
        }
        pos += chunk;
    }
}
// helpers gcm accelerato ///
// Inverte i bit di ogni byte in un registro XMM
static inline __m128i mm_bit_reverse(__m128i in) {
    const __m128i mask_l = _mm_set1_epi8(0x0f);
    const __m128i rev_tab = _mm_set_epi8(
        0x0, 0x8, 0x4, 0xc, 0x2, 0xa, 0x6, 0xe,
        0x1, 0x9, 0x5, 0xd, 0x3, 0xb, 0x7, 0xf);
    
    __m128i low = _mm_shuffle_epi8(rev_tab, _mm_and_si128(in, mask_l));
    __m128i high = _mm_shuffle_epi8(rev_tab, _mm_and_si128(_mm_srli_epi16(in, 4), mask_l));
    
    return _mm_or_si128(_mm_slli_epi16(low, 4), _mm_srli_epi16(high, 4));
}

// Inversione totale dei bit di un registro a 128 bit (Specchio completo)
/* solo byte pari
static inline __m128i GCM_REVERSE(__m128i in) {
    // 1. Invertiamo l'ordine dei byte (BSWAP)
    const __m128i bswap_mask = _mm_set_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    __m128i x = _mm_shuffle_epi8(in, bswap_mask);
    
    // 2. Invertiamo i bit dentro ogni byte
    const __m128i mask_l = _mm_set1_epi8(0x0f);
    const __m128i rev_tab = _mm_set_epi8(
        0x0, 0x8, 0x4, 0xc, 0x2, 0xa, 0x6, 0xe,
        0x1, 0x9, 0x5, 0xd, 0x3, 0xb, 0x7, 0xf);
    
    __m128i low = _mm_shuffle_epi8(rev_tab, _mm_and_si128(x, mask_l));
    __m128i high = _mm_shuffle_epi8(rev_tab, _mm_and_si128(_mm_srli_epi16(x, 4), mask_l));
    
    // Ricostruiamo: il nibble basso va a sinistra (shift 4) e l'alto a destra
    return _mm_or_si128(_mm_slli_epi16(low, 4), _mm_srli_epi16(high, 4));
}*/
static inline __m128i GCM_REVERSE(__m128i in) {
    // Inverte solo l'ordine dei byte (0->15, 1->14, etc)
    return _mm_shuffle_epi8(in, _mm_set_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15));
}

static inline __m128i 
internal_ghash_mul_only_xmm(__m128i a, __m128i b) 
{
    __m128i tmp3, tmp4, tmp5, tmp6, tmp7, tmp8;
    __m128i pol = _mm_set_epi64x(0x0000000000000001ULL, 0xc200000000000000ULL);

    // 1. Moltiplicazione Karatsuba (Invariata, è corretta)
    tmp3 = _mm_clmulepi64_si128(a, b, 0x00);
    tmp6 = _mm_clmulepi64_si128(a, b, 0x11);
    tmp4 = _mm_clmulepi64_si128(a, b, 0x10);
    tmp5 = _mm_clmulepi64_si128(a, b, 0x01);
    tmp4 = _mm_xor_si128(tmp4, tmp5);
    
    tmp3 = _mm_xor_si128(tmp3, _mm_slli_si128(tmp4, 8));
    tmp6 = _mm_xor_si128(tmp6, _mm_srli_si128(tmp4, 8));

    // 2. Riduzione Barrett (CORRETTA)
    // Primo stadio: sposta i bit bassi verso l'alto usando il polinomio
    tmp7 = _mm_clmulepi64_si128(tmp3, pol, 0x10); 
    tmp3 = _mm_xor_si128(tmp3, _mm_slli_si128(tmp7, 8));
    tmp6 = _mm_xor_si128(tmp6, _mm_srli_si128(tmp7, 8));
    
    // Secondo stadio: moltiplica la parte bassa "ripulita" e XORa con la parte alta
    tmp8 = _mm_clmulepi64_si128(tmp3, pol, 0x00);
    
    // Il risultato finale è solo lo XOR tra la parte alta originale e la riduzione
    return _mm_xor_si128(tmp6, tmp8); 
}
/////////////////////////////
static status_t
aesni_process_gcm(BCryptoRequest* request)
{
    AESNIContext* ctx = NULL;
    cpu_status cpu_state;
    status_t st = B_OK;
    bool encrypt;
    size_t dataVectorCount;
    size_t total_len = 0;
    
    // Buffer e variabili allineate
    alignas(16) uint8 h_raw[16] = {0};
    alignas(16) uint8 j0[16] = {0};
    alignas(16) uint8 ctr_curr[16] = {0};
    alignas(16) uint8 s0[16] = {0};
    //alignas(16) uint8 final_tag[16] = {0};
    alignas(16) uint8 len_blk[16] = {0};
    uint8 tag_acc[16] = {0};//{0xFF, 0xFF, 0xFF,0xFF, 0xFF, 0xFF,0xFF, 0xFF, 0xFF,0xFF, 0xFF, 0xFF,0xFF, 0xFF, 0xFF, 0xFF};//
    
    // Registri XMM
    __m128i h_ref;
    __m128i acc;
    __m128i b_ref;
    __m128i final_ghash;
    __m128i tag_res;
    
    // Variabili di loop
    size_t i, pos, j;
    uint8 *src, *dst;
    size_t vector_len, chunk;
    //uint64 total_bits;
    
    dprintf("AESNI: VectorCount = %llu\n", (unsigned long long)request->vectorCount);
    dprintf("AESNI: Tag Destination Ptr = %p\n", request->destination[request->vectorCount - 1].iov_base);
    
    // AAD
    size_t aad_len = request->aadLength;
    uint8* aad_ptr = (uint8*)request->aad;
    //size_t total_data_len = 0;
    

    ctx = (AESNIContext*)memalign(16, sizeof(AESNIContext));
    if (!ctx) return B_NO_MEMORY;
    memset(ctx, 0, sizeof(AESNIContext));

    cpu_state = disable_interrupts();
    if (!bcrypto_save_regs(&ctx->fpu_save)) {
        restore_interrupts(cpu_state);
        free(ctx);
        return B_ERROR;
    }

    encrypt = (request->operation == B_CRYPTO_ENCRYPT);
    st = aesni_expand_key(*ctx, (const uint8*)request->key, request->keyLength);
    if (st != B_OK) goto cleanup;

    // 1. Setup Chiave H e J0
    aesni_encrypt_block_xmm(ctx, h_raw, h_raw);
    
    if (request->ivLength == 12) {
        memcpy(j0, request->iv, 12);
        j0[15] = 1;
    } else {
        st = B_NOT_SUPPORTED;
        goto cleanup;
    }

    memcpy(ctr_curr, j0, 16);
    aesni_increment_gcm_ctr(ctr_curr);
    
    dataVectorCount = request->vectorCount - 1;

    if (ghash_accel) {
        // --- RAMO ACCELERATO (PCLMULQDQ) ---
        __m128i h_temp = _mm_loadu_si128((__m128i*)h_raw);
        h_temp = GCM_REVERSE(h_temp);
        
        // Shift a sinistra di 1 bit (fondamentale per allineamento GF128)
        h_ref = _mm_or_si128(
            _mm_slli_epi64(h_temp, 1), 
            _mm_srli_epi64(_mm_slli_si128(h_temp, 8), 63)
        );
        /*
        h_ref = _mm_or_si128(
            _mm_srli_epi64(h_temp, 1), 
            _mm_slli_epi64(_mm_srli_si128(h_temp, 8), 63)
        );*/
        //secondo claude:
        // CORRETTO: h_ref è semplicemente H byte-reversed
        //h_ref = h_temp;   // = GCM_REVERSE(h_raw), senza nessuno shift
        
        acc = _mm_setzero_si128();
        
        if (aad_ptr && aad_len > 0) {
            dprintf("AESNI: AAD ptr %p, len %zu\n", aad_ptr, aad_len);
            pos = 0;
            while (pos < aad_len) {
                chunk = (aad_len - pos < 16) ? aad_len - pos : 16;
                alignas(16) uint8 tmp_aad[16] = {0};
                memcpy(tmp_aad, aad_ptr + pos, chunk);

                b_ref = GCM_REVERSE(_mm_loadu_si128((__m128i*)tmp_aad));
                acc = _mm_xor_si128(acc, b_ref);
                acc = internal_ghash_mul_only_xmm(acc, h_ref);
                pos += chunk;
            }
        }

        for (i = 0; i < dataVectorCount; i++) {
            src = (uint8*)request->source[i].iov_base;
            dst = (uint8*)request->destination[i].iov_base;
            vector_len = request->source[i].iov_len;
            if (vector_len == 0) continue;

            pos = 0;
            while (pos < vector_len) {
                chunk = (vector_len - pos < 16) ? vector_len - pos : 16;
                alignas(16) uint8 tmp_in[16] = {0};
                alignas(16) uint8 tmp_out[16] = {0};
                alignas(16) uint8 block_to_hash[16] = {0};

                memcpy(tmp_in, src + pos, chunk);

                if (encrypt) {
                    aesni_encrypt_block_xmm(ctx, ctr_curr, tmp_out); 
                    for(j = 0; j < chunk; j++) tmp_out[j] ^= tmp_in[j];
                    memcpy(dst + pos, tmp_out, chunk);
                    
                    // GHASH vuole il ciphertext completato a zero
                    memcpy(block_to_hash, tmp_out, chunk);
                } else {
                    // Decifratura: GHASH vuole il ciphertext in ingresso
                    memcpy(block_to_hash, tmp_in, chunk);

                    aesni_encrypt_block_xmm(ctx, ctr_curr, tmp_out);
                    for(j = 0; j < chunk; j++) tmp_out[j] ^= tmp_in[j];
                    memcpy(dst + pos, tmp_out, chunk);
                }

                // Inversione e Accumulo (lavoriamo sempre su 16 byte puliti)
                b_ref = GCM_REVERSE(_mm_loadu_si128((__m128i*)block_to_hash));
                //b_ref = _mm_loadu_si128((__m128i*)block_to_hash);
                acc = _mm_xor_si128(acc, b_ref);
                acc = internal_ghash_mul_only_xmm(acc, h_ref);

                aesni_increment_gcm_ctr(ctr_curr);
                pos += chunk;
            }
            total_len += vector_len;
        }
        //dprintf("AESNI DEBUG: aad_ptr=%p, aad_len=%zu, total_len=%zu\n", aad_ptr, aad_len, total_len);

        uint64 aad_bits = (uint64)aad_len * 8;
        uint64 data_bits = (uint64)total_len * 8;

        alignas(16) uint64 len_buffer[2];
        len_buffer[0] = __builtin_bswap64(aad_bits);  // AAD nei primi 8 byte (BE)
        len_buffer[1] = __builtin_bswap64(data_bits); // DATA nei secondi 8 byte (BE)

        __m128i len_v_be = _mm_loadu_si128((__m128i*)len_buffer);
        //acc = _mm_xor_si128(acc, len_v_be);
        // secondo claude:
        acc = _mm_xor_si128(acc, GCM_REVERSE(len_v_be));
        acc = internal_ghash_mul_only_xmm(acc, h_ref);

        // --- FASE 4: Tag finale ---
        final_ghash = GCM_REVERSE(acc);
        aesni_encrypt_block_xmm(ctx, j0, s0);
        tag_res = _mm_xor_si128(final_ghash, _mm_loadu_si128((__m128i*)s0));
        
        alignas(16) uint8 temp_tag[16];
        _mm_storeu_si128((__m128i*)temp_tag, tag_res);
    
        // Scrittura nel buffer del tag (l'ultimo vettore della richiesta)
        //uint8* tag_out = (uint8*)request->destination[request->vectorCount - 1].iov_base;
        //_mm_storeu_si128((__m128i*)tag_out, tag_res);
        if (request->vectorCount > 0) {
            iovec* tag_vec = &request->destination[request->vectorCount - 1];
            uint8* tag_out = (uint8*)tag_vec->iov_base;
            
            if (tag_out != NULL) {
                _mm_storeu_si128((__m128i*)tag_out, tag_res);
                //memcpy(tag_out, temp_tag, 16);
                if (!encrypt) {
                    uint8* tag_expected = (uint8*)request->source[request->vectorCount - 1].iov_base;
                    alignas(16) uint8 tag_calc[16];
                    _mm_storeu_si128((__m128i*)tag_calc, tag_res);
                    
                    if (memcmp(tag_calc, tag_expected, 16) != 0) {
                        st = B_BAD_DATA; // Tag non corrispondente!
                    }
                } else {
                    dprintf("AESNI: Tag copiato a %p, primo byte: %02x\n", tag_out, tag_out[0]);
                }
            } else {
                dprintf("AESNI: ERRORE! tag_out è NULL\n");
            }
        }
    } else {
        // --- RAMO SOFTWARE (Fallback) ---
        
        if (aad_ptr && aad_len > 0) {
        	//dprintf("AESNI: Rilevato AAD fasullo? len: %zu\n", request->aadLength);
            
            pos = 0;
            while (pos < aad_len) {
                chunk = (aad_len - pos < 16) ? aad_len - pos : 16;
                alignas(16) uint8 tmp_aad[16] = {0};
                memcpy(tmp_aad, aad_ptr + pos, chunk);
                
                ghash_update_internal(tag_acc, h_raw, tmp_aad, 16); // Sempre 16 byte (padding a zero)
                pos += chunk;
            }
        }
        
        for (i = 0; i < dataVectorCount; i++) {
            src = (uint8*)request->source[i].iov_base;
            dst = (uint8*)request->destination[i].iov_base;
            vector_len = request->source[i].iov_len;
            if (vector_len == 0) continue;

            if (encrypt) {
                aesni_ctr_update_xmm(ctx, ctr_curr, src, dst, vector_len);
                ghash_update_internal(tag_acc, h_raw, dst, vector_len);
            } else {
                ghash_update_internal(tag_acc, h_raw, src, vector_len);
                aesni_ctr_update_xmm(ctx, ctr_curr, src, dst, vector_len);
            }
            total_len += vector_len;
        }

        memset(len_blk, 0, 16);
        
        uint64 aad_bits = (uint64)aad_len * 8;
        uint64 data_bits = (uint64)total_len * 8;
        // GCM vuole [AAD_bits (64 bit BE) | DATA_bits (64 bit BE)]
        for (j = 0; j < 8; j++) {
            len_blk[7 - j] = (aad_bits >> (j * 8)) & 0xFF;
            len_blk[15 - j] = (data_bits >> (j * 8)) & 0xFF;
        }
        
        /* senza AAD
        total_bits = (uint64)total_len * 8;
        for (j = 0; j < 8; j++) {
            len_blk[15 - j] = (total_bits >> (j * 8)) & 0xFF;
        }
        */
        
        ghash_update_internal(tag_acc, h_raw, len_blk, 16);
        
        aesni_encrypt_block_xmm(ctx, j0, s0);
        for (j = 0; j < 16; j++) tag_acc[j] ^= s0[j];
        // USCITA
        dst = (uint8*)request->destination[dataVectorCount].iov_base;
        if (dst != NULL) {
                memcpy(dst, tag_acc, 16);
        }
        if (!encrypt) {
            src = (uint8*)request->source[dataVectorCount].iov_base;
            if (memcmp(tag_acc, src, 16) != 0) {
                st = B_BAD_DATA;
            }
        }
    }


cleanup:
    bcrypto_restore_regs(&ctx->fpu_save);
    restore_interrupts(cpu_state);
    secure_memzero(ctx, sizeof(AESNIContext));
    free(ctx);
    return st;
}


/* --- Dispatcher Principale --- */

static status_t
aesni_process(BCryptoRequest* request)
{
	AESNIContext* ctx = NULL;
    status_t st = B_OK;
    bool encrypt;
    
    //alignas(16) AESNIContext ctx{};
    //AESNIContext* ctx = (AESNIContext*)malloc_etc(sizeof(AESNIContext), 16, 0);
    ctx = (AESNIContext*)memalign(16, sizeof(AESNIContext));
    if (ctx == NULL)
        return B_NO_MEMORY;

    // Pulizia iniziale (fondamentale!)
    memset(ctx, 0, sizeof(AESNIContext));

    /* ---- validate algorithm ---- */
    if (request->ivLength > 0) {
        if (request->ivLength != 16) {
            st = B_BAD_VALUE;
            goto out;
        }
            //return B_BAD_VALUE;
        memcpy(ctx->iv, request->iv, 16);
    }
    
    /* ---- key expansion ---- */
    {
    	cpu_status cpu_state = disable_interrupts();
        //bcrypto_save_regs(&ctx->fpu_save);
        if (!bcrypto_save_regs(&ctx->fpu_save)) {
        	restore_interrupts(cpu_state);
        	dprintf("AESNI: Cannot save fpu regs");
        	return B_NO_MEMORY;
        }
    	st = aesni_expand_key(*ctx, (const uint8*)request->key, request->keyLength);
    	bcrypto_restore_regs(&ctx->fpu_save);
        restore_interrupts(cpu_state);
    }
    if (st != B_OK)
        goto out;

    encrypt = (request->operation == B_CRYPTO_ENCRYPT);
    //fpu_state_t fpu_save;

    for (size_t i = 0; i < request->vectorCount; i++) {
        const uint8* srcBase = (const uint8*)request->source[i].iov_base;
        uint8* dstBase = (uint8*)request->destination[i].iov_base;
        size_t remaining = request->source[i].iov_len;
    
        while (remaining > 0) {
            // Elaboriamo al massimo 32KB per volta prima di ridare respiro alla CPU
            size_t chunkSize = min_c(remaining, (size_t)32 * 1024);
            
            cpu_status cpu_state = disable_interrupts();
            //bcrypto_save_regs(&ctx->fpu_save);
            if (!bcrypto_save_regs(&ctx->fpu_save)) {
            	restore_interrupts(cpu_state);
            	dprintf("AESNI: Cannot save fpu regs");
            	return B_NO_MEMORY;
            }
    
            if (request->mode == B_CRYPTO_MODE_ECB) {
                st = aesni_process_ecb(encrypt, ctx, srcBase, dstBase, chunkSize);
            } else if (request->mode == B_CRYPTO_MODE_CBC) {
                st = aesni_process_cbc(encrypt, ctx, srcBase, dstBase, chunkSize);
            } else if (request->mode == B_CRYPTO_MODE_CTR) {
                st = aesni_process_ctr(ctx, srcBase, dstBase, chunkSize);
            } else {
                st = B_NOT_SUPPORTED;
            }
            
            bcrypto_restore_regs(&ctx->fpu_save);
            restore_interrupts(cpu_state);
    
            if (st != B_OK) goto out;
    
            remaining -= chunkSize;
            srcBase += chunkSize;
            dstBase += chunkSize;
        }
    }

    if (request->mode != B_CRYPTO_MODE_ECB && request->iv != NULL)
        memcpy(request->iv, ctx->iv, 16);

out:
    //secure_memzero(&ctx, sizeof(ctx));
    if (ctx != NULL) {
        secure_memzero(ctx, sizeof(AESNIContext)); // Azzera la struttura vera
        free(ctx);
    }
    return st;
}

static BCryptoAlgorithm sAESNI;
static BCryptoAlgorithm sAESNI_GCM;

status_t
BInitAESNICrypto()
{
	uint32 caps = BGetStoredCryptoCapabilities();
    if (!(caps & B_CRYPTO_HW_AES_NI))
        return B_UNSUPPORTED;
        
    ghash_accel = caps & B_CRYPTO_GHASH_PCLMULQDQ;
    /* per ora lasciamo ghash_accel a false
     * visto che non caviamo un ragno dal buco
     * quando troveremo la logica corretta o avremo
     * funzioni che sputano quel che serve
     * riattiveremo questa condizione
     */
    const char* gcm_name = ghash_accel ? "AES-GCM (AES-NI/PCLMULQDQ)" : "AES-GCM (AES-NI/GHASH software)";
    
    /*
    if (ghash_accel) {
    	// tentativo di creare una funzione 
    	// ghash accelerata compatibile con 
    	// quella software
        sGCMHashFunc = aesni_ghash_multiply;
    } else {
        sGCMHashFunc = soft_ghash_multiply;
    }*/

    sAESNI = {
        .algorithm = B_CRYPTO_AES,                               // ID Algoritmo
        .mode = (BCryptoMode)(B_CRYPTO_MODE_CBC | B_CRYPTO_MODE_ECB | B_CRYPTO_MODE_CTR), // Modi supportati
        .flags = B_CRYPTO_ALG_HW_ACCEL,                      // Flags
        .name      = "AES CBC-ECB-CTR (Intel AES-NI)",
        .priority = 90,                                         // Priorità (Alta perché hardware)
        .Process = aesni_process                               // Callback
    };

    status_t status =  BRegisterCryptoAlgorithm(&sAESNI);
    if (status != B_OK)
        return status;
    
    sAESNI_GCM = {
        .algorithm = B_CRYPTO_AES,
        .mode = B_CRYPTO_MODE_GCM,
        .flags = B_CRYPTO_ALG_HW_ACCEL,
        //.name = "AES-GCM (Hardware Accelerated)",
        .priority = ghash_accel ? 95 : 90,
        .Process = aesni_process_gcm
    };
    strlcpy(sAESNI_GCM.name, gcm_name, sizeof(sAESNI_GCM.name));
    return BRegisterCryptoAlgorithm(&sAESNI_GCM);
}
#else
status_t BInitAESNICrypto() { return B_NOT_SUPPORTED; }
#endif
