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
#include <create_display_modes.h>

extern accelerant_info *gInfo;
/* old
status_t sm750_old_get_edid_info(void* info, size_t size, uint32* _version) {
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
}*/

status_t 
sm750_get_edid_info(void* info, size_t size, uint32* _version) 
{
    if (size < sizeof(struct edid1_info))
        return B_BUFFER_OVERFLOW;
    
    shared_info *si = gInfo->si;
    //uint8* raw_data;
    edid1_info* target = (edid1_info*)info;
    bool found = false;
    
    if (si->card_info.is_panel && gInfo->has_edid_panel) {
        *target = gInfo->edid_panel_info;
        found = true;
    } else if (!si->card_info.is_panel && gInfo->has_edid_crt) {
        *target = gInfo->edid_crt_info;
        found = true;
    }
    
    if (!found && si->card_info.has_edid_vesa) {
    	edid_decode(&gInfo->edid_vesa_info, &si->vesa_edid_raw);
        debug_printf("SM750_ACC: I2C fallito, fornisco EDID VESA di backup.\n");
        *target = gInfo->edid_vesa_info;
        found = true;
    }

    if (!found) {
        debug_printf("SM750_ACC: Nessun EDID trovato (I2C fallito e niente VESA).\n");
        return B_ERROR;
    }

    // 2. DECODIFICA (Qui avviene la magia)
    // Trasformiamo i 128 byte grezzi nella struttura che Haiku capisce
    // edid_decode((edid1_info*)info, (edid1_raw*)raw_data);

    if (_version != NULL)
        *_version = EDID_VERSION_1;

    debug_printf("SM750_ACC: EDID info fornito correttamente al sistema.\n");
    return B_OK;
}
/*
static void
decode_detailed_timing(edid1_detailed_timing *t, display_timing *timing)
{
    timing->pixel_clock = t->pixel_clock * 10;
    timing->h_display = t->h_active;
    timing->h_sync_start = t->h_active + t->h_sync_off;
    timing->h_sync_end = timing->h_sync_start + t->h_sync_width;
    timing->h_total = t->h_active + t->h_blank;

    timing->v_display = t->v_active;
    timing->v_sync_start = t->v_active + t->v_sync_off;
    timing->v_sync_end = timing->v_sync_start + t->v_sync_width;
    timing->v_total = t->v_active + t->v_blank;

    timing->flags = 0;
    if (t->interlaced) timing->flags |= B_TIMING_INTERLACED;
    if ((t->sync & 0x02)) timing->flags |= B_POSITIVE_HSYNC;
    if ((t->sync & 0x01)) timing->flags |= B_POSITIVE_VSYNC;
}

static void
fill_display_mode(display_timing *timing, color_space space, display_mode *mode)
{
    mode->timing = *timing;
    mode->space = space;
    
    // Virtual width/height di solito coincidono con il display fisico
    // a meno di panning o configurazioni particolari.
    mode->virtual_width = timing->h_display;
    mode->virtual_height = timing->v_display;
    
    mode->h_display_start = 0;
    mode->v_display_start = 0;
    mode->flags = 0; // Flags del display_mode, diversi dai flags del timing
}
// *******   ESEMPIO DI UTILIZZO *************************************
if (gInfo->si->card_info.has_vesa_edid_info) {
    display_timing preferredTiming;
    
    // 1. Estraiamo il timing dall'EDID
    decode_detailed_timing(&gInfo->si->vesa_edid_info.detailed_timing[0], &preferredTiming);
    
    // 2. Creiamo il display_mode completo usando lo spazio colore corrente
    // (Inizialmente B_RGB32, ma pronto per essere dinamico)
    fill_display_mode(&preferredTiming, B_RGB32, &gInfo->si->preferred_mode);
    
    debug_printf("SM750: Modalità preferita impostata a %dx%d @ 32bpp\n", 
        preferredTiming.h_display, preferredTiming.v_display);
}
// ********************************************************************
extern "C" status_t 
create_mode_list_from_edid(uint8* raw_buffer) 
{
    shared_info *si = gInfo->si;
    edid1_info info;
    display_mode edid_modes[MAX_EDID_MODES];
    uint32 edid_count = 0;
    
    // Se non passiamo un buffer I2C fresco, proviamo a usare quello VESA del kernel
    if (raw_buffer == NULL) {
        if (si->card_info.has_edid_vesa) {
            debug_printf("SM750_ACC: Nessun buffer EDID, uso EDID VESA di backup.\n");
            edid_decode(&gInfo->edid_vesa_info, &si->vesa_edid_raw);
            info = gInfo->edid_vesa_info; // Copia diretta della struct
        } else {
            debug_printf("SM750_ACC: Errore: nessun EDID disponibile (I2C o VESA).\n");
            return B_ERROR;
        }
    } else {
        // Decodifica il buffer grezzo letto via I2C
        edid_decode(&info, (edid1_raw*)raw_buffer);
        
        // Salvataggio locale per riferimento futuro nell'accelerante
        if (si->card_info.is_panel) {
            gInfo->edid_panel_info = info;
            gInfo->has_edid_panel = true;
        } else {
            gInfo->edid_crt_info = info;
            gInfo->has_edid_crt = true;
        }
    }

    // 1. Estraiamo i timing dall'EDID
    for (int i = 0; i < EDID1_NUM_DETAILED_MONITOR_DESC; i++) {
        if (info.detailed_monitor[i].monitor_desc_type == EDID1_IS_DETAILED_TIMING) {
            display_timing timing;
            
            // Decodifica solo i tempi
            decode_detailed_timing(&info.detailed_monitor[i].data.detailed_timing, &timing);
            
            // Assembla il display_mode (usiamo B_RGB32 di default)
            // TODO, dovremmo inserire ache B_RGB16 e 8!!!! così è limitante, e dovremmo anche
            // aggiungere i flags edid_modes[edid_count].flags = B_8_BIT_DAC | B_HARDWARE_CURSOR;
            fill_display_mode(&timing, B_RGB32, &edid_modes[edid_count]);

            // Se è il primo ed è il preferito dal monitor, aggiorniamo la shared_info
            if (edid_count == 0 && info.display.preferred_timing_mode) {
                if (si->card_info.is_panel) {
                    si->preferred_mode = edid_modes[edid_count];
                } else {
                    si->preferred_mode2 = edid_modes[edid_count];
                }
                debug_printf("SM750_ACC: Modalità preferita aggiornata da EDID (%dx%d)\n", 
                    timing.h_display, timing.v_display);
            }
            
            edid_count++;
            if (edid_count >= MAX_EDID_MODES) break;
        }
    }

    // 2. Aggiorniamo la lista globale dei modi
    if (edid_count > 0) {
        debug_printf("SM750_ACC: Generati %d modi da EDID.\n", edid_count);
        si->mode_count = edid_count;
        for (uint32 j = 0; j < edid_count; j++) {
            si->mode_list[j] = edid_modes[j];
        }
        return B_OK;
    }

    debug_printf("SM750_ACC: EDID non conteneva timing validi.\n");
    return B_ERROR;
}*/
extern "C" status_t 
create_mode_list_from_edid(uint8* raw_buffer) 
{
    shared_info *si = gInfo->si;
    edid1_info info;
    
    // 1. Decodifica (I2C o VESA)
    if (raw_buffer == NULL) {
        if (si->card_info.has_edid_vesa) {
            edid_decode(&info, &si->vesa_edid_raw);
        } else return B_ERROR;
    } else {
        edid_decode(&info, (edid1_raw*)raw_buffer);
        // Salvataggio per uso futuro
        if (si->card_info.is_panel) { gInfo->edid_panel_info = info; gInfo->has_edid_panel = true; }
        else { gInfo->edid_crt_info = info; gInfo->has_edid_crt = true; }
    }

    // 2. GENERAZIONE AUTOMATICA (Consigliato!)
    // Questa funzione di Haiku crea un'area, popola i modi per 8, 16 e 32 bit,
    // e gestisce i limiti di banda del chip se passati correttamente.
    display_mode* list = NULL;
    uint32 count = 0;
    
    // create_display_modes(nome_area, info_edid, lista_modi_extra, count_extra, 
    //                      modi_supportati, count_supportati, filtro_callback, 
    //                      risultato_lista, risultato_count)
    area_id new_area = create_display_modes("sm750 modes", &info, 
        NULL, 0, // Nessun modo extra
        NULL, 0, // Usa tutti i modi standard Haiku compatibili con EDID
        NULL,    // Nessun filtro
        &list, &count);
    /*listArea = create_display_modes("dummy",
		si.bHaveEDID ? &si.edidInfo : NULL,
		NULL, 0, si.colorSpaces, si.colorSpaceCount,
		(check_display_mode_hook)checkMode, &list, &count);
		
		magari mancano pezzi*/

    if (new_area >= 0) {
        // Aggiorniamo la shared info con la nuova area "dinamica"
        si->mode_list_area = new_area;
        si->mode_count = count;
        
        // Aggiorniamo il preferred mode per il driver
        if (count > 0) {
            if (si->card_info.is_panel) si->preferred_mode = list[0];
            else si->preferred_mode2 = list[0];
        }
        
        debug_printf("SM750_ACC: CreateDisplayModes ha generato %" B_PRIu32 " modi\n", count);
        return B_OK;
    }

    // --- SE VUOI CONTINUARE COL TUO METODO MANUALE ---
    // Devi aggiungere questo ciclo per i colori:
    /*
    uint32 color_spaces[] = { B_RGB32, B_RGB16, B_CMAP8 };
    for (int i = 0; i < edid_count_timing; i++) {
        for (int c = 0; c < 3; c++) {
            fill_display_mode(&timings[i], color_spaces[c], &si->mode_list[final_count]);
            si->mode_list[final_count].flags = B_8_BIT_DAC | B_HARDWARE_CURSOR;
            final_count++;
        }
    }
    */

    return B_ERROR;
}

