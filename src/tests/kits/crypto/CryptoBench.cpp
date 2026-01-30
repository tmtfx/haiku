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

#define BUFFER_SIZE (1024 * 1024) // 1MB
#define ITERATIONS 100

double get_time_ms() {
    return system_time() / 1000.0;
}

uint8* malloc_aligned(size_t size) {
    uint8* ptr;
    if (posix_memalign((void**)&ptr, 64, size) != 0) return NULL;
    return ptr;
}

void print_block(const char* label, uint8* data, size_t len) {
    printf("%-15s: ", label);
    for (size_t i = 0; i < len && i < 16; i++) {
        printf("%02x ", data[i]);
    }
    printf(len > 16 ? "...\n" : "\n");
}

void benchmark_openssl(uint8* data, size_t len, uint8* key, uint8* iv, uint8* output) {
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    
    double start = get_time_ms();
    for(int i=0; i < ITERATIONS; i++) {
        EVP_EncryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, key, iv);
        EVP_CIPHER_CTX_set_padding(ctx, 0);
        
        int outlen;
        EVP_EncryptUpdate(ctx, output, &outlen, data, len);
        int finalLen;
        EVP_EncryptFinal_ex(ctx, output + outlen, &finalLen);
    }
    double end = get_time_ms();
    
    printf("OpenSSL:      %7.2f ms (Total for %d MB)\n", (end - start), ITERATIONS);
    EVP_CIPHER_CTX_free(ctx);
}

void benchmark_haiku_kit(uint8* data, size_t len, uint8* key, uint8* iv, uint8* output) {
    BCrypto crypto;
    if (crypto.InitCheck() != B_OK) {
        printf("Error: Driver not available!\n");
        return;
    }

    iovec src = { data, len };
    iovec dst = { output, len };
    uint8 tempIV[16];
    
    BCryptoUserRequest req;
    memset(&req, 0, sizeof(req));
    req.operation = B_CRYPTO_ENCRYPT;
    req.algorithm = B_CRYPTO_AES;
    req.mode = B_CRYPTO_MODE_CBC;
    req.key = key; 
    req.keyLength = 16;
    req.ivLength = 16;
    req.source = &src; 
    req.destination = &dst;
    req.vectorCount = 1;

    double start = get_time_ms();
    for(int i=0; i < ITERATIONS; i++) {
        memcpy(tempIV, iv, 16);
        req.iv = tempIV;
        
        crypto.Process(req);
    }
    double end = get_time_ms();

    printf("Haiku Kit:    %7.2f ms (Total for %d MB)\n", (end - start), ITERATIONS);
}

int main() {
    uint8* data = malloc_aligned(BUFFER_SIZE);
    uint8* outOpenSSL = malloc_aligned(BUFFER_SIZE);
    uint8* outDriver = malloc_aligned(BUFFER_SIZE);

    if (!data || !outOpenSSL || !outDriver) {
        printf("Memory error.\n");
        return 1;
    }
    
    memset(data, 0xAA, BUFFER_SIZE);
    uint8 key[16] = {1,2,3,4,5,6,7,8,9,0,1,2,3,4,5,6};
    uint8 iv[16]  = {1,2,3,4,5,6,7,8,9,0,1,2,3,4,5,6};
    
    printf("--- Haiku Cryptografic Benchmark ---\n");
    printf("Config: AES-128-CBC, Buffer: 1MB, Iterations: %d\n\n", ITERATIONS);
    
    print_block("Original data", data, 16);
    print_block("Key", key, 16);
    print_block("IV", iv, 16);
    printf("------------------------------------\n");

    benchmark_openssl(data, BUFFER_SIZE, key, iv, outOpenSSL);
    benchmark_haiku_kit(data, BUFFER_SIZE, key, iv, outDriver);

    printf("------------------------------------\n");

    print_block("OpenSSL result", outOpenSSL, 16);
    print_block("Driver result", outDriver, 16);

    if (memcmp(outOpenSSL, outDriver, BUFFER_SIZE) == 0) {
        printf("\n[ OK ] CHECK PASSED: The data corresponds to 100%%.\n");
    } else {
        printf("\n[ !! ] ERROR: The data differs.\n");
    }

    free(data);
    free(outOpenSSL);
    free(outDriver);
    return 0;
}
