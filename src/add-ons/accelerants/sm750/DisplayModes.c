/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include <Accelerant.h>
#include <Debug.h>
#include <edid.h>
#include <string.h>
#include "DriverInterface.h"
#include "protos.h"
#include "sm750_macros.h"

extern accelerant_info *gInfo;

status_t sm750_get_edid_info(void* info, size_t size, uint32* _version) {
    shared_info *si = gInfo->si;

    if (size < sizeof(struct edid1_info))
        return B_BUFFER_OVERFLOW;

    if (si->card_info.is_panel) {
        if (!si->card_info.has_edid_panel)
            return B_ERROR;
        memcpy(info, si->edid_panel, sizeof(struct edid1_info));
    } else {
        if (!si->card_info.has_edid_crt)
            return B_ERROR;
        memcpy(info, si->edid_crt, sizeof(struct edid1_info));
    }
    
    if (_version != NULL)
        *_version = EDID_VERSION_1;

    return B_OK;
}

status_t
sm750_set_display_mode(display_mode *mode)
{
    shared_info *si = gInfo->si;
    vuint32 *regs = gInfo->regs;
    
    uint32 bpp = (mode->space == B_RGB32) ? 32 : 16;
    uint32 color_fmt = (mode->space == B_RGB32) ? 2 : 1; // 2=32bpp, 1=16bpp
    uint32 pitch = mode->timing.h_display * (bpp / 8);
    
    // 1. Programmiamo il Clock (PLL)
    sm750_program_pll(mode->timing.pixel_clock, si->card_info.is_panel);

    if (si->card_info.is_panel) {
        // --- PRIMARY (PANEL) ---
        SM750_WREG32(SM750_PANEL_H_TOTAL_ACTIVE, ((mode->timing.h_total - 1) << 16) | (mode->timing.h_display - 1));
        SM750_WREG32(SM750_PANEL_H_SYNC, ((mode->timing.h_sync_end - mode->timing.h_sync_start) << 16) | (mode->timing.h_sync_start - 1));
        SM750_WREG32(SM750_PANEL_V_TOTAL_ACTIVE, ((mode->timing.v_total - 1) << 16) | (mode->timing.v_display - 1));
        SM750_WREG32(SM750_PANEL_V_SYNC, ((mode->timing.v_sync_end - mode->timing.v_sync_start) << 16) | (mode->timing.v_sync_start - 1));

        SM750_WREG32(SM750_DISP_PANEL_FB_ADDR, 0); 
        SM750_WREG32(SM750_DISP_PANEL_FB_WIDTH, (pitch << 16) | pitch);

        // Controllo: Enable + Timing Enable + Format
        uint32 ctrl = (1 << 0) | (1 << 2) | (color_fmt << 13);
        if (!(mode->timing.flags & B_POSITIVE_HSYNC)) ctrl |= (1 << 3);
        if (!(mode->timing.flags & B_POSITIVE_VSYNC)) ctrl |= (1 << 4);

        SM750_WREG32(SM750_PANEL_CONTROL, ctrl);
    } else {
        // --- SECONDARY (CRT) ---
        SM750_WREG32(SM750_CRT_H_TOTAL_ACTIVE, ((mode->timing.h_total - 1) << 16) | (mode->timing.h_display - 1));
        SM750_WREG32(SM750_CRT_H_SYNC, ((mode->timing.h_sync_end - mode->timing.h_sync_start) << 16) | (mode->timing.h_sync_start - 1));
        SM750_WREG32(SM750_CRT_V_TOTAL_ACTIVE, ((mode->timing.v_total - 1) << 16) | (mode->timing.v_display - 1));
        SM750_WREG32(SM750_CRT_V_SYNC, ((mode->timing.v_sync_end - mode->timing.v_sync_start) << 16) | (mode->timing.v_sync_start - 1));

        SM750_WREG32(SM750_DISP_CRT_FB_ADDR, 0);
        SM750_WREG32(SM750_DISP_CRT_FB_WIDTH, (pitch << 16) | pitch);

        // CRT Control: Enable(0) + TimingEnable(1) + DAC Enable(2) + Format
        uint32 ctrl = (1 << 0) | (1 << 1) | (1 << 2) | (color_fmt << 13);
        
        // Selettore sorgente: Bit 9 (0 = Primary, 1 = Secondary)
        // Se stiamo pilotando il CRT come controller secondario, mettiamo 1
        ctrl |= (1 << 9); 

        if (!(mode->timing.flags & B_POSITIVE_HSYNC)) ctrl |= (1 << 3);
        if (!(mode->timing.flags & B_POSITIVE_VSYNC)) ctrl |= (1 << 4);

        SM750_WREG32(SM750_CRT_CONTROL, ctrl);
    }

    si->dm = *mode;
    return B_OK;
}
/* vecchia
status_t
sm750_set_display_mode(display_mode *mode)
{
    vuint32 *regs = gInfo->regs;
    shared_info *si = gInfo->si;
    
    uint32 bpp = (mode->space == B_RGB32) ? 32 : 16;
    uint32 color_fmt = (mode->space == B_RGB32) ? 2 : 1;
    uint32 pitch = mode->timing.h_display * (bpp / 8);
    
    //uint32 reg_base = si->card_info.is_panel ? 0x80000 : 0x80200;

    // A. Sequenza di sblocco Power & System
    uint32 misc = SYS_R(MISC_CTRL);
    debug_printf("misc_ctrl letto in set_display_mode: %d",misc);
    misc |= (1 << 2) | (1 << 10); // Abilita controller e instrada su CRT
    SYS_W(MISC_CTRL, misc);
    misc = SYS_R(MISC_CTRL);
    debug_printf("nuovo misc_ctrl in set_display_mode: %d",misc);

    // B. Clock
    program_crt_pll(mode->timing.pixel_clock, regs);

    // C. Timing Orizzontali
    if (si->card_info.is_panel) {
        // Scriviamo negli offset 0x80000
        SM750_WREG32(SM750_PANEL_H_TOTAL_ACTIVE, ((mode->timing.h_total - 1) << 16) | (mode->timing.h_display - 1));
        SM750_WREG32(SM750_PANEL_H_SYNC, ((mode->timing.h_sync_end - mode->timing.h_sync_start) << 16) | (mode->timing.h_sync_start - 1));
        SM750_WREG32(SM750_PANEL_V_TOTAL_ACTIVE, ((mode->timing.v_total - 1) << 16) | (mode->timing.v_display - 1));
        SM750_WREG32(SM750_PANEL_V_SYNC, ((mode->timing.v_sync_end - mode->timing.v_sync_start) << 16) | (mode->timing.v_sync_start - 1));
        SM750_WREG32(SM750_DISP_PANEL_FB_ADDR, 0);
        SM750_WREG32(SM750_DISP_PANEL_FB_WIDTH, (pitch << 16) | pitch);
        
        uint32 ctrl = (1 << 0) | (1 << 2) | (1 << 9) | (color_fmt << 13);
        
        if (!(mode->timing.flags & B_POSITIVE_HSYNC)) ctrl |= (1 << 3);
        if (!(mode->timing.flags & B_POSITIVE_VSYNC)) ctrl |= (1 << 4);

        SM750_REG32(SM750_PANEL_CONTROL) = ctrl;
    } else {
        // Scriviamo negli offset 0x80200
        SM750_WREG32(SM750_CRT_H_TOTAL_ACTIVE, ((mode->timing.h_total - 1) << 16) | (mode->timing.h_display - 1));
        SM750_WREG32(SM750_CRT_H_SYNC, ((mode->timing.h_sync_end - mode->timing.h_sync_start) << 16) | (mode->timing.h_sync_start - 1));
        SM750_WREG32(SM750_CRT_V_TOTAL_ACTIVE, ((mode->timing.v_total - 1) << 16) | (mode->timing.v_display - 1));
        SM750_WREG32(SM750_CRT_V_SYNC, ((mode->timing.v_sync_end - mode->timing.v_sync_start) << 16) | (mode->timing.v_sync_start - 1));

        SM750_WREG32(SM750_DISP_CRT_FB_ADDR, 0);
        SM750_WREG32(SM750_DISP_CRT_FB_WIDTH, (pitch << 16) | pitch);

        // F. CRT Control & DAC
        uint32 ctrl = (1 << 0) | (1 << 2) | (1 << 9) | (color_fmt << 13);
    
        // Polarità Sync (Fondamentale per la stabilità su monitor moderni)
        if (!(mode->timing.flags & B_POSITIVE_HSYNC)) ctrl |= (1 << 3);
        if (!(mode->timing.flags & B_POSITIVE_VSYNC)) ctrl |= (1 << 4);

        SM750_REG32(SM750_CRT_CONTROL) = ctrl;
    }
   

    si->dm = *mode;
    return B_OK;
}
*/
status_t
sm750_get_frame_buffer_config(frame_buffer_config *config)
{
    shared_info *si = gInfo->si;
    // Calcolo BPP al volo per evitare ridondanze in shared_info
    uint32 bpp = 0;
    switch (si->dm.space) {
        case B_RGB32: case B_RGBA32: bpp = 32; break;
        case B_RGB16: bpp = 16; break;
        default: bpp = 8; break;
    }
    
    config->frame_buffer = (void *)si->framebuffer;
    config->frame_buffer_dma = (void *)si->framebuffer_pci;
    config->bytes_per_row = si->dm.timing.h_display * (bpp / 8);

    //config->bytes_per_row = si->dm.timing.h_display * (si->bits_per_pixel / 8);
    return B_OK;
}


