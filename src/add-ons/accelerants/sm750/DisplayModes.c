/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include <Accelerant.h>
#include <Debug.h>
#include <edid.h>
#include <string.h>
#include "DriverInterface.h"
#include "protos.h"
#include "sm750_macros.h"

extern accelerant_info *gInfo;

status_t sm750_get_edid_info(void* info, size_t size, uint32* _version) {
    shared_info *si = gInfo->si;

    if (size < sizeof(struct edid1_info))
        return B_BUFFER_OVERFLOW;

    if (si->card_info.is_panel) {
        if (!si->card_info.has_edid_panel)
            return B_ERROR;
        memcpy(info, si->edid_panel, sizeof(struct edid1_info));
    } else {
        if (!si->card_info.has_edid_crt)
            return B_ERROR;
        memcpy(info, si->edid_crt, sizeof(struct edid1_info));
    }
    
    if (_version != NULL)
        *_version = EDID_VERSION_1;

    return B_OK;
}
/* old righe colorate verticali
void
sm750_set_crt_pitch(uint32 pitch)
{
	vuint32 *regs = gInfo->regs;
    // Il datasheet specifica allineamento a 128-bit (16 byte)
    // Valore da scrivere = pitch / 16
    uint32 aligned_pitch = pitch / 16;
    
    // Registro 0x080208:
    // Bit 29:20 -> Window Width
    // Bit 13:4  -> FB Offset
    // Entrambi i campi vogliono il valore allineato (pitch/16)
    uint32 reg_val = ((aligned_pitch & 0x3FF) << 20) | ((aligned_pitch & 0x3FF) << 4);
    
    SM750_WREG32(SM750_DISP_CRT_FB_WIDTH, reg_val);
    
    debug_printf("SM750_ACC: Pitch %u -> Aligned %u, Reg 0x80208: 0x%08x\n", 
        pitch, aligned_pitch, reg_val);
}*/
void
sm750_set_crt_pitch(uint32 pitch)
{
	vuint32 *regs = gInfo->regs;
    // pitch è in BYTES (es. 4096 per 1024x768x32)
    uint32 aligned_val = (pitch / 16) & 0x3FF; 

    // Costruiamo il registro come da datasheet:
    // Window Width (29:20) = aligned_val
    // FB Offset    (13:4)  = aligned_val
    // I bit 19:16 e 3:0 sono Hardwired a 0, quindi lo shift deve essere preciso.
    
    uint32 reg_val = (aligned_val << 20) | (aligned_val << 4);
    
    SM750_WREG32(SM750_DISP_CRT_FB_WIDTH, reg_val);
    
    debug_printf("SM750_ACC: Pitch Bytes %u -> Valore Allineato 0x%X\n", pitch, aligned_val);
    debug_printf("SM750_ACC: Scrittura Registro 0x80208: 0x%08X\n", reg_val);
}

status_t
sm750_set_crt_fb_addr(uint32 offset)
{
	vuint32 *regs = gInfo->regs;
	
	// Struttura registro:
	// S (Bit 31): Read-only status (Flip pending). Lo scriviamo a 0.
    // Ext (Bit 27): Memory Selection. Deve essere 0 per Local Memory.
    // Address (Bit 25:4): Indirizzo fisico allineato a 128-bit.
    // Bit 3:0: Hardwired a 0, allineamento a 16 byte
        
    // 1. Controllo allineamento (128-bit / 16 byte)
    if (offset & 0xF) {
        debug_printf("SM750_ACC: ERROR - FB Address 0x%08x non allineato a 16 byte!\n", offset);
        offset &= ~0xF; // Forza l'allineamento arrotondando per difetto
    }

    // 2. Controllo Limiti (Safe Guard per 16MB)
    if (offset >= (16 * 1024 * 1024)) {
        debug_printf("SM750_ACC: ERROR - FB Address 0x%08x fuori dai 16MB!\n", offset);
        return B_BAD_VALUE;
    }

    // 3. Costruzione registro 0x080204
    // Maschera 0x03FFFFF0: 
    // - Bit 31 (S) = 0
    // - Bit 30:28 (Res) = 0
    // - Bit 27 (Ext) = 0 (Local Memory)
    // - Bit 26 (Res) = 0
    // - Bit 25:4 (Address) = Il nostro valore
    // - Bit 3:0 = 0 (Hardwired)
    uint32 reg_val = (offset & 0x03FFFFF0);

    SM750_WREG32(SM750_DISP_CRT_FB_ADDR, reg_val);
    
    debug_printf("SM750_ACC: CRT FB Address impostato a: 0x%08x (Reg: 0x%08x)\n", offset, reg_val);
    return B_OK;
}

