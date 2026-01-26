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
	//dprintf("BCrypto: Richiesta giunta in BSubmitCryptoRequest\n");
	bool slowFast = false;
	
    if (!request) {
    	//dprintf("BCrypto: non c'è richiesta!\n");
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
        // Qui usiamo il loop SMAP che abbiamo perfezionato
        status_t st = B_OK;

        for (size_t i = 0; i < request->vectorCount; i++) {
            // Usiamo riferimenti locali ma non modifichiamo l'originale se const
            const iovec& srcOrig = request->source[i];
            iovec& dstOrig       = request->destination[i];
            
            if (srcOrig.iov_len == 0) continue;
            if (slowFast) {
                /* slower safer */
                uint8* kSrcBuffer = (uint8*)malloc(srcOrig.iov_len);
                uint8* kDstBuffer = (uint8*)malloc(srcOrig.iov_len);

                if (kSrcBuffer == NULL || kDstBuffer == NULL) {
                    free(kSrcBuffer);
                    free(kDstBuffer);
                    return B_NO_MEMORY;
                }
            
                if (user_memcpy(kSrcBuffer, srcOrig.iov_base, srcOrig.iov_len) != B_OK) {
                    free(kSrcBuffer);
                    free(kDstBuffer);
                    return B_BAD_ADDRESS;
                }
                // Salviamo gli indirizzi utente originali
                void* oldSrcBase = srcOrig.iov_base;
                void* oldDstBase = dstOrig.iov_base;
                
                // Usiamo il const_cast solo per il tempo della chiamata al driver.
                // Siccome siamo nel Kernel e abbiamo il lock, è sicuro.
                const_cast<iovec&>(srcOrig).iov_base = kSrcBuffer;
                dstOrig.iov_base = kDstBuffer;
                
                // 3. ESECUZIONE DEL DRIVER
                st = algo->Process(request);
                
                if (st == B_OK) {
                	if (user_memcpy(oldDstBase, kDstBuffer, srcOrig.iov_len) != B_OK)
                        st = B_BAD_ADDRESS;
                }
                
                secure_memzero(kSrcBuffer, srcOrig.iov_len);
                secure_memzero(kDstBuffer, srcOrig.iov_len);
                free(kSrcBuffer);
                free(kDstBuffer);
                
                const_cast<iovec&>(srcOrig).iov_base = oldSrcBase;
                dstOrig.iov_base = oldDstBase;
                
                if (st != B_OK) {
            	    //dprintf("BCrypto: l'elaborazione hybrid/sincrona non ha avuto successo\n");
            	    break;
                }
            } else {
                /* faster should be inline */
            
                uint8* kBuffer = (uint8*)malloc(srcOrig.iov_len);
                if (kBuffer == NULL) {
                	return B_NO_MEMORY;
                }

                if (user_memcpy(kBuffer, srcOrig.iov_base, srcOrig.iov_len) != B_OK) {
                    free(kBuffer);
                    return B_BAD_ADDRESS;
                }
                // Salviamo gli indirizzi utente originali
                void* oldSrcBase = srcOrig.iov_base;
                void* oldDstBase = dstOrig.iov_base;
                
                // Usiamo il const_cast solo per il tempo della chiamata al driver.
                // Siccome siamo nel Kernel e abbiamo il lock, è sicuro.
                const_cast<iovec&>(srcOrig).iov_base = kBuffer;
            	dstOrig.iov_base = kBuffer;
            	
            	// 3. ESECUZIONE DEL DRIVER
                st = algo->Process(request);
                
                if (st == B_OK) {
                	user_memcpy(oldDstBase, kBuffer, srcOrig.iov_len);
                }
                secure_memzero(kBuffer, srcOrig.iov_len);
                free(kBuffer);
                
                const_cast<iovec&>(srcOrig).iov_base = oldSrcBase;
                dstOrig.iov_base = oldDstBase;
                if (st != B_OK) {
            	    //dprintf("BCrypto: l'elaborazione hybrid/sincrona non ha avuto successo\n");
            	    break;
                }
            }

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
