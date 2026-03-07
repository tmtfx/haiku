/*
 * Software fallback crypto (AES CBC) for Haiku kernel
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include "SoftCrypto.h"
#include <string.h>
#include "soft_aes.h"
#include "soft_chacha20.h"
#include <debug.h>

/*
 * funzione helper per aes CTR
 */
static void
aes_increment_counter(uint8* counter)
{
    // Incrementa il contatore a 128 bit (Big-Endian come da standard NIST)
    for (int i = 15; i >= 0; i--) {
        if (++counter[i] != 0)
            break;
    }
}

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
    
    if (request->mode == B_CRYPTO_MODE_GCM) {
        // 1. Derivazione chiave Hash H = AES_K(0)
        uint8 h_key[16] = {0};
        soft_aes_encrypt_block(&ctx, h_key, h_key);

        // 2. Preparazione contatori (J0)
        uint8 j0[16];
        if (request->ivLength == 12) {
            memcpy(j0, request->iv, 12);
            memset(j0 + 12, 0, 3);
            j0[15] = 1;
        } else {
            // Se IV != 96 bit servirebbe un pre-hash GHASH, 
            // per ora limitiamoci allo standard 96-bit.
            soft_aes_zero(&ctx);
            return B_NOT_SUPPORTED; 
        }

        uint8 current_ctr[16];
        memcpy(current_ctr, j0, 16);
        aes_increment_counter(current_ctr);

        uint8 tag_acc[16] = {0};
        size_t total_len = 0;

        // 3. Loop sui vettori
        for (size_t i = 0; i < request->vectorCount; i++) {
            // Logica di esclusione del vettore TAG (come abbiamo discusso)
            if (!encrypt && i == request->vectorCount - 1) break;
            if (encrypt && i == request->vectorCount - 1 && request->vectorCount > 1) break;

            uint8* in = (uint8*)request->source[i].iov_base;
            uint8* out = (uint8*)request->destination[i].iov_base;
            size_t len = request->source[i].iov_len;
            total_len += len;

            for (size_t b = 0; b < len; b += 16) {
                size_t chunk = (len - b < 16) ? len - b : 16;
                uint8 block[16] = {0};
                memcpy(block, in + b, chunk);

                if (encrypt) {
                    uint8 ks[16];
                    soft_aes_encrypt_block(&ctx, current_ctr, ks);
                    for (size_t j = 0; j < chunk; j++) out[b + j] = block[j] ^ ks[j];
                    
                    uint8 hash_in[16] = {0};
                    memcpy(hash_in, out + b, chunk);
                    for (int j = 0; j < 16; j++) tag_acc[j] ^= hash_in[j];
                    ghash_multiply(tag_acc, h_key);
                } else {
                    for (size_t j = 0; j < chunk; j++) tag_acc[j] ^= block[j];
                    ghash_multiply(tag_acc, h_key);

                    uint8 ks[16];
                    soft_aes_encrypt_block(&ctx, current_ctr, ks);
                    for (size_t j = 0; j < chunk; j++) out[b + j] = block[j] ^ ks[j];
                }
                aes_increment_counter(current_ctr);
            }
        }

        // 4. Finalizzazione (AAD length 0 + Data length)
        uint8 len_block[16] = {0};
        uint64 data_bits = (uint64)total_len * 8;
        // Encode Big-Endian (ultimi 8 byte per i dati)
        for (int i = 0; i < 8; i++) len_block[15-i] = (data_bits >> (i * 8)) & 0xFF;
        
        for (int j = 0; j < 16; j++) tag_acc[j] ^= len_block[j];
        ghash_multiply(tag_acc, h_key);

        uint8 s0[16];
        soft_aes_encrypt_block(&ctx, j0, s0);
        for (int j = 0; j < 16; j++) tag_acc[j] ^= s0[j];

        soft_aes_zero(&ctx);

        if (encrypt) {
            memcpy(request->destination[request->vectorCount - 1].iov_base, tag_acc, 16);
            return B_OK;
        } else {
            void* providedTag = request->source[request->vectorCount - 1].iov_base;
            return (memcmp(tag_acc, providedTag, 16) == 0) ? B_OK : B_BAD_DATA;
        }
    }

    for (size_t i = 0; i < request->vectorCount; i++) {
        uint8* in_ptr = (uint8*)request->source[i].iov_base;
        uint8* out_ptr = (uint8*)request->destination[i].iov_base;
        size_t len = request->source[i].iov_len;
        //size_t blocks = request->source[i].iov_len / 16;
        // Per ECB e CBC, la lunghezza deve essere multipla di 16
        if (request->mode != B_CRYPTO_MODE_CTR && (len % 16) != 0) {
            soft_aes_zero(&ctx);
            return B_BAD_VALUE;
        }
        size_t blocks = len / 16;
        size_t b = 0;

        for (b = 0; b < blocks; b++) {
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
                aes_increment_counter(iv);
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

/* Wrapper per ChaCha20 in SoftCrypto.cpp */
static status_t
soft_chacha20_process(BCryptoRequest* request)
{
    if (!request || request->algorithm != B_CRYPTO_CHACHA20)
        return B_BAD_VALUE;

    // ChaCha20 usa chiavi da 256 bit (32 byte)
    if (request->keyLength != 32)
        return B_BAD_VALUE;

    // Prepariamo il contesto
    ChaCha20Context ctx;
    uint32 initialCounter = 0;
    uint8 nonce[12];

    if (request->iv && request->ivLength >= 16) {
        // I primi 4 byte dell'IV diventano il contatore
        memcpy(&initialCounter, request->iv, 4);
        // I restanti 12 byte sono il nonce
        memcpy(nonce, (uint8*)request->iv + 4, 12);
    } else if (request->iv && request->ivLength >= 12) {
        // Se l'utente passa solo 12 byte, assumiamo siano il nonce e counter=0
        initialCounter = 0;
        memcpy(nonce, request->iv, 12);
    }else {
        // Se l'utente non dà l'IV, usiamo tutto a zero (pericoloso ma evita crash)
        memset(nonce, 0, 12);
        initialCounter = 0;
    }

    chacha20_init(&ctx, (uint8*)request->key, nonce, initialCounter);

    // Processiamo ogni frammento (iovec)
    for (size_t i = 0; i < request->vectorCount; i++) {
    	if (request->source[i].iov_len == 0) continue;
        chacha20_process(&ctx, 
            (const uint8*)request->source[i].iov_base, 
            (uint8*)request->destination[i].iov_base, 
            request->source[i].iov_len);
    }

    // Aggiorniamo l'IV nella richiesta così l'utente può continuare la cifratura
    //if (request->iv && request->ivLength >= 16) {
    //    memcpy(request->iv, &ctx.state[12], 4); // Nuovo contatore
    //}
    // Fondamentale: riportiamo il nuovo stato del contatore nell'IV originale
    // in modo che chiamate successive continuino lo stream correttamente
    if (request->iv && request->ivLength >= 4) {
        memcpy(request->iv, &ctx.state[12], 4);
    }

    // Pulizia
    memset(&ctx, 0, sizeof(ctx));
    
    status_t status = B_OK;
    
    if (request->completionCallback) {
        request->completionCallback(request, status);
        return B_OK;
    }
    return status;
}

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
        .name      = "AES-CBC (Software)",
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
        .name      = "AES-ECB (Software)",
        .priority  = 10,
        .Process   = soft_aes_process // Nota: dovrai gestire il caso ECB nel process!
    };
    
    //return BRegisterCryptoAlgorithm(&sSoftAES_ECB);
    status = BRegisterCryptoAlgorithm(&sSoftAES_ECB);
    if (status != B_OK)
        return status;
        
    static BCryptoAlgorithm sSoftAES_CTR = {
        .algorithm = B_CRYPTO_AES,
        .mode      = B_CRYPTO_MODE_CTR,
        .flags     = B_CRYPTO_ALG_SOFTWARE,
        .name      = "AES-CTR (Software)",
        .priority  = 10,
        .Process   = soft_aes_process // punterà alla funzione aggiornata
    };
    status = BRegisterCryptoAlgorithm(&sSoftAES_CTR);
    if (status != B_OK)
        return status;
    
    static BCryptoAlgorithm sSoftAES_GCM = {
        .algorithm = B_CRYPTO_AES,
        .mode      = B_CRYPTO_MODE_GCM,
        .flags     = B_CRYPTO_ALG_SOFTWARE,
        .name      = "AES-GCM (Software)",
        .priority  = 10,
        .Process   = soft_aes_process
    };
    status = BRegisterCryptoAlgorithm(&sSoftAES_GCM);
    if (status != B_OK)
        return status;
    
    static BCryptoAlgorithm sSoftChaCha20 = {
        .algorithm = B_CRYPTO_CHACHA20,
        .mode      = B_CRYPTO_MODE_ANY,
        .flags     = B_CRYPTO_ALG_SOFTWARE,
        .name      = "ChaCha20 (Software)",
        .priority  = 10,
        .Process   = soft_chacha20_process 
    };
    return BRegisterCryptoAlgorithm(&sSoftChaCha20);
}
