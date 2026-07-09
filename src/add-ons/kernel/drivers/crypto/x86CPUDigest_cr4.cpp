/*
 * Intel/AMD Hardware SHA-NI (Accelerated Digest)
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include "x86CPUDigest.h"
#include "BCryptoCPU.h"
#include "BCryptoCore.h"
#include <crypto/BCryptoDefs.h>
#include "BCryptoAlgorithm.h"
#include <string.h>
#include <immintrin.h>
#include <arch/x86/arch_cpu.h> //
#include <arch/x86/arch_cpuasm.h>
#include <debug.h>

#if defined(__x86_64__) || defined(__i386__)

#pragma GCC target("sha,sse4.1")
/*
static const uint32_t K256[64] alignas(16) = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,
    0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
    0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,
    0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,
    0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
    0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,
    0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,
    0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
    0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};*/

static const __m128i MASK_ENDIAN = _mm_set_epi8(12,13,14,15, 8,9,10,11, 4,5,6,7, 0,1,2,3);
/*#define SHA256_RND(msg0, msg1, msg2, msg3, st0, st1, k_idx) \
    { \
        __m128i k_vec = _mm_loadu_si128((const __m128i*)&K256[k_idx]); \
        st1 = _mm_sha256rnds2_epu32(st1, st0, _mm_add_epi32(msg0, k_vec)); \
        st0 = _mm_sha256rnds2_epu32(st0, st1, _mm_add_epi32(_mm_alignr_epi8(msg0, msg0, 8), k_vec)); \
        msg0 = _mm_sha256msg1_epu32(msg0, msg1); \
        msg0 = _mm_add_epi32(msg0, _mm_sha256msg2_epu32(_mm_alignr_epi8(msg3, msg2, 4), msg3)); \
    }*/
#undef xgetbv
#define xgetbv(reg) ({ \
    uint32_t _low, _high; \
    __asm__ volatile ("xgetbv" : "=a" (_low), "=d" (_high) : "c" (reg)); \
    ((uint64_t)_low | ((uint64_t)_high << 32)); \
})

#undef xsetbv
#define xsetbv(reg, value) { \
    uint64_t _val = (uint64_t)(value); \
    uint32_t _low = (uint32_t)_val; \
    uint32_t _high = (uint32_t)(_val >> 32); \
    __asm__ volatile ("xsetbv" : : "a" (_low), "d" (_high), "c" (reg)); \
}
/*
status_t
x86_sha256_process_finalmente(BCryptoRequest* request)
{
    // 1. Impedisci context switch e interrupt
    // Se il kernel ci spostasse su un altro core o cambiasse thread
    // mentre abbiamo CR4 alterato, sarebbe un disastro (Kernel Panic).
    cpu_status cpu_state = disable_interrupts();

    // 2. Leggi lo stato attuale
    size_t old_cr4 = x86_read_cr4();
    uint64 old_xcr0 = xgetbv(0);

    // 3. ABILITA IL MONDO
    // IA32_CR4_OSXSAVE è il bit 18. Senza questo, xsetbv genera un'eccezione.
    x86_write_cr4(old_cr4 | (1UL << 18));
    
    // Abilitiamo x87 (bit 0), SSE (bit 1) e AVX (bit 2).
    // SHA-NI richiede formalmente che lo stato SSE sia abilitato nel sistema.
    xsetbv(0, old_xcr0 | 0x7);
    
    __m128i abcd = _mm_setr_epi32(0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a);
    __m128i efgh = _mm_setr_epi32(0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19);
    
    
    
    uint8_t* data = (uint8_t*)request->source[0].iov_base;
    size_t len = request->source[0].iov_len;
    uint32_t* hash_out = (uint32_t*)request->destination[0].iov_base;
    
    alignas(16) uint32_t msg[16] = {0};
    
    // Copiamo "abc" (3 byte)
    memcpy(msg, data, len); 

    // Aggiungiamo il bit di stop 0x80 subito dopo
    ((uint8_t*)msg)[len] = 0x80;

    // SHA-256 vuole la lunghezza totale in BIT alla fine del blocco (64-bit Big Endian)
    // "abc" = 3 byte = 24 bit.
    // 24 in hex è 0x18.
    // Lo mettiamo nell'ultima posizione (msg[15] è l'ultima parola da 32 bit)
    msg[15] = __builtin_bswap32(len * 8);
    
    // Maschera per conversione Little-Endian -> Big-Endian (Solo per il messaggio!)
    __m128i mask = _mm_setr_epi8(3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12);

    // Caricamento e raddrizzamento messaggio
    __m128i m0 = _mm_shuffle_epi8(_mm_loadu_si128((__m128i*)&msg[0]), mask);
    __m128i m1 = _mm_shuffle_epi8(_mm_loadu_si128((__m128i*)&msg[4]), mask);
    __m128i m2 = _mm_shuffle_epi8(_mm_loadu_si128((__m128i*)&msg[8]), mask);
    __m128i m3 = _mm_shuffle_epi8(_mm_loadu_si128((__m128i*)&msg[12]), mask);

    // 3. ROUNDS (Senza shuffle su K, come discusso)
    #define SHA256_STEP(msg_part, k_idx) { \
        __m128i k = _mm_loadu_si128((__m128i*)&K256[k_idx]); \
        __m128i msg_k = _mm_add_epi32(msg_part, k); \
        abcd = _mm_sha256rnds2_epu32(abcd, efgh, msg_k); \
        msg_k = _mm_shuffle_epi32(msg_k, 0x0E); \
        efgh = _mm_sha256rnds2_epu32(efgh, abcd, msg_k); \
    }

    #define SHA256_MSG_SCHED(w0, w1, w2, w3) \
        w0 = _mm_sha256msg1_epu32(w0, w1); \
        w0 = _mm_add_epi32(w0, _mm_alignr_epi8(w3, w2, 4)); \
        w0 = _mm_sha256msg2_epu32(w0, w3);

    // Esecuzione 64 Round
    SHA256_STEP(m0, 0);  SHA256_STEP(m1, 4);  SHA256_STEP(m2, 8);  SHA256_STEP(m3, 12);
    SHA256_MSG_SCHED(m0, m1, m2, m3); SHA256_STEP(m0, 16);
    SHA256_MSG_SCHED(m1, m2, m3, m0); SHA256_STEP(m1, 20);
    SHA256_MSG_SCHED(m2, m3, m0, m1); SHA256_STEP(m2, 24);
    SHA256_MSG_SCHED(m3, m0, m1, m2); SHA256_STEP(m3, 28);
    SHA256_MSG_SCHED(m0, m1, m2, m3); SHA256_STEP(m0, 32);
    SHA256_MSG_SCHED(m1, m2, m3, m0); SHA256_STEP(m1, 36);
    SHA256_MSG_SCHED(m2, m3, m0, m1); SHA256_STEP(m2, 40);
    SHA256_MSG_SCHED(m3, m0, m1, m2); SHA256_STEP(m3, 44);
    SHA256_MSG_SCHED(m0, m1, m2, m3); SHA256_STEP(m0, 48);
    SHA256_MSG_SCHED(m1, m2, m3, m0); SHA256_STEP(m1, 52);
    SHA256_MSG_SCHED(m2, m3, m0, m1); SHA256_STEP(m2, 56);
    SHA256_MSG_SCHED(m3, m0, m1, m2); SHA256_STEP(m3, 60);

    // 4. ESTRATTORE CORRETTO (La chiave è qui)
    alignas(16) uint32_t res_abcd[4];
    alignas(16) uint32_t res_efgh[4];
    _mm_store_si128((__m128i*)res_abcd, abcd);
    _mm_store_si128((__m128i*)res_efgh, efgh);

    uint32_t* out = hash_out;
    
    // Lo stato iniziale da sommare
    const uint32_t H[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };

    // Mappatura Intel: res[0]=D, res[1]=C, res[2]=B, res[3]=A
    // Quindi:
    out[0] = __builtin_bswap32(res_abcd[3] + H[0]); // A
    out[1] = __builtin_bswap32(res_abcd[2] + H[1]); // B
    out[2] = __builtin_bswap32(res_abcd[1] + H[2]); // C
    out[3] = __builtin_bswap32(res_abcd[0] + H[3]); // D

    out[4] = __builtin_bswap32(res_efgh[3] + H[4]); // E
    out[5] = __builtin_bswap32(res_efgh[2] + H[5]); // F
    out[6] = __builtin_bswap32(res_efgh[1] + H[6]); // G
    out[7] = __builtin_bswap32(res_efgh[0] + H[7]); // H

    // ------------------------------------------------------------------
    // 5. RIPRISTINO (Fondamentale per la stabilità di Haiku)
    // ------------------------------------------------------------------
    xsetbv(0, old_xcr0);
    x86_write_cr4(old_cr4);

    restore_interrupts(cpu_state);

    return B_OK;
}*/


