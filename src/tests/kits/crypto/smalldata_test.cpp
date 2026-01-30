#include <iostream>
#include <iomanip>
#include <vector>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <OS.h>
#include <crypto/BCrypto.h>
#include <crypto/BCryptoDefs.h>
#include <openssl/evp.h>

using namespace std;

// Funzione helper per stampare i dati in esadecimale
void print_hex(const char* label, const uint8* data, size_t len) {
    cout << left << setw(18) << setfill(' ') << label << ": "; 
    for (size_t i = 0; i < len; i++)
        cout << hex << setfill('0') << setw(2) << (int)data[i] << " ";
    cout << dec << setfill(' ') << endl;
}

void run_test(uint32 mode, const char* mode_name) {
    const int TEST_ITERATIONS = 10000;
    int l;
    
    uint8 *key, *iv, *data, *outDRV, *outRAW;
    uint8 outOSSL[16] = {0};
    
    // Allocazione memoria allineata per Zero-Copy
    posix_memalign((void**)&key, 16, 16);
    posix_memalign((void**)&iv, 16, 16);
    posix_memalign((void**)&data, 16, 16);
    posix_memalign((void**)&outDRV, 16, 16);
    posix_memalign((void**)&outRAW, 16, 16);
    
    uint8 tempKey[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    uint8 tempIv[16]  = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    
    memcpy(key, tempKey, 16);
    memcpy(iv, tempIv, 16);
    memset(data, 0xAA, 16);
    memset(outDRV, 0, 16);
    memset(outRAW, 0, 16);

    // --- 1. TEST OPENSSL (User Space) ---
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    const EVP_CIPHER* cipher = (mode == B_CRYPTO_MODE_ECB) ? EVP_aes_128_ecb() : EVP_aes_128_cbc();
    
    bigtime_t startOSSL = system_time();
    for(int i = 0; i < TEST_ITERATIONS; i++) {
        memcpy(iv, tempIv, 16);
        EVP_CipherInit_ex(ctx, cipher, NULL, key, iv, 1);
        EVP_CIPHER_CTX_set_padding(ctx, 0); 
        EVP_CipherUpdate(ctx, outOSSL, &l, data, 16);
        EVP_CipherFinal_ex(ctx, outOSSL + l, &l);
    }
    bigtime_t endOSSL = system_time();
    EVP_CIPHER_CTX_free(ctx);

    // --- 2. TEST BCrypto API (La nostra classe C++) ---
    BCrypto bc;
    BCryptoUserRequest req;
    req.algorithm = B_CRYPTO_AES;
    req.mode = (BCryptoMode)mode;
    req.operation = B_CRYPTO_ENCRYPT;
    req.key = key;
    req.keyLength = 16;
    req.iv = iv;
    req.ivLength = 16;
    iovec s = {data, 16}, d = {outDRV, 16};
    req.source = &s;
    req.destination = &d;
    req.vectorCount = 1;
    req.completionSem = -1;
    
    bigtime_t startDRV = system_time();
    for(int i = 0; i < TEST_ITERATIONS; i++) {
        memcpy(iv, tempIv, 16);
        bc.Process(req);
    }
    bigtime_t endDRV = system_time();

    // --- 3. TEST RAW IOCTL (Chiamata di sistema nuda) ---
    int fd = open("/dev/crypto/v1", O_RDWR);
    bigtime_t startRAW = 0, endRAW = 0;
    
    if (fd >= 0) {
        // Prepariamo una richiesta pulita per la ioctl
        BCryptoUserRequest rawReq = req;
        iovec rawS = {data, 16}, rawD = {outRAW, 16};
        rawReq.source = &rawS;
        rawReq.destination = &rawD;

        startRAW = system_time();
        for(int i = 0; i < TEST_ITERATIONS; i++) {
            memcpy(iv, tempIv, 16);
            ioctl(fd, B_CRYPTO_IOCTL_PROCESS, &rawReq);
        }
        endRAW = system_time();
        close(fd);
    }

    // --- RISULTATI ---
    cout << "\n--- TEST PERFORMANCE " << mode_name << " ---" << endl;
    print_hex("OpenSSL", outOSSL, 16);
    print_hex("BCrypto API", outDRV, 16);
    print_hex("Raw IOCTL", outRAW, 16);
    
    cout << fixed << setprecision(4);
    cout << "Avg Time OpenSSL    : " << (double)(endOSSL - startOSSL) / TEST_ITERATIONS << " us" << endl;
    cout << "Avg Time BCrypto API: " << (double)(endDRV - startDRV) / TEST_ITERATIONS << " us" << endl;
    if (fd >= 0)
        cout << "Avg Time Raw IOCTL  : " << (double)(endRAW - startRAW) / TEST_ITERATIONS << " us" << endl;

    // Verifica integrità
    bool success = (memcmp(outOSSL, outDRV, 16) == 0);
    if (fd >= 0) success &= (memcmp(outOSSL, outRAW, 16) == 0);

    cout << (success ? "[ OK ] The data match" : "[ FAIL ] Encryption error!") << endl;
    
    // Clean up
    free(key); free(iv); free(data); free(outDRV); free(outRAW);
}

int main() {
    cout << "Unified Performance Test..." << endl;
    run_test(B_CRYPTO_MODE_ECB, "ECB");
    run_test(B_CRYPTO_MODE_CBC, "CBC");
    return 0;
}
