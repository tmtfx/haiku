#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <File.h>
#include <crypto/BCrypto.h>

void print_hex(const char* label, const uint8* data, int len) {
    printf("%s: ", label);
    for (int i = 0; i < len; i++) printf("%02x", data[i]);
    printf("\n");
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        printf("Uso: %s <file_cifrato> <tag_hex>\n", argv[0]);
        printf("Esempio: %s file.pdf.enc ed8a783b3502ecf9f58330aa4daceaf3\n", argv[0]);
        return 1;
    }

    const char* inputPath = argv[1];
    const char* tagHex = argv[2];

    // 1. Setup BCrypto
    BCrypto crypto;
    crypto.SetAlgorithm(B_CRYPTO_AES);
    crypto.SetMode(B_CRYPTO_MODE_GCM);
    crypto.SetPadding(false, B_CRYPTO_PADDING_NONE);

    if (crypto.InitCheck() != B_OK) return 1;

    // 2. Converti il tag esadecimale in byte
    uint8 providedTag[16];
    for (int i = 0; i < 16; i++) {
        sscanf(tagHex + 2*i, "%02hhx", &providedTag[i]);
    }

    // 3. Carica il file cifrato
    BFile file(inputPath, B_READ_ONLY);
    off_t fileSize;
    file.GetSize(&fileSize);
    uint8* inputData = (uint8*)malloc(fileSize);
    uint8* decryptedData = (uint8*)malloc(fileSize);
    file.Read(inputData, fileSize);

    // 4. Parametri (devono essere uguali a quelli della cifratura)
    uint8 key[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 
                     0x09, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16};
    uint8 iv[12]  = {0x99, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00, 0xaa, 0xbb};

    printf("--- BCrypto GCM Decrypt & Verify ---\n");
    
    // 5. Chiamata a Decrypt
    // In GCM, Decrypt ricalcola il tag e lo confronta con providedTag
    ssize_t result = crypto.Decrypt(key, 16, iv, 12, inputData, fileSize, decryptedData, providedTag);

    if (result == B_BAD_DATA) {
        printf("\n❌ ERRORE: Integrità compromessa! Il tag non corrisponde.\n");
    } else if (result < 0) {
        printf("\n❌ Errore generico: %ld\n", result);
    } else {
        printf("\n✅ SUCCESSO: Integrità verificata e file decifrato.\n");
        
        // Salva il file decifrato per il confronto finale
        char outPath[1024];
        snprintf(outPath, sizeof(outPath), "%s.dec", inputPath);
        BFile outFile(outPath, B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
        outFile.Write(decryptedData, fileSize);
        printf("File decifrato salvato in: %s\n", outPath);
    }

    free(inputData); free(decryptedData);
    return 0;
}