static void
sm750_set_pitch(uint32 pitch, bool is_panel)
{
	vuint32 *regs = gInfo->regs;
    // pitch è in BYTES (es. 4096 per 1024x768x32)
    uint32 aligned_val = (pitch / 16) & 0x3FF; 

    // Costruiamo il registro come da datasheet:
    // Window Width (29:20) = aligned_val
    // FB Offset    (13:4)  = aligned_val
    // I bit 19:16 e 3:0 sono Hardwired a 0, quindi lo shift deve essere preciso.
    
    uint32 reg_val = (aligned_val << 20) | (aligned_val << 4);
    uint32 reg_offset = is_panel ? SM750_DISP_PANEL_FB_WIDTH : SM750_DISP_CRT_FB_WIDTH ;
    SM750_WREG32(reg_offset, reg_val);
    
    //debug_printf("SM750_ACC: Pitch Bytes %u -> Valore Allineato 0x%X\n", pitch, aligned_val);
    //debug_printf("SM750_ACC: Scrittura Registro 0x80208: 0x%08X\n", reg_val);
}

status_t
sm750_set_fb_addr(uint32 offset, bool is_panel)
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

    uint32 reg_offset = is_panel ? SM750_DISP_PANEL_FB_ADDR : SM750_DISP_CRT_FB_ADDR ;
    SM750_WREG32(reg_offset, reg_val);
    
    debug_printf("SM750_ACC: %s FB Address impostato a: 0x%08x (Reg: 0x%08x)\n", is_panel ? "PANEL": "CRT" ,offset, reg_val);
    return B_OK;
}
static void sm750_set_h_timing(uint32 total, uint32 active, bool is_panel)
{
	vuint32 *regs = gInfo->regs;
	// Struttura registro:
	// HT  (Bit 27:16): Total pixels - 1
    // HDE (Bit 11:0):  Active pixels - 1
    // I bit 31:28 e 15:12 devono rimanere 0 (Reserved)
    uint32 val = (( (total - 1)  & 0x0FFF) << 16) | 
                 (( (active - 1) & 0x0FFF));
    uint32 reg_offset = is_panel ? SM750_PANEL_H_TOTAL_ACTIVE : SM750_CRT_H_TOTAL_ACTIVE ;
    SM750_WREG32(reg_offset, val);
}

