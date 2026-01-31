/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include "BCrypto.h"
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include "BCryptoDefs.h"
#include <cstdio>
#include <new>

#include <vector>
#include <algorithm>
#define MAX_KERNEL_VECTORS 32
#define COALESCE_THRESHOLD (128 * 1024) // 128 KB


BCrypto::BCrypto() : fFd(-1),
      fPaddingEnabled(true),        // Di default lo abilitiamo (scelta sicura)
      fPaddingType(B_CRYPTO_PKCS7) // Standard universale
{
	fFd = open("/dev/crypto/v1", O_RDWR);
}

BCrypto::~BCrypto() {
	if (fFd >= 0)
		close(fFd);
}

status_t BCrypto::InitCheck() const {
	return fFd >= 0 ? B_OK : B_ERROR;
}

void
BCrypto::SetPadding(bool enable,BCryptoPaddingType type){
	fPaddingEnabled=enable;
	fPaddingType=type;
}

status_t 
BCrypto::GetRandomBytes(void* buffer, size_t len)
{
    if (fFd < 0) return B_NO_INIT;
    if (buffer == NULL || len == 0) return B_BAD_VALUE;

    BCryptoRandomRequest req;
    req.buffer = buffer;
    req.length = len;

    if (ioctl(fFd, B_CRYPTO_IOCTL_GET_RANDOM, &req) < 0)
        return B_ERROR;

    return req.result;
}


