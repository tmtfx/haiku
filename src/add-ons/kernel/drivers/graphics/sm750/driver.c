/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include <KernelExport.h>
#include <Drivers.h>
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

/*
typedef struct ChipInfo {
	uint16		chipID;			// PCI device id of the chip
	uint16		chipType;		// assigned chip type identifier
	const char*	chipName;		// user recognizable name for chip (must be < 32
								// chars)
};*/

static const struct ChipInfo sm750_chips[] = {
    { 0x0750, 0, "SM750 LynxExp" },
    { 0x0751, 1, "SM750LE" },
    { 0, 0, NULL } // Terminatore
};





static sm750_settings current_settings = {
    "sm750.accelerant", // accelerant
    false,              // dumprom
    0x00000000,         // logmask
    0,                  // memory (0 = auto)
    false,              // usebios
    true,               // hardcursor
    true                // dualhead
};

static DeviceInfo *devices[8]; // Supporto fino a 8 schede
pci_module_info *pci;


// Prototypes for device hook functions.

static status_t open_device(const char* name, uint32 flags, void** cookie);
static status_t close_device(void* dev);
static status_t free_device(void* dev);
static status_t read_device(void* dev, off_t pos, void* buf, size_t* len);
static status_t write_device(void* dev, off_t pos, const void* buf, size_t* len);
static status_t control_device(void* dev, uint32 msg, void* buf, size_t len);
void sm750_pixel_test(shared_info *si);


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
    // Arrotondiamo la dimensione alla pagina (4096)
    size = (size + B_PAGE_SIZE - 1) & ~(B_PAGE_SIZE - 1);

    area_id area = map_physical_memory(name, phys, size, 
        B_ANY_KERNEL_ADDRESS, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA | B_CLONEABLE_AREA, &virt);

    if (area < B_OK) {
        dprintf("SM750 ERROR: map_physical_memory(%s) failed: %s\n", name, strerror(area));
        *out_virt = NULL;
    } else {
        *out_virt = virt;
    }
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
            devices[count] = malloc(sizeof(DeviceInfo));
            if (!devices[count]) continue;
            memset(devices[count], 0, sizeof(DeviceInfo));
            devices[count]->pci = info;
            
            devices[count]->pChipInfo = &sm750_chips[0]; // default
            if (info.device_id == 0x0751) devices[count]->pChipInfo = &sm750_chips[1];

            sprintf(devices[count]->name, "graphics/sm750_%02x%02x%02x", 
                    info.bus, info.device, info.function);
            
            dprintf("SM750: Found %s at PCI %d:%d:%d\n", 
                devices[count]->pChipInfo->chipName, info.bus, info.device, info.function);
            snooze(100000); // TODO: da rimuovere questa riga
            count++;
        }
    }
    devices[count] = NULL;
    if (count == 0) {
    	dprintf("SM750 DEBUG: nessun dispostivo?!");
    	put_module(B_PCI_MODULE_NAME);
    }
    return count > 0 ? B_OK : ENODEV;
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
	dprintf("SM750: open_device() - Mappatura registri...\n");
    int32 i;
    DeviceInfo *di = NULL;
    //shared_info *si = NULL; //evitiamo doppioni
    status_t status;
    
    /* Trova quale scheda stiamo aprendo */
    for (i = 0; devices[i]; i++) {
        if (strcmp(name, devices[i]->name) == 0) {
            di = devices[i];
            break;
        }
    }
    if (!di) return B_BAD_VALUE;
    
    // --- ABILITAZIONE PCI ---
    // 1. Lettura e abilitazione Command Register
    uint16 pcicmd = pci->read_pci_config(di->pci.bus, di->pci.device, di->pci.function, PCI_command, 2);
    dprintf("SM750: PCI Command iniziale: 0x%04x\n", pcicmd);

    pcicmd |= PCI_command_memory | PCI_command_master | PCI_command_io;
    pci->write_pci_config(di->pci.bus, di->pci.device, di->pci.function, PCI_command, 2, pcicmd);

    pcicmd = pci->read_pci_config(di->pci.bus, di->pci.device, di->pci.function, PCI_command, 2);
    dprintf("SM750: PCI Command aggiornato: 0x%04x\n", pcicmd);

    // 2. Gestione VGA Control (Sblocco MMIO/VGA)
    // Nota: Assicurati che SM750_PCI_VGA_CTRL sia l'offset corretto (di solito 0x88 nello spazio PCI per SM750)
    uint32 vga_ctrl = pci->read_pci_config(di->pci.bus, di->pci.device, di->pci.function, SM750_PCI_VGA_CTRL, 4);
    dprintf("SM750: PCI VGA_CTRL iniziale: 0x%08" B_PRIx32 "\n", vga_ctrl);

    vga_ctrl |= (1 << 7); // Spesso usato per sbloccare il Decode o MMIO
    pci->write_pci_config(di->pci.bus, di->pci.device, di->pci.function, SM750_PCI_VGA_CTRL, 4, vga_ctrl);

    vga_ctrl = pci->read_pci_config(di->pci.bus, di->pci.device, di->pci.function, SM750_PCI_VGA_CTRL, 4);
    dprintf("SM750: PCI VGA_CTRL aggiornato: 0x%08" B_PRIx32 "\n", vga_ctrl);

    
    /* TODO:
       Protezione: se il device è già aperto, potremmo voler saltare alcune inizializzazioni 
       hardware, ma per ora teniamo il conteggio */
    di->openCount++;

    /* Alloca la shared_info (che sarà condivisa con l'accelerante) */
    dprintf("ora creo di->shared_area");
    di->shared_area = create_area("sm750 shared info", (void **)&(di->si), 
          B_ANY_KERNEL_ADDRESS, B_PAGE_SIZE, B_FULL_LOCK, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA | B_CLONEABLE_AREA);
    dprintf("di->shared_area creata");
    if (di->shared_area < 0) {
        di->openCount--;
        return di->shared_area;
    }
    //di->si = si; // Colleghiamo la shared_info alla nostra DeviceInfo
    
    //memset(si, 0, B_PAGE_SIZE);
    memset(di->si, 0, B_PAGE_SIZE);
    
    di->si->settings = current_settings;
    di->si->vendor_id = di->pci.vendor_id;
    di->si->device_id = di->pci.device_id;
    di->si->revision  = di->pci.revision;
    di->si->bus = di->pci.bus;
    di->si->device = di->pci.device;
    di->si->function = di->pci.function;

    /* Mappatura Registri (BAR 0) */
    //si->regs_area = map_mem((void **)&si->regs, di->info.u.h0.base_registers[0], 
    //                        di->info.u.h0.base_register_sizes[0], "sm750_regs");
    dprintf("ora mappo la memoria per di->regs_area");
    di->regs_area = map_mem((void **)&di->regs, di->pci.u.h0.base_registers[0], 
                            di->pci.u.h0.base_register_sizes[0], "sm750_regs_k");
    dprintf("memoria mappata per di->regs_area");
    di->si->regs_area = di->regs_area;
    
    // ORA che di->regs è valido, puoi usare le macro o SM750_REG32
    /* lo facciamo fare a sm750_init_chip()
    if (di->regs != NULL) {
        vuint32* regs = di->regs; // Serve alle macro SYS_R/W se le usi qui
    
        // Power Mode 0
        SYS_W(BOOTSTRAP, SYS_R(BOOTSTRAP) & ~0x00000003);
    
        // Enable Clocks & 2D Engine
        uint32 val = SYS_R(MISC_CTRL);
        val |= 0x00000001; // Clock enable
        val |= (1 << 4);   // 2D Engine enable
        SYS_W(MISC_CTRL, val);
    
        // Reset Motore Grafico (GE)
        SYS_W(MISC_CTRL, val | 0x00010000);
        snooze(500);
        SYS_W(MISC_CTRL, val & ~0x00010000);
    
        dprintf("SM750: Chip wake-up sequence completed in open_device\n");
    }*/
    
    //si->regs = (vuint32*)(uintptr_t)di->regs;  // NO! area kernel non area utente
    di->si->regs = NULL;
    
    // 2. CALCOLO MEMORIA (Sposta qui la logica prima di mappare il BAR1)
    dprintf("SM750: Settings - Memory: %u MB, PCI reports: %u bytes\n", 
            di->si->settings.memory, (uint32)di->pci.u.h0.base_register_sizes[1]);
    
    // Prendi la dimensione reale riportata dal bus PCI
    uint32 pci_bar_size = di->pci.u.h0.base_register_sizes[1];
    uint32 final_mem_size = pci_bar_size;

    // Se il PCI non riporta nulla, o vogliamo forzare un minimo
    if (final_mem_size == 0) {
        dprintf("SM750: PCI BAR1 is empty, forcing 8MB...\n");
        final_mem_size = 8 * 1024 * 1024;
    }

    // Se l'utente ha impostato una dimensione nei settings, usiamola
    if (di->si->settings.memory > 0) {
        final_mem_size = di->si->settings.memory * 1024 * 1024;
    }

    // --- IL FILTRO DI SICUREZZA ---
    // Non possiamo MAI mappare più di quanto il PCI BAR ci permetta fisicamente
    if (final_mem_size > pci_bar_size && pci_bar_size > 0) {
        dprintf("SM750: WARNING - Requested %uMB, but PCI BAR limit is %uMB. Capping.\n", 
                final_mem_size / (1024*1024), pci_bar_size / (1024*1024));
        final_mem_size = pci_bar_size;
    }

    di->si->card_info.mem_size = final_mem_size;
    
    di->si->framebuffer_pci = (phys_addr_t)di->pci.u.h0.base_registers[1];
    /* quella che dovrebbe andare ma non va
    di->fb_area = map_mem((void **)&di->framebuffer, di->pci.u.h0.base_registers[1], 
                          final_mem_size, "sm750_fb_k");
    di->si->fb_area = di->fb_area;*/
    
    // --- FORZATURA INDIRIZZO VESA ---