status_t
sm750_get_display_mode(display_mode *current_mode)
{
    shared_info *si = gInfo->si;
    *current_mode = si->dm; // Restituiamo l'ultima modalità salvata
    return B_OK;
}

status_t
sm750_propose_display_mode(display_mode *target, const display_mode *low, const display_mode *high)
{
	shared_info *si = gInfo->si;
    // 1. Limite Pixel Clock
    // Abbiamo detto che i tuoi DAC arrivano a 300MHz
    if (target->timing.pixel_clock > si->card_info.max_pclk) 
        return B_BAD_VALUE;

    // 2. Limite Memoria (16MB)
    uint32 bytes_per_pixel = 0;
    switch (target->space) {
        case B_RGB32: bytes_per_pixel = 4; break;
        case B_RGB16: bytes_per_pixel = 2; break;
        case B_CMAP8: bytes_per_pixel = 1; break;
        default: return B_BAD_VALUE;
    }
    
    uint32 mem_needed = target->virtual_width * target->virtual_height * bytes_per_pixel;
    if (mem_needed > si->card_info.mem_size) {
    	debug_printf("SM750_ACC: resolution exceeds memory size");
        return B_BAD_VALUE;
    }

    // 3. Allineamento Hardware
    // La SM750 di solito vuole la larghezza multipla di 8 o 16 pixel
    target->timing.h_display = (target->timing.h_display + 7) & ~7;
    
    return B_OK;
}
/*{
    shared_info *si = gInfo->si;
    
    // 1. Calcolo BPP richiesto
    uint32 bpp = 8;
    if (target->space == B_RGB32) bpp = 32;
    else if (target->space == B_RGB16) bpp = 16;
    
    // 2. Verifica memoria necessaria
    uint32 required_mem = target->timing.h_display * target->timing.v_display * (bpp / 8);
    if (required_mem > si->card_info.mem_size) {
        return B_BAD_VALUE; // Risoluzione troppo alta per questa scheda
    }

    // 3. Limiti Pixel Clock (SM750 arriva a circa 200MHz)
    if (target->timing.pixel_clock > 200000) {
        return B_BAD_VALUE;
    }

    return B_OK;
}*/

uint32 sm750_accelerant_mode_count(void) {
    // In produzione qui contiamo i modi validati dall'EDID
    return 1; // Proviamo con 1 solo modo per testare la stabilità
}

status_t sm750_get_mode_list(display_mode* dm) {
    // Definiamo un modo standard: 1024x768 @ 60Hz
    display_mode mode = {
        { 65000, 1024, 1048, 1184, 1344, 768, 771, 777, 806, 0 }, // Timing VESA
        B_RGB32,
        1024, 768, 0, 0
    };
    dm[0] = mode;
    return B_OK;
}
