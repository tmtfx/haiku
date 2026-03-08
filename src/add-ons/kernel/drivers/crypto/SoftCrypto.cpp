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
#include "SoftCryptoEngines.h"

/*
 * funzione helper per aes CTR

static void
aes_increment_counter(uint8* counter)
{
    // Incrementa il contatore a 128 bit (Big-Endian come da standard NIST)
    for (int i = 15; i >= 0; i--) {
        if (++counter[i] != 0)
            break;
    }
} */
/* ---------------------------------------------------------------- */
/* AES GCM BRIDGES                                                  */
/* ---------------------------------------------------------------- */
static status_t
soft_aes_gcm_stream_init(void** context, size_t* _contextSize, BCryptoOperation op, const uint8* key, size_t keyLen, 
                         const uint8* iv, size_t ivLen)
{
    if (!context || !key || !iv) return B_BAD_VALUE;

    // 1. Allochiamo il contesto specifico
    //SoftAESContext* ctx = (SoftAESContext*)malloc(sizeof(SoftAESContext));
    SoftAEADContext* aead = (SoftAEADContext*)malloc(sizeof(SoftAEADContext));
    SoftAESContext* aes = (SoftAESContext*)malloc(sizeof(SoftAESContext));
    GCMState* gcm = (GCMState*)malloc(sizeof(GCMState));

    if (!aead || !aes || !gcm) {
        free(aead); free(aes); free(gcm);
        return B_NO_MEMORY;
    }

    memset(aead, 0, sizeof(SoftAEADContext));
    memset(aes, 0, sizeof(SoftAESContext));
    memset(gcm, 0, sizeof(GCMState));
    
    // 2. Setup AES (Chiavi)
    status_t status = soft_aes_set_key(aes, key, keyLen);
    if (status != B_OK) {
        free(aead); free(aes); free(gcm);
        return status;
    }
    
    // 3. Setup GCM (Hash Key H)
    uint8 zeroBlock[16] = {0};
    // Usiamo ancora la funzione di blocco singola per l'inizializzazione
    soft_aes_encrypt_block(aes, zeroBlock, gcm->h_key);

    // 4. Setup IV / Counter
    if (ivLen == 12) {
        memcpy(gcm->counter, iv, 12);
        gcm->counter[15] = 1; 
        // Salviamo J0 per il tag finale
        memcpy(gcm->j0, gcm->counter, 16);
    } else {
        free(aead); free(aes); free(gcm);
        return B_NOT_SUPPORTED; 
    }
    
    for (int i = 15; i >= 12; i--) {
            if (++gcm->counter[i] != 0)
                break;
    }

    // 5. Assemblaggio AEAD
    aead->cipher_ctx = aes;
    aead->auth_ctx = gcm;
    aead->is_encrypting = (op == B_CRYPTO_ENCRYPT);
    aead->total_len = 0;

    *context = aead;
    *_contextSize = sizeof(SoftAEADContext);	
    return B_OK;
}

