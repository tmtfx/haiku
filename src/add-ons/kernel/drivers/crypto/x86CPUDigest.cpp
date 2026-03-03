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
#include <malloc.h>
static void debug_xmm(const char* label, __m128i reg) {
    uint32_t val[4];
    _mm_storeu_si128((__m128i*)val, reg);
    //printf("DEBUG %s: [%08x %08x %08x %08x]\n", label, val[0], val[1], val[2], val[3]);
    dprintf("SHA-NI DEBUG %s: [%08x %08x %08x %08x]\n", 
            label, val[0], val[1], val[2], val[3]);
}

#if defined(__x86_64__) || defined(__i386__)

#pragma GCC target("sha,sse4.1,ssse3")

// Maschera per invertire l'endianness dei 4 dword in un registro XMM
//static const __m128i MASK_ENDIAN = _mm_set_epi8(
//    12, 13, 14, 15, 8, 9, 10, 11, 4, 5, 6, 7, 0, 1, 2, 3
//);
// Definiamo la maschera come array di costanti immediate
/*alignas(16) const uint8_t mask_bytes[16] = {
    3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12
};
__m128i MASK_ENDIAN = _mm_load_si128((const __m128i*)mask_bytes);*/

static const uint32 K256[64] alignas(16) = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};
static void
_sha256_transform_core(__m128i& st0, __m128i& st1, const uint8* data)
{
	__m128i MASK_ENDIAN = _mm_set_epi8(
        12, 13, 14, 15, 8, 9, 10, 11, 4, 5, 6, 7, 0, 1, 2, 3
    );
    __m128i msg0, msg1, msg2, msg3, tmp;
    __m128i old_st0 = st0;
    __m128i old_st1 = st1;

    // 1. Caricamento e inversione Endian
    msg0 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(data + 0)),  MASK_ENDIAN);
    msg1 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(data + 16)), MASK_ENDIAN);
    msg2 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(data + 32)), MASK_ENDIAN);
    msg3 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(data + 48)), MASK_ENDIAN);

    // Round 0-3
    tmp = _mm_add_epi32(msg0, _mm_loadu_si128((const __m128i*)&K256[0]));
    st1 = _mm_sha256rnds2_epu32(st1, st0, tmp);
    st0 = _mm_sha256rnds2_epu32(st0, st1, _mm_alignr_epi8(tmp, tmp, 8));
    msg0 = _mm_sha256msg1_epu32(msg0, msg1);

    // Round 4-7
    tmp = _mm_add_epi32(msg1, _mm_loadu_si128((const __m128i*)&K256[4]));
    st1 = _mm_sha256rnds2_epu32(st1, st0, tmp);
    st0 = _mm_sha256rnds2_epu32(st0, st1, _mm_alignr_epi8(tmp, tmp, 8));
    msg1 = _mm_sha256msg1_epu32(msg1, msg2);

    // Round 8-11
    tmp = _mm_add_epi32(msg2, _mm_loadu_si128((const __m128i*)&K256[8]));
    st1 = _mm_sha256rnds2_epu32(st1, st0, tmp);
    st0 = _mm_sha256rnds2_epu32(st0, st1, _mm_alignr_epi8(tmp, tmp, 8));
    msg2 = _mm_sha256msg1_epu32(msg2, msg3);

    // Round 12-15
    tmp = _mm_add_epi32(msg3, _mm_loadu_si128((const __m128i*)&K256[12]));
    st1 = _mm_sha256rnds2_epu32(st1, st0, tmp);
    st0 = _mm_sha256rnds2_epu32(st0, st1, _mm_alignr_epi8(tmp, tmp, 8));
    msg3 = _mm_sha256msg1_epu32(msg3, msg0);

    // Da qui iniziamo a usare sha256msg2 per completare lo scheduling
    for (int j = 16; j < 64; j += 16) {
        // Round 0-3 del blocco da 16
        msg0 = _mm_add_epi32(msg0, _mm_sha256msg2_epu32(_mm_alignr_epi8(msg3, msg2, 4), msg3));
        tmp = _mm_add_epi32(msg0, _mm_loadu_si128((const __m128i*)&K256[j]));
        st1 = _mm_sha256rnds2_epu32(st1, st0, tmp);
        st0 = _mm_sha256rnds2_epu32(st0, st1, _mm_alignr_epi8(tmp, tmp, 8));
        msg0 = _mm_sha256msg1_epu32(msg0, msg1);

        // Round 4-7 del blocco da 16
        msg1 = _mm_add_epi32(msg1, _mm_sha256msg2_epu32(_mm_alignr_epi8(msg0, msg3, 4), msg0));
        tmp = _mm_add_epi32(msg1, _mm_loadu_si128((const __m128i*)&K256[j+4]));
        st1 = _mm_sha256rnds2_epu32(st1, st0, tmp);
        st0 = _mm_sha256rnds2_epu32(st0, st1, _mm_alignr_epi8(tmp, tmp, 8));
        msg1 = _mm_sha256msg1_epu32(msg1, msg2);

        // Round 8-11 del blocco da 16
        msg2 = _mm_add_epi32(msg2, _mm_sha256msg2_epu32(_mm_alignr_epi8(msg1, msg0, 4), msg1));
        tmp = _mm_add_epi32(msg2, _mm_loadu_si128((const __m128i*)&K256[j+8]));
        st1 = _mm_sha256rnds2_epu32(st1, st0, tmp);
        st0 = _mm_sha256rnds2_epu32(st0, st1, _mm_alignr_epi8(tmp, tmp, 8));
        msg2 = _mm_sha256msg1_epu32(msg2, msg3);

        // Round 12-15 del blocco da 16
        msg3 = _mm_add_epi32(msg3, _mm_sha256msg2_epu32(_mm_alignr_epi8(msg2, msg1, 4), msg2));
        tmp = _mm_add_epi32(msg3, _mm_loadu_si128((const __m128i*)&K256[j+12]));
        st1 = _mm_sha256rnds2_epu32(st1, st0, tmp);
        st0 = _mm_sha256rnds2_epu32(st0, st1, _mm_alignr_epi8(tmp, tmp, 8));
        msg3 = _mm_sha256msg1_epu32(msg3, msg0);
    }

    st0 = _mm_add_epi32(st0, old_st0);
    st1 = _mm_add_epi32(st1, old_st1);
}

