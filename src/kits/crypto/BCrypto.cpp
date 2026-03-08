/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include "BCrypto.h"
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include "BCryptoDefs.h"
#include <StorageDefs.h>
#include <DataIO.h>
#include <cstdio>
#include <new>

#include <vector>
#include <algorithm>
#define MAX_KERNEL_VECTORS 32
#define COALESCE_THRESHOLD (128 * 1024) // 128 KB


BCrypto::BCrypto() : fFd(-1),
      fPaddingEnabled(true),        // Di default lo abilitiamo (scelta sicura)
      fPaddingType(B_CRYPTO_PKCS7), // Standard universale
      fAlgorithm(B_CRYPTO_AES),    // Default sensato
      fMode(B_CRYPTO_MODE_CBC)      // Default sensato
{
	//fFd = open("/dev/crypto/v1", O_RDWR);
	char path[B_PATH_NAME_LENGTH];
	snprintf(path, sizeof(path), "/dev/%s", B_CRYPTO_DEVICE_NAME);
	fFd = open(path, O_RDWR);
}

BCrypto::~BCrypto() {
	if (fFd >= 0)
		close(fFd);
}

status_t BCrypto::InitCheck() const {
	return fFd >= 0 ? B_OK : B_ERROR;
}

void BCrypto::SetAlgorithm(BCryptoAlgorithmID algo) {
    fAlgorithm = algo;
}

void BCrypto::SetMode(BCryptoMode mode) {
    fMode = mode;
}

bool BCrypto::IsAlgorithmSupported(BCryptoAlgorithmID algorithm, uint32 flags)
{
	char path[B_PATH_NAME_LENGTH];
    snprintf(path, sizeof(path), "/dev/%s", B_CRYPTO_DEVICE_NAME);
    
    int fd = open(path, O_RDWR);
	if (fd < 0) {
		printf("DEBUG: Fallita apertura device!\n");
		return B_ERROR;
	}
	if (flags == 0) {
		status_t status = ioctl(fd, B_CRYPTO_IOCTL_CHECK_ALGO, &algorithm);
		close(fd);
		return (status == B_OK);
	}
	BCryptoAlgorithmInfo info;
    memset(&info, 0, sizeof(info));
    info.cookie = 0;
    bool found = false;
    while (ioctl(fd, B_CRYPTO_IOCTL_GET_NEXT_ALGO, &info) == B_OK) {
        if (info.id == algorithm) {
            // Verifichiamo se i flag corrispondono alla richiesta
            // (Usiamo l'operatore AND per supportare richieste di flag multipli)
            if ((info.flags & flags) == flags) {
                found = true;    
            }
            break;
        }
    }
    close(fd);
    return found ? true : false;
}

status_t BCrypto::GetNextAlgorithm(uint32* cookie,BCryptoAlgorithmInfo* info)
{
	if (fFd < 0) return B_NO_INIT;
	if (info == NULL || cookie == NULL) return B_BAD_VALUE;
	//return ioctl(fFd, B_CRYPTO_IOCTL_GET_NEXT_ALGO, info);
	// Sincronizziamo il cookie della struct con quello passato dall'utente
    info->cookie = *cookie;

    // Chiamata al driver tramite il file descriptor dell'oggetto
    status_t status = ioctl(fFd, B_CRYPTO_IOCTL_GET_NEXT_ALGO, info);

    if (status == B_OK) {
        // Aggiorniamo il valore del cookie per la prossima chiamata
        *cookie = info->cookie;
    }

    return status;
}

status_t
BCrypto::GetEngineName(BCryptoAlgorithmID algo, char* outName, size_t nameSize)
{
    if (fFd < 0) return B_NO_INIT;
    if (outName == NULL || nameSize == 0) return B_BAD_VALUE;

    BCryptoAlgorithmInfo info;
    uint32 cookie = 0;
    
    // Iteriamo sugli algoritmi registrati nel core
    while (GetNextAlgorithm(&cookie, &info) == B_OK) {
        if (info.id == algo) {
            // Trovato! Essendo la lista ordinata per priorità nel Core,
            // il primo che incontriamo è quello che verrà effettivamente usato.
            strlcpy(outName, info.vendor, nameSize);
            return B_OK;
        }
    }

    return B_ENTRY_NOT_FOUND;
}

