/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include "soft_blake.h"
#include <string.h>
#include <debug.h> //da rimuovere

/* ---------------------------------------------------
 *              BLAKE2b
 * --------------------------------------------------- */


// Costanti IV per BLAKE2b (64-bit)
static const uint64 blake2b_iv[8] = {
	0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL,
	0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
	0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
	0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL
};

// Tabella di permutazione dei messaggi (Sigma)
static const uint8 blake2_sigma[12][16] = {
	{  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15 },
	{ 14, 10,  4,  8,  9, 15, 13,  6,  1, 12,  0,  2, 11,  7,  5,  3 },
	{ 11,  8, 12,  0,  5,  2, 15, 13, 10, 14,  3,  6,  7,  1,  9,  4 },
	{  7,  9,  3,  1, 13, 12, 11, 14,  2,  6,  5, 10,  4,  0, 15,  8 },
	{  9,  0,  5,  7,  2,  4, 10, 15, 14,  1, 11, 12,  6,  8,  3, 13 },
	{  2, 12,  6, 10,  0, 11,  8,  3,  4, 13,  7,  5, 15, 14,  1,  9 },
	{ 12,  5,  1, 15, 14, 13,  4, 10,  0,  7,  6,  3,  9,  2,  8, 11 },
	{ 13, 11,  7, 14, 12,  1,  3,  9,  5,  0, 15,  4,  8,  6,  2, 10 },
	{  6, 15, 14,  9, 11,  3,  0,  8, 12,  2, 13,  7,  1,  4, 10,  5 },
	{ 10,  2,  8,  4,  7,  6,  1,  5, 15, 11,  9, 14,  3, 12, 13,  0 },
	{  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15 },
	{ 14, 10,  4,  8,  9, 15, 13,  6,  1, 12,  0,  2, 11,  7,  5,  3 }
};

// Funzione di rotazione a destra
static inline uint64 rotr64(uint64 w, uint32 n) {
	return (w >> n) | (w << (64 - n));
}

#define G(r,i,a,b,c,d) \
	do { \
		a = a + b + m[blake2_sigma[r][2*i]]; \
		d = rotr64(d ^ a, 32); \
		c = c + d; \
		b = rotr64(b ^ c, 24); \
		a = a + b + m[blake2_sigma[r][2*i+1]]; \
		d = rotr64(d ^ a, 16); \
		c = c + d; \
		b = rotr64(b ^ c, 63); \
	} while(0)
static void soft_blake2b_compress(SoftBlake2bContext* ctx, const uint8 block[128])
//extern "C" void soft_blake2b_compress(SoftBlake2bContext* ctx, const uint8 block[128])
{
	//uint64 m[16];
	//uint64 v[16];
	uint64* m = ctx->m_work;
    uint64* v = ctx->v_work;
	int i;

	// Carichiamo i dati in parole a 64-bit (Little Endian)
	for (i = 0; i < 16; ++i)
		memcpy(&m[i], block + i * 8, 8);

	// Inizializziamo il vettore di lavoro v
	for (i = 0; i < 8; ++i) v[i] = ctx->h[i];
	for (i = 0; i < 8; ++i) v[i + 8] = blake2b_iv[i];

	// Applichiamo il contatore e i flag di finalizzazione
	v[12] ^= ctx->t[0];
	v[13] ^= ctx->t[1];
	v[14] ^= ctx->f[0];
	v[15] ^= ctx->f[1];

	// 12 round di mescolamento
	for (i = 0; i < 12; ++i) {
		G(i, 0, v[0], v[4], v[8], v[12]);
		G(i, 1, v[1], v[5], v[9], v[13]);
		G(i, 2, v[2], v[6], v[10], v[14]);
		G(i, 3, v[3], v[7], v[11], v[15]);
		G(i, 4, v[0], v[5], v[10], v[15]);
		G(i, 5, v[1], v[6], v[11], v[12]);
		G(i, 6, v[2], v[7], v[8], v[13]);
		G(i, 7, v[3], v[4], v[9], v[14]);
	}

	// Aggiorniamo l'hash finale
	for (i = 0; i < 8; ++i)
		ctx->h[i] ^= v[i] ^ v[i + 8];
}

