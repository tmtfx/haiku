/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include <crypto/BCryptoDefs.h>
#include "BCryptoCPU.h"
#include <arch/x86/arch_cpu.h>
#include <string.h>
#include <debug.h>

bool gHasXsave = false;
uint64 gXsaveMask = 0;
uint32 gXsaveSize;

#define x86_cpuid(leaf, sub_leaf, eax, ebx, ecx, edx) \
    __asm__ volatile ("cpuid" \
        : "=a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx) \
        : "a" (leaf), "c" (sub_leaf))

#define SET_ALGO(info, algo) \
    (info)->algos_supported[B_GET_BLOCK(algo)] |= B_GET_BIT(algo)


void 
detect_cpu_xsave_size() {
    uint32 eax, ebx, ecx, edx;

    // CPUID foglia 0xD, sottofoglia 0
    x86_cpuid(0x0000000D, 0, eax, ebx, ecx, edx);

    // ebx = dimensione richiesta per le feature abilitate in XCR0
    gXsaveSize = ebx; 
    
    // Se per qualche motivo assurdo ebx è 0, usiamo un valore legacy sicuro
    if (gXsaveSize == 0)
        gXsaveSize = 512;

    dprintf("BCRYPTO: La CPU richiede %u byte per l'area XSAVE (XCR0 Mask: 0x%" B_PRIx64 ")\n", 
            gXsaveSize, gXsaveMask);
}


status_t
BGetCPUCryptoInfo(crypto_device_info* info)
{
    if (info == NULL)
        return B_BAD_VALUE;

    memset(info, 0, sizeof(crypto_device_info));
    strlcpy(info->vendor_name, "Unknown CPU", sizeof(info->vendor_name));

    uint32 eax, ebx, ecx, edx;

#if defined(__x86_64__) || defined(__i386__)
    // 1. Identificazione Vendor
    x86_cpuid(0, 0, eax, ebx, ecx, edx);
    if (ebx == 0x756e6547) strlcpy(info->vendor_name, "Intel", 32); 
    else if (ebx == 0x68747541 || ebx == 0x69746e65) strlcpy(info->vendor_name, "AMD", 32); 
    else if (ebx == 0x746e6543) strlcpy(info->vendor_name, "VIA", 32);
    
    if (x86_check_feature(IA32_FEATURE_EXT_XSAVE, FEATURE_EXT) &&
        x86_check_feature(IA32_FEATURE_EXT_OSXSAVE, FEATURE_EXT)) {
        gHasXsave = true;
        // Leggiamo la maschera XCR0 (questo va fatto comunque via assembly)
        uint32 low, high;
        asm volatile("xgetbv" : "=a" (low), "=d" (high) : "c" (0));
        gXsaveMask = ((uint64)high << 32) | low;
        detect_cpu_xsave_size();
    }
    // 1. AES-NI e RDRAND (Foglia 1, ECX -> FEATURE_EXT)
    if (x86_check_feature(IA32_FEATURE_EXT_AES, FEATURE_EXT)) {
        SET_ALGO(info, B_CRYPTO_AES);
        info->hw_type |= B_CRYPTO_HW_AES_NI;
    }
    
    if (x86_check_feature(IA32_FEATURE_EXT_RDRND, FEATURE_EXT)) {
        SET_ALGO(info, B_CRYPTO_RNG);
        info->hw_type |= B_CRYPTO_HW_RDRAND;
    }

    if (x86_check_feature(IA32_FEATURE_EXT_SSE4_1, FEATURE_EXT)) {
        info->hw_type |= B_CRYPTO_HW_SSE41;
    }

    // 2. AVX2 e SHA-NI (Foglia 7, EBX -> FEATURE_7_EBX)
    if (x86_check_feature(IA32_FEATURE_AVX2, FEATURE_7_EBX)) {
        info->hw_type |= B_CRYPTO_HW_AVX2;
    }

    if (x86_check_feature(IA32_FEATURE_SHA_NI, FEATURE_7_EBX)) {
        SET_ALGO(info, B_CRYPTO_SHA1);
        SET_ALGO(info, B_CRYPTO_SHA256);
        info->hw_type |= B_CRYPTO_HW_SHA_NI;
    }
    
    if (x86_check_feature(IA32_FEATURE_EXT_PCLMULQDQ, FEATURE_EXT)) {
        info->hw_type |= B_CRYPTO_GHASH_PCLMULQDQ;
    }

    // 3. VAES (Istruzioni AES vettoriali 256/512 bit - FEATURE_7_ECX)
    if (x86_check_feature(IA32_FEATURE_VAES, FEATURE_7_ECX)) {
        // Potresti voler aggiungere un flag B_CRYPTO_HW_VAES in futuro
    }
    // 4. Controllo VIA PadLock (Solo se è VIA)
    x86_cpuid(0, 0, eax, ebx, ecx, edx);
    if (ebx == 0x746e6543) {
        x86_cpuid(0xC0000000, 0, eax, ebx, ecx, edx);
        if (eax >= 0xC0000001) {
            x86_cpuid(0xC0000001, 0, eax, ebx, ecx, edx);
            if ((edx & 0x0C) == 0x0C) {
                //info->algos_supported |= B_CRYPTO_RNG;
                SET_ALGO(info, B_CRYPTO_RNG);
                info->hw_type |= B_CRYPTO_HW_PADLOCK_RNG;
            }
            if ((edx & 0xC0) == 0xC0) {
                //info->algos_supported |= B_CRYPTO_AES;
                SET_ALGO(info, B_CRYPTO_AES);
                info->hw_type |= B_CRYPTO_HW_VIA_PADLOCK;
            }
        }
    }
    
    // 5. Stima throughput
    info->max_throughput = (info->hw_type != 0) ? 1000 : 10; 

    return B_OK;
#else
    return B_NOT_SUPPORTED;
#endif
}