addr_t phys_fb = 0xf4000000; // L'indirizzo che abbiamo letto dal BOOT_INFO
final_mem_size = 16 * 1024 * 1024; // 16MB per stare sicuri
dprintf("ora mappo la memoria per di->fb_area");
di->fb_area = map_mem((void **)&di->framebuffer, 
                      phys_fb, 
                      final_mem_size, 
                      "sm750_fb_k");
dprintf("memoria mappata per di->fb_area");
di->si->fb_area = di->fb_area;
//di->si->framebuffer = (uint32*)di->framebuffer; // Passiamo l'indirizzo virtuale all'accelerante
di->si->card_info.mem_size = final_mem_size;
    
    di->si->framebuffer = NULL;
    dprintf("SM750: BAR0 (Kernel) mappato a %p (Area: %d)\n", di->regs, di->regs_area);

    /* Mappatura Framebuffer (BAR 1) */
    //si->fb_area = map_mem((void **)&si->framebuffer, di->info.u.h0.base_registers[1], 
    //                      di->info.u.h0.base_register_sizes[1], "sm750_fb");
    //dprintf("SM750: BAR1 mappato a %p (Area: %d), Phys: 0x%lx\n", di->framebuffer, di->fb_area, di->framebuffer_pci);
    dprintf("SM750: BAR1 (Kernel) mappato a %p (Area: %d)\n", di->framebuffer, di->fb_area);
            
    if (di->framebuffer == NULL || di->fb_area < 0) {
        dprintf("SM750: ERRORE FATALE - Mappatura BAR1 fallita!\n");
        status = B_ERROR;
        goto error;
    }
    if (di->regs == NULL) {
        dprintf("SM750: ERRORE - Puntatore registri NULL!\n");
        status = B_ERROR;
        goto error;
    }

    /* Inizializziamo l'heap: 
       - start: 0 (offset relativo all'inizio del BAR1)
       - length: dimensione totale
       - blockSize: 4KB (allineamento standard pagine)
       - heapEntries: 128 (bastano per gestire i vari buffer)
    */
    //di->si->mem_mgr = mem_init("sm750_vram", 0, final_mem_size, 4096, 128); roba vecchia da radeon agp...
    di->si->cursor.pci_address = di->si->framebuffer_pci + (di->si->card_info.mem_size - 16384);
    di->si->cursor.v_address = (void*)((uint8*)di->framebuffer + (di->si->card_info.mem_size - 16384));
    //uint32 cursor_offset = (2 * 1024 * 1024) - 16384; // Es: a 2MB meno 16KB
    //di->si->cursor.pci_address = di->si->framebuffer_pci + cursor_offset;
    //di->si->cursor.v_address = (void*)((uint8*)di->framebuffer + cursor_offset);
    di->si->mem_mgr = NULL; // Opzionale, se vuoi tenere il campo nella struct per ora
    // TODO: scrivere un gestore della memoria per assegnare memoria a cursore, framebuffer, overlay ecc..
    
    /*if (!di->si->mem_mgr) {
        //delete_area(si->regs_area);
        //delete_area(si->fb_area);
        //delete_area(shared_area);
        //return B_NO_MEMORY;
        status = B_NO_MEMORY;
        goto error;
    }*/

    /* 2. ESECUZIONE COLDSTART (Ora il chip è sveglio) */
    dprintf("SM750: Test lettura registro 0...\n");
    //uint32 val = di->regs[0];
    vuint32* regs = di->regs; 
    uint32 val = SM750_REG32(0x000000); 
    dprintf("SM750: ID letto via MMIO: 0x%08x\n", val);
        
    dprintf("SM750: open_device() - Inizializzazione chip...\n");
    sm750_init_chip(di);
    
    //vuint32* regs = di->regs;
    uint32 chip_id = SM750_REG32(0x000000);
    dprintf("SM750: Test lettura ID dopo init: 0x%08x\n", chip_id);
    
    /* 3. Allocazione spazio per il Cursore Hardware (64x64 pixel @ 32bpp = 16KB) */
    /* Lo mettiamo in fondo alla memoria per non dare fastidio al frontbuffer */
    //uint32 cursor_block;
    //uint32 cursor_offset;
    

    
    /*if (mem_alloc(di->si->mem_mgr, 16384, (void*)"cursor", &cursor_block, &cursor_offset) == B_OK) {
        di->si->cursor.pci_address = di->si->framebuffer_pci + cursor_offset;
        
        // USA di->framebuffer per il kernel!
        di->si->cursor.v_address = (void*)((uint8*)di->framebuffer + cursor_offset);
    }*/

    /* Passiamo il puntatore alla shared_info come cookie per le altre chiamate */
    //*cookie = si;
    *cookie = di;
    dprintf("SM750: DEBUG - Area IDs to clone: Shared=%d, Regs=%d, FB=%d\n", 
        di->shared_area, di->regs_area, di->fb_area);
    dprintf("SM750: Device %s opened successfully.\n", di->name);
    
    //dprintf("SM750: open_device() - Avvio Pixel Test...\n");
    //sm750_pixel_test(si);
    
    return B_OK;
