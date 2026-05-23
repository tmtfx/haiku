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
#include <edid.h>
#include <create_display_modes.h>
#include "memory_manager.h"
#include "DriverInterface.h"
#include "protos.h"
#include "sm750_macros.h"
#include "common_modes.h"

#define CALLED() debug_printf("SM750_ACC: %s\n", __FUNCTION__)

/* gInfo globale per l'accelerante */
accelerant_info g_info = { .shared_info_area = -1, .regs_area = -1, .fb_area = -1 };
accelerant_info *gInfo = &g_info;


static status_t init_vram_manager(shared_info* si) 
{
    // Usiamo il valore calcolato in init_chip
    uint32 desktopReserve = si->card_info.max_desktop_mem; 

    // L'heap per l'overlay e il cursore parte subito dopo la riserva desktop
    uint32 heapStart = desktopReserve;
    uint32 heapSize = si->card_info.mem_size - desktopReserve;

    debug_printf("SM750_ACC: Heap VRAM allocato a 0x%x (Size: %u KB)\n", heapStart, heapSize / 1024);

    // Inizializziamo l'heap sulla seconda metà della RAM
    // usiamo local_mem_mgr su stack per evitare smap con si->mem_mgr
    void* local_mem_mgr = (void*)mem_init("sm750_vram_heap", heapStart, heapSize, 8, 128);
    
    if (local_mem_mgr == NULL) {
        debug_printf("SM750_ACC ERROR: mem_init fallito!\n");
        return B_ERROR;
    }

    // --- ALLOCAZIONE CURSORE ---
    // Il cursore della SM750 in modalità "3-color + transparency" occupa 16KB.
    uint32 cursorBlockID;
    uint32 cursorOffset;
    status_t status = mem_alloc((mem_info*)local_mem_mgr, 16384, (void*)0x43555253, 
                                &cursorBlockID, &cursorOffset);
    
    if (status == B_OK) {
        si->cursor.vram_offset = cursorOffset;
        si->cursor.block_id = cursorBlockID;
        si->mem_mgr = local_mem_mgr;
        debug_printf("SM750_ACC: Cursore allocato dinamicamente a offset 0x%x\n", cursorOffset);
    } else {
        debug_printf("SM750_ACC ERROR: Impossibile allocare memoria per il cursore!\n");
    }

    return B_OK;
}


