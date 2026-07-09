/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <OS.h>
#include <crypto/BCrypto.h>
#include <crypto/BCryptoDefs.h>

void print_hex(const char* title, uint8* buf, size_t len) {
    printf("%s: ", title);
    for (size_t i = 0; i < len; i++) {
        printf("%02x ", buf[i]);
    }
    printf("\n");
}

int main() {
    printf("--- BCrypto Framework Test: Random Generator ---\n\n");

    // --- TEST 1: Call via IOCTL ---
    printf("[Test 1] Directly opening /dev/crypto/v1...\n");
    int fd = open("/dev/crypto/v1", O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "ERROR: could not open /dev/crypto/v1: %s\n", strerror(errno));
    } else {
        printf("Device correctly opened (fd: %d).\n", fd);
        printf("DEBUG: Using B_CRYPTO_IOCTL_GET_RANDOM = %d\n", B_CRYPTO_IOCTL_GET_RANDOM);

        BCryptoRandomRequest req;
        uint8 buffer[16];
        memset(buffer, 0, sizeof(buffer));

        req.buffer = buffer;
        req.length = sizeof(buffer);
        req.result = B_ERROR;

        if (ioctl(fd, B_CRYPTO_IOCTL_GET_RANDOM, &req) < 0) {
            fprintf(stderr, "ERROR: ioctl failed: %s\n", strerror(errno));
        } else {
            if (req.result == B_OK) {
                print_hex("Data generated (IOCTL)", buffer, sizeof(buffer));
            } else {
                printf("Driver returned error: 0x%lx\n", req.result);
            }
        }
        close(fd);
    }

    printf("\n------------------------------------------------\n\n");

    // --- TEST 2: Call via BCrypto Kit ---
    printf("[Test 2] Using BCrypto class (API Kit)...\n");
    BCrypto crypto;
    status_t initSt = crypto.InitCheck();
    
    if (initSt != B_OK) {
        fprintf(stderr, "ERROR: BCrypto::InitCheck() failed: %s\n", strerror(initSt));
    } else {
        uint8 kitBuffer[32];
        memset(kitBuffer, 0, sizeof(kitBuffer));
        
        status_t st = crypto.GetRandomBytes(kitBuffer, sizeof(kitBuffer));
        if (st == B_OK) {
            print_hex("Data generated (KIT)  ", kitBuffer, sizeof(kitBuffer));
            printf("[ OK ] Generation through Kit completed.\n");
        } else {
            fprintf(stderr, "ERROR: GetRandomBytes() failed: 0x%lx (%s)\n", st, strerror(st));
        }
    }

    return 0;
}
