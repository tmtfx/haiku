/*
 * Software AES core (Rijndael) for Haiku kernel crypto
 * Copyright 2026, Fabio Tomat
 * MIT License
 */

#include "soft_aes.h"
#include <string.h>
/* ---------------------------------------------------------------- */
/* AES GCM helperfuncs                                              */
/* ---------------------------------------------------------------- */
// Moltiplicazione per 'x' nel campo di Galois (Shift a destra con riduzione)
static void
gcm_shift_right(uint64 block[2])
{
    uint64 mask = (block[1] & 1) ? 0xE100000000000000ULL : 0;
    block[1] = (block[1] >> 1) | (block[0] << 63);
    block[0] = (block[0] >> 1) ^ mask;
}
// Implementazione GHASH (moltiplicazione X * H)
// Questa versione è bit-a-bit. È la più sicura da implementare correttamente
// prima di passare alle tabelle di accelerazione.
void ghash_multiply(uint8* x, const uint8* h)
{
    uint64 V[2], Z[2] = {0, 0};
    
    // Carichiamo H e X come big-endian 64-bit
    // In GCM i bit sono riflessi, quindi usiamo un approccio cauto
    //V[0] = ((uint64*)h)[0]; // Nota: andrebbe gestito l'endianness se non su x86
    //V[1] = ((uint64*)h)[1];
    V[0] = __builtin_bswap64(((uint64*)h)[0]); 
    V[1] = __builtin_bswap64(((uint64*)h)[1]);

    for (int i = 0; i < 128; i++) {
        // Se il bit i-esimo di X è 1 (partendo dal bit più significativo)
        if ((x[i >> 3] >> (7 - (i & 7))) & 1) {
            Z[0] ^= V[0];
            Z[1] ^= V[1];
        }
        gcm_shift_right(V);
    }
    
    //((uint64*)x)[0] = Z[0];
    //((uint64*)x)[1] = Z[1];
    ((uint64*)x)[0] = __builtin_bswap64(Z[0]);
    ((uint64*)x)[1] = __builtin_bswap64(Z[1]);
}

/* ---------------------------------------------------------------- */
/* AES tables (static const, kernel-safe)                           */
/* ---------------------------------------------------------------- */
static const uint8 sbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

static const uint8 inv_sbox[256] = {
    0x52,0x09,0x6a,0xd5,0x30,0x36,0xa5,0x38,0xbf,0x40,0xa3,0x9e,0x81,0xf3,0xd7,0xfb,
    0x7c,0xe3,0x39,0x82,0x9b,0x2f,0xff,0x87,0x34,0x8e,0x43,0x44,0xc4,0xde,0xe9,0xcb,
    0x54,0x7b,0x94,0x32,0xa6,0xc2,0x23,0x3d,0xee,0x4c,0x95,0x0b,0x42,0xfa,0xc3,0x4e,
    0x08,0x2e,0xa1,0x66,0x28,0xd9,0x24,0xb2,0x76,0x5b,0xa2,0x49,0x6d,0x8b,0xd1,0x25,
    0x72,0xf8,0xf6,0x64,0x86,0x68,0x98,0x16,0xd4,0xa4,0x5c,0xcc,0x5d,0x65,0xb6,0x92,
    0x6c,0x70,0x48,0x50,0xfd,0xed,0xb9,0xda,0x5e,0x15,0x46,0x57,0xa7,0x8d,0x9d,0x84,
    0x90,0xd8,0xab,0x00,0x8c,0xbc,0xd3,0x0a,0xf7,0xe4,0x58,0x05,0xb8,0xb3,0x45,0x06,
    0xd0,0x2c,0x1e,0x8f,0xca,0x3f,0x0f,0x02,0xc1,0xaf,0xbd,0x03,0x01,0x13,0x8a,0x6b,
    0x3a,0x91,0x11,0x41,0x4f,0x67,0xdc,0xea,0x97,0xf2,0xcf,0xce,0xf0,0xb4,0xe6,0x73,
    0x96,0xac,0x74,0x22,0xe7,0xad,0x35,0x85,0xe2,0xf9,0x37,0xe8,0x1c,0x75,0xdf,0x6e,
    0x47,0xf1,0x1a,0x71,0x1d,0x29,0xc5,0x89,0x6f,0xb7,0x62,0x0e,0xaa,0x18,0xbe,0x1b,
    0xfc,0x56,0x3e,0x4b,0xc6,0xd2,0x79,0x20,0x9a,0xdb,0xc0,0xfe,0x78,0xcd,0x5a,0xf4,
    0x1f,0xdd,0xa8,0x33,0x88,0x07,0xc7,0x31,0xb1,0x12,0x10,0x59,0x27,0x80,0xec,0x5f,
    0x60,0x51,0x7f,0xa9,0x19,0xb5,0x4a,0x0d,0x2d,0xe5,0x7a,0x9f,0x93,0xc9,0x9c,0xef,
    0xa0,0xe0,0x3b,0x4d,0xae,0x2a,0xf5,0xb0,0xc8,0xeb,0xbb,0x3c,0x83,0x53,0x99,0x61,
    0x17,0x2b,0x04,0x7e,0xba,0x77,0xd6,0x26,0xe1,0x69,0x14,0x63,0x55,0x21,0x0c,0x7d
};

