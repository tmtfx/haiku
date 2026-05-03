/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include <string.h>
#include <malloc.h>
#include "sm750_macros.h"
#include "DriverInterface.h"
#include "protos.h"
#include "memory_manager.h"
#include "sm750_macros.h"

extern accelerant_info *gInfo;

#define CALLED() debug_printf("SM750_ACC: %s\n", __FUNCTION__)

static void
sm750_set_video_scale(const overlay_window *window, const overlay_buffer *buffer)
{
	CALLED();
    vuint32 *regs = gInfo->regs;
    
    uint32 srcW = buffer->width;
    uint32 srcH = buffer->height;
    
    // In Haiku overlay_window usiamo width e height direttamente
    uint32 destW = window->width;
    uint32 destH = window->height;

    uint32 hScaleValue, vScaleValue;
    uint32 hsBit = 0, vsBit = 0;

    // Scala Orizzontale
    if (destW >= srcW) {
        hsBit = 0; // Expansion
        hScaleValue = (uint32)(((float)srcW / destW) * 4096.0f);
    } else {
        hsBit = 1; // Shrinking
        hScaleValue = (uint32)(((float)destW / srcW) * 4096.0f);
    }

    // Scala Verticale
    if (destH >= srcH) {
        vsBit = 0; // Expansion
        vScaleValue = (uint32)(((float)srcH / destH) * 4096.0f);
    } else {
        vsBit = 1; // Shrinking
        vScaleValue = (uint32)(((float)destH / srcH) * 4096.0f);
    }

    // Componiamo il registro 0x080058
    // VS (bit 31), VScale (27:16), HS (bit 15), HScale (11:0)
    uint32 videoScale = ((vsBit & 0x1) << 31) | ((vScaleValue & 0xFFF) << 16) |
                        ((hsBit & 0x1) << 15) | (hScaleValue & 0xFFF);

    SM750_WREG32(SM750_DISP_PANEL_VIDEO_SCALE, videoScale);
}

// --- HOOK 1: Quanti layer overlay abbiamo? ---
uint32 
sm750_overlay_count(const display_mode *dm)
{
	CALLED();
    // La SM750 ha 2 layer totali: Layer 1 (Desktop) e Layer 2 (Video/Overlay).
    // Quindi restituiamo 1 (un solo layer video disponibile).
    return 1;
}

// --- HOOK 2: Quali formati colore supportiamo? ---
const uint32 *
sm750_overlay_supported_spaces(const display_mode *dm)
{
	CALLED();
    static const uint32 spaces[] = {
        B_YCbCr422,	// YUY2 (Il più comune)
        B_RGB16,	// RGB 5:6:5
        B_RGB15,	// RGB 5:5:5
        0
    };
    return spaces;
}

// --- HOOK 3: Capacità dello Scaler ---
void 
sm750_get_overlay_constraints(const display_mode *dm, const overlay_buffer *ob,
    overlay_constraints *oc)
{
	CALLED();
    // Rapporto di scala (SM750 supporta upscaling generoso)
    oc->view.width_alignment = 7;      // Allineamento 8 pixel
    oc->view.height_alignment = 0;
    
    // Dimensioni sorgente (il video originale)
    oc->view.width.min = 32;
    oc->view.width.max = 1920; 
    oc->view.height.min = 32;
    oc->view.height.max = 1080;

    // Dimensioni destinazione (sullo schermo)
    oc->window.width.min = 32;
    oc->window.width.max = dm->virtual_width;
    oc->window.height.min = 32;
    oc->window.height.max = dm->virtual_height;
    
    // Fattore di scala (Haiku usa 1/64k come unità)
    oc->h_scale.min = 1.0f / 8.0f; 
    oc->h_scale.max = 8.0f;
    oc->v_scale.min = 1.0f / 8.0f;
    oc->v_scale.max = 8.0f;
}

