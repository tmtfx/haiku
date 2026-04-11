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
#define DEFAULT_INPUT_CLOCK 24000000

/* --- sm750_get_clocks --- */
void 
sm750_get_clocks(vuint32 *regs, shared_info *si)
{
    // Leggiamo i divisori attuali impostati dal BIOS
    uint32 mclk_reg = regs[0x38 >> 2];
    uint32 sclk_reg = regs[0x3C >> 2];
    
    dprintf("SM750: MCLK Reg: 0x%08" B_PRIx32 ", SCLK Reg: 0x%08" B_PRIx32 "\n", mclk_reg, sclk_reg);
    
    // Salviamo la frequenza di riferimento per l'accelerante
    si->card_info.f_ref = DEFAULT_INPUT_CLOCK / 1000000.0f; 
}

/* --- sm750_init_chip --- */
void sm750_init_chip(DeviceInfo *di) {
    if (!di || !di->regs) return;

    vuint32 *regs = di->regs;
    shared_info *si = di->si;

    dprintf("SM750: --- Inizializzazione Hardware ---\n");

    // 1. DIAGNOSTICA PCI/BOOT (Solo log)
    struct frame_buffer_boot_info* bi = (struct frame_buffer_boot_info*)get_boot_item(FRAME_BUFFER_BOOT_INFO, NULL);
    if (bi) {
        dprintf("SM750: VESA FB a 0x%" B_PRIx64 ", %" B_PRId32 "x%" B_PRId32 "\n", 
                (uint64)bi->physical_frame_buffer, bi->width, bi->height);
    }

    // 2. SVEGLIA E POWER MANAGEMENT
    // Impostiamo Power Mode 0 (Full power)
    uint32 pm_ctrl = regs[0x4C >> 2];
    regs[0x4C >> 2] = (pm_ctrl & ~0x3); 
    snooze(1000);

    // 3. CONFIGURAZIONE SYSTEM CONTROL (Offset 0x04)
    // Abilitiamo il motore 2D, il DAC e sblocchiamo l'accesso MMIO esteso
    uint32 sys_ctrl = regs[0x04 >> 2];
    sys_ctrl |= (1 << 0);  // Crystal Clock enable
    sys_ctrl |= (1 << 4);  // 2D Engine enable
    sys_ctrl |= (1 << 7);  // MMIO Access Enable
    sys_ctrl |= (1 << 12); // BAR Force (usa BAR1 per i registri)
    regs[0x04 >> 2] = sys_ctrl;

    // 4. VERIFICA MEMORIA (DRAM Control)
    // NON scrivere 0xDEADBEEF qui! Leggiamo solo la configurazione attuale.
    uint32 dram_ctrl = regs[0x10 >> 2];
    uint32 mem_size_code = (dram_ctrl >> 13) & 0x07;
    uint32 detected_mem;

    switch (mem_size_code) {
        case 0: detected_mem = 8 * 1024 * 1024; break;
        case 1: detected_mem = 16 * 1024 * 1024; break;
        case 2: detected_mem = 32 * 1024 * 1024; break;
        case 3: detected_mem = 64 * 1024 * 1024; break;
        default: detected_mem = 4 * 1024 * 1024; break;
    }
    
    // Aggiorniamo la shared info con la memoria reale rilevata dal chip
    si->card_info.mem_size = detected_mem;
    dprintf("SM750: Memoria rilevata dal controller: %" B_PRIu32 " MB\n", detected_mem / (1024*1024));

    // 5. VGA BYPASS (Necessario per usare il Framebuffer lineare in modo nativo)
    // Abilitiamo il bypass VGA su entrambe le pipe (Primary e Secondary)
    regs[0x80000 >> 2] |= (1 << 8); 
    regs[0x80200 >> 2] |= (1 << 8); 

    // 6. RESET MOTORE GRAFICO (GE)
    // Procedura standard: attiva reset, aspetta, disattiva reset
    regs[0x04 >> 2] |= 0x00010000;
    snooze(2000);
    regs[0x04 >> 2] &= ~0x00010000;
    snooze(2000);

    // 7. DIAGNOSTICA FINALE MMIO
    sm750_get_clocks(regs, si);
    dprintf("SM750: Chip ID: 0x%08" B_PRIx32 ", Rev: 0x%08" B_PRIx32 "\n", 
            regs[0x54 >> 2], regs[0x30 >> 2]);
    
    dprintf("SM750: Inizializzazione completata.\n");
}