static status_t
soft_aes_gcm_stream_update(void* context, const iovec* inVec, const iovec* outVec, size_t vecCount)
{
    SoftAEADContext* aead = (SoftAEADContext*)context;
    SoftAESContext* aes = (SoftAESContext*)aead->cipher_ctx;
    GCMState* gcm = (GCMState*)aead->auth_ctx;

    for (size_t i = 0; i < vecCount; i++) {
        const uint8* src = (const uint8*)inVec[i].iov_base;
        uint8* dst = (uint8*)outVec[i].iov_base;
        size_t len = inVec[i].iov_len;

        if (aead->is_encrypting) {
            // Cifra e poi aggiorna GHASH sul ciphertext (dst)
            soft_aes_ctr_update(aes, gcm->counter, src, dst, len);
            soft_ghash_update(gcm, dst, len);
        } else {
            // Aggiorna GHASH sul ciphertext (src) e poi decifra
            soft_ghash_update(gcm, src, len);
            soft_aes_ctr_update(aes, gcm->counter, src, dst, len);
        }

        aead->total_len += len;
    }

    return B_OK;
}
static status_t
soft_aes_gcm_stream_final(void* context, uint8* tag)
{
    if (!context || !tag) return B_BAD_VALUE;

    SoftAEADContext* aead = (SoftAEADContext*)context;
    SoftAESContext* aes = (SoftAESContext*)aead->cipher_ctx;
    GCMState* gcm = (GCMState*)aead->auth_ctx;

    // 1. Fase finale del GHASH: aggiungere le lunghezze
    // GCM vuole un blocco di 16 byte: [64-bit AAD len in bits] [64-bit Ciphertext len in bits]
    uint8 lenBlock[16];
    uint64 aadLenBits = 0; // Per ora non supportiamo AAD, quindi 0
    uint64 cipherLenBits = aead->total_len * 8; // Lunghezza in bit

    // Convertiamo in Big Endian (come vuole lo standard GCM)
    for (int i = 0; i < 8; i++) {
        lenBlock[i] = (aadLenBits >> (56 - i * 8)) & 0xFF;
        lenBlock[i + 8] = (cipherLenBits >> (56 - i * 8)) & 0xFF;
    }

    // Ultimo update del GHASH con il blocco lunghezze
    soft_ghash_update(gcm, lenBlock, 16);

    // 2. Calcolo finale del Tag: TAG = GHASH(H, A, C) XOR E_k(J0)
    uint8 s[16];
    soft_aes_encrypt_block(aes, gcm->j0, s); // Cifriamo il contatore iniziale J0

    for (int i = 0; i < 16; i++) {
        tag[i] = gcm->tag_acc[i] ^ s[i];
    }
    
    aead->total_len = 0;
    memset(gcm->tag_acc, 0, 16);
    // Riportiamo il counter al valore iniziale (J0 + 1)
    memcpy(gcm->counter, gcm->j0, 16);
    for (int j = 15; j >= 12; j--) { if (++gcm->counter[j] != 0) break; }

    /*// 3. Pulizia della memoria
    free(aes);
    free(gcm);
    free(aead);*/

    return B_OK;
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
        GCMState gcm{};
        uint8 zero[16] = {0};
        soft_aes_encrypt_block(&ctx, zero, gcm.h_key);

        if (request->ivLength == 12) {
            memcpy(gcm.counter, request->iv, 12);
            gcm.counter[15] = 1;
            memcpy(gcm.j0, gcm.counter, 16);
            for (int j = 15; j >= 12; j--) { if (++gcm.counter[j] != 0) break; }
        } else return B_NOT_SUPPORTED;

        size_t total_len = 0;
    
        // Determiniamo quanti vettori di DATI ci sono. 
        // In GCM, l'ultimo vettore è SEMPRE il Tag.
        size_t dataVectorCount = request->vectorCount - 1;

        for (size_t i = 0; i < dataVectorCount; i++) {
            uint8* src = (uint8*)request->source[i].iov_base;
            uint8* dst = (uint8*)request->destination[i].iov_base;
            size_t len = request->source[i].iov_len;
            if (len == 0) continue;
            total_len += len;

            if (encrypt) {
                soft_aes_ctr_update(&ctx, gcm.counter, src, dst, len);
                soft_ghash_update(&gcm, dst, len);
            } else {
                soft_ghash_update(&gcm, src, len);
                soft_aes_ctr_update(&ctx, gcm.counter, src, dst, len);
            }
        }

        // Finalizzazione Tag
        uint8 len_block[16] = {0};
        uint64 data_bits = (uint64)total_len * 8;
        // Fix: Encoding Big-Endian corretto (GCM usa 64 bit per la lunghezza)
        //for (int i = 0; i < 8; i++) len_block[15-i] = (data_bits >> (i * 8)) & 0xFF;
        uint64 aad_bits = 0; // Se non abbiamo AAD

// Scriviamo AAD len nei primi 8 byte (Big-Endian)
for (int i = 0; i < 8; i++) {
    len_block[i] = (aad_bits >> (56 - i * 8)) & 0xFF;
}
// Scriviamo Ciphertext len negli ultimi 8 byte (Big-Endian)
for (int i = 0; i < 8; i++) {
    len_block[i + 8] = (data_bits >> (56 - i * 8)) & 0xFF;
}
    
        soft_ghash_update(&gcm, len_block, 16);

        uint8 s0[16];
        soft_aes_encrypt_block(&ctx, gcm.j0, s0);
        for (int j = 0; j < 16; j++) gcm.tag_acc[j] ^= s0[j];

        if (encrypt) {
            // Scriviamo il tag nell'ultimo vettore di destinazione
            uint8* outTag = (uint8*)request->destination[request->vectorCount - 1].iov_base;
            memcpy(outTag, gcm.tag_acc, 16);
            return B_OK;
        } else {
            // Leggiamo il tag dall'ultimo vettore di sorgente
            uint8* providedTag = (uint8*)request->source[request->vectorCount - 1].iov_base;
            return (memcmp(gcm.tag_acc, providedTag, 16) == 0) ? B_OK : B_BAD_DATA;
        }
    }
/*
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
    return B_OK;*/
    // --- LOGICA CBC / CTR / ECB ---
    for (size_t i = 0; i < request->vectorCount; i++) {
        uint8* in_ptr = (uint8*)request->source[i].iov_base;
        uint8* out_ptr = (uint8*)request->destination[i].iov_base;
        size_t len = request->source[i].iov_len;

        if (request->mode == B_CRYPTO_MODE_CTR) {
            soft_aes_ctr_update(&ctx, iv, in_ptr, out_ptr, len);
        } else {
            // CBC ed ECB richiedono blocchi da 16
            if ((len % 16) != 0) {
            	soft_aes_zero(&ctx);
            	return B_BAD_VALUE;
            }
            
            for (size_t b = 0; b < len; b += 16) {
                uint8 block[16];
                memcpy(block, in_ptr + b, 16);
                if (request->mode == B_CRYPTO_MODE_ECB) {
                    if (encrypt) soft_aes_encrypt_block(&ctx, block, block);
                    else soft_aes_decrypt_block(&ctx, block, block);
                } else if (request->mode == B_CRYPTO_MODE_CBC) {
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
                memcpy(out_ptr + b, block, 16);
            }
        }
    }
    
    if (request->mode == B_CRYPTO_MODE_CBC || request->mode == B_CRYPTO_MODE_CTR) {
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
        .Process   = soft_aes_process,
        
        // Niente Hash per AES
        .HashInit     = nullptr,
        .HashUpdate   = nullptr,
        .HashFinal    = nullptr,
    
        // Streaming AEAD
        .StreamInit   = soft_aes_gcm_stream_init,
        .StreamUpdate = soft_aes_gcm_stream_update,
        .StreamFinal  = soft_aes_gcm_stream_final
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
