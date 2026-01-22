/*
 * Hardware-accelerated AES using Intel/AMD AES-NI instructions
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */


/*
 * Hardware-accelerated AES using Intel/AMD AES-NI instructions
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include "AESNI.h"

#include "BCryptoCapabilities.h"
#include <string.h>
#include "BCryptoAlgorithm.h"
#if ARCH_X86

#include <arch/x86/arch_cpu.h>
#include <wmmintrin.h>

static void
secure_memzero(void* p, size_t s)
{
    if (p == NULL)
        return;
    volatile uint8* cp = (volatile uint8*)p;
    while (s--)
        *cp++ = 0;
}

/* ------------------------------------------------------------- */
/* Key expansion helpers                                         */
/* ------------------------------------------------------------- */

static inline __m128i
aesni_keyassist(__m128i key, int rcon)
{
    __m128i tmp = _mm_aeskeygenassist_si128(key, rcon);
    tmp = _mm_shuffle_epi32(tmp, 0xff);
    key = _mm_xor_si128(key, _mm_slli_si128(key, 4));
    key = _mm_xor_si128(key, _mm_slli_si128(key, 4));
    key = _mm_xor_si128(key, _mm_slli_si128(key, 4));
    return _mm_xor_si128(key, tmp);
}

static void
aesni_expand_key_128(AESNIContext& ctx, const uint8* key)
{
    ctx.rounds = 10;

    ctx.encRoundKeys[0] = _mm_loadu_si128((const __m128i*)key);
    ctx.encRoundKeys[1] = aesni_keyassist(ctx.encRoundKeys[0], 0x01);
    ctx.encRoundKeys[2] = aesni_keyassist(ctx.encRoundKeys[1], 0x02);
    ctx.encRoundKeys[3] = aesni_keyassist(ctx.encRoundKeys[2], 0x04);
    ctx.encRoundKeys[4] = aesni_keyassist(ctx.encRoundKeys[3], 0x08);
    ctx.encRoundKeys[5] = aesni_keyassist(ctx.encRoundKeys[4], 0x10);
    ctx.encRoundKeys[6] = aesni_keyassist(ctx.encRoundKeys[5], 0x20);
    ctx.encRoundKeys[7] = aesni_keyassist(ctx.encRoundKeys[6], 0x40);
    ctx.encRoundKeys[8] = aesni_keyassist(ctx.encRoundKeys[7], 0x80);
    ctx.encRoundKeys[9] = aesni_keyassist(ctx.encRoundKeys[8], 0x1B);
    ctx.encRoundKeys[10] = aesni_keyassist(ctx.encRoundKeys[9], 0x36);
}

static void
aesni_expand_key_192(AESNIContext& ctx, const uint8* key)
{
    ctx.rounds = 12;

    __m128i t1 = _mm_loadu_si128((const __m128i*)key);        // first 128 bits
    __m128i t2 = _mm_loadu_si128((const __m128i*)(key + 8)); // remaining 64 bits (overlap)

    ctx.encRoundKeys[0] = t1;
    ctx.encRoundKeys[1] = t2;

    int idx = 1;
    uint8 rcon = 0x01;

    for (; idx < 12; ) {
        __m128i tmp = _mm_aeskeygenassist_si128(t2, rcon);
        tmp = _mm_shuffle_epi32(tmp, 0x55);

        t1 = _mm_xor_si128(t1, _mm_slli_si128(t1, 4));
        t1 = _mm_xor_si128(t1, _mm_slli_si128(t1, 4));
        t1 = _mm_xor_si128(t1, _mm_slli_si128(t1, 4));
        t1 = _mm_xor_si128(t1, tmp);

        ctx.encRoundKeys[++idx] = t1;

        tmp = _mm_shuffle_epi32(t1, 0xff);
        t2 = _mm_xor_si128(t2, _mm_slli_si128(t2, 4));
        t2 = _mm_xor_si128(t2, _mm_slli_si128(t2, 4));
        t2 = _mm_xor_si128(t2, tmp);

        ctx.encRoundKeys[++idx] = t2;

        rcon <<= 1;
    }
}

static void
aesni_expand_key_256(AESNIContext& ctx, const uint8* key)
{
    ctx.rounds = 14;

    __m128i t1 = _mm_loadu_si128((const __m128i*)key);
    __m128i t2 = _mm_loadu_si128((const __m128i*)(key + 16));

    ctx.encRoundKeys[0] = t1;
    ctx.encRoundKeys[1] = t2;

    for (int i = 2, rcon = 0x01; i <= 14; i += 2) {
        __m128i tmp = _mm_aeskeygenassist_si128(t2, rcon);
        tmp = _mm_shuffle_epi32(tmp, 0xff);

        t1 = _mm_xor_si128(t1, _mm_slli_si128(t1, 4));
        t1 = _mm_xor_si128(t1, _mm_slli_si128(t1, 4));
        t1 = _mm_xor_si128(t1, _mm_slli_si128(t1, 4));
        t1 = _mm_xor_si128(t1, tmp);
        ctx.encRoundKeys[i] = t1;

        tmp = _mm_shuffle_epi32(_mm_aeskeygenassist_si128(t1, 0x00), 0xaa);
        t2 = _mm_xor_si128(t2, _mm_slli_si128(t2, 4));
        t2 = _mm_xor_si128(t2, _mm_slli_si128(t2, 4));
        t2 = _mm_xor_si128(t2, _mm_slli_si128(t2, 4));
        t2 = _mm_xor_si128(t2, tmp);
        ctx.encRoundKeys[i + 1] = t2;

        rcon <<= 1;
    }
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
    for (size_t i = 1; i < ctx.rounds; i++)
        ctx.decRoundKeys[i] = _mm_aesimc_si128(ctx.encRoundKeys[ctx.rounds - i]);
    ctx.decRoundKeys[ctx.rounds] = ctx.encRoundKeys[0];

    return B_OK;
}