static void sm750_set_h_sync(uint32 start, uint32 end, bool is_panel)
{
	vuint32 *regs = gInfo->regs;
	// Struttura registro:
	// HSW (Bit 23:16): Sync width (pixels) - 8 bit
    // HS  (Bit 11:0):  Sync start (pixel number - 1) - 12 bit
    // Bit 31:24 e 15:12 devono rimanere 0 (Reserved)
    uint32 width = (end - start) & 0x00FF;
    uint32 val = (width << 16) | ((start - 1) & 0x0FFF);
    uint32 reg_offset = is_panel ? SM750_PANEL_H_SYNC : SM750_CRT_H_SYNC ;
    SM750_WREG32(reg_offset, val);
}


static void sm750_set_v_timing(uint32 total, uint32 active, bool is_panel)
{
	vuint32 *regs = gInfo->regs;
	// Struttura registro
	// VT  (Bit 26:16): Vertical total lines - 1 (11 bit)
    // VDE (Bit 10:0):  Vertical display end lines - 1 (11 bit)
    // Bit 31:27 e 15:11 devono rimanere 0 (Reserved)
    uint32 val = (( (total - 1)  & 0x07FF) << 16) | 
                 (( (active - 1) & 0x07FF));
    uint32 reg_offset = is_panel ? SM750_PANEL_V_TOTAL_ACTIVE : SM750_CRT_V_TOTAL_ACTIVE ;
    SM750_WREG32(reg_offset, val);
}

