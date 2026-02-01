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
    /*if (useIV) {
        memcpy(request->iv, iv, 16);
    }*/
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

    if (request->ivLength != 16)
        return B_BAD_VALUE;

    uint8 k[32];
    uint8 iv[16];
    
    // Usa user_memcpy per sicurezza o assicurati che STAC sia attivo
    if (user_memcpy(k, request->key, request->keyLength) != B_OK) return B_BAD_ADDRESS;
    if (user_memcpy(iv, request->iv, 16) != B_OK) return B_BAD_ADDRESS;
    
    SoftAESContext ctx{};
    status_t st = soft_aes_set_key(&ctx, k, request->keyLength);
    //status_t st = soft_aes_set_key(&ctx, (const uint8*)request->key, request->keyLength);
    if (st != B_OK)
        return st;

    //uint8 iv[16];
    //memcpy(iv, request->iv, 16);

    for (size_t i = 0; i < request->vectorCount; i++) {
        const iovec& src = request->source[i];
        iovec& dst       = request->destination[i];
        
        
        if (src.iov_len % 16 != 0)
            return B_BAD_VALUE;

        size_t blocks = src.iov_len / 16;
        for (size_t b = 0; b < blocks; b++) {
        	uint8 tmp_in[16];
            uint8 tmp_out[16];

            // 2. LEGGI DAL BUFFER UTENTE (ZeroCopy ma con attenzione)
            if (user_memcpy(tmp_in, (uint8*)src.iov_base + b*16, 16) != B_OK) return B_BAD_ADDRESS;
            //const uint8* in_block  = (const uint8*)src.iov_base + b*16;
            //uint8*       out_block = (uint8*)dst.iov_base + b*16;
            //uint8 tmp[16];

            if (encrypt) {
                //for (int j = 0; j < 16; j++) tmp[j] = in_block[j] ^ iv[j];
                //soft_aes_encrypt_block(&ctx, tmp, out_block);
                //memcpy(iv, out_block, 16);
                for (int j = 0; j < 16; j++) tmp_in[j] ^= iv[j];
                soft_aes_encrypt_block(&ctx, tmp_in, tmp_out);
                memcpy(iv, tmp_out, 16);
            } else {
                //soft_aes_decrypt_block(&ctx, in_block, tmp);
                //for (int j = 0; j < 16; j++) out_block[j] = tmp[j] ^ iv[j];
                //memcpy(iv, in_block, 16);
                uint8 next_iv[16];
                memcpy(next_iv, tmp_in, 16);
                soft_aes_decrypt_block(&ctx, tmp_in, tmp_out);
                for (int j = 0; j < 16; j++) tmp_out[j] ^= iv[j];
                memcpy(iv, next_iv, 16);
            }
            uint8* user_dst = (uint8*)dst.iov_base + b*16;
            if (user_memcpy(user_dst, tmp_out, 16) != B_OK) return B_BAD_ADDRESS;
        }
    }

    memcpy(request->iv, iv, 16);

    // Cleanup del contesto
    soft_aes_zero(&ctx);

    return B_OK;
}*/

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
    static BCryptoAlgorithm sSoftAES_CBC = {
        .algorithm = B_CRYPTO_AES,
        .mode      = B_CRYPTO_MODE_CBC,
        .flags     = B_CRYPTO_ALG_SOFTWARE,
        .priority  = 10,
        .Process   = soft_aes_cbc_process
    };
    status_t status = BRegisterCryptoAlgorithm(&sSoftAES_CBC);
    if (status != B_OK)
        return status;

    static BCryptoAlgorithm sSoftAES_ECB = {
        .algorithm = B_CRYPTO_AES,
        .mode      = B_CRYPTO_MODE_ECB,
        .flags     = B_CRYPTO_ALG_SOFTWARE,
        .priority  = 10,
        .Process   = soft_aes_cbc_process // Nota: dovrai gestire il caso ECB nel process!
    };
    
    return BRegisterCryptoAlgorithm(&sSoftAES_ECB);
}
