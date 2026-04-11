/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include <KernelExport.h>
#include <SupportDefs.h>
#include "DriverInterface.h"
#include "sm750_macros.h"

#include <boot_item.h>
#include <frame_buffer_console.h>

extern pci_module_info *pci;

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
    //reg = SYS_R(MCLK_CTRL);
    reg = regs[0x38 >> 2];
    dprintf("SM750: MCLK Control Reg: 0x%08" B_PRIx32 "\n", reg);

    /* SCLK - System Clock */
    //reg = SYS_R(SCLK_CTRL);
    reg = regs[0x3C >> 2];
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
    
    if (regs == NULL) {
        dprintf("SM750 ERROR: regs pointer is NULL! Aborting.\n");
        return;
    }
    
    shared_info *si = di->si;
    
    struct frame_buffer_boot_info* bufferInfo = (struct frame_buffer_boot_info*)get_boot_item(FRAME_BUFFER_BOOT_INFO, NULL);
    
    dprintf("SM750: --- DIAGNOSTICA DI BOOT ---\n");
if (bufferInfo != NULL) {
    // Usiamo %p o cast a uint64 per l'indirizzo fisico
    dprintf("SM750: [BOOT] FB Fisico VESA: 0x%" B_PRIx64 "\n", (uint64)bufferInfo->physical_frame_buffer);
    dprintf("SM750: [BOOT] Risoluzione: %" B_PRId32 "x%" B_PRId32 " (%" B_PRId32 " bpp)\n", 
            bufferInfo->width, bufferInfo->height, bufferInfo->depth);
    dprintf("SM750: [BOOT] Pitch: %" B_PRId32 "\n", bufferInfo->bytes_per_row);
} else {
    dprintf("SM750: [BOOT] Errore: bufferInfo è NULL\n");
}

// Stampa la BAR1 corretta usando la struttura pci_info standard di Haiku
// base_registers[0] è BAR0 (Regs), base_registers[1] è BAR1 (FrameBuffer)
// Usiamo la stessa interfaccia pci che abbiamo usato per il command register
uint32 bar1 = pci->read_pci_config(di->pci.bus, di->pci.device, di->pci.function, PCI_base_registers + 4, 4);
// Nota: PCI_base_registers + 4 perché ogni BAR è 4 byte. BAR0 è +0, BAR1 è +4.

dprintf("SM750: [BOOT] La nostra BAR1 Fisica (da PCI): 0x%08" B_PRIx32 "\n", bar1 & 0xFFFFFFF0);
    
    uint32 val, check;
    dprintf("SM750: --- [DEBUG-VERO] Sveglia Chip (Forza Bruta) ---\n");

    // 1. SBLOCCO POWER GATE (Offset 0x0C)
    // Forziamo Mode 0 (Full Power) e apriamo i gate
    regs[0x0C >> 2] = 0x00000000; 
    snooze(10000);

    // 2. FORZA IL CLOCK DELLA MEMORIA (DRAM Control - Offset 0x10)
    // Senza questo, il BAR1 non scrive nella VRAM fisica.
    val = regs[0x10 >> 2];
    val &= ~(0x7 << 13); // Pulisce i bit 13, 14, 15
    val |= (1 << 13);    // Imposta il codice per 16MB
    val |= (1 << 31); // Enable DRAM controller
    regs[0x10 >> 2] = val;
    si->card_info.mem_size = 16 * 1024 * 1024; // Diciamo a Haiku che sono 16MB
    dprintf("SM750: Forza geometria RAM a 16MB (DRAM_CTRL: 0x%08x)\n", val);
    snooze(10000);

    // 3. ABILITAZIONE CLOCK E DAC (MISC_CTRL - Offset 0x04)
    val = regs[0x04 >> 2];
    val |= (1 << 0) | (1 << 1); // Clocks
    val |= (1 << 2) | (1 << 3); // DAC Enable + Route to Secondary (VGA)
    val |= (1 << 4);            // 2D Engine
    val |= (1 << 7) | (1 << 12);// MMIO Esteso e BAR Force
    regs[0x04 >> 2] = val;
    snooze(5000);

    // 4. USCITA DA MODALITÀ VGA (VGA_CONFIG - Offset 0x88)
    regs[0x88 >> 2] = 0x00000006; // Linear + Graphic Mode
    
    // Spegni il "VGA Decode" nel registro MISC_CTRL (0x04)
    val = regs[0x04 >> 2];
    val |= (1 << 12); // BAR Force (usa BAR1, non indirizzi VGA)
    val &= ~(1 << 5); // Disabilita VGA Buffer (se presente in questo modello)
    regs[0x04 >> 2] = val;
    
    regs[0x80000 >> 2] |= (1 << 8) | (1 << 0); // Primary: VGA Bypass + Enable
    regs[0x80200 >> 2] |= (1 << 8) | (1 << 0); // Secondary: VGA Bypass + Enable
    
    dprintf("SM750: Log Post-Sveglia -> DRAM_CTRL: 0x%08x, VGA_CONF: 0x%08x\n", 
            regs[0x10 >> 2], regs[0x88 >> 2]);

    // --- FORZA PRIORITÀ MEMORIA ---
    // Registro 0x14 (DRAM Priority Control)
    // Diamo la priorità massima al Display Engine (CRT/Panel) rispetto alla CPU
    regs[0x14 >> 2] |= 0x0000000F;

