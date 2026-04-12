/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include <KernelExport.h>
#include <SupportDefs.h>
#include <boot_item.h>
#include <frame_buffer_console.h>

#include "DriverInterface.h"
#include "sm750_macros.h"

extern pci_module_info *pci;

/* Frequenza del cristallo di riferimento (Standard SM750) */
//#define DEFAULT_INPUT_CLOCK 24000000

/* --- sm750_get_clocks --- */
void 
sm750_get_clocks(vuint32 *regs, shared_info *si)
{
    // Offset corretti dal datasheet pag. 38
    uint32 sclk_reg = regs[0x44 >> 2]; // System PLL
    uint32 mclk_reg = regs[0x48 >> 2]; // Master (Memory) PLL
    uint32 vclk_reg = regs[0x4C >> 2]; // Display PLL
    
    dprintf("SM750: SCLK Reg (0x44): 0x%08" B_PRIx32 "\n", sclk_reg);
    dprintf("SM750: MCLK Reg (0x48): 0x%08" B_PRIx32 "\n", mclk_reg);
    dprintf("SM750: VCLK Reg (0x4C): 0x%08" B_PRIx32 "\n", vclk_reg);

    // Salviamo la frequenza di riferimento (24MHz)
    si->card_info.f_ref = 24.0f; 
}

