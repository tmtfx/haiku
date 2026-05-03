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

extern accelerant_info *gInfo;

// --- HOOK 1: Quanti layer overlay abbiamo? ---
uint32 
sm750_overlay_count(const display_mode *dm)
{
    // La SM750 ha 2 layer totali: Layer 1 (Desktop) e Layer 2 (Video/Overlay).
    // Quindi restituiamo 1 (un solo layer video disponibile).
    return 1;
}

// --- HOOK 2: Quali formati colore supportiamo? ---
const uint32 *
sm750_overlay_supported_spaces(const display_mode *dm)
{
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

void
sm750_configure_overlay(const overlay_window *window, const overlay_buffer *buffer)
{
    shared_info *si = gInfo->si;
    vuint32 *regs = gInfo->regs;

    // 1. Indirizzo del Buffer (Offset VRAM)
    // Usiamo l'offset salvato in buffer_dma durante l'allocazione
    uint32 bufferOffset = (uint32)(addr_t)buffer->buffer_dma;
    SM750_WREG32(SM750_VIDEO_FB_0_ADDR, bufferOffset & 0x03FFFFF0);
    
    // 2. Larghezza (Pitch) in byte
    SM750_WREG32(SM750_VIDEO_FB_WIDTH, (buffer->bytes_per_row << 16) | buffer->bytes_per_row);

    // 3. Posizione sullo schermo (Top-Left e Bottom-Right)
    uint32 topLeft = (window->width.start << 16) | (window->height.start & 0xFFFF);
    uint32 bottomRight = (window->width.end << 16) | (window->height.end & 0xFFFF);
    SM750_WREG32(SM750_VIDEO_PLANE_TL, topLeft);
    SM750_WREG32(SM750_VIDEO_PLANE_BR, bottomRight);

    // 4. Configurazione del Control Register (0x080040)
    uint32 control = SM750_RREG32(SM750_VIDEO_DISPLAY_CTRL);
    
    control &= ~0x00000003; // Pulisce i bit formato
    control |= 0x03;        // Imposta YUYV (11b)
    control |= (1 << 2);    // Enable Video Plane
    control |= (1 << 18);   // Enable Line Buffer
    control |= (1 << 9) | (1 << 8); // Enable Interpolation (Smooth scaling)
    
    // Assicuriamoci che il byte swapping sia coerente con Haiku
    control &= ~(1 << 12);  // 0 = YUYV

    SM750_WREG32(SM750_VIDEO_DISPLAY_CTRL, control);
}
