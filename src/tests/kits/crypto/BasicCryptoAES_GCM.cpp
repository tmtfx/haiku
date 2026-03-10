#include <stdio.h>
#include <string.h>
#include <crypto/BCrypto.h>

int main() {
    printf("BCrypto AES-GCM Test\n");

    BCrypto crypto;
    
    // Configurazione GCM
    crypto.SetAlgorithm(B_CRYPTO_AES);
    crypto.SetMode(B_CRYPTO_MODE_GCM);
    // GCM non usa padding classico (PKCS7), lo gestisce internamente
    crypto.SetPadding(false, B_CRYPTO_PADDING_NONE);

    if (crypto.InitCheck() != B_OK) {
        printf("Errore: Device /dev/crypto non trovato!\n");
        return 1;
    }
    char engineName[64] = "Sconosciuto";
    crypto.GetEngineName(B_CRYPTO_AES, B_CRYPTO_MODE_GCM,  engineName, sizeof(engineName));
    printf("Motore crittografico: %s\n", engineName);
    // 1. Dati di test
    uint8 key[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 
                     0x09, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16};
    uint8 iv[12]  = {0x99, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00, 0xAA, 0xBB};
    
    const char* plaintext = "Haiku OS - GCM Test Message";
    size_t dataLen = strlen(plaintext);
    
    uint8 ciphertext[64] = {0};
    uint8 tag[16] = {0};
    uint8 decrypted[64] = {0};

    printf("--- TEST GCM ---\n");
    printf("Input: %s\n", plaintext);

    // 2. CIFRATURA (Usa l'overload con outTag)
    // ssize_t Encrypt(uint8* key, size_t keyLen, uint8* iv, size_t ivLen, const void* in, size_t inLen, void* out, void* outTag)
    ssize_t encResult = crypto.Encrypt(key, 16, iv, 12, plaintext, dataLen, ciphertext, tag);

    if (encResult < 0) {
        printf("Errore in Encrypt: %s\n", strerror(encResult));
        return 1;
    }

    printf("Cifratura completata. Tag: ");
    for(int i=0; i<16; i++) printf("%02x", tag[i]);
    printf("\n");

    // 3. DECIFRATURA (Usa l'overload con inTag)
    // ssize_t Decrypt(uint8* key, size_t keyLen, uint8* iv, size_t ivLen, const void* in, size_t inLen, void* out, const void* inTag)
    // Ripristiniamo l'IV (perché il driver lo modifica durante il processo)
    uint8 ivDecrypt[12];
    memcpy(ivDecrypt, iv, 12);

    ssize_t decResult = crypto.Decrypt(key, 16, ivDecrypt, 12, ciphertext, dataLen, decrypted, tag);

    if (decResult < 0) {
        if (decResult == B_BAD_DATA) {
            printf("FALLITO: Tag non valido (Integrità compromessa)!\n");
        } else {
            printf("Errore in Decrypt: %ld\n", decResult);
        }
    } else {
        decrypted[decResult] = '\0'; // Nul-terminate
        printf("Decifratura riuscita: %s\n", decrypted);
        
        if (strcmp(plaintext, (char*)decrypted) == 0) {
            printf("SUCCESS: Il testo corrisponde!\n");
        }
    }

    return 0;
}
