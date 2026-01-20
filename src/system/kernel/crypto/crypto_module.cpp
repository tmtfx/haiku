/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include <module.h>
#include <KernelExport.h>

#include "drivers/padlock/PadLockRNG.h"
#include "drivers/padlock/PadLock.h"
#include "drivers/aesni/AESNI.h"
#include "drivers/software/SoftCrypto.h"
#include "BCryptoCore.h"
#include "BCryptoCapabilities.h"
#include "BCryptoAlgorithm.h"
#include "BCryptoDefs.h"

extern "C" status_t crypto_init_core();

struct crypto_module_info {
	module_info info;
	status_t (*register_algorithm)(BCryptoAlgorithm* algorithm);
	status_t (*unregister_algorithm)(BCryptoAlgorithmID algorithm);
	status_t (*submit_request)(BCryptoRequest* request);
//	uint32 (*get_capabilities)();
};

//static status_t
status_t
crypto_std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
		{
			status_t status = crypto_init_core();
            if (status != B_OK)
                return status;
			BInitPadLockRNG();
			BInitPadLockCrypto();
			BInitAESNICrypto();
			BInitSoftCrypto();
			return B_OK;
		}
		case B_MODULE_UNINIT:
			return B_OK;
	}

	return B_ERROR;
}

static struct crypto_module_info sCryptoModuleInfo = {
    {
        "crypto/v1",
        0,
        crypto_std_ops
    },
    // Qui aggiungerai i puntatori alle funzioni del tuo framework
    BRegisterCryptoAlgorithm,
    BUnregisterCryptoAlgorithm,
    BSubmitCryptoRequest
};

extern "C" __attribute__((visibility("default"))) module_info* modules[];

module_info* modules[] = {
    (module_info*)&sCryptoModuleInfo,
    NULL
};