status_t
x86_sha256_process_test(BCryptoRequest* request)
{
    cpu_status cpu_state = disable_interrupts();
    bcrypto_save_regs(&ctx->fpu_save);
    /* test registri leggono a cazzo
    asm volatile (
        "pxor %%xmm0, %%xmm0 \n\t"
        "pxor %%xmm1, %%xmm1 \n\t"
        "pxor %%xmm2, %%xmm2 \n\t"
        // ... ripetere per tutti i registri necessari ...
        : : : "xmm0","xmm1","xmm2","xmm3","xmm4","xmm5","xmm6","xmm7","xmm10"
    );

    // 1. STATO INIZIALE (A-H)
    // Intel SHA-NI: abcd = [D, C, B, A], efgh = [H, G, F, E]
    __m128i abcd = _mm_set_epi32(0xa54ff53a, 0x3c6ef372, 0xbb67ae85, 0x6a09e667);
    __m128i efgh = _mm_set_epi32(0x5be0cd19, 0x1f83d9ab, 0x9b05688c, 0x510e527f);
    
    __m128i m0 = _mm_set_epi32(0, 0, 0, 0x80000000); // Primo blocco del vuoto (Big Endian)
    __m128i m1 = _mm_setzero_si128();
    __m128i m2 = _mm_setzero_si128();
    __m128i m3 = _mm_setzero_si128();

    // ESEGUIAMO SOLO I PRIMI 4 ROUND (così controlliamo se la base è solida)
    // K[0..3] = 0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5
    __m128i k0 = _mm_set_epi32(0xe9b5dba5, 0xb5c0fbcf, 0x71374491, 0x428a2f98);
    
    __m128i msg_k = _mm_add_epi32(m0, k0);
    abcd = _mm_sha256rnds2_epu32(abcd, efgh, msg_k);
    msg_k = _mm_shuffle_epi32(msg_k, 0x0E);
    efgh = _mm_sha256rnds2_epu32(efgh, abcd, msg_k);

    // Estraiamo il risultato parziale dopo soli 4 round
    alignas(16) uint32_t res[4];
    _mm_store_si128((__m128i*)res, abcd);
*/
    uint32_t *out = (uint32_t *)request->destination[0].iov_base;

	uint32_t eax, ebx, ecx, edx;

	// 1. Interroghiamo Leaf 7, Subleaf 0 (per SHA-NI)
	// Usiamo l'assembly diretto per evitare errori di header
	eax = 7;
	ecx = 0;
	asm volatile (
		"cpuid"
		: "=a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx)
		: "a" (eax), "c" (ecx)
	);

	// Bit 29 di EBX: Supporto SHA-NI
	bool hasSHA = (ebx >> 29) & 0x1;

	// 2. Interroghiamo Leaf 1 (per OSXSAVE)
	eax = 1;
	asm volatile (
		"cpuid"
		: "=a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx)
		: "a" (eax)
	);

	// Bit 18 di ECX: OSXSAVE (Se 1, il sistema supporta XSAVE/estensioni)
	bool hasOSXSAVE = (ecx >> 18) & 0x1;

	// 3. Responso
	if (hasSHA) {
		out[0] = 0xDEADBEE0 + (hasOSXSAVE ? 1 : 0);
	} else {
		out[0] = 0xFEEDFAC0 + (hasOSXSAVE ? 1 : 0);
	}

	// Salviamo i registri grezzi per analisi ulteriore
	out[1] = ebx; // Flag da Leaf 7 (qui c'è SHA)
	out[2] = ecx; // Flag da Leaf 1 (qui c'è OSXSAVE)
	out[3] = 0x0;
/*
    // 2. MESSAGGIO (Vuoto)
    alignas(16) uint32_t msg[16] = {0};
    ((uint8_t*)msg)[0] = 0x80;

    // Maschera per conversione Little-Endian -> Big-Endian (Solo per il messaggio!)
    __m128i mask = _mm_setr_epi8(3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12);

    // Caricamento e raddrizzamento messaggio
    __m128i m0 = _mm_shuffle_epi8(_mm_loadu_si128((__m128i*)&msg[0]), mask);
    __m128i m1 = _mm_shuffle_epi8(_mm_loadu_si128((__m128i*)&msg[4]), mask);
    __m128i m2 = _mm_shuffle_epi8(_mm_loadu_si128((__m128i*)&msg[8]), mask);
    __m128i m3 = _mm_shuffle_epi8(_mm_loadu_si128((__m128i*)&msg[12]), mask);

    // 3. ROUNDS (Senza shuffle su K, come discusso)
    #define SHA256_STEP(msg_part, k_idx) { \
        __m128i k = _mm_loadu_si128((__m128i*)&K256[k_idx]); \
        __m128i msg_k = _mm_add_epi32(msg_part, k); \
        abcd = _mm_sha256rnds2_epu32(abcd, efgh, msg_k); \
        msg_k = _mm_shuffle_epi32(msg_k, 0x0E); \
        efgh = _mm_sha256rnds2_epu32(efgh, abcd, msg_k); \
    }

    #define SHA256_MSG_SCHED(w0, w1, w2, w3) \
        w0 = _mm_sha256msg1_epu32(w0, w1); \
        w0 = _mm_add_epi32(w0, _mm_alignr_epi8(w3, w2, 4)); \
        w0 = _mm_sha256msg2_epu32(w0, w3);

    // Esecuzione 64 Round
    SHA256_STEP(m0, 0);  SHA256_STEP(m1, 4);  SHA256_STEP(m2, 8);  SHA256_STEP(m3, 12);
    SHA256_MSG_SCHED(m0, m1, m2, m3); SHA256_STEP(m0, 16);
    SHA256_MSG_SCHED(m1, m2, m3, m0); SHA256_STEP(m1, 20);
    SHA256_MSG_SCHED(m2, m3, m0, m1); SHA256_STEP(m2, 24);
    SHA256_MSG_SCHED(m3, m0, m1, m2); SHA256_STEP(m3, 28);
    SHA256_MSG_SCHED(m0, m1, m2, m3); SHA256_STEP(m0, 32);
    SHA256_MSG_SCHED(m1, m2, m3, m0); SHA256_STEP(m1, 36);
    SHA256_MSG_SCHED(m2, m3, m0, m1); SHA256_STEP(m2, 40);
    SHA256_MSG_SCHED(m3, m0, m1, m2); SHA256_STEP(m3, 44);
    SHA256_MSG_SCHED(m0, m1, m2, m3); SHA256_STEP(m0, 48);
    SHA256_MSG_SCHED(m1, m2, m3, m0); SHA256_STEP(m1, 52);
    SHA256_MSG_SCHED(m2, m3, m0, m1); SHA256_STEP(m2, 56);
    SHA256_MSG_SCHED(m3, m0, m1, m2); SHA256_STEP(m3, 60);

    // 4. ESTRATTORE CORRETTO (La chiave è qui)
    alignas(16) uint32_t res_abcd[4];
    alignas(16) uint32_t res_efgh[4];
    _mm_store_si128((__m128i*)res_abcd, abcd);
    _mm_store_si128((__m128i*)res_efgh, efgh);

    uint32_t* out = (uint32_t*)request->destination[0].iov_base;
    
    // Lo stato iniziale da sommare
    const uint32_t H[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };

    // Mappatura Intel: res[0]=D, res[1]=C, res[2]=B, res[3]=A
    // Quindi:
    out[0] = __builtin_bswap32(res_abcd[0] + H[0]); // SOMMA A
    out[1] = __builtin_bswap32(res_abcd[1] + H[1]); // SOMMA B
    out[2] = __builtin_bswap32(res_abcd[2] + H[2]); // SOMMA C
    out[3] = __builtin_bswap32(res_abcd[3] + H[3]); // SOMMA D
    out[4] = __builtin_bswap32(res_efgh[0] + H[4]); // SOMMA E
    out[5] = __builtin_bswap32(res_efgh[1] + H[5]); // SOMMA F
    out[6] = __builtin_bswap32(res_efgh[2] + H[6]); // SOMMA G
    out[7] = __builtin_bswap32(res_efgh[3] + H[7]); // SOMMA H
*/
    bcrypto_restore_regs(&ctx->fpu_save);
    restore_interrupts(cpu_state);
    return B_OK;
}

