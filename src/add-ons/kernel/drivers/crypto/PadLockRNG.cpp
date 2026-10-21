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
static status_t
padlock_rng_generate(uint8* buffer, size_t length)
{
    size_t generated = 0;

    while (generated < length) {
        alignas(16) uint32 value[4] = {0};
        uint32 bytesGenerated = 0;
        uint32 statusFlag = 1;

        int retryCount = 5000;
        while (retryCount-- > 0) {
            uint32 result_eax = 0;
            uint32 control_edx = 0; // 0 = qualità standard / nessun filtro extra

            asm volatile (
                // xstore scrive all'indirizzo in EDI (o RDI in 64-bit)
                // EDX = Control word / input metrics
                // EAX = restituirà lo stato/lunghezza scritti a seconda dell'hardware
                ".byte 0x0f, 0xa7, 0xc0\n\t" 
                "setc %b[err]\n\t"           // Setta il byte di errore se c'è Carry
                : "=a" (result_eax),
                  [err] "=q" (statusFlag)
                : "D" (value),
                  "d" (control_edx)
                : "memory", "cc"
            );

            bytesGenerated = result_eax & 0xFFFF; // Estrae i byte generati effettivi

            if (statusFlag == 0 && bytesGenerated > 0)
                break; 

            #if defined(__x86_64__) || defined(__i386__)
            asm volatile("pause" ::: "memory");
            #endif
        }

        if (statusFlag != 0 || bytesGenerated == 0) {
            snooze(50);
            continue;
        }

        size_t toCopy = (length - generated < bytesGenerated) 
            ? (length - generated) : bytesGenerated;
        
        memcpy(buffer + generated, value, toCopy);
        generated += toCopy;
    }

    return B_OK;
}
/* crasha
static status_t
padlock_rng_generate(uint8* buffer, size_t length)
{
    size_t generated = 0;

    while (generated < length) {
        // 1. Allineamento rigoroso a 16 byte per xstore
        alignas(16) uint32 value[4] = {0};
        uint32 statusFlag = 1;
        uint32 bytesGenerated = 0;

        // 2. Loop con tentativi limitati per evitare il freeze infinito
        int retryCount = 5000;
        while (retryCount-- > 0) {
            asm volatile (
                "xorl %%edx, %%edx\n\t"     // EDX = 0 (fattore di qualità/nessun filtro)
                ".byte 0x0f, 0xa7, 0xc0\n\t" // Opcode esplicito per xstore (Compatibile con tutti i GCC)
                "setc %%cl\n\t"              // CL = 1 se Carry (errore/dati non pronti)
                "movzbl %%cl, %0\n\t"
                "movl %%eax, %1\n\t"         // EAX restituisce i byte scritti
                : "=r"(statusFlag), "=r"(bytesGenerated)
                : "D"(value)                 // RDI/EDI punta al buffer allineato a 16 byte
                : "eax", "edx", "ecx", "memory", "cc"
            );

            if (statusFlag == 0 && bytesGenerated > 0)
                break; // Dati pronti ed estratti con successo

            // Breve riposo per le pipeline della CPU
            #if defined(__x86_64__) || defined(__i386__)
            asm volatile("pause" ::: "memory");
            #endif
        }

        // 3. Se l'hardware non risponde dopo i tentativi, cedi il controllo allo scheduler di Haiku
        if (statusFlag != 0 || bytesGenerated == 0) {
            snooze(50); // Cede il passo per 50 microsecondi
            continue;
        }

        size_t toCopy = (length - generated < bytesGenerated) 
            ? (length - generated) : bytesGenerated;
        
        memcpy(buffer + generated, value, toCopy);
        generated += toCopy;
    }

    return B_OK;
}*/
/*
static status_t
padlock_rng_generate(uint8* buffer, size_t length)
{
    size_t generated = 0;
    
    while (generated < length) {
        uint32 value[2]; // Usiamo 8 byte per sicurezza, xstore è avido
        uint32 statusFlag;

        //
        // xstore (VIA PadLock RNG):
        // EDI = puntatore alla destinazione
        // EDX = 0 (qualità massima/nessun filtraggio hardware extra)
        // Risultato:
        // EAX = numero di byte generati (di solito 0 o 8)
        // Carry Flag = 0 se successo, 1 se errore/dati non pronti
        //
        asm volatile (
            "movl $0, %%edx\n\t"        // Qualità 0
            "xstore\n\t"                // Scrive in (EDI)
            "setc %%al\n\t"             // AL = 1 se Carry è settato (errore)
            "movzbl %%al, %1\n\t"       // statusFlag = AL
            : "=D"(value[0]), "=r"(statusFlag)
            : "D"(&value[0])            // EDI punta al nostro buffer locale
            : "eax", "edx", "memory", "cc"
        );

        if (statusFlag != 0) {
            // Se il chip non è pronto, facciamo un brevissimo relax e riproviamo
            // o ritorniamo errore se preferisci non bloccare
            continue; 
        }

        // xstore scrive solitamente 8 byte, ma noi ne copiamo solo quelli necessari
        size_t available = 8; 
        size_t toCopy = (length - generated < available) ? (length - generated) : available;
        
        memcpy(buffer + generated, value, toCopy);
        generated += toCopy;
    }
    
    return B_OK;
}*/

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
