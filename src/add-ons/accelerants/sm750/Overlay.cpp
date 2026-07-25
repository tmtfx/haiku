/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include <string.h>
#include <malloc.h>
#include "sm750_macros.h"
#include "DriverInterface.h"
#include "protos.h"
#include "memory_manager.h"
#include "sm750_macros.h"

extern accelerant_info *gInfo;

#define CALLED() debug_printf("SM750_ACC OVERLAY: %s\n", __FUNCTION__)

static void
sm750_set_video_scale(const overlay_window *window, const overlay_buffer *buffer)
{
//	CALLED();
    vuint32 *regs = gInfo->regs;
    
    uint32 srcW = buffer->width;
    uint32 srcH = buffer->height;
    
    uint32 destW = window->width;
    uint32 destH = window->height;

    uint32 hScaleValue, vScaleValue;
    uint32 hsBit = 0, vsBit = 0;

    // Horizontal scaling
    if (destW >= srcW) {
        hsBit = 0; // Expansion
        hScaleValue = (uint32)(((float)srcW / destW) * 4096.0f);
    } else {
        hsBit = 1; // Shrinking
        hScaleValue = (uint32)(((float)destW / srcW) * 4096.0f);
    }

    // Vertical scaling
    if (destH >= srcH) {
        vsBit = 0; // Expansion
        vScaleValue = (uint32)(((float)srcH / destH) * 4096.0f);
    } else {
        vsBit = 1; // Shrinking
        vScaleValue = (uint32)(((float)destH / srcH) * 4096.0f);
    }

    // Compose the register 0x080058
    // VS (bit 31), VScale (27:16), HS (bit 15), HScale (11:0)
    uint32 videoScale = ((vsBit & 0x1) << 31) | ((vScaleValue & 0xFFF) << 16) |
                        ((hsBit & 0x1) << 15) | (hScaleValue & 0xFFF);

    SM750_WREG32(SM750_DISP_PANEL_VIDEO_SCALE, videoScale);
}

uint32 
sm750_overlay_count(const display_mode *dm)
{
//	CALLED();
    // SM750 has 1 video layer and 1 alpha video layer (we probably would use it for 32 bit alphablended cursor)
    // Let's return 1
    return 1;
}

const uint32 *
sm750_overlay_supported_spaces(const display_mode *dm)
{
//	CALLED();
//	debug_printf("SM750_ACC: sm750_overlay_supported_spaces chiamato (dm: %p)\n", dm);
    static const uint32 spaces[] = {
        B_YCbCr422,	// YUY2 (most common)
        B_RGB16,	// RGB 5:6:5
        0
    };
    return spaces;
}

void 
sm750_get_overlay_constraints(const display_mode *dm, const overlay_buffer *ob,
    overlay_constraints *oc)
{
//	CALLED();
	if (dm == NULL) {
        //debug_printf("SM750_ACC: ATTENZIONE! display_mode è NULL in constraints\n");
        return;
    }
    if (!oc) {
    	//debug_printf("SM750_ACC: Richista overlay constraints con oc NULL");
    	return;
    }
    //debug_printf("SM750_ACC: Constraints per buffer %p\n", ob);
    oc->view.width_alignment = 1;//7      // Algnment to 8 pixels
    oc->view.height_alignment = 0;
    oc->window.width_alignment = 1;//7
    oc->window.height_alignment = 0;
    
    // Source dimensions (original video)
    oc->view.width.min = 32;
    oc->view.width.max = 1920; 
    oc->view.height.min = 32;
    oc->view.height.max = 1080;

    // Destination dimensions (on screen)
    if (dm) {
        oc->window.width.min = 32;
        oc->window.width.max = dm->virtual_width;
        oc->window.height.min = 32;
        oc->window.height.max = dm->virtual_height;
    }
    
    // Scale factor (Haiku uses 1/64k as unit)
    oc->h_scale.min = 1.0f / 8.0f; 
    oc->h_scale.max = 8.0f;
    oc->v_scale.min = 1.0f / 8.0f;
    oc->v_scale.max = 8.0f;
}

