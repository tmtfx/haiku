#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <iomanip>
#include <OS.h>
#include <crypto/BCrypto.h>

using namespace std;

void loadFromFile(const char* path, uint8* dest, size_t len) {
    ifstream f(path, ios::binary);
    if (!f.is_open()) {
        cerr << "Error: unable opening " << path << endl;
        exit(1); 
    }
    f.read((char*)dest, len);
    if (f.gcount() != (streamsize)len) {
        cerr << "Error: file " << path << " too small!" << endl;
        exit(1);
    }
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

    ifstream is(argv[4], ios::binary);
    ofstream os(argv[5], ios::binary);
    if (!is || !os) { cerr << "Error opening input/output file." << endl; return 1; }

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
    if (posix_memalign(&rawBuf, 16, bufSize) != 0) return B_NO_MEMORY;
    uint8* buffer = (uint8*)rawBuf;

    cout << (op == "enc" ? "Encrypting..." : "Decrypting...") << endl;

    bigtime_t startTime = system_time();

    while (is.read((char*)buffer, bufSize) || is.gcount() > 0) {
        size_t bytesRead = is.gcount();
        if (bytesRead % 16 != 0) {
            size_t extra = 16 - (bytesRead % 16);
            memset(buffer + bytesRead, 0, extra);
            bytesRead += extra;
        }

        iovec iov = {buffer, bytesRead};
        req.source = &iov;
        req.destination = &iov;

        if (bc.Process(req) != B_OK) {
            cerr << "Driver error while processing!" << endl;
            free(rawBuf);
            return 1;
        }
        os.write((char*)buffer, bytesRead);
    }

    bigtime_t endTime = system_time();

    free(rawBuf);
    
    double durationSec = (endTime - startTime) / 1000000.0;
    cout << "Completed successfully!" << endl;
    cout << "Time elapsed: " << fixed << setprecision(4) << durationSec << " seconds" << endl;
    
    return 0;
}
