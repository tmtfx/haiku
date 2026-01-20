/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include "BCryptoCore.h"

#include <lock.h>
#include <util/AutoLock.h>
//#include <AutoLocker.h>
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

//static BLocker sCryptoLock;
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

uint32
BGetStoredCryptoCapabilities()
{
    return sCryptoCapabilities;
}

void
crypto_uninit_core()
{
    mutex_destroy(&sCryptoLock);
}
status_t
BRegisterCryptoAlgorithm(BCryptoAlgorithm* algorithm)
{
    //BAutolock _(sCryptoLock);
    MutexLocker _(sCryptoLock);

    //AlgoNode* node = new AlgoNode;
    AlgoNode* node = new(std::nothrow) AlgoNode;
    if (node == NULL)
        return B_NO_MEMORY;
        
    node->algo = algorithm;
    sAlgorithms.Add(node);

    return B_OK;
}

status_t
BUnregisterCryptoAlgorithm(BCryptoAlgorithmID algorithm)
{
	//BAutolock _(sCryptoLock);
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
    MutexLocker _(sCryptoLock);

    BCryptoAlgorithm* best = NULL;

	DoublyLinkedList<AlgoNode>::Iterator it = sAlgorithms.GetIterator();
	while (AlgoNode* node = it.Next()) {
		BCryptoAlgorithm* algo = node->algo;

		if (algo->algorithm != request->algorithm)
			continue;

		if (!best || algo->priority > best->priority)
			best = algo;
	}
    /*
    for (AlgoNode* node = sAlgorithms.Head();
         node != NULL;
         node = sAlgorithms.GetNext(node)) {

        BCryptoAlgorithm* algo = node->algo;

        if (algo->algorithm != request->algorithm)
            continue;

        if (!best || algo->priority > best->priority)
            best = algo;
    }
    */

    if (!best)
        return B_NOT_SUPPORTED;

    return best->Process(request);
}
