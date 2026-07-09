/*
 * Software ChaCha20 for Haiku kernel crypto
 * Copyright 2026, Fabio Tomat
 * MIT License
 */

#include "soft_chacha20.h"
#include <string.h>

#define ROTL32(v, n) (((v) << (n)) | ((v) >> (32 - (n))))

// I 4 passi fondamentali del Quarter Round
#define QUARTERROUND(a, b, c, d) \
    a += b; d ^= a; d = ROTL32(d, 16); \
    c += d; b ^= c; b = ROTL32(b, 12); \
    a += b; d ^= a; d = ROTL32(d, 8);  \
    c += d; b ^= c; b = ROTL32(b, 7);

void
chacha20_init(ChaCha20Context* ctx, const uint8* key, const uint8* nonce, uint32 counter)
{
    // Costanti "expand 32-byte k"
    ctx->state[0] = 0x61707865;
    ctx->state[1] = 0x3320646e;
    ctx->state[2] = 0x79622d32;
    ctx->state[3] = 0x6b206574;

    // Chiave (8 word da 32 bit)
    for (int i = 0; i < 8; i++) {
        ctx->state[4 + i] = ((uint32*)key)[i];
    }

    // Contatore di blocco
    ctx->state[12] = counter;

    // Nonce (3 word da 32 bit per lo standard RFC 7539)
    // Se il tuo nonce è di 12 byte (96 bit)
    for (int i = 0; i < 3; i++) {
        ctx->state[13 + i] = ((uint32*)nonce)[i];
    }
}

void
chacha20_process(ChaCha20Context* ctx, const uint8* in, uint8* out, size_t len)
{
    uint32 x[16];
    uint8 keystream[64];
    size_t i;

    while (len > 0) {
        // Copiamo lo stato interno per lavorarci (lo stato originale evolve col contatore)
        memcpy(x, ctx->state, sizeof(x));

        // 20 round (10 iterazioni di: colonna + diagonale)
        for (i = 0; i < 10; i++) {
            // Round sulle colonne
            QUARTERROUND(x[0], x[4], x[8],  x[12])
            QUARTERROUND(x[1], x[5], x[9],  x[13])
            QUARTERROUND(x[2], x[6], x[10], x[14])
            QUARTERROUND(x[3], x[7], x[11], x[15])
            // Round sulle diagonali
            QUARTERROUND(x[0], x[5], x[10], x[15])
            QUARTERROUND(x[1], x[6], x[11], x[12])
            QUARTERROUND(x[2], x[7], x[8],  x[13])
            QUARTERROUND(x[3], x[4], x[9],  x[14])
        }

        // Aggiungiamo lo stato iniziale al risultato dei round (Add)
        for (i = 0; i < 16; i++) {
            x[i] += ctx->state[i];
        }

        // Generiamo il keystream in byte
        memcpy(keystream, x, 64);

        // XOR tra input e keystream
        size_t take = (len > 64) ? 64 : len;
        for (i = 0; i < take; i++) {
            out[i] = in[i] ^ keystream[i];
        }

        // Aggiorniamo puntatori e contatore
        len -= take;
        in += take;
        out += take;
        ctx->state[12]++; // Incrementa il blocco per il prossimo keystream
    }
}
