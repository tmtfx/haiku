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


BCrypto::BCrypto() : fFd(-1),
      fPaddingEnabled(true),        // Di default lo abilitiamo (scelta sicura)
      fPaddingType(B_CRYPTO_PKCS7), // Standard universale
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
}
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
}

status_t BCrypto::Process(BCryptoUserRequest& userReq) {
    if (fFd < 0) return B_NO_INIT;
    
    status_t st = ioctl(fFd, B_CRYPTO_IOCTL_PROCESS, &userReq);
    if (st < 0) return B_ERROR;
	return userReq.result;
}
