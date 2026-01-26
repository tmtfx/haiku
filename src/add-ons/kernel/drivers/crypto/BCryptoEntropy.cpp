/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include "BCryptoEntropy.h"
#include "BCryptoCore.h"
//#include "BCryptoDefs.h"
#include <crypto/BCryptoDefs.h>
#include <Random.h>
#include <random.h>

#include <device_manager.h>
#include <module.h>
#include <random_defs.h>
#include <debug.h> //da rimuovere una volta verificato

extern "C" status_t random_queue_randomness(uint64 value);

status_t
PadLockFeedEntropy()
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
