/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include <OS.h>
#include <KernelExport.h>
#include "ublox_gps.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>


static usb_module_info *gUSBModule = NULL;
static ublox_device *gDevices[8];
int32 api_version = B_CUR_DRIVER_API_VERSION;


// Checksum calculation Helper (Fletcher-8)
void calculate_ubx_checksum(uint8* packet, size_t len, uint8& ck_a, uint8& ck_b) {
	ck_a = 0; ck_b = 0;
	for (size_t i = 2; i < len - 2; i++) {
		ck_a += packet[i];
		ck_b += ck_a;
	}
}
static void ublox_read_callback(void *cookie, status_t status, void *data, size_t actualLength)
{
    ublox_device *dev = (ublox_device *)cookie;
    if (dev == NULL) return;
    dev->actual_length = (status == B_OK) ? actualLength : 0;
    
    release_sem(dev->read_sem);
}

static void ublox_write_callback(void* cookie, status_t status, void* data, size_t actualLength) {
	sem_id sem = (sem_id)(uintptr_t)cookie;
	release_sem(sem);
}


static status_t ublox_open(const char *name, uint32 flags, void **cookie)
{
    // Slot identification by name
    int index = -1;
    for (int i = 0; i < 8; i++) {
        char tempName[32];
        sprintf(tempName, "ports/ublox_gps/%d", i);
        if (strcmp(tempName, name) == 0) {
            index = i;
            break;
        }
    }

    if (index == -1 || gDevices[index] == NULL) {
        return B_ENTRY_NOT_FOUND;
    }
	
    ublox_device *dev = gDevices[index];

    // Getting exclusive access
    if (dev->open) {
        dprintf("ublox_gps: device già aperto\n");
        return B_BUSY;
    }
    if (dev->removed) return B_DEV_NOT_READY;
	atomic_add(&dev->open_count, 1);
    
    dev->open = true;
    *cookie = dev;

    //dprintf("ublox_gps: aperto slot %d (Pipe IN: %u, Pipe OUT: %u)\n", 
    //        index, (unsigned int)dev->bulk_in, (unsigned int)dev->bulk_out);
            
    return B_OK;
}

static status_t ublox_read(void *cookie, off_t pos, void *buffer, size_t *length)
{
    ublox_device *dev = (ublox_device *)cookie;
    if (dev->removed) return B_DEV_NOT_READY;
    if (dev == NULL || dev->bulk_in == 0) return B_BAD_VALUE;
    
	size_t bufferSize = *length;
    if (bufferSize > 16384) bufferSize = 16384;
    if (bufferSize == 0) return B_OK;
    
    void *kernelBuffer = malloc(bufferSize);
    if (kernelBuffer == NULL) return B_NO_MEMORY;
    
    dev->actual_length = 0; //clean previous length
    
    status_t status = gUSBModule->queue_bulk(dev->bulk_in, kernelBuffer, bufferSize, ublox_read_callback, dev);
	
    if (status != B_OK) {
    	dprintf("ublox_gps: error reading queue_bulk: %s\n", strerror(status));
    	free(kernelBuffer);
        *length = 0;
        //return status;
        return B_OK; // Return B_OK so 'cat' won't exit
    }
    
    status = acquire_sem_etc(dev->read_sem, 1, B_RELATIVE_TIMEOUT | B_CAN_INTERRUPT, 5000000);
    
    if (status == B_OK) {
    	if (dev->removed) {
            dprintf("ublox_gps: read function woke up by device removal\n");
            free(kernelBuffer);
            *length = 0;
            return B_DEV_NOT_READY; 
        }
        size_t bytesRead = dev->actual_length;
        if (user_memcpy(buffer, kernelBuffer, bytesRead) != B_OK) {
            dprintf("ublox_gps: ERRORE user_memcpy failed!\n");
            free(kernelBuffer);
            *length = 0;
            return B_BAD_ADDRESS;
        }
        
        *length = bytesRead;
        free(kernelBuffer);
        return B_OK; 
    } else if (status == B_TIMED_OUT) {
        // By returning B_OK we don't tell 'cat' EOF (0 bytes), so it won't exit.
        dprintf("ublox_gps: TIMEOUT - device didn't answer on the pipe %u\n", dev->bulk_in);
        gUSBModule->cancel_queued_transfers(dev->bulk_in);
        free(kernelBuffer);
        *length = 0;
        return B_OK;//return B_INTERRUPTED;
    } else if (status == B_INTERRUPTED) {
        // User pressed CTRL+C
        gUSBModule->cancel_queued_transfers(dev->bulk_in);
        free(kernelBuffer);
        *length = 0;
        return B_INTERRUPTED;
    }

    *length = 0;
    free(kernelBuffer);
    return B_ERROR;
}



