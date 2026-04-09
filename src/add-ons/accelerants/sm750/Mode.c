/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include <Accelerant.h>
#include <Debug.h>
#include "DriverInterface.h"
#include "protos.h"
#include "sm750_macros.h"

extern accelerant_info *gInfo;

/* Helper per scrivere nei registri MMIO usando la struttura shared_info */
/* Usiamo gInfo->shared_info->regs che abbiamo visto nel DriverInterface.h */
#define WRITE_REG(offset, val) (*(vuint32 *)((uint8 *)si->regs + (offset)) = (val))
#define READ_REG(offset) (*(vuint32 *)((uint8 *)si->regs + (offset)))

#define CRT_REG_BASE          0x080200
#define CRT_DISPLAY_CTRL      (CRT_REG_BASE + 0x00)
#define CRT_FB_ADDRESS        (CRT_REG_BASE + 0x04)
#define CRT_FB_WIDTH          (CRT_REG_BASE + 0x08)
#define CRT_H_TOTAL_ACTIVE    (CRT_REG_BASE + 0x0C)
#define CRT_H_SYNC            (CRT_REG_BASE + 0x10)
#define CRT_V_TOTAL_ACTIVE    (CRT_REG_BASE + 0x14)
#define CRT_V_SYNC            (CRT_REG_BASE + 0x18)

static void
sm750_set_crt_pll(shared_info *si, uint32 pixel_clock_khz)
{
    // Esempio per 40MHz (800x600): M=40, N=24, K=0 (OD=1)
    // Registro 0x000060 (CRT PLL Control)
    uint32 m = 40;
    uint32 n = 24;
    // Bit 18=1 (Crystal 24MHz), Bit 15:8=M, Bit 5:0=N
    uint32 pll_reg = (1 << 18) | (m << 8) | n;
    WRITE_REG(0x000060, pll_reg);
}

status_t 
sm750_set_display_mode(display_mode *mode) 
{
    // gInfo deve essere dichiarato come extern o essere parte del contesto
    shared_info *si = gInfo->shared_info;
    
    uint32 bytes_per_pixel = 4;
    uint32 color_format = 2; // 32 bpp
    
    if (mode->space == B_RGB16) {
        bytes_per_pixel = 2;
        color_format = 1;
    }

    uint32 h_active = mode->timing.h_display - 1;
    uint32 h_total  = mode->timing.h_total - 1;
    uint32 h_sync_start = mode->timing.h_sync_start - 1;
    uint32 h_sync_width = mode->timing.h_sync_end - mode->timing.h_sync_start;

    uint32 v_active = mode->timing.v_display - 1;
    uint32 v_total  = mode->timing.v_total - 1;
    uint32 v_sync_start = mode->timing.v_sync_start - 1;
    uint32 v_sync_height = mode->timing.v_sync_end - mode->timing.v_sync_start;

    uint32 pitch = mode->timing.h_display * bytes_per_pixel;

    // 1. Clock
    sm750_set_crt_pll(si, mode->timing.pixel_clock);

    // 2. Timing
    WRITE_REG(CRT_H_TOTAL_ACTIVE, (h_total << 16) | h_active);
    WRITE_REG(CRT_H_SYNC, (h_sync_start << 16) | h_sync_width);
    WRITE_REG(CRT_V_TOTAL_ACTIVE, (v_total << 16) | v_active);
    WRITE_REG(CRT_V_SYNC, (v_sync_start << 16) | v_sync_height);

    // 3. Memoria
    WRITE_REG(CRT_FB_ADDRESS, 0); 
    WRITE_REG(CRT_FB_WIDTH, (pitch << 16) | pitch);

    // 4. Attivazione (Bit 0: Enable, Bit 2: Graphics, Bit 9: DAC, Bit 13-14: Format)
    uint32 ctrl = (1 << 0) | (1 << 2) | (1 << 9) | (color_format << 13);
    WRITE_REG(CRT_DISPLAY_CTRL, ctrl);

    return B_OK;
}

status_t
sm750_get_display_mode(display_mode *current_mode)
{
    shared_info *si = gInfo->shared_info;
    *current_mode = si->dm; // Restituiamo l'ultima modalità salvata
    return B_OK;
}
status_t
sm750_get_frame_buffer_config(frame_buffer_config *config)
{
    shared_info *si = gInfo->shared_info;
    config->frame_buffer = (void *)si->framebuffer;
    config->frame_buffer_dma = (void *)si->framebuffer_pci;
    config->bytes_per_row = si->dm.timing.h_display * (si->bits_per_pixel / 8);
    return B_OK;
}
status_t
sm750_propose_display_mode(display_mode *target, const display_mode *low, const display_mode *high)
{
	// TODO
    // In un driver completo qui controlleremmo se il Pixel Clock è supportato.
    // Per ora, accettiamo il modo proposto.
    return B_OK;
}