// --- CONFIGURAZIONE PIPE SECONDARIA (VGA 2) ---
    regs[0x80204 >> 2] = 0x00000000; // FB Addr
    uint32 rowWidth = (800 * 4);
    regs[0x80208 >> 2] = (rowWidth << 16) | rowWidth; // Fetch Width

    uint32 pwr = regs[0x80218 >> 2];
    pwr |= (1 << 0) | (1 << 1); // VDE e HDE
    regs[0x80218 >> 2] = pwr; 

    uint32 ctrl = regs[0x80200 >> 2];
    ctrl |= (1 << 31) | (1 << 8) | (1 << 0); // Timing + Bypass + Enable
    regs[0x80200 >> 2] = ctrl;
    
// --- CONFIGURAZIONE PIPE PRIMARIA (VGA 1) ---
    regs[0x80004 >> 2] = 0x00000000; // FB Addr
    regs[0x80008 >> 2] = (rowWidth << 16) | rowWidth; 
    uint32 ctrl1 = regs[0x80000 >> 2];
    ctrl1 |= (1 << 31) | (1 << 8) | (1 << 0);
    regs[0x80000 >> 2] = ctrl1;

    // --- RESET MOTORE GRAFICO ---
    val = regs[0x04 >> 2];
    regs[0x04 >> 2] = val | 0x00010000;
    snooze(2000);
    regs[0x04 >> 2] = val & ~0x00010000;
    snooze(2000);
    
    // --- SBLOCCO ACCESSO MEMORIA (Forza l'interfaccia PCI) ---
    // Registro 0x000004 (System Control)
    // Proviamo ad abilitare esplicitamente il motore 2D e il Master PCI
    val = regs[0x04 >> 2];
    val |= (1 << 12); // Force PCI Master
    val |= (1 << 7);  // MMIO Access Enable
    val |= (1 << 31);
    regs[0x04 >> 2] = val;
    // --------------------------------------------------
    // --- TEST DI "VISIBILITÀ" (Color Fill Hardware) ---
    // Se non vediamo il fucsia scrivendo noi, proviamo a chiedere al chip
    // di colorare lo schermo usando il suo motore interno (se possibile).
    // Ma prima, assicuriamoci che il bit 8 (VGA Bypass) sia attivo ovunque.
    //regs[0x80000 >> 2] |= (1 << 8); // Primary Pipe
    //regs[0x80200 >> 2] |= (1 << 8); // Secondary Pipe
    // --------------------------------------------------

    // --- DIAGNOSTICA FINALE ---
    dprintf("SM750: Revision ID (0x30): 0x%08" B_PRIx32 "\n", regs[0x30 >> 2]);
    dprintf("SM750: ID via MMIO (0x00): 0x%08" B_PRIx32 "\n", regs[0]);

    // Rilevamento memoria reale dal controller DRAM
    uint32 dram_ctrl = regs[0x10 >> 2];
   
    // --- 4. RILEVAMENTO MEMORIA REALE ---
    uint32 mem_size_code = (dram_ctrl >> 13) & 0x07; 
    uint32 detected_mem;

    switch (mem_size_code) {
        case 0: detected_mem = 8 * 1024 * 1024; break;
        case 1: detected_mem = 16 * 1024 * 1024; break;
        case 2: detected_mem = 32 * 1024 * 1024; break;
        case 3: detected_mem = 64 * 1024 * 1024; break;
        default: detected_mem = 2 * 1024 * 1024; break;
    }
    si->card_info.mem_size = (detected_mem > si->card_info.mem_size && si->card_info.mem_size != 0) 
                             ? si->card_info.mem_size : detected_mem;

    // --- 5. LOG E DIAGNOSTICA ---
    sm750_get_clocks(di->regs, di->si);
    dprintf("SM750: ID (0x00): 0x%08x, Rev (0x30): 0x%08x\n", regs[0x00 >> 2], regs[0x30 >> 2]);
    
    SM750_REG32(0x000010) = 0xDEADBEEF;
    uint32 test_scratch = SM750_REG32(0x000010);
    //regs[0x10 >> 2] = 0xDEADBEEF; // Scratch test su 0x10
    //dprintf("SM750: Scratch (0x10): 0x%08x, Mem: %u MB\n", regs[0x10 >> 2], (uint32)(si->card_info.mem_size / (1024*1024)));
    dprintf("SM750: Scratch test (0x000010): 0x%08x (atteso 0xdeadbeef)\n", test_scratch);
    regs[0x10 >> 2] = dram_ctrl;
    dprintf("SM750: DRAM_CTRL: 0x%08" B_PRIx32 ", Detected Memory: %" B_PRIu32 " MB\n", 
            dram_ctrl, si->card_info.mem_size / (1024 * 1024));
    dprintf("SM750: Chip initialized and clocked.\n");
    // Tentativo di forzare un colore solido tramite il registro di sfondo (0x80264)
    // Se i motori sono accesi, questo dovrebbe colorare lo schermo indipendentemente dal FB
    //regs[0x80264 >> 2] = 0x00FF00FF; // Colore fucsia nel registro di "Back Ground"
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