static status_t send_ubx_packet(ublox_device* dev, uint8* packet, size_t len) {
	sem_id sem = create_sem(0, "ublox_write_sync");
	status_t status = gUSBModule->queue_bulk(dev->bulk_out, packet, len, 
		ublox_write_callback, (void*)(uintptr_t)sem);
    
	if (status == B_OK)
		status = acquire_sem_etc(sem, 1, B_RELATIVE_TIMEOUT, 1000000);
    
	delete_sem(sem);
	return status;
}

static status_t
ublox_write(void *cookie, off_t pos, const void *buffer, size_t *length)
{
    ublox_device *dev = (ublox_device *)cookie;
    dprintf("ublox_gps: write - buffer: %p, len: %lu, pipe: %u\n", 
        buffer, *length, dev->bulk_out);
    if (dev == NULL || dev->removed) return B_BAD_VALUE;
    
    
	size_t len = *length;
	if (len == 0) return B_OK;
    if (len > 4096) len = 4096; // Limite di sicurezza per il test
    
    if (dev->bulk_out == 0) {
        dprintf("ublox_gps: ERRORE - Pipe bulk_out non inizializzata!\n");
        return B_BAD_VALUE;
    }
    
    // Buffer kernel per SMAP
    char *kernelBuffer = (char *)malloc(len + 1); // +1 per sicurezza stringhe
    if (kernelBuffer == NULL) return B_NO_MEMORY;
    
    // Se l'utente scrive "RESET" nel terminale (es. echo RESET > /dev/ports/ublox_gps/0)
    if (user_memcpy(kernelBuffer, buffer, len) != B_OK) {
        free(kernelBuffer);
        return B_BAD_ADDRESS; // Errore di memoria utente non valida
    }
    
    dprintf("ublox_gps: invio %lu byte alla pipe %u\n", len, dev->bulk_out);
    
    kernelBuffer[len] = '\0'; // Terminatore per poter usare strncmp in pace
    
	status_t status = B_OK;
	
    // 3. ORA possiamo analizzare il contenuto senza rischi SMAP
    if (strncmp(kernelBuffer, "RESET", 5) == 0) {
        dprintf("ublox_gps: Comando RESET ricevuto!\n");
        
        uint8 resetCmd[] = { 
            0xB5, 0x62, 0x06, 0x04, 0x04, 0x00, 
            0x00, 0x00, 0x02, 0x00, 0x10, 0x68 
        };
        status = send_ubx_packet(dev, resetCmd, sizeof(resetCmd));
        //gUSBModule->queue_bulk(dev->bulk_out, resetCmd, sizeof(resetCmd), NULL, NULL);
        
        //free(kernelBuffer);
        //*length = 5; 
        //return B_OK;
    } else {
    	sem_id sem = create_sem(0, "ublox_write_sync");
        status = gUSBModule->queue_bulk(dev->bulk_out, kernelBuffer, len, 
                                        ublox_write_callback, (void*)(uintptr_t)sem);
        
        if (status == B_OK) {
            // Attendiamo che il trasferimento sia completato prima di proseguire
            status = acquire_sem_etc(sem, 1, B_RELATIVE_TIMEOUT, 1000000);
        }
        delete_sem(sem);
    }

    free(kernelBuffer);

    if (status != B_OK) {
    	dprintf("ublox_gps: errore queue_bulk write: %s (codice: %d)\n", 
            strerror(status), status);
        *length = 0;
        return status;
    }

    *length = len;
    return B_OK;
}

