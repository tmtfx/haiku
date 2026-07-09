/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <OS.h>
#include <crypto/BCryptoDefs.h>

void print_hex(const char* label, const uint8* data, size_t len) {
    printf("%s: ", label);
    for (size_t i = 0; i < len; i++) printf("%02x ", data[i]);
    printf("\n");
}

int main() {
    int fd = open("/dev/crypto/v1", O_RDWR);
    if (fd < 0) {
        perror("Failed opening the device");
        return 1;
    }

    uint8 key[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 
                     0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
    uint8 iv[16]  = {0};
    char plaintext[] = "Haiku Crypto OK"; // 16 bytes
    uint8 ciphertext[16];
    char decrypted[17];
    memset(decrypted, 0, 17);

    iovec src, dst;
    BCryptoUserRequest req;
    memset(&req, 0, sizeof(req));

    // --- Phase 1: Encryption ---
    src.iov_base = plaintext;
    src.iov_len = 16;
    dst.iov_base = ciphertext;
    dst.iov_len = 16;

    req.operation = B_CRYPTO_ENCRYPT;
    req.algorithm = B_CRYPTO_AES;
    req.mode      = B_CRYPTO_MODE_CBC;
    req.key = key; req.keyLength = 16;
    req.iv = iv;   req.ivLength = 16;
    req.source = &src; req.destination = &dst;
    req.vectorCount = 1; req.completionSem = -1;

    ioctl(fd, B_CRYPTO_IOCTL_PROCESS, &req);
    print_hex("Encrypted  ", ciphertext, 16);

    // --- Phase 2: Decryption ---
    // Resetting l'IV (fundamental in CBC mode!)
    memset(iv, 0, 16); 
    
    src.iov_base = ciphertext;
    dst.iov_base = decrypted;
    req.operation = B_CRYPTO_DECRYPT;

    ioctl(fd, B_CRYPTO_IOCTL_PROCESS, &req);
    printf("Decrypted: %s\n", decrypted);

    if (strcmp(plaintext, decrypted) == 0) {
        printf("\n✅ TEST PASSED: The data is intact!\n");
    } else {
        printf("\n❌ TEST FAILED: The decrypted data does not match.\n");
    }

    close(fd);
    return 0;
}
