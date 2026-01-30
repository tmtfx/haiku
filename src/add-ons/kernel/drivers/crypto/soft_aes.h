/*
 * Software AES core (Rijndael) for Haiku kernel crypto
 * Copyright 2026, Fabio Tomat
 * MIT License
 *
 * Provides block-level AES encryption/decryption and key schedule
 */

#ifndef _SOFT_AES_H_
#define _SOFT_AES_H_

#include <SupportDefs.h>

/* AES context for key schedule */
typedef struct SoftAESContext {
    uint8 encRoundKeys[240];   // max 14 rounds * 16 bytes
    uint8 decRoundKeys[240];
    int rounds;                // number of rounds (10/12/14)
} SoftAESContext;

/* Key setup */
status_t soft_aes_set_key(SoftAESContext* ctx, const uint8* key, size_t keyLength);

/* Encrypt/decrypt single 16-byte block */
void soft_aes_encrypt_block(SoftAESContext* ctx, const uint8* in, uint8* out);
void soft_aes_decrypt_block(SoftAESContext* ctx, const uint8* in, uint8* out);

/* Securely zeroize context */
void soft_aes_zero(SoftAESContext* ctx);

#endif // _SOFT_AES_H_