void
sm750_set_crt_h_timing(uint32 total, uint32 active)
{
	vuint32 *regs = gInfo->regs;
	// Struttura registro:
	// HT  (Bit 27:16): Total pixels - 1
    // HDE (Bit 11:0):  Active pixels - 1
    // I bit 31:28 e 15:12 devono rimanere 0 (Reserved)
    uint32 val = (( (total - 1)  & 0x0FFF) << 16) | 
                 (( (active - 1) & 0x0FFF));
    SM750_WREG32(SM750_CRT_H_TOTAL_ACTIVE, val);
}

void
sm750_set_crt_h_sync(uint32 start, uint32 end)
{
	vuint32 *regs = gInfo->regs;
	// Struttura registro:
	// HSW (Bit 23:16): Sync width (pixels) - 8 bit
    // HS  (Bit 11:0):  Sync start (pixel number - 1) - 12 bit
    // Bit 31:24 e 15:12 devono rimanere 0 (Reserved)
    uint32 width = (end - start) & 0x00FF;
    uint32 val = (width << 16) | ((start - 1) & 0x0FFF);
    SM750_WREG32(SM750_CRT_H_SYNC, val);
}

void
sm750_set_crt_v_timing(uint32 total, uint32 active)
{
	vuint32 *regs = gInfo->regs;
	// Struttura registro
	// VT  (Bit 26:16): Vertical total lines - 1 (11 bit)
    // VDE (Bit 10:0):  Vertical display end lines - 1 (11 bit)
    // Bit 31:27 e 15:11 devono rimanere 0 (Reserved)
    uint32 val = (( (total - 1)  & 0x07FF) << 16) | 
                 (( (active - 1) & 0x07FF));
    SM750_WREG32(SM750_CRT_V_TOTAL_ACTIVE, val);
}

void
sm750_set_crt_v_sync(uint32 start, uint32 end)
{
	vuint32 *regs = gInfo->regs;
	// Struttura registro
	// VSH (Bit 21:16): Vertical sync height (lines) - 6 bit
    // VS  (Bit 10:0):  Vertical sync start (line number - 1) - 11 bit
    // Bit 31:22 e 15:11 devono rimanere 0 (Reserved)
    uint32 height = (end - start) & 0x003F;
    uint32 val = (height << 16) | ((start - 1) & 0x07FF);
    SM750_WREG32(SM750_CRT_V_SYNC, val);
}

