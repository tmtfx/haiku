#include <iostream>
#include <fstream>
#include <vector>
#include <iomanip>
#include <openssl/evp.h>
#include <openssl/err.h>
#include <stdio.h>
#include <OS.h>

void handle_errors() {
    ERR_print_errors_fp(stderr);
    exit(1);
}
double get_time_ms() {
    return system_time() / 1000.0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Uso: " << argv[1] << " <file_da_cifrare>" << std::endl;
        return 1;
    }

    const char* input_path = argv[1];
    std::string output_path = std::string(input_path) + ".enc";

    // 1. Parametri fissi (gli stessi del tuo driver e di Node.js)
    unsigned char key[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                           0x09, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16};
    unsigned char iv[]  = {0x99, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00, 0xAA, 0xBB};

    // 2. Apri i file
    std::ifstream in_file(input_path, std::ios::binary);
    std::ofstream out_file(output_path, std::ios::binary);
    if (!in_file || !out_file) {
        std::cerr << "Errore nell'apertura dei file." << std::endl;
        return 1;
    }

	double start_from_ctx = get_time_ms();
    // 3. Inizializza OpenSSL
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) handle_errors();

    if (1 != EVP_EncryptInit_ex(ctx, EVP_aes_128_gcm(), NULL, NULL, NULL))
        handle_errors();

    if (1 != EVP_EncryptInit_ex(ctx, NULL, NULL, key, iv))
        handle_errors();

    // 4. Ciclo di lettura e cifratura
    const size_t BUF_SIZE = 64 * 1024; // Buffer da 64KB
    std::vector<unsigned char> in_buf(BUF_SIZE);
    std::vector<unsigned char> out_buf(BUF_SIZE + EVP_MAX_BLOCK_LENGTH);

    int out_len;
    while (in_file.read(reinterpret_cast<char*>(in_buf.data()), BUF_SIZE) || in_file.gcount() > 0) {
        if (1 != EVP_EncryptUpdate(ctx, out_buf.data(), &out_len, in_buf.data(), in_file.gcount()))
            handle_errors();
        out_file.write(reinterpret_cast<char*>(out_buf.data()), out_len);
    }

    // 5. Finalizzazione
    if (1 != EVP_EncryptFinal_ex(ctx, out_buf.data(), &out_len))
        handle_errors();
    out_file.write(reinterpret_cast<char*>(out_buf.data()), out_len);

    // 6. Estrazione del Tag
    unsigned char tag[16];
    if (1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag))
        handle_errors();
        
    double end_from_ctx = get_time_ms();

    // 7. Risultati
    std::cout << "Cifratura completata." << std::endl;
    std::cout << "File cifrato: " << output_path << std::endl;
    std::cout << "Tag: ";
    for (int i = 0; i < 16; i++)
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)tag[i];
    std::cout << std::dec << std::endl;
    printf("Time from context:    %7.2f ms\n", (end_from_ctx - start_from_ctx));

    EVP_CIPHER_CTX_free(ctx);
    return 0;
}
