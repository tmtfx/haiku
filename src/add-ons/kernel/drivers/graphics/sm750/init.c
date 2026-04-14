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
	/* vecchio
    // Offset corretti dal datasheet pag. 38
    uint32 sclk_reg = regs[0x44 >> 2]; // System PLL ma è questo? SM750_SYS_PWR_MODE_0_CLKC
    uint32 mclk_reg = regs[0x48 >> 2]; // Master (Memory) PLL ma è questo? SM750_SYS_PWR_MODE_1_CLKC
    uint32 vclk_reg = regs[0x4C >> 2]; // Display PLL 
    
    dprintf("SM750: SCLK Reg (0x44): 0x%08" B_PRIx32 "\n", sclk_reg);
    dprintf("SM750: MCLK Reg (0x48): 0x%08" B_PRIx32 "\n", mclk_reg);
    dprintf("SM750: VCLK Reg (0x4C): 0x%08" B_PRIx32 "\n", vclk_reg);
    */
    // 1. Configurazione del selettore Power Mode
    uint32 pwrCtrl = SM750_REG32(SM750_SYS_PWR_MODE_CTRL);
    pwrCtrl &= ~0x00000003; // Forza i bit 1:0 a 00 (Power Mode 0)
    // Assicuriamoci anche che l'oscillatore sia attivo (Bit 3)
    pwrCtrl |= (1 << 3); 
    SM750_WREG32(SM750_SYS_PWR_MODE_CTRL, pwrCtrl);
    snooze(500);
    uint32 clkStatus = SM750_REG32(SM750_SYS_CUR_CLK_STATUS);
    dprintf("SM750: Clock Status iniziale: 0x%08" B_PRIx32 "\n", clkStatus);

    if (!(clkStatus & (1 << 8))) {
    	// --- 1. SETTAGGIO POWER MODE 0 ---
        dprintf("SM750: I2C Clock spento! Lo abilito nei Power Mode 0 e 1...\n");
        uint32 mode0 = SM750_REG32(SM750_SYS_PWR_MODE_0_CLKC);
        mode0 |= (1 << 8); // I2C Clock
        mode0 |= (1 << 6); // GPIO Clock
        SM750_WREG32(SM750_SYS_PWR_MODE_0_CLKC, mode0);

        // --- 2. SETTAGGIO POWER MODE 1 ---
        uint32 mode1 = SM750_REG32(SM750_SYS_PWR_MODE_1_CLKC);
        mode1 |= (1 << 8); // I2C Clock (Sempre acceso anche qui!)
        mode1 |= (1 << 6); // GPIO Clock
        SM750_WREG32(SM750_SYS_PWR_MODE_1_CLKC, mode1);
    }
    snooze(1000);

    clkStatus = SM750_REG32(SM750_SYS_CUR_CLK_STATUS);
    dprintf("SM750: Verifica valore Clock Status finale: 0x%08" B_PRIx32 "\n", clkStatus);

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
    uint32 device_info = SM750_REG32(SM750_SYS_DEVID); // 0x000054
    uint16 device_id = (uint16)(device_info >> 16);
    uint8 revision = (uint8)(device_info & 0xFF);
    dprintf("SM750: Chip rilevato. ID: 0x%04X, Revisione: 0x%02X\n", device_id, revision);
    
    // --- 1. SVEGLIA IL CHIP (Power Mode 0) ---
    // --- SBLOCCO CLOCK (Power Mode 0) ---
    uint32 mode0_gate = SM750_REG32(SM750_SYS_PWR_MODE_0_CLKC); // 0x000044
    // Abilitiamo I2C (8), GPIO (6), 2D (3), Display (2), Memory (1) e DMA (0)
    mode0_gate |= (1 << 8) | (1 << 6) | (1 << 3) | (1 << 2) | (1 << 1) | (1 << 0);
    // Assicuriamoci che il VGA Clock (10) sia attivo se usiamo il CRT
    mode0_gate |= (1 << 10); 
    SM750_WREG32(SM750_SYS_PWR_MODE_0_CLKC, mode0_gate); // 0x000044
    dprintf("SM750: Power Mode 0 Clock Gate impostato: 0x%08x\n", mode0_gate);

    // --- 2. SELEZIONA IL POWER MODE E ACCENDI L'OSCILLATORE ---
    //Power Mode Control
    //Read/Write  MMIO_base + 0x00004C
    uint32 pwr_ctrl = SM750_REG32(SM750_SYS_PWR_MODE_CTRL); // 0x00004C
    pwr_ctrl &= ~0x00000003; // Forza Mode 0 (bit 1:0 = 00)
    pwr_ctrl |= (1 << 3);    // Assicurati che l'oscillatore sia acceso
    SM750_WREG32(SM750_SYS_PWR_MODE_CTRL, pwr_ctrl); // 0x00004C
    snooze(1000); // Diamogli un millisecondo per stabilizzare i clock
    
    // --- 2. ABILITAZIONE BUS E MEMORIA (Registro 0x00) ---
    uint32 sys_ctrl = SM750_REG32(SM750_SYS_CTRL); // 0x00
    
    // Togliamo l'isolamento (bit 0,1,2,3) per collegare RAM e uscite video
    sys_ctrl &= ~0x0000000F; 
    
    // Assicuriamoci che i sincronismi siano attivi (DPMS on, bit 31:30 = 00)
    sys_ctrl &= ~(3U << 30);
    
    SM750_WREG32(SM750_SYS_CTRL, sys_ctrl); //0x000000
    dprintf("SM750: System Control (0x00) configurato: 0x%08x\n", sys_ctrl);
    
    // --- 3. ORA FACCIAMO LA RILEVAZIONE (Dopo aver attivato i bus) ---
    // Usiamo la tua logica basata sul bit 3 (CRT is Normal)
    if (!(sys_ctrl & (1 << 3))) { 
        si->card_info.is_panel = false;
        si->card_info.active_outputs = 2; // CRT
        dprintf("SM750: CRT rilevato come attivo.\n");
    } else {
        si->card_info.is_panel = true;
        si->card_info.active_outputs = 1; // Panel
        dprintf("SM750: Panel rilevato come attivo.\n");
    }
    

    uint32 misc_ctrl = SM750_REG32(SM750_SYS_MISC_CTRL); // 0x000004
    // Assicuriamoci che:
    // 1. Il bit 6 (Rst) sia a 1 (Normal Operation)
    // 2. Il bit 0 (E) sia a 0 (Enable local memory)
    misc_ctrl |= (1 << 6);  // Forza Normal Mode
    misc_ctrl &= ~(1 << 0); // Forza Enable Memory
    SM750_WREG32(SM750_SYS_MISC_CTRL, misc_ctrl); // 0x000004
    dprintf("SM750: Miscellaneous Control (0x04) stabilizzato a: 0x%08x\n", misc_ctrl);
    
    
    // C. Rilevazione Memoria (LOGICA UNICA)
    uint32 mem_size_code = (misc_ctrl >> 12) & 0x03; // Bit 13:12
    uint32 detected_mem;

    switch (mem_size_code) {
        case 0: detected_mem = 16 * 1024 * 1024; break;
        case 1: detected_mem = 32 * 1024 * 1024; break;
        case 2: detected_mem = 64 * 1024 * 1024; break;
        case 3: detected_mem = 8  * 1024 * 1024; break;
        default: detected_mem = 8 * 1024 * 1024; break;
    }

    // Aggiorniamo la shared info
    si->card_info.mem_size = detected_mem;
    dprintf("SM750: Memoria VRAM rilevata (da 0x04): %u MB\n", detected_mem / (1024*1024));
    
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
    
    si->card_info.chip_id = di->pci.device_id;
    si->card_info.f_ref = 24.0f;
    si->card_info.max_sclk = 130000; // Valori tipici SM750 (130MHz)
    si->card_info.max_mclk = 150000; // 150MHz
    si->card_info.max_pclk = 300000; // 300MHz (Limite DAC)
    
    // F. VGA BYPASS (Necessario per usare il Framebuffer lineare in modo nativo)
    // Abilitiamo il bypass VGA su entrambe le pipe (Primary e Secondary)
    uint32 display_reg = si->card_info.is_panel ? SM750_PANEL_CONTROL : SM750_CRT_CONTROL; // 0x080000 : 0x080200
    uint32 ctrl = SM750_REG32(display_reg);

    // 1. Formato 32-bit (Bit 1:0 = 10) - Uguale per entrambi
    ctrl &= ~0x00000003;
    ctrl |= 0x00000002;

    // 2. Abilitazione Piano e Timing (Bit 2 e 8) - Uguale per entrambi
    ctrl |= (1 << 2) | (1 << 8);

    // 3. Selezione Sorgente Dati (QUI CAMBIA!)
    if (si->card_info.is_panel) {
        // Registro 0x80000: Bit 29:28. Vogliamo "Panel Data" (00)
        ctrl &= ~(3U << 28);
    
        // Extra per LCD: Abilitiamo i segnali di controllo (Bit 27, 26, 25, 24)
        ctrl |= (1 << 27) | (1 << 26) | (1 << 25) | (1 << 24);
    } else {
        // Registro 0x80200: Bit 19:18. Vogliamo "CRT Data" (10)
        ctrl &= ~(3U << 18);
        ctrl |= (2U << 18);
    
        // No Blank per CRT (Bit 10)
        ctrl &= ~(1 << 10);
    }
    SM750_WREG32(display_reg, ctrl);
    

    dprintf("SM750: Init  completato. Mem: %d MB, Mode: %s\n", 
            detected_mem / (1024*1024), si->card_info.is_panel ? "PANEL" : "CRT");
    
    // --- CONFIGURAZIONE GPIO PER EDID/I2C ---
    // 1. Leggiamo il registro corretto: 0x08
    uint32 gpio_ctrl = SM750_REG32(SM750_SYS_GPIO_CTRL); //0x000008
    // 2. Impostiamo i pin 30 e 31 come I2C (funzione speciale)
    // Invece di azzerarli, li mettiamo a 1.
    gpio_ctrl |= (1 << 31); // Pin 31 = I2C Data
    gpio_ctrl |= (1 << 30); // Pin 30 = I2C Clock
    // 3. Scriviamo il risultato per abilitare i2c
    SM750_WREG32(SM750_SYS_GPIO_CTRL, gpio_ctrl); //0x000008
    dprintf("SM750: GPIO Control (0x08) impostato per I2C: 0x%08x\n", gpio_ctrl);
   
   
    uint32 detect = SM750_REG32(SM750_CRT_MONITOR_DETECT);
    dprintf("SM750: Monitor Detect Status: 0x%08" B_PRIx32 "\n", detect);
    
    // 7. DIAGNOSTICA FINALE MMIO
    sm750_get_clocks(regs, si);
    
    // SPLASH SCREEN che non funziona per ora
    uint32* kfb = (uint32*)di->framebuffer;
    uint32 size = detected_mem / 4; // in pixel a 32bpp
    // Riempimento brutale ma efficace - metteremo logo se possibile
    for (uint32 i = 0; i < size; i++) {
        kfb[i] = 0xFFFF0000;
    }
    snooze(1500000); // 1.5 secondi di gloria

    // 1. DIAGNOSTICA PCI/BOOT (Solo log)
    struct frame_buffer_boot_info* bi = (struct frame_buffer_boot_info*)get_boot_item(FRAME_BUFFER_BOOT_INFO, NULL);
    if (bi) {
        dprintf("SM750: VESA FB a 0x%" B_PRIx64 ", %" B_PRId32 "x%" B_PRId32 "\n", 
                (uint64)bi->physical_frame_buffer, bi->width, bi->height);
    }

    //dprintf("SM750: Inizializzazione completata.\n");
}
