/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include <KernelExport.h>
#include <PCI.h>
#include <string.h>
#include <SupportDefs.h>
#include <boot_item.h>

#include <frame_buffer_console.h>
#include <stdlib.h>

#include "DriverInterface.h"
#include "sm750_macros.h"
#include "common_modes.h"
#include "sm750_logo.h"

extern pci_module_info *pci;

#define VESA_EDID_BOOT_INFO "vesa_edid/v1"
#define VESA_MODES_BOOT_INFO "vesa_modes/v1"


typedef struct BIOSState bios_state;

struct bios_regs {
    uint32 eax; uint32 ebx; uint32 ecx; uint32 edx;
    uint32 edi; uint32 esi; uint32 ebp; uint32 eflags;
    uint32 ds;  uint32 es;  uint32 fs;  uint32 gs;
};
typedef struct bios_regs bios_regs;

typedef struct {
    module_info info;
    status_t  (*prepare)(bios_state** _state);
    status_t  (*interrupt)(bios_state* state, uint8 vector, bios_regs* regs);
    void      (*finish)(bios_state* state);
    void* (*allocate_mem)(bios_state* state, size_t size);
    uint32    (*physical_address)(bios_state* state, void* virtualAddress);
    void* (*virtual_address)(bios_state* state, uint32 physicalAddress);
} sm750_bios_module_info; 

#define B_BIOS_MODULE_NAME "generic/bios/v1"

static status_t
GetEdidFromBIOS(edid1_raw* edidRaw)
{
    sm750_bios_module_info* biosModule;
    status_t status = get_module(B_BIOS_MODULE_NAME, (module_info**)&biosModule);
    if (status != B_OK) {
        dprintf("SM750: Unable loading the BIOS module: 0x%" B_PRIx32 "\n", status);
        return status;
    }

    bios_state* state;
    status = biosModule->prepare(&state);
    if (status != B_OK) {
        dprintf("SM750: bios_prepare() failed\n");
        put_module(B_BIOS_MODULE_NAME);
        return status;
    }

    bios_regs regs = {};
    regs.eax = 0x4f15; // VBE Function: Report DDC Capabilities
    regs.ebx = 0;
    dprintf("SM750: Check DDC Capabilities EAX=0x%x, EBX=0x%x\n", regs.eax, regs.ebx);
    
    status = biosModule->interrupt(state, 0x10, &regs);
    
    // Verifichiamo se il BIOS supporta la funzione (0x4f = Successo)
    if (status == B_OK && (regs.eax & 0xffff) == 0x4f) {
        edid1_raw* edid = (edid1_raw*)biosModule->allocate_mem(state, sizeof(edid1_raw));
        if (edid == NULL) {
            status = B_NO_MEMORY;
        } else {
            regs.eax = 0x4f15;
            regs.ebx = 1;  // Sottofunzione: Read EDID
            regs.ecx = 0;
            regs.edx = 0;
            regs.es  = (uint16)((addr_t)edid >> 4);
            regs.edi = (uint16)((addr_t)edid & 0x0f);

            status = biosModule->interrupt(state, 0x10, &regs);
            
            if (status == B_OK && (regs.eax & 0xffff) == 0x4f) {
                memcpy(edidRaw, edid, sizeof(edid1_raw));
                dprintf("SM750: EDID correctly read through BIOS interrupt!\n");
            } else {
                dprintf("SM750: BIOS failed reading the EDID (EAX=0x%x)\n", regs.eax);
                status = B_NOT_SUPPORTED;
            }
        }
    } else {
        dprintf("SM750: DDC not supported by Video BIOS\n");
        status = B_NOT_SUPPORTED;
    }

    biosModule->finish(state);
    put_module(B_BIOS_MODULE_NAME);
    return status;
}

static const color_space kFallbackSpaces[] = {
    B_RGB32,
    B_RGB16,
    B_CMAP8
};

