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
    // Usiamo direttamente la struct overlay passata nel buffer
    struct overlay* overlay = (struct overlay*)buffer;
    write32(SKL_PLANE_SURF_BASE(pipeIndex), overlay->buffer_offset);

    return B_OK;
}

// --- PROTOTIPI INTERNI PER HOOKS ---

uint32
gen9_overlay_count(const display_mode* mode)
{
    // Supportare fino a 4 hardware planes per Gen9+ (IceLake / GeminiLake / Skylake)
    // Questo permette ai player/video di usare più overlay simultanei.
    return 4;
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

    // Creiamo la struttura di overlay e allociamo memoria grafica
    struct overlay* overlay = (struct overlay*)malloc(sizeof(struct overlay));
    if (overlay == NULL)
        return B_NO_MEMORY;

    overlay->buffer.space = colorSpace;
    overlay->buffer.width = width;
    overlay->buffer.height = height;
    overlay->buffer.bytes_per_row = bytesPerRow;

    status_t status = intel_allocate_memory(size, 0, overlay->buffer_base);
    if (status < B_OK) {
        free(overlay);
        return status;
    }

    overlay->buffer_offset = overlay->buffer_base
        - (addr_t)gInfo->shared_info->graphics_memory;

    overlay->buffer.buffer = (uint8*)overlay->buffer_base;
    overlay->buffer.buffer_dma = (uint8*)gInfo->shared_info->physical_graphics_memory
        + overlay->buffer_offset;

    // Copia i dati nella struttura fornita dal chiamante
    *buffer = overlay->buffer;

    // Non memorizzare globalmente l'overlay: il buffer passato viene usato
    // direttamente dal chiamante quando richiesto.

    return B_OK;
}

status_t
gen9_release_overlay_buffer(const overlay_buffer* buffer)
{
    if (buffer == NULL)
        return B_BAD_VALUE;

    // Il puntatore passato è in realtà un struct overlay allocato da allocate_overlay_buffer
    struct overlay* overlay = (struct overlay*)buffer;

    if (overlay->buffer_base != 0)
        intel_free_memory(overlay->buffer_base);

    // Non toccare gInfo->current_overlay: non lo usiamo per la gestione multi-overlay
    free(overlay);

    return B_OK;
}

// --- GESTIONE OVERLAY SESSION ---
status_t
gen9_allocate_overlay(overlay_token* overlayToken)
{
    if (overlayToken == NULL)
        return B_BAD_VALUE;

    // cerchiamo uno slot libero tra 4 overlay (bitmask su shared_info->overlay_channel_used)
    for (uint32 i = 0; i < 4; i++) {
        uint32 mask = (1u << i);
        uint32 prev = atomic_or(&gInfo->shared_info->overlay_channel_used, mask);
        if ((prev & mask) == 0) {
            // abbiamo prenotato lo slot i
            *overlayToken = (overlay_token)(uintptr_t)(i + 1);
            // aggiorniamo il contatore token come nelle implementazioni legacy
            ++gInfo->shared_info->overlay_token;
            return B_OK;
        }
    }

    // nessuno slot libero
    return B_BUSY;
}

status_t
gen9_release_overlay(overlay_token overlayToken)
{
    // Validazione token e rilascio corrispondente slot
    if (overlayToken == 0)
        return B_BAD_VALUE;

    uintptr_t t = (uintptr_t)overlayToken;
    if (t == 0 || t > 4)
        return B_BAD_VALUE;

    uint32 idx = (uint32)(t - 1);
    uint32 mask = ~(1u << idx);

    // Disabilita il plane hardware relativo
    write32(SKL_PLANE_CTL_1_A + idx * 0x100, 0);
    write32(SKL_PLANE_SURF_1_A + idx * 0x100, 0);

    // reset overlay state similmente a legacy
    memset(&gInfo->last_overlay_view, 0, sizeof(gInfo->last_overlay_view));
    memset(&gInfo->last_overlay_window, 0, sizeof(gInfo->last_overlay_window));
    gInfo->last_vertical_overlay_scale = 0;
    gInfo->last_horizontal_overlay_scale = 0;

    atomic_and(&gInfo->shared_info->overlay_channel_used, mask);

    return B_OK;
}
