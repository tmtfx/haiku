/*
 * Copyright 2026, Haiku and the Pirati Del Frico contributors.
 * Distributed under the terms of the MIT License.
 *
 * This accelerant is intentionally minimal but functional. Structure and
 * API usage were derived from MIT-compatible Haiku sources and MIT-licensed
 * upstream Intel sources:
 *  - src/add-ons/accelerants/framebuffer/accelerant.cpp
 *  - src/add-ons/accelerants/framebuffer/hooks.cpp
 *  - src/add-ons/accelerants/framebuffer/mode.cpp
 *  - src/add-ons/accelerants/intel_extreme/accelerant.cpp
 *  - https://raw.githubusercontent.com/torvalds/linux/master/drivers/gpu/drm/i915/display/intel_ddi_buf_trans.c
 *  - https://raw.githubusercontent.com/torvalds/linux/master/drivers/gpu/drm/i915/display/intel_snps_phy.c
 *  - https://raw.githubusercontent.com/torvalds/linux/master/drivers/gpu/drm/i915/display/intel_snps_phy_regs.h
 *  - https://raw.githubusercontent.com/torvalds/linux/master/drivers/gpu/drm/i915/display/intel_cx0_phy.c
 *  - https://raw.githubusercontent.com/torvalds/linux/master/drivers/gpu/drm/i915/display/intel_cx0_phy_regs.h
 *
 */
#include <OS.h>

#include "accelerant_protos.h"
#include "accelerant.h"

#include <compute_display_timing.h>
#include <create_display_modes.h>
#include <ddc.h>
#include <dp.h>

#include <edid.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <AutoDeleterOS.h>

#define CALLED() debug_printf("INTEL_ARC_ACC: CALLED %s\n", __FUNCTION__)
accelerant_info* gInfo;
static engine_token sEngineToken = {1, 0, NULL};
static uint64 sSyncCounter = 1;

