/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include "BCryptoCore.h"
//#include "BCryptoDefs.h"
#include <crypto/BCryptoDefs.h>
#include "BCryptoCPU.h"

/*extern "C" {
    status_t crypto_manager_init();
    void crypto_manager_uninit();
    status_t register_crypto_device(crypto_device_info* info);
}*/

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
    
    //dprintf("BCrypto: [2] Entrato in crypto_init_core\n");
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
            //dprintf("BCrypto: CPU registrata nel Manager.\n");
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

    //dprintf("BCrypto: Pulizia Core e Manager\n");
    
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
	//dprintf("BCrypto: Registro algo %d, priorità %d\n", algorithm->algorithm, algorithm->priority);
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
	// TODO malloc is slow!!!!!! maybe we can use lock_memory
	/*
	 * This bool flag (slowFast) is for debug purposes:
	 * using a single buffer extremely improves performance
	 * as AES-NI works on XMM registers, usually working in-place
	 * doesn't corrupt data (no source overwrite while reading)
	 *
	 * in case data would be loaded in a different way by a future driver 
	 * this would cause problems.
	 * 
	 * if it's not a problem, using slowFast as false gives a 1,5x 
	 * boost in performance
	*/
	bool slowFast = false;
	// Soglia sicura per lavorare sullo stack: 512 byte ( Small Buffer Optimization )
	const size_t kStackThreshold = 512;
	
    if (!request) {
        return B_BAD_VALUE;
    }

    MutexLocker _(sCryptoLock);
    DoublyLinkedList<AlgoNode>::Iterator it = sAlgorithms.GetIterator();

    while (AlgoNode* node = it.Next()) {
        BCryptoAlgorithm* algo = node->algo;

        if (algo->algorithm != request->algorithm)
            continue;
        //if (algo->mode != request->mode && request->mode != 0)
        //    continue;
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
        
        
        
        // --- SCENARIO 2: Driver Sincroni (AESNI, PadLock, Soft) ---
        // Qui usiamo il loop SMAP
        status_t st = B_OK;

        for (size_t i = 0; i < request->vectorCount; i++) {
            // Usiamo riferimenti locali ma non modifichiamo l'originale se const
            const iovec& srcOrig = request->source[i];
            iovec& dstOrig       = request->destination[i];
            size_t len = srcOrig.iov_len;
            if (len == 0) continue;
            
            // Salviamo gli indirizzi utente originali
            void* oldSrcBase = srcOrig.iov_base;
            void* oldDstBase = dstOrig.iov_base;
            
            if (slowFast) {
                /* slower safer */
                uint8 stackSrc[kStackThreshold];
                uint8 stackDst[kStackThreshold];
                uint8* kSrcBuffer = nullptr;
                uint8* kDstBuffer = nullptr;
                bool heapUsed = false;
				if (len <= kStackThreshold) {
					kSrcBuffer = stackSrc;
                    kDstBuffer = stackDst;
                } else {
                    kSrcBuffer = (uint8*)malloc(len);
                    kDstBuffer = (uint8*)malloc(len);
                    if (!kSrcBuffer || !kDstBuffer) {
                        free(kSrcBuffer); free(kDstBuffer);
                        return B_NO_MEMORY;
                    }
                    heapUsed = true;
                }
            
                if (user_memcpy(kSrcBuffer, srcOrig.iov_base, len) != B_OK) {
                    if (heapUsed) { free(kSrcBuffer); free(kDstBuffer); }
                    return B_BAD_ADDRESS;
                }
                
                
                // Usiamo il const_cast solo per il tempo della chiamata al driver.
                // Siccome siamo nel Kernel e abbiamo il lock, è sicuro.
                const_cast<iovec&>(srcOrig).iov_base = kSrcBuffer;
                dstOrig.iov_base = kDstBuffer;
                
                // 3. ESECUZIONE DEL DRIVER
                st = algo->Process(request);
                
                if (st == B_OK) {
                	if (user_memcpy(oldDstBase, kDstBuffer, len) != B_OK)
                        st = B_BAD_ADDRESS;
                }
                
                secure_memzero(kSrcBuffer, len);
                secure_memzero(kDstBuffer, len);
                if (heapUsed) { free(kSrcBuffer); free(kDstBuffer); }
            } else {
                /* faster should be inline */
                uint8 stackBuffer[kStackThreshold]; // Buffer pre-allocato sullo stack
                uint8* kBuffer = nullptr;
                bool usedHeap = false;
                
                if (len <= kStackThreshold) {
                    kBuffer = stackBuffer;
                } else {
                	kBuffer = (uint8*)malloc(len);
                    if (kBuffer == NULL) return B_NO_MEMORY;
                    usedHeap = true;
                }

                if (user_memcpy(kBuffer, oldSrcBase, len) != B_OK) {
                    if (usedHeap) free(kBuffer);
                    return B_BAD_ADDRESS;
                }
                
                // Usiamo il const_cast solo per il tempo della chiamata al driver.
                // Siccome siamo nel Kernel e abbiamo il lock, è sicuro.
                const_cast<iovec&>(srcOrig).iov_base = kBuffer;
            	dstOrig.iov_base = kBuffer;
            	
            	// 3. ESECUZIONE DEL DRIVER
                st = algo->Process(request);
                
                if (st == B_OK) {
                	if (user_memcpy(oldDstBase, kBuffer, len) != B_OK)
                        st = B_BAD_ADDRESS;
                }
                
                // Pulizia di sicurezza (Sensitive data)
                secure_memzero(kBuffer, len);
                if (usedHeap) free(kBuffer);
            }
            // Ripristiniamo i puntatori originali per l'utente/callback
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
