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
    crypto.SetPadding(paddingEnabled, pType);

    BFile file(filePath, B_READ_ONLY);
    if (file.InitCheck() != B_OK) { perror("Errore input"); return 1; }
    off_t fileSize;
    file.GetSize(&fileSize);

    uint8 key[32];
    hex_to_bytes(keyHex, key, strlen(keyHex) / 2);
    uint8 iv[16] = {0};
    if (ivHex) hex_to_bytes(ivHex, iv, 16);

    std::vector<iovec> vecs;
    off_t remaining = fileSize;

    // --- CARICAMENTO VETTORIALE ---
    while (remaining > 0) {
        size_t toRead = std::min((off_t)CHUNK_SIZE, remaining);
        size_t allocSize = toRead;
        
        bool isLast = (remaining == (off_t)toRead);
        
        // Se stiamo cifrando e siamo all'ultimo blocco, allochiamo spazio extra per il padding
        if (op == B_CRYPTO_ENCRYPT && isLast && paddingEnabled) {
            allocSize = crypto.GetOutputSize(toRead, B_CRYPTO_ENCRYPT);
        }

        uint8* buffer = (uint8*)malloc(allocSize);
        if (!buffer) return 1;

        file.Read(buffer, toRead);
        
        // Applichiamo il padding nell'ultimo buffer se necessario
        if (op == B_CRYPTO_ENCRYPT && isLast && paddingEnabled) {
            // Usiamo una funzione helper o logica interna
            // Nota: BCrypto::_ApplyPadding è privata, ma qui simuliamo la logica
            size_t padLen = allocSize - toRead;
            if (pType == B_CRYPTO_PKCS7) {
                memset(buffer + toRead, (uint8)padLen, padLen);
            } else if (pType == B_CRYPTO_ISO7816) {
                buffer[toRead] = 0x80;
                if (padLen > 1) memset(buffer + toRead + 1, 0, padLen - 1);
            }
        }

        iovec iov = { buffer, allocSize };
        vecs.push_back(iov);
        remaining -= toRead;
    }

    // --- PROCESSAMENTO ---
    BCryptoUserRequest req;
    memset(&req, 0, sizeof(req));
    req.operation = (BCryptoOperation)op;
    req.algorithm = B_CRYPTO_AES; 
    req.mode      = B_CRYPTO_MODE_CBC;
    req.key = key; req.keyLength = 32;
    req.iv = iv;   req.ivLength = 16;
    req.source = vecs.data();
    req.destination = vecs.data();
    req.vectorCount = vecs.size();

    status_t status = crypto.Process(req);

    // --- SALVATAGGIO E RIMOZIONE PADDING ---
    if (status == B_OK) {
        BFile outFile(outPath, B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
        for (size_t i = 0; i < vecs.size(); i++) {
            size_t toWrite = vecs[i].iov_len;
            
            // Se decifriamo, l'ultimo blocco va pulito dal padding
            if (op == B_CRYPTO_DECRYPT && i == vecs.size() - 1 && paddingEnabled) {
                // Simuliamo _RemovePadding
                uint8* lastBuf = (uint8*)vecs[i].iov_base;
                if (pType == B_CRYPTO_PKCS7) {
                    uint8 pVal = lastBuf[toWrite - 1];
                    if (pVal <= 16) toWrite -= pVal;
                } // ... altre logiche di strip ...
            }
            outFile.Write(vecs[i].iov_base, toWrite);
        }
        printf("Successo: %s\n", outPath);
    }

    for (auto& iov : vecs) free(iov.iov_base);
    return 0;
}
