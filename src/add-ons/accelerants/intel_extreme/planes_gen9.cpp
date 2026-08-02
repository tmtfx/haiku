/*
 * Copyright 2026, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Supporto Hardware Planes (Gen9+ Skylake / Gemini Lake / Ice Lake)
 */

#include "accelerant.h"
#include "accelerant_protos.h"
#include <graphic_driver.h>
#include <debug.h>

#include "Pipes.h"

// --- REGISTRI PLANE SKL/GLK/ICL (Pipe A / Plane 1) ---
// Per Pipe B/C o altri Plane si applicano gli offset relativi (+0x100 per plane, +0x1000 per pipe)
#define SKL_PLANE_CTL_1_A           0x70180
#define SKL_PLANE_STRIDE_1_A        0x70188
#define SKL_PLANE_POS_1_A           0x7018C
#define SKL_PLANE_SIZE_1_A          0x70190
#define SKL_PLANE_SURF_1_A          0x7019C
#define SKL_PLANE_OFFSET_1_A        0x701A4

// Control Bits per PLANE_CTL
#define PLANE_ENABLE                (1 << 31)
#define PLANE_FORMAT_YUV420_8BPC    (0xC << 24) // NV12
#define PLANE_FORMAT_RGB_8888       (0x4 << 24)
#define PLANE_YUV_RANGE_CORRECTION  (1 << 21)

// Macro base per Skylake/GLK/ICL Planes
#define SKL_PLANE_CTL_BASE(pipe)      (0x70180 + ((pipe) * 0x1000))
#define SKL_PLANE_STRIDE_BASE(pipe)   (0x70188 + ((pipe) * 0x1000))
#define SKL_PLANE_POS_BASE(pipe)      (0x7018C + ((pipe) * 0x1000))
#define SKL_PLANE_SIZE_BASE(pipe)     (0x70190 + ((pipe) * 0x1000))
#define SKL_PLANE_SURF_BASE(pipe)     (0x7019C + ((pipe) * 0x1000))
#define SKL_PLANE_OFFSET_BASE(pipe)   (0x701A4 + ((pipe) * 0x1000))

// --- CONFIGURAZIONE REGISTRI PLANE MODERN (SKL / GLK / ICL) ---
status_t
gen9_configure_overlay(overlay_token overlayToken,
    const overlay_buffer* buffer, const overlay_window* window,
    const overlay_view* view)
{
    if (buffer == NULL || window == NULL || view == NULL)
        return B_BAD_VALUE;

    // Recuperiamo la pipe attiva (default Pipe A = 0 se non specificata)
    uint32 pipeIndex = 0; // o gInfo->shared_info->active_pipe

    uint32 planeCtl = PLANE_ENABLE;
    switch (buffer->space) {
        case B_RGB32:
            planeCtl |= PLANE_FORMAT_RGB_8888;
            break;
        case B_YCbCr422:
        default:
            planeCtl |= PLANE_FORMAT_YUV420_8BPC | PLANE_YUV_RANGE_CORRECTION;
            break;
    }

    uint32 pos = (window->v_start << 16) | (window->h_start & 0xFFFF);
    uint32 size = ((window->height - 1) << 16) | ((window->width - 1) & 0xFFFF);
    uint32 stride = buffer->bytes_per_row / 64; // Gen9 misura lo stride in blocchi da 64 byte

    // Scrittura sui registri calcolati per la Pipe corretta
    write32(SKL_PLANE_CTL_BASE(pipeIndex), planeCtl);
    write32(SKL_PLANE_STRIDE_BASE(pipeIndex), stride);
    write32(SKL_PLANE_POS_BASE(pipeIndex), pos);
    write32(SKL_PLANE_SIZE_BASE(pipeIndex), size);
    write32(SKL_PLANE_OFFSET_BASE(pipeIndex), 0);

    // SURFACE REGISTER TRIGGER:
    // Questa scrittura fa il latch hardware al VBLANK successivo
    struct overlay* overlay = (struct overlay*)buffer;
    write32(SKL_PLANE_SURF_BASE(pipeIndex), overlay->buffer_offset);

    return B_OK;
}