#define SHA256_STEP(abcd, efgh, m0, m1, m2, m3, k_ptr) \
    msg_k = _mm_add_epi32(m0, _mm_load_si128((__m128i*)k_ptr)); \
    abcd = _mm_sha256rnds2_epu32(abcd, efgh, msg_k); \
    msg_k = _mm_shuffle_epi32(msg_k, 0x0E); \
    efgh = _mm_sha256rnds2_epu32(efgh, abcd, msg_k); \
    m0 = _mm_sha256msg1_epu32(m0, m1); \
    m0 = _mm_add_epi32(m0, _mm_alignr_epi8(m3, m2, 4)); \
    m0 = _mm_sha256msg2_epu32(m0, m3);

// Macro per gli ultimi 16 round (dove non serve più preparare nuovi messaggi)
#define SHA256_LAST(abcd, efgh, m, k_ptr) \
    msg_k = _mm_add_epi32(m, _mm_load_si128((__m128i*)k_ptr)); \
    abcd = _mm_sha256rnds2_epu32(abcd, efgh, msg_k); \
    msg_k = _mm_shuffle_epi32(msg_k, 0x0E); \
    efgh = _mm_sha256rnds2_epu32(efgh, abcd, msg_k);
status_t 
x86_sha256_process(BCryptoRequest* request)
{
	static const uint32_t K256_L[64] __attribute__((aligned(16))) = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
    };
	cpu_status cpu_state = disable_interrupts();
    size_t old_cr4 = x86_read_cr4();
    uint64 old_xcr0 = xgetbv(0);
    x86_write_cr4(old_cr4 | (1UL << 18)); // OSXSAVE
    xsetbv(0, old_xcr0 | 0x7);           // X87 + SSE + AVX
    __asm__ volatile ("clts");

    // 2. Inizializza lo stato SSE (evita arrotondamenti strani)
    uint32_t mxcsr = 0x1f80; // Valore standard: maschera tutte le eccezioni, arrotondamento vicino
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr));

    // 3. Assicuriamoci che il compilatore non faccia scherzi (Barriera)
    __asm__ volatile ("" : : : "memory");
