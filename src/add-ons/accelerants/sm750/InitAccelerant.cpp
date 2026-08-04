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

/* global gInfo for accelerant */
accelerant_info g_info = {
    .shared_info_area = -1,
    .regs_area = -1,
    .fb_area = -1,
    .mode_list_area = -1,
    .vblank_thread = -1
};
accelerant_info *gInfo = &g_info;


static status_t init_vram_manager(shared_info* si) 
{
    uint32 desktopReserve = si->card_info.max_desktop_mem; 

    uint32 heapStart = desktopReserve; //soon after desktop reserve
    uint32 heapSize = si->card_info.mem_size - desktopReserve;

    debug_printf("SM750_ACC: Heap VRAM allocated to 0x%x (Size: %u KB)\n", heapStart, heapSize / 1024);

    // using local_mem_mgr on stack to avoid smap with si->mem_mgr
    void* local_mem_mgr = (void*)mem_init("sm750_vram_heap", heapStart, heapSize, 8, 128);
    
    if (local_mem_mgr == NULL) {
        debug_printf("SM750_ACC ERROR: mem_init failed!\n");
        return B_ERROR;
    }

    // SM750 hardware cursor in "3-color + transparency" mode uses 16KB.
    uint32 cursorBlockID;
    uint32 cursorOffset;
    status_t status = mem_alloc((mem_info*)local_mem_mgr, 16384, (void*)0x43555253, 
                                &cursorBlockID, &cursorOffset);
    
    if (status == B_OK) {
        si->cursor.vram_offset = cursorOffset;
        si->cursor.block_id = cursorBlockID;
        si->mem_mgr = local_mem_mgr;
        //debug_printf("SM750_ACC: Cursor dinamically allocated at offset 0x%x\n", cursorOffset);
    } else {
        debug_printf("SM750_ACC ERROR: Unable to allocate memory for the cursor!\n");
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

    // TRY HARDWARE (I2C) DETECTION
    if (sm750_read_edid(temp_edid_raw) == B_OK) {
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

    // LIST GENERATION
    if (edid_valid) {
        // Magic function from libkernel_graphics.a: creates an area with all the modes
        // supported by monitor looking at EDID timings.
        new_area = create_display_modes("sm750 modes", &info, 
            NULL, 0, NULL, 0, NULL, &list, &count);
    } 
    
    // FALLBACKS: BIOS OR 1024x768
    if (new_area < 0) {
        debug_printf("SM750_ACC: No valid EDIDs found. Using fallback BIOS/VesaTable.\n");
        
        uint32 vesa_count = 0;
        while (vesa_dmt_table[vesa_count].width != 0) vesa_count++;
    
        // 3 color spaces per timing (8, 16, 32 bit) + 1 for the preferred mode
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
            debug_printf("SM750_ACC: Preferred mode (Boot) set in list[0]: %dx%d\n", 
                pm->timing.h_display, pm->timing.v_display);
        } else {
            // Last resource: 1024x768 Standard
            display_mode safe_mode = {
                { 65000, 1024, 1048, 1184, 1344, 768, 771, 777, 806, 0 },
                B_RGB32, 1024, 768, 0, 0
            };
            list[0] = safe_mode;
            count = 1;
            debug_printf("SM750_ACC: Fallback on 1024x768, Safe Mode\n");
        }

        //Second step: add the other modes from the VESA table (avoiding duplicates)
        for (int i = 0; vesa_dmt_table[i].width != 0; i++) {
            const vesa_timing_t* vesa = &vesa_dmt_table[i];
            color_space spaces[] = { B_RGB32, B_RGB16, B_CMAP8 };

            for (int s = 0; s < 3; s++) {
                if (pm->timing.h_display == vesa->width && pm->timing.v_display == vesa->height)
                    continue;

                display_mode* dm = &list[count];
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
    // validation loop (if it fits within the maximum available desktop memory)
    uint32 validCount = 0;
    for (uint32 i = 0; i < count; i++) {
        display_mode *dm = &list[i];
        
        uint32 bytesPerPixel = 0;
        switch (dm->space) {
            case B_RGB32: bytesPerPixel = 4; break;
            case B_RGB16: bytesPerPixel = 2; break;
            case B_CMAP8: bytesPerPixel = 1; break;
            default: continue;
        }
        uint32 memNeeded = dm->virtual_width * dm->virtual_height * bytesPerPixel;

        bool modeOk = true;

        // Check Desktop Memory (notorious 12MB)
        if (memNeeded > gInfo->si->card_info.max_desktop_mem) {
            modeOk = false;
        }
        
        // Check Pixel Clock (physical DAC limit of SM750)
        if (dm->timing.pixel_clock > gInfo->si->card_info.max_pclk) {
            modeOk = false;
        }

        if (modeOk) {
            if (validCount != i) {
                list[validCount] = list[i];
            }
            validCount++;
        } else {
            debug_printf("SM750_ACC: %dx%d mode removed (requires: %u MB, limit: %u MB)\n", 
                dm->timing.h_display, dm->timing.v_display, 
                memNeeded / (1024*1024), 
                gInfo->si->card_info.max_desktop_mem / (1024*1024));
        }
    }
    count = validCount;

    // PUBLISH ON SHARED INFO
    gInfo->si->mode_list_area = new_area;
    gInfo->si->mode_count = count;
    
    return B_OK;
}

static status_t init_common(int fd,bool isClone) {
    gInfo->fd = fd;
    gInfo->is_clone = isClone;

    /* Retrieve the shared_info area from the driver via IOCTL */
    sm750_get_private_data gpd;
    gpd.magic = SM750_PRIVATE_DATA_MAGIC;

    if (ioctl(fd, ENG_GET_PRIVATE_DATA, &gpd, sizeof(gpd)) != B_OK) {
        debug_printf("SM750_ACC: ERROR ioctl ENG_GET_PRIVATE_DATA failed!\n");
        return B_ERROR;
    }
    
    /* Clone the shared_info */
    gInfo->shared_info_area = clone_area("sm750 shared info", (void **)&(gInfo->si),
        B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA, gpd.shared_info_area);
    
    if (gInfo->shared_info_area < 0) return gInfo->shared_info_area;
    
    shared_info *si = gInfo->si;
    
    if (si->regs_area <= 0) {
        debug_printf("SM750_ACC: ERROR! Invalid regs_area in shared_info!\n");
        return B_ERROR;
    }
    /* Clone MMIO (BAR1) registers */
    gInfo->regs_area = clone_area("sm750 regs user", (void **)&(gInfo->regs),
        B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA, gInfo->si->regs_area);
    
    if (gInfo->regs_area < 0) {
    	debug_printf("SM750_ACC: clone_area fatal error: %s\n", strerror(gInfo->regs_area));
        delete_area(gInfo->shared_info_area);
        return gInfo->regs_area;
    }

    if (gInfo->regs == NULL) {
        debug_printf("SM750_ACC: CRITIC ERROR! gInfo->regs is NULL after clone!\n");
        return B_ERROR;
    }
    
    if (!isClone) {
        // --- MEMORY MANAGER INITIALIZATION ---
        if (init_vram_manager(si) != B_OK) {
            debug_printf("SM750_ACC: WARNING - Memory Manager initialization failed!\n");
        }
        si->fbc.frame_buffer = si->framebuffer;
        si->fbc2.frame_buffer = si->framebuffer;

        status_t result = si->engine.lock.Init("SM750 2D engine lock");
		if (result == B_OK) {
			// nothing for now
		}
		if (si->card_info.has_edid_vesa) {
            edid_decode(&gInfo->edid_vesa_info, &si->vesa_edid_raw);
            //debug_printf("SM750_ACC: EDID VESA decoded\n");
        }
        create_mode_list();
        
        gInfo->mode_list_area = clone_area("sm750 modes user", (void**)&gInfo->mode_list,
            B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA, si->mode_list_area);
        if (gInfo->mode_list_area < 0) return gInfo->mode_list_area;

        si->vblank_sem = create_sem(0, "sm750_vblank_kernel_signal");
        si->vblank_sync_sem = create_sem(0, "sm750_vblank_sync_user");
        si->engine.lock.sem = create_sem(0, "sm750 engine sem");
        if (si->vblank_sem < B_OK || si->vblank_sync_sem < B_OK
            || si->engine.lock.sem < B_OK) {
            debug_printf("SM750_ACC: ERROR - Missing driver semaphores for vblank/engine\n");
            return B_ERROR;
        }

        atomic_set(&si->irq_enabled, 1);
        gInfo->vblank_thread = spawn_thread(
            sm750_vblank_service_thread, 
            "sm750 vblank service", 
            B_DISPLAY_PRIORITY, 
            gInfo);
        if (gInfo->vblank_thread >= 0) {
            resume_thread(gInfo->vblank_thread);
            //debug_printf("SM750_ACC: VBlank service thread started (ID: %" B_PRId32 ")\n", gInfo->vblank_thread);
        } else {
            debug_printf("SM750_ACC: ERROR spawn_thread failed!\n");
        }
    } else {
        gInfo->mode_list_area = clone_area("sm750 modes clone", (void**)&gInfo->mode_list,
            B_ANY_ADDRESS, B_READ_AREA, si->mode_list_area);
        
        if (gInfo->mode_list_area < 0) {
        	debug_printf("SM750_ACC: Error cloning mode_list_area\n");
        	return gInfo->mode_list_area;
        }
    }
    
    //gInfo->cursor_virtual_address = (void *)((addr_t)gInfo->framebuffer + si->cursor.vram_offset);
	gInfo->cursor_virtual_address = (void *)((addr_t)si->framebuffer + si->cursor.vram_offset);
    
    // Token for 2D engine
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
    
    strcpy(path, "/dev/");
    strlcat(path, (const char*)info, sizeof(path));

    int fd = open(path, O_RDWR);
    if (fd < 0) return errno;

    status_t status = init_common(fd, true);
    if (status != B_OK) {
        close(fd);
        return status;
    }

    return B_OK;
}

status_t sm750_get_accelerant_device_info(accelerant_device_info *adi) {
	//CALLED();
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
    shared_info* si = gInfo->si;
    thread_id vblankThread = gInfo->vblank_thread;

    if (si != NULL && !gInfo->is_clone) {
        si->accelerant_in_use = false;
        atomic_set(&si->irq_enabled, 0);
        if (si->vblank_sem >= B_OK)
            release_sem(si->vblank_sem);
    }

    if (vblankThread >= B_OK) {
        status_t result;
        wait_for_thread(vblankThread, &result);
    }

    if (gInfo->fb_area >= 0) delete_area(gInfo->fb_area);
    if (gInfo->regs_area >= 0) delete_area(gInfo->regs_area);
    if (gInfo->shared_info_area >= 0) delete_area(gInfo->shared_info_area);
    
    gInfo->regs = NULL;
    //gInfo->framebuffer = NULL;
    gInfo->vblank_thread = -1;
    gInfo->si = NULL;
}

void* get_accelerant_hook(uint32 feature, void* data) {
	//debug_printf("SM750_ACC: Request 0x%x\n", feature);
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
#ifdef IS_PIRATI_BUILD
        case B_SET_CURSOR_BITMAP:           return (void*)sm750_set_cursor_bitmap;
        case B_GET_CURSOR_BITS:             return (void*)sm750_get_cursor_bits;
#endif        
        /* Engine */
        case B_MOVE_DISPLAY:                return (void*)sm750_move_display_area;

        case B_ACCELERANT_ENGINE_COUNT:     return (void*)sm750_accelerant_engine_count;
        case B_ACQUIRE_ENGINE:              return (void*)sm750_acquire_engine;
        case B_RELEASE_ENGINE:              return (void*)sm750_release_engine;
        case B_WAIT_ENGINE_IDLE:            return (void*)sm750_wait_engine_idle;
        case B_GET_SYNC_TOKEN:              return (void*)sm750_get_sync_token;
        case B_SYNC_TO_TOKEN:               return (void*)sm750_sync_to_token;
        /* 2D engine (accelerazione hardware) */
        case B_FILL_RECTANGLE:			return (void*)sm750_fill_rectangle;
	case B_SCREEN_TO_SCREEN_BLIT:		return (void*)sm750_screen_to_screen_blit;
	case B_INVERT_RECTANGLE:		return (void*)sm750_invert_rectangle;
	case B_FILL_SPAN:			return (void*)sm750_fill_span;
		
	/* Overlay */
	case B_OVERLAY_COUNT:			return (void*)sm750_overlay_count;
	case B_OVERLAY_SUPPORTED_SPACES:	return (void*)sm750_overlay_supported_spaces;
	case B_OVERLAY_SUPPORTED_FEATURES:	return (void*)sm750_overlay_supported_features;
	case B_ALLOCATE_OVERLAY_BUFFER:		return (void*)sm750_allocate_overlay_buffer;
	case B_RELEASE_OVERLAY_BUFFER:		return (void*)sm750_release_overlay_buffer;
	case B_GET_OVERLAY_CONSTRAINTS:		return (void*)sm750_get_overlay_constraints;
	case B_ALLOCATE_OVERLAY:		return (void*)sm750_allocate_overlay;
	case B_RELEASE_OVERLAY:			return (void*)sm750_release_overlay;
	case B_CONFIGURE_OVERLAY:		return (void*)sm750_configure_overlay_api;
		
				
        default: return NULL;
    }
}