/* Rcon table for key expansion */
static const uint8 rcon[10] = {
    0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1B,0x36
};

/* ---------------------------------------------------------------- */
/* AES core helpers: xtime, mul, mixColumns, invMixColumns          */
/* ---------------------------------------------------------------- */

static inline uint8 xtime(uint8 x) {
    return (x << 1) ^ ((x & 0x80) ? 0x1b : 0x00);
}

static uint8 mul(uint8 a, uint8 b) {
    uint8 r = 0;
    while (b) {
        if (b & 1) r ^= a;
        a = xtime(a);
        b >>= 1;
    }
    return r;
}
static void shift_rows(uint8* s) {
    uint8 t[16];
    // Riga 0: nessuna rotazione
    t[0] = s[0]; t[4] = s[4]; t[8] = s[8]; t[12] = s[12];
    // Riga 1: rotazione a sinistra di 1
    t[1] = s[5]; t[5] = s[9]; t[9] = s[13]; t[13] = s[1];
    // Riga 2: rotazione a sinistra di 2
    t[2] = s[10]; t[6] = s[14]; t[10] = s[2]; t[14] = s[6];
    // Riga 3: rotazione a sinistra di 3
    t[3] = s[15]; t[7] = s[3]; t[11] = s[7]; t[15] = s[11];
    memcpy(s, t, 16);
}
static void inv_shift_rows(uint8* s) {
    uint8 t[16];
    // Riga 0: nessuna rotazione
    t[0] = s[0]; t[4] = s[4]; t[8] = s[8]; t[12] = s[12];
    // Riga 1: rotazione a DESTRA di 1
    t[1] = s[13]; t[5] = s[1]; t[9] = s[5]; t[13] = s[9];
    // Riga 2: rotazione a DESTRA di 2
    t[2] = s[10]; t[6] = s[14]; t[10] = s[2]; t[14] = s[6];
    // Riga 3: rotazione a DESTRA di 3
    t[3] = s[7]; t[7] = s[11]; t[11] = s[15]; t[15] = s[3];
    memcpy(s, t, 16);
}
static void mix_columns(uint8* s) {
    uint8 t[16];
    for (int c = 0; c < 4; c++) {
        int i = c*4;
        t[i+0] = mul(0x02,s[i+0]) ^ mul(0x03,s[i+1]) ^ s[i+2] ^ s[i+3];
        t[i+1] = s[i+0] ^ mul(0x02,s[i+1]) ^ mul(0x03,s[i+2]) ^ s[i+3];
        t[i+2] = s[i+0] ^ s[i+1] ^ mul(0x02,s[i+2]) ^ mul(0x03,s[i+3]);
        t[i+3] = mul(0x03,s[i+0]) ^ s[i+1] ^ s[i+2] ^ mul(0x02,s[i+3]);
    }
    memcpy(s,t,16);
}

