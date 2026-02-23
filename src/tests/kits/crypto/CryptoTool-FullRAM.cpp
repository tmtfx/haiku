/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <crypto/BCrypto.h>

void hex_to_bytes(const char* hex, uint8* bytes, size_t len) {
    for (size_t i = 0; i < len; i++) {
        sscanf(hex + 2 * i, "%02hhx", &bytes[i]);
    }
}

void usage(const char* prog) {
    printf("Utilizzo: %s -e|-d -k <key_hex> [-i <iv_hex>] -p <none|pkcs7|iso|zero> -f <input> -o <output>\n", prog);
    exit(1);
}

int main(int argc, char** argv) {
    int op = -1;
    char *keyHex = NULL, *ivHex = NULL, *filePath = NULL, *outPath = NULL;
    BCryptoPaddingType pType = B_CRYPTO_PADDING_NONE;
    bool paddingEnabled = false;
    int c;

    // Aggiunta 'o:' alla stringa di controllo
    while ((c = getopt(argc, argv, "edk:i:p:f:o:")) != -1) {
        switch (c) {
            case 'e': op = B_CRYPTO_ENCRYPT; break;
            case 'd': op = B_CRYPTO_DECRYPT; break;
            case 'k': keyHex = optarg; break;
            case 'i': ivHex = optarg; break;
            case 'f': filePath = optarg; break;
            case 'o': outPath = optarg; break; // Gestione output
            case 'p':
                paddingEnabled = true;
                if (strcmp(optarg, "pkcs7") == 0) pType = B_CRYPTO_PKCS7;
                else if (strcmp(optarg, "iso") == 0) pType = B_CRYPTO_ISO7816;
                else if (strcmp(optarg, "zero") == 0) pType = B_CRYPTO_ZERO_PADDING;
                break;
        }
    }

    if (op == -1 || !keyHex || !filePath || !outPath) usage(argv[0]);

    BCrypto crypto;
    if (crypto.InitCheck() != B_OK) return 1;
    crypto.SetPadding(paddingEnabled, pType);

    size_t keyLen = strlen(keyHex) / 2;
    uint8 key[32];
    hex_to_bytes(keyHex, key, keyLen);

    uint8 iv[16] = {0};
    if (ivHex) hex_to_bytes(ivHex, iv, 16);

    FILE* f = fopen(filePath, "rb");
    if (!f) { perror("Apertura input"); return 1; }
    fseek(f, 0, SEEK_END);
    size_t inputLen = ftell(f);
    rewind(f);

    size_t bufferSize = crypto.GetOutputSize(inputLen, (BCryptoOperation)op);
    uint8* buffer = (uint8*)malloc(bufferSize);
    fread(buffer, 1, inputLen, f);
    fclose(f);

    ssize_t resultSize = 0;
    if (op == B_CRYPTO_ENCRYPT)
        resultSize = crypto.Encrypt(key, keyLen, iv, 16, buffer, inputLen, buffer, bufferSize);
    else
        resultSize = crypto.Decrypt(key, keyLen, iv, 16, buffer, inputLen, buffer, bufferSize);

    if (resultSize >= 0) {
        FILE* fout = fopen(outPath, "wb");
        if (!fout) { perror("Apertura output"); return 1; }
        fwrite(buffer, 1, resultSize, fout);
        fclose(fout);
        printf("Fatto! File salvato in: %s\n", outPath);
    } else {
        printf("Errore: %zd\n", resultSize);
    }

    free(buffer);
    return 0;
}
