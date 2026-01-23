/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include <KernelExport.h>
#include <Drivers.h>
#include <string.h>
#include <vm/vm.h>        // IS_USER_ADDRESS, copyin/copyout
#include <device_manager.h>
#include "CryptoDevice.h"
#include "BCryptoCore.h"
#include "BCryptoDefs.h"
#include "BCryptoEntropy.h"

//#include "drivers/padlock/PadLock.h"
//#include "drivers/aesni/AESNI.h"
//#include "SoftCrypto.h"

//#include "../BCryptoDefs.h" SE NON LO TROVA DECOMMENTA
//#include "../BCryptoCore.h" SE NON LO TROVA DECOMMENTA
//#include <user_runtime.h>
static void
secure_memzero(void* p, size_t s)
{
    if (p == NULL)
        return;
    volatile uint8* cp = (volatile uint8*)p;
    while (s--)
        *cp++ = 0;
}

//-----------------------------------------------------------
// Device hooks
//-----------------------------------------------------------
/*static status_t
crypto_open(const char* name, uint32 flags, void** cookie)
{
    *cookie = NULL;
    return B_OK;
}*/

static status_t
crypto_open_modern(void* device_cookie, const char* name, int flags, void** cookie)
{
    *cookie = NULL;
    return B_OK;
}

static status_t
crypto_open_legacy(const char* name, uint32 flags, void** cookie)
{
    return crypto_open_modern(NULL, name, (int)flags, cookie);
}

static status_t
crypto_close(void* cookie)
{
    return B_OK;
}

static status_t
crypto_free(void* cookie)
{
    return B_OK;
}

static status_t
crypto_control(void* cookie, uint32 op, void* arg, size_t length)
{
    switch (op) {
        case B_CRYPTO_IOCTL_PROCESS: {
        	dprintf("crypto: elaborazione IOCTL\n");
            BCryptoUserRequest userReq;
            
            if (user_memcpy(&userReq, arg, sizeof(BCryptoUserRequest)) != B_OK) 
                return B_BAD_ADDRESS;
            

            BCryptoRequest req;
            req.operation = userReq.operation;
            req.algorithm = userReq.algorithm;
            req.mode = userReq.mode;
            req.flags = userReq.flags;
            
            if (userReq.keyLength > 64 || userReq.ivLength > 64)
                return B_BAD_VALUE;
            

            uint8 localKey[64];
            uint8 localIV[64];

            if (user_memcpy(localKey, userReq.key, userReq.keyLength) != B_OK) 
                return B_BAD_ADDRESS;
            
            if (user_memcpy(localIV, userReq.iv, userReq.ivLength) != B_OK) 
                return B_BAD_ADDRESS;
            

            req.key = localKey;
            req.keyLength = userReq.keyLength;
            req.iv = localIV;
            req.ivLength = userReq.ivLength;

            if (userReq.vectorCount > 32) 
                return B_DEVICE_FULL;
            

            iovec localSrc[32], localDst[32];

            if (user_memcpy(localSrc, userReq.source, sizeof(iovec) * userReq.vectorCount) != B_OK) 
                return B_BAD_ADDRESS;
            
            if (user_memcpy(localDst, userReq.destination, sizeof(iovec) * userReq.vectorCount) != B_OK) 
                return B_BAD_ADDRESS;

            req.source = localSrc;
            req.destination = localDst;
            req.vectorCount = userReq.vectorCount;
            req.completionCallback = NULL;

            // Chiamata al core del framework
            status_t status = BSubmitCryptoRequest(&req);

            // Riporta l'IV aggiornato all'utente
            user_memcpy(userReq.iv, req.iv, userReq.ivLength);
            
            if (userReq.completionSem >= 0) {
                userReq.result = status;
                release_sem(userReq.completionSem);
            }
            secure_memzero(localKey, sizeof(localKey));
            secure_memzero(localIV, sizeof(localIV));

            return status;
        }
    }
    return B_DEV_INVALID_IOCTL;
}


//static device_hooks sCryptoHooks = {
device_hooks sCryptoHooks = {
    crypto_open_legacy,
    crypto_close,
    crypto_free,
    crypto_control,
    NULL,//crypto_read,   // read
    NULL,//crypto_write,   // write
    NULL,
    NULL
};

struct device_module_info sCryptoDeviceModule = {
    {
        "drivers/crypto/api/v1",
        0,
        NULL
    },
    NULL, // init_device
    NULL, // uninit_device
    NULL, // remove_device
    
    crypto_open_modern,
    crypto_close,
    crypto_free,
    NULL, // read
    NULL, // write
    NULL, // io
    crypto_control,
    
    NULL, // select
    NULL  // deselect
};
//--------------------------
//-----------------------------------------------------------
// Driver entry points
//-----------------------------------------------------------
extern "C" status_t crypto_std_ops(int op, ...);

extern "C" status_t init_hardware()
{
	dprintf("crypto: init_hardware\n");
    return B_OK;
}

extern "C" status_t init_driver()
{
	dprintf("crypto: init_driver\n");
	/*status_t status = crypto_init_core();
    if (status != B_OK) {
        dprintf("crypto: fallimento crypto_init_core!\n");
        return status;
    }
    return B_OK;*/
    return crypto_std_ops(B_MODULE_INIT);
}

extern "C" void uninit_driver()
{
	dprintf("crypto: uninit_driver\n");
    //crypto_uninit_core();
    crypto_std_ops(B_MODULE_UNINIT);
}

extern "C" int32 api_version;
int32 api_version = B_CUR_DRIVER_API_VERSION;

extern "C" const char** publish_devices()
{
    static const char* devices[] = {
        B_CRYPTO_DEVICE_NAME,
        NULL
    };
    return devices;
}

extern "C" device_hooks* find_device(const char* name)
{
    if (strcmp(name, B_CRYPTO_DEVICE_NAME) == 0)
        return &sCryptoHooks;

    return NULL;
}