static void sm750_set_v_sync(uint32 start, uint32 end, bool is_panel)
{
	vuint32 *regs = gInfo->regs;
	// Struttura registro
	// VSH (Bit 21:16): Vertical sync height (lines) - 6 bit
    // VS  (Bit 10:0):  Vertical sync start (line number - 1) - 11 bit
    // Bit 31:22 e 15:11 devono rimanere 0 (Reserved)
    uint32 height = (end - start) & 0x003F;
    uint32 val = (height << 16) | ((start - 1) & 0x07FF);
    uint32 reg_offset = is_panel ? SM750_PANEL_V_SYNC : SM750_CRT_V_SYNC ;
    SM750_WREG32(reg_offset, val);
}

// In sm750_accelerant.c (o dove hai i puntatori alle funzioni)
/*
status_t
sm750_get_preferred_mode(display_mode* mode)
{
    shared_info *si = gInfo->si;
    display_mode *preferred = si->card_info.is_panel ? &si->preferred_mode : &si->preferred_mode2;

    // Se abbiamo una modalità preferita valida dall'EDID
    if (preferred->timing.pixel_clock > 0 && preferred->timing.h_display > 10) {
        *mode = *preferred;
        return B_OK;
    }

    // Altrimenti torniamo il fallback di sicurezza
    display_mode default_mode = {
        { 65000, 1024, 1048, 1184, 1344, 768, 771, 777, 806, 0 },
        B_RGB32, 1024, 768, 0, 0
    };
    *mode = default_mode;
    
    return B_OK;
}*/
status_t
sm750_get_preferred_mode(display_mode* mode)
{
    shared_info *si = gInfo->si;
    display_mode *preferred = si->card_info.is_panel ? &si->preferred_mode : &si->preferred_mode2;

    // Se abbiamo una modalità preferita dall'EDID, la diamo con orgoglio
    if (preferred->timing.pixel_clock > 0 && preferred->timing.h_display > 10) {
        *mode = *preferred;
        if (mode->space == 0) mode->space = B_RGB32;
        return B_OK;
    }

    // Se NON abbiamo l'EDID, non inventiamoci nulla. 
    // Restituiamo B_ERROR così l'App Server usa B_GET_MODE_LIST 
    // e sceglie la prima o la più alta disponibile.
    return B_ERROR; 
}

