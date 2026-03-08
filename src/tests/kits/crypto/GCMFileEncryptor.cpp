/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include <stdio.h>
#include <string.h>
#include <File.h>
#include <Entry.h>
#include <crypto/BCrypto.h>

void print_hex(const char* label, uint8* data, size_t len) {
    printf("%s: ", label);
    for(size_t i=0; i < len; i++) printf("%02x", data[i]);
    printf("\n");
}

int main(int argc, char* argv[]) {
    printf("Haiku BCrypto: File Streaming Test (GCM)\n");
    printf("------------------------------------------\n");

    // 1. Setup Driver
    BCrypto crypto;
    crypto.SetAlgorithm(B_CRYPTO_AES);
    crypto.SetMode(B_CRYPTO_MODE_GCM);

    if (crypto.InitCheck() != B_OK) {
        fprintf(stderr, "Errore: Sottosistema BCrypto non disponibile.\n");
        return 1;
    }

    // 2. Chiavi e IV (In un'app reale verrebbero da una password/KDF)
    uint8 key[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 
                     0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};
    uint8 iv[12]  = {0xAF, 0xBE, 0xAD, 0xDE, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    uint8 tag[16] = {0};

    const char* sourcePath = "test_data.txt";
    const char* encryptedPath = "test_data.enc";
    const char* decryptedPath = "test_data_restored.txt";

    // Creiamo un file di prova
    BFile testFile(sourcePath, B_CREATE_FILE | B_ERASE_FILE | B_WRITE_ONLY);
    const char* msg = "Questo messaggio e' salvato su disco e processato via BFile!\n"
                      "Il driver BCrypto gestisce lo streaming in blocchi da 64KB.\n";
    testFile.Write(msg, strlen(msg));
    testFile.Unset();

    // --- FASE 1: CIFRATURA ---
    printf("=> Cifratura di '%s'...\n", sourcePath);
    BFile srcFile(sourcePath, B_READ_ONLY);
    BFile dstFile(encryptedPath, B_CREATE_FILE | B_ERASE_FILE | B_WRITE_ONLY);

    if (srcFile.InitCheck() != B_OK || dstFile.InitCheck() != B_OK) {
        fprintf(stderr, "Errore nell'apertura dei file per la cifratura.\n");
        return 1;
    }

    ssize_t encBytes = crypto.Encrypt(key, 16, iv, 12, &srcFile, &dstFile, tag);
    if (encBytes < 0) {
        fprintf(stderr, "Errore durante Encrypt: %s\n", strerror(encBytes));
        return 1;
    }
    print_hex("Tag Generato", tag, 16);

    srcFile.Unset();
    dstFile.Unset();

    // --- FASE 2: DECIFRATURA ---
    printf("=> Decifratura di '%s'...\n", encryptedPath);
    BFile encFile(encryptedPath, B_READ_ONLY);
    BFile restoredFile(decryptedPath, B_CREATE_FILE | B_ERASE_FILE | B_WRITE_ONLY);

    // Reset IV (importante se il driver lo sporca o se riusiamo lo stesso oggetto)
    uint8 ivDec[12]; memcpy(ivDec, iv, 12);

    ssize_t decBytes = crypto.Decrypt(key, 16, ivDec, 12, &encFile, &restoredFile, tag);
    
    if (decBytes < 0) {
        if (decBytes == B_BAD_DATA) printf("ERRORE: Integrità fallita (Tag errato)!\n");
        else printf("Errore durante Decrypt: %ld\n", decBytes);
        return 1;
    }

    printf("=> SUCCESS: %zd byte decifrati correttamente.\n", decBytes);
    printf("Controlla il file '%s' per il risultato.\n", decryptedPath);

    return 0;
}