status_t
sm750_set_display_mode(display_mode *mode)
{
	debug_printf("SM750_ACC: Avvio impostazione display mode\n");
    shared_info *si = gInfo->si;
    vuint32 *regs = gInfo->regs;
    
    uint32 bpp = (mode->space == B_RGB32) ? 32 : 16;
    uint32 color_fmt = (mode->space == B_RGB32) ? 2 : 1; // 2=32bpp, 1=16bpp
    uint32 pitch = mode->timing.h_display * (bpp / 8);
    
    // 1. Programmiamo il Clock (PLL)
    debug_printf("SM750_ACC: programmazione pll...\n");
    sm750_program_pll(mode->timing.pixel_clock, si->card_info.is_panel);
    debug_printf("SM750_ACC: programmazione pll effettuata\n");

    if (si->card_info.is_panel) {
        // --- PRIMARY (PANEL) ---
        debug_printf("SM750_ACC: impostazione timings per PANEL...\n");
        SM750_WREG32(SM750_PANEL_H_TOTAL_ACTIVE, ((mode->timing.h_total - 1) << 16) | (mode->timing.h_display - 1));
        SM750_WREG32(SM750_PANEL_H_SYNC, ((mode->timing.h_sync_end - mode->timing.h_sync_start) << 16) | (mode->timing.h_sync_start - 1));
        SM750_WREG32(SM750_PANEL_V_TOTAL_ACTIVE, ((mode->timing.v_total - 1) << 16) | (mode->timing.v_display - 1));
        SM750_WREG32(SM750_PANEL_V_SYNC, ((mode->timing.v_sync_end - mode->timing.v_sync_start) << 16) | (mode->timing.v_sync_start - 1));

        debug_printf("SM750_ACC: impostazione PANEL_FB_ADDR a 0...\n");
        SM750_WREG32(SM750_DISP_PANEL_FB_ADDR, 0); 
        debug_printf("SM750_ACC: impostazione larghezza FB PANEL...\n");
        SM750_WREG32(SM750_DISP_PANEL_FB_WIDTH, (pitch << 16) | pitch);

        // Controllo: Enable + Timing Enable + Format
        uint32 ctrl = (1 << 0) | (1 << 2) | (color_fmt << 13);
        if (!(mode->timing.flags & B_POSITIVE_HSYNC)) ctrl |= (1 << 3);
        if (!(mode->timing.flags & B_POSITIVE_VSYNC)) ctrl |= (1 << 4);

        debug_printf("SM750_ACC: Scrittura sul registro di controllo di PANEL...\n");
        SM750_WREG32(SM750_PANEL_CONTROL, ctrl);
        debug_printf("SM750_ACC: Scrittura completata su registro di controllo di PANEL...\n");
    } else {
    	/* logorroic mode */
    	// Calcolo polarità
    	// --- CRT (Secondary Display) ---
        bool h_pos = (mode->timing.flags & B_POSITIVE_HSYNC);
        bool v_pos = (mode->timing.flags & B_POSITIVE_VSYNC);

        debug_printf("SM750_ACC: Setup CRT %dx%d (%d bpp)\n", mode->timing.h_display, mode->timing.v_display, bpp);

        // 1. Timings Orizzontali
        debug_printf("SM750_DEBUG: H_Total: %d, H_Disp: %d, H_SyncStart: %d, H_SyncEnd: %d\n",
            mode->timing.h_total, mode->timing.h_display, 
            mode->timing.h_sync_start, mode->timing.h_sync_end);
        
        sm750_set_crt_h_timing(mode->timing.h_total, mode->timing.h_display);
        
        sm750_set_crt_h_sync(mode->timing.h_sync_start, mode->timing.h_sync_end);

        // 2. Timings Verticali
        debug_printf("SM750_DEBUG: V_Total: %d, V_Disp: %d, V_SyncStart: %d, V_SyncEnd: %d\n",
            mode->timing.v_total, mode->timing.v_display, 
            mode->timing.v_sync_start, mode->timing.v_sync_end);
        
        sm750_set_crt_v_timing(mode->timing.v_total, mode->timing.v_display);
        
        sm750_set_crt_v_sync(mode->timing.v_sync_start, mode->timing.v_sync_end);

        // 3. Framebuffer Address (Registro 0x080204)
        // Puntiamo allo 0 fisico (inizio memoria video)
        // S (Bit 31): Read-only status (Flip pending). Lo scriviamo a 0.
        // Ext (Bit 27): Memory Selection. Deve essere 0 per Local Memory.
        // Address (Bit 25:4): Indirizzo fisico allineato a 128-bit.
        // Bit 3:0: Hardwired a 0.
        // Se vogliamo l'inizio della memoria (offset 0), il calcolo è semplice.
        // Se volessimo un offset, dovremmo assicurarci che sia multiplo di 16.
        //uint32 fb_offset = 0; // Inizio della VRAM locale
        // Puliamo l'indirizzo per sicurezza (anche se 0 è già pulito)
        // Mascheriamo per assicurarci di non toccare i bit Reserved o il bit Ext
        //uint32 fb_addr_val = (fb_offset & 0x03FFFFF0); 
        //SM750_WREG32(SM750_DISP_CRT_FB_ADDR, fb_addr_val);
        // usiamo la funzione helper
        sm750_set_crt_fb_addr(0);

        // 4. Pitch e Window Width (Registro 0x080208) - USA L'HELPER!
        sm750_set_crt_pitch(pitch);

        // 5. Registro di Controllo CRT (0x080200)
        uint32 ctrl = 0;
        // 1. Formato (Bit 1:0) -> 10 per 32bpp, 01 per 16bpp
        if (mode->space == B_RGB32) 
            ctrl |= 0x2; 
        else 
            ctrl |= 0x1;
        // 2. Abilita il piano grafico (Bit 2)
        ctrl |= (1 << 2); 
        // 3. Abilita i Timings (Bit 8) -> FONDAMENTALE per non avere schermo nero
        ctrl |= (1 << 8);
        // 4. Blanking (Bit 10) -> Deve essere 0 per mostrare i pixel. Lo è già.
        // 5. Polarità Sync (Bit 12 HSP, Bit 13 VSP)
        // Nota: Il datasheet dice 0=High, 1=Low. 
        // Haiku B_POSITIVE_HSYNC significa che vogliamo High (quindi bit a 0).
        if (!h_pos) ctrl |= (1 << 12);
        if (!v_pos) ctrl |= (1 << 13);
        // 6. Data Select (Bit 19:18) -> Vogliamo CRT Data (10)
        ctrl |= (2 << 18);
        // 7. VGA Data Shift (Bit 26) -> Di solito 0 (Enable)
        // ctrl |= (0 << 26);

        debug_printf("SM750_ACC: Writing CRT_CONTROL: 0x%08x\n", ctrl);
        SM750_WREG32(SM750_CRT_CONTROL, ctrl);
    	debug_printf("SM750_ACC: --- FINE SETUP CRT ---\n");
    }

    si->dm = *mode;
    debug_printf("SM750_ACC: Inizio test riempimento di rosso.\n");
    uint32 *fb = (uint32 *)gInfo->si->framebuffer;
    if (fb != NULL) {
        // Coloriamo solo una parte per sicurezza
        //for (int i = 0; i < (mode->timing.h_display * mode->timing.v_display); i++) {
        //    fb[i] = 0xFF0000; 
        //}
        for (int i = 0; i < (mode->timing.h_display * 100); i++) {
            fb[i] = 0xFF0000;
        }
    }
    debug_printf("SM750_ACC: Framebuffer riempito di rosso per test.\n");
    snooze(2000000);
    
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
    
    //config->frame_buffer = (void *)gInfo->framebuffer;
    config->frame_buffer = (void *)si->framebuffer; //ma noi avevamo messo NULL
    config->frame_buffer_dma = (void *)si->framebuffer_pci;
    config->bytes_per_row = si->dm.timing.h_display * (bpp / 8);

    //config->bytes_per_row = si->dm.timing.h_display * (si->bits_per_pixel / 8);
    return B_OK;
}