status_t
sm750_set_display_mode(display_mode *mode)
{
	debug_printf("SM750_ACC: Avvio impostazione display mode\n");
    shared_info *si = gInfo->si;
    vuint32 *regs = gInfo->regs;
    
    // --- LOG DI DEBUG PER IL PITCH ---
    /*debug_printf("SM750_ACC: Richiesta SET_DISPLAY_MODE:\n");
    debug_printf("SM750_ACC:  - Target Timing: %dx%d\n", mode->timing.h_display, mode->timing.v_display);
    debug_printf("SM750_ACC:  - Target Virtual: %dx%d\n", mode->virtual_width, mode->virtual_height);
    */
    //display_mode *pm = si->card_info.is_panel ? &si->preferred_mode : &si->preferred_mode2;
    // Vediamo cosa abbiamo in memoria come fallback/preferito
    /*
    debug_printf("SM750_ACC:  - In shared_info preferred: %dx%d\n", 
        pm->timing.h_display, pm->timing.v_display);
        */
    
    uint32 bpp = (mode->space == B_RGB32) ? 32 : 16;
    //uint32 color_fmt = (mode->space == B_RGB32) ? 2 : 1; // 2=32bpp, 1=16bpp
    //uint32 pitch = mode->timing.h_display * (bpp / 8);
    // 2. Usiamo virtual_width per il Pitch (la memoria reale)
    // Se h_display è 1024 ma virtual_width è 1024, il risultato non cambia.
    // Ma se virtual_width è 1040, il pitch sarà corretto e le righe spariranno.
    uint32 pitch = mode->virtual_width * (bpp / 8);
    uint32 desktop_size = pitch * mode->virtual_height;
    si->framebuffer_size = desktop_size;
    // La memoria libera per l'overlay inizia subito dopo il desktop
    // Allineiamo a 16 byte per sicurezza (richiesto dal chip SM750)
    si->first_free_vram_offset = (desktop_size + 15) & ~15;
    
    // Se l'overlay era attivo, qui dovremmo resettarlo o notificare il cambio
    // Per ora lo spegniamo per evitare che punti a zone di memoria vecchie
    uint32 video_ctrl = SM750_REG32(SM750_DISP_PANEL_VIDEO_DISP_CTRL);
    if (video_ctrl & (1 << 2)) {
        SM750_WREG32(SM750_DISP_PANEL_VIDEO_DISP_CTRL, video_ctrl & ~(1 << 2));
    }
    /*
    debug_printf("SM750_ACC: H_Disp: %u, Virtual_W: %u, Pitch: %u\n", 
                 mode->timing.h_display, mode->virtual_width, pitch);
    debug_printf("SM750_ACC:  - BPP: %u, Pitch calcolato: %u bytes\n", bpp, pitch);
    */
    
    // 1. Programmiamo il Clock (PLL)
    //debug_printf("SM750_ACC: programmazione pll...\n");
    //debug_printf("SM750_ACC: preferred mode pixel clock is %u:\n",pm->timing.pixel_clock);
    //debug_printf("SM750_ACC: preferred mode richiesto a sm750_set_display_mode %u:\n",mode->timing.pixel_clock);
    //uint32 target_clock = mode->timing.pixel_clock;

    // Non è questo a generare rumore ma i 2 pll impostati diversamente e il ramo inusato di panel/crt attivo
    // 2. Se è il valore "GTF" per la 1280x1024, riportalo alla "Verità" VESA
    //if (target_clock == 107964 || (target_clock > 107900 && target_clock < 108100)) {
    //	debug_printf("SM750_ACC: Rilevato clock GTF (%u), forzo a 108000 VESA DMT\n", target_clock);
    //    mode->timing.pixel_clock = 108000;
    //}
    
    sm750_program_pll(mode->timing.pixel_clock, si->card_info.is_panel);
    //debug_printf("SM750_ACC: programmazione pll effettuata\n");
    bool isPanel = si->card_info.is_panel;
    
    bool h_pos = (mode->timing.flags & B_POSITIVE_HSYNC);
    bool v_pos = (mode->timing.flags & B_POSITIVE_VSYNC);
    
    sm750_set_h_timing(mode->timing.h_total, mode->timing.h_display, isPanel);
    sm750_set_h_sync(mode->timing.h_sync_start, mode->timing.h_sync_end, isPanel);
    sm750_set_v_timing(mode->timing.v_total, mode->timing.v_display, isPanel);
    sm750_set_v_sync(mode->timing.v_sync_start, mode->timing.v_sync_end, isPanel);
    /*
    debug_printf("SM750_ACC: Setup %s %dx%d (%d bpp)\n", isPanel ? "PANEL" : "CRT", mode->timing.h_display, mode->timing.v_display, bpp);
    debug_printf("SM750_DEBUG: H_Total: %d, H_Disp: %d, H_SyncStart: %d, H_SyncEnd: %d\n",
            mode->timing.h_total, mode->timing.h_display, 
            mode->timing.h_sync_start, mode->timing.h_sync_end);
    debug_printf("SM750_DEBUG: V_Total: %d, V_Disp: %d, V_SyncStart: %d, V_SyncEnd: %d\n",
            mode->timing.v_total, mode->timing.v_display, 
            mode->timing.v_sync_start, mode->timing.v_sync_end);
            */
    // Puntiamo allo 0 fisico (inizio memoria video)
    // S (Bit 31): Read-only status (Flip pending). Lo scriviamo a 0.
    // Ext (Bit 27): Memory Selection. Deve essere 0 per Local Memory.
    // Address (Bit 25:4): Indirizzo fisico allineato a 128-bit.
    // Bit 3:0: Hardwired a 0.
    // usiamo la funzione helper
    sm750_set_fb_addr(0, isPanel);
    // Pitch e Window Width (Registro 0x080208) - USA L'HELPER!
    sm750_set_pitch(pitch, isPanel);
    
    uint32 ctrl = 0;
    // Formato (Bit 1:0) -> 10 per 32bpp, 01 per 16bpp
    if (mode->space == B_RGB32) 
        ctrl |= 0x2; 
    else 
        ctrl |= 0x1;
    //Abilita il piano grafico (Bit 2)
    ctrl |= (1 << 2);
    //Abilita i Timings (Bit 8) -> FONDAMENTALE per non avere schermo nero
    ctrl |= (1 << 8);
    // 4. Blanking (Bit 10) -> Deve essere 0 per mostrare i pixel. Lo è già.
    // 5. Polarità Sync (Bit 12 HSP, Bit 13 VSP)
    // Nota: Il datasheet dice 0=High, 1=Low. 
    // Haiku B_POSITIVE_HSYNC significa che vogliamo High (quindi bit a 0).
    if (!h_pos) ctrl |= (1 << 12);
    if (!v_pos) ctrl |= (1 << 13);

    if (isPanel) {
    	// Registro di Controllo PANEL (0x080000)
        // Bit di alimentazione LCD
        ctrl |= (1 << 24) | (1 << 25) | (1 << 26) | (1 << 27); 
        //debug_printf("SM750_ACC: Scrittura sul registro di controllo di PANEL: 0x%08x\n", ctrl);
        SM750_WREG32(SM750_PANEL_CONTROL, ctrl);
        si->fbc.bytes_per_row = pitch;
        si->fbc.frame_buffer_dma = (void *)si->framebuffer_pci;
        debug_printf("SM750_ACC: --- FINE SETUP PANEL ---\n");
    } else {
        // Registro di Controllo CRT (0x080200)
        // Data Select (Bit 19:18) -> Vogliamo CRT Data (10)
        ctrl |= (2 << 18);
        // 7. VGA Data Shift (Bit 26) -> Di solito 0 (Enable)
        // ctrl |= (0 << 26);
        //debug_printf("SM750_ACC: Scrittura sul registro di controllo del CRT: 0x%08x\n", ctrl);
        SM750_WREG32(SM750_CRT_CONTROL, ctrl);
        si->fbc2.bytes_per_row = pitch;
        si->fbc2.frame_buffer_dma = (void *)si->framebuffer_pci;
    	debug_printf("SM750_ACC: --- FINE SETUP CRT ---\n");
    }
    

    si->dm = *mode;
    sm750_init_2d_engine(&(si->dm));
    
    return B_OK;
}