static void
uninit_common(void)
{
	debug_printf("intel_arc.accelerant: uninit_common()\n");
	if (gInfo->overlay_mem_mgr != NULL) {
		mem_destroy(gInfo->overlay_mem_mgr);
		gInfo->overlay_mem_mgr = NULL;
	}
	if (!gInfo->is_clone) gInfo->shared_info->accelerant_in_use = false;
	/*if (gInfo->cursor_area >= B_OK) {
		delete_area(gInfo->cursor_area);
		gInfo->cursor_area = -1;
	}*/
	if (gInfo->frame_buffer_area >= B_OK)
		delete_area(gInfo->frame_buffer_area);
	if (gInfo->regs_area >= B_OK)
		delete_area(gInfo->regs_area);
	if (gInfo->shared_info_area >= B_OK)
		delete_area(gInfo->shared_info_area);

	if (gInfo->is_clone)
		close(gInfo->device);

	free(gInfo);
	gInfo = NULL;
}
/*
status_t dp_aux_read(uint32 address, uint8* buffer, size_t size)
{
    if (buffer == NULL || size == 0 || size > 16)
        return B_BAD_VALUE;

    // Costruzione comando AUX: Header 20 bit [Command (4b) | Address (16b) | Length-1 (8b)]
    uint32 aux_cmd = (DP_AUX_NATIVE_READ << 20) | ((address & 0xFFFFF) << 4) | ((size - 1) & 0xF);

    // 1. Scrivi il comando nel registro DATA1
    write_register(INTEL_ARC_MMIO_AUX_CH_DATA1_A, aux_cmd);

    // 2. Avvia la transazione AUX (BIT 31 = SEND_BUSY, BIT 26 = Interrupt enable/disable, Sync pulse len)
    uint32 ctl_val = (1U << 31) | (size << 20) | (5 << 16); // 5 us message length timeout
    write_register(INTEL_ARC_MMIO_AUX_CH_CTL_A, ctl_val);

    // 3. Polling per il completamento (Bit 31 torna a 0)
    int timeout = 1000;
    uint32 val = 0;
    read_register(INTEL_ARC_MMIO_AUX_CH_CTL_A, val);
    while ((val & (1U << 31)) && --timeout > 0) {
        snooze(10);
    }

    if (timeout <= 0)
        return B_TIMED_OUT;

    // 4. Leggi la risposta dal registro DATA
    uint32 rx_data = 0;
    read_register(INTEL_ARC_MMIO_AUX_CH_DATA1_A, rx_data);
    buffer[0] = (rx_data >> 16) & 0xFF; // Estrai byte letto

    return B_OK;
}

status_t get_transcoder_dp_config(uint32 trans_ddi_ctl_reg, dp_link_config& config)
{
    // Inizializzazione con le tue impostazioni di default
    config.lanes = 4;            // Default sicuro
    config.bpp = 24;             // Default 8 bpc (24 bpp)
    config.linkBandwidth = 540000; // Default 5.40 Gbps (HBR2) in kHz

    // 1. Leggi il registro TRANS_DDI_FUNC_CTL del transcoder attivo
    uint32 ddi_ctl = 0;
    status_t ret = read_register(trans_ddi_ctl_reg, ddi_ctl);

    if ((ret != B_OK)&&!(ddi_ctl & INTEL_ARC_PIPE_DDI_FUNC_ENABLE)) {
        return B_ERROR; // Transcoder disabilitato
    }

    // -------------------------------------------------------------
    // A. RECUPERO DEL NUMERO DI LANE (Bit [3:1])
    // -------------------------------------------------------------
    uint32 port_width = ddi_ctl & INTEL_ARC_PIPE_DDI_DP_WIDTH_MASK;
    switch (port_width) {
        case INTEL_ARC_PIPE_DDI_DP_WIDTH_1:
            config.lanes = 1;
            break;
        case INTEL_ARC_PIPE_DDI_DP_WIDTH_2:
            config.lanes = 2;
            break;
        case INTEL_ARC_PIPE_DDI_DP_WIDTH_3:
            config.lanes = 3;
            break;
        case INTEL_ARC_PIPE_DDI_DP_WIDTH_4:
        default:
            config.lanes = 4;
            break;
    }

    // -------------------------------------------------------------
    // B. RECUPERO BPC E BPP (Bit [22:20])
    // -------------------------------------------------------------
    uint32 bpc_bits = ddi_ctl & INTEL_ARC_PIPE_DDI_BPC_MASK;
    switch (bpc_bits) {
        case INTEL_ARC_PIPE_DDI_BPC_6:
            config.bpp = 18; // 6 bits per color * 3 RGB
            break;
        case INTEL_ARC_PIPE_DDI_BPC_8:
            config.bpp = 24; // 8 bits per color * 3 RGB
            break;
        case INTEL_ARC_PIPE_DDI_BPC_10:
            config.bpp = 30; // 10 bits per color * 3 RGB
            break;
        case INTEL_ARC_PIPE_DDI_BPC_12:
            config.bpp = 36; // 12 bits per color * 3 RGB
            break;
        //case TRANS_DDI_BPC_16:
        //    config.bpp = 48; // 16 bits per color * 3 RGB
        //    break;
        default:
            config.bpp = 24;
            break;
    }

    // -------------------------------------------------------------
    // C. RECUPERO LINK BANDWIDTH / LINK RATE (da DPCD via AUX)
    // -------------------------------------------------------------
    uint8 dpcd_bw = 0;
    if (dp_aux_read(DPCD_LINK_BW_SET, &dpcd_bw, 1) == B_OK && dpcd_bw != 0) {
        switch (dpcd_bw) {
            case 0x06:
                config.linkBandwidth = 162000; // RBR  (1.62 Gbps)
                break;
            case 0x0A:
                config.linkBandwidth = 270000; // HBR  (2.70 Gbps)
                break;
            case 0x14:
                config.linkBandwidth = 540000; // HBR2 (5.40 Gbps)
                break;
            case 0x1E:
                config.linkBandwidth = 810000; // HBR3 (8.10 Gbps)
                break;
            default:
                // Calcolo generico DPCD: BW = valore_dpcd * 27000 kHz
                config.linkBandwidth = (uint32)dpcd_bw * 27000;
                break;
        }
    }

    return B_OK;
}*/