overlay_buffer *
sm750_allocate_overlay_buffer(color_space cs, uint16 width, uint16 height)
{
//	CALLED();
    shared_info *si = gInfo->si;
    
    // Look for a free slot in myBuffer array
    int slot = -1;
    for (int i = 0; i < MAXBUFFERS; i++) {
        if (si->overlay.myBufferBlockID[i] == 0) {
            slot = i;
            break;
        }
    }

    if (slot == -1) return NULL;

    uint32 bytesPerPixel = 2; //if we use only supported formats
    //uint32 bytesPerPixel = (cs == B_RGB32) ? 4 : 2; // if B_RGB32 is kept for test purposes
    uint32 alignedPitch = (width * bytesPerPixel + 15) & ~15;
    uint32 size = alignedPitch * height;
    uint32 blockID, offset;
    
    // Add 15 extra bytes for alignment if mem_alloc gives us an odd offset
    uint32 allocSize = size + 15;
    
    // SPACE CHECK: do we have enough RAM after reserved desktop mem?
    uint32 available_vram = mem_get_free_memory((mem_info*)si->mem_mgr);

    if (size > available_vram) {
        debug_printf("SM750_ACC: Overlay too big! Free RAM heap: %u, Needed: %u\n", available_vram, size);
        return NULL;
    }
    
    if (mem_alloc((mem_info*)si->mem_mgr, allocSize, (void*)'VIDO', &blockID, &offset) != B_OK){
        debug_printf("SM750_ACC: Kernel VRAM Heap Out of Memory!\n");
        return NULL;
    }
    
    uint32 alignedOffset = (offset + 15) & ~15;

    // Fill share_info slot
    overlay_buffer *ob = &si->overlay.myBuffer[slot];
    ob->space = cs;
    ob->width = width;
    ob->height = height;
    ob->bytes_per_row = alignedPitch;
    //ob->buffer = (void *)((addr_t)gInfo->framebuffer + alignedOffset);
	ob->buffer = (void *)((addr_t)si->framebuffer + alignedOffset);
    ob->buffer_dma = (void *)(addr_t)alignedOffset;
    
    //if (ob) debug_printf("SM750_ACC: Buffer allocated. Original offset: 0x%08x, Aligned: 0x%08x, Pitch: %u\n", 
    //             offset, (uint32)(addr_t)ob->buffer_dma, alignedPitch);

    // Save blockID
    si->overlay.myBufferBlockID[slot] = blockID;

    return ob;
}