status_t
sm750_get_display_mode(display_mode *current_mode)
{
    shared_info *si = gInfo->si;
    *current_mode = si->dm; // Restituiamo l'ultima modalità salvata
    return B_OK;
}
/*
status_t
sm750_propose_display_mode(display_mode *target, const display_mode *low, const display_mode *high)
{
	shared_info *si = gInfo->si;
    // 1. Limite Pixel Clock
    // Abbiamo detto che i tuoi DAC arrivano a 300MHz
    if (target->timing.pixel_clock > si->card_info.max_pclk) 
        return B_BAD_VALUE;

    // 2. Limite Memoria (16MB)
    uint32 bytes_per_pixel = 0;
    switch (target->space) {
        case B_RGB32: bytes_per_pixel = 4; break;
        case B_RGB16: bytes_per_pixel = 2; break;
        case B_CMAP8: bytes_per_pixel = 1; break;
        default: return B_BAD_VALUE;
    }
    
    uint32 mem_needed = target->virtual_width * target->virtual_height * bytes_per_pixel;
    if (mem_needed > si->card_info.mem_size) {
    	debug_printf("SM750_ACC: resolution exceeds memory size");
        return B_BAD_VALUE;
    }

    // 3. Allineamento Hardware
    // La SM750 di solito vuole la larghezza multipla di 8 o 16 pixel
    target->timing.h_display = (target->timing.h_display + 7) & ~7;
    
    return B_OK;
}*/

status_t
sm750_propose_display_mode(display_mode *target, const display_mode *low, const display_mode *high)
{
    shared_info *si = gInfo->si;
    debug_printf("SM750_ACC: Avvio chiamata propose display mode...\n");

    // 1. Forza i valori virtuali se non impostati
    if (target->virtual_width < target->timing.h_display)
        target->virtual_width = target->timing.h_display;
    if (target->virtual_height < target->timing.v_display)
        target->virtual_height = target->timing.v_display;

    // 2. Controllo Memoria
    uint32 bpp = (target->space == B_RGB32) ? 4 : 2;
    uint32 mem_needed = target->virtual_width * target->virtual_height * bpp;
    
    if (mem_needed > si->card_info.mem_size)
        return B_BAD_VALUE;

    // 3. Allineamento (Molto importante per SM750)
    // Il pitch deve essere multiplo di 16 byte per il motore 2D/Display
    target->virtual_width = (target->virtual_width + 15) & ~15;

    debug_printf("SM750_ACC: Chiamata propose display mode terminata con successo...\n");
    return B_OK;
}

uint32 sm750_accelerant_mode_count(void) {
    // In produzione qui contiamo i modi validati dall'EDID
    return 1; // Proviamo con 1 solo modo per testare la stabilità
}

status_t 
sm750_get_mode_list(display_mode* dm) {
	debug_printf("SM750_ACC: Avvio chiamata get mode list...\n");
    display_mode mode = {
        { 65000, 1024, 1048, 1184, 1344, 768, 771, 777, 806, 0 }, // Timing
        B_RGB32,     // space
        1024, 768,   // virtual_width, virtual_height
        0, 0         // h_display_start, v_display_start
    };
    
    // Aggiungiamo i flag di polarità se necessari (Sync Positivo/Negativo)
    // Molti monitor moderni vogliono B_POSITIVE_HSYNC | B_POSITIVE_VSYNC
    mode.timing.flags = B_POSITIVE_HSYNC | B_POSITIVE_VSYNC;

    dm[0] = mode;
    debug_printf("SM750_ACC: Chiamata get mode list completata, restituendo in dm[0]...\n");
    return B_OK;
}
