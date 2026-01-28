/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <openssl/evp.h>
#include <OS.h>
#include <crypto/BCryptoDefs.h>

#define BUFFER_SIZE (1024 * 1024) // 1MB for throughput test
#define ITERATIONS 100

double get_time_ms() {
    return system_time() / 1000.0;
}

void benchmark_openssl(uint8* data, size_t len, uint8* key, uint8* iv) {
    uint8* out = (uint8*)malloc(len);
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    
    double start = get_time_ms();
    for(int i=0; i < ITERATIONS; i++) {
        EVP_EncryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, key, iv);
        int outlen;
        EVP_EncryptUpdate(ctx, out, &outlen, data, len);
        EVP_EncryptFinal_ex(ctx, out + outlen, &outlen);
    }
    double end = get_time_ms();
    
    printf("OpenSSL: %.2f ms (Average for %d MB)\n", (end - start), ITERATIONS);
    free(out);
    EVP_CIPHER_CTX_free(ctx);
}

void benchmark_haiku_driver(int fd, uint8* data, size_t len, uint8* key, uint8* iv) {
    uint8* out = (uint8*)malloc(len);
    iovec src = { data, len };
    iovec dst = { out, len };
    BCryptoUserRequest req;
    
    memset(&req, 0, sizeof(req));
    req.operation = B_CRYPTO_ENCRYPT;
    req.algorithm = B_CRYPTO_AES;
    req.mode = B_CRYPTO_MODE_CBC;
    req.key = key; req.keyLength = 16;
    req.iv = iv; req.ivLength = 16;
    req.source = &src; req.destination = &dst;
    req.vectorCount = 1;

    double start = get_time_ms();
    for(int i=0; i < ITERATIONS; i++) {
        ioctl(fd, B_CRYPTO_IOCTL_PROCESS, &req);
    }
    double end = get_time_ms();

    printf("Haiku Driver: %.2f ms (Average for %d MB)\n", (end - start), ITERATIONS);
    free(out);
}

int main() {
    uint8* data = (uint8*)malloc(BUFFER_SIZE);
    memset(data, 0xAA, BUFFER_SIZE);
    uint8 key[16] = {1,2,3,4,5,6,7,8,9,0,1,2,3,4,5,6};
    uint8 iv[16] = {0};

    int fd = open("/dev/crypto/v1", O_RDWR);
    if (fd < 0) return 1;

    printf("--- Benchmark Start (Buffer: 1MB, Iterations: %d) ---\n", ITERATIONS);
    benchmark_openssl(data, BUFFER_SIZE, key, iv);
    benchmark_haiku_driver(fd, data, (size_t)BUFFER_SIZE, key, iv);

    close(fd);
    free(data);
    return 0;
}