error:
    /* Pulizia in caso di fallimento */
    //if (di->si && di->si->mem_mgr) mem_destroy(di->si->mem_mgr);
    delete_area(di->regs_area);
    delete_area(di->fb_area);
    delete_area(di->shared_area);
    di->openCount--;
    return status;
}

static status_t
handle_pci_ioctl(DeviceInfo *di, uint32 op, void *arg)
{
    //sm750_get_set_pci *pci_data = (sm750_get_set_pci *)arg;
    sm750_get_set_pci pci_data;
    // Copia i parametri dallo spazio utente al kernel
    if (user_memcpy(&pci_data, arg, sizeof(pci_data)) != B_OK)
        return B_BAD_ADDRESS;
    
    // Sicurezza: controlla il magic number se lo hai definito
    // if (pci_data->magic != SM750_PRIVATE_DATA_MAGIC) return B_BAD_VALUE;

    if (op == ENG_GET_PCI) {
        pci_data.value = pci->read_pci_config(
            di->pci.bus, di->pci.device, di->pci.function, 
            pci_data.offset, pci_data.size);
        return user_memcpy(arg, &pci_data, sizeof(pci_data));
    } else if (op == ENG_SET_PCI) {
        pci->write_pci_config(
            di->pci.bus, di->pci.device, di->pci.function, 
            pci_data.offset, pci_data.size, pci_data.value
        );
        return B_OK;
    }

    return B_DEV_INVALID_IOCTL;
}