size_t 
BCrypto::GetOutputSize(size_t inputLen, BCryptoOperation op)
{
    if (!fPaddingEnabled || fPaddingType == B_CRYPTO_PADDING_NONE)
        return inputLen;

    if (op == B_CRYPTO_ENCRYPT) {
       switch (fPaddingType) {
            case B_CRYPTO_PKCS7:
            case B_CRYPTO_ISO7816:
                // Arrotonda sempre al multiplo di 16 successivo
                // Se inputLen è 16, diventa 32. Se è 15, diventa 16.
                return (inputLen + 16) & ~15;
            
            case B_CRYPTO_ZERO_PADDING:
                // Solo se necessario per arrivare a 16
                return (inputLen + 15) & ~15;
                
            default:
                return inputLen;
        }
    }
    return inputLen; // Per Decrypt restituiamo la dimensione del buffer cifrato
}
ssize_t 
BCrypto::Encrypt(uint8* key, size_t keyLen, uint8* iv, size_t ivLen,
                 const void* in, size_t inLen, void* out, size_t outSize) 
{
    size_t requiredSize = GetOutputSize(inLen, B_CRYPTO_ENCRYPT);
    if (outSize < requiredSize) return B_BAD_VALUE;

    uint8* dataToProcess = (uint8*)in;
    uint8* tempBuffer = nullptr;
    size_t processLen = inLen;

    // Se il padding è attivo e necessario (o se PKCS7 richiede sempre un blocco extra)
    if (fPaddingEnabled && fPaddingType != B_CRYPTO_PADDING_NONE) {
        processLen = requiredSize;
        tempBuffer = new(std::nothrow) uint8[processLen];
        if (!tempBuffer) return B_NO_MEMORY;
        
        memcpy(tempBuffer, in, inLen);
        _ApplyPadding(tempBuffer, inLen, processLen); 
        dataToProcess = tempBuffer;
    }

    iovec src = { dataToProcess, processLen };
    iovec dst = { out, processLen };
    
    BCryptoUserRequest req;
    // Specifichiamo ALGO e MODE correttamente
    _FillRequest(req, B_CRYPTO_ENCRYPT, B_CRYPTO_AES, B_CRYPTO_MODE_CBC,
                 key, keyLen, iv, ivLen, &src, &dst, 1);

    status_t st = Process(req); 
    
    if (tempBuffer) delete[] tempBuffer;
    return (st == B_OK) ? (ssize_t)processLen : st;
}
/*
ssize_t 
BCrypto::Encrypt(uint8* key, size_t keyLen, uint8* iv, size_t ivLen,
                 const void* in, size_t inLen, void* out, size_t outSize) 
{
    size_t requiredSize = GetOutputSize(inLen, B_CRYPTO_ENCRYPT);
    if (outSize < requiredSize)
        return B_BAD_VALUE;

    uint8* dataToProcess = (uint8*)in;
    uint8* tempBuffer = nullptr;
    size_t processLen = inLen;

    if (fPaddingEnabled && fPaddingType != B_CRYPTO_PADDING_NONE) {
        processLen = requiredSize;
        tempBuffer = new(std::nothrow) uint8[processLen];
        if (!tempBuffer) return B_NO_MEMORY;
        
        memcpy(tempBuffer, in, inLen);
        size_t padLen = processLen - inLen;
        switch (fPaddingType) {
            case B_CRYPTO_PKCS7:
                // Ogni byte ha il valore della lunghezza del padding (es. 0x03, 0x03, 0x03)
                memset(tempBuffer + inLen, (uint8)padLen, padLen);
                break;

            case B_CRYPTO_ISO7816:
                // Il primo byte è 0x80, i restanti sono 0x00
                tempBuffer[inLen] = 0x80;
                if (padLen > 1)
                    memset(tempBuffer + inLen + 1, 0, padLen - 1);
                break;

            case B_CRYPTO_ZERO_PADDING:
                // Tutti i byte sono 0x00
                memset(tempBuffer + inLen, 0, padLen);
                break;

            default:
                // Se non gestito, facciamo finta di nulla o diamo errore
                delete[] tempBuffer;
                return B_NOT_SUPPORTED;
        }
        
        dataToProcess = tempBuffer;
    }

    // Usiamo lo shortcut dei parametri pre-configurati
    BCryptoUserRequest req;
    memset(&req, 0, sizeof(req));
    req.operation   = B_CRYPTO_ENCRYPT;
    req.algorithm   = B_CRYPTO_AES;
    req.mode        = B_CRYPTO_MODE_CBC;
    req.key         = key;
    req.keyLength   = keyLen;
    req.iv          = iv;
    req.ivLength    = ivLen;
    
    iovec src = { dataToProcess, processLen };
    iovec dst = { out, processLen };
    req.source      = &src;
    req.destination = &dst;
    req.vectorCount = 1;

    status_t st = Process(req);
    
    if (tempBuffer) delete[] tempBuffer;

    if (st != B_OK) return st;
    return (ssize_t)processLen;
}*/
ssize_t 
BCrypto::Decrypt(uint8* key, size_t keyLen, uint8* iv, size_t ivLen,
                 const void* in, size_t inLen, void* out, size_t outSize) 
{
    if (inLen == 0 || (inLen % 16) != 0 || outSize < inLen) return B_BAD_VALUE;

    iovec src = { (void*)in, inLen };
    iovec dst = { out, inLen };

    BCryptoUserRequest req;
    _FillRequest(req, B_CRYPTO_DECRYPT, B_CRYPTO_AES, B_CRYPTO_MODE_CBC,
                 key, keyLen, iv, ivLen, &src, &dst, 1);

    status_t st = Process(req); 
    if (st != B_OK) return st;

    size_t finalLen = inLen;
    if (fPaddingEnabled && fPaddingType != B_CRYPTO_PADDING_NONE) {
        finalLen = _RemovePadding((uint8*)out, inLen);
        if (finalLen == (size_t)B_BAD_DATA) return B_BAD_DATA;
    }

    return (ssize_t)finalLen;
}
/*
ssize_t 
BCrypto::Decrypt(uint8* key, size_t keyLen, uint8* iv, size_t ivLen,
                 const void* in, size_t inLen, void* out, size_t outSize) 
{
    // Validazione base: AES richiede blocchi da 16
    if (inLen == 0 || (inLen % 16) != 0 || outSize < inLen)
        return B_BAD_VALUE;

    iovec src = { (void*)in, inLen };
    iovec dst = { out, inLen };

    BCryptoUserRequest req;
    memset(&req, 0, sizeof(req));
    req.operation   = B_CRYPTO_DECRYPT;
    req.algorithm   = B_CRYPTO_AES;
    req.mode        = B_CRYPTO_MODE_CBC;
    req.key         = key;
    req.keyLength   = keyLen;
    req.iv          = iv;
    req.ivLength    = ivLen;
    req.source      = &src;
    req.destination = &dst;
    req.vectorCount = 1;

    status_t st = Process(req);
    if (st != B_OK) return st;

    size_t finalLen = inLen;

    if (fPaddingEnabled && fPaddingType != B_CRYPTO_PADDING_NONE) {
        uint8* pOut = (uint8*)out;

        switch (fPaddingType) {
            case B_CRYPTO_PKCS7: {
                uint8 padValue = pOut[inLen - 1];
                // Il valore del padding deve essere tra 1 e 16
                if (padValue < 1 || padValue > 16) 
                    return B_BAD_DATA;

                // 2. TUTTI i byte del padding devono avere lo stesso valore
                for (size_t i = inLen - padValue; i < inLen; i++) {
                    if (pOut[i] != padValue)
                        return B_BAD_DATA; // Padding non valido!
                }
                
                finalLen = inLen - padValue;
                break;
            }

            case B_CRYPTO_ISO7816: {
                // Il padding ISO7816 deve finire con 0x80 seguito da zero o più 0x00
                // Cerchiamo lo 0x80 partendo dal fondo
                ssize_t i = inLen - 1;
                while (i >= (ssize_t)(inLen - 16) && pOut[i] == 0x00) {
                    i--;
                }
                
                if (i >= 0 && pOut[i] == 0x80) {
                    finalLen = i;
                } else {
                    return B_BAD_DATA; // Non abbiamo trovato lo 0x80 dove previsto
                }
                break;
            }

            case B_CRYPTO_ZERO_PADDING: {
                // Cerchiamo il primo byte non zero partendo dal fondo
                finalLen = 0;
                for (ssize_t i = inLen - 1; i >= 0; i--) {
                    if (pOut[i] != 0x00) {
                        finalLen = i + 1;
                        break;
                    }
                }
                break;
            }

            default:
                break;
        }
    }
    // Restituiamo il numero di byte "reali" (senza padding)
    return (ssize_t)finalLen;
}*/
status_t 
BCrypto::Process(BCryptoUserRequest& userReq) 
{
    if (fFd < 0) return B_NO_INIT;
    
    if (userReq.operation == B_CRYPTO_DIGEST) {
        return _ProcessDigest(userReq);
    }

    const size_t kMinZeroCopySize = 4096; 
    const int kMaxVects = 32;

    size_t vIndex = 0;
    while (vIndex < userReq.vectorCount) {
        
        // Se il vettore attuale è piccolo, usiamo il Coalescing (Ottimizzato)
        if (userReq.source[vIndex].iov_len < kMinZeroCopySize) {
            size_t coalesceTotal = 0;
            int vInCoalesce = 0;
            int maxToScan = std::min(kMaxVects, (int)(userReq.vectorCount - vIndex));
            
            for (int j = 0; j < maxToScan; j++) {
                // Ci fermiamo se il totale diventa troppo grande o troviamo un vettore "gigante"
                if (userReq.source[vIndex + j].iov_len >= kMinZeroCopySize) break;
                
                coalesceTotal += userReq.source[vIndex + j].iov_len;
                vInCoalesce++;
            }

            void* bounceBuffer = malloc(coalesceTotal);
            if (!bounceBuffer) return B_NO_MEMORY;

            // PACK
            size_t offset = 0;
            for (int j = 0; j < vInCoalesce; j++) {
                memcpy((uint8*)bounceBuffer + offset, userReq.source[vIndex + j].iov_base, userReq.source[vIndex + j].iov_len);
                offset += userReq.source[vIndex + j].iov_len;
            }

            iovec singleIov = { bounceBuffer, coalesceTotal };
            BCryptoUserRequest batchReq = userReq;
            batchReq.source = &singleIov;
            batchReq.destination = &singleIov;
            batchReq.vectorCount = 1;

            status_t err = ioctl(fFd, B_CRYPTO_IOCTL_PROCESS, &batchReq);

            if (err >= 0) {
                offset = 0;
                for (int j = 0; j < vInCoalesce; j++) {
                    memcpy(userReq.destination[vIndex + j].iov_base, (uint8*)bounceBuffer + offset, userReq.destination[vIndex + j].iov_len);
                    offset += userReq.destination[vIndex + j].iov_len;
                }
            }

            free(bounceBuffer);
            if (err < 0) return err;
            vIndex += vInCoalesce;

        } else {
            // --- STRATEGIA: ZERO-COPY DIRETTO (VELOCE) ---
            // Se il vettore è grande, ne mandiamo fino a 32 in un colpo solo.
            // Non stiamo a fare troppi controlli, il driver gestirà il resto.
            
            int vInDirect = std::min(kMaxVects, (int)(userReq.vectorCount - vIndex));

            BCryptoUserRequest directReq = userReq;
            directReq.source = &userReq.source[vIndex];
            directReq.destination = &userReq.destination[vIndex];
            directReq.vectorCount = vInDirect;

            status_t err = ioctl(fFd, B_CRYPTO_IOCTL_PROCESS, &directReq);
            if (err < 0) return err;

            vIndex += vInDirect;
        }
    }

    return B_OK;
}



