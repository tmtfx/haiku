/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
/*
#include "BCryptoCapabilities.h"
#include <arch/x86/arch_cpu.h>
#include <debug.h> //da rimuovere una volta verificato

#define x86_cpuid(leaf, eax, ebx, ecx, edx) \
    __asm__ volatile ("cpuid" \
        : "=a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx) \
        : "a" (leaf), "c" (0))

uint32
BGetCryptoCapabilities()
{
	dprintf("BCrypto: [3] Entrato in BGetCryptoCapabilities\n");
    uint32 caps = 0;
    uint32 eax = 0, ebx = 0, ecx = 0, edx = 0;

#if defined(__x86_64__) || defined(__x86__) || defined(__i386__)
	dprintf("BCrypto: [4] Sono in una cpu x86 compatibile\n");
	// 1. Feature standard trovate nel tuo arch_cpu.h
	if (x86_check_feature(IA32_FEATURE_EXT_AES, FEATURE_EXT)) {
        caps |= B_CPU_CRYPTO_AESNI;
        dprintf("BCrypto: AES-NI detected via x86_check_feature.\n");
    }

    // 2. SHA-NI (si trova in FEATURE_7_EBX, ovvero EBX del foglio 7)
    if (x86_check_feature(IA32_FEATURE_SHA_NI, FEATURE_7_EBX)) {
        caps |= B_CPU_CRYPTO_SHAEXT;
        dprintf("BCrypto: SHA-NI detected via x86_check_feature.\n");
    }
	// 2. Controllo manuale per VIA PadLock (dato che manca nel kernel)
	x86_cpuid(0, eax, ebx, ecx, edx);
    // Controllo rigoroso della stringa "CentaurHauls"
    if (ebx == 0x746e6543 && edx == 0x48727561 && ecx == 0x736c7561) {
        
        x86_cpuid(0xC0000000, eax, ebx, ecx, edx);
        if (eax >= 0xC0000001) {
            x86_cpuid(0xC0000001, eax, ebx, ecx, edx);
            
            dprintf("BCrypto: VIA Extended CPUID 0xC0000001.edx = 0x%08x\n", (unsigned int)edx);
            
            // Controllo bit 2 e 3 per RNG (0xC = 1100 binario)
            if ((edx & 0x0C) == 0x0C) {
                caps |= B_CPU_CRYPTO_PADLOCK_RNG;
                dprintf("BCrypto: VIA PadLock RNG rilevato e abilitato.\n");
            }

            // Controllo bit 6 e 7 per ACE (AES hardware) (0xC0 = 11000000 binario)
            if ((edx & 0xC0) == 0xC0) {
                caps |= B_CPU_CRYPTO_PADLOCK_AES;
                dprintf("BCrypto: VIA PadLock ACE (AES) rilevato e abilitato.\n");
            }
        } else {
            dprintf("BCrypto: VIA Extended CPUID non supportato (EAX < 0xC0000001).\n");
        }
    }
	if (caps == 0)
		dprintf("BCrypto: No hardware acceleration detected for this CPU.\n");
	else
		dprintf("BCrypto: Final caps bitmask: 0x%08" B_PRIx32 "\n", caps);
#endif

    return caps;
}
*/
//#include "BCryptoDefs.h"
#include <crypto/BCryptoDefs.h>
#include "BCryptoCPU.h"
#include <arch/x86/arch_cpu.h>
#include <string.h> 

#define x86_cpuid(leaf, sub_leaf, eax, ebx, ecx, edx) \
    __asm__ volatile ("cpuid" \
        : "=a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx) \
        : "a" (leaf), "c" (sub_leaf))

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
        info->algos_supported |= B_CRYPTO_AES;
        info->hw_type |= B_CRYPTO_HW_AES_NI;
    }

    // 3. Controllo Extended Features (CPUID Leaf 7, Sub-leaf 0) -> SHA-NI
    // Prima verifichiamo che il foglio 7 sia supportato
    x86_cpuid(0, 0, eax, ebx, ecx, edx);
    if (eax >= 7) {
        x86_cpuid(7, 0, eax, ebx, ecx, edx);
        // SHA-NI è il bit 29 di EBX
        if (ebx & (1 << 29)) {
            info->algos_supported |= B_CRYPTO_SHA1 | B_CRYPTO_SHA256;
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
                info->algos_supported |= B_CRYPTO_RNG;
                info->hw_type |= B_CRYPTO_HW_RNG;
            }
            if ((edx & 0xC0) == 0xC0) {
                info->algos_supported |= B_CRYPTO_AES;
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
