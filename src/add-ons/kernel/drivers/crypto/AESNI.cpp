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
#include <tmmintrin.h> //temp
#include <immintrin.h>
#include <malloc.h>
#include <stdlib.h> //per memalign
#include <debug.h>
#pragma GCC target("aes,sse4.2")

/* ----- debug ----- */
static void
dump_block(const char* label, const void* block) {
    const uint8* b = (const uint8*)block;
    dprintf("AESNI: %s: %02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n",
        label, b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
        b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
}
/* ------------------ */
static void
secure_memzero(void* p, size_t s)
{
    if (p == NULL)
        return;
    volatile uint8* cp = (volatile uint8*)p;
    while (s--)
        *cp++ = 0;
}

static ghash_multiply_func sGCMHashFunc = NULL;
static bool ghash_accel=false;
// Helper per riflettere i bit di un registro __m128i 
// (Necessario se la CPU non supporta istruzioni di riflessione rapida)
//static inline __m128i 
//ghash_reflect(__m128i x) {
//    // Swap dei byte per simulare la riflessione richiesta dal GCM
//    const __m128i mask = _mm_set_epi8(
//        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
//    return _mm_shuffle_epi8(x, mask);
//}

//static inline void
//aesni_increment_gcm_ctr(__m128i& ctr)
//{
//    // Il contatore GCM occupa i byte 12-15 del blocco IV
//    // Dobbiamo trattarli come un intero a 32-bit Big-Endian
//    alignas(16) uint8 bytes[16];
//    _mm_store_si128((__m128i*)bytes, ctr);
//
//    // Incremento manuale Big-Endian sui 4 byte finali
//    for (int i = 15; i >= 12; i--) {
//        if (++bytes[i] != 0)
//            break;
//    }
//
//    ctr = _mm_load_si128((__m128i*)bytes);
//}

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

__attribute__((target("pclmul,sse2,ssse3")))
static inline __m128i
aesni_ghash_core_xmm(__m128i x, __m128i h)
{
    // Moltiplicazione carry-less (4 moltiplicazioni per coprire i 128 bit)
    __m128i tmp0 = _mm_clmulepi64_si128(x, h, 0x00);
    __m128i tmp1 = _mm_clmulepi64_si128(x, h, 0x11);
    __m128i tmp2 = _mm_clmulepi64_si128(x, h, 0x10);
    __m128i tmp3 = _mm_clmulepi64_si128(x, h, 0x01);

    tmp2 = _mm_xor_si128(tmp2, tmp3);
    __m128i low = _mm_xor_si128(tmp0, _mm_slli_si128(tmp2, 8));
    __m128i high = _mm_xor_si128(tmp1, _mm_srli_si128(tmp2, 8));

    // Riduzione GCM standard (polinomio 0xE1 riflesso = 0xc200...)
    __m128i p = _mm_set_epi64x(0xc200000000000000ULL, 0);
    __m128i v0 = _mm_clmulepi64_si128(low, p, 0x00);
    __m128i v1 = _mm_xor_si128(low, _mm_slli_si128(v0, 8));
    
    __m128i v2 = _mm_clmulepi64_si128(v1, p, 0x00);
    __m128i v3 = _mm_clmulepi64_si128(v1, p, 0x10);
    
    return _mm_xor_si128(_mm_xor_si128(high, v3), _mm_srli_si128(v2, 8));
}

__attribute__((target("pclmul,sse2,ssse3")))
static void
aesni_ghash_multiply(uint8* x, const uint8* h)
{
    // Maschera per byte-reverse (come __builtin_bswap64 applicato a entrambi i qword)
    const __m128i BSWAP_MASK = _mm_set_epi8(0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15);

    // Caricamento e byte-swap per convertire da Big-Endian a Little-Endian
    __m128i a = _mm_shuffle_epi8(_mm_loadu_si128((__m128i*)x), BSWAP_MASK);
    __m128i b = _mm_shuffle_epi8(_mm_loadu_si128((__m128i*)h), BSWAP_MASK);

    // Moltiplicazione PCLMUL (4 parti per 128-bit × 128-bit)
    __m128i tmp0 = _mm_clmulepi64_si128(a, b, 0x00);
    __m128i tmp1 = _mm_clmulepi64_si128(a, b, 0x11);
    __m128i tmp2 = _mm_clmulepi64_si128(a, b, 0x10);
    __m128i tmp3 = _mm_clmulepi64_si128(a, b, 0x01);

    __m128i mid = _mm_xor_si128(tmp2, tmp3);
    __m128i low = _mm_xor_si128(tmp0, _mm_slli_si128(mid, 8));
    __m128i high = _mm_xor_si128(tmp1, _mm_srli_si128(mid, 8));

    // Riduzione GCM usando shift a DESTRA (come soft_gcm_shift_right)
    // Primo passo: riduci low con il polinomio GCM
    __m128i p = _mm_set_epi32(0xE1000000, 0, 0, 0);
    __m128i t0 = _mm_clmulepi64_si128(low, p, 0x10);
    __m128i v1 = _mm_xor_si128(low, _mm_slli_si128(t0, 8));
    
    // Secondo passo: riduzione finale
    __m128i t1 = _mm_clmulepi64_si128(v1, p, 0x10);
    __m128i res = _mm_xor_si128(_mm_xor_si128(high, t1), _mm_srli_si128(_mm_slli_si128(t0, 8), 8));

    // Salvataggio con byte-reverse finale per tornare a Big-Endian
    _mm_storeu_si128((__m128i*)x, _mm_shuffle_epi8(res, BSWAP_MASK));
}
/* versione clone di softcrypto */
static void
aesni_encrypt_block_xmm(AESNIContext* ctx, const uint8* in, uint8* out)
{
    __m128i block = _mm_loadu_si128((const __m128i*)in);
    block = _mm_xor_si128(block, ctx->encRoundKeys[0]);
    for (int i = 1; i < ctx->rounds; i++) {
        block = _mm_aesenc_si128(block, ctx->encRoundKeys[i]);
    }
    block = _mm_aesenclast_si128(block, ctx->encRoundKeys[ctx->rounds]);
    _mm_storeu_si128((__m128i*)out, block);
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
        sGCMHashFunc(tag_acc, h_key);
        
        pos += chunk;
    }
}

