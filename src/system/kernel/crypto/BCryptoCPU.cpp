/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include "BCryptoCapabilities.h"
#include <arch/x86/arch_cpu.h>
#include <debug.h> //da rimuovere una volta verificato


uint32
BGetCryptoCapabilities()
{
	dprintf("BCrypto: [3] Entrato in BGetCryptoCapabilities\n");
    uint32 caps = 0;

#if ARCH_X86 || ARCH_X86_64
	// 1. Feature standard trovate nel tuo arch_cpu.h
	if (x86_check_feature(IA32_FEATURE_EXT_AES)) {
		caps |= B_CPU_CRYPTO_AESNI;
		dprintf("BCrypto: AES-NI detected.\n");
	}
	if (x86_check_feature(IA32_FEATURE_SHA_NI)) {
		caps |= B_CPU_CRYPTO_SHAEXT;
		dprintf("BCrypto: SHA-NI (Extensions) detected.\n");
	}
	// 2. Controllo manuale per VIA PadLock (dato che manca nel kernel)
	cpuid_info info;
	get_current_cpuid(&info, 0xC0000000, 0);
	
	if (info.regs.eax >= 0xC0000001) {
		get_current_cpuid(&info, 0xC0000001, 0);
		
		dprintf("BCrypto: VIA Extended CPUID 0xC0000001.edx = 0x%08" B_PRIx32 "\n", info.regs.edx);
		
		// Controllo bit 2 e 3 per RNG (0xC = 1100 binario)
		if ((info.regs.edx & 0xC) == 0xC) {
			caps |= B_CPU_CRYPTO_PADLOCK_RNG;
			dprintf("BCrypto: VIA PadLock RNG detected and enabled.\n");
		}

		// Controllo bit 6 e 7 per ACE (AES hardware)
		if ((info.regs.edx & 0xC0) == 0xC0) {
			caps |= B_CPU_CRYPTO_PADLOCK_AES;
			dprintf("BCrypto: VIA PadLock ACE (AES) detected and enabled.\n");
		}
	} else {
		dprintf("BCrypto: VIA Extended CPUID not supported (EAX < 0xC0000001).\n");
	}
	if (caps == 0)
		dprintf("BCrypto: No hardware acceleration detected for this CPU.\n");
	else
		dprintf("BCrypto: Final caps bitmask: 0x%08" B_PRIx32 "\n", caps);
#endif

    return caps;
}

