#include <iostream>
#include <iomanip>
#include <fstream>
#include <vector>
#include <cstring>
#include <openssl/evp.h>

int main() {
    // 1. Configurazione identica (AES-256-GCM)
    unsigned char key[32] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                              0x09, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
                              0x17, 0x18, 0x19, 0x20, 0x21, 0x22, 0x23, 0x24,
                              0x25, 0x26, 0x27, 0x28, 0x29, 0x30, 0x31, 0x32 };
    
    unsigned char iv[12] = { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 };

    // 2. Lettura del file originale
    std::ifstream fileIn("test_data.txt", std::ios::binary | std::ios::ate);
    if (!fileIn.is_open()) {
        std::cerr << "Errore: test_data.txt non trovato!" << std::endl;
        return 1;
    }

    std::streamsize size = fileIn.tellg();
    fileIn.seekg(0, std::ios::beg);
    std::vector<unsigned char> plaintext(size);
    fileIn.read((char*)plaintext.data(), size);

    // 3. Setup OpenSSL
    std::vector<unsigned char> ciphertext(size);
    unsigned char tag[16];
    int len, ciphertext_len;

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL);
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, NULL);
    EVP_EncryptInit_ex(ctx, NULL, NULL, key, iv);

    // 4. Cifratura
    EVP_EncryptUpdate(ctx, ciphertext.data(), &len, plaintext.data(), plaintext.size());
    ciphertext_len = len;
    EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &len);
    ciphertext_len += len;
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag);

    // 5. SCRITTURA FILE BINARIO
    std::ofstream fileOut("test_openssl.enc", std::ios::binary);
    fileOut.write((char*)ciphertext.data(), ciphertext_len);
    fileOut.close();

    std::cout << "File 'test_openssl.enc' generato (" << ciphertext_len << " bytes)." << std::endl;
    
    // Stampiamo il tag per riferimento
    std::cout << "OpenSSL Reference Tag: ";
    for(int i=0; i<16; i++) printf("%02x", tag[i]);
    printf("\n");

    EVP_CIPHER_CTX_free(ctx);
    return 0;
}