void
sm750_configure_overlay(const overlay_window *window, const overlay_buffer *buffer)
{
//	CALLED();
	
	vuint32 *regs = gInfo->regs;
	
	if (buffer == NULL || window == NULL) {
		//debug_printf("SM750_ACC: Rilevato buffer/window NULL, spengo il piano video.\n");
        uint32 control = SM750_REG32(SM750_DISP_PANEL_VIDEO_DISP_CTRL);
        control &= ~(1 << 2); // Disabilita Video Plane (Bit 2)
        SM750_WREG32(SM750_DISP_PANEL_VIDEO_DISP_CTRL, control);
        return;
    }
    
    if (buffer->buffer_dma == NULL)
        return;

    // Buffer address (Offset VRAM)
    uint32 bufferOffset = (uint32)(addr_t)buffer->buffer_dma;
    uint32 targetAddr = bufferOffset & 0x03FFFFF0;
    // OTTIMIZZAZIONE: Se l'indirizzo del buffer è identico al precedente 
    // e lo scaling/finestra non cambiano drasticamente, possiamo evitare 
    // di ricalcolare e riscrivere tutti i registri ad ogni singolo frame.
    // (Verifichiamo direttamente sul registro hardware se sta già puntando lì)
    uint32 currentAddr = SM750_REG32(SM750_DISP_PANEL_VIDEO_FB0_ADDR);
    
    if (currentAddr == targetAddr) {
    	// Il buffer è lo stesso, aggiorniamo solo le coordinate della finestra 
        // nel caso si stia muovendo o ridimensionando, saltando il resto se immobile.
        uint32 top = (uint32)window->v_start;
        uint32 left = (uint32)window->h_start;
        uint32 bottom = top + window->height;
        uint32 right = left + window->width;

        uint32 topLeft = ((top & 0x7FF) << 16) | (left & 0x7FF);
        uint32 bottomRight = ((bottom & 0x7FF) << 16) | (right & 0x7FF);
        
        SM750_WREG32(SM750_DISP_PANEL_VIDEO_PL_TL_POS, topLeft);
        SM750_WREG32(SM750_DISP_PANEL_VIDEO_PL_BR_POS, bottomRight);
        sm750_set_video_scale(window, buffer);
        return;
    }
    
    SM750_WREG32(SM750_DISP_PANEL_VIDEO_FB0_ADDR, targetAddr);
    
    // End calculation: beginning + (Pitch * height) - 1
    uint32 bufferSize = buffer->bytes_per_row * buffer->height;
    uint32 endAddr = bufferOffset + bufferSize - 1;
    // FB 0 Last Address: 
    // Apply mask 0x03FFFFF0 to force bit 3:0 to zero 
    // 26 addressing bits (25:4).
    // with internal memory, bit 27 should stay 0.
    uint32 lastAddrReg = endAddr & 0x03FFFFF0;
    SM750_WREG32(SM750_DISP_PANEL_VIDEO_FB0_LAST_ADDR, lastAddrReg);
    
    // Width (Pitch)
    uint32 fbPitchUnits = buffer->bytes_per_row / 16;
    // (window width caclulation in units of 16 bytes.
    // BEWARE: If the pixel width is not divisible by 16,
    // we must round up to avoid cropping the video.
    //uint32 bytesPerPixel = (buffer->space == B_RGB32) ? 4 : 2;
    uint32 bytesPerPixel = 2;
    uint32 windowWidthBytes = buffer->width * bytesPerPixel;
    uint32 windowWidthUnits = (windowWidthBytes + 15) / 16;
    
    // Mask as datasheet wants (10 bit for field: 29:20 e 13:4)
    uint32 fbWidthReg = ((windowWidthUnits & 0x3FF) << 20) | ((fbPitchUnits & 0x3FF) << 4);
    //debug_printf("SM750_ACC: FB_WIDTH Reg (0x44): 0x%08x (WinUnits: %u, PitchUnits: %u)\n", fbWidthReg, windowWidthUnits, fbPitchUnits);

    SM750_WREG32(SM750_DISP_PANEL_VIDEO_FB_WIDTH, fbWidthReg);
    //SM750_WREG32(SM750_DISP_PANEL_VIDEO_FB_WIDTH, (pitchIn128BitUnits << 20) | (pitchIn128BitUnits << 4));

    // Window Coordinates
    // TL: h_start (Left), v_start (Top)
    uint32 top = (uint32)window->v_start;
    uint32 left = (uint32)window->h_start;
    
    // BR: caclulate Bottom and Right
    uint32 bottom = top + window->height;
    uint32 right = left + window->width;

    uint32 topLeft = ((top & 0x7FF) << 16) | (left & 0x7FF);
    uint32 bottomRight = ((bottom & 0x7FF) << 16) | (right & 0x7FF);
    
    SM750_WREG32(SM750_DISP_PANEL_VIDEO_PL_TL_POS, topLeft);
    SM750_WREG32(SM750_DISP_PANEL_VIDEO_PL_BR_POS, bottomRight);
    
    // Scaling Factor (0x58)
    sm750_set_video_scale(window, buffer);
    
    // Initial Scale (0x5C)
    // Set to 0 to start sampling from the buffer source
    SM750_WREG32(SM750_DISP_PANEL_VIDEO_INIT_SCALE, 0);
    
    // YUV constants initialization (Color Space Conversion)
    //SM750_WREG32(SM750_DISP_PANEL_VIDEO_YUV_CONST, 0x00531515);
    uint32 csc_video = SM750_REG32(SM750_DISP_PANEL_VIDEO_YUV_CONST);
    //debug_printf("SM750_ACC: YUV constants(Color Space Conversion) 0x%08x\n", csc_video);

    // Control Register Configuration (0x080040)
    uint32 control = SM750_REG32(SM750_DISP_PANEL_VIDEO_DISP_CTRL);
    //debug_printf("SM750_ACC: old value for video control register: 0x%08x\n", control);
    
    control = 0;

    uint32 format = 0;
    switch (buffer->space) {
        case B_YCbCr422: format = 3; break; // YUYV
        case B_RGB16:    format = 1; break; // 16bpp 5:6:5
        default:
            debug_printf("SM750_ACC: Format %d not supported by hardware! Force YUV.\n", buffer->space);
            format = 3; 
            break;
    }
    control |= (format & 0x3);
    
    //control = 0; // Resettiamo a zero per sicurezza
    
    // Ignoriamo quello che c'è scritto in buffer->space. 
    // Diciamo all'hardware che il buffer contiene pixel RGB16 (Formato = 1)
    //uint32 format = 1; 
    
    //debug_printf("SM750_ACC: HACK! Forzato formato hardware video a RGB16 (1)\n");
    //control |= (format & 0x3);

    // Enable Video Plane
    control |= (1 << 2);

    // Enable Interpolation (Smooth scaling)
    control |= (1 << 9) | (1 << 8);

    // Enable Line Buffer (Needed for scaling)
    control |= (1 << 18);

    // FIFO Request Level: 11 (Max fill priority)
    control |= (3 << 16);

    // Byte Swapping: 0 for YUYV, 1 for UYVY
    // Haiku B_YCbCr422 is usually Y0-U0-Y1-V0, so BS=0, chech this <----
    //control &= ~(1 << 12);

    // Ensure Force Scale 1/2 are off
    //control &= ~((1 << 11) | (1 << 10));
    
    //debug_printf("SM750_ACC: new video control register: 0x%08x\n", control);
    SM750_WREG32(SM750_DISP_PANEL_VIDEO_DISP_CTRL, control);
}