static status_t
aesni_process_gcm(BCryptoRequest* request)
{
	// 1. Setup Context (Usa la tua struttura AESNIContext)
    AESNIContext* ctx = (AESNIContext*)memalign(16, sizeof(AESNIContext));
    if (!ctx) return B_NO_MEMORY;
    memset(ctx, 0, sizeof(AESNIContext));

    cpu_status cpu_state = disable_interrupts();
    bcrypto_save_regs(&ctx->fpu_save);

    bool encrypt = (request->operation == B_CRYPTO_ENCRYPT);
    aesni_expand_key(*ctx, (const uint8*)request->key, request->keyLength);
    
    // 2. Calcolo H (Hash Key) = E(K, 0)
    uint8 h_key[16] = {0};
    aesni_encrypt_block_xmm(ctx, h_key, h_key); // Helper che fa i round AES

    // 3. Setup IV e J0 (Solo 12 byte supportati per ora)
    uint8 counter[16] = {0};
    uint8 j0[16] = {0};
    if (request->ivLength == 12) {
        memcpy(counter, request->iv, 12);
        counter[15] = 1;
        memcpy(j0, counter, 16);
        // Incremento contatore (GCM standard: gli ultimi 4 byte sono BE)
        for (int j = 15; j >= 12; j--) { if (++counter[j] != 0) break; }
    } else {
        // Cleanup e uscita se IV != 12
        bcrypto_restore_regs(&ctx->fpu_save);
        restore_interrupts(cpu_state);
        free(ctx);
        return B_NOT_SUPPORTED;
    }

    // 4. Accumulatore GHASH
    uint8 tag_acc[16] = {0};
    size_t total_len = 0;
    size_t dataVectorCount = request->vectorCount - 1;
	if (ghash_accel) {
        // --- PATH ACCELERATO: usa ghash_mul con H preprocessato ---
        // Preprocessing di H (come in stream_init)
        __m128i h_val = _mm_loadu_si128((__m128i*)h_key);
        h_val = _mm_shuffle_epi8(h_val, _mm_set_epi8(0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15));
        __m128i shift = _mm_srli_epi64(h_val, 63);
        __m128i carry = _mm_slli_si128(shift, 8);
        h_val = _mm_srli_epi64(h_val, 1);
        h_val = _mm_xor_si128(h_val, carry);
        
        __m128i acc = _mm_setzero_si128();

        // Loop di Processing
        for (size_t i = 0; i < dataVectorCount; i++) {
            uint8* src = (uint8*)request->source[i].iov_base;
            uint8* dst = (uint8*)request->destination[i].iov_base;
            size_t len = request->source[i].iov_len;
            if (len == 0) continue;
            total_len += len;

            // Cifratura CTR
            aesni_ctr_update_xmm(ctx, counter, src, dst, len);

            // GHASH Update sul ciphertext
            uint8* hash_src = encrypt ? dst : src;
            size_t pos = 0;
            while (pos < len) {
                size_t chunk = (len - pos < 16) ? len - pos : 16;
                __m128i data;
                if (chunk < 16) {
                    alignas(16) uint8 partial[16] = {0};
                    memcpy(partial, hash_src + pos, chunk);
                    data = _mm_load_si128((__m128i*)partial);
                } else {
                    data = _mm_loadu_si128((__m128i*)(hash_src + pos));
                }
                
                // XOR con accumulatore e moltiplica (come _aesni_gcm_update_internal)
                acc = _mm_xor_si128(acc, _mm_shuffle_epi8(data, _mm_set_epi8(0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15)));
                acc = ghash_mul(acc, h_val);
                
                pos += chunk;
            }
        }

        // Finalizzazione con blocco lunghezze
        alignas(16) uint8 lenBlock[16];
        uint64 aadLenBits = 0;
        uint64 cipherLenBits = total_len * 8;
        
        for (int i = 0; i < 8; i++) {
            lenBlock[i] = (aadLenBits >> (56 - i * 8)) & 0xFF;
            lenBlock[i + 8] = (cipherLenBits >> (56 - i * 8)) & 0xFF;
        }
        
        __m128i lb = _mm_load_si128((__m128i*)lenBlock);
        lb = _mm_shuffle_epi8(lb, _mm_set_epi8(0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15));
        acc = _mm_xor_si128(acc, lb);
        acc = ghash_mul(acc, h_val);
        
        // Riconverti e salva
        acc = _mm_shuffle_epi8(acc, _mm_set_epi8(0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15));
        _mm_storeu_si128((__m128i*)tag_acc, acc);

    } else {
        // 5. Loop di Processing (Replica esatta della logica SoftCrypto)
        for (size_t i = 0; i < dataVectorCount; i++) {
            uint8* src = (uint8*)request->source[i].iov_base;
            uint8* dst = (uint8*)request->destination[i].iov_base;
            size_t len = request->source[i].iov_len;
            if (len == 0) continue;
            total_len += len;

            if (encrypt) {
                // Cifra e poi aggiorna Hash con il DESTINATARIO (ciphertext)
                aesni_ctr_update_xmm(ctx, counter, src, dst, len);
                ghash_update_internal(tag_acc, h_key, dst, len);
            } else {
                // Aggiorna Hash con la SORGENTE (ciphertext) e poi decifra
                ghash_update_internal(tag_acc, h_key, src, len);
                aesni_ctr_update_xmm(ctx, counter, src, dst, len);
            }
        }

        // 6. Finalizzazione Tag (Identica alla tua versione Soft)
        uint8 len_block[16] = {0};
        uint64 data_bits = (uint64)total_len * 8;
        uint64 aad_bits = 0;

        for (int i = 0; i < 8; i++) {
            len_block[i] = (aad_bits >> (56 - i * 8)) & 0xFF;
            len_block[i + 8] = (data_bits >> (56 - i * 8)) & 0xFF;
        }
        
        ghash_update_internal(tag_acc, h_key, len_block, 16);

        
    }
    // 7. XOR finale con E(K, J0)
    uint8 s0[16];
    aesni_encrypt_block_xmm(ctx, j0, s0);
    for (int j = 0; j < 16; j++) tag_acc[j] ^= s0[j];

    // 8. Output
    status_t st = B_OK;
    if (encrypt) {
        uint8* outTag = (uint8*)request->destination[request->vectorCount - 1].iov_base;
        memcpy(outTag, tag_acc, 16);
    } else {
        uint8* providedTag = (uint8*)request->source[request->vectorCount - 1].iov_base;
        if (memcmp(tag_acc, providedTag, 16) != 0) st = B_BAD_DATA;
    }

    bcrypto_restore_regs(&ctx->fpu_save);
    restore_interrupts(cpu_state);
    free(ctx);
    return st;
}