static status_t 
create_mode_list() 
{
    display_mode* list = NULL;
    uint32 count = 0;
    area_id new_area = -1;
    edid1_info info;
    bool edid_valid = false;
    uint8 temp_edid_raw[128];
    gInfo->has_edid_panel = false;
    gInfo->has_edid_crt = false;
    bool is_panel = gInfo->si->card_info.is_panel;

    // 1. TENTA IL RILEVAMENTO HARDWARE (I2C)
    if (sm750_read_edid(temp_edid_raw) == B_OK) {
        // Segnamo che abbiamo trovato i dati
        // lo facciamo in create_mode_list_from_edid
        //if (is_panel) gInfo->has_edid_panel = true;
        //else gInfo->has_edid_crt = true;
        debug_printf("SM750_ACC: Succesfully read EDID from %s\n", is_panel ? "PANEL" : "CRT");
        if (create_mode_list_from_edid(temp_edid_raw) == B_OK) {
            debug_printf("SM750_ACC: Modes list updated with monitor data.\n");
            edid_valid = true;
        }
    }
    if (!edid_valid && gInfo->si->card_info.has_edid_vesa) {
        debug_printf("SM750_ACC: I2C EDID read failed. Trying VESA fallback...\n");
        if (create_mode_list_from_edid(NULL) == B_OK) {
            debug_printf("SM750_ACC: Modes list updated with VESA fallback data.\n");
            edid_valid = true;
        }
    }

    // 3. GENERAZIONE DELLA LISTA
    if (edid_valid) {
        // Funzione magica di libkernel_graphics.a: crea un'area con tutti i modi 
        // supportati dal monitor basandosi sui timing EDID.
        new_area = create_display_modes("sm750 modes", &info, 
            NULL, 0, NULL, 0, NULL, &list, &count);
    } 
    
    // 4. FALLBACK ESTREMO: BIOS O 1024x768
    if (new_area < 0) {
        debug_printf("SM750_ACC: Nessun EDID valido. Uso fallback BIOS/VesaTable.\n");
        
        // Creiamo un'area manualmente per contenere i nostri modi di fallback
        //size_t area_size = (MAX_EDID_MODES * sizeof(display_mode) + B_PAGE_SIZE - 1) 
        //    & ~(B_PAGE_SIZE - 1);
        uint32 vesa_count = 0;
        while (vesa_dmt_table[vesa_count].width != 0) vesa_count++;
    
        // Ogni timing avrà 3 spazi colore (8, 16, 32 bit) + 1 per il preferred mode
        uint32 total_needed = (vesa_count * 3) + 3; 

        size_t area_size = (total_needed * sizeof(display_mode) + B_PAGE_SIZE - 1) & ~(B_PAGE_SIZE - 1);
        
        new_area = create_area("sm750 modes fallback", (void**)&list, 
            B_ANY_ADDRESS, area_size, B_NO_LOCK, B_READ_AREA | B_WRITE_AREA);
        
        if (new_area < 0) return new_area;

        display_mode *pm = gInfo->si->card_info.is_panel ? 
            &gInfo->si->preferred_mode : &gInfo->si->preferred_mode2;
            
        if (pm->timing.h_display > 0 && pm->timing.v_display > 0) {
        	color_space spaces[] = { B_RGB32, B_RGB16, B_CMAP8 };
            for (int s = 0; s < 3; s++) {
                list[count] = *pm;
                list[count].space = spaces[s];
                count++;
            }
            debug_printf("SM750_ACC: Modo preferito (Boot) impostato in list[0]: %dx%d\n", 
                pm->timing.h_display, pm->timing.v_display);
        } else {
            // Ultima spiaggia: 1024x768 Standard
            display_mode safe_mode = {
                { 65000, 1024, 1048, 1184, 1344, 768, 771, 777, 806, 0 },
                B_RGB32, 1024, 768, 0, 0
            };
            list[0] = safe_mode;
            count = 1;
            debug_printf("SM750_ACC: Fallback su 1024x768 Safe Mode\n");
        }

        // Secondo passaggio: aggiungiamo gli altri modi dalla tabella VESA (evitando duplicati)
        for (int i = 0; vesa_dmt_table[i].width != 0; i++) {
            const vesa_timing_t* vesa = &vesa_dmt_table[i];
            color_space spaces[] = { B_RGB32, B_RGB16, B_CMAP8 };

            // Salta se è lo stesso del modo preferito già inserito
            for (int s = 0; s < 3; s++) {
                // Evitiamo duplicati rispetto al preferred mode già inserito
                if (pm->timing.h_display == vesa->width && pm->timing.v_display == vesa->height)
                    continue;

                display_mode* dm = &list[count];
                // Riempimento Timing (uguale a prima)
                dm->timing.pixel_clock = vesa->pixel_clock;
                dm->timing.h_display    = vesa->width;
                dm->timing.h_sync_start = vesa->h_sync_start;
                dm->timing.h_sync_end   = vesa->h_sync_end;
                dm->timing.h_total      = vesa->h_total;
                dm->timing.v_display    = vesa->height;
                dm->timing.v_sync_start = vesa->v_sync_start;
                dm->timing.v_sync_end   = vesa->v_sync_end;
                dm->timing.v_total      = vesa->v_total;
                dm->timing.flags        = vesa->flags;

                // IMPOSTA LO SPAZIO COLORE DINAMICAMENTE
                dm->space = spaces[s];
            
                dm->virtual_width  = vesa->width;
                dm->virtual_height = vesa->height;
                dm->h_display_start = 0;
                dm->v_display_start = 0;
                dm->flags = 0;

                count++;
            }
        }
    }
    // ciclo di validazione (se sta all'interno della massima memoria disponibile per il desktop)
    uint32 validCount = 0;
    for (uint32 i = 0; i < count; i++) {
        display_mode *dm = &list[i];
        
        // Calcoliamo lo spazio necessario (assumendo il caso peggiore: 32bpp)
        uint32 bytesPerPixel = 0;
        switch (dm->space) {
            case B_RGB32: bytesPerPixel = 4; break;
            case B_RGB16: bytesPerPixel = 2; break;
            case B_CMAP8: bytesPerPixel = 1; break;
            default: continue; // Salta formati sconosciuti
        }
        uint32 memNeeded = dm->virtual_width * dm->virtual_height * bytesPerPixel;

        bool modeOk = true;

        // Check Memoria Desktop (i famosi 12MB)
        if (memNeeded > gInfo->si->card_info.max_desktop_mem) {
            modeOk = false;
        }
        
        // Check Pixel Clock (limite fisico del DAC SM750)
        if (dm->timing.pixel_clock > gInfo->si->card_info.max_pclk) {
            modeOk = false;
        }

        if (modeOk) {
            // Se il modo è valido, lo teniamo (compattiamo la lista se necessario)
            if (validCount != i) {
                list[validCount] = list[i];
            }
            validCount++;
        } else {
            debug_printf("SM750_ACC: Modo %dx%d rimosso (richiede %u MB, limite %u MB)\n", 
                dm->timing.h_display, dm->timing.v_display, 
                memNeeded / (1024*1024), 
                gInfo->si->card_info.max_desktop_mem / (1024*1024));
        }
    }
    count = validCount;

    // 5. PUBBLICAZIONE NELLA SHARED INFO
    // Se c'era una vecchia area del kernel, non cancelliamola (gestita dal driver),
    // ma sovrascriviamo l'ID ufficiale per i cloni.
    gInfo->si->mode_list_area = new_area;
    gInfo->si->mode_count = count;
    
    // Nota: gInfo->mode_list e gInfo->mode_list_area locali verranno 
    // aggiornati in init_common subito dopo la chiamata a questa funzione.
    
    return B_OK;
}