status_t
sm750_release_overlay_buffer(const overlay_buffer *buffer)
{
	//CALLED();
	if (buffer == NULL)
        return B_BAD_VALUE;
        
    shared_info *si = gInfo->si;

    for (int i = 0; i < MAXBUFFERS; i++) {
        if (&si->overlay.myBuffer[i] == buffer) {
            mem_free((mem_info*)si->mem_mgr, si->overlay.myBufferBlockID[i], (void*)'VIDO');
            
            si->overlay.myBufferBlockID[i] = 0;
            return B_OK;
        }
    }

    return B_ERROR;
}

overlay_token
sm750_allocate_overlay(void)
{
	//CALLED();
	shared_info *si = gInfo->si;
    if (atomic_test_and_set(&si->overlay_in_use, 1, 0) != 0) {
        return NULL; 
    }

    si->overlay.overlay_token++;
    overlay_token token = (overlay_token)si->overlay.overlay_token;
    
    //debug_printf("SM750_ACC: Overlay allocated. Token ID: %p\n", token);
    return token;
}

status_t
sm750_release_overlay(overlay_token token)
{
	//CALLED();
	if (token != (overlay_token)gInfo->si->overlay.overlay_token) {
        return B_BAD_VALUE;
    }
	
    vuint32 *regs = gInfo->regs;

    // hardware off before releasing the token
    uint32 control = SM750_REG32(SM750_DISP_PANEL_VIDEO_DISP_CTRL);
    control &= ~(1 << 2); // Disable Video Plane
    SM750_WREG32(SM750_DISP_PANEL_VIDEO_DISP_CTRL, control);
    
    gInfo->si->overlay.overlay_token = 0;
    atomic_set(&gInfo->si->overlay_in_use, 0);
    
    snooze(10000); // 10ms

    //debug_printf("SM750_ACC: Overlay released and hardware off.\n");
    return B_OK;
}

status_t
sm750_configure_overlay_api(overlay_token token, const overlay_buffer *buffer,
    const overlay_window *window, const overlay_view *view)
{
	//CALLED();
	if (token != (overlay_token)gInfo->si->overlay.overlay_token) {
        return B_BAD_VALUE;
    }
	vuint32 *regs = gInfo->regs;
	
    // If buffer is NULL, the user wants to hide the overlay temporarily
    if (buffer == NULL) {
        uint32 control = SM750_REG32(SM750_DISP_PANEL_VIDEO_DISP_CTRL);
        control &= ~(1 << 2); 
        SM750_WREG32(SM750_DISP_PANEL_VIDEO_DISP_CTRL, control);
        return B_OK;
    }

    sm750_configure_overlay(window, buffer);

    return B_OK;
}

uint32
sm750_overlay_supported_features(uint32 space)
{
	//CALLED();
    // The SM750 is special: the video layer supports YUYV but doesn't have color keying.
    // The alpha video layer has color keying but not YUYV format.
    // B_OVERLAY_COLOR_KEY | // Transparency via color (essential)
    return B_OVERLAY_HORIZONTAL_FILTERING | // Scaling fluido orizzontale
           B_OVERLAY_VERTICAL_FILTERING;   // Scaling fluido verticale
}