status_t
BCrypto::GetEngineName(BCryptoAlgorithmID algo, BCryptoMode mode, char* outName, size_t nameSize)
{
    if (fFd < 0) return B_NO_INIT;
    if (outName == NULL || nameSize == 0) return B_BAD_VALUE;

    BCryptoAlgorithmInfo info;
    uint32 cookie = 0;
    
    // Iteriamo sugli algoritmi registrati nel core
    while (GetNextAlgorithm(&cookie, &info) == B_OK) {
        if (info.id == algo && info.mode == mode) {
            // Trovato! Essendo la lista ordinata per priorità nel Core,
            // il primo che incontriamo è quello che verrà effettivamente usato.
            strlcpy(outName, info.vendor, nameSize);
            return B_OK;
        }
    }

    return B_ENTRY_NOT_FOUND;
}


void
BCrypto::SetPadding(bool enable,BCryptoPaddingType type){
	fPaddingEnabled=enable;
	fPaddingType=type;
}

status_t 
BCrypto::GetRandomBytes(void* buffer, size_t len)
{
    if (fFd < 0) return B_NO_INIT;
    if (buffer == NULL || len == 0) return B_BAD_VALUE;

    BCryptoRandomRequest req;
    req.buffer = buffer;
    req.length = len;

    if (ioctl(fFd, B_CRYPTO_IOCTL_GET_RANDOM, &req) < 0)
        return B_ERROR;

    return req.result;
}