/* -------------- STREAMING ---------------- */

static inline __m128i _mm_bswap_epi128(__m128i in) {
    return _mm_shuffle_epi8(in, _mm_set_epi8(
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15));
}

// Riduzione nel campo di Galois GF(2^128) - Il "motore" del GHASH
__attribute__((target("pclmul,aes,sse2")))
static inline __m128i ghash_mul(__m128i a, __m128i b) {
    __m128i tmp3, tmp4, tmp5, tmp6, tmp7, tmp8;

    // Moltiplicazione 64x64 -> 128 (4 parti)
    tmp3 = _mm_clmulepi64_si128(a, b, 0x00);
    tmp4 = _mm_clmulepi64_si128(a, b, 0x11);
    tmp5 = _mm_clmulepi64_si128(a, b, 0x01);
    tmp6 = _mm_clmulepi64_si128(a, b, 0x10);

    tmp5 = _mm_xor_si128(tmp5, tmp6);
    tmp6 = _mm_slli_si128(tmp5, 8);
    tmp5 = _mm_srli_si128(tmp5, 8);
    tmp3 = _mm_xor_si128(tmp3, tmp6);
    tmp4 = _mm_xor_si128(tmp4, tmp5);

    // Riduzione GCM Polinomiale (Specifica per bit-reflected)
    tmp7 = _mm_slli_epi64(tmp3, 1);
    tmp8 = _mm_slli_epi64(tmp3, 2);
    tmp7 = _mm_xor_si128(tmp7, tmp8);
    tmp8 = _mm_slli_epi64(tmp3, 7);
    tmp7 = _mm_xor_si128(tmp7, tmp8);
    tmp8 = _mm_slli_si128(tmp7, 8);
    tmp7 = _mm_srli_si128(tmp7, 8);
    tmp3 = _mm_xor_si128(tmp3, tmp8);
    tmp4 = _mm_xor_si128(tmp4, tmp7);

    tmp7 = _mm_srli_epi64(tmp3, 63);
    tmp8 = _mm_srli_epi64(tmp3, 62);
    tmp7 = _mm_xor_si128(tmp7, tmp8);
    tmp8 = _mm_srli_epi64(tmp3, 57);
    tmp7 = _mm_xor_si128(tmp7, tmp8);

    return _mm_xor_si128(tmp4, tmp7);
}
static void
aesni_encrypt_block(AESNIContext* ctx, const uint8* src, uint8* dst)
{
    __m128i block = _mm_loadu_si128((const __m128i*)src);
    block = _mm_xor_si128(block, ctx->encRoundKeys[0]);
    
    for (int i = 1; i < ctx->rounds; i++) {
        block = _mm_aesenc_si128(block, ctx->encRoundKeys[i]);
    }
    block = _mm_aesenclast_si128(block, ctx->encRoundKeys[ctx->rounds]);
    
    _mm_storeu_si128((__m128i*)dst, block);
}
static void
aesni_ghash_update_block(uint8* tag_acc, const uint8* h_key, const uint8* data)
{
    // Carichiamo l'accumulatore attuale, la chiave H e il nuovo blocco
    __m128i acc = _mm_loadu_si128((__m128i*)tag_acc);
    __m128i h = _mm_loadu_si128((__m128i*)h_key);
    __m128i d = _mm_loadu_si128((__m128i*)data);

    // GCM GHASH: acc = (acc ^ data) * h
    // Ricordiamoci il bswap perché GCM è big-endian sui blocchi
    acc = _mm_xor_si128(acc, _mm_bswap_epi128(d));
    acc = ghash_mul(acc, h);

    // Salviamo l'accumulatore aggiornato
    _mm_storeu_si128((__m128i*)tag_acc, acc);
}
__attribute__((target("pclmul,aes")))
static void
_aesni_gcm_update_internal(SoftAEADContext* aead, const uint8* src, uint8* dst, size_t len)
{
	AESNIContext* aes = (AESNIContext*)aead->cipher_ctx;
    GCMState* gcm = (GCMState*)aead->auth_ctx;
    bool encrypt = aead->is_encrypting;
    // Usiamo loadu (unaligned) per sicurezza sui puntatori esterni
    __m128i h = _mm_loadu_si128((__m128i*)gcm->h_key);
    __m128i acc = _mm_loadu_si128((__m128i*)gcm->tag_acc);
    //__m128i ctr = _mm_bswap_epi128(_mm_loadu_si128((__m128i*)gcm->counter));

    __m128i keys[15];
    for (int i = 0; i <= aes->rounds; i++) keys[i] = aes->encRoundKeys[i];

    uint32* ctrPtr = (uint32*)gcm->counter;
    uint32 c = __builtin_bswap32(ctrPtr[3]); // Leggi gli ultimi 32 bit (BE)
    
    while (len >= 16) {
        // 1. Genera Keystream
        __m128i currentCtr = _mm_loadu_si128((__m128i*)gcm->counter);
        __m128i ks = currentCtr;
        
        //__m128i ks = _mm_bswap_epi128(ctr);
        ks = _mm_xor_si128(ks, keys[0]);
        for (int j = 1; j < aes->rounds; j++) 
            ks = _mm_aesenc_si128(ks, keys[j]);
        ks = _mm_aesenclast_si128(ks, keys[aes->rounds]);

        __m128i data = _mm_loadu_si128((__m128i*)src);
        __m128i result = _mm_xor_si128(data, ks);
        
        // 2. Logica AEAD: GHASH deve "vedere" sempre il ciphertext
        if (encrypt) {
            // Se cifriamo, il ciphertext è il RISULTATO
            _mm_storeu_si128((__m128i*)dst, result);
            acc = _mm_xor_si128(acc, _mm_bswap_epi128(result));
        } else {
            // Se decifriamo, il ciphertext è l'INPUT
            acc = _mm_xor_si128(acc, _mm_bswap_epi128(data));
            _mm_storeu_si128((__m128i*)dst, result);
        }
        
        if (aead->total_len == 0) { 
            dump_block(encrypt ? "ENC-Input (Plain)" : "DEC-Input (Cipher)", src);
            dump_block(encrypt ? "ENC-Output (Cipher)" : "DEC-Output (Plain)", dst);
            // dump_block("Accumulatore GHASH", &acc); // Richiederebbe cast
            alignas(16) uint8 tempAcc[16];
            _mm_store_si128((__m128i*)tempAcc, acc);
            dump_block("Accumulatore GHASH (corrente)", tempAcc);
        }
        
        acc = ghash_mul(acc, h);

        // 3. Incremento Counter (solo i 32 bit bassi/finali)
        //ctr = _mm_add_epi32(ctr, _mm_set_epi32(1, 0, 0, 0));
        c++;
        ctrPtr[3] = __builtin_bswap32(c); // Scrivi indietro in Big-Endian

        src += 16;
        dst += 16;
        len -= 16;
    }

    // Gestione Residuo
    if (len > 0) {
        alignas(16) uint8 partialBlock[16] = {0};
        alignas(16) uint8 keystream[16];

        //__m128i ks = _mm_bswap_epi128(ctr);
        __m128i ks = _mm_loadu_si128((__m128i*)gcm->counter);
        
        ks = _mm_xor_si128(ks, keys[0]);
        for (int j = 1; j < aes->rounds; j++) ks = _mm_aesenc_si128(ks, keys[j]);
        ks = _mm_aesenclast_si128(ks, keys[aes->rounds]);
        _mm_store_si128((__m128i*)keystream, ks);

        for (size_t i = 0; i < len; i++) {
            uint8 c;
            if (encrypt) {
                c = src[i] ^ keystream[i]; // Ciphertext = result
                dst[i] = c;
            } else {
                c = src[i];               // Ciphertext = input
                dst[i] = src[i] ^ keystream[i];
            }
            partialBlock[i] = c;
        }

        __m128i pData = _mm_load_si128((__m128i*)partialBlock);
        acc = _mm_xor_si128(acc, _mm_bswap_epi128(pData));
        acc = ghash_mul(acc, h);
    }

    // Salvataggio stato finale nel contesto
    _mm_storeu_si128((__m128i*)gcm->tag_acc, acc);
    //_mm_storeu_si128((__m128i*)gcm->counter, _mm_bswap_epi128(ctr));
}

