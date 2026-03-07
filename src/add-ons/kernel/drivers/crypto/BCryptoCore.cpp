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

bool bcrypto_save_regs(BCryptoFPUContext* ctx) {
	// Verifica di sicurezza: l'indirizzo DEVE essere allineato a 64 byte
	if (((uintptr_t)ctx & 63) != 0) {
        return false; 
    }
    // oppure così, ma è più lenta:
    //if (((uintptr_t)ctx % 64) != 0) {
    //    return false; 
    //}
    // questo nel caso dovessi aggiungere altri campi prima
    //if (((uintptr_t)ctx->state & 63) != 0) {
    //    return false; // O gestisci l'errore: XSAVE crasherebbe qui
    //}
    if (gXsaveSize > B_MAX_XSAVE_SIZE) {
        dprintf("BCRYPTO: La CPU richiede %u byte, ma il buffer è di soli %d!", 
              gXsaveSize, B_MAX_XSAVE_SIZE);
        return false;
    }

    if (gHasXsave) {
        // Usiamo l'assembly inline per invocare XSAVE
        // edx:eax contiene la maschera dei registri da salvare
        uint32 low = (uint32)gXsaveMask;
        uint32 high = (uint32)(gXsaveMask >> 32);

        // valutare se è meglio:
        //asm volatile (
        //    "xsave %[state]"
        //    : "=m" (ctx->state)  // Diciamo a GCC che scriviamo in memoria
        //    : [state] "m" (ctx->state), "a" (low), "d" (high)
        //    : "memory"
        //);
        
        //asm volatile (
        //    "xsave %[state]"
        //    : "=m" (*ctx) // Diciamo a GCC: "Guarda che scrivo nella memoria puntata da ctx"
        //    : [state] "m" (*ctx), "a" (low), "d" (high)
        //    : "memory"
        //);
        asm volatile (
            "xsave (%[ptr])"
            : : [ptr] "r" (ctx), "a" (low), "d" (high)
            : "memory"
        );
    } else {
        //asm volatile (
        //    "fxsave %[state]"
        //    : "=m" (ctx->state)
        //    : [state] "m" (ctx->state)
        //    : "memory"
        //);
        asm volatile (
            "fxsave (%[ptr])"
            : : [ptr] "r" (ctx)
            : "memory"
        );
    }
    return true;
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
        
        // Se state_size è sospettosamente piccolo (es. < 1024), sospetta un errore nel driver
        //if (session->state_size < 1024) {
        //     dprintf("BCRYPTO: ATTENZIONE! state_size molto piccolo, XSAVE potrebbe sforare!\n");
        //}
        
        st = lock_memory(session->algorithm_state, session->state_size, B_READ_DEVICE);
        
        if (st != B_OK) {
            // Se il lock fallisce, dobbiamo pulire lo stato appena creato dal driver
            // (Assumendo che tu abbia una funzione di cleanup o che il driver gestisca l'errore)
            bcrypto_secure_memzero(session->algorithm_state, session->state_size);
            free(session->algorithm_state);
            session->algorithm_state = NULL;
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
        
        //volatile uint8* fpu_ptr = (volatile uint8*)session->algorithm_state;
        //size_t size = session->state_size;

        // Tocchiamo l'inizio di ogni pagina coinvolta nel contesto
        //for (size_t offset = 0; offset < size; offset += 4096) {
        //    fpu_ptr[offset] = fpu_ptr[offset];
        //}
        
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
    // 1. Controllo base: se non c'è la sessione, non possiamo fare nulla.
    if (session == NULL)
        return B_BAD_VALUE;

    MutexLocker _(sCryptoLock);
    status_t st = B_OK;

    // 2. FASE DI CALCOLO (Solo se la sessione è attiva e abbiamo una richiesta)
    // Se request è NULL, saltiamo questa parte e andiamo dritti al CLEANUP.
    if (session->is_active && session->algorithm_state != NULL) {
        AlgoNode* node = _FindAlgorithm(session->algorithm, B_CRYPTO_MODE_ANY);
        
        if (node && node->algo->HashFinal) {
            uint8 tempDigest[64]; // Buffer sicuro sullo stack
            st = node->algo->HashFinal(session->algorithm_state, tempDigest);

            // Copiamo il digest all'utente solo se ci è stata passata una richiesta valida
            if (st == B_OK && request != NULL && request->destination != NULL) {
                size_t hashLen = decode_hash_length(session->algorithm);
                
                // Protezione SMAP e Lock per la memoria utente
                if (lock_memory(request->destination[0].iov_base, hashLen, B_READ_DEVICE) == B_OK) {
                    UserAccessExposer access;
                    memcpy(request->destination[0].iov_base, tempDigest, hashLen);
                    unlock_memory(request->destination[0].iov_base, hashLen, B_READ_DEVICE);
                } else {
                    st = B_BAD_ADDRESS;
                }
            }
        } else {
            st = B_NOT_SUPPORTED;
        }
    }

    // --- 3. CLEANUP OBBLIGATORIO (Il cuore della stabilità) ---
    // Gestiamo la memoria allocata in BHashInit indipendentemente dall'esito del calcolo.
    
    if (session->algorithm_state != NULL) {
        // Sblocco simmetrico (toglie il 'wired' dalle pagine)
        unlock_memory(session->algorithm_state, session->state_size, B_READ_DEVICE);
        
        // Pulizia dati sensibili
        bcrypto_secure_memzero(session->algorithm_state, session->state_size);
        
        // Restituzione allo Slab Allocator del Kernel
        free(session->algorithm_state);
        
        // Neutralizzazione dei puntatori per evitare Use-After-Free
        session->algorithm_state = NULL;
    }

    session->is_active = false;

    return st;
}
status_t
BStreamInit(crypto_session* session, BCryptoRequest* req)
{
	MutexLocker _(sCryptoLock);
    //BCryptoAlgorithm* algo = _FindAlgorithm(session->algorithm, session->mode);
    AlgoNode* node = _FindAlgorithm(session->algorithm, session->mode);
    //if (!node || !node->algo->StreamInit) return B_NOT_SUPPORTED;
    if (!node) return B_NOT_SUPPORTED;
    if (node->algo->StreamInit == nullptr) {
        dprintf("CryptoCore: Errore - l'algoritmo %d non supporta lo streaming!\n", session->algorithm);
        return B_NOT_SUPPORTED;
    }

    void* context = nullptr;
    size_t actualSize = 0;

    // Chiamiamo l'inizializzazione dell'algoritmo
    // StreamInit allocherà la struct corretta (SoftAESContext o AESNIContext)
    //status_t status = node->algo->StreamInit(&context, &actualSize, (const uint8*)req->key, req->keyLength, (const uint8*)req->iv, req->ivLength);
    // 1. Abilitiamo l'accesso alla memoria utente per leggere Key e IV
    status_t status;
    {
        UserAccessExposer access;
        status = node->algo->StreamInit(&context, &actualSize, 
            req->operation, 
            (const uint8*)req->key, req->keyLength, 
            (const uint8*)req->iv, req->ivLength);
    }
    if (status != B_OK) return status;

    // Proteggiamo la memoria del contesto (contiene le chiavi!)
    status = lock_memory(context, actualSize, B_READ_DEVICE);
    if (status != B_OK) {
        dprintf("CryptoCore: lock_memory fallito (%s), abortisco.\n", strerror(status));
        // Se il lock fallisce, puliamo e liberiamo subito per sicurezza
        bcrypto_secure_memzero(context, actualSize);
        free(context);
        return status;
    }
    session->algorithm_state = context;
    session->state_size = actualSize;
    session->is_active = true;
    return B_OK;
}
status_t
BStreamUpdate(crypto_session* session, BCryptoRequest* req)
{
	MutexLocker _(sCryptoLock);
    //BCryptoAlgorithm* algo = _FindAlgorithm(session->algorithm, session->mode);
    AlgoNode* node = _FindAlgorithm(session->algorithm, session->mode);
    if (!node || !node->algo->StreamUpdate) return B_NOT_SUPPORTED;
    
    status_t st = B_OK;
    size_t lockedSrcCount = 0;
    size_t lockedDstCount = 0;

    // 1. Blocco memoria SORGENTE (Lettura)
    for (size_t i = 0; i < req->vectorCount; i++) {
        st = lock_memory(req->source[i].iov_base, req->source[i].iov_len, B_READ_DEVICE);
        if (st != B_OK) break;
        lockedSrcCount++;
    }
    // 2. Blocco memoria DESTINAZIONE (Scrittura)
    if (st == B_OK) {
        for (size_t i = 0; i < req->vectorCount; i++) {
            // Nota: per la destinazione usiamo 0 o B_WRITE se disponibile, 
            // ma spesso B_READ_DEVICE è sufficiente per il lock fisico
            st = lock_memory(req->destination[i].iov_base, req->destination[i].iov_len, 0);
            if (st != B_OK) break;
            lockedDstCount++;
        }
    }
    
    if (st == B_OK) {
        UserAccessExposer access; // Disabilita SMAP tramite AC flag
        st = node->algo->StreamUpdate(session->algorithm_state, 
                                      req->source, 
                                      req->destination, 
                                      req->vectorCount);
    }

    // 4. Cleanup (Unlock) in ordine inverso o speculare
    for (size_t i = 0; i < lockedDstCount; i++) {
        unlock_memory(req->destination[i].iov_base, req->destination[i].iov_len, 0);
    }
    for (size_t i = 0; i < lockedSrcCount; i++) {
        unlock_memory(req->source[i].iov_base, req->source[i].iov_len, B_READ_DEVICE);
    }

    return st;

    // Passiamo il contesto salvato nella sessione
    //return node->algo->StreamUpdate(session->algorithm_state, req->source, req->destination, req->vectorCount); fa SMAP
    //return node->algo->StreamUpdate(session->algorithm_state, (const iovec*)req->source, (const iovec*)req->destination, req->vectorCount); se da problemi di cast, fa SMAP
}
status_t
BStreamFinal(crypto_session* session, BCryptoRequest* req)
{
	MutexLocker _(sCryptoLock);
    //BCryptoAlgorithm* algo = _FindAlgorithm(session->algorithm, session->mode);
    AlgoNode* node = _FindAlgorithm(session->algorithm, session->mode);
    if (!node || !node->algo->StreamFinal) return B_NOT_SUPPORTED;

    uint8 tag[16];
    status_t status = node->algo->StreamFinal(session->algorithm_state, tag);

    if (status == B_OK && req != nullptr && req->destination != nullptr) {
        // Copiamo il tag generato nel buffer di destinazione dell'utente
        // Usiamo memcpy sicura perché req->destination è già stato validato nell'ioctl
        //memcpy((uint8*)req->destination[0].iov_base, tag, 16); SMAP/page fault
        status_t lockStatus = lock_memory(req->destination[0].iov_base, 16, 0);
        
        if (lockStatus == B_OK) {
            // 2. Apriamo la porta SMAP per la copia
            UserAccessExposer access;
            memcpy((uint8*)req->destination[0].iov_base, tag, 16);
            
            unlock_memory(req->destination[0].iov_base, 16, 0);
        } else {
            status = lockStatus;
        }
    }

    // Pulizia di sicurezza
    bcrypto_secure_memzero(session->algorithm_state, session->state_size);
    //secure_memzero(session->algorithm_state, get_context_size(session->algorithm));
    unlock_memory(session->algorithm_state, session->state_size, B_READ_DEVICE);
    free(session->algorithm_state);

    session->algorithm_state = nullptr;
    session->state_size = 0;
    session->is_active = false;
    
    return status;
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
        size_t lockedSrcCount = 0;
        size_t lockedDstCount = 0;

        // Lock sorgenti
        // 1. LOCK: Proteggiamo i buffer
        for (size_t i = 0; i < request->vectorCount; i++) {
            st = lock_memory(request->source[i].iov_base, request->source[i].iov_len, B_READ_DEVICE);
            if (st != B_OK) break;
            lockedSrcCount++;
    
            if (request->source[i].iov_base != request->destination[i].iov_base) {
                st = lock_memory(request->destination[i].iov_base, request->destination[i].iov_len, B_READ_DEVICE);
                if (st != B_OK) break;
                lockedDstCount++;
            }
        }

        // 2. ESECUZIONE: Chiamiamo il driver ORA che la memoria è al sicuro
        if (st == B_OK) {
            UserAccessExposer access;
            st = algo->Process(request);
        }
        
        for (size_t i = 0; i < lockedSrcCount; i++)
             unlock_memory(request->source[i].iov_base, request->source[i].iov_len, B_READ_DEVICE);
        for (size_t i = 0; i < lockedDstCount; i++)
             unlock_memory(request->destination[i].iov_base, request->destination[i].iov_len, B_READ_DEVICE);
            
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
    	if (request->mode == B_CRYPTO_MODE_GCM) {
    		// --- GESTIONE COALESCING (Se troppi vettori o frammentazione eccessiva) ---
    		// NB: i vettori solitamente non superano qualche megabyte totali per singola chiamata ioctl
    		//     pertanto questa pratica è per lo più pratica e sicura tuttavia, se avessimo gigabyte
    		//     di dati, malloc nel kernel potrebbe fallire o frammentare la memoria!
    		if (request->vectorCount > 32) {
    		    size_t totalLen = 0;
    		    for (size_t i = 0; i < request->vectorCount; i++) totalLen += request->source[i].iov_len;
    		
    		    void* bounceBuffer = malloc(totalLen);
    		    if (!bounceBuffer) return B_NO_MEMORY;
    		
    		    // Pack dei dati
    		    size_t offset = 0;
    		    for (size_t i = 0; i < request->vectorCount; i++) {
    		        if (user_memcpy((uint8*)bounceBuffer + offset, request->source[i].iov_base, request->source[i].iov_len) != B_OK) {
    		            free(bounceBuffer);
    		            return B_BAD_ADDRESS;
    		        }
    		        offset += request->source[i].iov_len;
    		    }
    		
    		    // Prepariamo richiesta singola per il driver
    		    iovec kVec = { bounceBuffer, totalLen };
    		    BCryptoRequest kReq = *request;
    		    kReq.source = &kVec;
    		    kReq.destination = &kVec;
    		    kReq.vectorCount = 1;

    		    {
    		        UserAccessExposer access;
    		        st = algo->Process(&kReq);
    		    }

    		    // Unpack dei dati (solo se operazione riuscita)
    		    if (st == B_OK) {
    		        offset = 0;
    		        for (size_t i = 0; i < request->vectorCount; i++) {
    		            if (user_memcpy(request->destination[i].iov_base, (uint8*)bounceBuffer + offset, request->destination[i].iov_len) != B_OK) {
    		                st = B_BAD_ADDRESS;
    		                break;
    		            }
    		            offset += request->destination[i].iov_len;
    		        }
    		    }
    		    free(bounceBuffer);
    		    return _FinalizeRequest(request, st);
    		}
            // GCM deve essere processato in un colpo solo
            // 1. Lock memory per TUTTI i vettori (Dati + Tag)
            // --- GESTIONE ZERO-COPY CON ROLLBACK SICURO ---
            size_t lockedCount = 0;
            bool lockedDest[32] = { false };
            for (size_t i = 0; i < request->vectorCount; i++) {
                st = lock_memory(request->source[i].iov_base, request->source[i].iov_len, B_READ_DEVICE);
                if (st != B_OK) goto rollback;
        
                lockedCount = i + 1; // Segnamo che questo indice source è lockato

                if (request->source[i].iov_base != request->destination[i].iov_base && request->destination[i].iov_base != NULL) {
                    st = lock_memory(request->destination[i].iov_base, request->destination[i].iov_len, B_READ_DEVICE);
                    if (st != B_OK) goto rollback;
                    lockedDest[i] = true;
                }
            }

            // 2. Chiamata diretta al driver con l'intera catena di iovec
            {
                UserAccessExposer access;
                st = algo->Process(request); // Il driver GCM gestirà vectorCount internamente
            }
rollback:
            for (size_t i = 0; i < lockedCount; i++) {
                unlock_memory(request->source[i].iov_base, request->source[i].iov_len, B_READ_DEVICE);
                if (lockedDest[i]) {
                    unlock_memory(request->destination[i].iov_base, request->destination[i].iov_len, B_READ_DEVICE);
                }
            }

            if (st != B_OK) return st;
            return _FinalizeRequest(request, st);
        } else {
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