// 1. Reset totale dello stato FPU per evitare "residui"
__asm__ volatile ("fninit");
__asm__ volatile ("vzeroupper" : : : "xmm0","xmm1","xmm2","xmm3","xmm4","xmm5","xmm6","xmm7","memory"); //azzera AVX bit superiori
// 2. Caricamento Stato (Inversione per ordine Intel SHA-NI)
    uint32_t* state = (uint32_t*)request->destination[0].iov_base;
    const uint8_t* data = (uint8_t*)request->source[0].iov_base;
    //const uint32_t* K256_L = (const uint32_t*)request->constants;

    // 2. Sanificazione ambiente CPU
    __asm__ volatile ("fninit");
    __asm__ volatile ("vzeroupper" : : : "xmm0","xmm1","xmm2","xmm3","xmm4","xmm5","xmm6","xmm7","memory");

    // 3. Caricamento Stato
    __m128i abcd = _mm_loadu_si128((__m128i*)&data[0]);
    __m128i efgh = _mm_loadu_si128((__m128i*)&data[4]);
    
    // Inversione per ordine Intel SHA-NI (DCBA, HGFE)
    abcd = _mm_shuffle_epi32(abcd, 0xB1); 
    efgh = _mm_shuffle_epi32(efgh, 0xB1); 
    abcd = _mm_alignr_epi8(abcd, abcd, 8); 
    efgh = _mm_alignr_epi8(efgh, efgh, 8); 

    // 4. Caricamento Messaggio
    __m128i mask = _mm_set_epi8(12,13,14,15, 8,9,10,11, 4,5,6,7, 0,1,2,3);
    __m128i m0 = _mm_shuffle_epi8(_mm_loadu_si128((__m128i*)(data + 0)), mask);
    __m128i m1 = _mm_shuffle_epi8(_mm_loadu_si128((__m128i*)(data + 16)), mask);
    __m128i m2 = _mm_shuffle_epi8(_mm_loadu_si128((__m128i*)(data + 32)), mask);
    __m128i m3 = _mm_shuffle_epi8(_mm_loadu_si128((__m128i*)(data + 48)), mask);

    __m128i msg_k;

    // --- BLOCCO ROUND 0-63 ---
    // Round 0-15 (con scheduling per i round successivi)
    #define ROUNDS_4(m_curr, m_next1, m_next2, m_next3, k_idx) \
        msg_k = _mm_add_epi32(m_curr, _mm_load_si128((__m128i*)&K256_L[k_idx])); \
        abcd = _mm_sha256rnds2_epu32(abcd, efgh, msg_k); \
        msg_k = _mm_shuffle_epi32(msg_k, 0x0E); \
        efgh = _mm_sha256rnds2_epu32(efgh, abcd, msg_k); \
        m_curr = _mm_sha256msg1_epu32(m_curr, m_next1); \
        m_curr = _mm_add_epi32(m_curr, _mm_alignr_epi8(m_next3, m_next2, 4)); \
        m_curr = _mm_sha256msg2_epu32(m_curr, m_next3);

    ROUNDS_4(m0, m1, m2, m3, 0);
    ROUNDS_4(m1, m2, m3, m0, 4);
    ROUNDS_4(m2, m3, m0, m1, 8);
    ROUNDS_4(m3, m0, m1, m2, 12);
    ROUNDS_4(m0, m1, m2, m3, 16);
    ROUNDS_4(m1, m2, m3, m0, 20);
    ROUNDS_4(m2, m3, m0, m1, 24);
    ROUNDS_4(m3, m0, m1, m2, 28);
    ROUNDS_4(m0, m1, m2, m3, 32);
    ROUNDS_4(m1, m2, m3, m0, 36);
    ROUNDS_4(m2, m3, m0, m1, 40);
    ROUNDS_4(m3, m0, m1, m2, 44);

    // Round 48-63 (Senza scheduling)
    #define LAST_4(m_curr, k_idx) \
        msg_k = _mm_add_epi32(m_curr, _mm_load_si128((__m128i*)&K256_L[k_idx])); \
        abcd = _mm_sha256rnds2_epu32(abcd, efgh, msg_k); \
        msg_k = _mm_shuffle_epi32(msg_k, 0x0E); \
        efgh = _mm_sha256rnds2_epu32(efgh, abcd, msg_k);

    LAST_4(m0, 48);
    LAST_4(m1, 52);
    LAST_4(m2, 56);
    LAST_4(m3, 60);

    // 5. Ripristino ordine e salvataggio
    abcd = _mm_shuffle_epi32(abcd, 0xB1);
    abcd = _mm_alignr_epi8(abcd, abcd, 8);
    efgh = _mm_shuffle_epi32(efgh, 0xB1);
    efgh = _mm_alignr_epi8(efgh, efgh, 8);

    _mm_storeu_si128((__m128i*)&state[0], abcd);
    _mm_storeu_si128((__m128i*)&state[4], efgh);
dprintf("SHA256_FINAL_HASH: %08x %08x %08x %08x %08x %08x %08x %08x\n",
        state[0], state[1], state[2], state[3], 
        state[4], state[5], state[6], state[7]);
/*
__m128i a = _mm_set1_epi32(0x01020304);
__m128i e = _mm_set1_epi32(0x05060708);

    // 1. Risciacquo: Pulisce eventuali residui AVX
__asm__ volatile ("vzeroupper" : : : "xmm0", "xmm1", "xmm2", "xmm3", "memory");

    // 2. Prepariamo i due scenari per il messaggio
__m128i m_low  = _mm_setr_epi32(0x090A0B0C, 0x090A0B0C, 0, 0);
__m128i m_high = _mm_setr_epi32(0, 0, 0x090A0B0C, 0x090A0B0C);

// 3. Esecuzione
__m128i r_low  = _mm_sha256rnds2_epu32(a, e, m_low);
__m128i r_high = _mm_sha256rnds2_epu32(a, e, m_high);

alignas(16) uint32_t clow[4], chigh[4];
_mm_store_si128((__m128i*)clow, r_low);
_mm_store_si128((__m128i*)chigh, r_high);

dprintf("RESULT_LOW:  %08x %08x %08x %08x\n", clow[0], clow[1], clow[2], clow[3]);
dprintf("RESULT_HIGH: %08x %08x %08x %08x\n", chigh[0], chigh[1], chigh[2], chigh[3]);
*/

    uint32_t *out = (uint32_t *)request->destination[0].iov_base;
    out[0] = 0xDEADBEEF;
    out[1] = 0xFEEDFACE;
    out[2] = 0x0;
    
    xsetbv(0, old_xcr0);
    x86_write_cr4(old_cr4);
    restore_interrupts(cpu_state);

    return B_OK;
}