static status_t init_common(int fd,bool isClone) {
    //debug_printf("SM750_ACC: Inizio init_common\n");
    gInfo->fd = fd;
    gInfo->is_clone = isClone;

    /* 1. Recupera l'area shared_info dal driver tramite IOCTL */
    sm750_get_private_data gpd;
    gpd.magic = SM750_PRIVATE_DATA_MAGIC;

    if (ioctl(fd, ENG_GET_PRIVATE_DATA, &gpd, sizeof(gpd)) != B_OK) {
        debug_printf("SM750_ACC: ERROR ioctl ENG_GET_PRIVATE_DATA failed!\n");
        return B_ERROR;
    }
    
    /* 2. Clona la shared_info */
    gInfo->shared_info_area = clone_area("sm750 shared info", (void **)&(gInfo->si),
        B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA, gpd.shared_info_area);
    
    if (gInfo->shared_info_area < 0) return gInfo->shared_info_area;
    
    shared_info *si = gInfo->si;
    
    if (si->regs_area <= 0) {
        debug_printf("SM750_ACC: ERROR! Invalid regs_area in shared_info!\n");
        return B_ERROR;
    }
    /* 3. Clona i registri MMIO (BAR1) */
    gInfo->regs_area = clone_area("sm750 regs user", (void **)&(gInfo->regs),
        B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA, gInfo->si->regs_area);
    
    if (gInfo->regs_area < 0) {
    	debug_printf("SM750_ACC: clone_area fatal error: %s\n", strerror(gInfo->regs_area));
        delete_area(gInfo->shared_info_area);
        return gInfo->regs_area;
    }

    /* Test lettura ID per conferma MMIO */
    //vuint32* regs = gInfo->regs;
    if (gInfo->regs == NULL) {
        debug_printf("SM750_ACC: CRITIC ERROR! gInfo->regs is NULL after clone!\n");
        return B_ERROR;
    }
    
    /* 4. Clona il Framebuffer (BAR0) */
    void* fb_ptr = NULL;
    gInfo->fb_area = clone_area("sm750 fb user", &fb_ptr,
        B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA, gInfo->si->fb_area);
    
    if (gInfo->fb_area < 0) {
        delete_area(gInfo->regs_area);
        delete_area(gInfo->shared_info_area);
        return gInfo->fb_area;
    }

    /* salviamo il puntatore virtuale LOCALMENTE */
    gInfo->framebuffer = (uint8*)fb_ptr; 
    
    if (!isClone) {
        //si->framebuffer = (uint8*)fb_ptr;
        // --- INIZIALIZZAZIONE MEMORY MANAGER ---
        // Ora che sappiamo quanta RAM c'è, attiviamo il gestore
        if (init_vram_manager(si) != B_OK) {
            debug_printf("SM750_ACC: WARNING - Memory Manager initialization failed!\n");
        }
        si->fbc.frame_buffer=gInfo->framebuffer;
        si->fbc2.frame_buffer=gInfo->framebuffer;
                
        // qui benaphore per engine 2d
        status_t result = si->engine.lock.Init("SM750 2D engine lock");
		if (result == B_OK) {
			//result = si.overlayLock.Init("3DFX overlay lock");
			// abilitiamo l'overlay
		}
		if (si->card_info.has_edid_vesa) {
            edid_decode(&gInfo->edid_vesa_info, &si->vesa_edid_raw);
            debug_printf("SM750 Accelerante: EDID VESA decodificato\n");
        }
        create_mode_list();
        
        gInfo->mode_list_area = clone_area("sm750 modes user", (void**)&gInfo->mode_list,
            B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA, si->mode_list_area);
        if (gInfo->mode_list_area < 0) return gInfo->mode_list_area;
        // --- AVVIO SERVIZIO INTERRUPT ---
        si->vblank_sem = create_sem(0, "sm750_vblank_kernel_signal");
        si->vblank_sync_sem = create_sem(0, "sm750_vblank_sync_user");
        si->engine.lock.sem = create_sem(0, "sm750 engine sem");

        atomic_set(&si->irq_enabled, 1);
        gInfo->vblank_thread = spawn_thread(
            sm750_vblank_service_thread, 
            "sm750 vblank service", 
            B_DISPLAY_PRIORITY, 
            gInfo);
        if (gInfo->vblank_thread >= 0) {
            resume_thread(gInfo->vblank_thread);
            debug_printf("SM750_ACC: VBlank service thread avviato (ID: %" B_PRId32 ")\n", gInfo->vblank_thread);
        } else {
            debug_printf("SM750_ACC: ERRORE spawn_thread fallito!\n");
        }
    } else {
        gInfo->mode_list_area = clone_area("sm750 modes clone", (void**)&gInfo->mode_list,
            B_ANY_ADDRESS, B_READ_AREA, si->mode_list_area);
        
        if (gInfo->mode_list_area < 0) {
        	debug_printf("SM750_ACC: Error cloning mode_list_area\n");
        	return gInfo->mode_list_area;
        }
    }
    
    //si->cursor.v_address = (void *)((addr_t)gInfo->framebuffer + si->cursor.vram_offset);
    gInfo->cursor_virtual_address = (void *)((addr_t)gInfo->framebuffer + si->cursor.vram_offset);
    
    // 7. Token per il motore 2D (necessario per Haiku)
    gInfo->sm750_engine_token.engine_id = 1; 
    gInfo->sm750_engine_token.capability_mask = 0;
    gInfo->sm750_engine_token.opaque = NULL;
    
    //debug_printf("SM750_ACC: Engine Sem ID: %d\n", gInfo->si->engine.lock.sem);

    return B_OK;
}