void soft_blake2b_init(SoftBlake2bContext* ctx, size_t outlen)
{
	memset(ctx, 0, sizeof(SoftBlake2bContext));
	// IV XORed con i parametri (output length e key length)
	for (int i = 0; i < 8; ++i) ctx->h[i] = blake2b_iv[i];
	ctx->h[0] ^= 0x01010000 | outlen; 
	ctx->outlen = outlen;
}

void soft_blake2b_update(SoftBlake2bContext* ctx, const uint8* in, size_t inlen)
{
	for (size_t i = 0; i < inlen; ++i) {
		if (ctx->buflen == 128) {
			ctx->t[0] += ctx->buflen; // Aggiorna contatore byte
			if (ctx->t[0] < ctx->buflen) ctx->t[1]++;
			soft_blake2b_compress(ctx, ctx->buf);
			ctx->buflen = 0;
		}
		ctx->buf[ctx->buflen++] = in[i];
	}
}

void soft_blake2b_finalize(SoftBlake2bContext* ctx, uint8* out)
{
	ctx->t[0] += ctx->buflen;
	if (ctx->t[0] < ctx->buflen) ctx->t[1]++;
	
	ctx->f[0] = ~0ULL; // Flag di ultimo blocco
	
	// Riempimento con zeri se necessario
	memset(ctx->buf + ctx->buflen, 0, 128 - ctx->buflen);
	soft_blake2b_compress(ctx, ctx->buf);

	// Copia l'hash nel buffer di output
	uint8 result[64];
	for (int i = 0; i < 8; ++i)
		memcpy(result + i * 8, &ctx->h[i], 8);
	
	memcpy(out, result, ctx->outlen);
}


/* ---------------------------------------------------
 *              BLAKE2s
 * --------------------------------------------------- */
 
// Costanti IV per BLAKE2s (32-bit)
static const uint32 blake2s_iv[8] = {
	0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
	0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
};

// Rotazione a destra per 32 bit
static inline uint32 rotr32(const uint32 w, const uint32 n) {
    return (w >> n) | (w << (32 - n));
}

static inline uint32 load32(const void *p) {
    uint32 v;
    memcpy(&v, p, 4);
    return v;
}

#define Gs(r,i,a,b,c,d) \
    do { \
        a = (uint32)(a + b + m[blake2_sigma[r][2*i]]); \
        d = rotr32(d ^ a, 16); \
        c = (uint32)(c + d); \
        b = rotr32(b ^ c, 12); \
        a = (uint32)(a + b + m[blake2_sigma[r][2*i+1]]); \
        d = rotr32(d ^ a, 8); \
        c = (uint32)(c + d); \
        b = rotr32(b ^ c, 7); \
    } while(0)
static void soft_blake2s_compress(SoftBlake2sContext* ctx, const uint8 block[64])
//extern "C" void soft_blake2s_compress(SoftBlake2sContext* ctx, const uint8 block[64])
{
	//uint32 m[16];
	//uint32 v[16];
	uint32* m = ctx->m_work;
    uint32* v = ctx->v_work;
	int i;

	for (i = 0; i < 16; ++i)
		//memcpy(&m[i], block + i * 4, 4);
		m[i] = load32(block + i * 4);

	for (i = 0; i < 8; ++i) v[i] = ctx->h[i];
	for (i = 0; i < 8; ++i) v[i + 8] = blake2s_iv[i];

	v[12] ^= ctx->t[0]; // Contatore byte
	v[13] ^= ctx->t[1];
	v[14] ^= ctx->f[0]; // Flag finale
	v[15] ^= ctx->f[1];

	// BLAKE2s fa 10 round invece di 12
	for (i = 0; i < 10; ++i) {
		Gs(i, 0, v[0], v[4], v[8], v[12]);
		Gs(i, 1, v[1], v[5], v[9], v[13]);
		Gs(i, 2, v[2], v[6], v[10], v[14]);
		Gs(i, 3, v[3], v[7], v[11], v[15]);
		Gs(i, 4, v[0], v[5], v[10], v[15]);
		Gs(i, 5, v[1], v[6], v[11], v[12]);
		Gs(i, 6, v[2], v[7], v[8], v[13]);
		Gs(i, 7, v[3], v[4], v[9], v[14]);
	}

	for (i = 0; i < 8; ++i)
		ctx->h[i] ^= v[i] ^ v[i + 8];
}

