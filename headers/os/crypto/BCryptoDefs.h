/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef _B_CRYPTO_DEFS_H_
#define _B_CRYPTO_DEFS_H_

#include <OS.h>
#include <Drivers.h>
#include <SupportDefs.h>
#include <iovec.h>
#include <Errors.h>
#ifndef B_PENDING
#   define B_PENDING B_DEV_PENDING
#endif

#define B_CRYPTO_DEVICE_NAME "crypto/v1"

#define B_CRYPTO_HASH_MAX_SIZE 64
/*
struct crypto_device_info {
    char vendor_name[32];      // es: "Intel", "VIA", "Hifn"
    uint32 algos_supported;    // Bitmask di BCryptoAlgorithmID (AES, SHA...)
    uint32 hw_type;           // Bitmask di BCryptoHwCapability
    uint32 max_throughput;     // Per decidere quale dispositivo usare se ne hai due
    void* device_cookie;
};*/
#define B_CRYPTO_MAX_ALGO_BLOCKS 4 

struct crypto_device_info {
    char vendor_name[32];
    uint32 algos_supported[B_CRYPTO_MAX_ALGO_BLOCKS]; // 4 * 32 = 128 possibili algoritmi
    uint32 hw_type;
    uint32 max_throughput;
    void* device_cookie;
};

#define B_CRYPTO_ALGO_ID(block, bit) (((block) << 5) | (bit))
#define B_GET_BLOCK(id) ((id) >> 5)
#define B_GET_BIT(id)   (1 << ((id) & 31))

enum BCryptoAlgorithmID {
	B_CRYPTO_ALGO_NONE      = 0,
    B_CRYPTO_AES            = B_CRYPTO_ALGO_ID(0, 1), // 1
    B_CRYPTO_CHACHA20       = B_CRYPTO_ALGO_ID(0, 2), // 2

    // Hash (Blocco 1)
    B_CRYPTO_SHA1           = B_CRYPTO_ALGO_ID(1, 0), // 32, da 0 a 31 poi cambiare blocco
    B_CRYPTO_SHA224         = B_CRYPTO_ALGO_ID(1, 1),
    B_CRYPTO_SHA256         = B_CRYPTO_ALGO_ID(1, 2),
    B_CRYPTO_SHA384         = B_CRYPTO_ALGO_ID(1, 3),
    B_CRYPTO_SHA512         = B_CRYPTO_ALGO_ID(1, 4),
    B_CRYPTO_SHA3_256       = B_CRYPTO_ALGO_ID(1, 5),
    B_CRYPTO_SHA3_512       = B_CRYPTO_ALGO_ID(1, 6),
    B_CRYPTO_MD5            = B_CRYPTO_ALGO_ID(1, 7),
    B_CRYPTO_BLAKE2B        = B_CRYPTO_ALGO_ID(1, 8),
    B_CRYPTO_BLAKE2S        = B_CRYPTO_ALGO_ID(1, 9),// Ottimizzato 32-bit
    B_CRYPTO_BLAKE3         = B_CRYPTO_ALGO_ID(1, 10),

    // RNG e altro (Blocco 2)
    B_CRYPTO_RNG            = B_CRYPTO_ALGO_ID(2, 0)  // 64
};

enum BCryptoHwCapability {
    B_CRYPTO_HW_AES_NI        = 1 << 0,  // Set di istruzioni x86 (AES-NI)
    B_CRYPTO_HW_SHA_NI        = 1 << 1,  // Set di istruzioni x86 (SHA-NI)
    B_CRYPTO_HW_VIA_PADLOCK   = 1 << 2,  // Motore VIA ACE
    B_CRYPTO_HW_RNG           = 1 << 3,  // RNG generico
    B_CRYPTO_HW_ACCEL_ENGINE  = 1 << 4,  // Dispositivo esterno (PCIe/USB)
    B_CRYPTO_HW_RDRAND        = 1 << 5,  // RNG Intel/AMD
    B_CRYPTO_HW_PADLOCK_RNG   = 1 << 6,   // RNG di VIA PADLOCK
    B_CRYPTO_HW_SSE41         = 1 << 7,
    B_CRYPTO_HW_AVX2          = 1 << 8,
    B_CRYPTO_HW_AVX512        = 1 << 9
};