/* --- control_device (IOCTL) --- */
static status_t
control_device(void *cookie, uint32 op, void *arg, size_t len)
{
    DeviceInfo *di = (DeviceInfo *)cookie; // Il cookie è DeviceInfo!
    //shared_info *si = di->si; //not used
    
    switch (op) {
        /*case ENG_GET_PRIVATE_DATA:
            {
                sm750_get_private_data *gpd = (sm750_get_private_data *)arg;
                // Fondamentale: passiamo l'area della shared_info, 
                // non quella dei registri!
                gpd->shared_info_area = di->shared_area; 
                return B_OK;
            }*/
        case ENG_GET_PRIVATE_DATA: {
            sm750_get_private_data gpd;
            if (user_memcpy(&gpd, arg, sizeof(gpd)) != B_OK)
                return B_BAD_ADDRESS;
            if (gpd.magic != SM750_PRIVATE_DATA_MAGIC) {
                dprintf("SM750: Errore Magic Number! Atteso 0x%" B_PRIx32 ", ricevuto 0x%" B_PRIx32 "\n", 
                        (uint32)SM750_PRIVATE_DATA_MAGIC, gpd.magic);
                return B_BAD_VALUE;
            }
            //memset(&gpd, 0, sizeof(gpd));
            //gpd.magic = SM750_PRIVATE_DATA_MAGIC;
            gpd.shared_info_area = di->shared_area; // DEVE ESSERE di->shared_area!
            return user_memcpy(arg, &gpd, sizeof(gpd));
        }
        case ENG_GET_PCI:
        case ENG_SET_PCI:
            // Qui useremo pci->read_pci_config come abbiamo fatto in init.c
            // serve per permettere all'accelerante di leggere i registri PCI
            return handle_pci_ioctl(di, op, arg);
        case B_GET_ACCELERANT_SIGNATURE:
            strcpy((char *)arg, "sm750.accelerant");
            return B_OK;
        /* Qui aggiungeremo in futuro i controlli PCI se necessari */
    }
    return B_DEV_INVALID_IOCTL;
}

