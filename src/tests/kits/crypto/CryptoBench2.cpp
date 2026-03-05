/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/evp.h>
#include <OS.h>

#include <crypto/BCrypto.h>
#include <crypto/BCryptoDefs.h>

#define CHUNK_SIZE (1024 * 1024) // 1MB
#define ITERATIONS 100

// Helper per il calcolo del tempo
double get_time_ms() {
    return system_time() / 1000.0;
}

// Helper per l'allocazione allineata (ottimale per driver/DMA)
uint8* malloc_aligned(size_t size) {
    uint8* ptr;
    if (posix_memalign((void**)&ptr, 64, size) != 0) return NULL;
    return ptr;
}

// Helper per stampare i blocchi esadecimali
void print_block(const char* label, uint8* data, size_t len) {
    printf("%-25s: ", label);
    for (size_t i = 0; i < len && i < 16; i++) {
        printf("%02x ", data[i]);
    }
    printf(len > 16 ? "...\n" : "\n");
}

// --- BENCHMARK 1: TEST STANDARD 1MB ---
void benchmark_simple(uint8* data, uint8* key, uint8* iv) {
    uint8* outOpenSSL = malloc_aligned(CHUNK_SIZE);
    uint8* outHaiku = malloc_aligned(CHUNK_SIZE);
    
    printf("\n[ TEST 1 ] Cifratura singola da 1MB (Standard)\n");

    // OpenSSL
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    double start = get_time_ms();
    for(int i = 0; i < ITERATIONS; i++) {
        EVP_EncryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, key, iv);
        EVP_CIPHER_CTX_set_padding(ctx, 0);
        int outlen;
        EVP_EncryptUpdate(ctx, outOpenSSL, &outlen, data, CHUNK_SIZE);
    }
    double end = get_time_ms();
    printf("OpenSSL:   %7.2f ms\n", (end - start));
    EVP_CIPHER_CTX_free(ctx);

    // Haiku Kit
    BCrypto crypto;
    iovec src = { data, CHUNK_SIZE };
    iovec dst = { outHaiku, CHUNK_SIZE };
    uint8 tempIV[16];
    
    BCryptoUserRequest req;
    memset(&req, 0, sizeof(req));
    req.operation = B_CRYPTO_ENCRYPT;
    req.algorithm = B_CRYPTO_AES;
    req.mode = B_CRYPTO_MODE_CBC;
    req.key = key; req.keyLength = 16;
    req.ivLength = 16;
    req.source = &src; req.destination = &dst; req.vectorCount = 1;

    start = get_time_ms();
    for(int i = 0; i < ITERATIONS; i++) {
        memcpy(tempIV, iv, 16);
        req.iv = tempIV;
        crypto.Process(req);
    }
    end = get_time_ms();
    printf("Haiku Kit: %7.2f ms\n", (end - start));

    if (memcmp(outOpenSSL, outHaiku, CHUNK_SIZE) == 0)
        printf("RISULTATO: [ OK ] I dati corrispondono.\n");
    else
        printf("RISULTATO: [ !! ] ERRORE: Dati differenti!\n");

    free(outOpenSSL);
    free(outHaiku);
}

// --- BENCHMARK 2: TEST MULTI-CHUNK (PROPAGAZIONE IV) ---
void benchmark_multichunk(uint8* data_2mb, uint8* key, uint8* iv_start) {
    uint8* outOpenSSL = malloc_aligned(CHUNK_SIZE * 2);
    uint8* outHaiku = malloc_aligned(CHUNK_SIZE * 2);
    
    printf("\n[ TEST 2 ] Cifratura Multi-chunk da 2MB (Propagazione IV)\n");
    printf("Simulazione di due chiamate Process() consecutive...\n");

    // 1. Riferimento OpenSSL (Cifra 2MB in un colpo solo)
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    EVP_EncryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, key, iv_start);
    EVP_CIPHER_CTX_set_padding(ctx, 0);
    int outlen;
    EVP_EncryptUpdate(ctx, outOpenSSL, &outlen, data_2mb, CHUNK_SIZE * 2);
    EVP_CIPHER_CTX_free(ctx);

    // 2. Test Haiku (Due chiamate da 1MB usando lo stesso buffer IV)
    BCrypto crypto;
    uint8 iv_running[16];
    memcpy(iv_running, iv_start, 16);

    BCryptoUserRequest req;
    memset(&req, 0, sizeof(req));
    req.operation = B_CRYPTO_ENCRYPT;
    req.algorithm = B_CRYPTO_AES;
    req.mode = B_CRYPTO_MODE_CBC;
    req.key = key; req.keyLength = 16;
    req.iv = iv_running; req.ivLength = 16;
    req.vectorCount = 1;

    // Chunk 1
    iovec src1 = { data_2mb, CHUNK_SIZE };
    iovec dst1 = { outHaiku, CHUNK_SIZE };
    req.source = &src1; req.destination = &dst1;
    crypto.Process(req);

    // Chunk 2 (Dovrebbe usare l'IV aggiornato dal driver nella chiamata precedente)
    iovec src2 = { data_2mb + CHUNK_SIZE, CHUNK_SIZE };
    iovec dst2 = { outHaiku + CHUNK_SIZE, CHUNK_SIZE };
    req.source = &src2; req.destination = &dst2;
    crypto.Process(req);

    // 3. Analisi
    bool firstHalf = (memcmp(outOpenSSL, outHaiku, CHUNK_SIZE) == 0);
    bool secondHalf = (memcmp(outOpenSSL + CHUNK_SIZE, outHaiku + CHUNK_SIZE, CHUNK_SIZE) == 0);

    printf("Confronto Primo MB:  %s\n", firstHalf ? "[ OK ]" : "[ ERRORE ]");
    printf("Confronto Secondo MB: %s\n", secondHalf ? "[ OK ]" : "[ ERRORE ]");

    if (firstHalf && !secondHalf) {
        printf("\nANALISI FALLIMENTO:\n");
        printf("Il primo blocco è corretto, ma la catena CBC si è spezzata al secondo MB.\n");
        printf("Il driver non ha riportato l'IV aggiornato nel buffer 'iv_running'.\n");
        
        print_block("IV atteso (Ciphertext 1)", outOpenSSL + CHUNK_SIZE - 16, 16);
        print_block("IV attuale in memoria", iv_running, 16);
    } else if (firstHalf && secondHalf) {
        printf("RISULTATO: [ OK ] La propagazione dell'IV funziona correttamente!\n");
    }

    free(outOpenSSL);
    free(outHaiku);
}

int main() {
    uint8* data_2mb = malloc_aligned(CHUNK_SIZE * 2);
    if (!data_2mb) return 1;

    memset(data_2mb, 0xAA, CHUNK_SIZE * 2);
    uint8 key[16] = {1,2,3,4,5,6,7,8,9,0,1,2,3,4,5,6};
    uint8 iv[16]  = {1,2,3,4,5,6,7,8,9,0,1,2,3,4,5,6};

    printf("--- Haiku Crypto Multi-chunk Benchmark ---\n");
    
    benchmark_simple(data_2mb, key, iv);
    benchmark_multichunk(data_2mb, key, iv);

    free(data_2mb);
    return 0;
}
