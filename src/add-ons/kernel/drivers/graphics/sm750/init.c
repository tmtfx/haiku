/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include "DriverInterface.h"
#include "macros.h"

/* Frequenza del cristallo di riferimento (Standard SM750) */
#define DEFAULT_INPUT_CLOCK 24000000

/* --- sm750_get_clocks --- */
/* Legge lo stato attuale dei PLL impostati dal BIOS/Bootloader */
void 
sm750_get_clocks(shared_info *si)
{
	uint32 reg;
	
	/* MCLK - Memory Clock (Registro 0x00000048) */
	reg = SYS_R(SM750_SYS_MCLK_CTRL);
	// Qui andrebbe la formula: (Input * M) / (N * D)
	// Per ora leggiamo il valore grezzo per il debug
	dprintf("SM750: MCLK Control Reg: 0x%08" B_PRIx32 "\n", reg);

	/* SCLK - System Clock (Registro 0x0000004C) */
	reg = SYS_R(SM750_SYS_SCLK_CTRL);
	dprintf("SM750: SCLK Control Reg: 0x%08" B_PRIx32 "\n", reg);
	
	/* Salviamo la frequenza di riferimento nella shared_info */
	si->si.f_ref = DEFAULT_INPUT_CLOCK / 1000000.0f; // 24.0 MHz
}

void 
sm750_pixel_test(shared_info *si)
{
	if (!si->framebuffer) return;

	/* Supponiamo di essere in una modalità a 32 bit (4 byte per pixel) 
	   e una larghezza di almeno 800 pixel. 
	   Scriviamo i primi 100.000 pixel per vedere se appare qualcosa. */
	
	uint32 *fb = (uint32 *)si->framebuffer;
	uint32 color = 0x00FF00FF; // Un bel fucsia/magenta acceso

	dprintf("SM750: Inizio Pixel Test sul BAR1 (Virtual Addr: %p)...\n", fb);

	for (int i = 0; i < 500000; i++) {
		fb[i] = color;
		/* Cambiamo colore ogni tanto per creare delle strisce */
		if (i % 1000 == 0) color += 0x00010100; 
	}

	dprintf("SM750: Pixel Test completato.\n");
}

status_t sm750_init_chip(shared_info *si) {
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
	SYS_W(0x0000000C, 0x00000000);

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
	
	dprintf("SM750: Chip initialized and clocked.\n");
	
	sm750_pixel_test(si);

    return B_OK;
}
