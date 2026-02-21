#include <stdio.h>
#include <string.h>
#include <crypto/BCrypto.h>

void print_hex(const char* label, uint8* hash, size_t len) {
    printf("%-15s: ", label);
    for (size_t i = 0; i < len; i++) printf("%02x", hash[i]);
    printf("\n");
}

int main() {
    BCrypto crypto;
    uint8 digest[32];
    const char* testData = "abc";
    const char* expected = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";

    printf("=== BCrypto SHA-NI Diagnostic Test ===\n");
    printf("Input: '%s' (3 bytes)\n", testData);

    memset(digest, 0, sizeof(digest));
    
    // Chiamata al tuo driver
    status_t status = crypto.Digest(B_CRYPTO_SHA256, (uint8*)testData, 3, digest);

    if (status < 0) {
        fprintf(stderr, "Errore: il driver ha restituito %x\n", (int)status);
        return 1;
    }

    print_hex("Ottenuto", digest, 32);
    printf("%-15s: %s\n", "Atteso", expected);

    // Verifica veloce
    char resultStr[65];
    for (int i = 0; i < 32; i++) sprintf(resultStr + (i * 2), "%02x", digest[i]);

    if (strcmp(resultStr, expected) == 0) {
        printf("\n[\033[32mOK\033[0m] Corrispondenza perfetta!\n");
    } else {
        printf("\n[\033[31mFAIL\033[0m] I valori differiscono.\n");
        printf("Suggerimento: Controlla se i primi 4 byte dell'ottenuto sono\n");
        printf("una versione rimescolata di 'ba7816bf'.\n");
    }

    return 0;
}
