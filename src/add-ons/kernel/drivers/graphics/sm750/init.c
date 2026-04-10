/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include <KernelExport.h>
#include <SupportDefs.h>
#include "DriverInterface.h"
#include "sm750_macros.h"

void sm750_get_clocks(vuint32 *regs, shared_info *si);
void sm750_pixel_test(shared_info *si);

/* Frequenza del cristallo di riferimento (Standard SM750) */
#define DEFAULT_INPUT_CLOCK 24000000

/* --- sm750_get_clocks --- */
/* Legge lo stato attuale dei PLL impostati dal BIOS/Bootloader */
/*void 
sm750_get_clocks(shared_info *si)
{
	vuint32 *regs = si->regs;
	uint32 reg;
	
	// MCLK - Memory Clock (Registro 0x00000048)
	reg = SYS_R(MCLK_CTRL);
	// Qui andrebbe la formula: (Input * M) / (N * D)
	// Per ora leggiamo il valore grezzo per il debug
	dprintf("SM750: MCLK Control Reg: 0x%08" B_PRIx32 "\n", reg);

	// SCLK - System Clock (Registro 0x0000004C)
	reg = SYS_R(SCLK_CTRL);
	dprintf("SM750: SCLK Control Reg: 0x%08" B_PRIx32 "\n", reg);
	
	// Salviamo la frequenza di riferimento nella shared_info 
	si->card_info.f_ref = DEFAULT_INPUT_CLOCK / 1000000.0f; // 24.0 MHz
}*/


/* call this function in kernel as: sm750_get_clocks(di->regs, di->si);
 * in accelerant: sm750_get_clocks(gInfo->regs, gInfo->si);
 */
void 
sm750_get_clocks(vuint32 *regs, shared_info *si)
{
    // Rimuovi 'vuint32 *regs = si->regs;' perché ora lo passiamo come argomento
    uint32 reg;
    
    /* MCLK - Memory Clock */
    reg = SYS_R(MCLK_CTRL);
    dprintf("SM750: MCLK Control Reg: 0x%08" B_PRIx32 "\n", reg);

    /* SCLK - System Clock */
    reg = SYS_R(SCLK_CTRL);
    dprintf("SM750: SCLK Control Reg: 0x%08" B_PRIx32 "\n", reg);
    
    /* Salviamo la frequenza di riferimento */
    si->card_info.f_ref = DEFAULT_INPUT_CLOCK / 1000000.0f; 
}