/* --- Altri hook minimi --- */
static status_t close_device(void *cookie) { return B_OK; }
static status_t 
free_device(void *cookie) 
{ 
    DeviceInfo *di = (DeviceInfo *)cookie;
    if (di == NULL) return B_BAD_VALUE;

    /* Decrementiamo il conteggio. Se ci sono altre app che usano il driver, 
       non dobbiamo ancora distruggere tutto. */
    di->openCount--;

    if (di->openCount == 0) {
        dprintf("SM750: Last device closed. Cleaning up resources...\n");

        if (di->si != NULL) {
            /* 1. Distruggi il gestore memoria video */
            if (di->si->mem_mgr != NULL) {
                //mem_destroy(di->si->mem_mgr);
                //di->si->mem_mgr = NULL;
                area_id area_to_delete = di->shared_area;
                di->si = NULL;
                delete_area(area_to_delete);
            }
            
            /* 2. Cancella l'area della shared_info tramite il suo ID salvato */
            area_id area_to_delete = di->shared_area;
            di->si = NULL;
            di->shared_area = -1;
            delete_area(area_to_delete);
        }

        /* 3. Cancella le aree mappate nel Kernel */
        if (di->regs_area >= 0) {
            delete_area(di->regs_area);
            di->regs_area = -1;
            di->regs = NULL;
        }

        if (di->fb_area >= 0) {
            delete_area(di->fb_area);
            di->fb_area = -1;
            di->framebuffer = NULL;
        }
    }
    return B_OK;
}
static status_t read_device(void *cookie, off_t pos, void *buf, size_t *len) { return B_NOT_ALLOWED; }
static status_t write_device(void *cookie, off_t pos, const void *buf, size_t *len) { return B_NOT_ALLOWED; }