/* ------------------------------------------------------------- */
/* AES-NI CBC processing                                         */
/* ------------------------------------------------------------- */

static status_t
aesni_process_cbc(bool encrypt,
                     AESNIContext* ctx,
                     const uint8* in,
                     uint8* out,
                     size_t length)
{
    if (length % 16 != 0)
        return B_BAD_VALUE;

    size_t blocks = length / 16;
    __m128i iv = _mm_loadu_si128((const __m128i*)ctx->iv);

    for (size_t i = 0; i < blocks; i++) {
        __m128i block = _mm_loadu_si128((const __m128i*)(in + i * 16));

        if (encrypt) {
            block = _mm_xor_si128(block, iv);
            block = _mm_xor_si128(block, ctx->encRoundKeys[0]);
            for (size_t r = 1; r < ctx->rounds; r++)
                block = _mm_aesenc_si128(block, ctx->encRoundKeys[r]);
            block = _mm_aesenclast_si128(block, ctx->encRoundKeys[ctx->rounds]);
            iv = block;
        } else {
            __m128i tmp = block;
            block = _mm_xor_si128(block, ctx->decRoundKeys[0]);
            for (size_t r = 1; r < ctx->rounds; r++)
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
            for (size_t r = 1; r < ctx->rounds; r++)
                block = _mm_aesenc_si128(block, ctx->encRoundKeys[r]);
            block = _mm_aesenclast_si128(block, ctx->encRoundKeys[ctx->rounds]);
        } else {
            block = _mm_xor_si128(block, ctx->decRoundKeys[0]);
            for (size_t r = 1; r < ctx->rounds; r++)
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
        for (size_t r = 1; r < ctx->rounds; r++)
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
    AESNIContext ctx{};
    status_t st;

    /* ---- validate algorithm ---- */
    switch (request->algorithm) {
        case B_CRYPTO_AES_ECB:
            break;

        case B_CRYPTO_AES_CBC:
        case B_CRYPTO_AES_CTR:
            if (request->ivLength != 16)
                return B_BAD_VALUE;
            memcpy(ctx.iv, request->iv, 16);
            break;

        default:
            return B_NOT_SUPPORTED;
    }

    /* ---- key expansion ---- */
    st = aesni_expand_key(ctx, request->key, request->keyLength);
    if (st != B_OK)
        return st;

    bool encrypt = (request->operation == B_CRYPTO_ENCRYPT);

    /* ---- process iovecs ---- */
    for (size_t i = 0; i < request->vectorCount; i++) {
        const iovec& src = request->source[i];
        iovec& dst       = request->destination[i];

        switch (request->algorithm) {
            case B_CRYPTO_AES_ECB:
                st = aesni_process_ecb(
                    encrypt, &ctx,
                    (const uint8*)src.iov_base,
                    (uint8*)dst.iov_base,
                    src.iov_len);
                break;

            case B_CRYPTO_AES_CBC:
                st = aesni_process_cbc(
                    encrypt, &ctx,
                    (const uint8*)src.iov_base,
                    (uint8*)dst.iov_base,
                    src.iov_len);
                break;

            case B_CRYPTO_AES_CTR:
                st = aesni_process_ctr(
                    &ctx,
                    (const uint8*)src.iov_base,
                    (uint8*)dst.iov_base,
                    src.iov_len);
                break;
        }

        if (st != B_OK)
            goto out;
    }

    /* ---- copy back IV / counter ---- */
    if (request->algorithm != B_CRYPTO_AES_ECB)
        memcpy(request->iv, ctx.iv, 16);

out:
    //memset(&ctx, 0, sizeof(ctx)); // hygiene
    secure_memzero(&ctx, sizeof(ctx)); // se non va rimettere prima ma in fase di compilazione potrebbe essere ignorato!!!!!
    return st;
}

/*
static status_t
aesni_process(BCryptoRequest* request)
{
    if (request->algorithm != B_CRYPTO_AES_CBC)
        return B_NOT_SUPPORTED;

    if (request->ivLength != 16)
        return B_BAD_VALUE;

    AESNIContext ctx{};
    memcpy(ctx.iv, request->iv, 16);

    status_t st = aesni_expand_key(ctx, request->key, request->keyLength);
    if (st != B_OK)
        return st;

    bool encrypt = (request->operation == B_CRYPTO_ENCRYPT);

    for (size_t i = 0; i < request->vectorCount; i++) {
        const iovec& src = request->source[i];
        iovec& dst = request->destination[i];

        st = aesni_process_blocks(
            encrypt,
            &ctx,
            (const uint8*)src.iov_base,
            (uint8*)dst.iov_base,
            src.iov_len);

        if (st != B_OK)
            return st;
    }

    memcpy(request->iv, ctx.iv, 16);
   
    memset(&ctx, 0, sizeof(ctx)) // zeroing: AES keys are kept on the stack
    return B_OK;
}
*/

status_t
BInitAESNICrypto()
{
    if (!(BGetCryptoCapabilities() & B_CPU_CRYPTO_AESNI))
        return B_UNSUPPORTED;

    static BCryptoAlgorithm sAESNI = {
        .algorithm = B_CRYPTO_AES_CBC,
        .flags     = B_CRYPTO_HW_ACCEL,
        .priority  = 100,
        .Process   = aesni_process
    };

    return BRegisterCryptoAlgorithm(&sAESNI);
}

#else

status_t BInitAESNICrypto() { return B_NOT_SUPPORTED; }

#endif
