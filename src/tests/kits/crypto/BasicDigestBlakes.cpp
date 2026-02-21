#include <stdio.h>
#include <string.h>
#include <crypto/BCrypto.h>

void print_hex(const char* label, uint8* hash, size_t len) {
    printf("%-15s: ", label);
    for (size_t i = 0; i < len; i++) printf("%02x", hash[i]);
    printf("\n");
}

bool test_algorithm(BCrypto& crypto, BCryptoAlgorithmID id, const char* name, 
                    const char* input, size_t inputLen, const char* expected, size_t hashLen) {
    uint s;
    switch (id) {
    	case B_CRYPTO_BLAKE2B: {
            s = 64; // Massimo per BLAKE2b
            break;
    	}
    	case B_CRYPTO_BLAKE2S: {
    		s =32;
            break;
    	}
    	case B_CRYPTO_BLAKE3: {
    		s = 32; // Massimo per BLAKE2b
            break;
    	}
    	default:
    	    s = 32;
    }
    uint8 digest[s];
    memset(digest, 0, sizeof(digest));

    printf("\n--- Test %s ---\n", name);
    status_t status = crypto.Digest(id, (uint8*)input, inputLen, digest);

    if (status != B_OK) {
        printf("[\033[31mERROR\033[0m] Driver return status: %x\n", (int)status);
        return false;
    }

    print_hex("Ottenuto", digest, hashLen);
    
    // Verifica
    char resultStr[129];
    for (size_t i = 0; i < hashLen; i++) sprintf(resultStr + (i * 2), "%02x", digest[i]);

    if (strcmp(resultStr, expected) == 0) {
        printf("[\033[32mOK\033[0m] Corrispondenza perfetta!\n");
        return true;
    } else {
        printf("[\033[31mFAIL\033[0m] Atteso: %s\n", expected);
        return false;
    }
}

int main() {
    BCrypto crypto;
    const char* testData = "abc";
    size_t dataLen = 3;

    printf("=== BCrypto BLAKE Suite Diagnostic Test ===\n");
    printf("Input: '%s'\n", testData);

    // 1. BLAKE2b - 64 byte (512 bit)
    test_algorithm(crypto, B_CRYPTO_BLAKE2B, "BLAKE2b", testData, dataLen,
        "ba80a53f981c4d0d6a2797b69f12f6e94c212f14685ac4b74b12bb6fdbffa2d17d87c5392aab792dc252d5de4533cc9518d38aa8dbf1925ab92386edd4009923", 64);

    // 2. BLAKE2s - 32 byte (256 bit)
    test_algorithm(crypto, B_CRYPTO_BLAKE2S, "BLAKE2s", testData, dataLen,
        "508c3565d87114713e311091583592c7d5ea1f458599ef65c538795b44061722", 32);

    // 3. BLAKE3 - 32 byte (256 bit)
    test_algorithm(crypto, B_CRYPTO_BLAKE3, "BLAKE3", testData, dataLen,
        "6437b3ac38465133ffb65059135d1b6ae33bb62a74ad276edad20d4419673150", 32);

    return 0;
}
