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
{
	uint64 m[16];
	uint64 v[16];
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
/*
#define Gs(r,i,a,b,c,d) \
	do { \
		a = a + b + m[blake2_sigma[r][2*i]]; \
		d = rotr32(d ^ a, 16); \
		c = c + d; \
		b = rotr32(b ^ c, 12); \
		a = a + b + m[blake2_sigma[r][2*i+1]]; \
		d = rotr32(d ^ a, 8); \
		c = c + d; \
		b = rotr32(b ^ c, 7); \
	} while(0)
*/
/*#define Gs(r,i,a,b,c,d) \
    do { \
        a = (uint32)(a + b + m[blake2_sigma[r][2*i]]); \
        d = rotr32(d ^ a, 16); \
        c = (uint32)(c + d); \
        b = rotr32(b ^ c, 12); \
        a = (uint32)(a + b + m[blake2_sigma[r][2*i+1]]); \
        d = rotr32(d ^ a, 8); \
        c = (uint32)(c + d); \
        b = rotr32(b ^ c, 7); \
    } while(0)*/
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
{
	uint32 m[16];
	uint32 v[16];
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
	
	
	if (ctx->t[0] == 3) { // solo per il test "abc"
        dprintf("DEBUG: v[12]=%08x, v[14]=%08x\n", v[12], v[14]);
    }
    dprintf("DEBUG: m[0]=%08x, m[1]=%08x\n", (unsigned int)m[0], (unsigned int)m[1]);

	// BLAKE2s fa 10 round invece di 12
	for (i = 0; i < 10; ++i) {
	/*for (int r = 0; r < 10; ++r) {
		#define Gs(a,b,c,d,m1,m2) \
            a = (uint32)(a + b + m1); d = rotr32(d ^ a, 16); \
            c = (uint32)(c + d);      b = rotr32(b ^ c, 12); \
            a = (uint32)(a + b + m2); d = rotr32(d ^ a, 8); \
            c = (uint32)(c + d);      b = rotr32(b ^ c, 7);

        // Colonne
        Gs(v[0], v[4], v[8],  v[12], m[blake2_sigma[r][0]], m[blake2_sigma[r][1]]);
        Gs(v[1], v[5], v[9],  v[13], m[blake2_sigma[r][2]], m[blake2_sigma[r][3]]);
        Gs(v[2], v[6], v[10], v[14], m[blake2_sigma[r][4]], m[blake2_sigma[r][5]]);
        Gs(v[3], v[7], v[11], v[15], m[blake2_sigma[r][6]], m[blake2_sigma[r][7]]);
        // Diagonali
        Gs(v[0], v[5], v[10], v[15], m[blake2_sigma[r][8]], m[blake2_sigma[r][9]]);
        Gs(v[1], v[6], v[11], v[12], m[blake2_sigma[r][10]], m[blake2_sigma[r][11]]);
        Gs(v[2], v[7], v[8],  v[13], m[blake2_sigma[r][12]], m[blake2_sigma[r][13]]);
        Gs(v[3], v[4], v[9],  v[14], m[blake2_sigma[r][14]], m[blake2_sigma[r][15]]);
        
        #undef G
        */
		
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
	/*
	memset(ctx, 0, sizeof(SoftBlake2sContext));
	for (int i = 0; i < 8; ++i) ctx->h[i] = blake2s_iv[i];
	// XOR con parametri: outlen e keylen (0 in questo caso)
	//ctx->h[0] ^= 0x01010000 | (uint32)outlen;
	ctx->h[0] ^= 0x01010000 ^ (uint32)outlen;
	ctx->outlen = outlen;
	*/
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
 
static const uint32 blake3_iv[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
};

// Indici di permutazione specifici per BLAKE3
static const uint8 blake3_sigma[7][16] = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
    {2, 6, 3, 10, 7, 0, 4, 13, 1, 11, 12, 5, 9, 14, 15, 8},
    {3, 4, 10, 12, 13, 2, 7, 14, 6, 5, 9, 0, 11, 15, 8, 1},
    {10, 7, 12, 9, 14, 3, 13, 15, 4, 0, 11, 2, 5, 8, 1, 6},
    {12, 13, 9, 11, 15, 10, 14, 8, 7, 2, 5, 3, 0, 1, 6, 4},
    {9, 15, 11, 8, 1, 12, 15, 6, 13, 3, 0, 10, 2, 4, 7, 5}, // Nota: BLAKE3 ha 7 round
    {11, 1, 8, 6, 2, 9, 1, 4, 15, 10, 2, 12, 13, 0, 3, 7}
};

#define Gs3(r,i,a,b,c,d) \
    do { \
        a = a + b + m[blake3_sigma[r][2*i]]; \
        d = rotr32(d ^ a, 16); \
        c = c + d; \
        b = rotr32(b ^ c, 12); \
        a = a + b + m[blake3_sigma[r][2*i+1]]; \
        d = rotr32(d ^ a, 8); \
        c = c + d; \
        b = rotr32(b ^ c, 7); \
    } while(0)

// Flag BLAKE3
enum blake3_flags {
    CHUNK_START = 1 << 0,
    CHUNK_END   = 1 << 1,
    PARENT      = 1 << 2,
    ROOT        = 1 << 3
};

static void blake3_compress(const uint32 cv[8], const uint8 block[64], 
                           uint64 counter, uint8 block_len, uint8 flags, uint32 out[16])
{
    uint32 m[16];
    for (int i = 0; i < 16; i++) memcpy(&m[i], block + i * 4, 4);

    uint32 v[16] = {
        cv[0], cv[1], cv[2], cv[3], cv[4], cv[5], cv[6], cv[7],
        blake3_iv[0], blake3_iv[1], blake3_iv[2], blake3_iv[3],
        (uint32)counter, (uint32)(counter >> 32), (uint32)block_len, (uint32)flags
    };

    for (int r = 0; r < 7; r++) {
        Gs3(r, 0, v[0], v[4], v[8], v[12]); // Usiamo la stessa macro Gs di BLAKE2s
        Gs3(r, 1, v[1], v[5], v[9], v[13]);
        Gs3(r, 2, v[2], v[6], v[10], v[14]);
        Gs3(r, 3, v[3], v[7], v[11], v[15]);
        Gs3(r, 4, v[0], v[5], v[10], v[15]);
        Gs3(r, 5, v[1], v[6], v[11], v[12]);
        Gs3(r, 6, v[2], v[7], v[8], v[13]);
        Gs3(r, 7, v[3], v[4], v[9], v[14]);
    }

    for (int i = 0; i < 8; i++) {
        //v[i] ^= v[i + 8];
        //v[i + 8] ^= cv[i];
        out[i] = v[i] ^ v[i + 8];
        out[i + 8] = v[i + 8] ^ cv[i];
    }
    //memcpy(out, v, 16 * 4);
}

static void blake3_compress_parent(const uint32 left_child[8], const uint32 right_child[8],
                                  uint32 out[8])
{
    uint8 block[64];
    memcpy(block, left_child, 32);
    memcpy(block + 32, right_child, 32);

    uint32 full_out[16];
    // Il contatore per i nodi Parent è sempre 0
    // I flag includono sempre PARENT
    blake3_compress(blake3_iv, block, 0, 64, PARENT, full_out);
    memcpy(out, full_out, 32);
}

static void blake3_push_stack(SoftBlake3Context* ctx, uint32 new_cv[8])
{
    // ctx->stack_len tiene traccia di quanti nodi abbiamo (rappresenta l'altezza nell'albero)
    // Usiamo il bitmask di chunk_counter per capire quando unire
    uint64 i = ctx->chunk_counter;
    
    while (i & 1) {
        // Uniamo il nodo corrente con quello nello stack
        blake3_compress_parent(&ctx->stack[(ctx->stack_len - 1) * 8], new_cv, new_cv);
        ctx->stack_len--;
        i >>= 1;
    }
    
    // Salviamo il risultato nello stack
    memcpy(&ctx->stack[ctx->stack_len * 8], new_cv, 32);
    ctx->stack_len++;
}


void soft_blake3_init(SoftBlake3Context* ctx) {
    memset(ctx, 0, sizeof(SoftBlake3Context));
    memcpy(ctx->cv, blake3_iv, 32);
}

void soft_blake3_update(SoftBlake3Context* ctx, const uint8* in, size_t inlen)
{
    while (inlen > 0) {
        if (ctx->buflen == 64) {
            uint32 out[16];
            uint8 flags = (ctx->blocks_compressed == 0) ? CHUNK_START : 0;
            blake3_compress(ctx->cv, ctx->buf, ctx->chunk_counter, 64, flags, out);
            memcpy(ctx->cv, out, 32);
            ctx->blocks_compressed++;
            ctx->buflen = 0;
        }

        // Se abbiamo completato un Chunk (1024 byte)
        if (ctx->blocks_compressed == 16) {
            uint32 chunk_out[16];
            // L'ultimo blocco di un chunk ha il flag CHUNK_END
            blake3_compress(ctx->cv, ctx->buf, ctx->chunk_counter, 0, CHUNK_END, chunk_out); 
            
            // Push nell'albero di Merkle
            blake3_push_stack(ctx, chunk_out);
            
            // Reset per il prossimo chunk
            ctx->chunk_counter++;
            ctx->blocks_compressed = 0;
            memcpy(ctx->cv, blake3_iv, 32);
        }

        //size_t take = (inlen < (64 - ctx->buflen)) ? inlen : (64 - ctx->buflen);
        size_t space_left = 64 - (size_t)ctx->buflen;
        size_t take = (inlen < space_left) ? inlen : space_left;
        memcpy(ctx->buf + ctx->buflen, in, take);
        ctx->buflen += (uint8)take;
        in += take;
        inlen -= take;
    }
}

void soft_blake3_finalize(SoftBlake3Context* ctx, uint8* out, size_t outlen)
{
    uint32 current_cv[8];
    uint32 block_out[16];
    
    // 1. Finalizziamo l'ultimo chunk
    uint8 flags = CHUNK_END;
    if (ctx->blocks_compressed == 0) flags |= CHUNK_START;
    
    // Se questo è l'unico chunk di tutto il messaggio, è anche la ROOT
    if (ctx->stack_len == 0) flags |= ROOT;

    blake3_compress(ctx->cv, ctx->buf, ctx->chunk_counter, ctx->buflen, flags, block_out);
    memcpy(current_cv, block_out, 32);

    // 2. Risaliamo l'albero di Merkle (se ci sono nodi nello stack)
    while (ctx->stack_len > 0) {
        uint8 parent_flags = PARENT;
        // Se stiamo unendo l'ultimo nodo dello stack, questa è la ROOT
        if (ctx->stack_len == 1) parent_flags |= ROOT;
        
        // Prepariamo il blocco parent (64 byte: 32 left + 32 right)
        uint8 parent_block[64];
        memcpy(parent_block, &ctx->stack[(ctx->stack_len - 1) * 8], 32);
        memcpy(parent_block + 32, current_cv, 32);
        
        uint32 parent_out[16];
        // Nota: per i nodi PARENT il contatore è sempre 0 e il CV è l'IV iniziale
        blake3_compress(blake3_iv, parent_block, 0, 64, parent_flags, parent_out);
        
        memcpy(current_cv, parent_out, 32);
        ctx->stack_len--;
    }

    // 3. Copiamo il risultato finale
    memcpy(out, current_cv, outlen < 32 ? outlen : 32);
}
/*
void soft_blake3_update(SoftBlake3Context* ctx, const uint8* in, size_t inlen) {
    while (inlen > 0) {
        // Se il buffer è pieno (64 byte), comprimiamo
        if (ctx->buflen == 64) {
            uint32 out[16];
            uint8 flags = (ctx->blocks_compressed == 0) ? CHUNK_START : 0;
            blake3_compress(ctx->cv, ctx->buf, ctx->chunk_counter, 64, flags, out);
            memcpy(ctx->cv, out, 32);
            ctx->blocks_compressed++;
            ctx->buflen = 0;
        }

        // Se abbiamo finito un chunk (1024 byte = 16 blocchi da 64)
        if (ctx->blocks_compressed == 16) {
            // Qui andrebbe la logica di push nello stack per l'albero di Merkle
            ctx->chunk_counter++;
            ctx->blocks_compressed = 0;
            memcpy(ctx->cv, blake3_iv, 32);
        }

        size_t take = (inlen < (64 - ctx->buflen)) ? inlen : (64 - ctx->buflen);
        memcpy(ctx->buf + ctx->buflen, in, take);
        ctx->buflen += (uint8)take;
        in += take;
        inlen -= take;
    }
}

void soft_blake3_finalize(SoftBlake3Context* ctx, uint8* out, size_t outlen) {
    uint32 block_out[16];
    uint8 flags = CHUNK_END | ROOT;
    if (ctx->blocks_compressed == 0) flags |= CHUNK_START;
    
    // Se non abbiamo mai fatto merge (file piccolo), il chunk corrente è la root
    blake3_compress(ctx->cv, ctx->buf, ctx->chunk_counter, ctx->buflen, flags, block_out);
    
    // Per ora gestiamo solo l'output standard (32 byte)
    memcpy(out, block_out, outlen < 32 ? outlen : 32);
}*/
