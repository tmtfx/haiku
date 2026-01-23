/*
 * Hardware-accelerated AES using VIA PadLock ACE
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include "PadLock.h"
#include "BCryptoCore.h"
#include "BCryptoDefs.h"
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
    if (length % 16 != 0)
        return B_BAD_VALUE;

    uint32 ctrl = 0; 
    switch (ctx->keyLength) {
        case 16: ctrl = 0x0080; break;
        case 24: ctrl = 0x00C0; break;
        case 32: ctrl = 0x0100; break;
        default: return B_BAD_VALUE;
    }
    if (encrypt) ctrl |= 0x200;

    // Definiamo i suffissi e i registri in base all'architettura
#ifdef __x86_64__
    #define PUSHF "pushfq"
    #define POPF  "popfq"
    #define ADDR_REG "r" // a 64-bit i puntatori sono a 64-bit
#else
    #define PUSHF "pushfl"
    #define POPF  "popfl"
    #define ADDR_REG "r"
#endif

    asm volatile(
        PUSHF "\n\t"
        POPF  "\n\t"        // Resetta i flag per ACE
        "xcryptcbc\n\t"
        : "+S"(in), "+D"(out)
        : "d"(ctrl), "b"(ctx->key), "a"(ctx->iv), "c"(length / 16)
        : "memory", "cc"
    );

    return B_OK;
}
/*
static status_t
padlock_aes_process_block(bool encrypt, PadLockAESContext* ctx,
                         const uint8* in, uint8* out, size_t length)
{
    if (length % 16 != 0)
        return B_BAD_VALUE;

    // ACE setup: EAX controlla l'operazione
    // Bit 0-3: Round count (automatico se 0)
    // Bit 7: Key size (0=128, 1=192, 2=256 - mappato diversamente in EAX)
    uint32 ctrl = 0; 
    switch (ctx->keyLength) {
        case 16: ctrl = 0x0080; break;
        case 24: ctrl = 0x00C0; break;
        case 32: ctrl = 0x0100; break;
        default: return B_BAD_VALUE;
    }
    if (encrypt) ctrl |= 0x200; // Bit per encryption su alcune versioni, o via flag dedicata

    // L'istruzione xcryptcbc su VIA richiede:
    // ESI: source, EDI: dest, EBX: key, EDX: control, EAX: IV (o puntatore a IV)
    // Nota: l'assembly inline per PadLock è molto specifico sull'allineamento.
    asm volatile(
        "pushfl\n\t"
        "popfl\n\t"        // Resetta i flag per ACE
        "xcryptcbc\n\t"
        : "+S"(in), "+D"(out)
        : "d"(ctrl), "b"(ctx->key), "a"(ctx->iv), "c"(length / 16)
        : "memory"
    );

    return B_OK;
}*/

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
        B_CRYPTO_AES,
        B_CRYPTO_MODE_CBC,
        B_CRYPTO_ALG_HW_ACCEL,
        85, // Poco sotto AES-NI come priorità
        padlock_process
    };

    return BRegisterCryptoAlgorithm(&sPadLockAES);
}

#else
status_t BInitPadLockCrypto() { return B_NOT_SUPPORTED; }
#endif