static void inv_mix_columns(uint8* s) {
    uint8 t[16];
    for (int c = 0; c < 4; c++) {
        int i = c*4;
        t[i+0] = mul(0x0e,s[i+0]) ^ mul(0x0b,s[i+1]) ^ mul(0x0d,s[i+2]) ^ mul(0x09,s[i+3]);
        t[i+1] = mul(0x09,s[i+0]) ^ mul(0x0e,s[i+1]) ^ mul(0x0b,s[i+2]) ^ mul(0x0d,s[i+3]);
        t[i+2] = mul(0x0d,s[i+0]) ^ mul(0x09,s[i+1]) ^ mul(0x0e,s[i+2]) ^ mul(0x0b,s[i+3]);
        t[i+3] = mul(0x0b,s[i+0]) ^ mul(0x0d,s[i+1]) ^ mul(0x09,s[i+2]) ^ mul(0x0e,s[i+3]);
    }
    memcpy(s,t,16);
}

/* ---------------------------------------------------------------- */
/* Key schedule                                                      */
/* ---------------------------------------------------------------- */

static void sub_word(uint8* w) {
    for (int i = 0; i < 4; i++)
        w[i] = sbox[w[i]];
}

static void rot_word(uint8* w) {
    uint8 t = w[0];
    w[0] = w[1]; w[1] = w[2]; w[2] = w[3]; w[3] = t;
}

status_t soft_aes_set_key(SoftAESContext* ctx, const uint8* key, size_t keyLength) {
    if (!ctx || !key) return B_BAD_VALUE;

    int rounds = 0;
    int Nk = 0;

    switch (keyLength) {
        case 16: rounds = 10; Nk = 4; break;
        case 24: rounds = 12; Nk = 6; break;
        case 32: rounds = 14; Nk = 8; break;
        default: return B_BAD_VALUE;
    }
    ctx->rounds = rounds;

    // Copy initial key
    memcpy(ctx->encRoundKeys, key, keyLength);

    uint8 temp[4];
    int i = Nk;
    while (i < 4*(rounds+1)) {
        memcpy(temp, ctx->encRoundKeys + 4*(i-1), 4);

        if (i % Nk == 0) {
            rot_word(temp);
            sub_word(temp);
            temp[0] ^= rcon[(i/Nk)-1];
        } else if (Nk > 6 && i % Nk == 4) {
            sub_word(temp);
        }

        for (int j = 0; j < 4; j++) {
            ctx->encRoundKeys[4*i + j] = ctx->encRoundKeys[4*(i-Nk) + j] ^ temp[j];
        }
        i++;
    }

    // Build decryption round keys
    for (int r = 0; r <= rounds; r++) {
        uint8* enc = ctx->encRoundKeys + 16*r;
        uint8* dec = ctx->decRoundKeys + 16*r;
        if (r == 0 || r == rounds) {
            memcpy(dec, enc, 16);
        } else {
            memcpy(dec, enc, 16);
            inv_mix_columns(dec);
        }
    }

    return B_OK;
}

/* ---------------------------------------------------------------- */
/* Block encrypt/decrypt                                             */
/* ---------------------------------------------------------------- */
void soft_aes_encrypt_block(SoftAESContext* ctx, const uint8* in, uint8* out) {
    uint8 state[16];
    memcpy(state, in, 16);

    // Initial round key
    for (int i = 0; i < 16; i++)
        state[i] ^= ctx->encRoundKeys[i];

    // Round intermedi (1 a rounds-1)
    for (int r = 1; r < ctx->rounds; r++) {
        // 1. SubBytes
        for (int i = 0; i < 16; i++) state[i] = sbox[state[i]];
        // 2. ShiftRows (Mancava!)
        shift_rows(state);
        // 3. MixColumns
        mix_columns(state);
        // 4. AddRoundKey
        for (int i = 0; i < 16; i++)
            state[i] ^= ctx->encRoundKeys[16 * r + i];
    }

    // Final round (senza MixColumns)
    for (int i = 0; i < 16; i++) state[i] = sbox[state[i]];
    shift_rows(state);
    for (int i = 0; i < 16; i++)
        state[i] ^= ctx->encRoundKeys[16 * ctx->rounds + i];

    memcpy(out, state, 16);
}