static status_t
aesni_aes_gcm_stream_init(void** context, size_t* _contextSize, BCryptoOperation op, 
                          const uint8* key, size_t keyLen, const uint8* iv, size_t ivLen)
{
    if (!context || !key || !iv || ivLen != 12) return B_BAD_VALUE;

    SoftAEADContext* aead = (SoftAEADContext*)malloc(sizeof(SoftAEADContext));
    //AESNIContext* aes = (AESNIContext*)malloc(sizeof(AESNIContext));
    AESNIContext* aes = (AESNIContext*)memalign(64, sizeof(AESNIContext));
    //GCMState* gcm = (GCMState*)malloc(sizeof(GCMState));
    GCMState* gcm = (GCMState*)memalign(16, sizeof(GCMState));

    if (!aead || !aes || !gcm) {
        free(aead); free(aes); free(gcm);
        return B_NO_MEMORY;
    }

    memset(aead, 0, sizeof(SoftAEADContext));
    memset(aes, 0, sizeof(AESNIContext));
    memset(gcm, 0, sizeof(GCMState));
    
    cpu_status cpu_state = disable_interrupts();
    if (!bcrypto_save_regs(&aes->fpu_save)) {
        restore_interrupts(cpu_state);
        dprintf("AESNI: Errore critico - Impossibile salvare i registri FPU\n");
        return B_ERROR;
    }

    // Inizializza AES-NI (Espansione chiavi)
    aesni_expand_key(*aes, key, keyLen);

    // Entriamo in modalità FPU per calcolare H usando AES-NI
    
    
    // H = AES_encrypt(0)
    uint8 zero[16] = {0};
    aesni_encrypt_block(aes, zero, gcm->h_key);
    
    __m128i h_val = _mm_loadu_si128((__m128i*)gcm->h_key);
    
    // Invertiamo l'ordine dei byte (GCM Big Endian -> Little Endian per la CPU)
    h_val = _mm_shuffle_epi8(h_val, _mm_set_epi8(0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15));

    
    // Shift a sinistra di 1 bit con gestione del carry tra i due qword da 64 bit
    //__m128i h_hi = _mm_srli_epi64(h_val, 63);
    //__m128i h_lo = _mm_slli_epi64(h_val, 1);
    //h_hi = _mm_slli_si128(h_hi, 8);
    //h_val = _mm_or_si128(h_lo, h_hi);
    
    // SHIFT A DESTRA (NIST GCM standard per moltiplicazione riflessa)
__m128i shift = _mm_srli_epi64(h_val, 63);
__m128i carry = _mm_slli_si128(shift, 8); // Sposta il bit dal low al high qword
h_val = _mm_srli_epi64(h_val, 1);
h_val = _mm_xor_si128(h_val, carry);
    
    _mm_storeu_si128((__m128i*)gcm->h_key, h_val);

    bcrypto_restore_regs(&aes->fpu_save);
    restore_interrupts(cpu_state);

    // Setup J0 e Counter
    memcpy(gcm->j0, iv, 12);
    gcm->j0[15] = 1;
    memcpy(gcm->counter, gcm->j0, 16);
    // Incrementiamo il counter per il primo blocco dati (J0 + 1)
    for (int i = 15; i >= 12; i--) { if (++gcm->counter[i] != 0) break; }

    aead->cipher_ctx = aes;
    aead->auth_ctx = gcm;
    aead->is_encrypting = (op == B_CRYPTO_ENCRYPT);
    aead->total_len = 0;

    *context = aead;
    *_contextSize = sizeof(SoftAEADContext);
    
    dprintf("AESNI: Init %s - IV: %02x%02x%02x%02x...\n", 
    aead->is_encrypting ? "ENC" : "DEC", iv[0], iv[1], iv[2], iv[3]);
    dump_block("H-Key Preparata", gcm->h_key);
    dump_block("J0", gcm->j0);
    dump_block("Primo Counter (J0+1)", gcm->counter);
    
    return B_OK;
}
static status_t
aesni_aes_gcm_stream_update(void* context, const iovec* inVec, const iovec* outVec, size_t vecCount)
{
    SoftAEADContext* aead = (SoftAEADContext*)context;
    AESNIContext* aes = (AESNIContext*)aead->cipher_ctx;
    //GCMState* gcm = (GCMState*)aead->auth_ctx;

    // 1. Disabilitiamo interrupt e salviamo i registri FPU/SSE/AVX
    cpu_status cpu_state = disable_interrupts();
    if (!bcrypto_save_regs(&aes->fpu_save)) {
        restore_interrupts(cpu_state);
        dprintf("AESNI: Errore critico - Impossibile salvare i registri FPU\n");
        return B_ERROR;
    }

    // 2. Loop sui vettori di dati
    for (size_t i = 0; i < vecCount; i++) {
        const uint8* src = (const uint8*)inVec[i].iov_base;
        uint8* dst = (uint8*)outVec[i].iov_base;
        size_t len = inVec[i].iov_len;

        // Questa è la funzione che scriveremo con gli intrinseci _mm_clmulepi64_si128
        // e _mm_aesenc_si128
        _aesni_gcm_update_internal(aead, src, dst, len);

        aead->total_len += len;
    }

    // 3. Ripristiniamo tutto
    bcrypto_restore_regs(&aes->fpu_save);
    restore_interrupts(cpu_state);

    return B_OK;
}
static status_t
aesni_aes_gcm_stream_final(void* context, uint8* tag)
{
    SoftAEADContext* aead = (SoftAEADContext*)context;
    AESNIContext* aes = (AESNIContext*)aead->cipher_ctx;
    GCMState* gcm = (GCMState*)aead->auth_ctx;

    cpu_status cpu_state = disable_interrupts();
    if (!bcrypto_save_regs(&aes->fpu_save)) {
        restore_interrupts(cpu_state);
        dprintf("AESNI: Errore critico - Impossibile salvare i registri FPU\n");
        return B_ERROR;
    }

    // 1. Preparazione Blocco lunghezze (AAD len | Cipher len) in bit
    alignas(16) uint8 lenBlock[16];
    uint64 aadLenBits = 0; // Se non usi AAD
    uint64 cipherLenBits = aead->total_len * 8;
    
    // Formato Big-Endian come richiesto da NIST
    for (int i = 0; i < 8; i++) {
        lenBlock[i] = (aadLenBits >> (56 - i * 8)) & 0xFF;
        lenBlock[i + 8] = (cipherLenBits >> (56 - i * 8)) & 0xFF;
    }
    
    dump_block("Final-Acc-Before-Len", gcm->tag_acc);
    dump_block("Length-Block", lenBlock);

    // 2. Update finale GHASH con le lunghezze
    // Fondamentale: bswap perché aesni_ghash_update_block lavora su dati "riflessi"
    __m128i lb = _mm_loadu_si128((__m128i*)lenBlock);
    lb = _mm_bswap_epi128(lb); 
    aesni_ghash_update_block(gcm->tag_acc, gcm->h_key, (uint8*)&lb);
    
    dump_block("Final-Acc-After-Len", gcm->tag_acc);

    // 3. Preparazione Tag Mask (Cifratura di J0)
    alignas(16) uint8 s[16];
    aesni_encrypt_block(aes, gcm->j0, s);
    __m128i mask = _mm_loadu_si128((__m128i*)s);

    // 4. Recupero accumulatore GHASH e riporto all'ordine naturale (bswap)
    __m128i acc = _mm_loadu_si128((__m128i*)gcm->tag_acc);
    acc = _mm_bswap_epi128(acc); 

    // 5. XOR Finale tra GHASH e Tag Mask
    __m128i finalTag = _mm_xor_si128(acc, mask);
    
    // Scrittura nel buffer di output (il kernelTag[16] dell'IOCTL)
    _mm_storeu_si128((__m128i*)tag, finalTag);
    
    dump_block("Final-Tag-Generated", tag);

    bcrypto_restore_regs(&aes->fpu_save);
    restore_interrupts(cpu_state);
    return B_OK;
}

/* --- AES Process one shot --- */
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
    const char* gcm_name = ghash_accel ? "AES-GCM (AES-NI/PCLMULQDQ)" : "AES-GCM (AES-NI/GHASH software)";
    
    if (ghash_accel) {
    	// tentativo di creare una funzione 
    	// ghash accelerata compatibile con 
    	// quella software
        sGCMHashFunc = aesni_ghash_multiply;
    } else {
        sGCMHashFunc = soft_ghash_multiply;
    }

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
        .Process = aesni_process_gcm,
        
        // Niente Hash per AES
        .HashInit     = nullptr,
        .HashUpdate   = nullptr,
        .HashFinal    = nullptr,
    
        // Streaming AEAD
        .StreamInit   = aesni_aes_gcm_stream_init,
        .StreamUpdate = aesni_aes_gcm_stream_update,
        .StreamFinal  = aesni_aes_gcm_stream_final
    };
    strlcpy(sAESNI_GCM.name, gcm_name, sizeof(sAESNI_GCM.name));
    return BRegisterCryptoAlgorithm(&sAESNI_GCM);
}
#else
status_t BInitAESNICrypto() { return B_NOT_SUPPORTED; }
#endif
