/*
 * Software fallback crypto (AES CBC) for Haiku kernel
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include "SoftCrypto.h"
#include <string.h>
#include "soft_aes.h"
#include <debug.h>

/* ---------------------------------------------------------------- */
/* Internal helper: AES CBC processing                               */
/* ---------------------------------------------------------------- */
static status_t
soft_aes_process_internal(BCryptoRequest* request, bool encrypt)
{
	if (!request)
        return B_BAD_VALUE;

    if (request->algorithm != B_CRYPTO_AES)
        return B_NOT_SUPPORTED;

    if (request->keyLength != 16 &&
        request->keyLength != 24 &&
        request->keyLength != 32)
        return B_BAD_VALUE;
    
    bool useIV = (request->mode == B_CRYPTO_MODE_CBC || request->mode == B_CRYPTO_MODE_CTR);
    
    uint8 iv[16] = {0};
    if (useIV) {
        if (request->iv == NULL || request->ivLength < 16) return B_BAD_VALUE;
        memcpy(iv, request->iv, 16);
    }

    SoftAESContext ctx{};
    soft_aes_set_key(&ctx, (uint8*)request->key, request->keyLength);

    for (size_t i = 0; i < request->vectorCount; i++) {
        uint8* in_ptr = (uint8*)request->source[i].iov_base;
        uint8* out_ptr = (uint8*)request->destination[i].iov_base;
        size_t blocks = request->source[i].iov_len / 16;

        for (size_t b = 0; b < blocks; b++) {
            uint8 block[16];
            memcpy(block, in_ptr + (b * 16), 16);

            if (request->mode == B_CRYPTO_MODE_ECB) {
                if (encrypt) soft_aes_encrypt_block(&ctx, block, block);
                else soft_aes_decrypt_block(&ctx, block, block);
            } 
            else if (request->mode == B_CRYPTO_MODE_CBC) {
                if (encrypt) {
                    for (int j = 0; j < 16; j++) block[j] ^= iv[j];
                    soft_aes_encrypt_block(&ctx, block, block);
                    memcpy(iv, block, 16);
                } else {
                    uint8 next_iv[16];
                    memcpy(next_iv, block, 16);
                    soft_aes_decrypt_block(&ctx, block, block);
                    for (int j = 0; j < 16; j++) block[j] ^= iv[j];
                    memcpy(iv, next_iv, 16);
                }
            }
            else if (request->mode == B_CRYPTO_MODE_CTR) {
                // MODALITÀ CTR: Trasforma AES in un stream cipher
                uint8 keystream[16];
                soft_aes_encrypt_block(&ctx, iv, keystream); // Cifriamo il contatore
                for (int j = 0; j < 16; j++) block[j] ^= keystream[j];
                
                // Incremento del contatore (big-endian 128-bit)
                for (int j = 15; j >= 0; j--) {
                    if (++iv[j] != 0) break;
                }
            }
            memcpy(out_ptr + (b * 16), block, 16);
        }
    }
    
    if (useIV) {
        memcpy(request->iv, iv, 16);
    }
    soft_aes_zero(&ctx);
    return B_OK;
}

/*
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

    //if (request->ivLength != 16) return B_BAD_VALUE;

    bool useIV = (request->mode == B_CRYPTO_MODE_CBC);
    //if (useIV && (request->iv == NULL || request->ivLength < 16)) return B_BAD_VALUE;
    uint8 iv[16] = {0};
    if (useIV) {
        if (request->iv == NULL || request->ivLength < 16)
            return B_BAD_VALUE;
        // Leggiamo l'IV una volta sola all'inizio
        memcpy(iv, request->iv, 16);
    }
    uint8 k[32];
    memcpy(k, request->key, request->keyLength);
    SoftAESContext ctx{};
    
    
    status_t st = soft_aes_set_key(&ctx, k, request->keyLength);
    if (st != B_OK) return st;

    for (size_t i = 0; i < request->vectorCount; i++) {
        const iovec& src = request->source[i];
        iovec& dst       = request->destination[i];
        
        // Verifica allineamento blocchi AES
        if (src.iov_len % 16 != 0) return B_BAD_VALUE;
        uint8* in_ptr = (uint8*)src.iov_base;
        uint8* out_ptr = (uint8*)dst.iov_base;
        
        size_t blocks = src.iov_len / 16;
        for (size_t b = 0; b < blocks; b++) {
            uint8 block[16];
            memcpy(block, in_ptr + (b * 16), 16);

            if (encrypt) {
                if (useIV) {
                    for (int j = 0; j < 16; j++) block[j] ^= iv[j];
                }
                soft_aes_encrypt_block(&ctx, block, block);
                if (useIV) memcpy(iv, block, 16);
            } else {
                uint8 next_iv[16];
                if (useIV) memcpy(next_iv, block, 16);
                
                soft_aes_decrypt_block(&ctx, block, block);
                
                if (useIV) {
                    for (int j = 0; j < 16; j++) block[j] ^= iv[j];
                    memcpy(iv, next_iv, 16);
                }
            }
            memcpy(out_ptr + (b * 16), block, 16);
        }
    }

    // Fondamentale: copia l'IV aggiornato indietro per il prossimo pezzo
    if (useIV) {
        memcpy(request->iv, iv, 16);
    }
    soft_aes_zero(&ctx);
    return B_OK;
}
*/

/* ---------------------------------------------------------------- */
/* Public BCryptoRequest wrapper                                      */
/* ---------------------------------------------------------------- */
status_t soft_aes_process(BCryptoRequest* request)
{
    if (!request)
        return B_BAD_VALUE;
    bool encrypt = (request->operation == B_CRYPTO_ENCRYPT);
    //status_t st = soft_aes_cbc_process_internal(request, encrypt);
    status_t st = soft_aes_process_internal(request, encrypt);

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
    static BCryptoAlgorithm sSoftAES_CBC = {
        .algorithm = B_CRYPTO_AES,
        .mode      = B_CRYPTO_MODE_CBC,
        .flags     = B_CRYPTO_ALG_SOFTWARE,
        .priority  = 10,
        .Process   = soft_aes_process
    };
    status_t status = BRegisterCryptoAlgorithm(&sSoftAES_CBC);
    if (status != B_OK)
        return status;

    static BCryptoAlgorithm sSoftAES_ECB = {
        .algorithm = B_CRYPTO_AES,
        .mode      = B_CRYPTO_MODE_ECB,
        .flags     = B_CRYPTO_ALG_SOFTWARE,
        .priority  = 10,
        .Process   = soft_aes_process // Nota: dovrai gestire il caso ECB nel process!
    };
    
    return BRegisterCryptoAlgorithm(&sSoftAES_ECB);
}
