/*
 * Copyright 2018, Your Name <your@email.address>
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

status_t
BCrypto::Process(BCryptoUserRequest& userReq)
{
    if (fFd < 0) return B_NO_INIT;
        
    sem_id doneSem = create_sem(0, "crypto_completion");
    if (doneSem < B_OK) return doneSem;

    userReq.completionSem = doneSem;
    
    status_t st = ioctl(fFd, B_CRYPTO_IOCTL_PROCESS, &userReq);
    if (st == B_OK) {
        // Se il driver ritorna B_OK ma l'operazione è asincrona (B_PENDING internamente),
        // aspettiamo il semaforo. Se è già finito, acquire_sem ritorna subito.
        acquire_sem(doneSem);
        st = userReq.result;
    }

    delete_sem(doneSem);
    return st;
/*
    if (ioctl(fFd, B_CRYPTO_IOCTL_PROCESS, &userReq) < 0) {
        delete_sem(doneSem);
        return B_ERROR;
    }

    acquire_sem(doneSem);

    status_t result = userReq.result;
    delete_sem(doneSem);

    return result;*/
}
