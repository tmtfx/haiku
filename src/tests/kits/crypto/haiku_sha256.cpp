/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include <stdio.h>
#include <string.h>
#include <File.h>
#include <DataIO.h>
#include <crypto/BCrypto.h> // La tua API
#include <crypto/BCryptoDefs.h> // per B_CRYPTO_HASH_MAX_SIZE

class RawPointerIO : public BDataIO {
    int fFd;
public:
    RawPointerIO(int fd) : fFd(fd) {}
    virtual ssize_t Read(void* b, size_t s) { return read(fFd, b, s); }
    virtual ssize_t Write(const void* b, size_t s) { return -1; }
};

void print_hash(const char* label, uint8* hash, size_t len) {
    printf("%s: ", label);
    for (size_t i = 0; i < len; i++) {
        printf("%02x", hash[i]);
    }
    printf("\n");
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <file_da_calcolare>\n", argv[0]);
        return 1;
    }
    /* raw reading from pipe
    int fd = open(argv[1], O_RDONLY); 
    if (fd < 0) return 1;

    RawPointerIO source(fd); // Questo non cerca metadati, apre e basta!
    BCrypto crypto;
    uint8 digest[32];

    // Adesso vedrai la stampa istantaneamente!
    status_t status = crypto.Digest(B_CRYPTO_SHA256, &source, digest);
    
    close(fd);
    return 0;
    */
    // 1. Apriamo il file usando l'API nativa di Haiku
    BFile file(argv[1], B_READ_ONLY);
    if (file.InitCheck() != B_OK) {
        fprintf(stderr, "Errore: Impossibile aprire il file '%s'\n", argv[1]);
        return 1;
    }

    // 2. Istanziamo la tua libreria crittografica
    BCrypto crypto;
    uint8 digest[B_CRYPTO_HASH_MAX_SIZE]; // Spazio per SHA-256

    printf("Calcolo SHA-256 in corso per: %s...\n", argv[1]);

    // 3. Chiamata alla nuova API streaming che accetta BDataIO
    // Nota: BFile eredita da BDataIO, quindi funziona perfettamente!
    status_t status = crypto.Digest(B_CRYPTO_SHA256, &file, digest);

    if (status == B_OK) {
        print_hash("SHA-256", digest, decode_hash_length(B_CRYPTO_SHA256));
    } else {
        fprintf(stderr, "Errore durante il calcolo: %s\n", strerror(status));
    }

    return (status == B_OK) ? 0 : 1;
}
