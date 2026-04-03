/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include <KernelExport.h>
//#include <Drivers.h>
#include <graphic_driver.h>
#include <PCI.h>
#include <OS.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <driver_settings.h>
#include "DriverInterface.h"
#include "sm750_macros.h"
#include "memory_manager.h"

/* Identificativi Silicon Motion */
#define VENDOR_ID_SMI 0x126f
#define DEVICE_ID_SM750 0x0750

int32 api_version = B_CUR_DRIVER_API_VERSION;

/* Stato globale del driver */
typedef struct {
    pci_info info;
    char name[B_OS_NAME_LENGTH];
} sm750_device_info;

static sm750_settings current_settings = {
    "sm750.accelerant", // accelerant
    false,              // dumprom
    0x00000000,         // logmask
    0,                  // memory (0 = auto)
    false,              // usebios
    true,               // hardcursor
    true                // dualhead
};

static sm750_device_info *devices[8]; // Supporto fino a 8 schede
static pci_module_info *pci;


// Prototypes for device hook functions.

static status_t open_device(const char* name, uint32 flags, void** cookie);
static status_t close_device(void* dev);
static status_t free_device(void* dev);
static status_t read_device(void* dev, off_t pos, void* buf, size_t* len);
static status_t write_device(void* dev, off_t pos, const void* buf, size_t* len);
static status_t control_device(void* dev, uint32 msg, void* buf, size_t len);


static device_hooks gDeviceHooks = {
    open_device,    /* open */
    close_device,   /* close */
    free_device,    /* free */
    control_device, /* control/ioctl */
    read_device,    /* read */
    write_device,   /* write */
    NULL,           /* select */
    NULL,           /* deselect */
    NULL,           /* readv */
    NULL            /* writev */
};


/* Helper per la mappatura */
static area_id
map_mem(void **out_virt, phys_addr_t phys, uint32 size, const char *name)
{
    void *virt = NULL;
    area_id area = map_physical_memory(name, phys, size, 
                                       B_ANY_KERNEL_ADDRESS, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA | B_CLONEABLE_AREA, &virt);
    if (area >= 0)
        *out_virt = virt;
    else
        *out_virt = NULL;
    return area;
}


/* --- Hook: init_hardware --- */
status_t init_hardware(void) {
    pci_info info;
    status_t status = get_module(B_PCI_MODULE_NAME, (module_info **)&pci);
    if (status != B_OK) return status;

    bool found = false;
    for (int32 i = 0; pci->get_nth_pci_info(i, &info) == B_OK; i++) {
        if (info.vendor_id == VENDOR_ID_SMI && info.device_id == DEVICE_ID_SM750) {
            found = true;
            break;
        }
    }
    put_module(B_PCI_MODULE_NAME);
    return found ? B_OK : B_ERROR;
}

/* --- Hook: init_driver --- */
status_t init_driver(void) {
	
	void *settings_handle;

    /* Caricamento impostazioni dallo stile VIA/MGA */
    settings_handle = load_driver_settings("sm750.settings");
    if (settings_handle != NULL) {
        const char *item;
        char *end;
        uint32 value;

        // Parametro: accelerant
        item = get_driver_parameter(settings_handle, "accelerant", "", "");
        if (strlen(item) > 0 && strlen(item) < sizeof(current_settings.accelerant) - 1) {
            strcpy(current_settings.accelerant, item);
        }

        // Parametro: dumprom
        current_settings.dumprom = get_driver_boolean_parameter(settings_handle, "dumprom", false, false);

        // Parametro: logmask (esadecimale)
        item = get_driver_parameter(settings_handle, "logmask", "0x00000000", "0x00000000");
        value = strtoul(item, &end, 0);
        if (*end == '\0') current_settings.logmask = value;

        // Parametro: memory (decimale)
        item = get_driver_parameter(settings_handle, "memory", "0", "0");
        value = strtoul(item, &end, 0);
        if (*end == '\0') current_settings.memory = value;

        // Parametri Booleani
        current_settings.hardcursor = get_driver_boolean_parameter(settings_handle, "hardcursor", true, true);
        current_settings.usebios    = get_driver_boolean_parameter(settings_handle, "usebios", false, false);
        current_settings.dualhead   = get_driver_boolean_parameter(settings_handle, "dualhead", true, true);

        unload_driver_settings(settings_handle);
    }
	
    status_t status = get_module(B_PCI_MODULE_NAME, (module_info **)&pci);
    if (status != B_OK) return status;

    int32 count = 0;
    pci_info info;
    for (int32 i = 0; pci->get_nth_pci_info(i, &info) == B_OK && count < 8; i++) {
        if (info.vendor_id == VENDOR_ID_SMI && info.device_id == DEVICE_ID_SM750) {
            devices[count] = malloc(sizeof(sm750_device_info));
            devices[count]->info = info;
            sprintf(devices[count]->name, "graphics/sm750_%02x%02x%02x", 
                    info.bus, info.device, info.function);
            count++;
        }
    }
    devices[count] = NULL;
    return B_OK;
}

/* --- Hook: uninit_driver --- */
void uninit_driver(void) {
    for (int32 i = 0; devices[i]; i++) free(devices[i]);
    put_module(B_PCI_MODULE_NAME);
}