size_t 
BCrypto::GetOutputSize(size_t inputLen, BCryptoOperation op)
{
    if (!fPaddingEnabled || fPaddingType == B_CRYPTO_PADDING_NONE)
        return inputLen;

    if (op == B_CRYPTO_ENCRYPT) {
       switch (fPaddingType) {
            case B_CRYPTO_PKCS7:
            case B_CRYPTO_ISO7816:
                // Arrotonda sempre al multiplo di 16 successivo
                // Se inputLen è 16, diventa 32. Se è 15, diventa 16.
                return (inputLen + 16) & ~15;
            
            case B_CRYPTO_ZERO_PADDING:
                // Solo se necessario per arrivare a 16
                return (inputLen + 15) & ~15;
                
            default:
                return inputLen;
        }
    }
    return inputLen; // Per Decrypt restituiamo la dimensione del buffer cifrato
}
ssize_t 
BCrypto::Encrypt(uint8* key, size_t keyLen, uint8* iv, size_t ivLen,
                 const void* in, size_t inLen, void* out, size_t outSize) 
{
    size_t requiredSize = GetOutputSize(inLen, B_CRYPTO_ENCRYPT);
    if (outSize < requiredSize) return B_BAD_VALUE;

    uint8* dataToProcess = (uint8*)in;
    uint8* tempBuffer = nullptr;
    size_t processLen = inLen;

    // Se il padding è attivo e necessario (o se PKCS7 richiede sempre un blocco extra)
    if (fPaddingEnabled && fPaddingType != B_CRYPTO_PADDING_NONE) {
        processLen = requiredSize;
        tempBuffer = new(std::nothrow) uint8[processLen];
        if (!tempBuffer) return B_NO_MEMORY;
        
        memcpy(tempBuffer, in, inLen);
        _ApplyPadding(tempBuffer, inLen, processLen); 
        dataToProcess = tempBuffer;
    }

    iovec src = { dataToProcess, processLen };
    iovec dst = { out, processLen };
    
    BCryptoUserRequest req;
    // Specifichiamo ALGO e MODE correttamente
    _FillRequest(req, B_CRYPTO_ENCRYPT, fAlgorithm, fMode,
                 key, keyLen, iv, ivLen, &src, &dst, 1);

    status_t st = Process(req); 
    
    if (tempBuffer) delete[] tempBuffer;
    return (st == B_OK) ? (ssize_t)processLen : st;
}
ssize_t
BCrypto::Encrypt(uint8* key, size_t keyLen, uint8* iv, size_t ivLen,
                 BDataIO* source, BDataIO* destination)
{
    if (!source || !destination) return B_BAD_VALUE;

    const size_t kBufferSize = 1024 * 1024; // 1MB
    // +16 per sicurezza per contenere l'eventuale blocco di padding extra
    uint8* inBuffer = new(std::nothrow) uint8[kBufferSize + 16];
    uint8* outBuffer = new(std::nothrow) uint8[kBufferSize + 16];
    
    if (!inBuffer || !outBuffer) {
        delete[] inBuffer; delete[] outBuffer;
        return B_NO_MEMORY;
    }

    status_t err = B_OK;
    ssize_t bytesRead;
    size_t totalWritten = 0;
    bool paddingApplied = false;
    
    while (true) {
        bytesRead = source->Read(inBuffer, kBufferSize);
        if (bytesRead < 0) { 
            err = (status_t)bytesRead; 
            break; 
        }
        
        size_t currentLen = (size_t)bytesRead;
        
        // Se leggiamo meno della capacità del buffer, significa che il file è finito.
        // Questo include il caso in cui bytesRead è 0 (file finito esattamente su 1MB).
        if (currentLen < kBufferSize) {
            if (fPaddingEnabled && fPaddingType != B_CRYPTO_PADDING_NONE) {
                // Calcola la dimensione con padding (es. se 0 -> 16, se 10 -> 16)
                size_t paddedLen = GetOutputSize(currentLen, B_CRYPTO_ENCRYPT);
                _ApplyPadding(inBuffer, currentLen, paddedLen);
                currentLen = paddedLen;
            }
            paddingApplied = true;
        }

        if (currentLen > 0) {
            iovec src = { inBuffer, currentLen };
            iovec dst = { outBuffer, currentLen };
            BCryptoUserRequest req;
            
            // Importante: 'iv' punta al buffer che il driver aggiorna 
            // automaticamente dopo ogni chiamata a Process().
            _FillRequest(req, B_CRYPTO_ENCRYPT, fAlgorithm, fMode,
                         key, keyLen, iv, ivLen, &src, &dst, 1);

            err = Process(req);
            if (err != B_OK) break;

            ssize_t written = destination->Write(outBuffer, currentLen);
            if (written < 0) { 
                err = (status_t)written; 
                break; 
            }
            totalWritten += (size_t)written;
        }

        // Se abbiamo applicato il padding, abbiamo finito il file.
        if (paddingApplied) break; 
    }
    delete[] inBuffer;
    delete[] outBuffer;
    
    return (err == B_OK) ? (ssize_t)totalWritten : (ssize_t)err;
}
// GCM STREAMING
ssize_t 
BCrypto::Encrypt(uint8* key, size_t keyLen, uint8* iv, size_t ivLen,
                 BDataIO* source, BDataIO* destination, void* outTag)
{
    // Verifica parametri di base
    if (fMode != B_CRYPTO_MODE_GCM) return B_BAD_VALUE;
    if (!source || !destination || !outTag) return B_BAD_VALUE;

    status_t err;
    
    // 1. GCM_INIT: Inizializziamo la sessione nel driver
    BCryptoUserRequest initReq;
    memset(&initReq, 0, sizeof(initReq));
    initReq.operation = B_CRYPTO_ENCRYPT;
    initReq.algorithm = fAlgorithm;
    initReq.mode = fMode;
    initReq.key = key;
    initReq.keyLength = keyLen;
    initReq.iv = iv;
    initReq.ivLength = ivLen;

    err = ioctl(fFd, B_CRYPTO_IOCTL_STREAM_INIT, &initReq);
    if (err != B_OK) return err;

    // 2. LOOP DI CIFRATURA: Leggiamo a blocchi e cifriamo
    const size_t kBufferSize = 1024 * 64; // Chunk da 64KB per bilanciare RAM e performance
    uint8* bufferSrc = (uint8*)malloc(kBufferSize);
    uint8* bufferDst = (uint8*)malloc(kBufferSize);
    
    if (!bufferSrc || !bufferDst) {
        free(bufferSrc); free(bufferDst);
        return B_NO_MEMORY;
    }

    ssize_t totalEncrypted = 0;
    ssize_t bytesRead;
    
    // Leggiamo finché ci sono dati nel BDataIO sorgente
    while ((bytesRead = source->Read(bufferSrc, kBufferSize)) > 0) {
        iovec iovSrc = { bufferSrc, (size_t)bytesRead };
        iovec iovDst = { bufferDst, (size_t)bytesRead };

        BCryptoUserRequest updateReq;
        memset(&updateReq, 0, sizeof(updateReq));
        updateReq.source = &iovSrc;
        updateReq.destination = &iovDst;
        updateReq.vectorCount = 1;

        // Il driver cifra il blocco e aggiorna l'accumulatore GHASH internamente
        err = ioctl(fFd, B_CRYPTO_IOCTL_STREAM_UPDATE, &updateReq);
        if (err != B_OK) break;

        // Scriviamo il risultato cifrato nella destinazione
        ssize_t bytesWritten = destination->Write(bufferDst, bytesRead);
        if (bytesWritten != bytesRead) {
            err = B_IO_ERROR;
            break;
        }
        totalEncrypted += bytesWritten;
    }

    // Pulizia buffer temporanei
    free(bufferSrc);
    free(bufferDst);

    // Se c'è stato un errore durante il loop, interrompiamo prima del Final
    if (err != B_OK) return err;

    // 3. GCM_FINAL: Recuperiamo il Tag di autenticazione finale
    uint8 computedTag[16];
    iovec tagIov = { computedTag, 16 };
    
    BCryptoUserRequest finalReq;
    memset(&finalReq, 0, sizeof(finalReq));
    finalReq.destination = &tagIov;
    finalReq.vectorCount = 1;

    err = ioctl(fFd, B_CRYPTO_IOCTL_STREAM_FINAL, &finalReq);
    if (err != B_OK) return err;

    // Copiamo il tag generato nel buffer di output dell'utente
    memcpy(outTag, computedTag, 16);

    return totalEncrypted;
}
// Versione speciale per Cifratura GCM
ssize_t
BCrypto::Encrypt(uint8* key, size_t keyLen, uint8* iv, size_t ivLen,
                 const void* in, size_t inLen, void* out, void* outTag)
{
    if (fMode != B_CRYPTO_MODE_GCM) return B_BAD_VALUE;
    if (outTag == NULL) return B_BAD_VALUE;

    // Preparazione vettori: 0 = Dati, 1 = Tag
    iovec srcVecs[2] = { {(void*)in, inLen}, {outTag, 16} };
    iovec dstVecs[2] = { {out, inLen}, {outTag, 16} };

    BCryptoUserRequest req;
    _FillRequest(req, B_CRYPTO_ENCRYPT, fAlgorithm, fMode,
                 key, keyLen, iv, ivLen, srcVecs, dstVecs, 2);

    status_t st = Process(req);
    return (st == B_OK) ? (ssize_t)inLen : (ssize_t)st;
}
// Per la Decifratura GCM
ssize_t
BCrypto::Decrypt(uint8* key, size_t keyLen, uint8* iv, size_t ivLen,
                 const void* in, size_t inLen, void* out, const void* inTag)
{
    if (fMode != B_CRYPTO_MODE_GCM) return B_BAD_VALUE;
    if (inTag == NULL || in == NULL || out == NULL) return B_BAD_VALUE;

    // In decifratura il Tag è un INPUT (serve al driver per validare)
    iovec srcVecs[2] = { {(void*)in, inLen}, {(void*)inTag, 16} };
    iovec dstVecs[1] = { {out, inLen} }; // La destinazione ha solo i dati in chiaro

    BCryptoUserRequest req;
    _FillRequest(req, B_CRYPTO_DECRYPT, fAlgorithm, fMode,
                 key, keyLen, iv, ivLen, srcVecs, dstVecs, 2);

    status_t st = Process(req);
    if (st == B_OK)
        return (ssize_t)inLen;
    
    return (ssize_t)st;
}
// Decifratura GCM streaming
ssize_t 
BCrypto::Decrypt(uint8* key, size_t keyLen, uint8* iv, size_t ivLen,
                 BDataIO* source, BDataIO* destination, const void* inTag)
{
    if (fMode != B_CRYPTO_MODE_GCM) return B_BAD_VALUE;
    if (!source || !destination || !inTag) return B_BAD_VALUE;

    status_t err;
    
    // 1. GCM_INIT
    BCryptoUserRequest initReq;
    memset(&initReq, 0, sizeof(initReq));
    initReq.operation = B_CRYPTO_DECRYPT;
    initReq.algorithm = fAlgorithm;
    initReq.mode = fMode;
    initReq.key = key;
    initReq.keyLength = keyLen;
    initReq.iv = iv;
    initReq.ivLength = ivLen;

    err = ioctl(fFd, B_CRYPTO_IOCTL_STREAM_INIT, &initReq);
    if (err != B_OK) return err;

    // 2. LOOP READ -> UPDATE -> WRITE
    const size_t kBufferSize = 1024 * 64; // 64KB chunk
    uint8* bufferSrc = (uint8*)malloc(kBufferSize);
    uint8* bufferDst = (uint8*)malloc(kBufferSize);
    
    if (!bufferSrc || !bufferDst) {
        free(bufferSrc); free(bufferDst);
        return B_NO_MEMORY;
    }

    ssize_t totalDecrypted = 0;
    ssize_t bytesRead;
    
    while ((bytesRead = source->Read(bufferSrc, kBufferSize)) > 0) {
        iovec iovSrc = { bufferSrc, (size_t)bytesRead };
        iovec iovDst = { bufferDst, (size_t)bytesRead };

        BCryptoUserRequest updateReq;
        memset(&updateReq, 0, sizeof(updateReq));
        updateReq.source = &iovSrc;
        updateReq.destination = &iovDst;
        updateReq.vectorCount = 1;

        err = ioctl(fFd, B_CRYPTO_IOCTL_STREAM_UPDATE, &updateReq);
        if (err != B_OK) break;

        ssize_t bytesWritten = destination->Write(bufferDst, bytesRead);
        if (bytesWritten != bytesRead) {
            err = B_IO_ERROR;
            break;
        }
        totalDecrypted += bytesWritten;
    }

    free(bufferSrc);
    free(bufferDst);

    if (err != B_OK) return err;

    // 3. GCM_FINAL & VALIDATION
    uint8 computedTag[16];
    iovec tagIov = { computedTag, 16 };
    
    BCryptoUserRequest finalReq;
    memset(&finalReq, 0, sizeof(finalReq));
    finalReq.destination = &tagIov;
    finalReq.vectorCount = 1;

    err = ioctl(fFd, B_CRYPTO_IOCTL_STREAM_FINAL, &finalReq);
    if (err != B_OK) return err;

    // CONFRONTO TAG: Se non corrispondono, i dati sono compromessi!
    if (memcmp(computedTag, inTag, 16) != 0) {
        // Nota: In un'app reale, qui dovresti probabilmente cancellare 
        // il file di destinazione perché i dati sono falsi.
        return B_BAD_DATA; 
    }

    return totalDecrypted;
}
// Decifratura normale / no GCM
ssize_t 
BCrypto::Decrypt(uint8* key, size_t keyLen, uint8* iv, size_t ivLen,
                 const void* in, size_t inLen, void* out, size_t outSize) 
{
	if (fMode != B_CRYPTO_MODE_CTR && fMode != B_CRYPTO_MODE_GCM && fAlgorithm != B_CRYPTO_CHACHA20) {
        if (inLen == 0 || (inLen % 16) != 0) return B_BAD_VALUE;
    }
    if (outSize < inLen) return B_BAD_VALUE;

    iovec src = { (void*)in, inLen };
    iovec dst = { out, inLen };

    BCryptoUserRequest req;
    _FillRequest(req, B_CRYPTO_DECRYPT, fAlgorithm, fMode,
                 key, keyLen, iv, ivLen, &src, &dst, 1);

    status_t st = Process(req); 
    if (st != B_OK) return st;

    size_t finalLen = inLen;
    if (fPaddingEnabled && fPaddingType != B_CRYPTO_PADDING_NONE) {
        finalLen = _RemovePadding((uint8*)out, inLen);
        if (finalLen == (size_t)B_BAD_DATA) return B_BAD_DATA;
    }

    return (ssize_t)finalLen;
}
ssize_t
BCrypto::Decrypt(uint8* key, size_t keyLen, uint8* iv, size_t ivLen,
                 BDataIO* source, BDataIO* destination)
{
    if (!source || !destination) return B_BAD_VALUE;

    const size_t kBufferSize = 1024 * 1024;
    uint8* inBuffer = new(std::nothrow) uint8[kBufferSize];
    uint8* outBuffer = new(std::nothrow) uint8[kBufferSize];
    
    if (!inBuffer || !outBuffer) {
        delete[] inBuffer; delete[] outBuffer;
        return B_NO_MEMORY;
    }

    status_t err = B_OK;
    ssize_t bytesRead;
    size_t totalWritten = 0;
    
    uint8 lastBlock[kBufferSize];
    size_t lastBlockLen = 0;
    bool firstChunk = true;

    while ((bytesRead = source->Read(inBuffer, kBufferSize)) > 0) {
        if (!firstChunk) {
            ssize_t written = destination->Write(lastBlock, lastBlockLen);
            if (written < 0) { err = (status_t)written; break; }
            totalWritten += (size_t)written;
        }

        iovec src = { inBuffer, (size_t)bytesRead };
        iovec dst = { lastBlock, (size_t)bytesRead };
        BCryptoUserRequest req;
        _FillRequest(req, B_CRYPTO_DECRYPT, fAlgorithm, fMode,
                     key, keyLen, iv, ivLen, &src, &dst, 1);

        err = Process(req);
        if (err != B_OK) break;

        lastBlockLen = (size_t)bytesRead;
        firstChunk = false;
    }

    if (err == B_OK && lastBlockLen > 0) {
        size_t finalLen = lastBlockLen;
        if (fPaddingEnabled && fPaddingType != B_CRYPTO_PADDING_NONE) {
            finalLen = _RemovePadding(lastBlock, lastBlockLen);
            if (finalLen == (size_t)B_BAD_DATA) err = B_BAD_DATA;
        }

        if (err == B_OK && finalLen > 0) {
            ssize_t written = destination->Write(lastBlock, finalLen);
            if (written < 0) err = (status_t)written;
            else totalWritten += (size_t)written;
        }
    }

    delete[] inBuffer;
    delete[] outBuffer;
    
    return (err == B_OK) ? (ssize_t)totalWritten : (ssize_t)err;
}
status_t 
BCrypto::Process(BCryptoUserRequest& userReq) 
{
    if (fFd < 0) return B_NO_INIT;
    
    if (userReq.operation == B_CRYPTO_DIGEST) {
        status_t err = ioctl(fFd, B_CRYPTO_IOCTL_HASH_INIT, &userReq.algorithm);
        if (err != B_OK) return err;

        /* senza chunking 
        for (size_t i = 0; i < userReq.vectorCount; i++) {
            BCryptoUserRequest updateReq = userReq;
            updateReq.source = &userReq.source[i];
            updateReq.vectorCount = 1;
            err = ioctl(fFd, B_CRYPTO_IOCTL_HASH_UPDATE, &updateReq);
            if (err != B_OK) return err;
        }*/
        for (size_t i = 0; i < userReq.vectorCount; i++) {
            // CHUNKING QUI: Spezziamo il vettore se è troppo grande
            const size_t kMaxChunk = 1024 * 1024; // 1MB
            uint8* base = (uint8*)userReq.source[i].iov_base;
            size_t len = userReq.source[i].iov_len;
            size_t done = 0;

            while (done < len) {
                size_t toProcess = std::min(kMaxChunk, len - done);
                iovec tempIov = { base + done, toProcess };
            
                BCryptoUserRequest updateReq = userReq;
                updateReq.source = &tempIov;
                updateReq.vectorCount = 1;

                err = ioctl(fFd, B_CRYPTO_IOCTL_HASH_UPDATE, &updateReq);
                if (err != B_OK) return err;
                done += toProcess;
            }
        }

        return ioctl(fFd, B_CRYPTO_IOCTL_HASH_FINAL, &userReq);
    }
    
    if (fMode == B_CRYPTO_MODE_GCM) {
    	/* TODO: Supporto Streaming GCM (AEAD)
         * Attualmente GCM è implementato come operazione atomica (One-Shot).
         * Per supportare flussi BDataIO arbitrariamente grandi senza caricare tutto in RAM:
         * 1. Implementare B_CRYPTO_IOCTL_GCM_INIT   (Setup H e J0)
         * 2. Implementare B_CRYPTO_IOCTL_GCM_UPDATE (Cifratura blocchi e aggiornamento GHASH)
         * 3. Implementare B_CRYPTO_IOCTL_GCM_FINAL  (XOR con J0 e calcolo Tag finale)
         *
         * Al momento, inviamo il vettore completo al driver.
         */
        // Inviamo tutti i vettori (Dati + Tag) in un'unica chiamata
        return ioctl(fFd, B_CRYPTO_IOCTL_PROCESS, &userReq);
    }

    const size_t kMinZeroCopySize = 4096; 
    const int kMaxVects = 32;

    size_t vIndex = 0;
    while (vIndex < userReq.vectorCount) {
        
        // Se il vettore attuale è piccolo, usiamo il Coalescing (Ottimizzato)
        if (userReq.source[vIndex].iov_len < kMinZeroCopySize) {
            size_t coalesceTotal = 0;
            int vInCoalesce = 0;
            int maxToScan = std::min(kMaxVects, (int)(userReq.vectorCount - vIndex));
            
            for (int j = 0; j < maxToScan; j++) {
                // Ci fermiamo se il totale diventa troppo grande o troviamo un vettore "gigante"
                if (userReq.source[vIndex + j].iov_len >= kMinZeroCopySize) break;
                
                coalesceTotal += userReq.source[vIndex + j].iov_len;
                vInCoalesce++;
            }

            void* bounceBuffer = malloc(coalesceTotal);
            if (!bounceBuffer) return B_NO_MEMORY;

            // PACK
            size_t offset = 0;
            for (int j = 0; j < vInCoalesce; j++) {
                memcpy((uint8*)bounceBuffer + offset, userReq.source[vIndex + j].iov_base, userReq.source[vIndex + j].iov_len);
                offset += userReq.source[vIndex + j].iov_len;
            }

            iovec singleIov = { bounceBuffer, coalesceTotal };
            BCryptoUserRequest batchReq = userReq;
            batchReq.source = &singleIov;
            batchReq.destination = &singleIov;
            batchReq.vectorCount = 1;

            status_t err = ioctl(fFd, B_CRYPTO_IOCTL_PROCESS, &batchReq);

            if (err >= 0) {
                offset = 0;
                for (int j = 0; j < vInCoalesce; j++) {
                    memcpy(userReq.destination[vIndex + j].iov_base, (uint8*)bounceBuffer + offset, userReq.destination[vIndex + j].iov_len);
                    offset += userReq.destination[vIndex + j].iov_len;
                }
            }

            free(bounceBuffer);
            if (err < 0) return err;
            vIndex += vInCoalesce;

        } else {
            // --- STRATEGIA: ZERO-COPY DIRETTO (VELOCE) ---
            /*
            // Se il vettore è grande, ne mandiamo fino a 32 in un colpo solo.
            // Non stiamo a fare troppi controlli, il driver gestirà il resto.
            
            */
            // --- ORA CON CHUNKING ---
            const size_t kMaxChunkSize = 1024 * 1024; // 1MB, multiplo di 16 e 64
            if (userReq.source[vIndex].iov_len > kMaxChunkSize) {
                size_t totalLen = userReq.source[vIndex].iov_len;
                size_t processed = 0;
                uint8* srcBase = (uint8*)userReq.source[vIndex].iov_base;
                uint8* dstBase = (uint8*)userReq.destination[vIndex].iov_base;
                while (processed < totalLen) {
                        size_t toProcess = std::min(kMaxChunkSize, totalLen - processed);
        
                        iovec tempSrc = { srcBase + processed, toProcess };
                        iovec tempDst = { dstBase + processed, toProcess };
        
                        BCryptoUserRequest chunkReq = userReq;
                        chunkReq.source = &tempSrc;
                        chunkReq.destination = &tempDst;
                        chunkReq.vectorCount = 1;

                        // Se è un digest, dobbiamo usare HASH_UPDATE, 
                        // se è cifratura usiamo PROCESS (che nel driver deve gestire lo stato dell'IV)
                        status_t err = ioctl(fFd, B_CRYPTO_IOCTL_PROCESS, &chunkReq);
        
                        if (err < 0) return err;
                        
                        processed += toProcess;
                }
                vIndex++;
            } else { // --- O SENZA CHUNKING
            	int vInDirect = std::min(kMaxVects, (int)(userReq.vectorCount - vIndex));

                BCryptoUserRequest directReq = userReq;
                directReq.source = &userReq.source[vIndex];
                directReq.destination = &userReq.destination[vIndex];
                directReq.vectorCount = vInDirect;

                status_t err = ioctl(fFd, B_CRYPTO_IOCTL_PROCESS, &directReq);
                if (err < 0) return err;

                vIndex += vInDirect;
            }
       }
    }

    return B_OK;
}



