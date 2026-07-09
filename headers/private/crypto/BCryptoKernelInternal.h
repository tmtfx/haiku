/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef _B_CRYPTO_KERNEL_INTERNAL_H_
#define _B_CRYPTO_KERNEL_INTERNAL_H_

#include <OS.h>
#include <SupportDefs.h>
#include <iovec.h>
#include <crypto/BCryptoDefs.h>
#include <arch/x86/arch_cpu.h>

#define B_MAX_XSAVE_SIZE 5120

struct BCryptoFPUContext {
    uint8 state[B_MAX_XSAVE_SIZE] __attribute__((aligned(64))); //servono 2560 ma per test mettimo pagina intera
} __attribute__((aligned(64)));


struct BCryptoRequest {
    BCryptoOperation		operation;
    BCryptoAlgorithmID		algorithm;
    BCryptoMode				mode;
    uint32					flags; 
    const void*				key;
    size_t					keyLength;
    void*					iv;
    size_t					ivLength;
    const iovec*			source;
    iovec*					destination;
    size_t					vectorCount;
    const uint8*            aad;
    size_t                  aadLength;
    status_t				(*completionCallback)(BCryptoRequest*, status_t);
    void*					userCookie;
};

struct crypto_session {
	BCryptoOperation    op;
    BCryptoAlgorithmID  algorithm;
    void*               algorithm_state; // Puntatore al contesto specifico (es: SHA256_CTX)
    BCryptoMode         mode;
    size_t              state_size;
    bool                is_active;       // True se Init è stata chiamata
};

struct UserAccessExposer {
    UserAccessExposer() {
    	if (x86_check_feature(IA32_FEATURE_SMAP, FEATURE_7_EBX))
            __asm__ __volatile__ ("stac" : : : "cc");
    }
    ~UserAccessExposer() {
    	if (x86_check_feature(IA32_FEATURE_SMAP, FEATURE_7_EBX))
            __asm__ __volatile__ ("clac" : : : "cc");
    }
};


// Prototipi delle funzioni di salvataggio/ripristino
extern "C" {
    bool bcrypto_save_regs(BCryptoFPUContext* ctx);
    void bcrypto_restore_regs(BCryptoFPUContext* ctx);
}
#endif // _B_CRYPTO_KERNEL_INTERNAL_H_
