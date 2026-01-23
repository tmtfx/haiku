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
static void
secure_memzero(void* p, size_t s)
{
    if (p == NULL)
        return;
    volatile uint8* cp = (volatile uint8*)p;
    while (s--)
        *cp++ = 0;
}

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
// Funzione helper per gestire la callback in modo uniforme
static status_t _FinalizeRequest(BCryptoRequest* request, status_t st) {
    if (request->completionCallback) {
        request->completionCallback(request, st);
        return B_OK;
    }
    return st;
}

status_t
BSubmitCryptoRequest(BCryptoRequest* request)
{
    if (!request)
        return B_BAD_VALUE;

    MutexLocker _(sCryptoLock);
    
    DoublyLinkedList<AlgoNode>::Iterator it = sAlgorithms.GetIterator();
    while (AlgoNode* node = it.Next()) {
        BCryptoAlgorithm* algo = node->algo;

        if (algo->algorithm != request->algorithm)
            continue;
        if (algo->mode != request->mode && request->mode != 0)
            continue;
            
        // --- SCENARIO 1: Driver con supporto Asincrono Reale (Schede PCIe) ---
        // Se l'algoritmo ha un flag che indica "Gestisco io la memoria/DMA"
        if (algo->flags & B_CRYPTO_ALG_ASYNC) {
            status_t st = algo->Process(request);
            
            if (st == B_PENDING) {
                // Il driver ha preso in carico tutto. 
                // Il Core non tocca i buffer e non chiama la callback.
                return B_OK; 
            }
            // Se ritorna B_OK o errore, proseguiamo alla gestione callback
            return _FinalizeRequest(request, st);
        }
        
        // --- SCENARIO 2: Driver Sincroni (AESNI, PadLock, Soft) ---
        // Qui usiamo il loop SMAP che abbiamo perfezionato
        status_t st = B_OK;

        for (size_t i = 0; i < request->vectorCount; i++) {
            // Usiamo riferimenti locali ma non modifichiamo l'originale se const
            const iovec& srcOrig = request->source[i];
            iovec& dstOrig       = request->destination[i];
            
            if (srcOrig.iov_len == 0) continue;
            
            uint8* kBuffer = (uint8*)malloc(srcOrig.iov_len);
            if (kBuffer == NULL) return B_NO_MEMORY;

            if (user_memcpy(kBuffer, srcOrig.iov_base, srcOrig.iov_len) != B_OK) {
                free(kBuffer);
                return B_BAD_ADDRESS;
            }

            /* --- IL TRUCCO --- */
            // Salviamo gli indirizzi utente originali
            void* oldSrcBase = srcOrig.iov_base;
            void* oldDstBase = dstOrig.iov_base;

            // Usiamo il const_cast solo per il tempo della chiamata al driver.
            // Siccome siamo nel Kernel e abbiamo il lock, è sicuro.
            const_cast<iovec&>(srcOrig).iov_base = kBuffer;
            dstOrig.iov_base = kBuffer;

            // 3. ESECUZIONE DEL DRIVER
            st = algo->Process(request);

            // 4. GESTIONE RISULTATO
            if (st == B_OK) {
                user_memcpy(oldDstBase, kBuffer, srcOrig.iov_len);
            }

            secure_memzero(kBuffer, srcOrig.iov_len);
            free(kBuffer);

            // RIPRISTINO: rimettiamo i puntatori utente originali
            const_cast<iovec&>(srcOrig).iov_base = oldSrcBase;
            dstOrig.iov_base = oldDstBase;

            if (st != B_OK) break;
        }

        /*// 5. GESTIONE CALLBACK
        if (request->completionCallback) {
            request->completionCallback(request, st);
            return B_OK; 
        }

        return st;*/
        return _FinalizeRequest(request, st);
    }
    
    return B_NOT_SUPPORTED;
}


/*
status_t
BSubmitCryptoRequest(BCryptoRequest* request)
{
    if (!request)
        return B_BAD_VALUE;

    MutexLocker _(sCryptoLock);
    
    DoublyLinkedList<AlgoNode>::Iterator it = sAlgorithms.GetIterator();
    while (AlgoNode* node = it.Next()) {
        BCryptoAlgorithm* algo = node->algo;

        // 1. Controllo Algoritmo e Modo
        if (algo->algorithm != request->algorithm)
            continue;
        if (algo->mode != request->mode && request->mode != 0)
            continue;

        // 2. GESTIONE SMAP CENTRALIZZATA
        // Prepariamo i buffer per ogni iovec prima di chiamare il driver
        for (size_t i = 0; i < request->vectorCount; i++) {
            const iovec& src = request->source[i];
            iovec& dst = request->destination[i];
            
            if (src.iov_len == 0) continue;
            
            // Creiamo un buffer kernel temporaneo
            uint8* kBuffer = (uint8*)malloc(src.iov_len);
            if (kBuffer == NULL) return B_NO_MEMORY;

            // Salviamo i puntatori utente originali
            void* userSrcBase = src.iov_base;
            void* userDstBase = dst.iov_base;

            // Copiamo i dati dall'utente al kernel
            if (user_memcpy(kBuffer, userSrcBase, src.iov_len) != B_OK) {
                free(kBuffer);
                return B_BAD_ADDRESS;
            }

            // Sostituiamo i puntatori: ora il driver vedrà solo memoria kernel!
            src.iov_base = kBuffer;
            dst.iov_base = kBuffer;

            // 3. ESECUZIONE DEL DRIVER
            status_t st = algo->Process(request);

            // 4. GESTIONE RISULTATO E SMAP (Rollback/Commit)
            if (st == B_OK) {
                // Successo: copiamo i dati cifrati/decifrati all'utente
                user_memcpy(userDstBase, kBuffer, src.iov_len);
            }

            // Pulizia
            secure_memzero(kBuffer, src.iov_len);
            free(kBuffer);

            // Ripristiniamo i puntatori originali per l'utente
            src.iov_base = userSrcBase;
            dst.iov_base = userDstBase;

            if (st != B_OK) return st;
        }

        // 5. GESTIONE CALLBACK (Centralizzata)
        // Se c'è una callback, la chiamiamo noi qui. I driver non devono più farlo!
        if (request->completionCallback) {
            request->completionCallback(request, B_OK);
            return B_OK; 
        }

        return B_OK;
    }
    
    return B_NOT_SUPPORTED;
}*/