static status_t create_mode_list(shared_info* si) {
    uint32 vesa_count = 0;
    while (vesa_dmt_table[vesa_count].width != 0) {
        vesa_count++;
    }
    uint32 total_modes = vesa_count * 3;
    
    size_t size = (total_modes * sizeof(display_mode) + B_PAGE_SIZE - 1) & ~(B_PAGE_SIZE - 1);
    
    display_mode* kernel_list = (display_mode*)malloc(size);
    if (kernel_list == NULL) return B_NO_MEMORY;
    memset(kernel_list, 0, size);

    uint32 count = 0;

    for (int i = 0; vesa_dmt_table[i].width != 0; i++) {
        const vesa_timing_t* vesa = &vesa_dmt_table[i];
        
        for (int s = 0; s < 3; s++) {
            display_mode* dm = &kernel_list[count];
            
            dm->timing.pixel_clock = vesa->pixel_clock;
            dm->timing.h_display    = vesa->width;
            dm->timing.h_sync_start = vesa->h_sync_start;
            dm->timing.h_sync_end   = vesa->h_sync_end;
            dm->timing.h_total      = vesa->h_total;
            dm->timing.v_display    = vesa->height;
            dm->timing.v_sync_start = vesa->v_sync_start;
            dm->timing.v_sync_end   = vesa->v_sync_end;
            dm->timing.v_total      = vesa->v_total;
            dm->timing.flags        = vesa->flags;

            dm->space = kFallbackSpaces[s];
            dm->virtual_width  = vesa->width;
            dm->virtual_height = vesa->height;
            dm->h_display_start = 0;
            dm->v_display_start = 0;
            dm->flags = 0;

            count++;
        }
    }

    display_mode* user_list = NULL;
    area_id m_area = create_area("sm750 modes", (void **)&user_list,
        B_ANY_KERNEL_ADDRESS, size, B_FULL_LOCK, 
        B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA | B_READ_AREA | B_WRITE_AREA | B_CLONEABLE_AREA);

    if (m_area < 0) {
        free(kernel_list);
        return m_area;
    }

    if (user_memcpy(user_list, kernel_list, size) != B_OK) {
        dprintf("SM750: user_memcpy failed in create_mode_list!\n");
    }

    free(kernel_list);

    si->mode_count = count;
    si->mode_list_area = m_area;

    dprintf("SM750: create_mode_list completed. # of Modes: %u\n", count);
    return B_OK;
}

static void draw_logo(DeviceInfo *di, display_mode* dm) {
    if (!di || !dm) return;

    void* fb_virt = NULL;
    area_id fb_area = -1;
    uint32 fb_size = di->pci.u.h0.base_register_sizes[0];
    if (fb_size == 0)
        return;

    fb_size = (fb_size + B_PAGE_SIZE - 1) & ~(B_PAGE_SIZE - 1);
    fb_area = map_physical_memory("sm750_fb_init_logo",
        di->pci.u.h0.base_registers[0], fb_size, B_ANY_KERNEL_ADDRESS,
        B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, &fb_virt);
    if (fb_area < B_OK || fb_virt == NULL)
        return;

    shared_info *si = di->si;
    uint32* fb = (uint32*)fb_virt;
    
    uint32 screenWidth = dm->virtual_width;
    uint32 screenHeight = dm->virtual_height;
    
    uint32 bytesPerRow = si->fbc.bytes_per_row;
    if (bytesPerRow == 0)
        bytesPerRow = screenWidth * 4; 

    uint32 fbPitch = bytesPerRow / 4; 

    uint32 logoW = 640;
    uint32 logoH = 183;

    int32 startX = (int32)((screenWidth - logoW) / 2);
    int32 startY = (int32)((screenHeight - logoH) / 2);

    if (startX < 0) startX = 0;
    if (startY < 0) startY = 0;

    for (uint32 y = 0; y < logoH && (startY + (int32)y) < (int32)screenHeight; y++) {
        for (uint32 x = 0; x < logoW && (startX + (int32)x) < (int32)screenWidth; x++) {
            uint32 fbIndex = (uint32)((startY + (int32)y) * (int32)fbPitch + (startX + (int32)x));
            fb[fbIndex] = sm750_logo[y * logoW + x];
        }
    }

    delete_area(fb_area);
}