void
BCrypto::_FillRequest(BCryptoUserRequest& req, BCryptoOperation op, BCryptoAlgorithmID algo, BCryptoMode mode,
                      uint8* key, size_t keyLen, uint8* iv, size_t ivLen, 
                      iovec* src, iovec* dst, int vCount)
{
    memset(&req, 0, sizeof(req));
    req.operation   = op;
    req.algorithm   = algo;
    req.mode        = mode;
    req.key         = key;
    req.keyLength   = keyLen;
    req.iv          = iv;
    req.ivLength    = ivLen;
    req.source      = src;
    req.destination = dst;
    req.vectorCount = vCount;
}
status_t
BCrypto::Digest(BCryptoAlgorithmID algo, const void* data, size_t len, void* outHash)
{
    size_t hLen = GetHashLength(algo);
    if (hLen == 0) return B_BAD_VALUE;

    iovec src = { (void*)data, len };
    uint8 tempHash[64];
    iovec dst = { tempHash, sizeof(tempHash) };

    BCryptoUserRequest req;
    _FillRequest(req, B_CRYPTO_DIGEST, algo, B_CRYPTO_MODE_ANY,
                 nullptr, 0, nullptr, 0, &src, &dst, 1);

    // Deleghiamo tutto a Process che ora gestisce Init, Update (chunked) e Final
    status_t err = Process(req);
    
    if (err == B_OK)
        memcpy(outHash, tempHash, hLen);
        
    return err;
}
status_t
BCrypto::Digest(BCryptoAlgorithmID algo, BDataIO* source, void* outHash)
{
	//printf("BCrypto: digest in straming\n");
	//fflush(stdout);
    // A. Inizializzazione
    status_t err = ioctl(fFd, B_CRYPTO_IOCTL_HASH_INIT, &algo);
    if (err != B_OK) return err;

    // B. Ciclo di Update (Leggiamo il file/socket a pezzi)
    uint8 buffer[65536]; // Pezzi da 64KB, amichevoli per la cache
    ssize_t bytesRead;
    
    while ((bytesRead = source->Read(buffer, sizeof(buffer))) > 0) {
        iovec src = { buffer, (size_t)bytesRead };
        BCryptoUserRequest req;
        _FillRequest(req, B_CRYPTO_DIGEST, algo, B_CRYPTO_MODE_ANY,
                     nullptr, 0, nullptr, 0, &src, nullptr, 1);
        
        err = ioctl(fFd, B_CRYPTO_IOCTL_HASH_UPDATE, &req);
        if (err != B_OK) break;
    }

    // C. Finalizzazione
    if (err == B_OK) {
        uint8 tempHash[64];
        iovec dst = { tempHash, sizeof(tempHash) };
        BCryptoUserRequest req;
        _FillRequest(req, B_CRYPTO_DIGEST, algo, B_CRYPTO_MODE_ANY,
                     nullptr, 0, nullptr, 0, nullptr, &dst, 1);
                     
        err = ioctl(fFd, B_CRYPTO_IOCTL_HASH_FINAL, &req);
        if (err == B_OK)
            memcpy(outHash, tempHash, GetHashLength(algo));
    }

    return err;
}
size_t
BCrypto::_RemovePadding(uint8* buffer, size_t len)
{
    if (len == 0 || (len % 16) != 0) return B_BAD_DATA;

    switch (fPaddingType) {
        case B_CRYPTO_PKCS7: {
            uint8 padValue = buffer[len - 1];
            if (padValue < 1 || padValue > 16) return B_BAD_DATA;
            for (size_t i = len - padValue; i < len; i++) {
                if (buffer[i] != padValue) return B_BAD_DATA;
            }
            return len - padValue;
        }

        case B_CRYPTO_ISO7816: {
            ssize_t i = len - 1;
            // Cerchiamo lo 0x80 partendo dal fondo, saltando gli zeri
            while (i >= (ssize_t)(len - 16) && buffer[i] == 0x00) {
                i--;
            }
            if (i >= 0 && buffer[i] == 0x80) {
                return (size_t)i;
            }
            return B_BAD_DATA;
        }

        case B_CRYPTO_ZERO_PADDING: {
            size_t i = len;
            while (i > 0 && buffer[i - 1] == 0) i--;
            return i;
        }

        default:
            return len;
    }
}

void
BCrypto::_ApplyPadding(uint8* buffer, size_t inputLen, size_t totalLen)
{
    size_t padLen = totalLen - inputLen;
    if (padLen == 0) return;

    switch (fPaddingType) {
        case B_CRYPTO_PKCS7:
            // Ogni byte ha il valore della lunghezza del padding
            memset(buffer + inputLen, (uint8)padLen, padLen);
            break;

        case B_CRYPTO_ISO7816:
            // Il primo byte è 0x80, i restanti sono 0x00
            buffer[inputLen] = 0x80;
            if (padLen > 1)
                memset(buffer + inputLen + 1, 0, padLen - 1);
            break;

        case B_CRYPTO_ZERO_PADDING:
            // Tutti i byte sono 0x00
            memset(buffer + inputLen, 0, padLen);
            break;

        default:
            break;
    }
}
size_t
BCrypto::GetHashLength(BCryptoAlgorithmID algo) const
{
	return decode_hash_length(algo);
}
