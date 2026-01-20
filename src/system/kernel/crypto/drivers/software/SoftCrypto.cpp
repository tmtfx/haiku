/*
 * Software fallback crypto
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include "SoftCrypto.h"
#include <string.h>


/* ---------------------------------------------------------------- */
/* Helper: AES CBC processing                                        */
/* ---------------------------------------------------------------- */
static status_t
soft_aes_cbc_process_internal(BCryptoRequest* request, bool encrypt)
{
    if (request->algorithm != B_CRYPTO_AES_CBC)
        return B_NOT_SUPPORTED;

    if (request->keyLength != 16 &&
        request->keyLength != 24 &&
        request->keyLength != 32)
        return B_BAD_VALUE;

    if (request->ivLength != 16)
        return B_BAD_VALUE;

    SoftAESContext ctx{};
    status_t st = soft_aes_set_key(&ctx, (const uint8*)request->key, request->keyLength);
    if (st != B_OK)
        return st;

    uint8 iv[16];
    memcpy(iv, request->iv, 16);

    for (size_t i = 0; i < request->vectorCount; i++) {
        const iovec& src = request->source[i];
        iovec& dst       = request->destination[i];

        if (src.iov_len % 16 != 0)
            return B_BAD_VALUE;

        size_t blocks = src.iov_len / 16;
        for (size_t b = 0; b < blocks; b++) {
            const uint8* in_block  = (const uint8*)src.iov_base + b*16;
            uint8*       out_block = (uint8*)dst.iov_base + b*16;

            if (encrypt) {
                // CBC: XOR input with IV
                uint8 tmp[16];
                for (int j = 0; j < 16; j++)
                    tmp[j] = in_block[j] ^ iv[j];

                soft_aes_encrypt_block(&ctx, tmp, out_block);
                memcpy(iv, out_block, 16);
            } else {
                // CBC decrypt
                uint8 tmp[16];
                soft_aes_decrypt_block(&ctx, in_block, tmp);

                for (int j = 0; j < 16; j++)
                    out_block[j] = tmp[j] ^ iv[j];

                memcpy(iv, in_block, 16);
            }
        }
    }

    memcpy(request->iv, iv, 16);
    soft_aes_zero(&ctx); // secure cleanup

    return B_OK;
}

/* ---------------------------------------------------------------- */
/* Public BCryptoRequest wrapper                                     */
/* ---------------------------------------------------------------- */
status_t soft_aes_cbc_process(BCryptoRequest* request)
{
    bool encrypt = (request->operation == B_CRYPTO_ENCRYPT);
    return soft_aes_cbc_process_internal(request, encrypt);
}

/* ---------------------------------------------------------------- */
/* Register software fallback                                         */
/* ---------------------------------------------------------------- */
status_t BInitSoftCrypto()
{
    static BCryptoAlgorithm sSoftAES = {
        .algorithm = B_CRYPTO_AES_CBC,
        .flags     = 0,       // software fallback
        .priority  = 10,      // priorità minima
        .Process   = soft_aes_cbc_process
    };

    return BRegisterCryptoAlgorithm(&sSoftAES);
}








/* roba vecchia */

/*
static const uint8 rcon[10] = {
    0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1B,0x36
};

static uint32
rotword(uint32 w)
{
    return (w << 8) | (w >> 24);
}

static uint32
subword(uint32 w)
{
    return (sbox[(w >> 24) & 0xff] << 24)
         | (sbox[(w >> 16) & 0xff] << 16)
         | (sbox[(w >> 8)  & 0xff] << 8)
         |  sbox[w & 0xff];
}

static void
aes_key_expand(const uint8* key, size_t keyLen, SoftAESContext& ctx)
{
    int nk = keyLen / 4;
    ctx.rounds = nk + 6;

    for (int i = 0; i < nk; i++) {
        ctx.roundKeys[i] =
            (key[4*i] << 24) |
            (key[4*i+1] << 16) |
            (key[4*i+2] << 8) |
             key[4*i+3];
    }

    for (int i = nk; i < 4 * (ctx.rounds + 1); i++) {
        uint32 temp = ctx.roundKeys[i - 1];
        if (i % nk == 0)
            temp = subword(rotword(temp)) ^ (rcon[(i / nk) - 1] << 24);
        else if (nk > 6 && (i % nk) == 4)
            temp = subword(temp);
        ctx.roundKeys[i] = ctx.roundKeys[i - nk] ^ temp;
    }
}

static void
add_round_key(uint8* state, const uint32* rk)
{
    for (int i = 0; i < 4; i++) {
        state[4*i+0] ^= rk[i] >> 24;
        state[4*i+1] ^= rk[i] >> 16;
        state[4*i+2] ^= rk[i] >> 8;
        state[4*i+3] ^= rk[i];
    }
}

static void
sub_bytes(uint8* s)
{
    for (int i = 0; i < 16; i++)
        s[i] = sbox[s[i]];
}

static void
shift_rows(uint8* s)
{
    uint8 t;

    t=s[1];  s[1]=s[5];  s[5]=s[9];  s[9]=s[13]; s[13]=t;
    t=s[2];  s[2]=s[10]; s[10]=t;    t=s[6];    s[6]=s[14]; s[14]=t;
    t=s[3];  s[3]=s[15]; s[15]=s[11]; s[11]=s[7]; s[7]=t;
}

static void
aes_encrypt_block(uint8* block, SoftAESContext& ctx)
{
    add_round_key(block, ctx.roundKeys);

    for (int r = 1; r < ctx.rounds; r++) {
        sub_bytes(block);
        shift_rows(block);
        add_round_key(block, ctx.roundKeys + 4*r);
    }

    sub_bytes(block);
    shift_rows(block);
    add_round_key(block, ctx.roundKeys + 4*ctx.rounds);
}

status_t
soft_aes_cbc_process(BCryptoRequest* request)
{
    if (request->algorithm != B_CRYPTO_AES_CBC)
        return B_NOT_SUPPORTED;

    if (request->ivLength != 16)
        return B_BAD_VALUE;

    if (request->keyLength != 16 &&
        request->keyLength != 24 &&
        request->keyLength != 32)
        return B_BAD_VALUE;

    SoftAESContext ctx{};
    memcpy(ctx.iv, request->iv, 16);
    aes_key_expand(request->key, request->keyLength, ctx);

    bool encrypt = (request->operation == B_CRYPTO_ENCRYPT);

    for (size_t v = 0; v < request->vectorCount; v++) {
        const iovec& src = request->source[v];
        iovec& dst = request->destination[v];

        if (src.iov_len % 16 != 0)
            return B_BAD_VALUE;

        for (size_t i = 0; i < src.iov_len; i += 16) {
            uint8 block[16];
            memcpy(block, (uint8*)src.iov_base + i, 16);

            if (encrypt) {
                for (int j = 0; j < 16; j++)
                    block[j] ^= ctx.iv[j];
                aes_encrypt_block(block, ctx);
                memcpy(ctx.iv, block, 16);
            }

            memcpy((uint8*)dst.iov_base + i, block, 16);
        }
    }

    memcpy(request->iv, ctx.iv, 16);
    return B_OK;
}

status_t
BInitSoftCrypto()
{
    static BCryptoAlgorithm sSoftAES = {
        .algorithm = B_CRYPTO_AES_CBC,
        .flags     = 0,
        .priority  = 10,
        .Process   = soft_aes_cbc_process
    };

    return BRegisterCryptoAlgorithm(&sSoftAES);
}
*/