static void sm750_init_interrupts(DeviceInfo* info) 
{
	vuint32* regs = info->regs;

	// 1. PULIZIA MASTER: 
	// Scriviamo 1 su tutti i bit del registro Clear (0x20)
	// 31:5 Reserved, 4 ZV1 Port, 3 ZV0 Port, 2 CRT VSYNC, 1 Panel VSYNC, 0 VGA VSYNC
	SM750_WREG32(SM750_SYS_RAW_INT_CLEAR, 0x0000001F);

	// 2. PULIZIA 2D ENGINE (0x100050)
	// Datasheet dice: "Write 0 to clear"
	uint32 engineStatus = SM750_REG32(SM750_2D_STATUS);
	engineStatus &= ~0x00000003; // Azzera bit 0 (2D) e 1 (CSC)
	SM750_WREG32(SM750_2D_STATUS, engineStatus);

	// 3. PULIZIA DMA (0x0D0020)
	uint32 dmaStatus = SM750_REG32(SM750_DMA_ABORT_INTERRUPT);
	SM750_WREG32(SM750_DMA_ABORT_INTERRUPT, dmaStatus); 

    // 4. PULIZIA V-SYNC (Panel e CRT)
    // Dobbiamo cercare se ci sono bit di "Clear" nei registri 0x080000 (Panel)
    // e 0x080200 (CRT). Se non li troviamo, il Clear al punto 1 potrebbe bastare.
    // NO il bit (11) del vertical-sync di CRT 80200 è in sola lettura
    uint32 panelStatus = SM750_REG32(SM750_PANEL_CONTROL);
    panelStatus &= ~(1 << 11); // 0 a VSYNC
    //panelStatus |= (1 << 11); // 1 a VSYNC
    panelStatus |= 0x0000006; // abilita primary graphics plane e formato 32bit
    SM750_WREG32(SM750_PANEL_CONTROL, panelStatus); 

	// 5. PULIZIA PWM
	// bit 3 PWM Interrupt Pending. In order to clear a pending interrupt, write a “1” in
	// the IP bit.

	uint32 pwmstat = SM750_REG32(SM750_PWM0);
	SM750_WREG32(SM750_PWM0,pwmstat|(1<<3));
	pwmstat = SM750_REG32(SM750_PWM1);
	SM750_WREG32(SM750_PWM1,pwmstat|(1<<3));
	pwmstat = SM750_REG32(SM750_PWM2);
	SM750_WREG32(SM750_PWM2,pwmstat|(1<<3));
	
	// TENTATIVO scrittura maschera anche se da datasheet è solo read only!
	SM750_WREG32(SM750_SYS_INT_MASK,0xFE0013FF);
	// ma a quanto pare accetta i valori e abilita gli interrupt! <------ !!!
	
	info->si->irq_enabled = 1;
}

