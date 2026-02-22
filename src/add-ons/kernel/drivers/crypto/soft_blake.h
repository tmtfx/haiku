/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef _SOFT_BLAKE_H
#define _SOFT_BLAKE_H

#include <SupportDefs.h>

/* Dimensioni massime dei digest */
#define BLAKE2B_OUTBYTES 64
#define BLAKE2S_OUTBYTES 32
#define BLAKE3_OUTBYTES  32

/* Contesto per BLAKE2b (64-bit) */
typedef struct {
    uint64 h[8];
    uint64 t[2];
    uint64 f[2];
    uint8  buf[128];
    size_t buflen;
    size_t outlen;
    
    uint64 v_work[16]; 
    uint64 m_work[16];
} SoftBlake2bContext;

/* Contesto per BLAKE2s (32-bit) */
typedef struct {
    uint32 h[8];
    uint32 t[2];
    uint32 f[2];
    uint8  buf[64];
    size_t buflen;
    size_t outlen;
    
    uint32 v_work[16]; 
    uint32 m_work[16];
} SoftBlake2sContext;

/* Contesto per BLAKE3 */
//typedef struct {
//    uint32 cv[8];
//    uint64 chunk_counter;
//    uint8  buf[64];
//    uint8  buflen;
//    uint8  blocks_compressed;
//    uint8  flags;
//    /* BLAKE3 richiede uno stack per l'albero di Merkle per file grandi */
//    uint32 stack[8 * 64];
//    size_t stack_len;
//} SoftBlake3Context;

#define BLAKE3_CHUNK_START          (1u << 0)
#define BLAKE3_CHUNK_END            (1u << 1)
#define BLAKE3_PARENT               (1u << 2)
#define BLAKE3_ROOT                 (1u << 3)
#define BLAKE3_CHUNK_SIZE           1024
#define BLAKE3_BLOCK_SIZE           64

// Il contesto deve tracciare lo stato dell'albero
typedef struct {
    uint32 h[8];
    uint32 t[2];        // Counter (numero di blocchi nel chunk corrente)
    uint32 stack[64][8]; // Per conservare gli hash dei nodi genitori
    uint8  stack_depth;
    uint8  buf[BLAKE3_CHUNK_SIZE];
    size_t buflen;
    uint32 flags;
    
    // BUFFER DI LAVORO (Sposta il carico dallo stack allo heap)
    uint32 v_work[16]; 
    uint32 m_work[16];
} SoftBlake3Context;


/* Prototipi BLAKE2b */
void soft_blake2b_init(SoftBlake2bContext* ctx, size_t outlen);
void soft_blake2b_update(SoftBlake2bContext* ctx, const uint8* in, size_t inlen);
void soft_blake2b_finalize(SoftBlake2bContext* ctx, uint8* out);

/* Prototipi BLAKE2s */
void soft_blake2s_init(SoftBlake2sContext* ctx, size_t outlen);
void soft_blake2s_update(SoftBlake2sContext* ctx, const uint8* in, size_t inlen);
void soft_blake2s_finalize(SoftBlake2sContext* ctx, uint8* out);

/* Prototipi BLAKE3 */
void soft_blake3_init(SoftBlake3Context* ctx);
void soft_blake3_update(SoftBlake3Context* ctx, const uint8* in, size_t inlen);
void soft_blake3_finalize(SoftBlake3Context* ctx, uint8* out, size_t outlen);

#endif /* _SOFT_BLAKE_H */
