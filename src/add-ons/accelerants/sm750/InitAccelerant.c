/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include <Accelerant.h>
#include <stdlib.h>
#include <Debug.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "DriverInterface.h"
#include "protos.h"
#include "sm750_macros.h"

/* gInfo globale per l'accelerante */
accelerant_info g_info = { .shared_info_area = -1, .regs_area = -1, .fb_area = -1 };
accelerant_info *gInfo = &g_info;

static status_t init_common(int fd) {
    debug_printf("SM750_ACC: Inizio init_common\n");
    gInfo->fd = fd;

    /* 1. Recupera l'area shared_info dal driver tramite IOCTL */
    sm750_get_private_data gpd;
    gpd.magic = SM750_PRIVATE_DATA_MAGIC;

    if (ioctl(fd, ENG_GET_PRIVATE_DATA, &gpd, sizeof(gpd)) != B_OK) {
        debug_printf("SM750_ACC: ERRORE ioctl ENG_GET_PRIVATE_DATA fallita!\n");
        return B_ERROR;
    }
    
    /* 2. Clona la shared_info */
    gInfo->shared_info_area = clone_area("sm750 shared info", (void **)&(gInfo->si),
        B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA, gpd.shared_info_area);
    
    if (gInfo->shared_info_area < 0) return gInfo->shared_info_area;
    if (gInfo->si->regs_area <= 0) {
        debug_printf("SM750_ACC: ERRORE! regs_area invalida nella shared_info!\n");
        return B_ERROR;
    }
    /* 3. Clona i registri MMIO (BAR1) */
    gInfo->regs_area = clone_area("sm750 regs user", (void **)&(gInfo->regs),
        B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA, gInfo->si->regs_area);
    
    if (gInfo->regs_area < 0) {
    	debug_printf("SM750_ACC: Errore fatale clone_area: %s\n", strerror(gInfo->regs_area));
        delete_area(gInfo->shared_info_area);
        return gInfo->regs_area;
    }

    /* Test lettura ID per conferma MMIO */
    //vuint32* regs = gInfo->regs;
    if (gInfo->regs == NULL) {
        debug_printf("SM750_ACC: ERRORE CRITICO! gInfo->regs è NULL dopo il clone!\n");
        return B_ERROR;
    }
    // fin qui funziona tutto da syslog rimosso i vari debug_prinft
    
    /* 4. Clona il Framebuffer (BAR0) */
    void* fb_ptr = NULL;
    gInfo->fb_area = clone_area("sm750 fb user", &fb_ptr,
        B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA, gInfo->si->fb_area);
    
    if (gInfo->fb_area < 0) {
        delete_area(gInfo->regs_area);
        delete_area(gInfo->shared_info_area);
        return gInfo->fb_area;
    }

    /* IMPORTANTE: salviamo il puntatore virtuale LOCALMENTE */
    //gInfo->framebuffer = (uint8*)fb_ptr; TOLTO, ci deve essere solo un framebuffer in shared_info e basta
    gInfo->si->framebuffer = (uint8*)fb_ptr;
    
    /* --- TEST PIXEL (OPZIONALE) --- */
    /*debug_printf("SM750_ACC: Test Framebuffer...\n");
    uint32 *fb = (uint32*)gInfo->framebuffer;
    // Riempiamo i primi 2MB (circa) per non eccedere se la memoria è poca
    for (int i = 0; i < (512 * 1024); i++) {
        fb[i] = 0x00FF00FF; 
    }
    debug_printf("SM750_ACC: Fine Test Framebuffer...\n");*/
    
    shared_info *si = gInfo->si;
    bool is_panel = si->card_info.is_panel;

    // Scegliamo il buffer corretto dove salvare i dati
    uint8* edid_buffer = is_panel ? si->edid_panel : si->edid_crt;
	debug_printf("Inizio lettura EDID...\n");
    if (sm750_read_edid(edid_buffer) == B_OK) {
        // Segnamo che abbiamo trovato i dati
        if (is_panel) si->card_info.has_edid_panel = true;
        else si->card_info.has_edid_crt = true;
        debug_printf("SM750_ACC: EDID letto con successo su %s\n", is_panel ? "PANEL" : "CRT");
        if (create_mode_list_from_edid(edid_buffer) == B_OK) {
            debug_printf("SM750_ACC: Lista modi generata (%d modi trovati)\n", si->mode_count);
        } else {
            debug_printf("SM750_ACC: EDID presente ma nessun Detailed Timing valido trovato.\n");
            goto fallback;
        }
    } else {
fallback:
        debug_printf("SM750: EDID non disponibile, uso modalità provvisoria 1024x768 su %s\n", is_panel ? "PANEL" : "CRT");
        display_mode safe_mode = {
            { 65000, 1024, 1048, 1184, 1344, 768, 771, 777, 806, 0 },
            B_RGB32, 1024, 768, 0, 0
        };
        safe_mode.timing.flags = B_POSITIVE_HSYNC | B_POSITIVE_VSYNC;
        si->mode_list[0] = safe_mode;
        si->preferred_mode = safe_mode;
        si->mode_count = 1; 
        si->card_info.has_edid_panel = false;
        si->card_info.has_edid_crt = false;
    }
    

    return B_OK;
}