static status_t
init_common(int device, bool isClone)
{
	debug_printf("intel_arc.accelerant: init_common(device=%d, isClone=%d)\n", device, isClone);
	gInfo = (accelerant_info*)malloc(sizeof(accelerant_info));
	if (gInfo == NULL)
		return B_NO_MEMORY;
	MemoryDeleter infoDeleter(gInfo);

	memset(gInfo, 0, sizeof(accelerant_info));
	gInfo->device = device;
	gInfo->is_clone = isClone;
	gInfo->shared_info_area = -1;
	gInfo->regs_area = -1;
	gInfo->mode_list_area = -1;
	gInfo->frame_buffer_area = -1;
	gInfo->overlay_mem_mgr = NULL;
	gInfo->has_edid = false;
	gInfo->last_hotplug_event_count = 0;

	intel_arc_get_private_data data;
	data.magic = INTEL_ARC_PRIVATE_DATA_MAGIC;
	if (ioctl(device, INTEL_ARC_GET_PRIVATE_DATA, &data, sizeof(data)) != 0) {
		debug_printf("intel_arc.accelerant ERROR: IOCTL INTEL_ARC_GET_PRIVATE_DATA failed\n");
		return B_ERROR;
	}

	AreaDeleter sharedDeleter(clone_area("intel arc shared info",
		(void**)&gInfo->shared_info, B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA,
		data.shared_info_area));
	status_t status = gInfo->shared_info_area = sharedDeleter.Get();
	if (status < B_OK) {
		debug_printf("intel_arc.accelerant ERROR: Failed to clone shared info area: %s\n", strerror(status));
		return status;
	}

	if (gInfo->shared_info->registers_area >= B_OK) {
		AreaDeleter regsDeleter(clone_area("intel arc regs", (void**)&gInfo->registers,
			B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA,
			gInfo->shared_info->registers_area));
		status = gInfo->regs_area = regsDeleter.Get();
		if (status < B_OK) {
			debug_printf("intel_arc.accelerant ERROR: Failed to clone registers area: %s\n", strerror(status));
			return status;
		}
		regsDeleter.Detach();
	}
	/* this is not correct, being a PLANE, cursor address should stay in VRAM not in RAM as
	 * while finding a solution that doesn't kill the pipe shutting down the video output
	 * for now let's disable the Hardware Cursor
	gInfo->cursor_area = -1;
    if (!gInfo->shared_info->bDisableHdwCursor && gInfo->shared_info->cursor_area >= B_OK) {
        AreaDeleter cursorDeleter(clone_area("intel arc userland cursor",
            &gInfo->shared_info->cursor_virtual_base,
            B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA,
            gInfo->shared_info->cursor_area));

        status = gInfo->cursor_area = cursorDeleter.Get();
        if (status < B_OK) {
            debug_printf("intel_arc.accelerant WARNING: Failed to clone cursor area: %s. Disabling HW cursor.\n",
                strerror(status));
            gInfo->shared_info->bDisableHdwCursor = true;
            gInfo->shared_info->cursor_virtual_base = NULL;
        } else {
            cursorDeleter.Detach();
            debug_printf("intel_arc.accelerant: Cursor area cloned at Userland Virt: %p\n",
                gInfo->shared_info->cursor_virtual_base);
        }
    } else {
        gInfo->shared_info->cursor_virtual_base = NULL;
    }
    */
	if (!gInfo->shared_info->bDisableHdwCursor)	gInfo->shared_info->bDisableHdwCursor = true; //for now disable hardware cursor
	

	if (gInfo->shared_info != NULL)
		gInfo->last_hotplug_event_count = gInfo->shared_info->hotplug_event_count;
	
	infoDeleter.Detach();
	sharedDeleter.Detach();

	if (!isClone) {
		read_edid_from_hardware();

		status = create_mode_list();
		if (status != B_OK) {
			uninit_common();
			return status;
		}
		/* why cloning if frame buffer has B_READ_AREA and B_WRITE_AREA ?
		area_info info;
		status = ioctl(gInfo->device, INTEL_ARC_CLONE_FRAME_BUFFER, &info, sizeof(info));
		if (status == B_OK) {
			gInfo->frame_buffer_area = info.area;
			gInfo->frame_buffer = info.address;
			debug_printf("intel_arc.accelerant: Cloned Framebuffer area: %" B_PRId32 " at %p\n", info.area, info.address);
			status_t overlayStatus = init_overlay_memory_manager();
			if (overlayStatus != B_OK)
				debug_printf("intel_arc.accelerant: overlay VRAM heap unavailable: %s\n", strerror(overlayStatus));
		} else {
			debug_printf("intel_arc.accelerant ERROR: Failed to clone framebuffer: %s\n", strerror(status));
		}*/
		/* you can actually do the same by uncommenting this code, it should work, but overlay cloning... mmmh who knows...
		if (gInfo->shared_info->frame_buffer_area >= B_OK) {
            AreaDeleter fbDeleter(clone_area("intel arc framebuffer",
                (void**)&gInfo->frame_buffer, B_ANY_ADDRESS,
                B_READ_AREA | B_WRITE_AREA,
                gInfo->shared_info->frame_buffer_area));

            status = gInfo->frame_buffer_area = fbDeleter.Get();
            if (status < B_OK) {
                debug_printf("intel_arc.accelerant ERROR: Failed to clone framebuffer area: %s\n", strerror(status));
                uninit_common();
                return status;
            }
            fbDeleter.Detach();
        } else {
            debug_printf("intel_arc.accelerant ERROR: Invalid shared frame_buffer_area\n");
            uninit_common();
            return B_ERROR;
        }*/
        
        gInfo->frame_buffer_area = gInfo->shared_info->frame_buffer_area;
		gInfo->frame_buffer = (void*)gInfo->shared_info->frame_buffer;
		if (!gInfo->shared_info->bDisableOverlay) {
			status_t overlayStatus = init_overlay_memory_manager();
			if (overlayStatus != B_OK)
				debug_printf("intel_arc.accelerant: overlay VRAM heap unavailable: %s\n", strerror(overlayStatus));
			else
				debug_printf("intel_arc.accelerant: overlay VRAM heap initialized\n");
		}
		/* if you have previously cloned the framebuffer you can now allocate the cursor space
		 * still it won't work as this accelerant doesn't cover all the steps needed for cursor
		 * plane setup (like watermarking and other amenities).
		if (!gInfo->shared_info->bDisableHdwCursor) {
            uint32 cursorOffset = 0;
            uint32 cursorBlockID = 0;
            const uint32 kCursorSize = 16384; // 16 KB

            if (gInfo->overlay_mem_mgr != NULL) {
                if (mem_alloc(gInfo->overlay_mem_mgr, kCursorSize, NULL, &cursorBlockID, &cursorOffset) != B_OK) {
                    cursorOffset = 0;
                }
            }

            // Fallback: usa gli ultimi 16KB del framebuffer
            if (cursorOffset == 0 && gInfo->shared_info->frame_buffer_size > kCursorSize) {
                cursorOffset = gInfo->shared_info->frame_buffer_size - kCursorSize;
            }

            // Ora gInfo->frame_buffer è un puntatore valido per l'app_server!
            gInfo->shared_info->cursor_virtual_base = (void*)((addr_t)gInfo->frame_buffer + cursorOffset);
            gInfo->shared_info->cursor_physical_base = cursorOffset;

            debug_printf("intel_arc.accelerant: HW Cursor VRAM allocated at offset 0x%" B_PRIx32 " (Virt: %p)\n",
                    cursorOffset, gInfo->shared_info->cursor_virtual_base);
        }*/
	} else {
		read_edid_from_hardware();
		
		status = create_mode_list();
		if (status != B_OK) {
			uninit_common();
			return status;
		}
	}
	/*
	const int8 pipe = gInfo->shared_info->active_pipe;
	const uint32 pipeOffset = (uint32)pipe * INTEL_ARC_MMIO_PIPE_OFFSET;
	
	uint32 gopDdiCtl = 0;
	status_t ret = read_register(INTEL_ARC_MMIO_PIPE_A_DDI_FUNC_CTL + pipeOffset, gopDdiCtl);
	if ((ret == B_OK) && (gopDdiCtl & (1 << 31)) != 0) { // Se è già abilitato dal GOP
		uint32 modeSel = (gInfo->shared_info->pipe_ddi_func_ctl[pipe] & INTEL_ARC_PIPE_DDI_MODESEL_MASK) >> 24;
		if (modeSel == INTEL_ARC_PIPE_DDI_MODE_DP_SST || modeSel == INTEL_ARC_PIPE_DDI_MODE_DP_MST) {
			gInfo->shared_info->dp_link_trained_by_gop = true;
			get_transcoder_dp_config(INTEL_ARC_MMIO_PIPE_A_DDI_FUNC_CTL + pipeOffset, gInfo->shared_info->dp_boot_config);
			debug_printf("intel_arc.accelerant: lanes detected at boot: %d\n",gInfo->shared_info->dp_boot_config.lanes);
			debug_printf("intel_arc.accelerant: bpp detected at boot: %d\n",gInfo->shared_info->dp_boot_config.bpp);
			debug_printf("intel_arc.accelerant: link bandwidth detected at boot: %d\n",gInfo->shared_info->dp_boot_config.linkBandwidth);
		} else {
			gInfo->shared_info->dp_link_trained_by_gop = false;
		}
	} else {
		debug_printf("intel_arc.accelerant: couldn't establish if gop already set dplink\n");
		gInfo->shared_info->dp_link_trained_by_gop = false;
	}
	*/
	return B_OK;
}