void
BCrypto::_FillRequest(BCryptoUserRequest& req, BCryptoOperation op, BCryptoAlgorithmID algo, BCryptoMode mode,
                      uint8* key, size_t keyLen, uint8* iv, size_t ivLen, 
                      iovec* src, iovec* dst, int vCount)
{
    memset(&req, 0, sizeof(req));
    req.operation   = op;
    req.algorithm   = algo;
    req.mode        = mode;
    req.key         = key;
    req.keyLength   = keyLen;
    req.iv          = iv;
    req.ivLength    = ivLen;
    req.source      = src;
    req.destination = dst;
    req.vectorCount = vCount;
}

status_t
BCrypto::_ProcessDigest(BCryptoUserRequest& userReq)
{
    size_t totalSize = 0;
    for (size_t i = 0; i < userReq.vectorCount; i++)
        totalSize += userReq.source[i].iov_len;

    if (totalSize == 0) return B_BAD_VALUE;

    // Coalescing forzato per garantire l'atomicità dell'hash nel driver
    void* bounceBuffer = malloc(totalSize);
    if (!bounceBuffer) return B_NO_MEMORY;

    size_t offset = 0;
    for (size_t i = 0; i < userReq.vectorCount; i++) {
        memcpy((uint8*)bounceBuffer + offset, 
               userReq.source[i].iov_base, userReq.source[i].iov_len);
        offset += userReq.source[i].iov_len;
    }

    uint8 hashResult[64]; // Buffer locale per ospitare il digest più grande possibile
    iovec src = { bounceBuffer, totalSize };
    iovec dst = { hashResult, sizeof(hashResult) };

    // Prepariamo la richiesta per il driver usando i tuoi enum
    BCryptoUserRequest digestReq;
    _FillRequest(digestReq, B_CRYPTO_DIGEST, userReq.algorithm, B_CRYPTO_MODE_ANY,
                 nullptr, 0, nullptr, 0, &src, &dst, 1);

    status_t err = ioctl(fFd, B_CRYPTO_IOCTL_PROCESS, &digestReq);

    if (err == B_OK && userReq.destination != nullptr) {
        size_t hLen = GetHashLength(userReq.algorithm);
        if (hLen > 0) {
            // Copiamo il risultato nel primo iovec di destinazione dell'utente
            memcpy(userReq.destination[0].iov_base, hashResult, hLen);
        }
    }

    free(bounceBuffer);
    return err;
    
    /* versione flattata
    size_t totalSize = 0;
    for (size_t i = 0; i < userReq.vectorCount; i++)
        totalSize += userReq.source[i].iov_len;

    if (totalSize == 0) return B_BAD_VALUE;

    void* bounceBuffer = malloc(totalSize);
    if (!bounceBuffer) return B_NO_MEMORY;

    // Compattiamo i dati
    size_t offset = 0;
    for (size_t i = 0; i < userReq.vectorCount; i++) {
        memcpy((uint8*)bounceBuffer + offset, 
               userReq.source[i].iov_base, userReq.source[i].iov_len);
        offset += userReq.source[i].iov_len;
    }

    uint8 hashResult[64];
    memset(hashResult, 0, sizeof(hashResult));

    // DEFINIAMO GLI IOVEC QUI E NON PASSIAMO I LORO INDIRIZZI A FUNZIONI ESTERNE
    iovec localSrc = { bounceBuffer, totalSize };
    iovec localDst = { hashResult, sizeof(hashResult) };

    // Prepariamo la richiesta direttamente
    BCryptoUserRequest digestReq;
    memset(&digestReq, 0, sizeof(digestReq));
    
    digestReq.operation   = B_CRYPTO_DIGEST;
    digestReq.algorithm   = userReq.algorithm;
    digestReq.mode        = B_CRYPTO_MODE_ANY;
    digestReq.source      = &localSrc;  // Punta a localSrc in questo stack
    digestReq.destination = &localDst; // Punta a localDst in questo stack
    digestReq.vectorCount = 1;

    // Chiamata diretta
    status_t err = ioctl(fFd, B_CRYPTO_IOCTL_PROCESS, &digestReq);

    if (err == B_OK && userReq.destination != nullptr) {
        size_t hLen = GetHashLength(userReq.algorithm);
        if (hLen > 0) {
            // Copiamo il risultato finale nel buffer dell'utente
            memcpy(userReq.destination[0].iov_base, hashResult, hLen);
        }
    }

    free(bounceBuffer);
    return err;
    */
}

