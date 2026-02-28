/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include <crypto/BCryptoDefs.h>
#include "BCryptoCPU.h"
#include <arch/x86/arch_cpu.h>
#include <string.h> 

bool gHasSSE41 = false;
bool gHasAVX2 = false;

void bcrypto_detect_cpu_features() {
    gHasSSE41 = x86_check_feature(IA32_FEATURE_EXT_SSE4_1, FEATURE_EXT);
    gHasAVX2 = x86_check_feature(IA32_FEATURE_AVX2, FEATURE_7_EBX);
}


#define x86_cpuid(leaf, sub_leaf, eax, ebx, ecx, edx) \
    __asm__ volatile ("cpuid" \
        : "=a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx) \
        : "a" (leaf), "c" (sub_leaf))

#define SET_ALGO(info, algo) \
    (info)->algos_supported[B_GET_BLOCK(algo)] |= B_GET_BIT(algo)

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

    // 2. Controllo Standard (CPUID Leaf 1) -> AES-NI
    x86_cpuid(1, 0, eax, ebx, ecx, edx);
    if (ecx & (1 << 25)) {
        //info->algos_supported |= B_CRYPTO_AES;
        SET_ALGO(info, B_CRYPTO_AES);
        info->hw_type |= B_CRYPTO_HW_AES_NI;
    }
	// RDRAND è il bit 30 di ECX
    if (ecx & (1 << 30)) {
        //info->algos_supported |= B_CRYPTO_RNG;
        SET_ALGO(info, B_CRYPTO_RNG);
        info->hw_type |= B_CRYPTO_HW_RDRAND;
    }
    // 3. Controllo Extended Features (CPUID Leaf 7, Sub-leaf 0) -> SHA-NI
    // Prima verifichiamo che il foglio 7 sia supportato
    x86_cpuid(0, 0, eax, ebx, ecx, edx);
    if (eax >= 7) {
        x86_cpuid(7, 0, eax, ebx, ecx, edx);
        // SHA-NI è il bit 29 di EBX
        if (ebx & (1 << 29)) {
            //info->algos_supported |= B_CRYPTO_SHA1 | B_CRYPTO_SHA256;
            SET_ALGO(info, B_CRYPTO_SHA1);
            SET_ALGO(info, B_CRYPTO_SHA256);
            info->hw_type |= B_CRYPTO_HW_SHA_NI;
        }
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
