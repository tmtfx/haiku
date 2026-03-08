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
#if defined(__x86_64__) || defined(__i386__)
#include "BCryptoCPU.h"
#include <arch/x86/arch_cpu.h>
#include <wmmintrin.h>
#include <tmmintrin.h> //temp
#include <malloc.h>
#pragma GCC target("aes,sse4.2")

static void
secure_memzero(void* p, size_t s)
{
    if (p == NULL)
        return;
    volatile uint8* cp = (volatile uint8*)p;
    while (s--)
        *cp++ = 0;
}

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

status_t
BInitAESNICrypto()
{
    if (!(BGetStoredCryptoCapabilities() & B_CRYPTO_HW_AES_NI))
        return B_UNSUPPORTED;

    static BCryptoAlgorithm sAESNI = {
        .algorithm = B_CRYPTO_AES,                               // ID Algoritmo
        .mode = (BCryptoMode)(B_CRYPTO_MODE_CBC | B_CRYPTO_MODE_ECB | B_CRYPTO_MODE_CTR), // Modi supportati
        .flags = B_CRYPTO_ALG_HW_ACCEL,                      // Flags
        .name      = "AES CBC-ECB-CTR (Intel AES-NI)",
        .priority = 90,                                         // Priorità (Alta perché hardware)
        .Process = aesni_process                               // Callback
    };

    return BRegisterCryptoAlgorithm(&sAESNI);
}
#else
status_t BInitAESNICrypto() { return B_NOT_SUPPORTED; }
#endif
