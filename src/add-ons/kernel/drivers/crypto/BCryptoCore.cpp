/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include "BCryptoCore.h"
#include <crypto/BCryptoDefs.h>
#include "BCryptoCPU.h"
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif
    #include "BCryptoDevice.h" 
    // Se BCryptoDevice.h non ha già l'extern "C" al suo interno, 
    // lo forziamo qui per le funzioni del manager.
#ifdef __cplusplus
}
#endif

#include <lock.h>
#include <util/AutoLock.h>
#include <util/DoublyLinkedList.h>
#include <new>
#include <debug.h>
#include <malloc.h> // Per malloc/free

using BPrivate::AutoLocker;

struct MutexLocking {
    typedef mutex* Lockable;
    static inline status_t Lock(Lockable lock) { return mutex_lock(lock); }
    static inline void Unlock(Lockable lock) { mutex_unlock(lock); }
};

//static void
//secure_memzero(void* p, size_t s)
//{
//    if (p == NULL)
//        return;
//    volatile uint8* cp = (volatile uint8*)p;
//    while (s--)
//        *cp++ = 0;
//}

static mutex sCryptoLock;
static bool sCoreInitialized = false;

struct AlgoNode : DoublyLinkedListLinkImpl<AlgoNode> {
    BCryptoAlgorithm* algo;
};

static DoublyLinkedList<AlgoNode> sAlgorithms;
static uint32 sCryptoCapabilities = 0;

extern "C" status_t
crypto_init_core()
{
    if (sCoreInitialized) return B_OK;
    
    mutex_init(&sCryptoLock, "crypto core lock");
    
    // 1. Inizializza il manager dei dispositivi
    status_t status = crypto_manager_init();
    if (status != B_OK) {
        mutex_destroy(&sCryptoLock);
        return status;
    }
    
    // 2. Rileva e registra la CPU nel Manager
    crypto_device_info cpuInfo;
    if (BGetCPUCryptoInfo(&cpuInfo) == B_OK) {
        status = register_crypto_device(&cpuInfo);
        if (status == B_OK) {
            sCryptoCapabilities = cpuInfo.hw_type; 
        }
    }

    sCoreInitialized = true;
    return B_OK;
}

extern "C" void
crypto_uninit_core()
{
    if (!sCoreInitialized) return;
    
    // Svuota la lista degli algoritmi
    MutexLocker _(&sCryptoLock);
    while (AlgoNode* node = sAlgorithms.RemoveHead()) {
        delete node;
    }

    crypto_manager_uninit();
    mutex_destroy(&sCryptoLock);
    
    sCoreInitialized = false;
    sCryptoCapabilities = 0;
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
    if (!request) {
        return B_BAD_VALUE;
    }

    MutexLocker _(sCryptoLock);
    DoublyLinkedList<AlgoNode>::Iterator it = sAlgorithms.GetIterator();

    while (AlgoNode* node = it.Next()) {
        BCryptoAlgorithm* algo = node->algo;

        if (algo->algorithm != request->algorithm) continue;

        if (algo->mode != B_CRYPTO_MODE_ANY) {
            // Verifichiamo se il bit richiesto è presente nella maschera del driver
            // Esempio: (3 & 2) -> 2. Se il risultato è 0, il bit non c'è.
            if ((algo->mode & request->mode) == 0)
                continue;
        }
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
        
        // --- SCENARIO 3: Zero-Copy (Mappatura diretta memmoria utente) ---
        status_t st = B_OK;
        for (size_t i = 0; i < request->vectorCount; i++) {
            const iovec& srcOrig = request->source[i];
            iovec& dstOrig       = request->destination[i];
            size_t len = srcOrig.iov_len;

            if (len == 0) continue;

            // 1. Blocchiamo e mappiamo la memoria dell'utente
            // lock_memory assicura che le pagine non vengano spostate o mandate in swap
            status_t lockStatus = lock_memory(srcOrig.iov_base, len, B_READ_DEVICE);
            if (lockStatus != B_OK) return lockStatus;

            // Se sorgente e destinazione sono diverse, blocchiamo anche la destinazione
            bool separateDest = (srcOrig.iov_base != dstOrig.iov_base);
            if (separateDest) {
                st = lock_memory(dstOrig.iov_base, len, B_READ_DEVICE);
                if (st != B_OK) {
                    unlock_memory(srcOrig.iov_base, len, B_READ_DEVICE);
                    return st;
                }
            }
            
            __asm__ __volatile__ ("stac" : : : "cc");//user_access_enable();

            // 2. Prepariamo una richiesta locale "Direct"
            // AES-NI può lavorare direttamente sull'indirizzo utente se siamo nel contesto del thread chiamante
            BCryptoRequest localReq = *request;
            localReq.source = const_cast<iovec*>(&srcOrig);
            localReq.destination = &dstOrig;
            localReq.vectorCount = 1;

            // 3. ESECUZIONE DEL DRIVER (AES-NI)
            // Ora il driver riceve puntatori che puntano alla RAM fisica dell'utente
            st = algo->Process(&localReq);
            
            __asm__ __volatile__ ("clac" : : : "cc");//user_access_disable();

            // 4. Sblocchiamo la memoria
            unlock_memory(srcOrig.iov_base, len, B_READ_DEVICE);
            if (separateDest) {
                unlock_memory(dstOrig.iov_base, len, B_READ_DEVICE);
            }

            if (st != B_OK) break;
        }
        return _FinalizeRequest(request, st);
    }
    return B_NOT_SUPPORTED;
}

status_t
BFillBufferWithRandom(void* buffer, size_t length)
{
    // Invece di chiamare direttamente RDRAND, cerchiamo il miglior RNG registrato
    BCryptoRequest req{};
    iovec vec = { buffer, length };
    
    req.operation = B_CRYPTO_DIGEST; // O una costante B_CRYPTO_GENERATE
    req.algorithm = B_CRYPTO_RNG;
    req.destination = &vec;
    req.source = req.destination;
    req.vectorCount = 1;
    req.flags = B_CRYPTO_ALG_HW_ACCEL | B_CRYPTO_ALG_KERNEL_SPACE ; // Vogliamo solo hardware!
    
    // Passiamo per la logica ufficiale, così se c'è PadLock o RDRAND, 
    // il Core sceglie quello a priorità maggiore.
    return BSubmitCryptoRequest(&req);
}
