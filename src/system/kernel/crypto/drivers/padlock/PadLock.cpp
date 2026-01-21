/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include "PadLock.h"

#include "../../BCryptoCore.h"
#include "../../BCryptoCapabilities.h"
#include "../../BCryptoDefs.h"

#include <string.h>

#if ARCH_X86
#include <arch/x86/arch_cpu.h>
#endif

static status_t
padlock_aes_process_block(bool encrypt,
                          //const PadLockAESContext* ctx,
                          PadLockAESContext* ctx,
                          const uint8* in,
                          uint8* out,
                          size_t length)
{
#if ARCH_X86
    if (length % 16 != 0)
        return B_BAD_VALUE;

    size_t blocks = length / 16;
	
	// ACE setup
    uint32 eax = 0; // EAX = key length + encrypt/decrypt flag
    switch (ctx->keyLength) {
        case 16: eax = 0x0080; break; // 128-bit
        case 24: eax = 0x00C0; break; // 192-bit
        case 32: eax = 0x0100; break; // 256-bit
        default: return B_BAD_VALUE;
    }
    if (encrypt) eax |= 0x1; // encrypt flag
	
    for (size_t i = 0; i < blocks; i++) {
        const uint8* src = in + i*16;
        uint8* dst       = out + i*16;

        asm volatile(
            "movdqu (%0), %%xmm0\n\t"    // carica blocco input
            "movdqu (%1), %%xmm1\n\t"    // carica IV
            "movdqu (%2), %%xmm2\n\t"    // carica key
            "mov %3, %%eax\n\t"          // EAX = encrypt + key len
            "xcryptcbc %%xmm0, %%xmm2\n\t"
            "movdqu %%xmm0, (%4)\n\t"    // store output
            :
            : "r"(src), "r"(ctx->iv), "r"(ctx->key), "r"(eax), "r"(dst)
            : "eax", "xmm0", "xmm1", "xmm2", "memory"
        );

        // Aggiorna IV
        if (encrypt)
            memcpy(ctx->iv, dst, 16);
        else
            memcpy(ctx->iv, src, 16);
    }

    return B_OK;
#else
    return B_NOT_SUPPORTED;
#endif
}

static status_t
padlock_process(BCryptoRequest* request)
{
	if (!request)
        return B_BAD_VALUE;
    //if (request->algorithm != B_CRYPTO_AES_CBC)
    if (request->algorithm != B_CRYPTO_AES)
        return B_NOT_SUPPORTED;
    if (request->mode != B_CRYPTO_MODE_CBC)
        return B_NOT_SUPPORTED;
        
    if (request->keyLength != 16 &&
        request->keyLength != 24 &&
        request->keyLength != 32)
        return B_BAD_VALUE;

    if (request->ivLength != 16)
        return B_BAD_VALUE;

    PadLockAESContext ctx{};
    memcpy(ctx.key, request->key, request->keyLength);
    memcpy(ctx.iv, request->iv, 16);
    ctx.keyLength = request->keyLength;

    bool encrypt = (request->operation == B_CRYPTO_ENCRYPT);
    status_t st = B_OK;

    for (size_t i = 0; i < request->vectorCount; i++) {
        const iovec& src = request->source[i];
        iovec& dst       = request->destination[i];

        if (src.iov_len != dst.iov_len)
            return B_BAD_VALUE;
        if (src.iov_len % 16 != 0)
            return B_BAD_VALUE;

        st = padlock_aes_process_block(
            encrypt,
            &ctx,
            (const uint8*)src.iov_base,
            (uint8*)dst.iov_base,
            src.iov_len
        );

        if (st != B_OK)
            break;
    }

    memcpy(request->iv, ctx.iv, 16);
    explicit_bzero(&ctx, sizeof(ctx));
    
    if (request->completionCallback) {
        request->completionCallback(request, st);
        return B_OK; // in async, ritorna sempre B_OK
    }

    return st;
}



status_t
BInitPadLockCrypto()
{
    uint32 caps = BGetCryptoCapabilities();
    if (!(caps & B_CPU_CRYPTO_PADLOCK))
        return B_UNSUPPORTED;

    static BCryptoAlgorithm sPadLockAES = {
        .algorithm = B_CRYPTO_AES,
        .mode      = B_CRYPTO_MODE_CBC,
        .flags     = B_CRYPTO_ALG_HW_ACCEL,
        .priority  = 90,
        .Process   = padlock_process
    };

    return BRegisterCryptoAlgorithm(&sPadLockAES);
}