status_t
intel_arc_init_accelerant(int device)
{
	debug_printf("intel_arc.accelerant: intel_arc_init_accelerant() start\n");
	status_t status = init_common(device, false);
	if (status != B_OK)
		return status;
	//if (gInfo->shared_info->accelerant_in_use) {
    //     uninit_common();
    //     return B_NOT_ALLOWED;
    //}
	gInfo->shared_info->accelerant_in_use = true;
	
	debug_printf("intel_arc.accelerant: Summary: Pipe=%d, DDI Port=%u, Detected Ports Mask=0x%02x\n",
		gInfo->shared_info->active_pipe,
		gInfo->shared_info->active_ddi_port,
		gInfo->shared_info->detected_port_bits);

	return B_OK;
}

ssize_t
intel_arc_accelerant_clone_info_size(void)
{
	return B_PATH_NAME_LENGTH;
}

void
intel_arc_get_accelerant_clone_info(void* info)
{
	ioctl(gInfo->device, INTEL_ARC_GET_DEVICE_NAME, info, B_PATH_NAME_LENGTH);
}

status_t
intel_arc_clone_accelerant(void* data)
{
	debug_printf("intel_arc.accelerant: intel_arc_clone_accelerant()\n");
	char path[B_PATH_NAME_LENGTH];
	snprintf(path, sizeof(path), "/dev/%s", (const char*)data);

	int fd = open(path, B_READ_WRITE);
	if (fd < 0)
		return errno;

	status_t status = init_common(fd, true);
	if (status != B_OK) {
		close(fd);
		return status;
	}

	read_edid_from_hardware();

	status = gInfo->mode_list_area = clone_area("intel arc cloned modes",
		(void**)&gInfo->mode_list, B_ANY_ADDRESS, B_READ_AREA,
		gInfo->shared_info->mode_list_area);
	if (status < B_OK) {
		uninit_common();
		return status;
	}

	area_info info;
	status = ioctl(gInfo->device, INTEL_ARC_CLONE_FRAME_BUFFER, &info, sizeof(info));
	if (status == B_OK) {
		gInfo->frame_buffer_area = info.area;
		gInfo->frame_buffer = info.address;
		if (!gInfo->shared_info->bDisableOverlay) {
			status_t overlayStatus = init_overlay_memory_manager();
			if (overlayStatus != B_OK)
				debug_printf("intel_arc.accelerant: overlay VRAM heap unavailable in clone: %s\n",
					strerror(overlayStatus));
		}
	}

	return B_OK;
}

