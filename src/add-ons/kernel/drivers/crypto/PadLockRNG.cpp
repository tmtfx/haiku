/*
 * VIA PadLock Hardware RNG
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include <string.h>         // Per memcpy
#include <KernelExport.h>   // Per utilità kernel generali
#include <SupportDefs.h>    // Per i tipi come status_t, uint8, ecc.
#include "PadLockRNG.h"
#include "BCryptoCPU.h"
#include "BCryptoCore.h"
#include "BCryptoDevice.h"
#include <cstring>

#if defined(__x86_64__) || defined(__i386__)

#define VIA_PADLOCK_MSR 0x1107
#define VIA_RNG_ENABLE  (1 << 3) // Bit 3: RNG Enable
#define VIA_ACE_ENABLE  (1 << 0) // Bit 0: ACE (Advanced Cryptography Engine) Enable


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

static status_t
padlock_rng_generate(uint8* buffer, size_t length)
{
    size_t generated = 0;

    while (generated < length) {
        alignas(16) uint8 temp[16] = {0};
        
        size_t requestedBytes = (length - generated >= 8) ? 8 : (length - generated);
        uint32 count = (uint32)requestedBytes;
        uint32 control_edx = 0;
        uint32 statusFlag = 1;
        uint8* ptr = temp;

        // Limitiamo rigidamente i tentativi per evitare il blocco totale della CPU al 100%
        int retryCount = 1000;
        bool success = false;

        while (retryCount-- > 0) {
            count = (uint32)requestedBytes;
            ptr = temp;

            asm volatile (
                "rep\n\t"
                ".byte 0x0f, 0xa7, 0xc0\n\t" // REP XSTORE
                "setc %b[err]\n\t"
                : [err] "=q" (statusFlag),
                  "+c" (count),
                  "+D" (ptr)
                : [ctrl] "d" (control_edx)
                : "memory", "cc"
            );

            if (statusFlag == 0) {
                success = true;
                break;
            }

            // Pausa per lasciare respirare il bus e la CPU
            asm volatile("pause" ::: "memory");
        }

        // Se dopo 1000 tentativi l'hardware non ha sputato dati, 
        // tracciamo l'errore nel log del kernel ed usciamo dal driver (restituendo errore)
        // anziché tenere la CPU incollata al 100% per sempre.
        if (!success) {
            dprintf("PADLOCK RNG ERROR: Nano X2 non rilascia entropia (status: %u, count rimasto: %u)\n", 
                statusFlag, count);
            return B_ERROR;
        }

        size_t bytesWritten = requestedBytes - count;
        if (bytesWritten == 0) bytesWritten = requestedBytes;

        size_t toCopy = (length - generated < bytesWritten) ? (length - generated) : bytesWritten;
        if (toCopy == 0) toCopy = requestedBytes;

        memcpy(buffer + generated, temp, toCopy);
        generated += toCopy;
    }

    return B_OK;
}

static status_t
padlock_rng_process(BCryptoRequest* request)
{
    // Il Core passa per BCryptoRequest, noi estraiamo i buffer
    for (size_t i = 0; i < request->vectorCount; i++) {
        status_t st = padlock_rng_generate(
            (uint8*)request->destination[i].iov_base, 
            request->destination[i].iov_len
        );
        if (st != B_OK) return st;
    }
    return B_OK;
}

status_t
BInitPadLockRNG()
{
    if (!(BGetStoredCryptoCapabilities() & B_CRYPTO_HW_PADLOCK_RNG))
        return B_UNSUPPORTED;
    
    enable_padlock_hardware();

    static BCryptoAlgorithm sPadLockRNG = {
        .algorithm = B_CRYPTO_RNG,
        .mode = B_CRYPTO_MODE_ANY,
        .flags = B_CRYPTO_ALG_HW_ACCEL,
        .name      = "RNG (VIA Padlock)",
        .priority = 100, // Massima priorità per l'hardware RNG
        .Process = padlock_rng_process
    };

    return BRegisterCryptoAlgorithm(&sPadLockRNG);
}
#else
status_t BInitPadLockRNG() { return B_NOT_SUPPORTED; }
#endif