static void
extract_digest(__m128i st0, __m128i st1, uint8* out)
{
    uint32* out32 = (uint32*)out;
    // Inversione finale: SHA-NI tiene A in posizione 0, noi lo vogliamo nel byte 0 dell'output
    // Quindi: out[0-3] = A, out[4-7] = B, etc.
    out32[0] = __builtin_bswap32(_mm_extract_epi32(st0, 3)); 
    out32[1] = __builtin_bswap32(_mm_extract_epi32(st0, 2));
    out32[2] = __builtin_bswap32(_mm_extract_epi32(st0, 1));
    out32[3] = __builtin_bswap32(_mm_extract_epi32(st0, 0));
    out32[4] = __builtin_bswap32(_mm_extract_epi32(st1, 3));
    out32[5] = __builtin_bswap32(_mm_extract_epi32(st1, 2));
    out32[6] = __builtin_bswap32(_mm_extract_epi32(st1, 1));
    out32[7] = __builtin_bswap32(_mm_extract_epi32(st1, 0));
}

// Logica di Update corretta per residui (Cruciale per streaming e one-shot iov)
static void
_update(x86_sha256_context* ctx, const uint8* data, size_t len)
{
    if (len == 0) return;
    ctx->total_len += len;

    if (ctx->buffer_len > 0) {
        size_t fill = 64 - ctx->buffer_len;
        if (len >= fill) {
            memcpy(ctx->buffer + ctx->buffer_len, data, fill);
            _sha256_transform_core(ctx->state0, ctx->state1, ctx->buffer);
            ctx->buffer_len = 0;
            len -= fill;
            data += fill;
        }
    }

    while (len >= 64) {
        _sha256_transform_core(ctx->state0, ctx->state1, data);
        len -= 64;
        data += 64;
    }

    if (len > 0) {
        memcpy(ctx->buffer + ctx->buffer_len, data, len);
        ctx->buffer_len += len;
    }
}

