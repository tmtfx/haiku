/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include "BCrypto.h"
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include "BCryptoDefs.h"

BCrypto::BCrypto() {
	fFd = open("/dev/crypto/v1", O_RDWR);
}

BCrypto::~BCrypto() {
	if (fFd >= 0)
		close(fFd);
}

status_t BCrypto::InitCheck() const {
	return fFd >= 0 ? B_OK : B_ERROR;
}

status_t BCrypto::Encrypt(uint8* key, size_t keyLen, uint8* iv, size_t ivLen,
	const void* in, void* out, size_t len) {
	return _DoOperation(B_CRYPTO_ENCRYPT, key, keyLen, iv, ivLen, in, out, len);
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

status_t BCrypto::_DoOperation(uint32 op, uint8* key, size_t keyLen, 
	uint8* iv, size_t ivLen, const void* in, void* out, size_t len) {
	
	if (fFd < 0) return B_NO_INIT;

	iovec src = { (void*)in, len };
	iovec dst = { out, len };
	
	BCryptoUserRequest req;
	memset(&req, 0, sizeof(req));
	
	req.operation = static_cast<BCryptoOperation>(op);
	req.algorithm = B_CRYPTO_AES; // O parametrizzabile
	req.mode = B_CRYPTO_MODE_CBC;
	req.key = key;
	req.keyLength = keyLen;
	req.iv = iv;
	req.ivLength = ivLen;
	req.source = &src;
	req.destination = &dst;
	req.vectorCount = 1;

	if (ioctl(fFd, B_CRYPTO_IOCTL_PROCESS, &req) < 0)
		return B_ERROR;

	return B_OK;
}
status_t BCrypto::Process(BCryptoUserRequest& userReq) {
    if (fFd < 0) return B_NO_INIT;

    // 1. Setup richiesta interna
    fInternalReq.algorithm = userReq.algorithm;
    fInternalReq.mode = userReq.mode;
    fInternalReq.operation = userReq.operation;
    fInternalReq.key = userReq.key;
    fInternalReq.keyLength = userReq.keyLength;
    fInternalReq.iv = userReq.iv;
    fInternalReq.ivLength = userReq.ivLength;
    fInternalReq.vectorCount = userReq.vectorCount;
    fInternalReq.completionSem = -1;

    // 2. Copia dei vettori (per stabilità SMAP)
    if (userReq.vectorCount > 0 && userReq.vectorCount <= 32) {
        memcpy(fInternalSrc, userReq.source, sizeof(iovec) * userReq.vectorCount);
        memcpy(fInternalDst, userReq.destination, sizeof(iovec) * userReq.vectorCount);
    } else {
        return B_BAD_VALUE;
    }

    fInternalReq.source = fInternalSrc;
    fInternalReq.destination = fInternalDst;

    // 3. Chiamata al Kernel
    status_t st = ioctl(fFd, B_CRYPTO_IOCTL_PROCESS, &fInternalReq);

    // 4. RITORNO DATI SELETTIVO (Cruciale!)
    if (st == B_OK) {
        // Aggiorniamo solo il risultato e l'IV, NON i puntatori source/dest
        userReq.result = fInternalReq.result;
        
        // Se il driver ha aggiornato l'IV nella nostra struct interna, 
        // lo riportiamo nel buffer dell'utente
        if (userReq.iv != NULL && fInternalReq.iv != NULL) {
            memcpy(userReq.iv, fInternalReq.iv, userReq.ivLength);
        }
    }
    return (st == B_OK) ? fInternalReq.result : st;
}
