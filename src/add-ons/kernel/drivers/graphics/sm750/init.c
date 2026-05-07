/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include <KernelExport.h>
#include <PCI.h>
//#include <drivers/bios.h>
#include <string.h>  // Per memcpy
#include <SupportDefs.h>
//typedef struct bios_regs bios_regs;
#include <boot_item.h>
//#include <edid.h>
//typedef enum bios_type_enum bios_type_enum;
//#include <vesa_info.h>

#include <frame_buffer_console.h>

#include "DriverInterface.h"
#include "sm750_macros.h"
#include "common_modes.h"
#include "sm750_logo.h"
#include "memory_manager.h"

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

static status_t init_vram_manager(shared_info* si) 
{
    // Usiamo il valore calcolato in init_chip
    uint32 desktopReserve = si->card_info.max_desktop_mem; 

    // L'heap per l'overlay e il cursore parte subito dopo la riserva desktop
    uint32 heapStart = desktopReserve;
    uint32 heapSize = si->card_info.mem_size - desktopReserve;

    dprintf("SM750: Heap VRAM allocato a 0x%x (Size: %u KB)\n", heapStart, heapSize / 1024);

    // Inizializziamo l'heap sulla seconda metà della RAM
    si->mem_mgr = (void*)mem_init("sm750_vram_heap", heapStart, heapSize, 8, 128);
    
    if (si->mem_mgr == NULL) {
    	dprintf("SM750 ERROR: mem_init fallito!\n");
    	return B_ERROR;
    }

    // --- ALLOCAZIONE CURSORE ---
    // Il cursore della SM750 in modalità "3-color + transparency" occupa 16KB.
    uint32 cursorBlockID;
    uint32 cursorOffset;
    status_t status = mem_alloc((mem_info*)si->mem_mgr, 16384, (void*)0x43555253, // Tag 'CURS'
                                &cursorBlockID, &cursorOffset);
    
    if (status == B_OK) {
        si->cursor.pci_address = cursorOffset; 
        si->cursor.block_id = cursorBlockID;
        dprintf("SM750: Cursore allocato dinamicamente a offset 0x%x\n", cursorOffset);
    } else {
        dprintf("SM750 ERROR: Impossibile allocare memoria per il cursore!\n");
    }

    return B_OK;
}

static status_t
GetEdidFromBIOS(edid1_raw* edidRaw)
{
    sm750_bios_module_info* biosModule;
    status_t status = get_module(B_BIOS_MODULE_NAME, (module_info**)&biosModule);
    if (status != B_OK) {
        dprintf("SM750: Impossibile caricare il modulo BIOS: 0x%" B_PRIx32 "\n", status);
        return status;
    }

    bios_state* state;
    status = biosModule->prepare(&state);
    if (status != B_OK) {
        dprintf("SM750: bios_prepare() fallito\n");
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
        // Allociamo memoria "bassa" accessibile dal BIOS per l'EDID
        edid1_raw* edid = (edid1_raw*)biosModule->allocate_mem(state, sizeof(edid1_raw));
        if (edid == NULL) {
            status = B_NO_MEMORY;
        } else {
            regs.eax = 0x4f15;
            regs.ebx = 1;  // Sottofunzione: Read EDID
            regs.ecx = 0;
            regs.edx = 0;
            // Calcoliamo segmento e offset per l'indirizzo reale
            regs.es  = (uint16)((addr_t)edid >> 4);
            regs.edi = (uint16)((addr_t)edid & 0x0f);

            status = biosModule->interrupt(state, 0x10, &regs);
            
            if (status == B_OK && (regs.eax & 0xffff) == 0x4f) {
                // Copiamo l'EDID nella nostra struttura finale
                memcpy(edidRaw, edid, sizeof(edid1_raw));
                dprintf("SM750: EDID letto correttamente via BIOS Interrupt!\n");
            } else {
                dprintf("SM750: BIOS fallito nel leggere l'EDID (EAX=0x%x)\n", regs.eax);
                status = B_NOT_SUPPORTED;
            }
        }
    } else {
        dprintf("SM750: DDC non supportato dal BIOS Video\n");
        status = B_NOT_SUPPORTED;
    }

    biosModule->finish(state);
    put_module(B_BIOS_MODULE_NAME);
    return status;
}