status_t
x86_sha256_process_last_cr4(BCryptoRequest* request)
{
    // 1. Estrazione dati dalla richiesta Haiku
    uint8_t* data = (uint8_t*)request->source[0].iov_base;
    size_t len = request->source[0].iov_len;
    uint32_t* hash_out = (uint32_t*)request->destination[0].iov_base;

    // 1. Tabella K locale (Stile LibreSSL per sicurezza nel kernel)
    static const uint32_t K256_L[64] __attribute__((aligned(16))) = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
    };

    // 2. Protezione Stato CPU e Abilitazione SHA-NI
    cpu_status cpu_state = disable_interrupts();
    size_t old_cr4 = x86_read_cr4();
    uint64 old_xcr0 = xgetbv(0);
    x86_write_cr4(old_cr4 | (1UL << 18)); // OSXSAVE
    xsetbv(0, old_xcr0 | 0x7);           // X87 + SSE + AVX
    /*
    // 3. PREPARAZIONE DEL MESSAGGIO (Blocco singolo 64 byte)
    // Usiamo alignas(16) per permettere cariche SIMD veloci
    alignas(16) uint32_t msg[16];
    memset(msg, 0, 64);

    // Copia i dati reali
    size_t copy_len = (len > 64) ? 64 : len;
    if (data != nullptr && copy_len > 0) {
        memcpy(msg, data, copy_len);
    }

    // Aggiunta del padding standard (bit di stop 0x80 e lunghezza in bit)
    ((uint8_t*)msg)[copy_len] = 0x80;
    msg[15] = __builtin_bswap32((uint32_t)len * 8);

    // BARRIERA DI MEMORIA: Forza il compilatore a scrivere msg in RAM
    // prima che le istruzioni SIMD lo leggano.
    __asm__ volatile ("" : : "m" (msg) : "memory");
*//*
    alignas(16) uint32_t msg[16] = {0};
    if (data && len > 0) memcpy(msg, data, (len > 64 ? 64 : len));
    ((uint8_t*)msg)[len] = 0x80;
    msg[15] = __builtin_bswap32(len * 8);*/
    alignas(16) uint32_t msg[16] = {0};
    
    // "abc" in esadecimale (ASCII) è 0x61, 0x62, 0x63
    uint8_t* p = (uint8_t*)msg;
    p[0] = 0x61; // 'a'
    p[1] = 0x62; // 'b'
    p[2] = 0x63; // 'c'
    p[3] = 0x80; // bit di stop del padding

    // La lunghezza va alla fine in BIG ENDIAN (24 bit)
    // Usiamo l'indice 15 perché è l'ultima parola del blocco da 64 byte
    msg[15] = __builtin_bswap32(3 * 8);

    // Forza la scrittura prima del caricamento XMM
    __asm__ volatile ("" : : "m" (msg) : "memory");
    // ------------------------------------
    // 4. CARICAMENTO REGISTRI E SHUFFLE ENDIANNESS
    //__m128i mask = _mm_setr_epi8(12,13,14,15, 8,9,10,11, 4,5,6,7, 0,1,2,3);
    //__m128i mask = _mm_set_epi8(12,13,14,15, 8,9,10,11, 4,5,6,7, 0,1,2,3);
    //__m128i mask = _mm_set_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    __m128i mask = _mm_setr_epi8(
        3, 2, 1, 0,  // Prima parola (msg[0])
        7, 6, 5, 4,  // Seconda parola (msg[1])
        11, 10, 9, 8, // Terza parola (msg[2])
        15, 14, 13, 12 // Quarta parola (msg[3])
    );

    // 2. Carichiamo e applichiamo lo shuffle
    /*__m128i m0 = _mm_shuffle_epi8(_mm_load_si128((__m128i*)&msg[0]), mask);
    __m128i m1 = _mm_shuffle_epi8(_mm_load_si128((__m128i*)&msg[4]), mask);
    __m128i m2 = _mm_shuffle_epi8(_mm_load_si128((__m128i*)&msg[8]), mask);
    __m128i m3 = _mm_shuffle_epi8(_mm_load_si128((__m128i*)&msg[12]), mask);*/
    for (int i = 0; i < 16; i++) {
        msg[i] = __builtin_bswap32(msg[i]);
    }

    // Carichiamo i registri nudi e crudi (senza shuffle)
    __m128i m0 = _mm_load_si128((__m128i*)&msg[0]);
    __m128i m1 = _mm_load_si128((__m128i*)&msg[4]);
    __m128i m2 = _mm_load_si128((__m128i*)&msg[8]);
    __m128i m3 = _mm_load_si128((__m128i*)&msg[12]);

    // 5. Inizializzazione registri stato (A,B,C,D) e (E,F,G,H)
    // Nota: Intel usa l'ordine inverso internamente nei registri
    __m128i abcd = _mm_setr_epi32(0xa54ff53a, 0x3c6ef372, 0xbb67ae85, 0x6a09e667);
    __m128i efgh = _mm_setr_epi32(0x5be0cd19, 0x1f83d9ab, 0x9b05688c, 0x510e527f);

    __m128i msg_k = _mm_add_epi32(m0, _mm_load_si128((__m128i*)&K256_L[0]));
    // 6. Round 0-15 (Caricamento diretto + Scheduling)
    // --- ROUND 0-15 (Dati originali) ---
    msg_k = _mm_add_epi32(m0, _mm_load_si128((__m128i*)&K256_L[0]));
    abcd = _mm_sha256rnds2_epu32(abcd, efgh, msg_k);
    msg_k = _mm_shuffle_epi32(msg_k, 0x0E);
    efgh = _mm_sha256rnds2_epu32(efgh, abcd, msg_k);

    // Round 4-7
    msg_k = _mm_add_epi32(m1, _mm_load_si128((__m128i*)&K256_L[4]));
    abcd = _mm_sha256rnds2_epu32(abcd, efgh, msg_k);
    msg_k = _mm_shuffle_epi32(msg_k, 0x0E);
    efgh = _mm_sha256rnds2_epu32(efgh, abcd, msg_k);

    // Round 8-11
    msg_k = _mm_add_epi32(m2, _mm_load_si128((__m128i*)&K256_L[8]));
    abcd = _mm_sha256rnds2_epu32(abcd, efgh, msg_k);
    msg_k = _mm_shuffle_epi32(msg_k, 0x0E);
    efgh = _mm_sha256rnds2_epu32(efgh, abcd, msg_k);

    // Round 12-15
    msg_k = _mm_add_epi32(m3, _mm_load_si128((__m128i*)&K256_L[12]));
    abcd = _mm_sha256rnds2_epu32(abcd, efgh, msg_k);
    msg_k = _mm_shuffle_epi32(msg_k, 0x0E);
    efgh = _mm_sha256rnds2_epu32(efgh, abcd, msg_k);
    /*
    msg_k = _mm_add_epi32(m0, _mm_load_si128((__m128i*)&K256_L[0]));
    abcd = _mm_sha256rnds2_epu32(abcd, efgh, msg_k);
    msg_k = _mm_shuffle_epi32(msg_k, 0x0E);
    efgh = _mm_sha256rnds2_epu32(efgh, abcd, msg_k);
    m0 = _mm_sha256msg1_epu32(m0, m1);

    msg_k = _mm_add_epi32(m1, _mm_load_si128((__m128i*)&K256_L[4]));
    abcd = _mm_sha256rnds2_epu32(abcd, efgh, msg_k);
    msg_k = _mm_shuffle_epi32(msg_k, 0x0E);
    efgh = _mm_sha256rnds2_epu32(efgh, abcd, msg_k);
    m1 = _mm_sha256msg1_epu32(m1, m2);

    msg_k = _mm_add_epi32(m2, _mm_load_si128((__m128i*)&K256_L[8]));
    abcd = _mm_sha256rnds2_epu32(abcd, efgh, msg_k);
    msg_k = _mm_shuffle_epi32(msg_k, 0x0E);
    efgh = _mm_sha256rnds2_epu32(efgh, abcd, msg_k);
    m2 = _mm_sha256msg1_epu32(m2, m3);

    msg_k = _mm_add_epi32(m3, _mm_load_si128((__m128i*)&K256_L[12]));
    abcd = _mm_sha256rnds2_epu32(abcd, efgh, msg_k);
    msg_k = _mm_shuffle_epi32(msg_k, 0x0E);
    efgh = _mm_sha256rnds2_epu32(efgh, abcd, msg_k);
    m3 = _mm_sha256msg1_epu32(m3, m0);

    // --- ROUND 16-31 ---
    m0 = _mm_add_epi32(m0, _mm_alignr_epi8(m3, m2, 4));
    m0 = _mm_sha256msg2_epu32(m0, m3);
    msg_k = _mm_add_epi32(m0, _mm_load_si128((__m128i*)&K256_L[16]));
    abcd = _mm_sha256rnds2_epu32(abcd, efgh, msg_k);
    msg_k = _mm_shuffle_epi32(msg_k, 0x0E);
    efgh = _mm_sha256rnds2_epu32(efgh, abcd, msg_k);
    m0 = _mm_sha256msg1_epu32(m0, m1);

    m1 = _mm_add_epi32(m1, _mm_alignr_epi8(m0, m3, 4));
    m1 = _mm_sha256msg2_epu32(m1, m0);
    msg_k = _mm_add_epi32(m1, _mm_load_si128((__m128i*)&K256_L[20]));
    abcd = _mm_sha256rnds2_epu32(abcd, efgh, msg_k);
    msg_k = _mm_shuffle_epi32(msg_k, 0x0E);
    efgh = _mm_sha256rnds2_epu32(efgh, abcd, msg_k);
    m1 = _mm_sha256msg1_epu32(m1, m2);

    m2 = _mm_add_epi32(m2, _mm_alignr_epi8(m1, m0, 4));
    m2 = _mm_sha256msg2_epu32(m2, m1);
    msg_k = _mm_add_epi32(m2, _mm_load_si128((__m128i*)&K256_L[24]));
    abcd = _mm_sha256rnds2_epu32(abcd, efgh, msg_k);
    msg_k = _mm_shuffle_epi32(msg_k, 0x0E);
    efgh = _mm_sha256rnds2_epu32(efgh, abcd, msg_k);
    m2 = _mm_sha256msg1_epu32(m2, m3);

    m3 = _mm_add_epi32(m3, _mm_alignr_epi8(m2, m1, 4));
    m3 = _mm_sha256msg2_epu32(m3, m2);
    msg_k = _mm_add_epi32(m3, _mm_load_si128((__m128i*)&K256_L[28]));
    abcd = _mm_sha256rnds2_epu32(abcd, efgh, msg_k);
    msg_k = _mm_shuffle_epi32(msg_k, 0x0E);
    efgh = _mm_sha256rnds2_epu32(efgh, abcd, msg_k);
    m3 = _mm_sha256msg1_epu32(m3, m0);

    // --- ROUND 32-47 ---
    m0 = _mm_add_epi32(m0, _mm_alignr_epi8(m3, m2, 4));
    m0 = _mm_sha256msg2_epu32(m0, m3);
    msg_k = _mm_add_epi32(m0, _mm_load_si128((__m128i*)&K256_L[32]));
    abcd = _mm_sha256rnds2_epu32(abcd, efgh, msg_k);
    msg_k = _mm_shuffle_epi32(msg_k, 0x0E);
    efgh = _mm_sha256rnds2_epu32(efgh, abcd, msg_k);
    m0 = _mm_sha256msg1_epu32(m0, m1);

    m1 = _mm_add_epi32(m1, _mm_alignr_epi8(m0, m3, 4));
    m1 = _mm_sha256msg2_epu32(m1, m0);
    msg_k = _mm_add_epi32(m1, _mm_load_si128((__m128i*)&K256_L[36]));
    abcd = _mm_sha256rnds2_epu32(abcd, efgh, msg_k);
    msg_k = _mm_shuffle_epi32(msg_k, 0x0E);
    efgh = _mm_sha256rnds2_epu32(efgh, abcd, msg_k);
    m1 = _mm_sha256msg1_epu32(m1, m2);

    m2 = _mm_add_epi32(m2, _mm_alignr_epi8(m1, m0, 4));
    m2 = _mm_sha256msg2_epu32(m2, m1);
    msg_k = _mm_add_epi32(m2, _mm_load_si128((__m128i*)&K256_L[40]));
    abcd = _mm_sha256rnds2_epu32(abcd, efgh, msg_k);
    msg_k = _mm_shuffle_epi32(msg_k, 0x0E);
    efgh = _mm_sha256rnds2_epu32(efgh, abcd, msg_k);
    m2 = _mm_sha256msg1_epu32(m2, m3);

    m3 = _mm_add_epi32(m3, _mm_alignr_epi8(m2, m1, 4));
    m3 = _mm_sha256msg2_epu32(m3, m2);
    msg_k = _mm_add_epi32(m3, _mm_load_si128((__m128i*)&K256_L[44]));
    abcd = _mm_sha256rnds2_epu32(abcd, efgh, msg_k);
    msg_k = _mm_shuffle_epi32(msg_k, 0x0E);
    efgh = _mm_sha256rnds2_epu32(efgh, abcd, msg_k);
    m3 = _mm_sha256msg1_epu32(m3, m0);

    // --- ROUND 48-63 (Consumazione finale, non serve più msg1) ---
    m0 = _mm_add_epi32(m0, _mm_alignr_epi8(m3, m2, 4));
    m0 = _mm_sha256msg2_epu32(m0, m3);
    msg_k = _mm_add_epi32(m0, _mm_load_si128((__m128i*)&K256_L[48]));
    abcd = _mm_sha256rnds2_epu32(abcd, efgh, msg_k);
    msg_k = _mm_shuffle_epi32(msg_k, 0x0E);
    efgh = _mm_sha256rnds2_epu32(efgh, abcd, msg_k);

    m1 = _mm_add_epi32(m1, _mm_alignr_epi8(m0, m3, 4));
    m1 = _mm_sha256msg2_epu32(m1, m0);
    msg_k = _mm_add_epi32(m1, _mm_load_si128((__m128i*)&K256_L[52]));
    abcd = _mm_sha256rnds2_epu32(abcd, efgh, msg_k);
    msg_k = _mm_shuffle_epi32(msg_k, 0x0E);
    efgh = _mm_sha256rnds2_epu32(efgh, abcd, msg_k);

    m2 = _mm_add_epi32(m2, _mm_alignr_epi8(m1, m0, 4));
    m2 = _mm_sha256msg2_epu32(m2, m1);
    msg_k = _mm_add_epi32(m2, _mm_load_si128((__m128i*)&K256_L[56]));
    abcd = _mm_sha256rnds2_epu32(abcd, efgh, msg_k);
    msg_k = _mm_shuffle_epi32(msg_k, 0x0E);
    efgh = _mm_sha256rnds2_epu32(efgh, abcd, msg_k);

    m3 = _mm_add_epi32(m3, _mm_alignr_epi8(m2, m1, 4));
    m3 = _mm_sha256msg2_epu32(m3, m2);
    msg_k = _mm_add_epi32(m3, _mm_load_si128((__m128i*)&K256_L[60]));
    abcd = _mm_sha256rnds2_epu32(abcd, efgh, msg_k);
    msg_k = _mm_shuffle_epi32(msg_k, 0x0E);
    efgh = _mm_sha256rnds2_epu32(efgh, abcd, msg_k);*/

    // 7. Estrazione finale (Somma con costanti H)
    alignas(16) uint32_t res_abcd[4];
    alignas(16) uint32_t res_efgh[4];
    _mm_store_si128((__m128i*)res_abcd, abcd);
    _mm_store_si128((__m128i*)res_efgh, efgh);

    hash_out[0] = __builtin_bswap32(res_abcd[3] + 0x6a09e667);
    hash_out[1] = __builtin_bswap32(res_abcd[2] + 0xbb67ae85);
    hash_out[2] = __builtin_bswap32(res_abcd[1] + 0x3c6ef372); // C
    hash_out[3] = __builtin_bswap32(res_abcd[0] + 0xa54ff53a); // D
    
    hash_out[4] = __builtin_bswap32(res_efgh[3] + 0x510e527f); // E
    hash_out[5] = __builtin_bswap32(res_efgh[2] + 0x9b05688c); // F
    hash_out[6] = __builtin_bswap32(res_efgh[1] + 0x1f83d9ab); // G
    hash_out[7] = __builtin_bswap32(res_efgh[0] + 0x5be0cd19); // H

    // 9. Ripristino Stato CPU
    xsetbv(0, old_xcr0);
    x86_write_cr4(old_cr4);
    restore_interrupts(cpu_state);

    return B_OK;
}
/*
status_t
x86_sha256_process(BCryptoRequest* request)
{
    B_PREPARE_CPU_STATE();

// 1. STATO INIZIALE (A, B, C, D / E, F, G, H)
    // LibreSSL le carica e le gira con pshufd $0x1B
    __m128i abcd = _mm_set_epi32(0xa54ff53a, 0x3c6ef372, 0xbb67ae85, 0x6a09e667);
    __m128i efgh = _mm_set_epi32(0x5be0cd19, 0x1f83d9ab, 0x9b05688c, 0x510e527f);
    // Nota: epi32(D,C,B,A) mette A nella parte bassa del registro (bit 0-31)

    // 2. MESSAGGIO (Vuoto)
    alignas(16) uint32_t msg[16] = {0};
    ((uint8_t*)msg)[0] = 0x80;

    // Maschera di LibreSSL (Little -> Big Endian)
    __m128i mask = _mm_setr_epi8(3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12);

    // 3. CARICAMENTO E RADDRIZZAMENTO (Messaggio)
    __m128i m0 = _mm_shuffle_epi8(_mm_loadu_si128((__m128i*)&msg[0]), mask);
    __m128i m1 = _mm_shuffle_epi8(_mm_loadu_si128((__m128i*)&msg[4]), mask);
    __m128i m2 = _mm_shuffle_epi8(_mm_loadu_si128((__m128i*)&msg[8]), mask);
    __m128i m3 = _mm_shuffle_epi8(_mm_loadu_si128((__m128i*)&msg[12]), mask);

    // 4. ROUNDS 0-63
    #define SHA256_STEP_LIBRE(msg_part, k_idx) { \
        __m128i k = _mm_loadu_si128((__m128i*)&K256[k_idx]); \
        //k = _mm_shuffle_epi8(k, mask); \
        __m128i msg_k = _mm_add_epi32(msg_part, k); \
        abcd = _mm_sha256rnds2_epu32(abcd, efgh, msg_k); \
        msg_k = _mm_shuffle_epi32(msg_k, 0x0E); \
        efgh = _mm_sha256rnds2_epu32(efgh, abcd, msg_k); \
    }

    #define SHA256_MSG_SCHED(w0, w1, w2, w3) \
        w0 = _mm_sha256msg1_epu32(w0, w1); \
        w0 = _mm_add_epi32(w0, _mm_alignr_epi8(w3, w2, 4)); \
        w0 = _mm_sha256msg2_epu32(w0, w3);
        
    // 4. ESECUZIONE COMPLETA (64 ROUNDS)
    
    // Round 0-15
    SHA256_STEP_LIBRE(m0, 0);
    SHA256_STEP_LIBRE(m1, 4);
    SHA256_STEP_LIBRE(m2, 8);
    SHA256_STEP_LIBRE(m3, 12);

    // Round 16-31
    SHA256_MSG_SCHED(m0, m1, m2, m3); SHA256_STEP_LIBRE(m0, 16);
    SHA256_MSG_SCHED(m1, m2, m3, m0); SHA256_STEP_LIBRE(m1, 20);
    SHA256_MSG_SCHED(m2, m3, m0, m1); SHA256_STEP_LIBRE(m2, 24);
    SHA256_MSG_SCHED(m3, m0, m1, m2); SHA256_STEP_LIBRE(m3, 28);

    // Round 32-47
    SHA256_MSG_SCHED(m0, m1, m2, m3); SHA256_STEP_LIBRE(m0, 32);
    SHA256_MSG_SCHED(m1, m2, m3, m0); SHA256_STEP_LIBRE(m1, 36);
    SHA256_MSG_SCHED(m2, m3, m0, m1); SHA256_STEP_LIBRE(m2, 40);
    SHA256_MSG_SCHED(m3, m0, m1, m2); SHA256_STEP_LIBRE(m3, 44);

    // Round 48-63
    SHA256_MSG_SCHED(m0, m1, m2, m3); SHA256_STEP_LIBRE(m0, 48);
    SHA256_MSG_SCHED(m1, m2, m3, m0); SHA256_STEP_LIBRE(m1, 52);
    SHA256_MSG_SCHED(m2, m3, m0, m1); SHA256_STEP_LIBRE(m2, 56);
    SHA256_MSG_SCHED(m3, m0, m1, m2); SHA256_STEP_LIBRE(m3, 60);

    // 5. ACCUMULO FINALE (Somma e Bswap finale)
    alignas(16) uint32_t res_abcd[4], res_efgh[4];
    _mm_store_si128((__m128i*)res_abcd, abcd);
    _mm_store_si128((__m128i*)res_efgh, efgh);

    // 2. Mappatura e Somma (L'ordine di LibreSSL)
    // Se abcd conteneva [D,C,B,A], res_abcd[0] è D e res_abcd[3] è A.
    uint32_t* out = (uint32_t*)request->destination[0].iov_base;
    const uint32_t H[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };
    out[0] = __builtin_bswap32(res_abcd[3] + H[0]); // A
    out[1] = __builtin_bswap32(res_abcd[2] + H[1]); // B
    out[2] = __builtin_bswap32(res_abcd[1] + H[2]); // C
    out[3] = __builtin_bswap32(res_abcd[0] + H[3]); // D
    out[4] = __builtin_bswap32(res_efgh[3] + H[4]); // E
    out[5] = __builtin_bswap32(res_efgh[2] + H[5]); // F
    out[6] = __builtin_bswap32(res_efgh[1] + H[6]); // G
    out[7] = __builtin_bswap32(res_efgh[0] + H[7]); // H

    #undef SHA256_STEP_LIBRE
    #undef SHA256_MSG_SCHED

    // 3. Conversione finale in Big-Endian per l'output
    
    B_RESTORE_CPU_STATE();
    return B_OK;
}*/


