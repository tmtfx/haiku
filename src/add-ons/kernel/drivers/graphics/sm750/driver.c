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

#define VENDOR_ID_SMI 0x126f
#define DEVICE_ID_SM750 0x0750

int32 api_version = B_CUR_DRIVER_API_VERSION;

static const struct ChipInfo sm750_chips[] = {
    { 0x0750, 0, "SM750 LynxExp" },
    { 0x0751, 1, "SM750LE" },
    { 0, 0, NULL }
};

static sm750_settings current_settings = {
    "sm750.accelerant", true, 0, 0, false, true, true
};

static DeviceInfo *devices[8];
pci_module_info *pci;

/* --- Prototypes --- */
static status_t open_device(const char* name, uint32 flags, void** cookie);
static status_t close_device(void* dev);
static status_t free_device(void* dev);
static status_t read_device(void* dev, off_t pos, void* buf, size_t* len);
static status_t write_device(void* dev, off_t pos, const void* buf, size_t* len);
static status_t control_device(void* dev, uint32 msg, void* buf, size_t len);

static device_hooks gDeviceHooks = {
    open_device, close_device, free_device, control_device,
    read_device, write_device, NULL, NULL, NULL, NULL
};

/* Helper per la mappatura */
static area_id
map_mem(void **out_virt, phys_addr_t phys, uint32 size, const char *name)
{
    void *virt = NULL;
    size = (size + B_PAGE_SIZE - 1) & ~(B_PAGE_SIZE - 1);
    area_id area = map_physical_memory(name, phys, size, B_ANY_KERNEL_ADDRESS, 
        B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA | B_CLONEABLE_AREA, &virt);

    if (area < B_OK) {
        dprintf("SM750 ERROR: map_physical_memory(%s) failed\n", name);
        *out_virt = NULL;
    } else {
        *out_virt = virt;
    }
    return area;
}

/* --- Hooks di sistema --- */
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

status_t init_driver(void) {
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
            devices[count]->pChipInfo = (info.device_id == 0x0751) ? &sm750_chips[1] : &sm750_chips[0];
            sprintf(devices[count]->name, "graphics/sm750_%02x%02x%02x", info.bus, info.device, info.function);
            
            dprintf("SM750: Trovato %s a PCI %d:%d:%d\n", 
                devices[count]->pChipInfo->chipName, info.bus, info.device, info.function);
            count++;
        }
    }
    devices[count] = NULL;
    return count > 0 ? B_OK : ENODEV;
}

void uninit_driver(void) {
    for (int32 i = 0; devices[i]; i++) free(devices[i]);
    put_module(B_PCI_MODULE_NAME);
}

const char **publish_devices(void) {
    static const char *names[9];
    int32 i;
    for (i = 0; devices[i]; i++) names[i] = devices[i]->name;
    names[i] = NULL;
    return names;
}

device_hooks *find_device(const char *name) {
    for (int32 i = 0; devices[i]; i++) {
        if (strcmp(name, devices[i]->name) == 0) return &gDeviceHooks;
    }
    return NULL;
}

