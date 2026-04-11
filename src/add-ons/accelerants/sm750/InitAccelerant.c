/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include <Accelerant.h>
#include <stdlib.h>
#include <Debug.h>
#include "DriverInterface.h"
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include "protos.h"
#include "sm750_macros.h"

status_t sm750_init_accelerant(int fd);
void sm750_uninit_accelerant(void);

//static accelerant_info gInfo;
accelerant_info g_info = { .shared_info_area = -1, .regs_area = -1, .fb_area = -1 }; // Rimuovi static per renderla globale
accelerant_info *gInfo = &g_info;


static status_t init_common(int fd) {
	debug_printf("SM750_ACC: Inizio init_common\n");
	//memset(gInfo, 0, sizeof(accelerant_info));
    gInfo->fd = fd;

    sm750_get_private_data gpd;
    gpd.magic = SM750_PRIVATE_DATA_MAGIC;

    if (ioctl(fd, ENG_GET_PRIVATE_DATA, &gpd, sizeof(gpd)) != B_OK) {
        debug_printf("SM750_ACC: ERRORE ioctl fallita!\n");
        return B_ERROR;
    }
    
    debug_printf("SM750_ACC: Area ID ricevuta dal driver: %d\n", (int)gpd.shared_info_area);

    gInfo->shared_info_area = clone_area("sm750 shared info", (void **)&(gInfo->si),
        B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA, gpd.shared_info_area);
    if (gInfo->shared_info_area < 0) {
        debug_printf("SM750_ACC: ERRORE clone_area fallito: 0x%08x\n", (int)gInfo->shared_info_area);
        return gInfo->shared_info_area;
    }
    debug_printf("SM750_ACC: Shared Info clonato a %p\n", gInfo->si);

    if (gInfo->si->regs_area < 0) {
        debug_printf("SM750_ACC: ERRORE - ID area registri non valido nel driver!\n");
        return B_ERROR;
    }
    gInfo->regs_area = clone_area("sm750 regs area", (void **)&(gInfo->regs),
        B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA, gInfo->si->regs_area);
    debug_printf("SM750_ACC: Registri clonati a %p (ID: %d)\n", gInfo->regs, (int)gInfo->regs_area);
    
    if (gInfo->regs_area < 0) {
    	debug_printf("SM750_ACC: ERRORE clone registri fallito!\n");
        delete_area(gInfo->shared_info_area);
        return gInfo->regs_area;
    }
    vuint32* regs = gInfo->regs;
    debug_printf("SM750_ACC: MMIO ID letto dall'accelerante: 0x%08x\n", SM750_REG32(0x000000));

    //gInfo->fb_area = clone_area("sm750 fb user", (void **)&(gInfo->si->framebuffer),
    //    B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA, gInfo->si->fb_area);
    void* user_fb_ptr = NULL;
    gInfo->fb_area = clone_area("sm750 fb user", &user_fb_ptr,
        B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA, gInfo->si->fb_area);
    if (gInfo->fb_area < 0) {
        debug_printf("SM750_ACC: ERRORE clone framebuffer fallito: %d\n", gInfo->fb_area);
        delete_area(gInfo->regs_area);
        delete_area(gInfo->shared_info_area);
        return gInfo->fb_area;
    }

    gInfo->si->framebuffer = (uint8*)user_fb_ptr;
    
    debug_printf("SM750_ACC: Framebuffer clonato con successo all'indirizzo %p\n", gInfo->si->framebuffer);
    debug_printf("SM750_ACC: Avvio test pixel fucsia da inviare al framebuffer!\n");
    uint32 *fb = (uint32*)gInfo->si->framebuffer;
    for (int i = 0; i < 500000; i++) {
        fb[i] = 0x00FF00FF; // Fucsia/Magenta
    }
    debug_printf("SM750_ACC: Framebuffer riempito. Attesa 2 secondi...\n");
    snooze(2000000); 
    debug_printf("SM750_ACC: Fine attesa.\n");
    return B_OK;
}


status_t 
sm750_init_accelerant(int fd) 
{
	gInfo->is_clone = false;
    status_t result = init_common(fd);
    
    if (result == B_OK) {
        /* Impediamo aperture multiple dell'accelerante primario */
        if (gInfo->si->accelerant_in_use) {
            sm750_uninit_accelerant();
            return B_NOT_ALLOWED;
        }
        
        gInfo->si->accelerant_in_use = true;
    }
    return result;
}

void 
sm750_uninit_accelerant(void) {
    /* 1. Segna che l'accelerante è libero (solo se siamo il primario) */
    if (!gInfo->is_clone && gInfo->si != NULL) {
        gInfo->si->accelerant_in_use = false;
    }

    /* 2. Rilascia le aree (l'ordine non è critico qui, ma facciamolo pulito) */
    //if (gInfo->shared_info_area >= 0)
    delete_area(gInfo->shared_info_area);
    //if (gInfo->regs_area >= 0)
    delete_area(gInfo->regs_area);
    
    gInfo->regs = NULL;
    gInfo->si = NULL;
    gInfo->regs_area = -1;
    gInfo->shared_info_area = -1;
}

/* Entry point richiesto da Haiku */
void*
get_accelerant_hook(uint32 feature, void* data)
{
    switch (feature) {
        /* Gestione Accelerante */
        case B_INIT_ACCELERANT:
            return (void*)sm750_init_accelerant;
        case B_UNINIT_ACCELERANT:
            return (void*)sm750_uninit_accelerant;
        
        /* Gestione Modi Video (Aggiungi questi!) */
        case B_SET_DISPLAY_MODE:
            return (void*)sm750_set_display_mode;
        case B_GET_DISPLAY_MODE:
            return (void*)sm750_get_display_mode;
        case B_PROPOSE_DISPLAY_MODE:
            return (void*)sm750_propose_display_mode;
        case B_GET_FRAME_BUFFER_CONFIG:
            return (void*)sm750_get_frame_buffer_config;

        /* Se hai già le funzioni per il cursore, aggiungi anche queste */
        /*case B_SET_CURSOR_SHAPE:
            return (void*)sm750_set_cursor_shape;
        case B_MOVE_CURSOR:
            return (void*)sm750_move_cursor;
        case B_SHOW_CURSOR:
            return (void*)sm750_show_cursor;*/
        
        /* Hook per l'engine 2D (accelerazione hardware) */
        /*case B_FILL_RECTANGLE:
			return (void*)sm750_fill_rectangle;
		case B_SCREEN_TO_SCREEN_BLIT:
			return (void*)sm750_screen_to_screen_blit;*/

        case B_SET_CURSOR_SHAPE:
            return (void*)sm750_set_cursor_shape;
        case B_MOVE_CURSOR:
            return (void*)sm750_move_cursor;
        case B_SHOW_CURSOR:
            return (void*)sm750_show_cursor;
        case B_FILL_RECTANGLE:
        case B_SCREEN_TO_SCREEN_BLIT:
            return NULL;
        
        default:
            return NULL;
    }
}