status_t sm750_init_accelerant(int fd) {
    gInfo->is_clone = false;
    status_t result = init_common(fd,false);
    
    if (result == B_OK) {
        if (gInfo->si->accelerant_in_use) {
            sm750_uninit_accelerant();
            return B_NOT_ALLOWED;
        }
        gInfo->si->accelerant_in_use = true;
    }
    return result;
}


/* Info sul clone (Haiku le usa per condividere l'accelerante tra app) */
uint32 sm750_accelerant_clone_info_size(void) {
	CALLED();
    // clone info is device name, so return its maximum size
	return B_PATH_NAME_LENGTH;
}

void sm750_get_accelerant_clone_info(void *data) {
	CALLED();
    strcpy((char *)data, gInfo->si->device_path);
}

status_t sm750_clone_accelerant(void* info)
{
    CALLED();
    char path[B_PATH_NAME_LENGTH];
    
    // Costruiamo il path completo
    strcpy(path, "/dev/");
    strlcat(path, (const char*)info, sizeof(path));

    int fd = open(path, O_RDWR);
    if (fd < 0) return errno;

    // Inizializziamo l'accelerante clone
    status_t status = init_common(fd, true);
    if (status != B_OK) {
        close(fd);
        return status;
    }

    return B_OK;
}

status_t sm750_get_accelerant_device_info(accelerant_device_info *adi) {
	CALLED();
    adi->version = 1;
    strcpy(adi->name, "Silicon Motion SM750");
    strcpy(adi->chipset, "SM750");
    strcpy(adi->serial_no, "Rev A");
    adi->memory = gInfo->si->card_info.mem_size;
    adi->dac_speed = 300000; // 300MHz
    return B_OK;
}

