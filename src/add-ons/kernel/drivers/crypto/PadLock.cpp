/*
 * Hardware-accelerated AES using VIA PadLock ACE
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include "PadLock.h"
#include "BCryptoCore.h"
#include <crypto/BCryptoDefs.h>
#include "BCryptoCPU.h" // Per BGetStoredCryptoCapabilities

#include <string.h>

#if defined(__x86_64__) || defined(__i386__)

static void
secure_memzero(void* p, size_t s)
{
    if (p == NULL) return;
    volatile uint8* cp = (volatile uint8*)p;
    while (s--) *cp++ = 0;
}

static status_t
padlock_aes_process_block(bool encrypt, PadLockAESContext* ctx,
                         const uint8* in, uint8* out, size_t length)
{
	// 1. Check allineamento (Cruciale per evitare General Protection Fault)
    // L'istruzione xcryptcbc richiede che source, destination, IV e Key 
    // siano allineati a 16 byte.
    if (((uintptr_t)in & 0xF) != 0 || ((uintptr_t)out & 0xF) != 0)
        return B_BAD_VALUE;
        
    if (length % 16 != 0)
        return B_BAD_VALUE;

    // 2. Control Word per VIA Nano / ACE2
    // Bit 0-3: Round count (0 = automatico basato sulla chiave)
    // Bit 9: 0 = Decrypt, 1 = Encrypt
    // Bit 10-11: Key Size (0 = 128, 1 = 192, 2 = 256)
    uint32 ctrl = 0; 
    switch (ctx->keyLength) {
        //case 16: ctrl = 0x0080; break;
        //case 24: ctrl = 0x00C0; break;
        //case 32: ctrl = 0x0100; break;
        case 16: ctrl = 0x0;    break; // AES-128: Bit 7 = 0, Bit 8 = 0
        case 24: ctrl = 0x400;  break; // AES-192: Bit 10 = 1 (alcune versioni) o Bit 7/8
        case 32: ctrl = 0x800;  break; // AES-256
        default: return B_BAD_VALUE;
    }
    if (encrypt) ctrl |= 0x200;

    // Variabili locali per gestire i registri che la CPU modificherà
    size_t blocks = length / 16;
    uint8* ivPtr = ctx->iv;
    
    // Definiamo i suffissi e i registri in base all'architettura
#ifdef __x86_64__
    #define PUSHF "pushfq"
    #define POPF  "popfq"
    //#define ADDR_REG "r" // a 64-bit i puntatori sono a 64-bit
#else
    #define PUSHF "pushfl"
    #define POPF  "popfl"
    //#define ADDR_REG "r"
#endif

    asm volatile(
        PUSHF "\n\t"
        POPF  "\n\t"        // Resetta i flag EFLAGS (fondamentale per Padlock)
        "xcryptcbc\n\t"
        : "+S"(in), "+D"(out), "+a"(ivPtr), "+c"(blocks)
        : "d"(ctrl), "b"(ctx->key)
        : "memory", "cc"
    );

    return B_OK;
}

static status_t
padlock_process(BCryptoRequest* request)
{
    if (request->algorithm != B_CRYPTO_AES || request->mode != B_CRYPTO_MODE_CBC)
        return B_NOT_SUPPORTED;
        
    PadLockAESContext ctx{};
    memcpy(ctx.key, request->key, request->keyLength);
    if (request->iv)
        memcpy(ctx.iv, request->iv, 16);
    ctx.keyLength = request->keyLength;

    bool encrypt = (request->operation == B_CRYPTO_ENCRYPT);
    status_t st = B_OK;

    for (size_t i = 0; i < request->vectorCount; i++) {
        const iovec& src = request->source[i];
        iovec& dst       = request->destination[i];
        
        if (src.iov_len == 0) continue;

        st = padlock_aes_process_block(encrypt, &ctx, 
                                      (const uint8*)src.iov_base, 
                                      (uint8*)dst.iov_base, 
                                      src.iov_len);
        if (st != B_OK) break;
    }

    // Riporta l'IV aggiornato al chiamante
    if (request->iv)
        memcpy(request->iv, ctx.iv, 16);
        
    secure_memzero(&ctx, sizeof(ctx));
    return st;
}

status_t
BInitPadLockCrypto()
{
    // Verifichiamo se BCryptoCPU ha trovato PadLock
    if (!(BGetStoredCryptoCapabilities() & B_CRYPTO_HW_VIA_PADLOCK))
        return B_UNSUPPORTED;

    static BCryptoAlgorithm sPadLockAES = {
        .algorithm = B_CRYPTO_AES,
        .mode = B_CRYPTO_MODE_CBC,
        .flags = B_CRYPTO_ALG_HW_ACCEL,
        .name      = "AES CBC (VIA Padlock)",
        .priority = 85, // Poco sotto AES-NI come priorità
        .Process = padlock_process
    };

    return BRegisterCryptoAlgorithm(&sPadLockAES);
}

#else
status_t BInitPadLockCrypto() { return B_NOT_SUPPORTED; }
#endif
