/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <iomanip>
#include <OS.h>
#include <crypto/BCrypto.h>

using namespace std;

// Caricamento binario Key/IV
void loadFromFile(const char* path, uint8* dest, size_t len) {
    ifstream f(path, ios::binary);
    if (!f.is_open()) { exit(1); }
    f.read((char*)dest, len);
}

int main(int argc, char* argv[]) {
    if (argc < 6) {
        cout << "Usage: " << argv[0] << " <enc/dec> <key_file> <iv_file> <input_file> <output_file>" << endl;
        return 1;
    }

    string op = argv[1];
    uint8 key[16], iv[16];
    loadFromFile(argv[2], key, 16);
    loadFromFile(argv[3], iv, 16);

    ifstream is(argv[4], ios::binary | ios::ate); // Apre alla fine per sapere la dimensione
    streamsize fileSize = is.tellg();
    is.seekg(0, ios::beg);

    ofstream os(argv[5], ios::binary);
    
    BCrypto bc;
    BCryptoUserRequest req;
    req.algorithm = B_CRYPTO_AES;
    req.mode = B_CRYPTO_MODE_CBC;
    req.operation = (op == "enc") ? B_CRYPTO_ENCRYPT : B_CRYPTO_DECRYPT;
    req.key = key; req.keyLength = 16;
    req.iv = iv;   req.ivLength = 16;
    req.vectorCount = 1;

    const size_t bufSize = 64 * 1024; 
    void* rawBuf;
    posix_memalign(&rawBuf, 16, bufSize);
    uint8* buffer = (uint8*)rawBuf;

    cout << (op == "enc" ? "Encrypting (PKCS7)..." : "Decrypting (PKCS7)...") << endl;
    bigtime_t startTime = system_time();

    if (op == "enc") {
        // --- CIFRATURA CON PADDING ---
        while (is.read((char*)buffer, bufSize) || is.gcount() > 0) {
            size_t bytesRead = is.gcount();
            bool isLastBlock = is.eof() || (is.peek() == EOF);

            if (isLastBlock) {
                // Calcola padding
                size_t paddingNeeded = 16 - (bytesRead % 16);
                for (size_t i = 0; i < paddingNeeded; i++) {
                    buffer[bytesRead + i] = (uint8)paddingNeeded;
                }
                bytesRead += paddingNeeded;
            }

            iovec iov = {buffer, bytesRead};
            req.source = &iov; req.destination = &iov;
            bc.Process(req);
            os.write((char*)buffer, bytesRead);
            
            // Caso speciale: se il file finisce esattamente su 16 byte, 
            // isLastBlock era vero ma dobbiamo assicurarci di non aver già scritto
            // il blocco di padding se bytesRead era già multiplo di 16 prima del padding
            // (La logica sopra lo gestisce correttamente).
        }
    } else {
        // --- DECIFRATURA CON RIMOZIONE PADDING ---
        streamsize processed = 0;
        while (is.read((char*)buffer, bufSize) || is.gcount() > 0) {
            size_t bytesRead = is.gcount();
            processed += bytesRead;

            iovec iov = {buffer, bytesRead};
            req.source = &iov; req.destination = &iov;
            bc.Process(req);

            if (processed >= fileSize) {
                // Ultimo blocco decifrato: leggi l'ultimo byte per il padding
                uint8 paddingVal = buffer[bytesRead - 1];
                if (paddingVal > 0 && paddingVal <= 16) {
                    bytesRead -= paddingVal;
                }
            }
            os.write((char*)buffer, bytesRead);
        }
    }

    bigtime_t endTime = system_time();
    free(rawBuf);
    cout << "Completed! Time: " << (endTime - startTime) / 1000000.0 << "s" << endl;
    return 0;
}
