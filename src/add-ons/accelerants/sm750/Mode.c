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

/*
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
    shared_info *si = gInfo->si;
    
    uint32 bytes_per_pixel = 4;
    uint32 color_format = 2; // 32 bpp
    
    switch (mode->space) {
        case B_RGB32: case B_RGBA32:
            bytes_per_pixel = 4;
            color_format = 2;
            break;
        case B_RGB16: case B_RGB15:
            bytes_per_pixel = 2;
            color_format = 1;
            break;
        case B_CMAP8:
            bytes_per_pixel = 1;
            color_format = 0;
            break;
    }
    si->bits_per_pixel = bytes_per_pixel * 8;
    si->dm = *mode;

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
    
    uint32 dpms = READ_REG(SM750_SYS_SCLK_CTRL);//0x00003C);
    dpms &= ~0x00000003; // HSYNC e VSYNC attivi
    dpms |= (1 << 2);    // DAC On
    //WRITE_REG(0x00003C, dpms);
    WRITE_REG(SM750_SYS_SCLK_CTRL, dpms);

    // Registro di routing (0x000004 - MISC_CTRL)
    // Assicuriamoci che il CRT non sia in standby
    uint32 misc = READ_REG(SM750_SYS_MISC_CTRL);//0x000004);
    misc |= (1 << 10); // Abilita l'uscita CRT
    //WRITE_REG(0x000004, misc);
    WRITE_REG(SM750_SYS_MISC_CTRL, misc);

    return B_OK;
}*/
/* 3^ tentativo
static void
sm750_program_crt_pll(uint32 target_khz) //Calcolo PLL (Dynamic Clocking)
{
    uint32 f_ref = 24000; // Cristallo standard 24MHz
    uint32 best_m = 0, best_n = 0, best_od = 0;
    uint32 best_error = 0xffffffff;
    vuint32 *regs = gInfo->regs;

    // L'SM750 ha dei limiti sui divisori (N: 1-63, M: 1-255, OD: 0,1,2)
    for (uint32 od = 0; od <= 2; od++) {
        uint32 divisor = 1 << od;
        for (uint32 n = 1; n <= 63; n++) {
            for (uint32 m = 1; m <= 255; m++) {
                uint32 current_f = (f_ref * m / n) / divisor;
                uint32 error = abs((int32)target_khz - (int32)current_f);
                if (error < best_error) {
                    best_error = error;
                    best_m = m; best_n = n; best_od = od;
                }
            }
        }
    }

    // Costruiamo il valore del registro CRT PLL (0x000060)
    // Bit 31: 0 (PLL mode), Bit 18: 1 (24MHz Crystal), 
    // Bit 15-8: M, Bit 7-6: OD, Bit 5-0: N
    uint32 pll_val = (1 << 18) | (best_m << 8) | (best_od << 6) | best_n;
    SYS_W(PLL_CTRL, pll_val); // O l'offset specifico per il CRT se diverso
}

status_t
sm750_set_display_mode(display_mode *mode)
{
    vuint32 *regs = gInfo->regs;
    shared_info *si = gInfo->si;

    // 1. Sincronizzazione System Control
    uint32 misc = SYS_R(MISC_CTRL);
    misc |= (1 << 2) | (1 << 10); // Video Control + CRT Path
    SYS_W(MISC_CTRL, misc);

    // 2. Programmazione Clock
    sm750_program_crt_pll(mode->timing.pixel_clock);

    // 3. Timing Orizzontali (Total & Active)
    // Macro DISP_W usa SM750_DISP_...
    uint32 h_data = ((mode->timing.h_total - 1) << 16) | (mode->timing.h_display - 1);
    SM750_REG32(SM750_CRT_H_TOTAL_ACTIVE) = h_data;

    uint32 h_sync = ((mode->timing.h_sync_end - mode->timing.h_sync_start) << 16) 
                  | (mode->timing.h_sync_start - 1);
    SM750_REG32(SM750_CRT_H_SYNC) = h_sync;

    // 4. Timing Verticali
    uint32 v_data = ((mode->timing.v_total - 1) << 16) | (mode->timing.v_display - 1);
    SM750_REG32(SM750_CRT_V_TOTAL_ACTIVE) = v_data;

    uint32 v_sync = ((mode->timing.v_sync_end - mode->timing.v_sync_start) << 16) 
                  | (mode->timing.v_sync_start - 1);
    SM750_REG32(SM750_CRT_V_SYNC) = v_sync;

    // 5. Formato Colore e Pitch
    uint32 color_format = 0;
    uint32 bpp = 8;
    switch (mode->space) {
        case B_RGB32: case B_RGBA32: bpp = 32; color_format = 2; break;
        case B_RGB16: bpp = 16; color_format = 1; break;
    }

    uint32 bytes_per_row = mode->timing.h_display * (bpp / 8);
    // Registro FB_WIDTH: Pitch (31:16) | Pitch (15:0)
    SM750_REG32(SM750_DISP_CRT_FB_WIDTH) = (bytes_per_row << 16) | bytes_per_row;
    SM750_REG32(SM750_DISP_CRT_FB_ADDR) = 0; // Offset 0 in VRAM

    // 6. CRT Control (Il colpo di grazia)
    uint32 ctrl = (1 << 0)  // Enable CRT
                | (1 << 2)  // Graphics Mode
                | (1 << 9)  // Enable DAC
                | (color_format << 13);

    // Polarità (Standard VGA: Negative syncs)
    if (!(mode->timing.flags & B_POSITIVE_HSYNC)) ctrl |= (1 << 3);
    if (!(mode->timing.flags & B_POSITIVE_VSYNC)) ctrl |= (1 << 4);

    SM750_REG32(SM750_CRT_CONTROL) = ctrl;

    si->dm = *mode;
    return B_OK;
}*/
/* 4^ tentativo */
static void program_crt_pll(uint32 target_khz, vuint32 *regs) {
    uint32 m, n, od, best_m = 2, best_n = 1, best_od = 0;
    uint32 f_ref = 24000;
    uint32 min_err = 0xFFFFFFFF;

    for (od = 0; od <= 2; od++) {
        for (n = 1; n <= 63; n++) {
            for (m = 2; m <= 255; m++) {
                uint32 cur = (f_ref * m / n) >> od;
                uint32 err = abs((int32)target_khz - (int32)cur);
                if (err < min_err) {
                    min_err = err; best_m = m; best_n = n; best_od = od;
                }
            }
        }
    }
    // Scrittura registro SM750_SYS_PLL_CTRL
    SYS_W(PLL_CTRL, (1 << 18) | (best_m << 8) | (best_od << 6) | best_n);
}