overlay_buffer *
sm750_allocate_overlay_buffer(color_space cs, uint16 width, uint16 height)
{
	CALLED();
    shared_info *si = gInfo->si;
    uint32 bytesPerPixel = 2; // Per YCbCr422
    uint32 size = width * height * bytesPerPixel;
    
    uint32 blockID;
    uint32 offset;

    // CHIAMATA AL MEMORY MANAGER (tramite la nostra libreria mem_mgr)
    // Usiamo un tag specifico 'OVER' per identificarlo
    status_t status = mem_alloc((mem_info*)si->mem_mgr, size, (void*)0x4F564552, 
                                &blockID, &offset);

    if (status != B_OK) return NULL;

    overlay_buffer *ob = (overlay_buffer *)malloc(sizeof(overlay_buffer));
    if (!ob) {
        // mem_free(si->mem_mgr, blockID); // Da implementare
        return NULL;
    }

    ob->space = cs;
    ob->width = width;
    ob->height = height;
    ob->bytes_per_row = width * bytesPerPixel;
    
    // Indirizzo virtuale per l'AppServer (per scrivere i frame video)
    ob->buffer = (void *)((addr_t)gInfo->framebuffer + offset);
    
    // Indirizzo "PCI" per i registri hardware (salviamo l'offset nel campo reserved)
    // Nota: Haiku usa buffer_dma per l'indirizzo fisico/PCI
    ob->buffer_dma = (void *)(addr_t)offset; 

    return ob;
}
// TODO DA RIVEDERE TOTALMENTE valori casuali
void
sm750_configure_overlay(const overlay_window *window, const overlay_buffer *buffer)
{
	CALLED();
    //shared_info *si = gInfo->si;
    vuint32 *regs = gInfo->regs;

    // 1. Indirizzo del Buffer (Offset VRAM)
    // Usiamo l'offset salvato in buffer_dma durante l'allocazione
    uint32 bufferOffset = (uint32)(addr_t)buffer->buffer_dma;
    SM750_WREG32(SM750_DISP_PANEL_VIDEO_FB0_ADDR, bufferOffset & 0x03FFFFF0);
    
    // 2. Larghezza (Pitch)
    uint32 pitchIn128BitUnits = buffer->bytes_per_row / 16;
    // Assicuriamoci che non superi i limiti dei bit (10 bit per campo)
    pitchIn128BitUnits &= 0x3FF;
    SM750_WREG32(SM750_DISP_PANEL_VIDEO_FB_WIDTH, (pitchIn128BitUnits << 20) | (pitchIn128BitUnits << 4));
    //SM750_WREG32(SM750_DISP_PANEL_VIDEO_FB_WIDTH, (buffer->bytes_per_row << 16) | buffer->bytes_per_row);

    // 3. Coordinate Finestra
    // TL: h_start (Left), v_start (Top)
    uint32 top = (uint32)window->v_start;
    uint32 left = (uint32)window->h_start;
    
    // BR: calcoliamo Bottom e Right
    uint32 bottom = top + window->height;
    uint32 right = left + window->width;

    uint32 topLeft = ((top & 0x7FF) << 16) | (left & 0x7FF);
    uint32 bottomRight = ((bottom & 0x7FF) << 16) | (right & 0x7FF);
    
    SM750_WREG32(SM750_DISP_PANEL_VIDEO_PL_TL_POS, topLeft);
    SM750_WREG32(SM750_DISP_PANEL_VIDEO_PL_BR_POS, bottomRight);
    
    // 4. Scaling Factor (0x58)
    // Calcolato con (src/dest) * 4096 (2^12) e impostazione bit HS/VS
    sm750_set_video_scale(window, buffer);
    
    // 5. Initial Scale (0x5C)
    // Impostiamo a 0 per iniziare il campionamento dall'origine del buffer
    SM750_WREG32(SM750_DISP_PANEL_VIDEO_INIT_SCALE, 0);
    
    // Inizializzazione costanti YUV (Color Space Conversion)
    // Se non hai i valori esatti del datasheet, usiamo un default comune per SM750
    // Spesso è 0x00(Y) 0x53(R) 0x15(G) 0x15(B) o simile.
    //SM750_WREG32(SM750_DISP_PANEL_VIDEO_YUV_CONST, 0x00531515);
    uint32 csc_video = SM750_REG32(SM750_DISP_PANEL_VIDEO_YUV_CONST);
    debug_printf("SM750_ACC: costanti YUV (Color Space Conversion) 0x%08x\n", csc_video);

    // 4. Configurazione del Control Register (0x080040)
    uint32 control = SM750_REG32(SM750_DISP_PANEL_VIDEO_DISP_CTRL);
    debug_printf("SM750_ACC: vecchio registro video control: 0x%08x\n", control);
    
    control = 0;

    // Formato YUYV (11b)
    control |= (3 & 0x3);

    // Abilitazione Video Plane
    control |= (1 << 2);

    // Abilitazione Interpolazione (Smooth scaling)
    control |= (1 << 9) | (1 << 8);

    // Abilitazione Line Buffer (Necessario per lo scaling)
    control |= (1 << 18);

    // FIFO Request Level: 11 (Massima priorità di riempimento)
    control |= (3 << 16);

    // Byte Swapping: 0 per YUYV, 1 per UYVY
    // Haiku B_YCbCr422 è solitamente Y0-U0-Y1-V0, quindi BS=0
    control &= ~(1 << 12);

    // Assicuriamoci che i Force Scale 1/2 siano spenti
    control &= ~((1 << 11) | (1 << 10));
    debug_printf("SM750_ACC: nuovo registro video control: 0x%08x\n", control);

    SM750_WREG32(SM750_DISP_PANEL_VIDEO_DISP_CTRL, control);
}
