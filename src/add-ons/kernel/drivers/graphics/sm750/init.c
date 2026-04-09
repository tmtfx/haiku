/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include <KernelExport.h>
#include <SupportDefs.h>
#include "DriverInterface.h"
#include "sm750_macros.h"

void sm750_get_clocks(shared_info *si);
void sm750_pixel_test(shared_info *si);

/* Frequenza del cristallo di riferimento (Standard SM750) */
#define DEFAULT_INPUT_CLOCK 24000000

/* --- sm750_get_clocks --- */
/* Legge lo stato attuale dei PLL impostati dal BIOS/Bootloader */
void 
sm750_get_clocks(shared_info *si)
{
	vuint32 *regs = si->regs;
	uint32 reg;
	
	/* MCLK - Memory Clock (Registro 0x00000048) */
	reg = SYS_R(MCLK_CTRL);
	// Qui andrebbe la formula: (Input * M) / (N * D)
	// Per ora leggiamo il valore grezzo per il debug
	dprintf("SM750: MCLK Control Reg: 0x%08" B_PRIx32 "\n", reg);

	/* SCLK - System Clock (Registro 0x0000004C) */
	reg = SYS_R(SCLK_CTRL);
	dprintf("SM750: SCLK Control Reg: 0x%08" B_PRIx32 "\n", reg);
	
	/* Salviamo la frequenza di riferimento nella shared_info */
	si->card_info.f_ref = DEFAULT_INPUT_CLOCK / 1000000.0f; // 24.0 MHz
}

void sm750_init_chip(shared_info *si) {
	vuint32 *regs = (vuint32 *)si->regs;
	dprintf("SM750: --- Sveglia Chip ---\n");

    /* 1. Forza la Power Mode 0 (Full Power) */
    /* Il registro 0x00000C gestisce il risparmio energetico */
    uint32 pwrMode = SYS_R(BOOTSTRAP);//0x00000C
    pwrMode &= ~0x00000003; // Pulisce i bit di modalità (00 = Mode 0)
    SYS_W(BOOTSTRAP, pwrMode);//0x00000C
    
    /* 2. Abilita i Clock (Gate Control) */
    /* Molti SM750 richiedono che i moduli siano abilitati esplicitamente */
    /* Registro 0x000004: System Control */
    uint32 sysCtrl = SYS_R(MISC_CTRL);//0x000004
    sysCtrl |= (1 << 4); // Spesso usato per abilitare il motore 2D
    SYS_W(MISC_CTRL, sysCtrl);//0x000004

    /* 3. Un piccolo trucco: Reset del motore grafico */
    SM750_REG32(0x00) = SM750_REG32(0x00) | 0x00010000;
    snooze(1000);
    SM750_REG32(0x00) = SM750_REG32(0x00) & ~0x00010000;
    
    dprintf("SM750: Identificazione hardware (0x08): 0x%08x\n", SM750_REG32(0x08));
    
    dprintf("SM750: Power Mode impostata. Controllo clock...\n");
	
	if (regs == NULL) {
        dprintf("SM750 ERROR: regs pointer is NULL! Aborting init_chip.\n");
        return;
    }
	dprintf("SM750: --- Initializing Hardware ---\n");
	dprintf("SM750: Testing MMIO access at %p...\n", regs);

    /* Invece di usare subito la macro complessa, facciamo un test manuale */
    /* Leggiamo l'offset 0 direttamente */
    uint32 test = regs[0]; 
    dprintf("SM750: Manual read offset 0: 0x%08x\n", test);

    /* Se arriviamo qui, le macro SYS_R sono sicure da usare */
    uint32 misc = SYS_R(MISC_CTRL); 
    dprintf("SM750: MISC_CTRL via macro: 0x%08x\n", misc);
    
    dprintf("SM750: Vendor ID: 0x%04x, Device ID: 0x%04x\n", si->vendor_id, si->device_id);
    
    uint32 val;

    /* Sblocco dei registri (SMI specific) */
    /* Alcuni chip SM750 hanno un sistema di protezione. 
       Assicuriamoci che il modulo System sia pronto. */
    
    /* 1. Sblocco del System Control */
	/* Il registro 0x00000000 (MISC_CTRL) controlla il reset dei moduli */
	val = SYS_R(MISC_CTRL);
	
	/* Abilitazione del modulo di selezione clock e il motore 2D */
	/* Bit 0: Spesso usato per abilitare il gate del clock principale */
	/* Verificare se è Bit 0!!!!! */
    val |= 0x00000001;
    SYS_W(MISC_CTRL, val);

    /* 2. Power Mode Configuration */
	/* SM750 ha diverse Power Mode (0, 1, Sleep). 
	   Impostiamo la Power Mode 0 (Full Power) */
	// Registro 0x0000000C: Power Mode Control
	//SYS_W(0x0000000C, 0x00000000);
	SYS_W(BOOTSTRAP, 0x00000000);
	//SM750_REG32(0x0000000C) = 0x00000000;

    /* 3. Reset dei motori grafici (Senza toccare il display)
	 * Bit 16: GE (Graphic Engine) Reset
	 * Bit 17: CSC (Color Space Conversion) Reset 
	 */
    val = SYS_R(MISC_CTRL); 
    SYS_W(MISC_CTRL, val | 0x00010000); // Set reset bit SYS_W(MISC_CTRL, val | (1 << 16));
    snooze(500);                       // Attesa 1ms
    SYS_W(MISC_CTRL, val & ~0x00010000); // Clear reset bit SYS_W(MISC_CTRL, val & ~(1 << 16));
    
    /* 4. Lettura clock attuali per conferma */
	sm750_get_clocks(si);
	//dprintf("SM750: Mapped %" B_PRIu32 " MB of VRAM at %p\n", si->card_info.mem_size / (1024 * 1024), si->framebuffer);
    dprintf("SM750: Mapped %" B_PRIu32 " MB of VRAM\n", si->card_info.mem_size / (1024 * 1024));
	dprintf("SM750: Chip initialized and clocked.\n");

//    return B_OK;
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