static status_t ublox_control(void* cookie, uint32 op, void* arg, size_t length) {
    ublox_device* dev = (ublox_device*)cookie;
    if (dev->removed) return B_DEV_NOT_READY;

    switch (op) {
        case UBLOX_SET_UPDATE_RATE: {
            uint16 rate;
            if (user_memcpy(&rate, arg, sizeof(uint16)) != B_OK) return B_BAD_ADDRESS;
            
            uint8 packet[] = { 
                0xB5, 0x62, 0x06, 0x08, 0x06, 0x00, 
                (uint8)(rate & 0xFF), (uint8)(rate >> 8), 
                0x01, 0x00, 0x01, 0x00, 0x00, 0x00 
            };
            calculate_ubx_checksum(packet, sizeof(packet), packet[12], packet[13]);
            
            status_t stat = send_ubx_packet(dev, packet, sizeof(packet));
            if (stat == B_OK) dev->current_rate = rate; // SNOOPING
            return stat;
        }

        case UBLOX_SET_BAUD_RATE: {
            uint32 baud;
            if (user_memcpy(&baud, arg, sizeof(uint32)) != B_OK) return B_BAD_ADDRESS;
            
            uint8 packet[] = { 
                0xB5, 0x62, 0x06, 0x00, 0x14, 0x00, 0x01, 0x00, 0x00, 0x00, 
                0xD0, 0x08, 0x00, 0x00, 
                (uint8)(baud & 0xFF), (uint8)((baud >> 8) & 0xFF), 
                (uint8)((baud >> 16) & 0xFF), (uint8)((baud >> 24) & 0xFF),
                0x07, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 
            };
            calculate_ubx_checksum(packet, sizeof(packet), packet[26], packet[27]);
            
            status_t stat = send_ubx_packet(dev, packet, sizeof(packet));
            if (stat == B_OK) dev->current_baud = baud; // SNOOPING
            return stat;
        }

        case UBLOX_GET_UPDATE_RATE:
            return user_memcpy(arg, &dev->current_rate, sizeof(uint16));

        case UBLOX_GET_BAUD_RATE:
            return user_memcpy(arg, &dev->current_baud, sizeof(uint32));
        case UBLOX_SAVE_CONFIG: {
			// UBX-CFG-CFG: Salva la configurazione corrente
			uint8 packet[] = { 
				0xB5, 0x62, 0x06, 0x09, 0x0D, 0x00, 
				0x00, 0x00, 0x00, 0x00, // Clear mask
				0xFF, 0xFF, 0x00, 0x00, // Save mask (tutto)
				0x00, 0x00, 0x00, 0x00, // Load mask
				0x03,                   // Device mask (1=BBR, 2=Flash, 3=Entrambe)
				0x1E, 0x4F              // Checksum Fletcher precalcolato
			};
    
			// Non serve ricalcolare il checksum perché il payload è fisso
			status_t stat = send_ubx_packet(dev, packet, sizeof(packet));
			if (stat == B_OK) {
				dprintf("ublox_gps: configurazione salvata nella memoria permanente.\n");
			}
			return stat;
		}
    }
    return B_DEV_INVALID_IOCTL;
}
static status_t ublox_close(void *cookie)
{
    ublox_device *dev = (ublox_device *)cookie;
    atomic_add(&dev->open_count, -1);
    dev->open = false;
    return B_OK;
}

static status_t ublox_free(void *cookie)
{
    ublox_device *dev = (ublox_device *)cookie;
    dev->open = false;

    // Se il device è stato rimosso fisicamente E il file viene chiuso,
    // allora è sicuro cancellare tutto.
    /*
    if (dev->removed) {
        dprintf("ublox_gps: pulizia finale memoria slot %d\n", dev->number);
        delete_sem(dev->read_sem);
        free(dev);
    }
    */
    if (dev->removed && dev->open_count == 0) {
    	dprintf("ublox_gps: free - pulizia finale\n");
        delete_sem(dev->read_sem);
        free(dev);
    }
    return B_OK;
}