void
intel_arc_uninit_accelerant(void)
{
	debug_printf("intel_arc.accelerant: intel_arc_uninit_accelerant()\n");
	if (gInfo->mode_list_area >= B_OK)
		delete_area(gInfo->mode_list_area);
	uninit_common();
}

status_t
intel_arc_get_accelerant_device_info(accelerant_device_info* info)
{
	info->version = B_ACCELERANT_VERSION;
	snprintf(info->name, sizeof(info->name), "Intel ARC");
	snprintf(info->chipset, sizeof(info->chipset), "%s",
		gInfo->shared_info->device_identifier);
	snprintf(info->serial_no, sizeof(info->serial_no), "%04x:%04x",
		gInfo->shared_info->vendor_id, gInfo->shared_info->device_id);
	info->memory = gInfo->shared_info->frame_buffer_size > 0xffffffffULL
		? 0xffffffffU : (uint32)gInfo->shared_info->frame_buffer_size;
	info->dac_speed = 0;
	return B_OK;
}

sem_id
intel_arc_accelerant_retrace_semaphore(void)
{
	return gInfo->shared_info != NULL ? gInfo->shared_info->vblank_sem : -1;
}

uint32
intel_arc_accelerant_engine_count(void)
{
	return 1;
}

status_t
intel_arc_acquire_engine(uint32 capabilities, uint32 maxWait,
	sync_token* syncToken, engine_token** engineToken)
{
	(void)capabilities;
	(void)maxWait;

	if (syncToken != NULL)
		intel_arc_sync_to_token(syncToken);

	*engineToken = &sEngineToken;
	return B_OK;
}