void soft_blake2s_init(SoftBlake2sContext* ctx, size_t outlen)
{
	memset(ctx, 0, sizeof(SoftBlake2sContext));
    for (int i = 0; i < 8; ++i) ctx->h[i] = blake2s_iv[i];
    
    // BLAKE2s specifica: h[0] ^= 0x01010000 | (keylen << 8) | outlen
    // Con keylen = 0 e outlen = 32: 0x01010020
    uint32 param = 0x01010000 | (uint32)outlen;
    ctx->h[0] ^= param; 
    
    ctx->outlen = outlen;
}

void soft_blake2s_update(SoftBlake2sContext* ctx, const uint8* in, size_t inlen)
{
	for (size_t i = 0; i < inlen; ++i) {
		if (ctx->buflen == 64) { // Blocco da 64 byte per BLAKE2s
			ctx->t[0] += ctx->buflen;
			if (ctx->t[0] < ctx->buflen) ctx->t[1]++;
			soft_blake2s_compress(ctx, ctx->buf);
			ctx->buflen = 0;
		}
		ctx->buf[ctx->buflen++] = in[i];
	}
}

void soft_blake2s_finalize(SoftBlake2sContext* ctx, uint8* out)
{
	ctx->t[0] += (uint32)ctx->buflen;
	if (ctx->t[0] < (uint32)ctx->buflen) ctx->t[1]++;
	
	ctx->f[0] = 0xFFFFFFFF; // Flag ultimo blocco (32-bit)
	//ctx->f[1] = 0;
	
	memset(ctx->buf + ctx->buflen, 0, 64 - ctx->buflen);
	soft_blake2s_compress(ctx, ctx->buf);

	/*uint8 result[32];
	for (int i = 0; i < 8; ++i)
		memcpy(result + i * 4, &ctx->h[i], 4);
	
	memcpy(out, result, ctx->outlen);*/
	// Conversione esplicita in Little-Endian per l'output
    for (int i = 0; i < 8; ++i) {
        out[i*4 + 0] = (uint8)(ctx->h[i] >>  0);
        out[i*4 + 1] = (uint8)(ctx->h[i] >>  8);
        out[i*4 + 2] = (uint8)(ctx->h[i] >> 16);
        out[i*4 + 3] = (uint8)(ctx->h[i] >> 24);
    }
}

/* ---------------------------------------------------
 *              BLAKE3
 * --------------------------------------------------- */

// BLAKE3 usa una permutazione diversa da BLAKE2
static const uint8 blake3_msg_schedule[7][16] = {
	{  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15 },
	{  2,  6,  3, 10,  7,  0,  4, 13,  1, 11, 12,  5,  9, 14, 15,  8 },
	{  3,  4, 10, 12, 13,  2,  7, 14,  6,  5,  9,  0, 11, 15,  8,  1 },
	{ 10,  7, 12,  9, 14,  3, 13, 15,  4,  0, 11,  2,  5,  8,  1,  6 },
	{ 12, 13,  9, 11, 15, 10, 14,  8,  7,  2,  5,  3,  0,  1,  6,  4 },
	{  9, 14, 11, 15,  8, 12,  5,  1, 13,  3,  0, 10,  2,  6,  4,  7 },
	{ 11, 15,  8,  1, 14,  9,  5, 12,  6,  0, 10,  2, 13,  4,  7,  3 }
};

