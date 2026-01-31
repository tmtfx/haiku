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

struct crypto_device_info {
    char vendor_name[32];      // es: "Intel", "VIA", "Hifn"
    uint32 algos_supported;    // Bitmask di BCryptoAlgorithmID (AES, SHA...)
    uint32 hw_type;           // Bitmask di BCryptoHwCapability
    uint32 max_throughput;     // Per decidere quale dispositivo usare se ne hai due
    void* device_cookie;
};

enum BCryptoAlgorithmID {
    // Cifrari simmetrici
    B_CRYPTO_AES        = 0x0001,

    // Hash
    B_CRYPTO_SHA1       = 0x0010,
    B_CRYPTO_SHA224     = 0x0011,
    B_CRYPTO_SHA256     = 0x0012,
    B_CRYPTO_SHA384     = 0x0013,
    B_CRYPTO_SHA512     = 0x0014,
    B_CRYPTO_SHA3_256   = 0x0015,
    B_CRYPTO_SHA3_512   = 0x0016,
    B_CRYPTO_MD5        = 0x0017,
    B_CRYPTO_BLAKE2B    = 0x0018,

    // RNG
    B_CRYPTO_RNG        = 0x0020
};

enum BCryptoHwCapability {
    B_CRYPTO_HW_AES_NI        = 1 << 0,  // Set di istruzioni x86 (AES-NI)
    B_CRYPTO_HW_SHA_NI        = 1 << 1,  // Set di istruzioni x86 (SHA-NI)
    B_CRYPTO_HW_VIA_PADLOCK   = 1 << 2,  // Motore VIA ACE
    B_CRYPTO_HW_RNG           = 1 << 3,  // RNG generico
    B_CRYPTO_HW_ACCEL_ENGINE  = 1 << 4,  // Dispositivo esterno (PCIe/USB)
    B_CRYPTO_HW_RDRAND        = 1 << 5,  // RNG Intel/AMD
    B_CRYPTO_HW_PADLOCK_RNG   = 1 << 6   // RNG di VIA PADLOCK
    
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
    B_CRYPTO_DIGEST
};

enum {
    B_CRYPTO_ALG_HW_ACCEL    = 0x01,
    B_CRYPTO_ALG_SOFTWARE    = 0x02,
    B_CRYPTO_ALG_ASYNC       = 0x04,
    B_CRYPTO_ALG_KERNEL_SPACE= 0x08
};



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
    B_CRYPTO_IOCTL_HASH_FINAL  = B_CRYPTO_IOCTL_BASE + 5
    // Questo comando invia una richiesta e riceve un risultato
};

typedef enum {
    B_CRYPTO_PADDING_NONE = 0,
    B_CRYPTO_PKCS7        = 1,
    B_CRYPTO_ISO7816      = 2,
    B_CRYPTO_ZERO_PADDING = 3
} BCryptoPaddingType;

#endif
