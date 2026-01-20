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

#include "../../BCryptoCapabilities.h"
#include <string.h>

#if ARCH_X86

#include <arch/x86/arch_cpu.h>
#include <wmmintrin.h>

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
aesni_process_blocks(bool encrypt,
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

/* ------------------------------------------------------------- */
/* BCrypto integration                                           */
/* ------------------------------------------------------------- */

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
    return B_OK;
}

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




/*
#include "AESNI.h"
#include "BCryptoCapabilities.h"
#include <string.h>

#if ARCH_X86
#include <arch/x86/arch_cpu.h>
#include <emmintrin.h>
#include <wmmintrin.h> // AES-NI intrinsics
#endif

#if ARCH_X86

static void generate_aesni_round_keys(const uint8* key, size_t keyLength, uint8* roundKeys)
{
    // Simplified: use AES-NI intrinsics _mm_aeskeygenassist_si128
    __m128i temp1, temp2;
    __m128i* rk = (__m128i*)roundKeys;

    if (keyLength == 16) { // 128-bit
        temp1 = _mm_loadu_si128((__m128i*)key);
        rk[0] = temp1;
        for (int i = 1; i <= 10; i++) {
            temp2 = _mm_aeskeygenassist_si128(temp1, i);
            temp1 = _mm_xor_si128(temp1, _mm_slli_si128(temp1, 4));
            temp1 = _mm_xor_si128(temp1, _mm_slli_si128(temp1, 4));
            temp1 = _mm_xor_si128(temp1, _mm_slli_si128(temp1, 4));
            temp1 = _mm_xor_si128(temp1, _mm_shuffle_epi32(temp2, 0xff));
            rk[i] = temp1;
        }
    } else {
        // TODO: implement 192/256-bit key expansion
    }
}

static status_t aesni_process_block(bool encrypt, AESNIContext* ctx,
                                    const uint8* in, uint8* out, size_t length)
{
    if (length % 16 != 0)
        return B_BAD_VALUE;

    size_t blocks = length / 16;

    for (size_t i = 0; i < blocks; i++) {
        __m128i block = _mm_loadu_si128((__m128i*)(in + i*16));
        __m128i iv    = _mm_loadu_si128((__m128i*)ctx->iv);

        if (encrypt)
            block = _mm_xor_si128(block, iv);

        for (size_t r = 0; r < ctx->nr; r++) {
            if (encrypt) {
                if (r < ctx->nr - 1)
                    block = _mm_aesenc_si128(block, ((__m128i*)ctx->roundKeys)[r]);
                else
                    block = _mm_aesenclast_si128(block, ((__m128i*)ctx->roundKeys)[r]);
            } else {
                if (r < ctx->nr - 1)
                    block = _mm_aesdec_si128(block, ((__m128i*)ctx->roundKeys)[r]);
                else
                    block = _mm_aesdeclast_si128(block, ((__m128i*)ctx->roundKeys)[r]);
            }
        }

        _mm_storeu_si128((__m128i*)(out + i*16), block);

        // update IV for CBC
        if (encrypt)
            _mm_storeu_si128((__m128i*)ctx->iv, block);
        else
            _mm_storeu_si128((__m128i*)ctx->iv, _mm_loadu_si128((__m128i*)(in + i*16)));
    }

    return B_OK;
}

status_t aesni_process_request(BCryptoRequest* request)
{
    if (request->algorithm != B_CRYPTO_AES_CBC)
        return B_NOT_SUPPORTED;

    AESNIContext ctx{};
    size_t nrRounds = 0;

    switch (request->keyLength) {
        case 16: nrRounds = 10; break;
        case 24: nrRounds = 12; break;
        case 32: nrRounds = 14; break;
        default: return B_BAD_VALUE;
    }
    ctx.nr = nrRounds;
    memcpy(ctx.iv, request->iv, 16);

    // Generate round keys
    generate_aesni_round_keys(request->key, request->keyLength, ctx.roundKeys);

    bool encrypt = (request->operation == B_CRYPTO_ENCRYPT);

    for (size_t i = 0; i < request->vectorCount; i++) {
        const iovec& src = request->source[i];
        iovec& dst       = request->destination[i];

        if (src.iov_len % 16 != 0)
            return B_BAD_VALUE;

        status_t st = aesni_process_block(encrypt, &ctx,
                                          (const uint8*)src.iov_base,
                                          (uint8*)dst.iov_base,
                                          src.iov_len);
        if (st != B_OK)
            return st;
    }

    memcpy(request->iv, ctx.iv, 16);

    return B_OK;
}

status_t BInitAESNICrypto()
{
    if (!(BGetCryptoCapabilities() & B_CPU_CRYPTO_AESNI))
        return B_UNSUPPORTED;

    static BCryptoAlgorithm sAESNI = {
        .algorithm = B_CRYPTO_AES_CBC,
        .flags     = B_CRYPTO_HW_ACCEL,
        .priority  = 100, // più alto di PadLock
        .Process   = aesni_process_request
    };

    return BRegisterCryptoAlgorithm(&sAESNI);
}

#else

status_t aesni_process_request(BCryptoRequest* request) { return B_NOT_SUPPORTED; }
status_t BInitAESNICrypto() { return B_NOT_SUPPORTED; }

#endif
*/
