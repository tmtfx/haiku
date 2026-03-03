/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include "BCryptoCore.h"
#include <crypto/BCryptoDefs.h>
#include "BCryptoCPU.h"
#include <string.h>
#include <crypto/BCryptoKernelInternal.h>
#include "BCryptoDevice.h" 

#include <lock.h>
#include <util/AutoLock.h>
#include <util/DoublyLinkedList.h>
#include <new>
#include <debug.h>
#include <malloc.h>

using BPrivate::AutoLocker;

void bcrypto_save_regs(BCryptoFPUContext* ctx) {
    // Verifica di sicurezza: l'indirizzo DEVE essere allineato a 64 byte
    if (((uintptr_t)ctx->state & 63) != 0) {
        return; // O gestisci l'errore: XSAVE crasherebbe qui
    }

    if (gHasXsave) {
        // Usiamo l'assembly inline per invocare XSAVE
        // edx:eax contiene la maschera dei registri da salvare
        uint32 low = (uint32)gXsaveMask;
        uint32 high = (uint32)(gXsaveMask >> 32);
        
        /*asm volatile (
            "xsave %[state]"
            : : [state] "m" (ctx->state), "a" (low), "d" (high)
            : "memory"
        );*/
        /* valutare se è meglio:
        asm volatile (
            "xsave %[state]"
            : "=m" (ctx->state)  // Diciamo a GCC che scriviamo in memoria
            : [state] "m" (ctx->state), "a" (low), "d" (high)
            : "memory"
        );*/
        asm volatile (
            "xsave %[state]"
            : "=m" (*ctx) // Diciamo a GCC: "Guarda che scrivo nella memoria puntata da ctx"
            : [state] "m" (*ctx), "a" (low), "d" (high)
            : "memory"
        );
    } else {
        /*asm volatile (
            "fxsave %[state]"
            : : [state] "m" (ctx->state)
            : "memory"
        );*/
        /* valutare se è meglio:*/
        asm volatile (
            "fxsave %[state]"
            : "=m" (ctx->state)
            : [state] "m" (ctx->state)
            : "memory"
        );
    }
}

void bcrypto_restore_regs(BCryptoFPUContext* ctx) {
    if (gHasXsave) {
        uint32 low = (uint32)gXsaveMask;
        uint32 high = (uint32)(gXsaveMask >> 32);
        
        asm volatile (
            "xrstor %[state]"
            : : [state] "m" (ctx->state), "a" (low), "d" (high)
            : "memory"
        );
    } else {
        asm volatile (
            "fxrstor %[state]"
            : : [state] "m" (ctx->state)
            : "memory"
        );
    }
}

struct MutexLocking {
    typedef mutex* Lockable;
    static inline status_t Lock(Lockable lock) { return mutex_lock(lock); }
    static inline void Unlock(Lockable lock) { mutex_unlock(lock); }
};

static mutex sCryptoLock;
static bool sCoreInitialized = false;

struct AlgoNode : DoublyLinkedListLinkImpl<AlgoNode> {
    BCryptoAlgorithm* algo;
};

static DoublyLinkedList<AlgoNode> sAlgorithms;
static uint32 sCryptoCapabilities = 0;

status_t
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

void
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


static AlgoNode*
_FindAlgorithm(BCryptoAlgorithmID id, BCryptoMode mode)
{
    DoublyLinkedList<AlgoNode>::Iterator it = sAlgorithms.GetIterator();
    while (AlgoNode* node = it.Next()) {
        if (node->algo->algorithm == id) {
            if (mode == B_CRYPTO_MODE_ANY || (node->algo->mode & mode) != 0)
                return node;
        }
    }
    return NULL;
}

// Ritorna B_OK se l'algoritmo esiste, B_ENTRY_NOT_FOUND altrimenti
status_t
BCheckAlgorithmAvailability(BCryptoAlgorithmID id)
{
    MutexLocker _(sCryptoLock);
    
    DoublyLinkedList<AlgoNode>::Iterator it = sAlgorithms.GetIterator();
    while (AlgoNode* node = it.Next()) {
        if (node->algo->algorithm == id)
            return B_OK;
    }
    
    return B_ENTRY_NOT_FOUND;
}

