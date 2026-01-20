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

static status_t
crypto_std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
			BInitPadLockRNG();
			BInitPadLockCrypto();
			BInitAESNICrypto();
			BInitSoftCrypto();
			return B_OK;

		case B_MODULE_UNINIT:
			return B_OK;
	}

	return B_ERROR;
}

static module_info sCryptoModule = {
    "kernel/crypto",
    0,
    crypto_std_ops
};

module_info* modules[] = {
    &sCryptoModule,
    NULL
};