status_t
sm750_set_display_mode(display_mode *mode)
{
    vuint32 *regs = gInfo->regs;
    shared_info *si = gInfo->si;

    // A. Sequenza di sblocco Power & System
    uint32 misc = SYS_R(MISC_CTRL);
    misc |= (1 << 2) | (1 << 10); // Abilita controller e instrada su CRT
    SYS_W(MISC_CTRL, misc);

    // B. Clock
    program_crt_pll(mode->timing.pixel_clock, regs);

    // C. Timing Orizzontali
    SM750_REG32(SM750_CRT_H_TOTAL_ACTIVE) = 
        ((mode->timing.h_total - 1) << 16) | (mode->timing.h_display - 1);
    SM750_REG32(SM750_CRT_H_SYNC) = 
        ((mode->timing.h_sync_end - mode->timing.h_sync_start) << 16) | (mode->timing.h_sync_start - 1);

    // D. Timing Verticali
    SM750_REG32(SM750_CRT_V_TOTAL_ACTIVE) = 
        ((mode->timing.v_total - 1) << 16) | (mode->timing.v_display - 1);
    SM750_REG32(SM750_CRT_V_SYNC) = 
        ((mode->timing.v_sync_end - mode->timing.v_sync_start) << 16) | (mode->timing.v_sync_start - 1);

    // E. Framebuffer Config
    uint32 bpp = (mode->space == B_RGB32) ? 32 : 16;
    uint32 color_fmt = (mode->space == B_RGB32) ? 2 : 1;
    uint32 pitch = mode->timing.h_display * (bpp / 8);

    SM750_REG32(SM750_DISP_CRT_FB_ADDR) = 0;
    SM750_REG32(SM750_DISP_CRT_FB_WIDTH) = (pitch << 16) | pitch;

    // F. CRT Control & DAC
    uint32 ctrl = (1 << 0) | (1 << 2) | (1 << 9) | (color_fmt << 13);
    
    // Polarità Sync (Fondamentale per la stabilità su monitor moderni)
    if (!(mode->timing.flags & B_POSITIVE_HSYNC)) ctrl |= (1 << 3);
    if (!(mode->timing.flags & B_POSITIVE_VSYNC)) ctrl |= (1 << 4);

    SM750_REG32(SM750_CRT_CONTROL) = ctrl;

    si->dm = *mode;
    return B_OK;
}
/*
static void
sm750_program_pll(uint32 target_pc_khz, uint32 pll_reg_offset) //Cerca-PLL (Algoritmo Dinamico)
{
    // Valori tipici SM750
    uint32 f_ref = 24000; // 24MHz
    uint32 best_m = 0, best_n = 0, best_od = 0;
    int32 min_diff = 0x7FFFFFFF;

    for (uint32 od = 0; od <= 6; od++) { // Output Divider 2^od
        for (uint32 n = 1; n <= 63; n++) {
            for (uint32 m = 1; m <= 255; m++) {
                uint32 current_f = (f_ref * m / n) >> od;
                int32 diff = target_pc_khz - current_f;
                if (abs(diff) < abs(min_diff)) {
                    min_diff = diff;
                    best_m = m; best_n = n; best_od = od;
                }
            }
        }
    }
    
    // Scrittura nel registro usando la macro
    // Bit 31: select PLL, Bit 15-8: M, Bit 5-0: N, Bit 7-6: OD
    uint32 val = (1 << 18) | (best_m << 8) | (best_od << 6) | best_n;
    // Usiamo direttamente l'offset passato
    SM750_REG32(pll_reg_offset) = val;
}

status_t 
sm750_set_display_mode(display_mode *mode) 
{
    shared_info *si = gInfo->si;
    vuint32 *regs = gInfo->regs; // Necessario per le macro SM750_REG32
    
    // 1. SVEGLIA IL SISTEMA (System Control)
    // Abilitiamo il controller video e il motore 2D tramite il MISC_CTRL
    uint32 misc = SYS_R(MISC_CTRL);
    misc |= (1 << 2);  // Video Control Enable
    misc |= (1 << 4);  // 2D Engine Enable
    misc |= (1 << 10); // CRT Enable (Routing segnale alla VGA)
    SYS_W(MISC_CTRL, misc);

    // 2. POWER MANAGEMENT
    // Impostiamo la Power Mode 0 (Full Power) per garantire i clock
    SYS_W(SYS_SCLK_CTRL, (1 << 31) | 0x00000003); 

    // 3. CALCOLO E PROGRAMMAZIONE PLL
    // Supponiamo che sm750_program_pll scriva nel registro SM750_SYS_PLL_CTRL
    // o direttamente in SM750_SYS_M2XCLK_CTRL per il display
    sm750_program_pll(mode->timing.pixel_clock, SM750_SYS_PLL_CTRL);

    // 4. TIMING ORIZZONTALI (Usa macro DISP_W e i nuovi registri CRT_H_...)
    // Registro 0x80000: Total (31:16) | Active (15:0)
    uint32 h_total_active = ((mode->timing.h_total - 1) << 16) | (mode->timing.h_display - 1);
    SM750_REG32(SM750_CRT_H_TOTAL_ACTIVE) = h_total_active;
    
    // H_SYNC: Width (31:16) | Start (15:0)
    uint32 h_sync_width_start = ((mode->timing.h_sync_end - mode->timing.h_sync_start) << 16) | (mode->timing.h_sync_start - 1);
    SM750_REG32(SM750_CRT_H_SYNC) = h_sync_width_start;

    // 5. TIMING VERTICALI
    uint32 v_total_active = ((mode->timing.v_total - 1) << 16) | (mode->timing.v_display - 1);
    SM750_REG32(SM750_CRT_V_TOTAL_ACTIVE) = v_total_active;
    
    uint32 v_sync_height_start = ((mode->timing.v_sync_end - mode->timing.v_sync_start) << 16) | (mode->timing.v_sync_start - 1);
    SM750_REG32(SM750_CRT_V_SYNC) = v_sync_height_start;

    // 6. CONFIGURAZIONE FRAMEBUFFER (Indirizzo e Pitch)
    SM750_REG32(SM750_DISP_CRT_FB_ADDR) = 0; // Inizio della VRAM
    
    uint32 bpp = 32;
    uint32 color_format = 2; // Default 32bpp
    if (mode->space == B_RGB16) { bpp = 16; color_format = 1; }
    
    uint32 pitch = mode->timing.h_display * (bpp / 8);
    SM750_REG32(SM750_DISP_CRT_FB_WIDTH) = (pitch << 16) | pitch;

    // 7. ABILITAZIONE FINALE (CRT CONTROL)
    uint32 crt_ctrl = (1 << 0)  // Enable CRT
                    | (1 << 2)  // Graphics Mode
                    | (1 << 9)  // Enable DAC
                    | (color_format << 13);
    
    // Gestione Polarità Sync (Production Code!)
    if (!(mode->timing.flags & B_POSITIVE_HSYNC)) crt_ctrl |= (1 << 3);
    if (!(mode->timing.flags & B_POSITIVE_VSYNC)) crt_ctrl |= (1 << 4);

    SM750_REG32(SM750_CRT_CONTROL) = crt_ctrl;

    si->dm = *mode;
    return B_OK;
}*/

status_t
sm750_get_display_mode(display_mode *current_mode)
{
    shared_info *si = gInfo->si;
    *current_mode = si->dm; // Restituiamo l'ultima modalità salvata
    return B_OK;
}
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
sm750_propose_display_mode(display_mode *target, const display_mode *low, const display_mode *high)
{
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
}
