/*
 * Hardware-accelerated AES using VIA PadLock ACE
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include "PadLock.h"
#include "BCryptoCore.h"
#include <crypto/BCryptoDefs.h>
#include "BCryptoCPU.h" // Per BGetStoredCryptoCapabilities
#include <debug.h>
#include <string.h>

#define VIA_PADLOCK_MSR 0x1107
#define VIA_RNG_ENABLE  (1 << 3) // Bit 3: RNG Enable
#define VIA_ACE_ENABLE  (1 << 0) // Bit 0: ACE (Advanced Cryptography Engine) Enable

#if defined(__x86_64__) || defined(__i386__)

static void
secure_memzero(void* p, size_t s)
{
    if (p == NULL) return;
    volatile uint8* cp = (volatile uint8*)p;
    while (s--) *cp++ = 0;
}

static void
enable_padlock_hardware(void)
{
    uint32 low, high;

    asm volatile (
        "rdmsr"
        : "=a" (low), "=d" (high)
        : "c" (VIA_PADLOCK_MSR)
    );

    uint64 msr = ((uint64)high << 32) | low;
    uint64 required_flags = VIA_RNG_ENABLE | VIA_ACE_ENABLE;

    if ((msr & required_flags) != required_flags) {
        msr |= required_flags;
        low = (uint32)(msr & 0xFFFFFFFF);
        high = (uint32)(msr >> 32);

        asm volatile (
            "wrmsr"
            :
            : "c" (VIA_PADLOCK_MSR), "a" (low), "d" (high)
        );

        dprintf("PADLOCK: Hardware ACE & RNG abilitato globalmente via MSR 0x1107 (valore: 0x%lx)\n", msr);
    }
}
/*
static status_t
padlock_aes_process_block(bool encrypt, PadLockAESContext* ctx,
                          const uint8* in, uint8* out, size_t length)
{
    // 1. Controllo rigoroso dell'allineamento a 16 byte (Requisito VIA PadLock)
    if (((uintptr_t)in & 0xF) != 0 || ((uintptr_t)out & 0xF) != 0 ||
        ((uintptr_t)ctx->key & 0xF) != 0 || ((uintptr_t)ctx->iv & 0xF) != 0) {
        dprintf("PADLOCK: Buffer non allineato a 16 byte! src:%p dst:%p key:%p iv:%p\n", 
            in, out, ctx->key, ctx->iv);
        return B_BAD_VALUE;
    }
        
    if (length % 16 != 0) {
        dprintf("PADLOCK: lunghezza non multipla di 16\n");
        return B_BAD_VALUE;
    }

    // 2. Control Word per VIA Nano / ACE2
    uint32 ctrl = 0; 
    switch (ctx->keyLength) {
        case 16: ctrl = 0x0;   break; // AES-128
        case 24: ctrl = 0x400; break; // AES-192
        case 32: ctrl = 0x800; break; // AES-256
        default: return B_BAD_VALUE;
    }
    if (encrypt) ctrl |= 0x200;

    // Control Word statica allineata a 16 byte nello spazio kernel
    alignas(16) static uint32 s_control_word;
    s_control_word = ctrl;

    size_t blocks = length / 16;

    const uint8* src = in;
    uint8* dst = out;
    uint8* iv = ctx->iv;
    const uint8* key_ptr = ctx->key;
    uint32* cw_ptr = &s_control_word;

    // Esecuzione diretta di xcryptcbc senza overhead di copia dei dati
    // ESI = src, EDI = dst, EAX = iv, EBX = key_ptr, EDX = cw_ptr, ECX = blocks
    asm volatile(
        "pushfq\n\t"
        "popfq\n\t"
        ".byte 0x0f, 0xa7, 0xe8\n\t" // xcryptcbc
        : "+S" (src),
          "+D" (dst),
          "+a" (iv),
          "+c" (blocks)
        : "d" (cw_ptr),
          "b" (key_ptr)
        : "memory", "cc"
    );

    return B_OK;
}*/
/* ritorna B_BAD_VALUE perché i blocchi non sono allineati a 16byte
static status_t
padlock_aes_process_block(bool encrypt, PadLockAESContext* ctx,
                          const uint8* in, uint8* out, size_t length)
{
    // Se l'input o l'output non sono allineati a 16 byte, anziché rischiare un Invalid Opcode 
    // con una malloc disallineata, restituiamo B_BAD_VALUE (il framework userà il fallback software).
    if (((uintptr_t)in & 0xF) != 0 || ((uintptr_t)out & 0xF) != 0) {
        dprintf("PADLOCK: Buffer non allineato a 16 byte (src:%p dst:%p). Skip hardware.\n", in, out);
        return B_BAD_VALUE;
    }
        
    if (length == 0 || length % 16 != 0) {
        return B_BAD_VALUE;
    }

    uint32 ctrl = 0; 
    switch (ctx->keyLength) {
        case 16: ctrl = 0x0;   break; // AES-128
        case 24: ctrl = 0x400; break; // AES-192
        case 32: ctrl = 0x800; break; // AES-256
        default: return B_BAD_VALUE;
    }
    if (encrypt) ctrl |= 0x200;

    alignas(16) static uint32 s_control_word;
    s_control_word = ctrl;

    size_t blocks = length / 16;

    const uint8* src = in;
    uint8* dst = out;
    uint8* iv = ctx->iv;
    const uint8* key_ptr = ctx->key;
    uint32* cw_ptr = &s_control_word;

    // Esecuzione diretta e sicura al 100% con Zero-Copy su buffer nativamente allineati
    asm volatile(
        "pushfq\n\t"
        "popfq\n\t"
        ".byte 0x0f, 0xa7, 0xe8\n\t" // xcryptcbc
        : "+S" (src),
          "+D" (dst),
          "+a" (iv),
          "+c" (blocks)
        : "d" (cw_ptr),
          "b" (key_ptr)
        : "memory", "cc"
    );

    return B_OK;
}*/
/* invalid op code!
static status_t
padlock_aes_process_block(bool encrypt, PadLockAESContext* ctx,
                                    const uint8* in, uint8* out, size_t length)
{
    if (length % 16 != 0)
        return B_BAD_VALUE;

    uint32 ctrl = 0; 
    switch (ctx->keyLength) {
        case 16: ctrl = 0x0;   break;
        case 24: ctrl = 0x400; break;
        case 32: ctrl = 0x800; break;
        default: return B_BAD_VALUE;
    }
    if (encrypt) ctrl |= 0x200;

    alignas(16) static uint32 s_control_word;
    s_control_word = ctrl;

    size_t total_blocks = length / 16;
    const uint8* key_ptr = ctx->key;
    uint32* cw_ptr = &s_control_word;

    // Buffer di transito locale allineato a 16 byte (stile AES-NI)
    // Questo ci permette di accettare qualsiasi puntatore in/out (anche non allineati)
    // senza mai fare crashare l'hardware Padlock!
    alignas(16) uint8 block_in[16];
    alignas(16) uint8 block_out[16];
    alignas(16) uint8 current_iv[16];
    
    // Copia l'IV iniziale nel nostro contesto locale
    memcpy(current_iv, ctx->iv, 16);

    for (size_t i = 0; i < total_blocks; i++) {
        // Caricamento sicuro (anche da puntatori non allineati, esattamente come _mm_loadu_si128)
        memcpy(block_in, in + (i * 16), 16);

        // Per ogni singolo blocco, xcryptcbc elabora 1 blocco (blocks = 1)
        size_t blocks = 1;
        const uint8* src = block_in;
        uint8* dst = block_out;
        uint8* iv_ptr = current_iv;

        asm volatile(
            "pushfq\n\t"
            "popfq\n\t"
            ".byte 0x0f, 0xa7, 0xe8\n\t" // xcryptcbc
            : "+S" (src),
              "+D" (dst),
              "+a" (iv_ptr),
              "+c" (blocks)
            : "d" (cw_ptr),
              "b" (key_ptr)
            : "memory", "cc"
        );

        // Salvataggio sicuro nel buffer di output (anche non allineato, come _mm_storeu_si128)
        memcpy(out + (i * 16), block_out, 16);
    }

    // Restituiamo l'IV aggiornato al contesto
    memcpy(ctx->iv, current_iv, 16);

    return B_OK;
}*/
static status_t
padlock_aes_process_block(bool encrypt, PadLockAESContext* ctx,
                                    const uint8* in, uint8* out, size_t length)
{
    if (length % 16 != 0)
        return B_BAD_VALUE;

    uint32 ctrl = 0; 
    switch (ctx->keyLength) {
        case 16: ctrl = 0x0;   break;
        case 24: ctrl = 0x400; break;
        case 32: ctrl = 0x800; break;
        default: return B_BAD_VALUE;
    }
    if (encrypt) ctrl |= 0x200;

    alignas(16) static uint32 s_control_word;
    s_control_word = ctrl;

    size_t total_blocks = length / 16;
    const uint8* key_ptr = ctx->key; // Ora ctx->key è garantito allineato a 16 byte dalla struct!
    uint32* cw_ptr = &s_control_word;

    alignas(16) uint8 block_in[16];
    alignas(16) uint8 block_out[16];
    alignas(16) uint8 current_iv[16];
    
    memcpy(current_iv, ctx->iv, 16);

    for (size_t i = 0; i < total_blocks; i++) {
        memcpy(block_in, in + (i * 16), 16);

        size_t blocks = 1;
        const uint8* src = block_in;
        uint8* dst = block_out;
        uint8* iv_ptr = current_iv;

        asm volatile(
            "pushfq\n\t"
            "popfq\n\t"
            "rep xcryptcbc\n\t"
            : "+S" (src),
              "+D" (dst),
              "+a" (iv_ptr),
              "+c" (blocks)
            : "d" (cw_ptr),
              "b" (key_ptr)
            : "memory", "cc"
        );

        memcpy(out + (i * 16), block_out, 16);
    }

    memcpy(ctx->iv, current_iv, 16);

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
    
    enable_padlock_hardware();

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