size_t
BCrypto::_RemovePadding(uint8* buffer, size_t len)
{
    if (len == 0 || (len % 16) != 0) return B_BAD_DATA;

    switch (fPaddingType) {
        case B_CRYPTO_PKCS7: {
            uint8 padValue = buffer[len - 1];
            if (padValue < 1 || padValue > 16) return B_BAD_DATA;
            for (size_t i = len - padValue; i < len; i++) {
                if (buffer[i] != padValue) return B_BAD_DATA;
            }
            return len - padValue;
        }

        case B_CRYPTO_ISO7816: {
            ssize_t i = len - 1;
            // Cerchiamo lo 0x80 partendo dal fondo, saltando gli zeri
            while (i >= (ssize_t)(len - 16) && buffer[i] == 0x00) {
                i--;
            }
            if (i >= 0 && buffer[i] == 0x80) {
                return (size_t)i;
            }
            return B_BAD_DATA;
        }

        case B_CRYPTO_ZERO_PADDING: {
            size_t i = len;
            while (i > 0 && buffer[i - 1] == 0) i--;
            return i;
        }

        default:
            return len;
    }
}

void
BCrypto::_ApplyPadding(uint8* buffer, size_t inputLen, size_t totalLen)
{
    size_t padLen = totalLen - inputLen;
    if (padLen == 0) return;

    switch (fPaddingType) {
        case B_CRYPTO_PKCS7:
            // Ogni byte ha il valore della lunghezza del padding
            memset(buffer + inputLen, (uint8)padLen, padLen);
            break;

        case B_CRYPTO_ISO7816:
            // Il primo byte è 0x80, i restanti sono 0x00
            buffer[inputLen] = 0x80;
            if (padLen > 1)
                memset(buffer + inputLen + 1, 0, padLen - 1);
            break;

        case B_CRYPTO_ZERO_PADDING:
            // Tutti i byte sono 0x00
            memset(buffer + inputLen, 0, padLen);
            break;

        default:
            break;
    }
}
size_t
BCrypto::GetHashLength(BCryptoAlgorithmID algo)
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
        default:                 return 0;
    }
}