static void soft_blake3_compress(SoftBlake3Context* ctx, const uint32 cv[8], const uint8 block[64], 
                                 uint32 t, uint32 flags, uint32 block_len, uint32 out[16])
{
	//if (flags & 8) { // 8 è il valore bitwise di ROOT
    //    dprintf("DEBUG: Sto processando il blocco ROOT!\n");
    //}
    /* non usiamo più per rischio di riempire lo stack li abbiamo messi nel contesto
    uint32 m[16];
    uint32 v[16];*/
    uint32* m = ctx->m_work;
    uint32* v = ctx->v_work;
    for (int i = 0; i < 16; i++) m[i] = load32(block + i * 4);
    //dprintf("BLAKE3_DEBUG: t=%u, flags=%u, len=%u\n", t, flags, block_len);
    //dprintf("BLAKE3_DEBUG: m[0]=%08x, m[1]=%08x\n", m[0], m[1]);

    // Inizializzazione stato
    for (int i = 0; i < 8; i++) v[i] = cv[i];
    //for (int i = 0; i < 4; i++) v[i+8] = blake2s_iv[i]; lo scriviamo così:
    v[8]  = blake2s_iv[0];                              // v[8..11] fissi
    v[9]  = blake2s_iv[1];
    v[10] = blake2s_iv[2];
    v[11] = blake2s_iv[3];
    v[12] = t;        // Counter t0
    v[13] = 0;        // BLAKE3 usa t1 solo per messaggi immensi (> 2^32)
    v[14] = block_len;       // In BLAKE3 la lunghezza del blocco è fissa a 64
    v[15] = flags;
    
    //dprintf("BLAKE3_DEBUG: v_init[0..3]: %08x %08x %08x %08x\n", v[0], v[1], v[2], v[3]);

    for (int r = 0; r < 7; r++) { // BLAKE3 fa solo 7 round!
        #define Gw(a,b,c,d,m1,m2) \
            a = (uint32)(a + b + m1); d = rotr32(d ^ a, 16); \
            c = (uint32)(c + d);      b = rotr32(b ^ c, 12); \
            a = (uint32)(a + b + m2); d = rotr32(d ^ a, 8); \
            c = (uint32)(c + d);      b = rotr32(b ^ c, 7);

        Gw(v[0], v[4], v[8],  v[12], m[blake3_msg_schedule[r][0]], m[blake3_msg_schedule[r][1]]);
        Gw(v[1], v[5], v[9],  v[13], m[blake3_msg_schedule[r][2]], m[blake3_msg_schedule[r][3]]);
        Gw(v[2], v[6], v[10], v[14], m[blake3_msg_schedule[r][4]], m[blake3_msg_schedule[r][5]]);
        Gw(v[3], v[7], v[11], v[15], m[blake3_msg_schedule[r][6]], m[blake3_msg_schedule[r][7]]);
        Gw(v[0], v[5], v[10], v[15], m[blake3_msg_schedule[r][8]], m[blake3_msg_schedule[r][9]]);
        Gw(v[1], v[6], v[11], v[12], m[blake3_msg_schedule[r][10]], m[blake3_msg_schedule[r][11]]);
        Gw(v[2], v[7], v[8],  v[13], m[blake3_msg_schedule[r][12]], m[blake3_msg_schedule[r][13]]);
        Gw(v[3], v[4], v[9],  v[14], m[blake3_msg_schedule[r][14]], m[blake3_msg_schedule[r][15]]);
    }
    
    //dprintf("BLAKE3_DEBUG: v_post_rounds[0..3]: %08x %08x %08x %08x\n", v[0], v[1], v[2], v[3]);

    // Output: XOR tra le due metà
    for (int i = 0; i < 8; i++) {
        out[i] = v[i] ^ v[i+8];
        out[i+8] = v[i+8] ^ cv[i];
    }
}