status_t
sm750_get_frame_buffer_config(frame_buffer_config *config)
{
    shared_info *si = gInfo->si;
    // Calcolo BPP al volo per evitare ridondanze in shared_info
    //uint32 bpp = 0;
    //switch (si->preferred_mode.space) {
    //    case B_RGB32: case B_RGBA32: bpp = 32; break;
    //    case B_RGB16: bpp = 16; break;
    //    default: bpp = 8; break;
    //}
    
    config->frame_buffer = (void *)gInfo->framebuffer; //usiamo il locale
    //config->frame_buffer = (void *)si->framebuffer; //ma noi avevamo messo NULL
    //config->frame_buffer_dma = (void *)si->framebuffer_pci;
    //config->bytes_per_row = si->dm.timing.h_display * (bpp / 8);
    
    if (si->card_info.is_panel) {
        config->frame_buffer_dma = si->fbc.frame_buffer_dma;
        config->bytes_per_row = si->fbc.bytes_per_row;
    } else {
        config->frame_buffer_dma = si->fbc2.frame_buffer_dma;
        config->bytes_per_row = si->fbc2.bytes_per_row;
    }

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
}*/

uint32 sm750_accelerant_mode_count(void) {
    return gInfo->si->mode_count;
}
/* for test
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
}*/
status_t 
sm750_get_mode_list(display_mode* dm) 
{
    //if (gInfo->si->mode_count == 0) return B_ERROR;
    shared_info *si = gInfo->si;
    
    //debug_printf("SM750_ACC: get_mode_list chiamata. mode_count = %u\n", si->mode_count);
    
    if (si->mode_count == 0 || gInfo->mode_list == NULL) {
        debug_printf("SM750_ACC: ERROR - Nessun modo trovato!\n");
        return B_ERROR;
    }
    // Vediamo cosa c'è nel primo modo prima di copiare
    /*
    debug_printf("SM750_ACC: Modo[0] prima di memcpy: %ux%u @ %.2f Hz, Clock: %u\n", 
        gInfo->mode_list[0].timing.h_display, 
        gInfo->mode_list[0].timing.v_display,
        (float)gInfo->mode_list[0].timing.pixel_clock * 1000 / 
        ((uint32)gInfo->mode_list[0].timing.h_total * gInfo->mode_list[0].timing.v_total),
        gInfo->mode_list[0].timing.pixel_clock);
        */
    
    // Copiamo la nostra lista precostruita nell'array passato da Haiku
    memcpy(dm, gInfo->mode_list, si->mode_count * sizeof(display_mode));
    //debug_printf("SM750_ACC: Memcpy effettuata con successo.\n");
    return B_OK;
}

