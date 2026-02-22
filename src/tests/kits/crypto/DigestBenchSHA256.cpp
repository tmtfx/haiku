/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <OS.h>
#include <File.h>
#include <DataIO.h>
#include <crypto/BCrypto.h>

void print_hash(const char* label, uint8* hash, size_t len, double ms, double mbps) {
    printf("%-15s: ", label);
    for (size_t i = 0; i < len; i++) printf("%02x", hash[i]);
    if (ms > 0) {
        printf(" | %7.2f ms | %7.2f MB/s", ms, mbps);
    }
    printf("\n");
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <file>\n", argv[0]);
        return 1;
    }

    const char* filename = argv[1];
    BFile file(filename, B_READ_ONLY);
    if (file.InitCheck() != B_OK) {
        fprintf(stderr, "Errore: impossibile aprire %s\n", filename);
        return 1;
    }

    off_t size;
    file.GetSize(&size);
    if (size <= 0) {
        fprintf(stderr, "Errore: il file è vuoto o invalido.\n");
        return 1;
    }
    double sizeMB = (double)size / (1024.0 * 1024.0);

    BCrypto crypto;
    uint8 digest[64];
    memset(digest, 0, sizeof(digest));
    
    printf("DEBUG: Indirizzo buffer digest (userspace): %p\n", digest);
    size_t hashLen = crypto.GetHashLength(B_CRYPTO_SHA256);
    bigtime_t start, end;

    printf("Benchmark SHA-256 su: %s (%.2f MB)\n", filename, sizeMB);
    printf("-------------------------------------------------------------------------------\n");
/*
    // --- TEST 1: STREAMING (BDataIO) ---
    printf("Esecuzione Test Streaming...\n");
    start = system_time();
    status_t status = crypto.Digest(B_CRYPTO_SHA256, &file, digest);
    end = system_time();

    if (status == B_OK) {
        double duration = (end - start) / 1000.0; // ms
        double mbps = sizeMB / ((end - start) / 1000000.0);
        print_hash("Streaming IO", digest, hashLen, duration, mbps);
    } else {
        fprintf(stderr, "Errore Streaming: %s\n", strerror(status));
    }*/

    // --- TEST 2: BUFFERIZZATO (void*) ---
    // Prepariamo la memoria
    uint8* fullBuffer = (uint8*)malloc(size);
    if (fullBuffer == NULL) {
        fprintf(stderr, "Errore: impossibile allocare %.2f MB\n", sizeMB);
    } else {
    	printf("DEBUG: Indirizzo fullBuffer (userspace): %p\n", fullBuffer);
        file.ReadAt(0, fullBuffer, size);
        
        printf("Esecuzione Test Full RAM...\n");
        start = system_time();
        status_t status = crypto.Digest(B_CRYPTO_SHA256, fullBuffer, size, digest);
        end = system_time();

        if (status == B_OK) {
            double duration = (end - start) / 1000.0; // ms
            double mbps = sizeMB / ((end - start) / 1000000.0);
            print_hash("Full RAM", digest, hashLen, duration, mbps);
        } else {
            fprintf(stderr, "Errore Buffer: %s\n", strerror(status));
        }
        free(fullBuffer);
    }

    printf("-------------------------------------------------------------------------------\n");
    return 0;
}