// --- PROTOTIPI INTERNI PER HOOKS ---

uint32
gen9_overlay_count(const display_mode* mode)
{
    // Per ora ritorniamo 1 se vogliamo abilitare il driver sui plane moderni,
    // oppure 0 se siamo in fase di testing/fallback.
    return 1;
}

const uint32*
gen9_overlay_supported_spaces(const display_mode* mode)
{
    static const uint32 spaces[] = {
        B_YCbCr422,
        B_RGB32,
        0
    };
    return spaces;
}

// --- CONSTRAINTS GEOMETRICI PER GEN9+ PLANES ---
status_t
gen9_get_overlay_constraints(const display_mode* mode,
    const overlay_buffer* buffer, overlay_constraints* constraints)
{
    if (buffer == NULL || constraints == NULL)
        return B_BAD_VALUE;

    // Vincoli per Plane SKL/GLK/ICL:
    // Allineamento indirizzo memoria a 4KB (page boundary)
    constraints->view.h_alignment = 2;
    constraints->view.v_alignment = 1;
    constraints->view.width_alignment = 2;
    constraints->view.height_alignment = 1;

    // Limiti di risoluzione sorgente (fino a 4096 per Gen9+)
    constraints->view.min_width = 16;
    constraints->view.min_height = 16;
    constraints->view.max_width = 4096;
    constraints->view.max_height = 4096;

    // Finestra di destinazione sul display
    constraints->window.h_alignment = 1;
    constraints->window.v_alignment = 1;
    constraints->window.width_alignment = 1;
    constraints->window.height_alignment = 1;

    constraints->window.min_width = 16;
    constraints->window.min_height = 16;
    constraints->window.max_width = 4096;
    constraints->window.max_height = 4096;

    // Fattori di scala gestiti dallo Scaler Hardware dedicato dei Plane
    constraints->h_scale.min = 0.125f;  // Downscale max 8x
    constraints->h_scale.max = 3.0f;    // Upscale max 3x
    constraints->v_scale.min = 0.125f;
    constraints->v_scale.max = 3.0f;

    return B_OK;
}

// --- ALLOCAZIONE BUFFER OVERLAY / PLANE ---
status_t
gen9_allocate_overlay_buffer(color_space colorSpace, uint16 width,
    uint16 height, overlay_buffer* buffer)
{
    if (buffer == NULL)
        return B_BAD_VALUE;

    // Calcolo bytes_per_row basato sul formato (allineato a 64 byte per il Ring/Plane)
    uint32 bytesPerPixel = (colorSpace == B_RGB32) ? 4 : 2;
    uint32 bytesPerRow = (width * bytesPerPixel + 63) & ~63;

    size_t size = bytesPerRow * height;

    // Allocazione nella memoria grafica condivisa
    // NOTA: Usa la funzione di allocazione gInfo / sharedInfo del tuo driver
    buffer->space = colorSpace;
    buffer->width = width;
    buffer->height = height;
    buffer->bytes_per_row = bytesPerRow;

    // TODO: Assegna offset di memoria ring/framebuffer
    // buffer->buffer_offset = allocated_offset;

    return B_OK;
}

status_t
gen9_release_overlay_buffer(const overlay_buffer* buffer)
{
    if (buffer == NULL)
        return B_BAD_VALUE;

    // Libera il buffer allocato precedentemente
    return B_OK;
}

// --- GESTIONE OVERLAY SESSION ---
status_t
gen9_allocate_overlay(overlay_token* overlayToken)
{
    if (overlayToken == NULL)
        return B_BAD_VALUE;

    *overlayToken = (overlay_token)1; // Token arbitrario per il Plane 1
    return B_OK;
}

status_t
gen9_release_overlay(overlay_token overlayToken)
{
    // Spegne il plane e libera il token
    write32(SKL_PLANE_CTL_1_A, 0); // Disabilita Plane
    write32(SKL_PLANE_SURF_1_A, 0); // Flush
    return B_OK;
}
