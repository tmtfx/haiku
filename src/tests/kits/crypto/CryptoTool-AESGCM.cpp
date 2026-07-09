/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include <stdio.h>
#include <string.h>
#include <File.h>
#include <DataIO.h>
#include <crypto/BCrypto.h>

void print_hex(const char* label, const uint8* data, size_t len) {
    printf("%s: ", label);
    for (size_t i = 0; i < len; i++) printf("%02x", data[i]);
    printf("\n");
}

int main(int argc, char** argv) {
    printf("BCrypto AES-GCM Streaming Test\n");
    printf("------------------------------\n");

    // 1. Setup Chiave e IV (256-bit key, 96-bit IV)
    uint8 key[32] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                      0x09, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
                      0x17, 0x18, 0x19, 0x20, 0x21, 0x22, 0x23, 0x24,
                      0x25, 0x26, 0x27, 0x28, 0x29, 0x30, 0x31, 0x32 };
    uint8 iv[12]  = { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 };
    uint8 tag[16];

    BCrypto crypto;
    crypto.SetAlgorithm(B_CRYPTO_AES);
    crypto.SetMode(B_CRYPTO_MODE_GCM);
    crypto.SetPadding(false, B_CRYPTO_PADDING_NONE);
    if (crypto.InitCheck() != B_OK) {
        printf("Errore: Device /dev/crypto non trovato!\n");
        return 1;
    }
    char engineName[64] = "Sconosciuto";
    crypto.GetEngineName(B_CRYPTO_AES, B_CRYPTO_MODE_GCM, engineName, sizeof(engineName));
    printf("Motore crittografico: %s\n", engineName);

    // 2. CREAZIONE FILE DI TEST
    const char* sourcePath = "test_data.txt";
    const char* encryptedPath = "test_data.enc";
    const char* decryptedPath = "test_data.dec";

    {
        BFile sourceFile(sourcePath, B_CREATE_FILE | B_ERASE_FILE | B_WRITE_ONLY);
        const char* content = "Haiku OS Streaming Crypto Test - Questo è un messaggio molto lungo per testare i chunk da 64KB...";
        sourceFile.Write(content, strlen(content));
    }

    // 3. CIFRATURA (Streaming)
    printf("Inizio cifratura...\n");
    BFile fileIn(sourcePath, B_READ_ONLY);
    BFile fileOutEnc(encryptedPath, B_CREATE_FILE | B_ERASE_FILE | B_WRITE_ONLY);

    ssize_t encResult = crypto.Encrypt(key, 32, iv, 12, &fileIn, &fileOutEnc, tag);
    if (encResult < 0) {
        fprintf(stderr, "Errore cifratura: %s\n", strerror(encResult));
        return 1;
    }
    print_hex("Tag generato", tag, 16);

    // 4. DECIFRATURA (Streaming)
    printf("Inizio decifratura...\n");
    BFile fileEncIn(encryptedPath, B_READ_ONLY);
    BFile fileOutDec(decryptedPath, B_CREATE_FILE | B_ERASE_FILE | B_WRITE_ONLY);
    
    uint8 tagVerifica[16];
    memcpy(tagVerifica, tag, 16);

    ssize_t decResult = crypto.Decrypt(key, 32, iv, 12, &fileEncIn, &fileOutDec, tagVerifica);
    
    if (decResult == B_BAD_DATA) {
        fprintf(stderr, "ERRORE: Tag non valido! I dati sono stati manomessi.\n");
        return 1;
    } else if (decResult < 0) {
        fprintf(stderr, "Errore decifratura: %s\n", strerror(decResult));
        return 1;
    }

    printf("Decifratura completata con successo.\n");

    // 5. VERIFICA FINALE
    BFile original(sourcePath, B_READ_ONLY);
    BFile result(decryptedPath, B_READ_ONLY);
    
    off_t sizeOrig, sizeRes;
    original.GetSize(&sizeOrig);
    result.GetSize(&sizeRes);

    if (sizeOrig == sizeRes) {
        printf("SUCCESS: Le dimensioni corrispondono (%lld byte)!\n", sizeOrig);
    } else {
        printf("FAILURE: Dimensioni diverse!\n");
    }

    return 0;
}