// Riempie la struct info basandosi sul cookie (indice)
status_t
BGetAlgorithmInfo(BCryptoAlgorithmInfo* info)
{
    if (info == NULL)
        return B_BAD_VALUE;

    MutexLocker _(sCryptoLock);
    
    uint32 target = info->cookie;
    uint32 current = 0;

    DoublyLinkedList<AlgoNode>::Iterator it = sAlgorithms.GetIterator();
    while (AlgoNode* node = it.Next()) {
        if (current == target) {
            info->id = node->algo->algorithm;
            info->flags = node->algo->flags;
            strlcpy(info->vendor, node->algo->name, sizeof(info->vendor));
            
            // Prepariamo il cookie per la chiamata successiva
            info->cookie = target + 1;
            return B_OK;
        }
        current++;
    }

    return B_ENTRY_NOT_FOUND;
}

/*   NOTE TODO
 * Il "Dangling Pointer" in BRegisterCryptoAlgorithm
 *
 * Nella tua funzione di registrazione, aggiungi un nodo alla lista 
 * ma non copi la struttura dell'algoritmo.
 *
 *     node->algo = algorithm; // ERRORE POTENZIALE
 *
 * Se il driver che registra l'algoritmo ha dichiarato BCryptoAlgorithm come 
 * variabile locale (sullo stack) o se il driver viene scaricato, node->algo 
 * punterà a memoria non valida. Soluzione: Dato che nel driver abbiamo usato 
 * static, per ora funziona, ma per un Core robusto sarebbe meglio fare una 
 * copia della struttura o assicurarsi che il driver resti in memoria.
 */
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
BHashInit(crypto_session* session)
{
    MutexLocker _(sCryptoLock);
    AlgoNode* node = _FindAlgorithm(session->algorithm, B_CRYPTO_MODE_ANY);
    if (!node || !node->algo->HashInit) return B_NOT_SUPPORTED;

    status_t st = node->algo->HashInit(&session->algorithm_state, &session->state_size);
    if (st == B_OK) {
    	/* O blocco la memoria del contesto
    	 * o forzo in update la residenza in memoria del contesto */
    	// BLOCCO DELLA MEMORIA DEL CONTESTO
        // Usiamo B_READ_DEVICE | B_WRITE_DEVICE perché XSAVE scrive e XRSTOR legge.
        st = lock_memory(session->algorithm_state, session->state_size, B_READ_DEVICE);
        
        if (st != B_OK) {
            // Se il lock fallisce, dobbiamo pulire lo stato appena creato dal driver
            // (Assumendo che tu abbia una funzione di cleanup o che il driver gestisca l'errore)
            return st; 
        }
        // ---------------------------------- //
        session->is_active = true;
    }
    return st;
}
status_t
BHashUpdate(crypto_session* session, BCryptoUserRequest* request)
{
    MutexLocker _(sCryptoLock);
    AlgoNode* node = _FindAlgorithm(session->algorithm, B_CRYPTO_MODE_ANY);
    if (!node || !node->algo->HashUpdate) return B_NOT_SUPPORTED;
    
    // Lock della memoria sorgente (chunk attuale)
    status_t st = B_OK;
    size_t lockedCount = 0;
    for (size_t i = 0; i < request->vectorCount; i++) {
        st = lock_memory(request->source[i].iov_base, request->source[i].iov_len, B_READ_DEVICE);
        if (st != B_OK) break;
        lockedCount++;
    }

    if (st == B_OK) {
    	/* O forzo la residenza in memoria del contesto FPU
    	 * o faccio lock_memory sul contesto in BHashInit */
        // FORZA la residenza della memoria del contesto FPU
        // Scriviamo uno zero (o leggiamo) nel primo e ultimo byte della struct
        // per assicurarci che il kernel carichi la pagina PRIMA di disabilitare gli interrupt.
        //volatile uint8* fpu_ptr = (volatile uint8*)&((SoftSHA256Context*)session->algorithm_state)->fpu_save;
        //fpu_ptr[0] = fpu_ptr[0]; 
        //fpu_ptr[sizeof(BCryptoFPUContext) - 1] = fpu_ptr[sizeof(BCryptoFPUContext) - 1];
        
        UserAccessExposer access;
        st = node->algo->HashUpdate(session->algorithm_state, request->source, request->vectorCount);
    }

    // Sblocchiamo subito
    for (size_t i = 0; i < lockedCount; i++) {
        unlock_memory(request->source[i].iov_base, request->source[i].iov_len, B_READ_DEVICE);
    }

    return st;
}
status_t
BHashFinal(crypto_session* session, BCryptoUserRequest* request)
{
	if (session == NULL || !session->is_active)
        return B_BAD_VALUE;
        
    MutexLocker _(sCryptoLock);
    AlgoNode* node = _FindAlgorithm(session->algorithm, B_CRYPTO_MODE_ANY);
    if (!node || !node->algo->HashFinal) return B_NOT_SUPPORTED;
    
    uint8 tempDigest[64]; // Massimo per SHA-512
    status_t st = node->algo->HashFinal(session->algorithm_state, tempDigest);

    if (st == B_OK) {
        size_t hashLen = decode_hash_length(session->algorithm);
        // Copiamo il risultato nel buffer di destinazione dell'utente
        // Assumiamo che request->destination[0] sia valido
        st = lock_memory(request->destination[0].iov_base, hashLen, B_READ_DEVICE);
        if (st == B_OK) {
            /*__asm__ __volatile__ ("stac" : : : "cc");//user_access_enable();
            memcpy(request->destination[0].iov_base, tempDigest, hashLen);
            __asm__ __volatile__ ("clac" : : : "cc");//user_access_disable();*/
            {
            	UserAccessExposer access;
            	memcpy(request->destination[0].iov_base, tempDigest, hashLen);
            }
            unlock_memory(request->destination[0].iov_base, hashLen, B_READ_DEVICE);
        }
    }
    // 3. CLEANUP DELLA SESSIONE
    // Sblocchiamo la memoria del contesto che avevamo lockato in BHashInit
    if (session->algorithm_state != NULL) {
        unlock_memory(session->algorithm_state, session->state_size, B_READ_DEVICE);
    }

    // Segniamo la sessione come non più attiva
    session->is_active = false;

    return st;
}