// --- API IMPLEMENTATION ---

status_t x86_sha256_init_bridge(void** ctx_out, size_t* size) {
    *size = sizeof(x86_sha256_context);
    *ctx_out = memalign(64, *size);
    if (!*ctx_out) return B_NO_MEMORY;
    
    x86_sha256_context* ctx = (x86_sha256_context*)*ctx_out;
    memset(ctx, 0, sizeof(x86_sha256_context));
    
    ctx->state0 = _mm_set_epi32(0xa54ff53a, 0x3c6ef372, 0xbb67ae85, 0x6a09e667);
    ctx->state1 = _mm_set_epi32(0x5be0cd19, 0x1f83d9ab, 0x9b05688c, 0x510e527f);
/*ctx->state0 = _mm_set_epi32(
    0x6a09e667, // A finisce nell'indice 3
    0xbb67ae85, // B finisce nell'indice 2
    0x3c6ef372, // C finisce nell'indice 1
    0xa54ff53a  // D finisce nell'indice 0
);

ctx->state1 = _mm_set_epi32(
    0x510e527f, // E finisce nell'indice 3
    0x9b05688c, // F finisce nell'indice 2
    0x1f83d9ab, // G finisce nell'indice 1
    0x5be0cd19  // H finisce nell'indice 0
);*/
    return B_OK;
}

status_t x86_sha256_update_bridge(void* ctx_void, const iovec* vecs, size_t count) {
    x86_sha256_context* ctx = (x86_sha256_context*)ctx_void;
    cpu_status cpu_state = disable_interrupts();
    bcrypto_save_regs(&ctx->fpu_save);
    for (size_t i = 0; i < count; i++)
        _update(ctx, (const uint8*)vecs[i].iov_base, vecs[i].iov_len);
    bcrypto_restore_regs(&ctx->fpu_save);
    restore_interrupts(cpu_state);
    return B_OK;
}

status_t x86_sha256_final_bridge(void* ctx_void, uint8* out) {
    x86_sha256_context* ctx = (x86_sha256_context*)ctx_void;
    cpu_status cpu_state = disable_interrupts();
    bcrypto_save_regs(&ctx->fpu_save);

    alignas(16) uint8 pad[128];
    uint64 total_bits = ctx->total_len * 8;
    
    size_t last_len = ctx->buffer_len;
    memcpy(pad, ctx->buffer, last_len);
    pad[last_len++] = 0x80;

    size_t pad_size = (last_len <= 56) ? 64 : 128;
    memset(pad + last_len, 0, pad_size - last_len);
    
    uint64 be_bits = __builtin_bswap64(total_bits);
    memcpy(pad + pad_size - 8, &be_bits, 8);

    _sha256_transform_core(ctx->state0, ctx->state1, pad);
    if (pad_size == 128)
        _sha256_transform_core(ctx->state0, ctx->state1, pad + 64);

    extract_digest(ctx->state0, ctx->state1, out);
    
    bcrypto_restore_regs(&ctx->fpu_save);
    restore_interrupts(cpu_state);
    free(ctx);
    return B_OK;
}


status_t x86_sha256_process(BCryptoRequest* request) {
    void* ctx;
    size_t size;
    x86_sha256_init_bridge(&ctx, &size);
    x86_sha256_update_bridge(ctx, request->source, request->vectorCount);
    return x86_sha256_final_bridge(ctx, (uint8*)request->destination[0].iov_base);
}

status_t BInitx86CPUDigest() {
    if (!(BGetStoredCryptoCapabilities() & B_CRYPTO_HW_SHA_NI))
        return B_UNSUPPORTED;

    static BCryptoAlgorithm sSHA256_HW = {
        .algorithm = B_CRYPTO_SHA256,
        .mode      = B_CRYPTO_MODE_ANY,
        .flags     = B_CRYPTO_ALG_HW_ACCEL,
        .name      = "SHA256 (SHA-NI)",
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

