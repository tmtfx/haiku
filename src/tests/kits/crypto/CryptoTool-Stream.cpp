/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <vector>
#include <algorithm>

#include <File.h>
#include <crypto/BCrypto.h>

#define CHUNK_SIZE (128 * 1024) // 128 KB per chunk

void hex_to_bytes(const char* hex, uint8* bytes, size_t len) {
    for (size_t i = 0; i < len; i++) {
        sscanf(hex + 2 * i, "%02hhx", &bytes[i]);
    }
}

void usage(const char* prog) {
    printf("Utilizzo: %s -e|-d -k <key_hex> [-i <iv_hex>] -p <none|pkcs7|iso|zero> -f <input> -o <output>\n", prog);
    printf("Per esempio: %s -e -k 00112233445566778899aabbccddeeff -i ffeeddccbbaa99887766554433221100 -p pkcs7 -f file.zip -o file.enc\n", prog);
    exit(1);
}

int main(int argc, char** argv) {
    int op = -1;
    char *keyHex = NULL, *ivHex = NULL, *filePath = NULL, *outPath = NULL;
    BCryptoPaddingType pType = B_CRYPTO_PADDING_NONE;
    bool paddingEnabled = false;
    int c;

    while ((c = getopt(argc, argv, "edk:i:p:f:o:")) != -1) {
        switch (c) {
            case 'e': op = B_CRYPTO_ENCRYPT; break;
            case 'd': op = B_CRYPTO_DECRYPT; break;
            case 'k': keyHex = optarg; break;
            case 'i': ivHex = optarg; break;
            case 'f': filePath = optarg; break;
            case 'o': outPath = optarg; break;
            case 'p':
                paddingEnabled = true;
                if (strcmp(optarg, "pkcs7") == 0) pType = B_CRYPTO_PKCS7;
                else if (strcmp(optarg, "iso") == 0) pType = B_CRYPTO_ISO7816;
                else if (strcmp(optarg, "zero") == 0) pType = B_CRYPTO_ZERO_PADDING;
                else if (strcmp(optarg, "none") == 0) paddingEnabled = false;
                break;
        }
    }

    if (op == -1 || !keyHex || !filePath || !outPath) usage(argv[0]);

    BCrypto crypto;
    if (crypto.InitCheck() != B_OK) return 1;
    
    // Configuriamo l'algoritmo e il padding una volta sola
    crypto.SetAlgorithm(B_CRYPTO_AES);
    crypto.SetMode(B_CRYPTO_MODE_CBC);
    crypto.SetPadding(paddingEnabled, pType);

    // Apriamo i file usando le classi Haiku (che ereditano da BDataIO)
    BFile inputFile(filePath, B_READ_ONLY);
    BFile outputFile(outPath, B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);

    if (inputFile.InitCheck() != B_OK || outputFile.InitCheck() != B_OK) {
        fprintf(stderr, "Errore nell'apertura dei file\n");
        return 1;
    }

    uint8 key[32];
    size_t keyLen = strlen(keyHex) / 2;
    if (keyLen != 16 && keyLen != 24 && keyLen != 32) {
        fprintf(stderr, "Errore: Lunghezza chiave non valida (%zu byte). Usare 16, 24 o 32.\n", keyLen);
        return 1;
    }
    hex_to_bytes(keyHex, key, keyLen);
    uint8 iv[16] = {0};
    if (ivHex) hex_to_bytes(ivHex, iv, 16);

    // --- IL CUORE DEL PROGRAMMA ---
    ssize_t result;
    bigtime_t startTime = system_time();
    if (op == B_CRYPTO_ENCRYPT) {
        result = crypto.Encrypt(key, keyLen, iv, 16, &inputFile, &outputFile);
    } else {
        result = crypto.Decrypt(key, keyLen, iv, 16, &inputFile, &outputFile);
    }
    bigtime_t endTime = system_time();
    double durationSeconds = (endTime - startTime) / 1000000.0;

    if (result >= 0) {
    	double megabytes = (double)result / (1024.0 * 1024.0);
        double speed = (durationSeconds > 0) ? (megabytes / durationSeconds) : 0;

        printf("\n--- Risultati Operazione ---\n");
        printf("Dati processati: %zd byte (%.2f MB)\n", result, megabytes);
        printf("Tempo impiegato: %.4f secondi\n", durationSeconds);
        printf("Velocità media:  %.2f MB/s\n", speed);
        printf("----------------------------\n");
        printf("Successo! Processati %zd byte.\n", result);
    } else {
        fprintf(stderr, "Errore durante il processamento: %s\n", strerror(result));
        return 1;
    }

    return 0;
}