void sm750_init_chip(DeviceInfo *di) {
	if (!di || !di->regs) {
		dprintf("di or di->regs are null");
		return;
	}
    vuint32 *regs = di->regs;
    shared_info *si = di->si;
    //extern pci_module_info *pci;
    
    uint32 val;

    dprintf("SM750: --- Sveglia Chip ---\n");

    if (regs == NULL) {
        dprintf("SM750 ERROR: regs pointer is NULL! Aborting.\n");
        return;
    }

    // --- 2. POWER & SYSTEM CLOCK ---
    // Impostiamo Full Power (Mode 0)
    uint32 pwrMode = SYS_R(BOOTSTRAP); // 0x00000C
    pwrMode &= ~0x00000003; 
    SYS_W(BOOTSTRAP, pwrMode);

    // Abilitiamo il gate dei clock e il motore 2D
    val = SYS_R(MISC_CTRL); // 0x000004
    val |= 0x00000001;      // Clock selection enable
    val |= (1 << 4);        // 2D Engine enable
    SYS_W(MISC_CTRL, val);
    snooze(2000);

    // --- 3. RESET MOTORI GRAFICI ---
    // Resettiamo il Graphic Engine (GE) per partire da uno stato pulito
    val = SYS_R(MISC_CTRL);
    SYS_W(MISC_CTRL, val | 0x00010000); // Bit 16: GE Reset
    snooze(500);
    SYS_W(MISC_CTRL, val & ~0x00010000);
    snooze(500);

    // --- 4. RILEVAMENTO MEMORIA REALE ---
    // Ora che il chip è sveglio, interroghiamo il controller DRAM
    uint32 dram_ctrl = SYS_R(DRAM_CTRL);
    uint32 mem_size_code = (dram_ctrl >> 13) & 0x07; 
    uint32 detected_mem;

    switch (mem_size_code) {
        case 0: detected_mem = 8 * 1024 * 1024; break;
        case 1: detected_mem = 16 * 1024 * 1024; break;
        case 2: detected_mem = 32 * 1024 * 1024; break;
        case 3: detected_mem = 64 * 1024 * 1024; break;
        default: detected_mem = 2 * 1024 * 1024; break;
    }
    if (detected_mem > si->card_info.mem_size && si->card_info.mem_size != 0) {
        dprintf("SM750: WARNING - Chip says %" B_PRIu32 "MB, but PCI BAR1 is %" B_PRIu32 "MB. Using PCI limit.\n", 
            detected_mem / (1024*1024), si->card_info.mem_size / (1024*1024));
    } else {
        si->card_info.mem_size = detected_mem;
    }

    // --- 5. LOG E DIAGNOSTICA ---
    sm750_get_clocks(di->regs, di->si);
    snooze(1000);
    
    dprintf("SM750: Vendor ID: 0x%04x, Device ID: 0x%04x\n", si->vendor_id, si->device_id);
    
    uint32 mmio_id = SM750_REG32(0x000000);
    dprintf("SM750: ID via MMIO: 0x%08x (Atteso: 0x0750126f)\n", mmio_id);
    
    SM750_REG32(0x000010) = 0xDEADBEEF;
    uint32 test_scratch = SM750_REG32(0x000010);
    dprintf("SM750: Scratch test (0x000010): 0x%08x (atteso 0xdeadbeef)\n", test_scratch);
    
    dprintf("SM750: DRAM_CTRL: 0x%08" B_PRIx32 ", Detected Memory: %" B_PRIu32 " MB\n", 
            dram_ctrl, si->card_info.mem_size / (1024 * 1024));
    dprintf("SM750: Chip initialized and clocked.\n");
}

void 
sm750_pixel_test(shared_info *si)
{
    /* Usiamo il puntatore della shared_info che è già mappato */
    if (si == NULL || si->framebuffer == NULL) {
        dprintf("SM750: Pixel Test abortito, framebuffer non mappato.\n");
        return;
    }

    /* Usiamo volatile per assicurarci che ogni scrittura avvenga davvero sulla scheda */
    volatile uint32 *fb = (volatile uint32 *)si->framebuffer;
    dprintf("SM750: Test singolo pixel...\n");
    fb[0] = 0xFFFFFFFF; // Scrivi un pixel bianco all'inizio
    dprintf("SM750: Pixel scritto con successo!\n");

    //uint32 color = 0x00FF00FF; // Magenta/Fucsia

    dprintf("SM750: Inizio Pixel Test sul BAR1 (Virtual Addr: %p)...\n", (void*)fb);

    /* Scriviamo mezzo milione di pixel. 
       A 32bpp, sono circa 2MB di dati. 
    for (int32 i = 0; i < 500000; i++) {
        fb[i] = color;
        
        // Creiamo delle bande di colore sfumate
        if (i % 2000 == 0) {
            color += 0x00000100; // Cambia leggermente il verde
        }
    }*/
    uint32 max_pixels = si->card_info.mem_size / 4;
    dprintf("SM750: Inizio Pixel Test su %" B_PRIu32 " MB (%u pixel)...\n", 
            si->card_info.mem_size / (1024 * 1024), max_pixels);
    for (uint32 i = 0; i < max_pixels; i++) {
        fb[i] = 0xFF00FF; // Fucsia
    }

    dprintf("SM750: Pixel Test completato.\n");
}