static status_t device_added(usb_device device, void **cookie)
{
	const usb_device_descriptor *desc = gUSBModule->get_device_descriptor(device);
	if (desc == NULL) {
		dprintf("ublox_gps: errore - impossibile ottenere il descrittore del device\n");
		return B_ERROR;
	}
	const usb_configuration_info *config = gUSBModule->get_configuration(device);
    if (config == NULL) {
    	dprintf("ublox_gps: impossibile ottenere la configurazione\n");
    	return B_ERROR;
    }
    if (desc->vendor_id != UBLOX_VENDOR_ID || desc->product_id != UBLOX_7_PRODUCT_ID) {
        return B_ERROR;
	}
	
	gUSBModule->set_configuration(device, config);
	
	
	bool interfaceValid = false;
    const usb_interface_info *interface = NULL;
	for (size_t iter = 0; iter < config->interface_count; iter++) {
        interface = config->interface[iter].active;
        if (interface->endpoint_count >= 2) {
            interfaceValid = true;
            break; 
        }
    }
	if (!interfaceValid) {
        dprintf("ublox_gps: Salto interfaccia inutile (pochi endpoint)\n");
        return B_ERROR; // Fondamentale: non allochiamo slot per interfacce inutili
    }
	
	ublox_device *dev = (ublox_device *)malloc(sizeof(ublox_device));
	if (dev == NULL) {
		dprintf("ublox_gps: errore - memoria esaurita!\n");
		return B_NO_MEMORY;
	}
    
	memset(dev, 0, sizeof(ublox_device));
    
	int slot = -1;
	for (int i = 0; i < 8; i++) {
		if (gDevices[i] == NULL) {
			slot = i;
			break;
		}
	}

	if (slot == -1) {
		dprintf("ublox_gps: errore - nessun slot libero (max 8)\n");
	    free(dev);
	    return B_ERROR; // Troppi dispositivi connessi!
	}

	dprintf("ublox_gps: assegnato allo slot %d\n", slot);

    for (size_t i = 0; i < interface->endpoint_count; i++) {
        usb_endpoint_info *endpoint = &interface->endpoint[i];
        if (endpoint->descr->attributes == USB_ENDPOINT_ATTR_BULK) {
            if (endpoint->descr->endpoint_address & USB_ENDPOINT_ADDR_DIR_IN) {
                dev->bulk_in = endpoint->handle;
                dprintf("ublox_gps: salvata pipe Bulk IN: %u\n", dev->bulk_in);
            } else {
                dev->bulk_out = endpoint->handle;
                dprintf("ublox_gps: salvata pipe Bulk OUT: %u\n", dev->bulk_out);
            }
        }
    }
    dev->device = device;
    dev->number = slot;
    dev->read_sem = create_sem(0, "ublox_gps_read_sem");

    gDevices[slot] = dev;
    *cookie = dev;

    dprintf("ublox_gps: CONFIGURATO slot %d (IN: %u, OUT: %u)\n", 
            slot, dev->bulk_in, dev->bulk_out);
    dev->current_rate = 1000;
    dev->current_baud = 9600;
    return B_OK;
}

static status_t device_removed(void *cookie)
{
    ublox_device *dev = (ublox_device *)cookie;
    dprintf("ublox_gps: rimozione dispositivo in corso...\n");
    dev->removed = true;
    if (dev->number >= 0 && dev->number < 8)
    	gDevices[dev->number] = NULL;
    gUSBModule->cancel_queued_transfers(dev->bulk_in);
    gUSBModule->cancel_queued_transfers(dev->bulk_out);
    release_sem_etc(dev->read_sem, 1, B_RELEASE_ALL);
    if (atomic_get(&dev->open_count) == 0) {
    	dprintf("ublox_gps: removed - pulizia finale\n");
        delete_sem(dev->read_sem);
        free(dev);
    }
    return B_OK;
}

static usb_notify_hooks notify_hooks = {
    device_added,
    device_removed
};
status_t init_hardware()
{
    // Questo dice al kernel: "Sì, prova a caricarmi!"
    return B_OK; 
}

static usb_support_descriptor gSupportedDevices[] = {
    { 0, 0, 0, UBLOX_VENDOR_ID, UBLOX_7_PRODUCT_ID }
};
status_t init_driver()
{
    status_t status = get_module(B_USB_MODULE_NAME, (module_info **)&gUSBModule);
    if (status != B_OK) {
        dprintf("ublox_gps: errore get_module(B_USB_MODULE_NAME) -> %s\n", strerror(status));
        return status;
    }
    status = gUSBModule->register_driver("ublox_gps", gSupportedDevices, 1, NULL);
    if (status != B_OK) {
        dprintf("ublox_gps: errore register_driver -> %s\n", strerror(status));
    }
    status = gUSBModule->install_notify("ublox_gps", &notify_hooks);
    if (status != B_OK) {
        dprintf("ublox_gps: errore install_notify -> %s\n", strerror(status));
    }
    return B_OK;
}

void uninit_driver()
{
    gUSBModule->uninstall_notify("ublox_gps");
    put_module(B_USB_MODULE_NAME);
}

static device_hooks gUbloxHooks = {
    ublox_open,            /* open */
    ublox_close,           /* close */
    ublox_free,            /* free */
    ublox_control,         /* control */
    ublox_read,            /* read */
    ublox_write,           /* write */
    NULL,                  /* select */
    NULL,                  /* deselect */
    NULL,                  /* read_pages */
    NULL                   /* write_pages */
};


const char ** publish_devices()
{
    static char names[9][32];
    static const char *namePtrs[10];
    int count = 0;

    for (int i = 0; i < 8; i++) {
        if (gDevices[i] != NULL) {
            sprintf(names[count], "ports/ublox_gps/%d", i);
            namePtrs[count] = names[count];
            count++;
        }
    }
    namePtrs[count] = NULL;
    return namePtrs;
}

device_hooks* find_device(const char* name) {
    return &gUbloxHooks;
}
