/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include "BCryptoCore.h"

#include <lock.h>
#include <util/AutoLock.h>
#include <util/DoublyLinkedList.h>
#include "BCryptoCapabilities.h"
#include <new>
#include <debug.h>

using BPrivate::AutoLocker;

struct MutexLocking {
	typedef mutex* Lockable;
	static inline status_t Lock(Lockable lock) { return mutex_lock(lock); }
	static inline void Unlock(Lockable lock) { mutex_unlock(lock); }
};

//typedef AutoLocker<mutex, mutex_lock, mutex_unlock> MutexLocker;
//typedef AutoLocker<mutex, MutexLocking::Lock, MutexLocking::Unlock> MutexLocker;


static mutex sCryptoLock;

struct AlgoNode : DoublyLinkedListLinkImpl<AlgoNode> {
    BCryptoAlgorithm* algo;
};

static DoublyLinkedList<AlgoNode> sAlgorithms;
static uint32 sCryptoCapabilities = 0;

extern "C" status_t
crypto_init_core()
{
	dprintf("BCrypto: [2] Entrato in crypto_init_core\n");
    mutex_init(&sCryptoLock, "crypto core lock");
    sCryptoCapabilities = BGetCryptoCapabilities();
    return B_OK;
}

extern "C" void
crypto_uninit_core()
{
    mutex_destroy(&sCryptoLock);
}
uint32
BGetStoredCryptoCapabilities()
{
    return sCryptoCapabilities;
}

status_t
BRegisterCryptoAlgorithm(BCryptoAlgorithm* algorithm)
{
	if (!algorithm)
        return B_BAD_VALUE;
        
    MutexLocker _(sCryptoLock);

    AlgoNode* node = new(std::nothrow) AlgoNode;
    if (node == NULL)
        return B_NO_MEMORY;
        
    node->algo = algorithm;
    //sAlgorithms.Add(node);
    //append by priority
    DoublyLinkedList<AlgoNode>::Iterator it = sAlgorithms.GetIterator();
    AlgoNode* insertBefore = nullptr;

    while (AlgoNode* existing = it.Next()) {
        if (algorithm->priority > existing->algo->priority) {
            insertBefore = existing;
            break;
        }
    }
    
    if (insertBefore)
        sAlgorithms.InsertBefore(insertBefore, node);
    else
        sAlgorithms.Add(node); 

    return B_OK;
}

status_t
BUnregisterCryptoAlgorithm(BCryptoAlgorithmID algorithm)
{
	MutexLocker _(sCryptoLock);
	
	DoublyLinkedList<AlgoNode>::Iterator it = sAlgorithms.GetIterator();
	while (AlgoNode* node = it.Next()) {
		if (node->algo->algorithm == algorithm) {
			sAlgorithms.Remove(node);
			delete node;
			return B_OK;
		}
	}
	return B_ENTRY_NOT_FOUND;

}

status_t
BSubmitCryptoRequest(BCryptoRequest* request)
{
	if (!request)
        return B_BAD_VALUE;

    MutexLocker _(sCryptoLock);
    
    //priority ordered list already done: the first found is the best

	DoublyLinkedList<AlgoNode>::Iterator it = sAlgorithms.GetIterator();
	while (AlgoNode* node = it.Next()) {
		BCryptoAlgorithm* algo = node->algo;

		if (algo->algorithm != request->algorithm)
			continue;
        //sync
		if (!request->completionCallback)
            return algo->Process(request);
        //async
        status_t st = algo->Process(request);

        if (st != B_OK)
            return st;

        // Il driver si occuperà di chiamare request->completionCallback()
        return B_OK;
	}
	
	return B_NOT_SUPPORTED;
}
