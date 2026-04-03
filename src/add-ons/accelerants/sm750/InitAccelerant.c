/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include <Accelerant.h>
#include <stdlib.h>
#include "DriverInterface.h"
#include <errno.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    int         fd;                 /* File descriptor del driver /dev/graphics/... */
    shared_info *si;                /* Puntatore alla shared info clonata */
    area_id     shared_info_area;   /* ID area shared info */
    uint32      *regs;              /* Puntatore ai registri MMIO clonati */
    area_id     regs_area;          /* ID area registri */
    bool        is_clone;           /* Vero se è un clone */
} accelerant_info;

static accelerant_info gInfo;

static status_t init_common(int fd) {
    gInfo.fd = fd;

    sm750_get_private_data gpd;
    gpd.magic = SM750_PRIVATE_DATA_MAGIC;

    /* 1. Ottieni ID aree dal driver */
    if (ioctl(fd, ENG_GET_PRIVATE_DATA, &gpd, sizeof(gpd)) != B_OK)
        return B_ERROR;

    /* 2. Clona la Shared Info */
    gInfo.shared_info_area = clone_area("sm750 shared info", (void **)&(gInfo.si),
        B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA, gpd.shared_info_area);
    if (gInfo.shared_info_area < 0) return gInfo.shared_info_area;

    /* 3. Clona i Registri MMIO (BAR0) */
    /* Usiamo l'ID salvato nella shared_info dal driver kernel */
    gInfo.regs_area = clone_area("sm750 regs area", (void **)&(gInfo.regs),
        B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA, gInfo.si->regs_area);
    
    if (gInfo.regs_area < 0) {
        delete_area(gInfo.shared_info_area);
        return gInfo.regs_area;
    }

    return B_OK;
}

/* Entry point principale */
status_t init_accelerant(int fd) {
    gInfo.is_clone = false;
    status_t result = init_common(fd);
    
    if (result == B_OK) {
        /* Se l'accelerante è già in uso da un'altra istanza primaria, ritorna errore */
        if (gInfo.si->accelerant_in_use) {
            uninit_accelerant();
            return B_NOT_ALLOWED;
        }
        
        /* Inizializza il lock dell'engine (Benaphore) */
        // result = INIT_BEN(gInfo.si->engine_lock);
        
        gInfo.si->accelerant_in_use = true;
    }
    return result;
}

void uninit_accelerant(void) {
    /* Rilascia le aree */
    delete_area(gInfo.regs_area);
    delete_area(gInfo.shared_info_area);
    
    gInfo.regs = NULL;
    gInfo.si = NULL;

    /* Se non è un clone, segna che l'accelerante è libero */
    if (!gInfo.is_clone && gInfo.si) {
        gInfo.si->accelerant_in_use = false;
    }
}