static void blake3_compress_chunk(SoftBlake3Context* ctx, const uint32 cv_in[8], const uint8* chunk, 
                                  size_t chunk_len, uint32 chunk_counter, uint32 flags, 
                                  uint32 out_cv[8])
{
    uint32 cv[8];
    memcpy(cv, cv_in, 32);
    
    // Il primo blocco di ogni chunk ha sempre il flag CHUNK_START
    uint32 chunk_flags = flags | BLAKE3_CHUNK_START;
    
    size_t bytes_left = chunk_len;
    const uint8* p = chunk;

    // Processa i blocchi interni (tutti da 64 byte tranne potenzialmente l'ultimo)
    while (bytes_left > 64) {
        uint32 out16[16];
        // In BLAKE3, v[12] riceve l'indice del chunk (chunk_counter), NON il numero del blocco
        soft_blake3_compress(ctx, cv, p, chunk_counter, chunk_flags, 64, out16);
        
        // Il Chaining Value per il blocco successivo sono le prime 8 parole dell'output
        memcpy(cv, out16, 32);
        
        // Dopo il primo blocco, rimuoviamo il flag CHUNK_START
        chunk_flags = flags; 
        bytes_left -= 64;
        p += 64;
    }

    // L'ultimo blocco del chunk ha sempre il flag CHUNK_END
    uint32 final_flags = chunk_flags | BLAKE3_CHUNK_END;
    uint32 out16[16];
    
    // Gestione dell'ultimo blocco (padding con zeri se < 64 byte)
    uint8 last_block[64];
    memset(last_block, 0, 64);
    memcpy(last_block, p, bytes_left);
    
    // Passiamo la lunghezza reale (bytes_left) che finirà in v[14]
    soft_blake3_compress(ctx, cv, last_block, chunk_counter, final_flags, (uint32)bytes_left, out16);
    
    // L'output finale del chunk sono i primi 32 byte (8 parole)
    memcpy(out_cv, out16, 32);
}

static void blake3_push_stack(SoftBlake3Context* ctx, uint32 cv[8]) {
    uint32 i = 0;
    // Mentre abbiamo nodi da unire (albero di Merkle)
    while (i < 64 && (ctx->stack_depth & (1 << i))) {
        uint32 parent_out[16];
        uint8 block[64];
        // Copiamo i due CV figli in un unico blocco da 64 byte
        memcpy(block, ctx->stack[i], 32);
        memcpy(block + 32, cv, 32);
        
        soft_blake3_compress(ctx, blake2s_iv, block, 0, BLAKE3_PARENT, 64, parent_out);
        memcpy(cv, parent_out, 32);
        i++;
    }
    //memcpy(ctx->stack[i], cv, 32);
    //ctx->stack_depth++;
    if (i < 64) {
        memcpy(ctx->stack[i], cv, 32);
        ctx->stack_depth++;
    }
}

void soft_blake3_init(SoftBlake3Context* ctx)
{
	dprintf("BLAKE3: IV CHECK: %08x %08x\n", blake2s_iv[0], blake2s_iv[1]);
    memset(ctx, 0, sizeof(SoftBlake3Context));
    for (int i = 0; i < 8; i++) {
        ctx->h[i] = blake2s_iv[i];
    }
    // I flag iniziali sono 0, verranno impostati durante il processo
}


void soft_blake3_update(SoftBlake3Context* ctx, const uint8* in, size_t inlen)
{
    while (inlen > 0) {
        // Se il buffer del chunk corrente è pieno, comprimilo
        if (ctx->buflen == BLAKE3_CHUNK_SIZE) {
            uint32 chunk_cv[8];
            // t è l'indice del chunk (0, 1, 2...)
            blake3_compress_chunk(ctx, ctx->h, ctx->buf, BLAKE3_CHUNK_SIZE, 
                                  ctx->t[0]++, ctx->flags, chunk_cv);
            blake3_push_stack(ctx, chunk_cv);
            ctx->buflen = 0;
        }

        size_t want = BLAKE3_CHUNK_SIZE - ctx->buflen;
        size_t take = (inlen < want) ? inlen : want;
        memcpy(ctx->buf + ctx->buflen, in, take);

        ctx->buflen += take;
        in += take;
        inlen -= take;
    }
}




