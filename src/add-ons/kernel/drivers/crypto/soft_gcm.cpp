#include "SoftCryptoEngines.h"
#include <string.h>

/* Helper statico per lo shift a destra (Galois Field multiplication) */
/* Helper per lo shift a destra standard GCM */
static void
soft_gcm_shift_right(uint64 block[2])
{
    // Il bit che determina la riduzione è il bit meno significativo (LSB)
    // dell'ULTIMO byte, ovvero block[1] & 1.
    bool reduce = (block[1] & 1);
    
    block[1] = (block[1] >> 1) | (block[0] << 63);
    block[0] = (block[0] >> 1);
    
    if (reduce) {
        // Polinomio di riduzione GCM: 0xE1 (in formato Big Endian riflesso)
        block[0] ^= 0xE100000000000000ULL;
    }
}

void
soft_ghash_multiply(uint8* x, const uint8* h)
{
    uint64 Z[2] = {0, 0};
    uint64 V[2];
    uint64 h_be[2], x_be[2];

    // Caricamento Big-Endian (Fondamentale!)
    memcpy(h_be, h, 16);
    memcpy(x_be, x, 16);

    V[0] = __builtin_bswap64(h_be[0]);
    V[1] = __builtin_bswap64(h_be[1]);

    // Algoritmo di moltiplicazione bit-per-bit
    for (int i = 0; i < 128; i++) {
        // Estraiamo il bit i-esimo dal blocco x (in ordine NIST)
        // Il bit 0 è il MSB del byte 0.
        if ((x[i >> 3] >> (7 - (i & 7))) & 1) {
            Z[0] ^= V[0];
            Z[1] ^= V[1];
        }
        soft_gcm_shift_right(V);
    }

    // Risultato finale in Big-Endian
    uint64 res[2];
    res[0] = __builtin_bswap64(Z[0]);
    res[1] = __builtin_bswap64(Z[1]);
    memcpy(x, res, 16);
}

/* Aggiornamento dell'accumulatore GHASH */
void 
soft_ghash_update(GCMState* state, const uint8* data, size_t len)
{
    size_t pos = 0;
    while (pos < len) {
        // Gestiamo il blocco corrente (max 16 byte)
        size_t chunk = (len - pos < 16) ? len - pos : 16;
        
        // Se il chunk è incompleto (< 16), usiamo un buffer temporaneo 
        // per evitare di leggere memoria non valida (causa dei Panic)
        if (chunk < 16) {
            uint8 partial[16] = {0};
            memcpy(partial, data + pos, chunk);
            for (int j = 0; j < 16; j++) 
                state->tag_acc[j] ^= partial[j];
        } else {
            for (int j = 0; j < 16; j++) 
                state->tag_acc[j] ^= data[pos + j];
        }

        // Dopo ogni XOR di un blocco (anche parziale), moltiplichiamo per H
        soft_ghash_multiply(state->tag_acc, state->h_key);
        
        pos += chunk;
    }
}
