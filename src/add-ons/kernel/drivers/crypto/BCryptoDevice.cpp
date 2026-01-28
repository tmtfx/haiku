#include "BCryptoDevice.h"
#include <crypto/BCryptoDefs.h>
#include <lock.h>      // Per mutex
#include <debug.h>
#include <string.h>

#define MAX_CRYPTO_DEVICES 8

static crypto_device_info sRegisteredDevices[MAX_CRYPTO_DEVICES];
static int32 sDeviceCount = 0;
static mutex sDeviceListLock;

extern "C" {
// Inizializza il manager
status_t
crypto_manager_init()
{
    mutex_init(&sDeviceListLock, "crypto devices lock");
    sDeviceCount = 0;
    memset(sRegisteredDevices, 0, sizeof(sRegisteredDevices));
    return B_OK;
}

// Funzione che i "driver" chiamano per farsi conoscere
status_t
register_crypto_device(crypto_device_info* info)
{
    mutex_lock(&sDeviceListLock);

    if (sDeviceCount >= MAX_CRYPTO_DEVICES) {
        mutex_unlock(&sDeviceListLock);
        return B_NO_MEMORY;
    }

    // Copiamo le info nel registro
    memcpy(&sRegisteredDevices[sDeviceCount], info, sizeof(crypto_device_info));
    
    //dprintf("BCrypto: Registrato dispositivo [%s], Algos: 0x%" B_PRIx32 ", HW: 0x%" B_PRIx32 "\n", info->vendor_name, info->algos_supported, info->hw_type);

    sDeviceCount++;

    mutex_unlock(&sDeviceListLock);
    return B_OK;
}

int32
get_registered_device_count(void)
{
    return sDeviceCount;
}
// Funzione per trovare il miglior dispositivo per un certo algoritmo
crypto_device_info*
find_best_device(BCryptoAlgorithmID algo)
{
    crypto_device_info* best = NULL;
    uint32 maxThroughput = 0;

    mutex_lock(&sDeviceListLock);
    for (int i = 0; i < sDeviceCount; i++) {
        if (sRegisteredDevices[i].algos_supported & algo) {
            if (sRegisteredDevices[i].max_throughput > maxThroughput) {
                maxThroughput = sRegisteredDevices[i].max_throughput;
                best = &sRegisteredDevices[i];
            }
        }
    }
    mutex_unlock(&sDeviceListLock);
    
    return best;
}

void
crypto_manager_uninit()
{
    mutex_lock(&sDeviceListLock);
    
    // Se avessimo allocato memoria dinamica per i cookie, 
    // qui dovremmo inviare un segnale ai driver per liberarla.
    sDeviceCount = 0;
    
    mutex_unlock(&sDeviceListLock);
    mutex_destroy(&sDeviceListLock);
}

}