sem_id sm750_retrace_semaphore(void)
{
    return gInfo->si->vblank_sync_sem;
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
	debug_printf("SM750_ACC: Request 0x%x\n", feature);
    switch (feature) {
        case B_INIT_ACCELERANT:             return (void*)sm750_init_accelerant;
        case B_ACCELERANT_CLONE_INFO_SIZE:  return (void*)sm750_accelerant_clone_info_size;
        case B_GET_ACCELERANT_CLONE_INFO:   return (void*)sm750_get_accelerant_clone_info;
        case B_CLONE_ACCELERANT:            return (void*)sm750_clone_accelerant;
        case B_UNINIT_ACCELERANT:           return (void*)sm750_uninit_accelerant;
        case B_GET_ACCELERANT_DEVICE_INFO:  return (void*)sm750_get_accelerant_device_info;
        case B_ACCELERANT_RETRACE_SEMAPHORE: return (void *)sm750_retrace_semaphore;
        
        
        /* Display Modes */
        case B_ACCELERANT_MODE_COUNT:       return (void*)sm750_accelerant_mode_count;	//0x100
        case B_GET_MODE_LIST:               return (void*)sm750_get_mode_list;			//0x101
        case B_PROPOSE_DISPLAY_MODE:        return (void*)sm750_propose_display_mode;	//0x102
        case B_SET_DISPLAY_MODE:            return (void*)sm750_set_display_mode;		//0x103
        case B_GET_DISPLAY_MODE:            return (void*)sm750_get_display_mode;		//0x104
        case B_GET_FRAME_BUFFER_CONFIG:     return (void*)sm750_get_frame_buffer_config;//0x105
        case B_GET_PIXEL_CLOCK_LIMITS:      return (void*)sm750_get_pixel_clock_limits;	//0x106
        case B_GET_TIMING_CONSTRAINTS:		return (void*)sm750_get_timing_constraints;	//0x107
        //B_MOVE_DISPLAY
        case B_SET_INDEXED_COLORS:			return (void*)sm750_set_indexed_colors;
        case B_DPMS_CAPABILITIES:			return (void*)sm750_dpms_capabilities;
        case B_DPMS_MODE:					return (void*)sm750_dpms_mode;
        case B_SET_DPMS_MODE:				return (void*)sm750_set_dpms_mode;
        case B_GET_PREFERRED_DISPLAY_MODE:  return (void*)sm750_get_preferred_mode;
        //B_GET_MONITOR_INFO
        case B_GET_EDID_INFO:               return (void*)sm750_get_edid_info;
        //B_SET_BRIGHTNESS
        //B_GET_BRIGHTNESS
        
        /* Cursor */
        case B_MOVE_CURSOR:                 return (void*)sm750_move_cursor;			//0x200
        case B_SET_CURSOR_SHAPE:            return (void*)sm750_set_cursor_shape;
        case B_SHOW_CURSOR:                 return (void*)sm750_show_cursor;
        case B_SET_CURSOR_BITMAP:           return (void*)sm750_set_cursor_bitmap;
        
        /* Engine */
        case B_MOVE_DISPLAY:                return (void*)sm750_move_display_area;

        case B_ACCELERANT_ENGINE_COUNT:     return (void*)sm750_accelerant_engine_count;
        case B_ACQUIRE_ENGINE:              return (void*)sm750_acquire_engine;
        case B_RELEASE_ENGINE:              return (void*)sm750_release_engine;
        case B_WAIT_ENGINE_IDLE:            return (void*)sm750_wait_engine_idle;
        case B_GET_SYNC_TOKEN:              return (void*)sm750_get_sync_token;
        case B_SYNC_TO_TOKEN:               return (void*)sm750_sync_to_token;
        /* Hook per l'engine 2D (accelerazione hardware) */
        case B_FILL_RECTANGLE:				return (void*)sm750_fill_rectangle;
		case B_SCREEN_TO_SCREEN_BLIT:		return (void*)sm750_screen_to_screen_blit;
		case B_INVERT_RECTANGLE:			return (void*)sm750_invert_rectangle;
		case B_FILL_SPAN:					return (void*)sm750_fill_span;
		
		/* Overlay */
		case B_OVERLAY_COUNT:				return (void*)sm750_overlay_count;
		case B_OVERLAY_SUPPORTED_SPACES:	return (void*)sm750_overlay_supported_spaces;
		case B_OVERLAY_SUPPORTED_FEATURES:	return (void*)sm750_overlay_supported_features;
		case B_ALLOCATE_OVERLAY_BUFFER:		return (void*)sm750_allocate_overlay_buffer;
		case B_RELEASE_OVERLAY_BUFFER:		return (void*)sm750_release_overlay_buffer;
		case B_GET_OVERLAY_CONSTRAINTS:		return (void*)sm750_get_overlay_constraints;
		case B_ALLOCATE_OVERLAY:			return (void*)sm750_allocate_overlay;
		case B_RELEASE_OVERLAY:				return (void*)sm750_release_overlay;
		case B_CONFIGURE_OVERLAY:			return (void*)sm750_configure_overlay_api;
		
				
        default: return NULL;
    }
}