/* --- open_device --- */
static status_t
open_device(const char *name, uint32 flags, void **cookie)
{
    DeviceInfo *di = NULL;
    for (int32 i = 0; devices[i]; i++) {
        if (strcmp(name, devices[i]->name) == 0) { di = devices[i]; break; }
    }
    if (!di) return B_BAD_VALUE;

    if (di->openCount == 0) {
        // 1. Abilitazione Bus Master e Memoria PCI
        uint16 pcicmd = pci->read_pci_config(di->pci.bus, di->pci.device, di->pci.function, PCI_command, 2);
        pcicmd |= PCI_command_memory | PCI_command_master;
        pci->write_pci_config(di->pci.bus, di->pci.device, di->pci.function, PCI_command, 2, pcicmd);

        // 2. Sblocco MMIO (VGA Control)
        uint32 vga_ctrl = pci->read_pci_config(di->pci.bus, di->pci.device, di->pci.function, SM750_PCI_VGA_CTRL, 4);
        vga_ctrl |= (1 << 7); 
        pci->write_pci_config(di->pci.bus, di->pci.device, di->pci.function, SM750_PCI_VGA_CTRL, 4, vga_ctrl);

        // 3. Allocazione Shared Info
        di->shared_area = create_area("sm750 shared info", (void **)&(di->si), 
            B_ANY_KERNEL_ADDRESS, B_PAGE_SIZE, B_FULL_LOCK, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA | B_CLONEABLE_AREA);
        if (di->shared_area < 0) return di->shared_area;
        memset(di->si, 0, B_PAGE_SIZE);
        strncpy(di->si->device_path, name, B_PATH_NAME_LENGTH);
        memcpy(&di->si->settings, &current_settings, sizeof(sm750_settings));

        // 4. Mappatura REGISTRI (BAR 1 - 2MB)
        di->regs_area = map_mem((void **)&di->regs, di->pci.u.h0.base_registers[1], 
                               di->pci.u.h0.base_register_sizes[1], "sm750_regs_k");
        
        // 5. Mappatura FRAMEBUFFER (BAR 0 - 64MB)
        // Usiamo la dimensione riportata dal PCI o forziamo 16MB per sicurezza iniziale
        //uint32 mem_size = di->pci.u.h0.base_register_sizes[0];
        //if (mem_size > 64*1024*1024) mem_size = 64*1024*1024; 

        di->fb_area = map_mem((void **)&di->framebuffer, di->pci.u.h0.base_registers[0], 
                             di->pci.u.h0.base_register_sizes[0], "sm750_fb_k");

        if (di->regs_area < 0 || di->fb_area < 0) {
            delete_area(di->shared_area);
            return B_ERROR;
        }

        // 6. Inizializzazione Chip (Wake up)
        di->si->regs = NULL; // L'accelerante mapperà la sua versione
        di->si->framebuffer = NULL; // idem
        di->si->regs_area = di->regs_area; // Passa l'ID         
        di->si->fb_area = di->fb_area;     // Passa l'ID numerico
        di->si->framebuffer_pci = (phys_addr_t)di->pci.u.h0.base_registers[0];
       
        sm750_init_chip(di);
    }

    di->openCount++;
    *cookie = di;
    return B_OK;
}

/* --- control_device --- */
static status_t
control_device(void *cookie, uint32 op, void *arg, size_t len)
{
    DeviceInfo *di = (DeviceInfo *)cookie;
    switch (op) {
        case ENG_GET_PRIVATE_DATA: {
            sm750_get_private_data gpd;
            if (user_memcpy(&gpd, arg, sizeof(gpd)) != B_OK) return B_BAD_ADDRESS;
            if (gpd.magic != SM750_PRIVATE_DATA_MAGIC) return B_BAD_VALUE;
            gpd.shared_info_area = di->shared_area;
            return user_memcpy(arg, &gpd, sizeof(gpd));
        }
        case B_GET_ACCELERANT_SIGNATURE:
            strcpy((char *)arg, "sm750.accelerant");
            return B_OK;
    }
    return B_DEV_INVALID_IOCTL;
}

static status_t close_device(void *cookie) { return B_OK; }

static status_t free_device(void *cookie) {
    DeviceInfo *di = (DeviceInfo *)cookie;
    if (--di->openCount == 0) {
        delete_area(di->shared_area);
        delete_area(di->regs_area);
        delete_area(di->fb_area);
        di->si = NULL;
        di->regs = NULL;
        di->framebuffer = NULL;
    }
    return B_OK;
}

static status_t read_device(void *cookie, off_t pos, void *buf, size_t *len) { return B_NOT_ALLOWED; }
static status_t write_device(void *cookie, off_t pos, const void *buf, size_t *len) { return B_NOT_ALLOWED; }