/* --- sm750_init_chip --- */
void sm750_init_chip(DeviceInfo *di) {
    if (!di || !di->regs) return;
    
    dprintf("SM750: --- Inizializzazione Hardware ---\n");

    vuint32 *regs = di->regs;
    shared_info *si = di->si;
    // Verifica ID via MMIO (Offset 0x54)
    uint32 dev_id = SM750_REG32(0x54);
    dprintf("SM750: MMIO ID Check: 0x%08" B_PRIx32 "\n", dev_id);
    // Forza Power Mode 0 (Sveglia)
    SM750_WREG32(0x4C, (SM750_REG32(0x4C) & ~0x3));
    snooze(1000);
    
    // B. Configurazione System Control (Sblocca MMIO e 2D)
    uint32 sys_ctrl_04 = SM750_REG32(0x04);
    sys_ctrl_04 |= (1 << 0) | (1 << 4) | (1 << 7) | (1 << 12);
    SM750_WREG32(0x04, sys_ctrl_04);
    
    // C. Rilevazione Memoria (LOGICA UNICA)
    uint32 dram_ctrl = SM750_REG32(0x10);
    uint32 mem_size_code = (dram_ctrl >> 13) & 0x07;
    uint32 detected_mem;
    switch (mem_size_code) {
        case 0: detected_mem = 8 * 1024 * 1024; break;
        case 1: detected_mem = 16 * 1024 * 1024; break;
        case 2: detected_mem = 32 * 1024 * 1024; break;
        case 3: detected_mem = 64 * 1024 * 1024; break;
        default: detected_mem = 4 * 1024 * 1024; break;
    }
    if (detected_mem > 16 * 1024 * 1024) {
        dprintf("SM750: Warning, rilevati %d MB ma limito a 16 MB (Safe Mode)\n", detected_mem / (1024*1024));
        detected_mem = 16 * 1024 * 1024;
    }
    si->card_info.mem_size = detected_mem;
    
    // D. Rilevazione Uscita Attiva (LOGICA UNICA)
    /*
    uint32 sys_ctrl = SM750_REG32(0x00); 
    bool crt_is_not_3stated = !(sys_ctrl & (1 << 3));   // Bit 3: CRT Interface
    bool panel_is_not_3stated = !(sys_ctrl & (1 << 0)); // Bit 0: Panel Interface
    if (crt_is_not_3stated) {
        di->si->card_info.is_panel = false; // Priorità al CRT se non è spento
        dprintf("SM750: CRT rilevato come interfaccia attiva.\n");
    } else if (panel_is_not_3stated) {
        di->si->card_info.is_panel = true;
        dprintf("SM750: Panel rilevato come interfaccia attiva.\n");
    } else {
        // Se sono entrambi spenti (strano), default su CRT
        di->si->card_info.is_panel = false;
        dprintf("SM750: Nessuna interfaccia attiva rilevata, uso CRT.\n");
    }
    */
    /*oppure così
    uint32 display_ctrl = SM750_REG32(0x00080020); 

        // Se il bit 2 è attivo, il CRT è abilitato
        if (display_ctrl & (1 << 2)) {
            di->si->card_info.is_panel = false; // Stiamo usando il CRT (0x80200)
        } else {
            di->si->card_info.is_panel = true;  // Stiamo usando il Panel (0x80000)
        }
    */
    uint32 sys_ctrl_00 = SM750_REG32(0x00);
    if (!(sys_ctrl_00 & (1 << 3))) { // CRT is Normal
        si->card_info.is_panel = false;
        si->card_info.active_outputs = 2; // CRT
    } else {
        si->card_info.is_panel = true;
        si->card_info.active_outputs = 1; // Panel
    }
    
    si->card_info.chip_id = di->pci.device_id;
    si->card_info.f_ref = 24.0f;
    si->card_info.max_sclk = 130000; // Valori tipici SM750 (130MHz)
    si->card_info.max_mclk = 150000; // 150MHz
    si->card_info.max_pclk = 300000; // 300MHz (Limite DAC)
    
    // E. RESET MOTORE GRAFICO (GE)
    SM750_WREG32(0x04, SM750_REG32(0x04) | 0x00010000);
    snooze(2000);
    SM750_WREG32(0x04, SM750_REG32(0x04) & ~0x00010000);
    snooze(2000);
    
    // F. VGA BYPASS (Necessario per usare il Framebuffer lineare in modo nativo)
    // Abilitiamo il bypass VGA su entrambe le pipe (Primary e Secondary)
    SM750_WREG32(0x80000, SM750_REG32(0x80000) | (1 << 8));
    SM750_WREG32(0x80200, SM750_REG32(0x80200) | (1 << 8));

    dprintf("SM750: Init  completato. Mem: %d MB, Mode: %s\n", 
            detected_mem / (1024*1024), si->card_info.is_panel ? "PANEL" : "CRT");
    
    // Registro MISC_CTRL (Offset 0x04)
    // Bit 18: GPIO 31:24 Control Selection
    // Se settato a 1, i pin sono controllati dal registro GPIO, se 0 da altre unità.
    uint32 misc_ctrl = SM750_REG32(0x04);
    misc_ctrl |= (1 << 18); 
    SM750_WREG32(0x04, misc_ctrl);
    // Registro GPIO Control (Offset 0x1000C dal datasheet)
    // Dobbiamo azzerare i bit relativi ai pin 30 e 31 per forzarli come GPIO
    uint32 gpio_ctrl = SM750_REG32(0x1000C);
    gpio_ctrl &= ~(1 << 31); // SCL (CRT)
    gpio_ctrl &= ~(1 << 30); // SDA (CRT)
    SM750_WREG32(0x1000C, gpio_ctrl);
    
    uint32 detect = SM750_REG32(SM750_CRT_MONITOR_DETECT);
    dprintf("SM750: Monitor Detect Status: 0x%08" B_PRIx32 "\n", detect);
    
    // 7. DIAGNOSTICA FINALE MMIO
    sm750_get_clocks(regs, si);
    
    uint32 mmio_id = SM750_REG32(0x54);
    uint32 revision = SM750_REG32(0x30);
    dprintf("SM750: Init completato. MMIO ID: 0x%08X, Rev: 0x%08X\n", mmio_id, revision);


    // 1. DIAGNOSTICA PCI/BOOT (Solo log)
    struct frame_buffer_boot_info* bi = (struct frame_buffer_boot_info*)get_boot_item(FRAME_BUFFER_BOOT_INFO, NULL);
    if (bi) {
        dprintf("SM750: VESA FB a 0x%" B_PRIx64 ", %" B_PRId32 "x%" B_PRId32 "\n", 
                (uint64)bi->physical_frame_buffer, bi->width, bi->height);
    }

    //dprintf("SM750: Inizializzazione completata.\n");
}
