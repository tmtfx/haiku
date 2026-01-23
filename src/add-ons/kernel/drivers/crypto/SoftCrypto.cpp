/*
 * Software fallback crypto (AES CBC) for Haiku kernel
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include "SoftCrypto.h"
#include <string.h>

/* ---------------------------------------------------------------- */
/* Internal helper: AES CBC processing                               */
/* ---------------------------------------------------------------- */
static status_t
soft_aes_cbc_process_internal(BCryptoRequest* request, bool encrypt)
{
    if (!request)
        return B_BAD_VALUE;

    if (request->algorithm != B_CRYPTO_AES)
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
            uint8 tmp[16];

            if (encrypt) {
                for (int j = 0; j < 16; j++)
                    tmp[j] = in_block[j] ^ iv[j];

                soft_aes_encrypt_block(&ctx, tmp, out_block);
                memcpy(iv, out_block, 16);
            } else {
                soft_aes_decrypt_block(&ctx, in_block, tmp);
                for (int j = 0; j < 16; j++)
                    out_block[j] = tmp[j] ^ iv[j];
                memcpy(iv, in_block, 16);
            }
        }
    }

        /* fix smap moved to BCryptoCore
        size_t len       = src.iov_len;
        if (len == 0) continue;

        uint8* kernelBuffer = (uint8*)malloc(len);
        if (kernelBuffer == NULL) return B_NO_MEMORY;

        if (user_memcpy(kernelBuffer, src.iov_base, len) != B_OK) {
            free(kernelBuffer);
            return B_BAD_ADDRESS;
        }

        if (len % 16 != 0)
            return B_BAD_VALUE;

        size_t blocks = len / 16;
        for (size_t b = 0; b < blocks; b++) {
            uint8* block = kernelBuffer + b * 16;
            uint8 current_ciphertext_block[16];

            if (encrypt) {
                // ENCRYPT: l'IV è il risultato (già nel buffer), facile.
                for (int j = 0; j < 16; j++)
                    block[j] ^= iv[j];
            
                soft_aes_encrypt_block(&ctx, block, block);
                memcpy(iv, block, 16); 
            } else {
                // DECRYPT: dobbiamo salvare il ciphertext prima di sovrascriverlo!
                memcpy(current_ciphertext_block, block, 16); 
            
                soft_aes_decrypt_block(&ctx, block, block);
            
                for (int j = 0; j < 16; j++)
                    block[j] ^= iv[j];
            
                // Il ciphertext di questo blocco diventa l'IV per il prossimo
                memcpy(iv, current_ciphertext_block, 16);
            }
            
        }
        
        if (user_memcpy(dst.iov_base, kernelBuffer, len) != B_OK) {
            free(kernelBuffer);
            return B_BAD_ADDRESS;
        }

        free(kernelBuffer);
    }*/

    memcpy(request->iv, iv, 16);

    // Cleanup del contesto
    soft_aes_zero(&ctx);

    return B_OK;
}

/* ---------------------------------------------------------------- */
/* Public BCryptoRequest wrapper                                      */
/* ---------------------------------------------------------------- */
status_t soft_aes_cbc_process(BCryptoRequest* request)
{
    if (!request)
        return B_BAD_VALUE;

    bool encrypt = (request->operation == B_CRYPTO_ENCRYPT);
    status_t st = soft_aes_cbc_process_internal(request, encrypt);

    // Se è definito un callback, chiamiamolo (compatibilità asincrona)
    if (request->completionCallback) {
        request->completionCallback(request, st);
        return B_OK; // in modalità async, BSubmitCryptoRequest ritorna B_OK
    }

    // Sincrono
    return st;
}

/* ---------------------------------------------------------------- */
/* Register software fallback                                         */
/* ---------------------------------------------------------------- */
status_t BInitSoftCrypto()
{
    static BCryptoAlgorithm sSoftAES = {
        .algorithm = B_CRYPTO_AES,
        .flags     = B_CRYPTO_ALG_SOFTWARE,
        .priority  = 10,        // priorità minima
        .Process   = soft_aes_cbc_process
    };

    return BRegisterCryptoAlgorithm(&sSoftAES);
}
