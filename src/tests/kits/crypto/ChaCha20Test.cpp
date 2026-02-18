/*
 * Copyright 2018, Your Name <your@email.address>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include <crypto/BCrypto.h>
#include <crypto/BCryptoDefs.h>
#include <stdio.h>
#include <string.h>

void print_hex(const char* label, uint8* data, size_t len) {
    printf("%s: ", label);
    for (size_t i = 0; i < len; i++)
        printf("%02x ", data[i]);
    printf("\n");
}

int main() {
	if (!BCrypto::IsAlgorithmSupported(B_CRYPTO_CHACHA20,0)) {
	   printf("ChaCha20 non è disponibile su questo sistema.\n");
 	   return 1;
	}
    // 1. Configurazione dati (RFC 7539 Test Vector)
    uint8 key[32];
    for (int i = 0; i < 32; i++) key[i] = i; // 00 01 02...

    uint8 iv[16] = {0};
    //iv[0] = 1; // Contatore iniziale (i primi 4 byte nel nostro wrapper)
    iv[0] = 0x01; 
    iv[1] = 0x00;
    iv[2] = 0x00;
    iv[3] = 0x00;
    iv[4 + 7] = 0x4a;
    //iv[7] = 0x4a; // Parte del Nonce (offset +4 nel wrapper, quindi indice 7)

    const char* plaintext = "Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, sunscreen would be it.";
    size_t len = strlen(plaintext);
    uint8 ciphertext[128] = {0};

    // 2. Uso dell'API BCrypto
    BCrypto crypto;
    crypto.SetAlgorithm(B_CRYPTO_CHACHA20);
    /*if (status != B_OK) {
        printf("Errore: ChaCha20 non supportato dal kernel!\n");
        return 1;
    }*/

    // ChaCha20 non ha modalità o padding, quindi impostiamo default
    crypto.SetMode(B_CRYPTO_MODE_ANY);
    crypto.SetPadding(false);

    // 3. Esecuzione
    status_t status = crypto.Encrypt(key, 32, iv, 16, plaintext, len, ciphertext, len);

    //if (status == B_OK) {
    if (status > 0) {
        printf("Cifratura completata con successo!\n");
        print_hex("Output", ciphertext, 16);
        
        // Verifica con i primi due byte attesi (0x6e, 0x2e)
        if (ciphertext[0] == 0x6e && ciphertext[1] == 0x2e) {
            printf("RISULTATO: CONFORME ALLA RFC 7539! \n");
        } else {
            printf("RISULTATO: ERRATO. Qualcosa non va nel wrapper o nel driver.\n");
        }
    } else {
        printf("Errore durante la cifratura: %s\n", strerror(status));
    }

    return 0;
}