enum BCryptoMode {
    B_CRYPTO_MODE_ANY = 0,  // let the driver decide internally what to do
    B_CRYPTO_MODE_ECB = 1 << 0, // 1
    B_CRYPTO_MODE_CBC = 1 << 1, // 2
    B_CRYPTO_MODE_CTR = 1 << 2, // 4
    B_CRYPTO_MODE_GCM = 1 << 3  // 8
};

enum BCryptoOperation {
    B_CRYPTO_ENCRYPT = 1,
    B_CRYPTO_DECRYPT,
    B_CRYPTO_DIGEST,
    B_CRYPTO_RANDOM
};

enum {
    B_CRYPTO_ALG_HW_ACCEL    = 0x01,
    B_CRYPTO_ALG_SOFTWARE    = 0x02,
    B_CRYPTO_ALG_ASYNC       = 0x04,
    B_CRYPTO_ALG_KERNEL_SPACE= 0x08
};

typedef struct {
    uint32              cookie;      // In/Out per l'iterazione
    BCryptoAlgorithmID  id;          // Out
    BCryptoMode         mode;        // Out
    uint32              flags;       // Out (B_CRYPTO_ALG_SOFTWARE, etc.)
    char                vendor[32];  // Out
} BCryptoAlgorithmInfo;

typedef struct {
    BCryptoOperation    operation;
    BCryptoAlgorithmID  algorithm;
    BCryptoMode         mode;
    uint32              flags;

    void* key;
    size_t              keyLength;
    void* iv;
    size_t              ivLength;

    const iovec*        source;
    iovec*              destination;
    size_t              vectorCount;

    // Sostituiamo il puntatore a funzione con un semaforo
    sem_id              completionSem; 
    status_t            result;
} BCryptoUserRequest;

struct BCryptoRandomRequest {
    void* buffer;
    size_t length;
    status_t result;
};

enum {
    B_CRYPTO_IOCTL_BASE = B_DEVICE_OP_CODES_END + 100,
    B_CRYPTO_IOCTL_PROCESS    = B_CRYPTO_IOCTL_BASE + 1,
    B_CRYPTO_IOCTL_GET_RANDOM = B_CRYPTO_IOCTL_BASE + 2,
    B_CRYPTO_IOCTL_HASH_INIT   = B_CRYPTO_IOCTL_BASE + 3,
    B_CRYPTO_IOCTL_HASH_UPDATE = B_CRYPTO_IOCTL_BASE + 4,
    B_CRYPTO_IOCTL_HASH_FINAL  = B_CRYPTO_IOCTL_BASE + 5,
    B_CRYPTO_IOCTL_GET_NEXT_ALGO = B_CRYPTO_IOCTL_BASE + 6,
    B_CRYPTO_IOCTL_CHECK_ALGO    = B_CRYPTO_IOCTL_BASE + 7,
    B_CRYPTO_IOCTL_STREAM_INIT = B_CRYPTO_IOCTL_BASE + 8,
    B_CRYPTO_IOCTL_STREAM_UPDATE = B_CRYPTO_IOCTL_BASE + 9,
    B_CRYPTO_IOCTL_STREAM_FINAL = B_CRYPTO_IOCTL_BASE + 10
};

typedef enum {
    B_CRYPTO_PADDING_NONE = 0,
    B_CRYPTO_PKCS7        = 1,
    B_CRYPTO_ISO7816      = 2,
    B_CRYPTO_ZERO_PADDING = 3
} BCryptoPaddingType;

static inline size_t
decode_hash_length(BCryptoAlgorithmID algo)
{
    switch (algo) {
        case B_CRYPTO_MD5:       return 16;
        case B_CRYPTO_SHA1:      return 20;
        case B_CRYPTO_SHA224:    return 28;
        case B_CRYPTO_SHA256:    return 32;
        case B_CRYPTO_SHA3_256:  return 32;
        case B_CRYPTO_SHA384:    return 48;
        case B_CRYPTO_SHA512:    return 64;
        case B_CRYPTO_SHA3_512:  return 64;
        case B_CRYPTO_BLAKE2B:   return 64;
        case B_CRYPTO_BLAKE2S:   return 32;
        case B_CRYPTO_BLAKE3:    return 32;
        default:                 return 0;
    }
}

#endif
