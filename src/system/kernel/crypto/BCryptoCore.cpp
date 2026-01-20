/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include "BCryptoCore.h"

#include <lock.h>
#include <Autolock.h>
#include <Locker.h>
#include <util/DoublyLinkedList.h>

static BLocker sCryptoLock;

struct AlgoNode : DoublyLinkedListLinkImpl<AlgoNode> {
    BCryptoAlgorithm* algo;
};

static DoublyLinkedList<AlgoNode> sAlgorithms;

status_t
BRegisterCryptoAlgorithm(BCryptoAlgorithm* algorithm)
{
    BAutolock _(sCryptoLock);

    AlgoNode* node = new AlgoNode;
    node->algo = algorithm;
    sAlgorithms.Add(node);

    return B_OK;
}

status_t
BUnregisterCryptoAlgorithm(BCryptoAlgorithmID algorithm)
{
    BAutolock _(sCryptoLock);

    for (AlgoNode* node = sAlgorithms.Head();
         node != NULL;
         node = sAlgorithms.GetNext(node)) {

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
    BAutolock _(sCryptoLock);

    BCryptoAlgorithm* best = NULL;

    for (AlgoNode* node = sAlgorithms.Head();
         node != NULL;
         node = sAlgorithms.GetNext(node)) {

        BCryptoAlgorithm* algo = node->algo;

        if (algo->algorithm != request->algorithm)
            continue;

        if (!best || algo->priority > best->priority)
            best = algo;
    }

    if (!best)
        return B_NOT_SUPPORTED;

    return best->Process(request);
}