/* --- sm750_init_chip --- */
void sm750_init_chip(DeviceInfo *di) {
    if (!di || !di->regs) return;

    vuint32 *regs = di->regs;
    shared_info *si = di->si;
    uint32 device_info = SM750_REG32(SM750_SYS_DEVID);
    si->card_info.chip_id = (uint16)(device_info >> 16);
    uint16 device_id = (uint16)(device_info >> 16);
    uint8 revision = (uint8)(device_info & 0xFF);
    dprintf("SM750: Chip detected. ID: 0x%04X, Revision: 0x%02X\n", device_id, revision);
    
    sm750_init_interrupts(di);
    
    edid1_raw local_edid;
    edid1_raw* boot_edid = (edid1_raw*)get_boot_item(VESA_EDID_BOOT_INFO, NULL);

    if (boot_edid != NULL) {
        memcpy(&si->vesa_edid_raw, boot_edid, sizeof(edid1_raw));
        si->card_info.has_edid_vesa = true;
    } else {
        if (GetEdidFromBIOS(&local_edid) == B_OK) {
        	memcpy(&si->vesa_edid_raw, &local_edid, sizeof(edid1_raw));
            si->card_info.has_edid_vesa = true;
        } else {
        	si->card_info.has_edid_vesa = false;
        }
    }
    
    if (si->settings.force_CRT) {
        si->card_info.is_panel = false;
        si->card_info.active_outputs = 2; // Forza CRT
        dprintf("SM750: User override enabled! Forcing CRT branch (Analog).\n");
    } 
    else if (si->settings.force_Panel) {
        si->card_info.is_panel = true;
        si->card_info.active_outputs = 1; // Forza PANEL
        dprintf("SM750: User override enabled! Forcing PANEL branch (Digital).\n");
    } 
    else if (si->card_info.has_edid_vesa) {
        si->card_info.is_panel = true;
        si->card_info.active_outputs = 1;
        dprintf("SM750: EDID detected -> Using PANEL branch.\n");
    } 
    else {
        // Fallback: niente file, niente EDID. 
        // Imposta il default sulla base della tua scheda CRT.
        si->card_info.is_panel = false; 
        si->card_info.active_outputs = 2;
        dprintf("SM750: EDID not detected -> Using CRT branch.\n");
    }
    
    // --- SVEGLIA IL CHIP (Power Mode 0) ---
    // --- SBLOCCO CLOCK (Power Mode 0) ---
    uint32 mode0_gate = SM750_REG32(SM750_SYS_PWR_MODE_0_CLKC); // 0x000044
    // Abilitiamo GPIO (6), 2D (3), Display (2), Memory (1) e DMA (0)
    mode0_gate |= (1 << 6) | (1 << 4) | (1 << 3) | (1 << 2) | (1 << 1) | (1 << 0); //attiviamo clock DMA, Local memory, Display , 2D, CSC, GPIO, I2C
    //mode0_gate &= ~(1 << 6); // Forza a 0 il clock GPIO visto che non riusciamo a usare il registro di direzione
    //mode0_gate &= ~(1 << 8); // Forza a 0 il clock I2C hardware visto che è rotto
    //mode0_gate |= (1 << 10); // Assicuriamoci che il VGA Clock (10) sia attivo se usiamo il CRT
    mode0_gate &= ~(1 << 5); // Disattiviamo ZV
    mode0_gate &= ~(1 << 7); // Disattiviamo SSP
    mode0_gate &= ~(1 << 9); // Disattiviamo PWM
    //mode0_gate &= ~(1 << 10); // Disattiviamo VGA
    //mode0_gate |= (1 << 10); // Attiviamo VGA e vediamo se la generazione degli interrupt dipende da questo
    if (si->card_info.is_panel) {
        mode0_gate &= ~(1 << 10); // Spegniamo il clock VGA se siamo su PANEL
        mode0_gate |= (1 << 8);   // Accendiamo I2C/DDC per il Panel
    } else {
        mode0_gate |= (1 << 10);  // FONDAMENTALE: Accendiamo il clock VGA per il CRT!
        mode0_gate &= ~(1 << 8);  // Spegniamo I2C hardware se non serve
    }
    SM750_WREG32(SM750_SYS_PWR_MODE_0_CLKC, mode0_gate); // 0x000044
    //dprintf("SM750: Power Mode 0 Clock Gate set to: 0x%08x\n", mode0_gate);
    
    uint32 vga_mode = SM750_REG32(SM750_SYS_VGA_CONFIG);
    vga_mode |= (1 << 2); //selettore CLOCK da vga a primary panel
    SM750_WREG32(SM750_SYS_VGA_CONFIG, vga_mode);
    snooze(100);
    vga_mode = SM750_REG32(SM750_SYS_VGA_CONFIG);

    // --- SELEZIONA IL POWER MODE E ACCENDI L'OSCILLATORE ---
    //Power Mode Control
    uint32 pwr_ctrl = SM750_REG32(SM750_SYS_PWR_MODE_CTRL); // 0x00004C
    pwr_ctrl &= ~0x00000003; // Forza Mode 0 (bit 1:0 = 00)
    pwr_ctrl |= (1 << 3);    // Assicurati che l'oscillatore sia acceso
    SM750_WREG32(SM750_SYS_PWR_MODE_CTRL, pwr_ctrl); // 0x00004C
    snooze(1000); // Stabilizzazione clock
    
    // --- ABILITAZIONE BUS E MEMORIA (Registro 0x00) ---
    uint32 sys_ctrl = SM750_REG32(SM750_SYS_CTRL); // 0x00
    // Togliamo l'isolamento (bit 0,1,2,3) per collegare RAM e uscite video
    sys_ctrl &= ~0x0000000F; 
    // Assicuriamoci che i sincronismi siano attivi (DPMS on, bit 31:30 = 00)
    sys_ctrl &= ~(3U << 30);
    SM750_WREG32(SM750_SYS_CTRL, sys_ctrl); //0x000000
    
    // --- ORA FACCIAMO LA RILEVAZIONE (Dopo aver attivato i bus) ---
    // Sono stati effettuati vari tentativi al fine di non usare la logica di rilevamento 
    // usata poco fa, leggendo SM750_SYS_CTRL con if (!(sys_ctrl & (1 << 3))) viene indicato 
    // crt anche se la scheda dispone di HDMI,
    // nemmeno leggendo SM750_CRT_MONITOR_DETECT abbiamo un valore affidabile, rileva sulla 
    // porta0 un monitor anche se non abilitato, in pratica con 2 suoi bit si rileva la presenza
    // del monitor (25 e 27) e con altri 2 bit li si abilita (24 e 26). Di default con hdmi 
    // viene rilevato un monitor alla porta 0 ma non è abilitato. Non attendibile
    // Nemmeno usando i valori preimpostati di panel control e crt control funziona:
    // Viene sempre attivato il ramo panel anche nella scheda con sole porte VGA -> ramo CRT
    // L'unica differenza prodotta dalle due schede è che la scheda che utilizza il ramo PANEL (HDMI)
    // riesce ad ottenere l'edid dal bios, si deduce che il CRT usa le linee DDC standard (VGA pin 12 e 15).
    // mentre l'HDMI usa il canale I2C dedicato (che noi non riusciamo ad usare) e quindi:
    // Se il bootloader ci ha passato un EDID, sappiamo che è una scheda con porta HDMI,
    // in caso contrario assumiamo sia una con sole porte CRT
    

    uint32 misc_ctrl = SM750_REG32(SM750_SYS_MISC_CTRL); // 0x000004
    // Assicuriamoci che:
    // 1. Il bit 6 (Rst) sia a 1 (Normal Operation)
    // 2. Il bit 0 (E) sia a 0 (Enable local memory)
    // 3. Forza RA (bit 5) a 0 per tenere le pagine attive (Performance UP)
    misc_ctrl &= ~(1 << 5);
    misc_ctrl |= (1 << 6);  // Forza Normal Mode
    misc_ctrl &= ~(1 << 0); // Forza Enable Memory
    // 2. Prova a allentare il refresh (bit 26:25) da 00 a 01 
    // (Refresh ogni 16x100 invece di 8x100)
    misc_ctrl &= ~(3 << 25);
    misc_ctrl |= (1 << 25);
    SM750_WREG32(SM750_SYS_MISC_CTRL, misc_ctrl); // 0x000004
    
    // Rilevazione Memoria (LOGICA UNICA)
    uint32 mem_size_code = (misc_ctrl >> 12) & 0x03; // Bit 13:12
    uint32 detected_mem;

    switch (mem_size_code) {
        case 0: detected_mem = 16 * 1024 * 1024; break;
        case 1: detected_mem = 32 * 1024 * 1024; break;
        case 2: detected_mem = 64 * 1024 * 1024; break;
        case 3: detected_mem = 8  * 1024 * 1024; break;
        default: detected_mem = 8 * 1024 * 1024; break;
    }

    si->card_info.mem_size = detected_mem;
    if (si->card_info.mem_size > 12 * 1024 * 1024)
        si->card_info.max_desktop_mem = 12 * 1024 * 1024;
    else
        si->card_info.max_desktop_mem = si->card_info.mem_size - (2 * 1024 * 1024);
    dprintf("SM750: Detected VRAM memory (from Reg 0x000004): %u MB\n", detected_mem / (1024*1024));
    
    bool showLogo = false;
    display_mode *dm = si->card_info.is_panel ? &si->preferred_mode : &si->preferred_mode2;
    struct frame_buffer_boot_info* bi = (struct frame_buffer_boot_info*)get_boot_item(FRAME_BUFFER_BOOT_INFO, NULL);
    if (bi) {
        uint32 bpp = 32;
        si->real_framebuffer_size = bi->width * bi->height * (bpp / 8);
        
        bool found = false;

        for (int i = 0; vesa_dmt_table[i].width != 0; i++) {
            if (vesa_dmt_table[i].width == bi->width && vesa_dmt_table[i].height == bi->height) {
            
                dm->timing.pixel_clock = vesa_dmt_table[i].pixel_clock;
                dm->timing.h_display    = vesa_dmt_table[i].width;
                dm->timing.h_sync_start = vesa_dmt_table[i].h_sync_start;
                dm->timing.h_sync_end   = vesa_dmt_table[i].h_sync_end;
                dm->timing.h_total      = vesa_dmt_table[i].h_total;
            
                dm->timing.v_display    = vesa_dmt_table[i].height;
                dm->timing.v_sync_start = vesa_dmt_table[i].v_sync_start;
                dm->timing.v_sync_end   = vesa_dmt_table[i].v_sync_end;
                dm->timing.v_total      = vesa_dmt_table[i].v_total;
                dm->timing.flags        = vesa_dmt_table[i].flags;
            
                found = true;
                showLogo = true;
                break;
            }
        }
        if (!found) {
            dprintf("SM750: WARNING! Boot resolution not in our table. Using 1024x768 fallback\n");

            dm->timing.pixel_clock = 65000;
            dm->timing.h_display    = 1024;
            dm->timing.h_sync_start = 1048;
            dm->timing.h_sync_end   = 1184;
            dm->timing.h_total      = 1344;
    
            dm->timing.v_display    = 768;
            dm->timing.v_sync_start = 771;
            dm->timing.v_sync_end   = 777;
            dm->timing.v_total      = 806;
            dm->timing.flags        = 0; // Standard 1024x768 usa sync negativi di solito
    
            dm->virtual_width = 1024;
            dm->virtual_height = 768;
            dm->h_display_start = 0;
            dm->v_display_start = 0;
        }

        if (bi && bi->depth <= 8) dm->space = B_CMAP8;
        else if (bi && bi->depth <= 16) dm->space = B_RGB16;
        else dm->space = B_RGB32;
        dm->virtual_width = dm->timing.h_display;
        dm->virtual_height = dm->timing.v_display;
    }
        
    // VGA BYPASS (Necessario per usare il Framebuffer lineare in modo nativo)
    // Abilitiamo il bypass VGA su entrambe le pipe (Primary e Secondary)
    uint32 display_reg = si->card_info.is_panel ? SM750_PANEL_CONTROL : SM750_CRT_CONTROL; // 0x080000 : 0x080200
    uint32 notdisplay_reg = si->card_info.is_panel ? SM750_CRT_CONTROL : SM750_PANEL_CONTROL; // 0x080200 : 0x080000
    uint32 ctrl = SM750_REG32(display_reg);
    uint32 nctrl = SM750_REG32(notdisplay_reg);
    

    // Formato 32-bit (Bit 1:0 = 10) - Uguale per entrambi
    ctrl &= ~0x00000003;
    ctrl |= 0x00000002;
    nctrl &= ~0x00000003;
    nctrl |= 0x00000002;


    // Abilitazione Piano e Timing (Bit 2 e 8) - Uguale per entrambi
    ctrl |= (1 << 2) | (1 << 8);
    nctrl |= (1 << 2) | (1 << 8);

    // Selezione Sorgente Dati (QUI CAMBIA!)
    if (si->card_info.is_panel) {
        // Registro 0x80000: Bit 29:28. Vogliamo "Panel Data" (00)
        ctrl &= ~(3U << 28); // Panel Data for Primary Display
        nctrl &= ~(3U << 18); // Panel Data for Secondary Display
    
        // Extra per LCD: Abilitiamo i segnali di controllo (Bit 27, 26, 25, 24)
        ctrl |= (1 << 27) | (1 << 26) | (1 << 25) | (1 << 24);
        
    } else {
        // Registro 0x80200: Bit 19:18. Vogliamo "CRT Data" (10)
        ctrl &= ~(3U << 18);
        ctrl |= (2U << 18);  // CRT Data for Secondary Display
        nctrl &= ~(3U << 28); 
        nctrl |= (2U << 28); // Secondary Display Data for Primary Disaply
        nctrl &= ~(1 << 2); // disattiva il piano grafico primario, altrimenti vedo immagine falsata e rovinata
        // nota d'esercizio: disattivando il piano CRT e lasciando attivo il piano PANEL, il desktop compare a monitor ma difettato
        // da righe orizzontali che traslano l'immagine.
        // o la mia scheda non supporta il ramo PANEL, o il ramo PANEL non è correttamente configurato, o c'è altro in ballo
        // indagare...
    
        // No Blank per CRT (Bit 10)
        ctrl &= ~(1 << 10);
    }
    SM750_WREG32(display_reg, ctrl);
    SM750_WREG32(notdisplay_reg, nctrl);
    
    si->card_info.f_ref = 14.31818f; //24.0f; sembra sia 14.318 da datasheet vecchio valore NTSC
    si->card_info.max_sclk = 130000; // Valori tipici SM750 (130MHz)
    si->card_info.max_mclk = 145000; // 145MHz da datasheet
    si->card_info.max_pclk = 300000; // 300MHz (Limite DAC)
    
    create_mode_list(si);
    
    uint32 bpp = 0;
    switch (dm->space) {
        case B_RGB32: case B_RGBA32: bpp = 32; break;
        case B_RGB16: bpp = 16; break;
        default: bpp = 8; break;
    }
    
    si->fbc.frame_buffer = NULL;
    si->fbc.frame_buffer_dma = (void *)(addr_t)di->pci.u.h0.base_registers[0]; // che è si->framebuffer_pci
    si->fbc.bytes_per_row = dm->timing.h_display * (bpp / 8);
    si->fbc2 = si->fbc; // per sicurezza copiamo la configurazione anche nell'altra uscita
    
    if (showLogo) {
    	display_mode *dm = si->card_info.is_panel ? &si->preferred_mode : &si->preferred_mode2;
    	draw_logo(di, dm);
    }
    snooze(2000000); // 2 seconds of glory
}
