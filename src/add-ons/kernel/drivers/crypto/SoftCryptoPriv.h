/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef SOFT_CRYPTO_PRIV_H
#define SOFT_CRYPTO_PRIV_H

#include <SupportDefs.h>
#include <crypto/BCrypto.h> // Per BCryptoOperation e BCryptoFPUContext
#include <crypto/BCryptoKernelInternal.h> // Per BCryptoFPUContext
#include <immintrin.h>      // Per __m128i

#if defined(__x86_64__) || defined(__i386__)
// --- I MOTORI (Specifici) ---
struct AESNIContext {
	alignas(64) BCryptoFPUContext fpu_save;
    alignas(16) __m128i encRoundKeys[15]; 
    alignas(16) __m128i decRoundKeys[15];
    int     rounds;
    uint8   iv[16];
} __attribute__((aligned(64)));
#endif

typedef struct {
    uint8 encRoundKeys[240];
    uint8 decRoundKeys[240];
    int rounds;
} SoftAESContext;

typedef struct {
    uint32 state[16];
} SoftChaChaContext;

// --- GLI AUTENTICATORI (Specifici) ---

typedef struct {
    uint8 h_key[16];
    uint8 tag_acc[16];
    uint8 counter[16];
    uint8 j0[16]; // Salviamo J0 qui per il Final
} GCMState;

// --- IL CONTENITORE UNIVERSALE (AEAD) ---

typedef struct {
    // Puntatori ai contesti specifici (AES, GCM, ecc.)
    void* cipher_ctx;
    void* auth_ctx;
    bool is_encrypting;
    uint64 total_len;
} SoftAEADContext;

#endif
