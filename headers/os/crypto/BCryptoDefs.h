/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef _B_CRYPTO_DEFS_H_
#define _B_CRYPTO_DEFS_H_

#include <Drivers.h>
#include <SupportDefs.h>
#include <Errors.h>
#ifndef B_PENDING
#   define B_PENDING B_DEV_PENDING
#endif

#define B_CRYPTO_IOCTL_GET_RANDOM   _IOWR('crpt', 10, BCryptoRandomRequest)
/* refuso
enum {
    B_CRYPTO_IOCTL_SUBMIT = B_DEVICE_OP_CODES_END + 1
};
//#define B_CRYPTO_IOCTL_PROCESS  (B_DEVICE_OP_CODES_END + 1)*/

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
    B_CRYPTO_SHA256     = 0x0011,
    B_CRYPTO_SHA512     = 0x0012,

    // RNG
    B_CRYPTO_RNG        = 0x0020
};

enum BCryptoHwCapability {
    B_CRYPTO_HW_AES_NI        = 1 << 0,  // Set di istruzioni x86 (AES-NI)
    B_CRYPTO_HW_SHA_NI        = 1 << 1,  // Set di istruzioni x86 (SHA-NI)
    B_CRYPTO_HW_VIA_PADLOCK   = 1 << 2,  // Motore VIA ACE
    B_CRYPTO_HW_RNG           = 1 << 3,  // Generatore di numeri casuali (VIA o altro)
    B_CRYPTO_HW_ACCEL_ENGINE  = 1 << 4   // Dispositivo esterno (PCIe/USB)
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
    B_CRYPTO_ALG_ASYNC       = 0x04
};

enum {
    B_CRYPTO_IOCTL_BASE = B_DEVICE_OP_CODES_END + 100,
    
    // Questo comando invia una richiesta e riceve un risultato
    B_CRYPTO_IOCTL_PROCESS = B_CRYPTO_IOCTL_BASE
};

#endif