status_t
sm750_propose_display_mode(display_mode *target, const display_mode *low, const display_mode *high)
{
	debug_printf("SM750_ACC: chiamata a sm750_propose_display_mode");
    // 1. Forza uno spazio colore supportato
    if (target->space != B_RGB32 && target->space != B_RGB16 && target->space != B_CMAP8)
        target->space = B_RGB32;

    // 2. Allineamento larghezza (SM750 richiede 8 o 16 pixel per il pitch)
    target->virtual_width = (target->virtual_width + 15) & ~15;
    
    if (target->virtual_width < target->timing.h_display)
        target->virtual_width = target->timing.h_display;
    if (target->virtual_height < target->timing.v_display)
        target->virtual_height = target->timing.v_display;
        
    // 2.1 Verifica rispetto ai limiti low/high
    if (target->virtual_width < low->virtual_width)
        target->virtual_width = low->virtual_width;
    if (target->virtual_width > high->virtual_width)
        target->virtual_width = high->virtual_width;
        
    // Ri-verifica l'allineamento dopo il clamping (potrebbe essere necessario)
    target->virtual_width &= ~15;
    
    // Se dopo il clamping la larghezza è diventata inferiore al minimo richiesto dall'hardware
    // o dalla riga di scansione (timing.h_display), allora la modalità è impossibile.
    if (target->virtual_width < target->timing.h_display)
         return B_BAD_VALUE;

    // 3. Controllo memoria video
    uint32 bytesPerPixel = 0;
    switch (target->space) {
        case B_RGB32: bytesPerPixel = 4; break;
        case B_RGB16: bytesPerPixel = 2; break;
        case B_CMAP8: bytesPerPixel = 1; break;
    }
    
    uint32 memNeeded = target->virtual_width * target->virtual_height * bytesPerPixel;
    // Il Desktop NON deve invadere l'area dell'heap (gli ultimi 4MB)
    // Usiamo 12MB come limite invalicabile per il frame buffer primario

    if (memNeeded > gInfo->si->card_info.max_desktop_mem) {
        debug_printf("SM750: Modalità rifiutata - serve %u byte, limite desktop %u\n", 
                  memNeeded, gInfo->si->card_info.max_desktop_mem);
    return B_BAD_VALUE;
    }

    // 4. Limite Pixel Clock (SM750: circa 300MHz per il DAC)
    // pixel_clock è in kHz, quindi 300000 kHz = 300 MHz
    if (target->timing.pixel_clock > 300000)
        return B_BAD_VALUE;

    // 5. Controllo finale: se il target è fuori dai limiti assoluti passati
    // (A volte utile come check di sicurezza finale)
    if (target->timing.pixel_clock < low->timing.pixel_clock 
        || target->timing.pixel_clock > high->timing.pixel_clock)
        return B_BAD_VALUE;

    return B_OK;
}

status_t sm750_set_indexed_colors(uint32 count, uint8 first, uint8 *colors, uint32 flags) {
    // Non supportiamo gli 8-bit per ora, ma rispondiamo OK per far felice Haiku
    return B_OK;
}
// da rivedere questi valori
status_t
sm750_get_timing_constraints(display_timing_constraints *dtc)
{
    // Allineamento orizzontale: SMI richiede multipli di 8 pixel
    dtc->h_res = 8;
    
    // Sync orizzontale (in pixel)
    dtc->h_sync_min = 8;
    dtc->h_sync_max = 4096; // Valore arbitrario alto
    
    // Blanking orizzontale (in pixel)
    dtc->h_blank_min = 8;
    dtc->h_blank_max = 4096;
    
    // Allineamento verticale: di riga in riga
    dtc->v_res = 1;
    
    // Sync verticale (in linee)
    dtc->v_sync_min = 1;
    dtc->v_sync_max = 2048;
    
    // Blanking verticale (in linee)
    dtc->v_blank_min = 1;
    dtc->v_blank_max = 2048;

    return B_OK;
}