status_t
intel_arc_release_engine(engine_token* engineToken, sync_token* syncToken)
{
	if (syncToken != NULL)
		return intel_arc_get_sync_token(engineToken, syncToken);

	return B_OK;
}

void
intel_arc_wait_engine_idle(void)
{
}

status_t
intel_arc_get_sync_token(engine_token* engineToken, sync_token* syncToken)
{
	if (engineToken == NULL || syncToken == NULL)
		return B_BAD_VALUE;

	syncToken->engine_id = engineToken->engine_id;
	syncToken->counter = sSyncCounter++;
	memset(syncToken->opaque, 0, sizeof(syncToken->opaque));
	return B_OK;
}

status_t
intel_arc_sync_to_token(sync_token* syncToken)
{
	(void)syncToken;
	intel_arc_wait_engine_idle();
	return B_OK;
}



uint32
intel_arc_accelerant_mode_count(void)
{
	(void)handle_hotplug_event();
	return gInfo->shared_info->mode_count;
}



extern "C" void*
get_accelerant_hook(uint32 feature, void* /*data*/)
{
	switch (feature) {
		case B_INIT_ACCELERANT:
			return (void*)intel_arc_init_accelerant;
		case B_UNINIT_ACCELERANT:
			return (void*)intel_arc_uninit_accelerant;
		case B_CLONE_ACCELERANT:
			return (void*)intel_arc_clone_accelerant;
		case B_ACCELERANT_CLONE_INFO_SIZE:
			return (void*)intel_arc_accelerant_clone_info_size;
		case B_GET_ACCELERANT_CLONE_INFO:
			return (void*)intel_arc_get_accelerant_clone_info;
		case B_GET_ACCELERANT_DEVICE_INFO:
			return (void*)intel_arc_get_accelerant_device_info;
		case B_ACCELERANT_RETRACE_SEMAPHORE:
			return (void*)intel_arc_accelerant_retrace_semaphore;
		case B_ACCELERANT_ENGINE_COUNT:
			return (void*)intel_arc_accelerant_engine_count;
		case B_ACQUIRE_ENGINE:
			return (void*)intel_arc_acquire_engine;
		case B_RELEASE_ENGINE:
			return (void*)intel_arc_release_engine;
		case B_WAIT_ENGINE_IDLE:
			return (void*)intel_arc_wait_engine_idle;
		case B_GET_SYNC_TOKEN:
			return (void*)intel_arc_get_sync_token;
		case B_SYNC_TO_TOKEN:
			return (void*)intel_arc_sync_to_token;
		case B_DPMS_CAPABILITIES:
			return (void*)intel_arc_dpms_capabilities;
		case B_DPMS_MODE:
			return (void*)intel_arc_dpms_mode;
		case B_SET_DPMS_MODE:
			return (void*)intel_arc_set_dpms_mode;

		case B_ACCELERANT_MODE_COUNT:
			return (void*)intel_arc_accelerant_mode_count;
		case B_GET_MODE_LIST:
			return (void*)intel_arc_get_mode_list;
		case B_PROPOSE_DISPLAY_MODE:
			return (void*)intel_arc_propose_display_mode;
		case B_GET_PREFERRED_DISPLAY_MODE:
			return (void*)intel_arc_get_preferred_mode;
		case B_SET_DISPLAY_MODE:
			return (void*)intel_arc_set_display_mode;
		case B_GET_DISPLAY_MODE:
			return (void*)intel_arc_get_display_mode;
		case B_GET_EDID_INFO:
			return (void*)intel_arc_get_edid_info;
		case B_GET_FRAME_BUFFER_CONFIG:
			return (void*)intel_arc_get_frame_buffer_config;
		case B_GET_PIXEL_CLOCK_LIMITS:
			return (void*)intel_arc_get_pixel_clock_limits;
		case B_SET_INDEXED_COLORS:
			return (void*)intel_arc_set_indexed_colors;
		
		// OVERLAYS
		case B_OVERLAY_COUNT:
			return (void*)(gInfo->shared_info->bDisableOverlay ? NULL : intel_arc_overlay_count);
		case B_OVERLAY_SUPPORTED_SPACES:
			return (void*)(gInfo->shared_info->bDisableOverlay ? NULL : intel_arc_overlay_supported_spaces);
		case B_OVERLAY_SUPPORTED_FEATURES:
			return (void*)(gInfo->shared_info->bDisableOverlay ? NULL : intel_arc_overlay_supported_features);
		case B_ALLOCATE_OVERLAY_BUFFER:
			return (void*)(gInfo->shared_info->bDisableOverlay ? NULL : intel_arc_allocate_overlay_buffer);
		case B_RELEASE_OVERLAY_BUFFER:
			return (void*)(gInfo->shared_info->bDisableOverlay ? NULL : intel_arc_release_overlay_buffer);
		case B_GET_OVERLAY_CONSTRAINTS:
			return (void*)(gInfo->shared_info->bDisableOverlay ? NULL : intel_arc_get_overlay_constraints);
		case B_ALLOCATE_OVERLAY:
			return (void*)(gInfo->shared_info->bDisableOverlay ? NULL : intel_arc_allocate_overlay);
		case B_RELEASE_OVERLAY:
			return (void*)(gInfo->shared_info->bDisableOverlay ? NULL : intel_arc_release_overlay);
		case B_CONFIGURE_OVERLAY:
			return (void*)(gInfo->shared_info->bDisableOverlay ? NULL : intel_arc_configure_overlay);
		
		// HW CUR
		case B_SET_CURSOR_SHAPE:
            return (void*)(gInfo->shared_info->bDisableHdwCursor ? NULL : SetCursorShape);
        case B_MOVE_CURSOR:
            return (void*)(gInfo->shared_info->bDisableHdwCursor ? NULL : MoveCursor);
        case B_SHOW_CURSOR:
            return (void*)(gInfo->shared_info->bDisableHdwCursor ? NULL : ShowCursor);

#ifdef IS_PIRATI_BUILD
        case B_SET_CURSOR_BITMAP:
            return (void*)(gInfo->shared_info->bDisableHdwCursor ? NULL : SetCursorBitmap);
        case B_GET_CURSOR_BITS:
            return (void*)(gInfo->shared_info->bDisableHdwCursor ? NULL : GetCursorBits);
#endif
	}

	return NULL;
}

