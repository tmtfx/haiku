/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include "BCryptoEntropy.h"
#include "BCryptoCore.h"
#include <crypto/BCryptoDefs.h>
#include <Random.h>
#include <random.h>
#include <inttypes.h>

#include <device_manager.h>
#include <module.h>
#include <random_defs.h>
#include <debug.h> //da rimuovere una volta verificato

#include <kernel.h>

extern "C" status_t random_queue_randomness(uint64 value);

static thread_id sEntropyThread = -1;
static bool sStopEntropyThread = false;
static uint64 sTotalEntropyInjected = 0; // Il nostro contatore globale

static status_t
entropy_feeder_thread(void* data)
{
    uint8 buffer[64]; // 512 bit di entropia pura ad ogni ciclo
    dprintf("BCrypto: Entropy feeder thread started.\n");
    random_for_controller_interface* randomModule = NULL;
    
    if (get_module(RANDOM_FOR_CONTROLLER_MODULE_NAME, (module_info**)&randomModule) != B_OK) {
        dprintf("BCrypto: Could not load random module for feeder!\n");
        return B_ERROR;
    }
    
    while (!sStopEntropyThread) {
        if (BFillBufferWithRandom(buffer, sizeof(buffer)) == B_OK) {
            // Iniezione sicura tramite modulo
            uint64* dataPtr = (uint64*)buffer;
            for (size_t i = 0; i < (sizeof(buffer) / sizeof(uint64)); i++) {
                randomModule->queue_randomness(dataPtr[i]);
            }
            
            sTotalEntropyInjected += sizeof(buffer);
            
            // Log periodico usando la macro corretta per uint64
            if ((sTotalEntropyInjected % (sizeof(buffer) * 100)) == 0) {
                dprintf("BCrypto: Total hardware entropy injected: %" B_PRIu64 " bytes\n", 
                    sTotalEntropyInjected);
            }
        }
        snooze(5000000); 
    }
    put_module(RANDOM_FOR_CONTROLLER_MODULE_NAME);
    dprintf("BCrypto: Entropy feeder thread stopping. Total injected: %" B_PRIu64 " bytes\n",
             sTotalEntropyInjected);
    return B_OK;
}

status_t
BStartEntropyFeeder()
{
    // Avviamo solo se c'è almeno un RNG hardware registrato
    if (!(BGetStoredCryptoCapabilities() & B_CRYPTO_HW_RNG))
        return B_NOT_SUPPORTED;

    if (sEntropyThread >= 0) return B_OK;

    sStopEntropyThread = false;
    sEntropyThread = spawn_kernel_thread(entropy_feeder_thread, 
        "crypto_entropy_feeder", B_LOW_PRIORITY, NULL);
    
    if (sEntropyThread < B_OK) return sEntropyThread;

    return resume_thread(sEntropyThread);
}

void
BStopEntropyFeeder()
{
    if (sEntropyThread < 0) return;
    sStopEntropyThread = true;
    status_t exitValue;
    wait_for_thread(sEntropyThread, &exitValue);
    sEntropyThread = -1;
}

status_t
EntropyManager()
{
    uint8 buffer[64];
    BCryptoRequest req{};
    iovec vec = { (void*)buffer, sizeof(buffer) };

    req.operation = B_CRYPTO_DIGEST;
    req.algorithm = B_CRYPTO_RNG;
    req.destination = &vec;
    req.vectorCount = 1;

    status_t st = BSubmitCryptoRequest(&req);
    if (st != B_OK) {
        return st;
    }
    random_for_controller_interface* random;
	if (get_module(RANDOM_FOR_CONTROLLER_MODULE_NAME, (module_info**)&random) == B_OK) {
		uint64* data = (uint64*)buffer;
		for (size_t i = 0; i < (sizeof(buffer) / sizeof(uint64)); i++) {
			random->queue_randomness(data[i]);
		}
		dprintf("BCrypto: Fed %zu bytes of PadLock entropy to the system pool.\n", sizeof(buffer));
		put_module(RANDOM_FOR_CONTROLLER_MODULE_NAME);
	} else {
		dprintf("BCrypto: Could not load random module to feed entropy!\n");
	}
    //add_advanced_entropy(buffer, sizeof(buffer),sizeof(buffer) * 8);
    //add_entropy(buffer, sizeof(buffer));
    //add_advanced_entropy(buffer, sizeof(buffer), sizeof(buffer) * 8);

    return B_OK;
}
