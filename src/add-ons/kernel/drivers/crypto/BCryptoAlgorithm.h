/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef _B_CRYPTO_ALGORITHM_H_
#define _B_CRYPTO_ALGORITHM_H_


#include <SupportDefs.h>
#include <crypto/BCryptoDefs.h>
#include <crypto/BCryptoKernelInternal.h>
#include <util/DoublyLinkedList.h>

struct BCryptoAlgorithm : public DoublyLinkedListLinkImpl<BCryptoAlgorithm> {
    BCryptoAlgorithmID  algorithm;
    BCryptoMode         mode;
    uint32              flags;
    char                name[32];
    int32               priority;
    status_t            (*Process)(BCryptoRequest* request);

    status_t            (*HashInit)(void** context, size_t* contextSize);
    status_t            (*HashUpdate)(void* context, const iovec* vecs, size_t count);
    status_t            (*HashFinal)(void* context, uint8* outDigest);
    
    //status_t            (*StreamInit)(void** context, const uint8* key, size_t keyLen, const uint8* iv, size_t ivLen);
    status_t            (*StreamInit)(void** context, size_t* _contextSize, const uint8* key, size_t keyLen, const uint8* iv, size_t ivLen);
    status_t            (*StreamUpdate)(void* context, const iovec* src, const iovec* dst, size_t count);
    status_t            (*StreamFinal)(void* context, uint8* outTag);
};
#endif