/* cannot handle gpio direction tried unlocking vga registers */
/* didn't work
static void sm750_unlock_vga_registers()
{
    // 1. Leggiamo SR21 (Display Control)
    pci->write_io_8(0x3C4, 0x21); // Seleziona indice 0x21
    uint8 sr21 = pci->read_io_8(0x3C5);
    dprintf("SM750: SR21 prima: 0x%02x\n", sr21);

    // 2. Sblocchiamo i pin (Azzera i bit 0-3 che spesso forzano il DDC hardware)
    pci->write_io_8(0x3C4, 0x21);
    pci->write_io_8(0x3C5, sr21 & ~0x0F); 

    // 3. Leggiamo SR6B (GPIO Selection)
    pci->write_io_8(0x3C4, 0x6B);
    uint8 sr6b = pci->read_io_8(0x3C5);
    dprintf("SM750: SR6B prima: 0x%02x\n", sr6b);
    
    // In alcuni chip SM, scrivere 0 in SR6B abilita il controllo GPIO MMIO
    pci->write_io_8(0x3C4, 0x6B);
    pci->write_io_8(0x3C5, 0x00); 
}*/

/* --- sm750_get_clocks --- */
/*
void 
sm750_get_clocks(vuint32 *regs, shared_info *si)
{
    // 1. Configurazione del selettore Power Mode
    uint32 pwrCtrl = SM750_REG32(SM750_SYS_PWR_MODE_CTRL);
    pwrCtrl &= ~0x00000003; // Forza i bit 1:0 a 00 (Power Mode 0)
    // Assicuriamoci anche che l'oscillatore sia attivo (Bit 3)
    pwrCtrl |= (1 << 3); 
    SM750_WREG32(SM750_SYS_PWR_MODE_CTRL, pwrCtrl);
    snooze(500);
    uint32 clkStatus = SM750_REG32(SM750_SYS_CUR_CLK_STATUS);
    dprintf("SM750: Clock Status: 0x%08" B_PRIx32 "\n", clkStatus);

    if (!(clkStatus & (1 << 8))) {
    	// --- 1. SETTAGGIO POWER MODE 0 ---
        dprintf("SM750: Spengo I2C Clock perché buggato! Abilito GPIO nei Power Mode 0 e 1...\n");
        uint32 mode0 = SM750_REG32(SM750_SYS_PWR_MODE_0_CLKC);
        //mode0 |= (1 << 8); // I2C Clock // BUGGATO
        mode0 &= ~(1 << 8); // I2C Clock OFF
        mode0 |= (1 << 6); // GPIO Clock
        SM750_WREG32(SM750_SYS_PWR_MODE_0_CLKC, mode0);

        // --- 2. SETTAGGIO POWER MODE 1 ---
        uint32 mode1 = SM750_REG32(SM750_SYS_PWR_MODE_1_CLKC);
        //mode1 |= (1 << 8); // I2C Clock (Sempre acceso anche qui!) // BUGGATO
        mode1 &= ~(1 << 8); // I2C Clock OFF
        mode1 |= (1 << 6); // GPIO Clock
        SM750_WREG32(SM750_SYS_PWR_MODE_1_CLKC, mode1);
    }
    snooze(1000);
    // Salviamo la frequenza di riferimento (24MHz)
    //si->card_info.f_ref = 24.0f; 
    // sembra che la frequenza di riferimento sia 14.31818f
    //si->card_info.f_ref = 14.31818f; // già impostato prima...
}*/
static status_t create_mode_list(shared_info* si) {
    size_t size = (MAX_EDID_MODES * sizeof(display_mode) + B_PAGE_SIZE - 1) & ~(B_PAGE_SIZE - 1);
    display_mode* local_list = NULL;

    area_id m_area = create_area("sm750 modes", (void **)&local_list,
        B_ANY_KERNEL_ADDRESS, size, B_FULL_LOCK, 
        B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA | B_READ_AREA | B_WRITE_AREA | B_CLONEABLE_AREA);

    if (m_area < 0) return m_area;

    uint32 count = 0;
    
    // TODO: Qui potresti analizzare si->vesa_edid_info per filtrare i modi!
    if (si->card_info.has_edid_vesa) {
    	// TODO
        dprintf("SM750: EDID presente, potrei filtrare i modi ma non ancora implementato...\n");
    }

    for (int i = 0; vesa_dmt_table[i].width != 0 && count < MAX_EDID_MODES; i++) {
        // USA local_list, NON si->mode_list!
        display_mode* dm = &local_list[count]; 
        const vesa_timing_t* vesa = &vesa_dmt_table[i];

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
        
        dm->space = B_RGB32;
        dm->virtual_width  = vesa->width;
        dm->virtual_height = vesa->height;
        dm->h_display_start = 0;
        dm->v_display_start = 0;
        dm->flags = 0;
        
        count++;
    }

    si->mode_count = count;
    si->mode_list_area = m_area;
    //si->mode_list = local_list; // Ora l'assegnazione è sicura dopo il ciclo!

    dprintf("SM750: create_mode_list finito. Modi: %u\n", count);
    return B_OK;
}