void soft_blake3_finalize(SoftBlake3Context* ctx, uint8* out, size_t outlen)
{
	/* Originale
    // 1. Comprimi l'ultimo chunk rimasto nel buffer
    uint32 current_cv[8];
    uint32 chunk_flags = ctx->flags;
    
    // Se l'albero è vuoto (messaggio corto), questo è anche il ROOT
    if (ctx->stack_depth == 0) chunk_flags |= BLAKE3_ROOT;
    
    blake3_compress_chunk(ctx->h, ctx->buf, ctx->buflen, 
                          ctx->t[0], chunk_flags, current_cv);

    // 2. Risali l'albero (Merge dei nodi nello stack)
    while (ctx->stack_depth > 0) {
        ctx->stack_depth--;
        uint32 parent_out[16];
        uint8 block[64];
        
        // Il nodo a sinistra viene dallo stack, quello a destra è il corrente
        memcpy(block, ctx->stack[ctx->stack_depth], 32);
        memcpy(block + 32, current_cv, 32);
        
        uint32 flags = BLAKE3_PARENT;
        if (ctx->stack_depth == 0) flags |= BLAKE3_ROOT;
        
        soft_blake3_compress(blake2s_iv, block, 0, flags, 64, parent_out);
        memcpy(current_cv, parent_out, 32);
    }

    // 3. Output finale (estrazione Little Endian dal CV risultante)
    for (int i = 0; i < 8; i++) {
        out[i*4+0] = (uint8)(current_cv[i] >> 0);
        out[i*4+1] = (uint8)(current_cv[i] >> 8);
        out[i*4+2] = (uint8)(current_cv[i] >> 16);
        out[i*4+3] = (uint8)(current_cv[i] >> 24);
    }*/
    uint32 current_cv[8];
    uint32 out16[16]; // Buffer per l'output finale esteso
    
    // 1. Gestione dell'ultimo chunk rimasto nel buffer
    uint32 flags = ctx->flags | BLAKE3_CHUNK_END;
    
    // Se non ci sono nodi nello stack, questo chunk è anche l'inizio e la radice
    if (ctx->stack_depth == 0) {
        flags |= BLAKE3_CHUNK_START | BLAKE3_ROOT;
        
        uint8 last_block[64];
        memset(last_block, 0, 64);
        memcpy(last_block, ctx->buf, ctx->buflen);
        
        // Esecuzione finale: otteniamo out16 direttamente
        soft_blake3_compress(ctx, ctx->h, last_block, ctx->t[0], flags, (uint32)ctx->buflen, out16);
    } else {
        // Messaggio lungo: comprimiamo l'ultimo chunk come CV e risaliamo
        blake3_compress_chunk(ctx, ctx->h, ctx->buf, ctx->buflen, 
                              ctx->t[0], flags, current_cv);

        // 2. Risalita dell'albero (Merge dei nodi nello stack)
        while (ctx->stack_depth > 0) {
            ctx->stack_depth--;
            uint8 block[64];
            
            // Il nodo a sinistra viene dallo stack, quello a destra è il CV corrente
            memcpy(block, ctx->stack[ctx->stack_depth], 32);
            memcpy(block + 32, current_cv, 32);
            
            uint32 parent_flags = BLAKE3_PARENT;
            if (ctx->stack_depth == 0) {
                // Siamo arrivati alla radice dell'albero!
                parent_flags |= BLAKE3_ROOT;
                soft_blake3_compress(ctx, blake2s_iv, block, 0, parent_flags, 64, out16);
            } else {
                // Nodo intermedio: calcoliamo il CV per il livello superiore
                uint32 temp_out[16];
                soft_blake3_compress(ctx, blake2s_iv, block, 0, parent_flags, 64, temp_out);
                memcpy(current_cv, temp_out, 32);
            }
        }
    }

    // 3. Estrazione finale (Little Endian) da out16
    // BLAKE3 può produrre output arbitrariamente lunghi, qui limitiamo a outlen (tipicamente 32)
    size_t words_to_copy = outlen / 4;
    for (size_t i = 0; i < words_to_copy; i++) {
        out[i*4+0] = (uint8)(out16[i] >> 0);
        out[i*4+1] = (uint8)(out16[i] >> 8);
        out[i*4+2] = (uint8)(out16[i] >> 16);
        out[i*4+3] = (uint8)(out16[i] >> 24);
    }
}