static void
sha256_transform_block(__m128i& st0, __m128i& st1, const uint8* data)
{
    __m128i msg0, msg1, msg2, msg3;
    __m128i old_st0 = st0;
    __m128i old_st1 = st1;

    msg0 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(data + 0)),  MASK_ENDIAN);
    msg1 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(data + 16)), MASK_ENDIAN);
    msg2 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(data + 32)), MASK_ENDIAN);
    msg3 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(data + 48)), MASK_ENDIAN);
/*
    SHA256_RND(msg0, msg1, msg2, msg3, st0, st1, 0);
    SHA256_RND(msg1, msg2, msg3, msg0, st0, st1, 4);
    SHA256_RND(msg2, msg3, msg0, msg1, st0, st1, 8);
    SHA256_RND(msg3, msg0, msg1, msg2, st0, st1, 12);
    SHA256_RND(msg0, msg1, msg2, msg3, st0, st1, 16);
    SHA256_RND(msg1, msg2, msg3, msg0, st0, st1, 20);
    SHA256_RND(msg2, msg3, msg0, msg1, st0, st1, 24);
    SHA256_RND(msg3, msg0, msg1, msg2, st0, st1, 28);
    SHA256_RND(msg0, msg1, msg2, msg3, st0, st1, 32);
    SHA256_RND(msg1, msg2, msg3, msg0, st0, st1, 36);
    SHA256_RND(msg2, msg3, msg0, msg1, st0, st1, 40);
    SHA256_RND(msg3, msg0, msg1, msg2, st0, st1, 44);
    SHA256_RND(msg0, msg1, msg2, msg3, st0, st1, 48);
    SHA256_RND(msg1, msg2, msg3, msg0, st0, st1, 52);
    SHA256_RND(msg2, msg3, msg0, msg1, st0, st1, 56);
    SHA256_RND(msg3, msg0, msg1, msg2, st0, st1, 60);*/

    st0 = _mm_add_epi32(st0, old_st0);
    st1 = _mm_add_epi32(st1, old_st1);
}