/* --- Hook: publish_devices --- */
const char **publish_devices(void) {
    static const char *names[9];
    int32 i;
    for (i = 0; devices[i]; i++) names[i] = devices[i]->name;
    names[i] = NULL;
    return names;
}

/* --- Hook: find_device --- */
device_hooks *find_device(const char *name) {
    int32 i;
    
    /* Verifichiamo che il nome richiesto sia tra quelli che abbiamo pubblicato */
    for (i = 0; devices[i]; i++) {
        if (strcmp(name, devices[i]->name) == 0) {
            return &gDeviceHooks;
        }
    }

    return NULL;
}

/* --- open_device --- */
static status_t
open_device(const char *name, uint32 flags, void **cookie)
{
    int32 i;
    sm750_device_info *di = NULL;
    shared_info *si;
    
    /* Trova quale scheda stiamo aprendo */
    for (i = 0; devices[i]; i++) {
        if (strcmp(name, devices[i]->name) == 0) {
            di = devices[i];
            break;
        }
    }
    if (!di) return B_BAD_VALUE;

    /* Alloca la shared_info (che sarà condivisa con l'accelerante) */
    area_id shared_area = create_area("sm750 shared info", (void **)&si,
        B_ANY_KERNEL_ADDRESS, B_PAGE_SIZE, B_FULL_LOCK, B_READ_AREA | B_WRITE_AREA);
    
    if (shared_area < 0) return shared_area;
    memset(si, 0, B_PAGE_SIZE);
    
    si->settings = current_settings;

    /* Mappatura Registri (BAR 0) */
    si->regs_area = map_mem((void **)&si->regs, di->info.u.h0.base_registers[0], 
                            di->info.u.h0.base_register_sizes[0], "sm750_regs");

    /* Mappatura Framebuffer (BAR 1) */
    si->fb_area = map_mem((void **)&si->framebuffer, di->info.u.h0.base_registers[1], 
                          di->info.u.h0.base_register_sizes[1], "sm750_fb");

    si->framebuffer_pci = (phys_addr_t)di->info.u.h0.base_registers[1];

    /* 1. Inizializzazione del Gestore Memoria Video */
    /* Assumiamo per ora che la memoria sia quella riportata dal PCI o forzata nei settings */
    uint32 mem_size = di->info.u.h0.base_register_sizes[1]; 
    if (si->settings.memory > 0) mem_size = si->settings.memory * 1024 * 1024;

    /* Inizializziamo l'heap: 
       - start: 0 (offset relativo all'inizio del BAR1)
       - length: dimensione totale
       - blockSize: 4KB (allineamento standard pagine)
       - heapEntries: 128 (bastano per gestire i vari buffer)
    */
    si->mem_mgr = mem_init("sm750_vram", 0, mem_size, 4096, 128);
    
    if (!si->mem_mgr) {
        delete_area(si->regs_area);
        delete_area(si->fb_area);
        delete_area(shared_area);
        return B_NO_MEMORY;
    }

    /* 2. ESECUZIONE COLDSTART (Ora il chip è sveglio) */
    sm750_init_chip(si);
    
    /* 3. Allocazione spazio per il Cursore Hardware (64x64 pixel @ 32bpp = 16KB) */
    /* Lo mettiamo in fondo alla memoria per non dare fastidio al frontbuffer */
    uint32 cursor_block;
    uint32 cursor_offset;
    if (mem_alloc(si->mem_mgr, 16384, (void*)"cursor", &cursor_block, &cursor_offset) == B_OK) {
        si->cursor.pci_address = si->framebuffer_pci + cursor_offset;
        si->cursor.v_address = si->framebuffer + cursor_offset;
    }

    /* Passiamo il puntatore alla shared_info come cookie per le altre chiamate */
    *cookie = si;
    
    return B_OK;
}

/* --- control_device (IOCTL) --- */
static status_t
control_device(void *cookie, uint32 op, void *arg, size_t len)
{
    shared_info *si = (shared_info *)cookie;
    
    switch (op) {
        case ENG_GET_PRIVATE_DATA:
            {
                sm750_get_private_data *gpd = (sm750_get_private_data *)arg;
                gpd->shared_info_area = si->regs_area; // o area dedicata
                return B_OK;
            }
        /* Gestiremo qui le altre chiamate PCI */
    }
    return B_DEV_INVALID_IOCTL;
}

/* --- Altri hook minimi --- */
static status_t close_device(void *cookie) { return B_OK; }
static status_t free_device(void *cookie) { 
	shared_info *si = (shared_info *)cookie;

    if (si != NULL) {
        /* 1. Distruggi il gestore memoria video */
        if (si->mem_mgr != NULL) {
            mem_destroy(si->mem_mgr);
        }

        /* 2. Cancella le aree mappate */
        delete_area(si->regs_area);
        delete_area(si->fb_area);

        /* 3. Infine cancella l'area shared_info stessa */
        area_id area = find_area("sm750 shared info");
        if (area >= 0) delete_area(area);
    }

    return B_OK;
}
static status_t read_device(void *cookie, off_t pos, void *buf, size_t *len) { return B_NOT_ALLOWED; }
static status_t write_device(void *cookie, off_t pos, const void *buf, size_t *len) { return B_NOT_ALLOWED; }