status_t
BSubmitCryptoRequest(BCryptoRequest* request)
{
    if (!request) {
        return B_BAD_VALUE;
    }

    MutexLocker _(sCryptoLock);
    
    AlgoNode* node = _FindAlgorithm(request->algorithm, request->mode);
    if (!node)
        return B_NOT_SUPPORTED;
    
    BCryptoAlgorithm* algo = node->algo;
    
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
    
    if (request->operation == B_CRYPTO_DIGEST) {
    	/*  HARD WAY */
        status_t st = B_OK;
        size_t lockedCount = 0;

        // Lock sorgenti
        for (size_t i = 0; i < request->vectorCount; i++) {
            st = lock_memory(request->source[i].iov_base, request->source[i].iov_len, B_READ_DEVICE);
            if (st != B_OK) break;
            lockedCount++;
        }

        if (st == B_OK) {
            // Lock destinazione
            st = lock_memory(request->destination[0].iov_base, request->destination[0].iov_len, B_READ_DEVICE);
            if (st == B_OK) {
            	{
            		UserAccessExposer access;
            		st = algo->Process(request); 
                }
                unlock_memory(request->destination[0].iov_base, request->destination[0].iov_len, B_READ_DEVICE);
            }
        }

        // Unlock sorgenti (solo quelli effettivamente lockati)
        for (size_t i = 0; i < lockedCount; i++) {
            unlock_memory(request->source[i].iov_base, request->source[i].iov_len, B_READ_DEVICE);
        }
            
        return _FinalizeRequest(request, st);
        /* DEBUG WAY 
        status_t st = B_OK;
        size_t hashLen = decode_hash_length(request->algorithm);
    
        // 1. Alloca buffer kernel per l'input (supponendo vectorCount 1 per il test)
        // Se hai più vettori, dovresti sommare le lunghezze e concatenarli.
        void* kSrc = malloc(request->source[0].iov_len);
        if (kSrc == NULL) return B_NO_MEMORY;

        // 2. Alloca buffer kernel per il digest di output
        uint8 kDest[64]; // Massimo per SHA512/BLAKE2b

        // 3. Copia i dati dall'utente al kernel (gestisce SMAP in sicurezza)
        if (user_memcpy(kSrc, request->source[0].iov_base, request->source[0].iov_len) != B_OK) {
            free(kSrc);
            return B_BAD_ADDRESS;
        }

        // 4. Prepara la richiesta "Kernel-to-Kernel"
        iovec kSrcVec = { kSrc, request->source[0].iov_len };
        iovec kDestVec = { kDest, hashLen };
    
        BCryptoRequest kReq = *request;
        kReq.source = &kSrcVec;
        kReq.destination = &kDestVec;
        kReq.vectorCount = 1;

        // 5. Esegui il driver (niente UserAccessExposer qui, è memoria kernel!)
        st = algo->Process(&kReq);

        // 6. Se è andata bene, riporta il digest all'utente
        if (st == B_OK) {
            if (user_memcpy(request->destination[0].iov_base, kDest, hashLen) != B_OK)
                st = B_BAD_ADDRESS;
        }

        free(kSrc);
        return _FinalizeRequest(request, st);*/
    } else {
        status_t st = B_OK;
        for (size_t i = 0; i < request->vectorCount; i++) {
            const iovec& srcOrig = request->source[i];
            iovec& dstOrig       = request->destination[i];
            size_t len = srcOrig.iov_len;

            if (len == 0) continue;
    
            // 1. Blocchiamo e mappiamo la memoria dell'utente
            // lock_memory assicura che le pagine non vengano spostate o mandate in swap
            st = lock_memory(srcOrig.iov_base, len, B_READ_DEVICE);
            if (st != B_OK) return st;

            // Se sorgente e destinazione sono diverse, blocchiamo anche la destinazione
            bool separateDest = (srcOrig.iov_base != dstOrig.iov_base);
            if (separateDest) {
                st = lock_memory(dstOrig.iov_base, len, B_READ_DEVICE);
                if (st != B_OK) {
                    unlock_memory(srcOrig.iov_base, len, B_READ_DEVICE);
                    return st;
                }
            }
            //__asm__ __volatile__ ("stac" : : : "cc");//user_access_enable();
            {
                UserAccessExposer access;
                // 2. Prepariamo una richiesta locale "Direct"
                // AES-NI può lavorare direttamente sull'indirizzo utente se siamo nel contesto del thread chiamante
                BCryptoRequest localReq = *request;
                //localReq.source = const_cast<iovec*>(&srcOrig);
                //localReq.destination = &dstOrig;
                localReq.source = &request->source[i];
                localReq.destination = &request->destination[i];
                localReq.vectorCount = 1;
    
                // 3. ESECUZIONE DEL DRIVER (AES-NI)
                // Ora il driver riceve puntatori che puntano alla RAM fisica dell'utente
                st = algo->Process(&localReq);
            }
            //__asm__ __volatile__ ("clac" : : : "cc");//user_access_disable();
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
    
    req.operation = B_CRYPTO_RANDOM; // O una costante B_CRYPTO_GENERATE
    req.algorithm = B_CRYPTO_RNG;
    req.destination = &vec;
    req.source = req.destination;
    req.vectorCount = 1;
    req.flags = B_CRYPTO_ALG_HW_ACCEL | B_CRYPTO_ALG_KERNEL_SPACE ; // Vogliamo solo hardware!
    
    // Passiamo per la logica ufficiale, così se c'è PadLock o RDRAND, 
    // il Core sceglie quello a priorità maggiore.
    return BSubmitCryptoRequest(&req);
}