static void draw_logo(DeviceInfo *di, display_mode* dm) {
    if (!di || !di->framebuffer || !dm) return;

    shared_info *si = di->si;
    uint32* fb = (uint32*)di->framebuffer;
    
    uint32 screenWidth = dm->virtual_width;
    uint32 screenHeight = dm->virtual_height;
    
    uint32 bytesPerRow = si->fbc.bytes_per_row;
    if (bytesPerRow == 0)
        bytesPerRow = screenWidth * 4; 

    uint32 fbPitch = bytesPerRow / 4; 

    // Usiamo uint32 anche per le dimensioni del logo
    uint32 logoW = 640;
    uint32 logoH = 183;

    int32 startX = (int32)((screenWidth - logoW) / 2);
    int32 startY = (int32)((screenHeight - logoH) / 2);

    if (startX < 0) startX = 0;
    if (startY < 0) startY = 0;

    //dprintf("SM750: Disegno logo su %ux%u (Pitch: %u)\n", screenWidth, screenHeight, fbPitch);

    // Cambiato int in uint32 per i cicli
    for (uint32 y = 0; y < logoH && (startY + (int32)y) < (int32)screenHeight; y++) {
        for (uint32 x = 0; x < logoW && (startX + (int32)x) < (int32)screenWidth; x++) {
            uint32 fbIndex = (uint32)((startY + (int32)y) * (int32)fbPitch + (startX + (int32)x));
            fb[fbIndex] = sm750_logo[y * logoW + x];
        }
    }
}

/*
void enable_interrupts(device_info* info) {
	vuint32* regs = info->regs;
    
	// 1. Pulizia preventiva
	SM750_WREG32(SM750_SYS_RAW_INT_CLEAR, 0xFFFFFFFF); 
    
	// 2. Mascheramento: Abilitiamo Panel V-Sync (bit 0) e CRT V-Sync (bit 1)
	// Abilitiamo anche l'Engine 2D (bit 12) per quando lo sbloccheremo.
	// HEI NO! il registro SM750_SYS_INT_MASK è di sola lettura!
	// di fatto gli interrupt sono attivi di default
	//SM750_WREG32(SM750_SYS_INT_MASK, (1 << 0) | (1 << 1) | (1 << 12));
	// 2. Puliamo il 2D Engine (che sappiamo essere a 0x100050)
	uint32 engineStatus = SM750_REG32(SM750_2D_STATUS);
	engineStatus &= ~0x03; // Puliamo bit 0 e 1 scrivendo 0 come da datasheet
	SM750_WREG32(SM750_2D_STATUS, engineStatus);

	info->shared_info->irq_enabled = 1;
}*/
static void sm750_init_interrupts(DeviceInfo* info) 
{
	vuint32* regs = info->regs;

	// 1. PULIZIA MASTER: 
	// Scriviamo 1 su tutti i bit del registro Clear (0x20)
	// Questo dovrebbe resettare la logica di combinazione degli interrupt
	// 31:5 Reserved, 4 ZV1 Port, 3 ZV0 Port, 2 CRT VSYNC, 1 Panel VSYNC, 0 VGA VSYNC
	SM750_WREG32(SM750_SYS_RAW_INT_CLEAR, 0x0000001F);

	// 2. PULIZIA 2D ENGINE (0x100050)
	// Datasheet dice: "Write 0 to clear"
	uint32 engineStatus = SM750_REG32(SM750_2D_STATUS);
	engineStatus &= ~0x00000003; // Azzera bit 0 (2D) e 1 (CSC)
	SM750_WREG32(SM750_2D_STATUS, engineStatus);

	// 3. PULIZIA DMA (0x0D0020) - Nuovo!
	// Visto che hai trovato i registri DMA, puliamo anche qui per sicurezza
	// Solitamente scrivere 1 sui bit di interrupt status del DMA li pulisce
	uint32 dmaStatus = SM750_REG32(SM750_DMA_ABORT_INTERRUPT);
	SM750_WREG32(SM750_DMA_ABORT_INTERRUPT, dmaStatus); 

    // 4. PULIZIA V-SYNC (Panel e CRT)
    // Dobbiamo cercare se ci sono bit di "Clear" nei registri 0x080000 (Panel)
    // e 0x080200 (CRT). Se non li troviamo, il Clear al punto 1 potrebbe bastare.

	// 5. PULIZIA PWM
	// bit 3 PWM Interrupt Pending. In order to clear a pending interrupt, write a “1” in
	//the IP bit.

	uint32 pwmstat = SM750_REG32(SM750_PWM0);
	SM750_WREG32(SM750_PWM0,pwmstat|(1<<3));
	pwmstat = SM750_REG32(SM750_PWM1);
	SM750_WREG32(SM750_PWM1,pwmstat|(1<<3));
	pwmstat = SM750_REG32(SM750_PWM2);
	SM750_WREG32(SM750_PWM2,pwmstat|(1<<3));
    
    
	// ABILITAZIONE LOGICA
	// Siccome la Mask (0x28) è Read-Only, non possiamo scriverci.
	// Dobbiamo sperare che il valore letto non sia 0x00000000.
	uint32 currentIntActive = SM750_REG32(SM750_SYS_RAW_INT_STATUS);
	dprintf("SM750: System RAW Interrupt status: 0x%08" B_PRIx32 "\n", currentIntActive);
	uint32 intstatus = SM750_REG32(SM750_SYS_INT_STATUS);
	dprintf("SM750: Interrupt status: 0x%08" B_PRIx32 "\n", intstatus);
	uint32 currentMask = SM750_REG32(SM750_SYS_INT_MASK);
	dprintf("SM750: System Interrupt Mask is: 0x%08" B_PRIx32 "\n", currentMask);

	info->si->irq_enabled = 1;
}