void soft_aes_decrypt_block(SoftAESContext* ctx, const uint8* in, uint8* out) {
    uint8 state[16];
    memcpy(state, in, 16);

    // Round iniziale (AddRoundKey con l'ultima chiave)
    for (int i = 0; i < 16; i++)
        state[i] ^= ctx->encRoundKeys[16 * ctx->rounds + i];

    // Round intermedi a ritroso
    for (int r = ctx->rounds - 1; r > 0; r--) {
        inv_shift_rows(state);
        for (int i = 0; i < 16; i++) state[i] = inv_sbox[state[i]];
        
        // AddRoundKey
        for (int i = 0; i < 16; i++)
            state[i] ^= ctx->encRoundKeys[16 * r + i];
        
        // InvMixColumns
        inv_mix_columns(state);
    }

    // Final round
    inv_shift_rows(state);
    for (int i = 0; i < 16; i++) state[i] = inv_sbox[state[i]];
    for (int i = 0; i < 16; i++)
        state[i] ^= ctx->encRoundKeys[i];

    memcpy(out, state, 16);
}

/* ---------------------------------------------------------------- */
/* Secure zeroing of context                                         */
/* ---------------------------------------------------------------- */
static void
secure_memzero(void* p, size_t s)
{
    if (p == NULL)
        return;
    volatile uint8* cp = (volatile uint8*)p;
    while (s--)
        *cp++ = 0;
}

void
soft_aes_zero(SoftAESContext* ctx)
{
    if (!ctx) 
        return;
    secure_memzero(ctx, sizeof(*ctx));
}
void
soft_aes_gcm_update_internal(SoftAESContext* ctx, const uint8* src, uint8* dst, size_t len)
{
    size_t offset = 0;
    uint8 keystream[16];
    uint8 block[16];

    while (offset < len) {
        // 1. Prepariamo il blocco di keystream per questo round CTR
        // Incrementiamo il contatore (solo gli ultimi 32 bit come da standard GCM)
        ctx->counter[15]++;
        if (ctx->counter[15] == 0) {
            ctx->counter[14]++;
            if (ctx->counter[14] == 0) {
                ctx->counter[13]++;
                if (ctx->counter[13] == 0) ctx->counter[12]++;
            }
        }

        // Cifriamo il contatore per ottenere il keystream
        soft_aes_encrypt_block(ctx, ctx->counter, keystream);

        // 2. Determiniamo quanti byte processare in questo blocco (max 16)
        size_t chunk = (len - offset < 16) ? (len - offset) : 16;

        if (ctx->is_encrypting) {
            // --- ENCRYPTION ---
            for (size_t i = 0; i < chunk; i++) {
                dst[offset + i] = src[offset + i] ^ keystream[i];
            }
            // GHASH usa il ciphertext (dst)
            memset(block, 0, 16);
            memcpy(block, dst + offset, chunk);
            for (int i = 0; i < 16; i++) ctx->tag_acc[i] ^= block[i];
            ghash_multiply(ctx->tag_acc, ctx->h_key);
        } else {
            // --- DECRYPTION ---
            // GHASH usa il ciphertext (src)
            memset(block, 0, 16);
            memcpy(block, src + offset, chunk);
            for (int i = 0; i < 16; i++) ctx->tag_acc[i] ^= block[i];
            ghash_multiply(ctx->tag_acc, ctx->h_key);

            // Poi decifriamo per l'utente
            for (size_t i = 0; i < chunk; i++) {
                dst[offset + i] = src[offset + i] ^ keystream[i];
            }
        }

        offset += chunk;
    }
}