status_t sm750_init_accelerant(int fd) {
    gInfo->is_clone = false;
    status_t result = init_common(fd);
    
    if (result == B_OK) {
        if (gInfo->si->accelerant_in_use) {
            sm750_uninit_accelerant();
            return B_NOT_ALLOWED;
        }
        gInfo->si->accelerant_in_use = true;
    }
    return result;
}

void sm750_uninit_accelerant(void) {
    if (gInfo->si != NULL && !gInfo->is_clone) {
        gInfo->si->accelerant_in_use = false;
    }

    if (gInfo->fb_area >= 0) delete_area(gInfo->fb_area);
    if (gInfo->regs_area >= 0) delete_area(gInfo->regs_area);
    if (gInfo->shared_info_area >= 0) delete_area(gInfo->shared_info_area);
    
    gInfo->regs = NULL;
    gInfo->si->framebuffer = NULL;
    gInfo->si = NULL;
}

void* get_accelerant_hook(uint32 feature, void* data) {
    switch (feature) {
        case B_INIT_ACCELERANT:             return (void*)sm750_init_accelerant;
        case B_UNINIT_ACCELERANT:           return (void*)sm750_uninit_accelerant;
        case B_ACCELERANT_CLONE_INFO_SIZE:  return (void*)sm750_accelerant_clone_info_size;
        case B_GET_ACCELERANT_CLONE_INFO:   return (void*)sm750_get_accelerant_clone_info;
        case B_CLONE_ACCELERANT:            return (void*)sm750_clone_accelerant;
        case B_GET_ACCELERANT_DEVICE_INFO:  return (void*)sm750_get_accelerant_device_info;
        
        /* Display Modes */
        case B_ACCELERANT_MODE_COUNT:       return (void*)sm750_accelerant_mode_count;
        case B_GET_MODE_LIST:               return (void*)sm750_get_mode_list;
        case B_GET_PREFERRED_DISPLAY_MODE:  return (void*)sm750_get_preferred_mode;
        case B_PROPOSE_DISPLAY_MODE:        return (void*)sm750_propose_display_mode;
        case B_SET_DISPLAY_MODE:            return (void*)sm750_set_display_mode;
        case B_GET_DISPLAY_MODE:            return (void*)sm750_get_display_mode;
        case B_GET_FRAME_BUFFER_CONFIG:     return (void*)sm750_get_frame_buffer_config;
        case B_GET_PIXEL_CLOCK_LIMITS:      return (void*)sm750_get_pixel_clock_limits;
        case B_GET_EDID_INFO:               return (void*)sm750_get_edid_info;
        
        /* Cursor */
        case B_SET_CURSOR_SHAPE:            return (void*)sm750_set_cursor_shape;
        case B_MOVE_CURSOR:                 return (void*)sm750_move_cursor;
        case B_SHOW_CURSOR:                 return (void*)sm750_show_cursor;
        
        /* Engine */
        case B_MOVE_DISPLAY:                return (void*)sm750_move_display_area;

        case B_ACCELERANT_ENGINE_COUNT:     return (void*)sm750_accelerant_engine_count;
        case B_ACQUIRE_ENGINE:              return (void*)sm750_acquire_engine;
        case B_RELEASE_ENGINE:              return (void*)sm750_release_engine;
        case B_WAIT_ENGINE_IDLE:            return (void*)sm750_wait_engine_idle;
        case B_GET_SYNC_TOKEN:              return (void*)sm750_get_sync_token;
        case B_SYNC_TO_TOKEN:               return (void*)sm750_sync_to_token;
        /* Hook per l'engine 2D (accelerazione hardware) */
        /*case B_FILL_RECTANGLE:
			return (void*)sm750_fill_rectangle;
		case B_SCREEN_TO_SCREEN_BLIT:
			return (void*)sm750_screen_to_screen_blit;*/
        default: return NULL;
    }
}
