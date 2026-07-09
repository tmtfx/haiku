/*
 * Copyright 2018, Your Name <your@email.address>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <File.h>
#include <crypto/BCrypto.h>

double get_time_ms() {
    return system_time() / 1000.0;
}

void print_hex(const char* label, const uint8* data, int len) {
    printf("%s: ", label);
    for (int i = 0; i < len; i++) printf("%02x", data[i]);
    printf("\n");
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Uso: %s <file_da_cifrare>\n", argv[0]);
        return 1;
    }

    const char* inputPath = argv[1];

	double start_from_ctx = get_time_ms();
    // 1. Setup BCrypto
    BCrypto crypto;
    crypto.SetAlgorithm(B_CRYPTO_AES);
    crypto.SetMode(B_CRYPTO_MODE_GCM);
    crypto.SetPadding(false, B_CRYPTO_PADDING_NONE);

    if (crypto.InitCheck() != B_OK) {
        fprintf(stderr, "Errore: Sottosistema BCrypto non trovato!\n");
        return 1;
    }

    // 2. Lettura del file in memoria
    BFile file(inputPath, B_READ_ONLY);
    if (file.InitCheck() != B_OK) {
        fprintf(stderr, "Errore: Impossibile aprire il file %s\n", inputPath);
        return 1;
    }

    off_t fileSize;
    file.GetSize(&fileSize);
    
    uint8* inputData = (uint8*)malloc(fileSize);
    uint8* outputData = (uint8*)malloc(fileSize);
    if (!inputData || !outputData) {
        fprintf(stderr, "Errore: Memoria insufficiente per %lld byte\n", fileSize);
        return 1;
    }

    file.Read(inputData, fileSize);

    // 3. Parametri fissi per il test (Standard NIST)
    uint8 key[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 
                     0x09, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16};
    uint8 iv[12]  = {0x99, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00, 0xaa, 0xbb};
    uint8 tag[16] = {0};

    printf("--- BCrypto GCM One-Shot Test ---\n");
    char engineName[64] = "Sconosciuto";
    crypto.GetEngineName(B_CRYPTO_AES, B_CRYPTO_MODE_GCM, engineName, sizeof(engineName));//B_CRYPTO_MODE_CBC,
    printf("Motore crittografico: %s\n", engineName);
    printf("File: %s (%lld byte)\n", inputPath, fileSize);

    // 4. Chiamata alla tua funzione Encrypt
    double start_from_command = get_time_ms();
    ssize_t result = crypto.Encrypt(key, 16, iv, 12, inputData, fileSize, outputData, tag);
    double end_from_ctx = get_time_ms();

    if (result < 0) {
        fprintf(stderr, "Errore durante la cifratura: %ld\n", result);
    } else {
        printf("Cifratura completata!\n");
        print_hex("Tag generato", tag, 16);

        // Opzionale: salva il file cifrato per verifica esterna
        char outPath[1024];
        snprintf(outPath, sizeof(outPath), "%s.enc", inputPath);
        BFile outFile(outPath, B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
        outFile.Write(outputData, fileSize);
        printf("File cifrato salvato in: %s\n", outPath);
        printf("Time from context:    %7.2f ms\n", (end_from_ctx - start_from_ctx));
        printf("Time for command:    %7.2f ms\n", (end_from_ctx - start_from_command));
    }

    // Cleanup
    free(inputData);
    free(outputData);

    return 0;
}