/* --- sm750_init_chip --- */
void sm750_init_chip(DeviceInfo *di) {
    if (!di || !di->regs) return;
    
    //dprintf("SM750: --- Inizializzazione Hardware ---\n");
    
    //dprintf("SM750: --- DUMP SEQUENCER ESTESO ---\n");
    //// Array degli indici "sospetti" che non compaiono nel datasheet ufficiale
    //uint8 indices[] = {0x00, 0x01, 0x21, 0x30, 0x62, 0x6A, 0x6B, 0x6C};
    //for (uint32 i = 0; i < sizeof(indices); i++) {
    //    // Scriviamo l'indice sulla porta 0x3C4
    //    pci->write_io_8(0x3C4, indices[i]);
    //    // Leggiamo il valore corrispondente dalla porta 0x3C5
    //    uint8 val = pci->read_io_8(0x3C5);
    //    dprintf("SM750: SEQ Index 0x%02x = 0x%02x\n", indices[i], val);
    //}
    //sm750_unlock_vga_registers();

    vuint32 *regs = di->regs;
    shared_info *si = di->si;
    // Verifica ID via MMIO (Offset 0x54)
    uint32 device_info = SM750_REG32(SM750_SYS_DEVID); // 0x000054
    si->card_info.chip_id = (uint16)(device_info >> 16);
    uint16 device_id = (uint16)(device_info >> 16);
    uint8 revision = (uint8)(device_info & 0xFF);
    dprintf("SM750: Chip detected. ID: 0x%04X, Revision: 0x%02X\n", device_id, revision);
    
    
    sm750_init_interrupts(di);
    
    edid1_raw* boot_edid = (edid1_raw*)get_boot_item(VESA_EDID_BOOT_INFO, NULL);

    // versione senza debug
    //if (boot_edid) {
    //    memcpy(&si->vesa_edid_raw, boot_edid, sizeof(edid1_raw));
    //    si->card_info.has_edid_vesa = true;
    //} else if (GetEdidFromBIOS(&si->vesa_edid_raw) == B_OK) {
    //    si->card_info.has_edid_vesa = true;
    //} else {
    //    si->card_info.has_edid_vesa = false;
    //}
    if (boot_edid != NULL) {
        memcpy(&si->vesa_edid_raw, boot_edid, sizeof(edid1_raw));
        si->card_info.has_edid_vesa = true;
        dprintf("SM750: EDID recuperato dal Bootloader\n");
    } else {
        dprintf("SM750: Bootloader vuoto, provo GetEdidFromBIOS...\n");
        if (GetEdidFromBIOS(&si->vesa_edid_raw) == B_OK) {
            si->card_info.has_edid_vesa = true;
            dprintf("SM750: EDID recuperato correttamente dal BIOS\n");
        } else {
            si->card_info.has_edid_vesa = false;
            dprintf("SM750: Nessun EDID trovato via BIOS\n");
        }
    }
    
    // --- 1. SVEGLIA IL CHIP (Power Mode 0) ---
    // --- SBLOCCO CLOCK (Power Mode 0) ---
    uint32 mode0_gate = SM750_REG32(SM750_SYS_PWR_MODE_0_CLKC); // 0x000044
    // Abilitiamo GPIO (6), 2D (3), Display (2), Memory (1) e DMA (0)
    mode0_gate |= (1 << 3) | (1 << 2) | (1 << 1) | (1 << 0); //(1 << 6) | 
    mode0_gate &= ~(1 << 6); // Forza a 0 il clock GPIO visto che non riusciamo a usare il registro di direzione
    mode0_gate &= ~(1 << 8); // Forza a 0 il clock I2C hardware visto che è rotto
    mode0_gate |= (1 << 10); // Assicuriamoci che il VGA Clock (10) sia attivo se usiamo il CRT
    SM750_WREG32(SM750_SYS_PWR_MODE_0_CLKC, mode0_gate); // 0x000044
    //dprintf("SM750: Power Mode 0 Clock Gate set to: 0x%08x\n", mode0_gate);

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
    //dprintf("SM750: System Control (0x00) configurato: 0x%08x\n", sys_ctrl);
    
    // --- 3. ORA FACCIAMO LA RILEVAZIONE (Dopo aver attivato i bus) ---
    // Usiamo la tua logica basata sul bit 3 (CRT is Normal)
    // Rilevazione uscita (Semplificata: CRT se bit 3 è 0)
    // versione senza debug:
    //si->card_info.is_panel = (sys_ctrl & (1 << 3)) ? true : false;
    //si->card_info.active_outputs = si->card_info.is_panel ? 1 : 2;
    if (!(sys_ctrl & (1 << 3))) { 
        si->card_info.is_panel = false;
        si->card_info.active_outputs = 2; // CRT
        dprintf("SM750: CRT detected as active.\n");
    } else {
        si->card_info.is_panel = true;
        si->card_info.active_outputs = 1; // Panel
        dprintf("SM750: Panel detected as active.\n");
    }
    

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
    //dprintf("SM750: Miscellaneous Control (0x04) stabilizzato a: 0x%08x\n", misc_ctrl);
    
    
    // C. Rilevazione Memoria (LOGICA UNICA)
    uint32 mem_size_code = (misc_ctrl >> 12) & 0x03; // Bit 13:12
    // versione senza debug
    //uint32 sizes[] = { 16, 32, 64, 8 };
    //si->card_info.mem_size = sizes[mem_size_code] * 1024 * 1024;
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
    if (si->card_info.mem_size > 12 * 1024 * 1024)
        si->card_info.max_desktop_mem = 12 * 1024 * 1024;
    else
        si->card_info.max_desktop_mem = si->card_info.mem_size - (2 * 1024 * 1024);
    dprintf("SM750: Detected VRAM memory (from Reg 0x000004): %u MB\n", detected_mem / (1024*1024));
    
    
    // --- INIZIALIZZAZIONE MEMORY MANAGER ---
    // Ora che sappiamo quanta RAM c'è, attiviamo il gestore
    if (init_vram_manager(si) != B_OK) {
        dprintf("SM750: WARNING - Memory Manager initialization failed!\n");
    }
    
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
    
    bool showLogo = false;
    display_mode *dm = si->card_info.is_panel ? &si->preferred_mode : &si->preferred_mode2;
    struct frame_buffer_boot_info* bi = (struct frame_buffer_boot_info*)get_boot_item(FRAME_BUFFER_BOOT_INFO, NULL);
    if (bi) {
        //dprintf("SM750: VESA FB a 0x%" B_PRIx64 ", %" B_PRId32 "x%" B_PRId32 "\n", 
        //        (uint64)bi->physical_frame_buffer, bi->width, bi->height);
        dprintf("SM750: FrameBuffer says %ux%u\n", bi->width, bi->height);
        // set memory bounds
        uint32 bpp = 32; // Quasi sempre 32 al boot
        si->framebuffer_size = bi->width * bi->height * (bpp / 8);
        // Spostiamo l'inizio della memoria libera dopo il desktop del boot
        si->first_free_vram_offset = si->framebuffer_size;
        
        bool found = false;

        for (int i = 0; vesa_dmt_table[i].width != 0; i++) {
            if (vesa_dmt_table[i].width == bi->width && vesa_dmt_table[i].height == bi->height) {
                //dprintf("SM750: Trovato timing VESA DMT per %ux%u\n", bi->width, bi->height);
            
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
            // Qui puoi mettere il codice per la 1024x768 come visto prima
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
    
        // Altri parametri obbligatori
        dm->space = B_RGB32;
        dm->virtual_width = dm->timing.h_display;
        dm->virtual_height = dm->timing.v_display;
    }
    /*
    edid1_info* edidInfo = (edid1_info*)get_boot_item(VESA_EDID_BOOT_INFO, NULL);
	if (edidInfo != NULL) {
		si->card_info.has_vesa_edid_info = true;
		memcpy(&si->vesa_edid_info, edidInfo, sizeof(edid1_info));
		dprintf("SM750: EDID recuperato dal bootloader.\n");
	} else {
		si->card_info.has_vesa_edid_info = false;
		dprintf("SM750: Impossibile recuperare l'EDID dal bootloader.\n");
	}*/
	//if (gInfo->si->card_info.has_vesa_edid_info) {
	//	dprintf("SM750_ACC: EDID VESA trovato! Lo usiamo come fallback.\n");
		// Se la tua lettura I2C fallisce, copia questa info 
		// nella struct edid locale dell'accelerante
	//}
    
      
    // Initialize 2D engine Benaphore 
    // NO LO FACCIAMO in init_common con is_clone false
    //si->engine.lock.sem = create_sem(0, "sm750 engine benaphore");
    //si->engine.lock.ben = 0;
    //set_sem_owner(si->engine.lock.sem, B_SYSTEM_TEAM);

    
    // F. VGA BYPASS (Necessario per usare il Framebuffer lineare in modo nativo)
    // Abilitiamo il bypass VGA su entrambe le pipe (Primary e Secondary)
    uint32 display_reg = si->card_info.is_panel ? SM750_PANEL_CONTROL : SM750_CRT_CONTROL; // 0x080000 : 0x080200
    uint32 notdisplay_reg = si->card_info.is_panel ? SM750_CRT_CONTROL : SM750_PANEL_CONTROL; // 0x080200 : 0x080000
    uint32 ctrl = SM750_REG32(display_reg);
    uint32 nctrl = SM750_REG32(notdisplay_reg);
    

    // 1. Formato 32-bit (Bit 1:0 = 10) - Uguale per entrambi
    ctrl &= ~0x00000003;
    ctrl |= 0x00000002;
    nctrl &= ~0x00000003;
    nctrl |= 0x00000002;


    // 2. Abilitazione Piano e Timing (Bit 2 e 8) - Uguale per entrambi
    ctrl |= (1 << 2) | (1 << 8);
    nctrl |= (1 << 2) | (1 << 8);

    // 3. Selezione Sorgente Dati (QUI CAMBIA!)
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
        nctrl &= ~(1 << 2); // disattiva il piano grafico primario, ma siamo sicuri che funziona poi? TODO, provare a rimuovere
    
        // No Blank per CRT (Bit 10)
        ctrl &= ~(1 << 10);
    }
    SM750_WREG32(display_reg, ctrl);
    SM750_WREG32(notdisplay_reg, nctrl);
    
    //si->card_info.chip_id = di->pci.device_id; fatto con registro sopra
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
    si->fbc.frame_buffer_dma = (void *)(addr_t)di->pci.u.h0.base_registers[0];
    si->fbc.bytes_per_row = dm->timing.h_display * (bpp / 8);
    si->fbc2 = si->fbc; // per sicurezza copiamo la configurazione anche nell'altra uscita
    
    

    //dprintf("SM750: Init  completato. Mem: %d MB, Mode: %s\n", detected_mem / (1024*1024), si->card_info.is_panel ? "PANEL" : "CRT");
    
    // --- INIZIO CONFIGURAZIONE GPIO PER EDID/I2C ---
    /*
    // 1. Leggiamo il registro corretto: 0x08
    uint32 gpio_ctrl = SM750_REG32(SM750_SYS_GPIO_CTRL); //0x000008
    // 2. Impostiamo i pin 30 e 31 come I2C (funzione speciale)
    // Invece di azzerarli, li mettiamo a 1.
    //gpio_ctrl |= (1 << 31); // Pin 31 = I2C Data
    //gpio_ctrl |= (1 << 30); // Pin 30 = I2C Clock
    // ANZI NO!!!! I2C BROKEN
    // Mettiamo a 0 i bit 31 e 30 per usare i pin come GPIO normali
    gpio_ctrl &= ~(1U << 31); // SDA
    gpio_ctrl &= ~(1U << 30); // SCL
    // 3. Scriviamo il risultato per DISabilitare i2c visto che è buggato facciamo a mano
    SM750_WREG32(SM750_SYS_GPIO_CTRL, gpio_ctrl); //0x000008
    dprintf("SM750: GPIO Control (0x08) impostato per GPIO: 0x%08x\n", gpio_ctrl);
    
    // Poi disabilita anche gli interrupt sui pin GPIO 30 e 31 (Registro 0x010010)
    // Il registro gestisce i pin 25-31 nei primi bit
    uint32 gpio_int = SM750_REG32(SM750_GPIO_INT_SETUP);
    gpio_int &= ~(1U << 6); // Corrisponde al pin 31 (Enable31) -> 0: Regular GPIO
    gpio_int &= ~(1U << 5); // Corrisponde al pin 30 (Enable30) -> 0: Regular GPIO
    SM750_WREG32(SM750_GPIO_INT_SETUP, gpio_int);
    
    // Configura Direzione Iniziale (Entrambi Output per ora)
    //uint32 gpio_dir = SM750_REG32(SM750_GPIO_DIRECTION);
    uint32 gpio_dir_high = SM750_REG32(SM750_GPIO_DIR_HIGH);
    //gpio_dir |= (1U << 31) | (1U << 30); 
    gpio_dir_high |= (1U << 15) | (1U << 14); 
    //SM750_WREG32(SM750_GPIO_DIRECTION, gpio_dir);
    SM750_WREG32(SM750_GPIO_DIR_HIGH, gpio_dir_high);

    // 5. Stato di IDLE (Bus High)
    //uint32 gpio_data = SM750_REG32(SM750_GPIO_DATA);
    //gpio_data |= (1U << 31) | (1U << 30);
    //SM750_WREG32(SM750_GPIO_DATA, gpio_data);
    uint32 gpio_data_high = SM750_REG32(SM750_GPIO_DATA_HIGH);
    gpio_data_high |= (1U << 15) | (1U << 14);
    SM750_WREG32(SM750_GPIO_DATA_HIGH, gpio_data_high);
    // TERMINE configurazione iniziale GPIO per nostro bit-banging
    dprintf("SM750: GPIO High Configurati - Dir: 0x%08x, Data: 0x%08x\n", 
        SM750_REG32(SM750_GPIO_DIR_HIGH), SM750_REG32(SM750_GPIO_DATA_HIGH));
    */
   
    uint32 detect = SM750_REG32(SM750_CRT_MONITOR_DETECT);
    dprintf("SM750: Monitor Detect Status: 0x%08" B_PRIx32 "\n", detect);
    
    // 7. DIAGNOSTICA FINALE MMIO
    //sm750_get_clocks(regs, si);
    
    // SPLASH SCREEN che non funziona per ora
    //uint32* kfb = (uint32*)di->framebuffer;
    //uint32 size = detected_mem / 4; // in pixel a 32bpp
    //// Riempimento brutale ma efficace - metteremo logo se possibile
    //for (uint32 i = 0; i < size; i++) {
    //    kfb[i] = 0xFFFF0000;
    //}
    if (showLogo) {
    	display_mode *dm = si->card_info.is_panel ? &si->preferred_mode : &si->preferred_mode2;
    	draw_logo(di, dm);
    }
    snooze(3000000); // 3 seconds of glory

    //dprintf("SM750: Inizializzazione completata.\n");
}
