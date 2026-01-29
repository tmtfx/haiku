#include <iostream>
#include <iomanip>
#include <vector>
#include <cstring>
#include <OS.h>
#include <crypto/BCrypto.h>
#include <openssl/evp.h>

using namespace std;
void print_hex(const char* label, const uint8* data, size_t len) {
    cout << left << setw(15) << setfill(' ') << label << ": "; 
    for (size_t i = 0; i < len; i++)
        cout << hex << setfill('0') << setw(2) << (int)data[i] << " ";
    cout << dec << setfill(' ') << endl;
}

void run_test(uint32 mode, const char* mode_name) {
	const int TEST_ITERATIONS = 10000;
	int l;
	
    uint8 *key, *iv, *data, *outDRV;
    uint8 outOSSL[16] = {0};
    
    // Allocate aligned memory
    posix_memalign((void**)&key, 16, 16);
    posix_memalign((void**)&iv, 16, 16);
    posix_memalign((void**)&data, 16, 16);
    posix_memalign((void**)&outDRV, 16, 16);
    
    uint8 tempKey[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    uint8 tempIv[16]  = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    
    memcpy(key, tempKey, 16);
    memcpy(iv, tempIv, 16);
    memset(data, 0xAA, 16);
    memset(outDRV, 0, 16);

    // --- OPENSSL ---
    
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    const EVP_CIPHER* cipher = (mode == B_CRYPTO_MODE_ECB) ? EVP_aes_128_ecb() : EVP_aes_128_cbc();
    bigtime_t startOSSL = system_time();
    for(int i = 0; i < TEST_ITERATIONS; i++) {
    	memcpy(iv, tempIv, 16);
        // Init è necessario per resettare l'IV se facciamo test singoli da 16 byte
        EVP_CipherInit_ex(ctx, cipher, NULL, key, iv, 1);
        EVP_CIPHER_CTX_set_padding(ctx, 0); 
        EVP_CipherUpdate(ctx, outOSSL, &l, data, 16);
        EVP_CipherFinal_ex(ctx, outOSSL + l, &l);
    }
    bigtime_t endOSSL = system_time();
    EVP_CIPHER_CTX_free(ctx);
    

    // --- DRIVER ---
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
    
    //cout << "BEFORE - outDRV addr: " << (void*)outDRV << endl; 
    bigtime_t startDRV = system_time();
    for(int i=0; i<TEST_ITERATIONS; i++) {
    	memcpy(iv, tempIv, 16);
        bc.Process(req);
    }
    bigtime_t endDRV = system_time();
    //cout << "AFTER  - outDRV addr: " << (void*)outDRV << endl; 


    cout << "\n--- TEST " << mode_name << " ---" << endl;
    print_hex("OSSL", outOSSL, 16);
    print_hex("DRV", outDRV, 16);
    
    cout << "Average Time OSSL : " << (double)(endOSSL - startOSSL) / TEST_ITERATIONS << " us" << endl;
    cout << "Average Time DRIVER : " << (double)(endDRV - startDRV) / TEST_ITERATIONS << " us" << endl;
    if (memcmp(outOSSL, outDRV, 16) == 0) cout << "[ OK ]" << endl;
    else cout << "[ FAIL ]" << endl;
    
}

int main() {
    run_test(B_CRYPTO_MODE_ECB, "ECB");
    run_test(B_CRYPTO_MODE_CBC, "CBC");
    return 0;
}