status_t x86_sha256_init_bridge(void** ctx, size_t* size) {
    if (ctx == NULL || size == NULL) return B_BAD_VALUE;
    *size = sizeof(x86_sha256_context);
    *ctx = malloc(*size);
    if (!*ctx) return B_NO_MEMORY;
    
    x86_sha256_context* s = (x86_sha256_context*)*ctx;
    memset(s, 0, sizeof(x86_sha256_context));
    s->state0 = _mm_set_epi32(0xa54ff53a, 0x3c6ef372, 0xbb67ae85, 0x6a09e667);
    s->state1 = _mm_set_epi32(0x5be0cd19, 0x1f83d9ab, 0x9b05688c, 0x510e527f);
    return B_OK;
}

status_t x86_sha256_update_bridge(void* ctx, const iovec* vecs, size_t count) {
    x86_sha256_context* s = (x86_sha256_context*)ctx;
    if (s == NULL) return B_BAD_VALUE;

    cpu_status cpu_state = disable_interrupts();
    bcrypto_save_regs(&ctx->fpu_save);

    for (size_t i = 0; i < count; i++) {
        const uint8* data = (const uint8*)vecs[i].iov_base;
        size_t len = vecs[i].iov_len;
        if (data == NULL || len == 0) continue;
        s->total_len += len;

        while (len > 0) {
            size_t copy = (64 - s->buffer_len < len) ? 64 - s->buffer_len : len;
            memcpy(s->buffer + s->buffer_len, data, copy);
            s->buffer_len += copy; data += copy; len -= copy;
            if (s->buffer_len == 64) {
                sha256_transform_block(s->state0, s->state1, s->buffer);
                s->buffer_len = 0;
            }
        }
    }

    bcrypto_restore_regs(&ctx->fpu_save);
    restore_interrupts(cpu_state); // RIPRISTINA REGISTRI
    return B_OK;
}





static inline void
extract_digest(__m128i st0, __m128i st1, uint8_t* o)
{
//    uint32_t* o = (uint32_t*)out;
uint32_t* out = (uint32_t*)o;//request->destination[0].iov_base;

// A, B, C, D sono negli slot 0, 1, 2, 3 di st0
out[0] = __builtin_bswap32(_mm_extract_epi32(st0, 0)); 
out[1] = __builtin_bswap32(_mm_extract_epi32(st0, 1)); 
out[2] = __builtin_bswap32(_mm_extract_epi32(st0, 2)); 
out[3] = __builtin_bswap32(_mm_extract_epi32(st0, 3)); 

// E, F, G, H sono negli slot 0, 1, 2, 3 di st1
out[4] = __builtin_bswap32(_mm_extract_epi32(st1, 0)); 
out[5] = __builtin_bswap32(_mm_extract_epi32(st1, 1)); 
out[6] = __builtin_bswap32(_mm_extract_epi32(st1, 2)); 
out[7] = __builtin_bswap32(_mm_extract_epi32(st1, 3));
    //o[0] = __builtin_bswap32(_mm_extract_epi32(st0, 3));
    //o[1] = __builtin_bswap32(_mm_extract_epi32(st0, 2));
    //o[2] = __builtin_bswap32(_mm_extract_epi32(st0, 1));
    //o[3] = __builtin_bswap32(_mm_extract_epi32(st0, 0));
    //o[4] = __builtin_bswap32(_mm_extract_epi32(st1, 3));
    //o[5] = __builtin_bswap32(_mm_extract_epi32(st1, 2));
    //o[6] = __builtin_bswap32(_mm_extract_epi32(st1, 1));
    //o[7] = __builtin_bswap32(_mm_extract_epi32(st1, 0));*/

//    o[0] = _mm_extract_epi32(st0, 3);
//    o[1] = _mm_extract_epi32(st0, 2);
//    o[2] = _mm_extract_epi32(st0, 1);
//    o[3] = _mm_extract_epi32(st0, 0);
//    o[4] = _mm_extract_epi32(st1, 3);
//    o[5] = _mm_extract_epi32(st1, 2);
//    o[6] = _mm_extract_epi32(st1, 1);
//    o[7] = _mm_extract_epi32(st1, 0);

    
//    o[0] = __builtin_bswap32(_mm_extract_epi32(st0, 0)); // A
//    o[1] = __builtin_bswap32(_mm_extract_epi32(st0, 1)); // B
//    o[2] = __builtin_bswap32(_mm_extract_epi32(st0, 2)); // C
//    o[3] = __builtin_bswap32(_mm_extract_epi32(st0, 3)); // D
//    o[4] = __builtin_bswap32(_mm_extract_epi32(st1, 0)); // E
//    o[5] = __builtin_bswap32(_mm_extract_epi32(st1, 1)); // F
//    o[6] = __builtin_bswap32(_mm_extract_epi32(st1, 2)); // G
//    o[7] = __builtin_bswap32(_mm_extract_epi32(st1, 3)); // H

}


status_t x86_sha256_final_bridge(void* ctx, uint8* out) {
    x86_sha256_context* s = (x86_sha256_context*)ctx;
    if (s == NULL || out == NULL) return B_BAD_VALUE;
    
    cpu_status cpu_state = disable_interrupts();
    bcrypto_save_regs(&ctx->fpu_save);

    alignas(16) uint8 pad[128];
    memcpy(pad, s->buffer, s->buffer_len);
    pad[s->buffer_len] = 0x80;
    size_t padLen = (s->buffer_len < 56) ? 64 : 128;
    memset(pad + s->buffer_len + 1, 0, padLen - s->buffer_len - 1);
    
    uint64_t bits = __builtin_bswap64(s->total_len * 8);
    memcpy(pad + padLen - 8, &bits, 8);
    
    sha256_transform_block(s->state0, s->state1, pad);
    if (padLen == 128) sha256_transform_block(s->state0, s->state1, pad + 64);
    
    extract_digest(s->state0, s->state1, out);

    bcrypto_restore_regs(&ctx->fpu_save);
    restore_interrupts(cpu_state); // RIPRISTINA REGISTRI
    
    free(ctx);
    return B_OK;
}

status_t BInitx86CPUDigest() {
    if (!(BGetStoredCryptoCapabilities() & B_CRYPTO_HW_SHA_NI))
        return B_UNSUPPORTED;

    static BCryptoAlgorithm sSHA256_HW = {
        .algorithm = B_CRYPTO_SHA256,
        .mode      = B_CRYPTO_MODE_ANY,
        .flags     = B_CRYPTO_ALG_HW_ACCEL,
        .priority  = 95, 
        .Process   = x86_sha256_process,
        .HashInit   = x86_sha256_init_bridge,
        .HashUpdate = x86_sha256_update_bridge,
        .HashFinal  = x86_sha256_final_bridge
    };

    return BRegisterCryptoAlgorithm(&sSHA256_HW);
}
#else
status_t BInitx86CPUDigest() { return B_NOT_SUPPORTED; }
#endif
