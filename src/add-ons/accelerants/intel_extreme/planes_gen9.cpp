/*
 * Copyright 2026, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Supporto Hardware Planes Unificato (Gen9+ Skylake / Gemini Lake / Ice Lake)
 */

#include "accelerant.h"
#include "accelerant_protos.h"
#include <graphic_driver.h>
#include <Debug.h>
#include <cstring>
#include <SupportDefs.h>

#include "Pipes.h"

// --- REGISTRI BASE PLANE SKL/GLK/ICL ---
#define SKL_PLANE_CTL_1_A            0x70180
#define SKL_PLANE_STRIDE_1_A         0x70188
#define SKL_PLANE_POS_1_A            0x7018C
#define SKL_PLANE_SIZE_1_A           0x70190
#define SKL_PLANE_SURF_1_A           0x7019C
#define SKL_PLANE_OFFSET_1_A         0x701A4

// Control Bits per PLANE_CTL
#define PLANE_ENABLE                 (1u << 31)
#define PLANE_FORMAT_RGB_8888        (0x4u << 24)
#define PLANE_FORMAT_YUV420_8BPC     (0xCu << 24) // NV12 / YUV420
#define PLANE_YUV_RANGE_CORRECTION   (1u << 21)
#define PLANE_CTL_TILED_LINEAR       (0u << 10)   // Linear Framebuffer
#define SKL_PLANE_CTL_ALPHA_DISABLE  (1u << 4)    // Forza 100% opaco su RGB

// Macro per calcolare l'offset del Plane (Plane 1 = GUI, Plane 2 = Overlay/Sprite)
#define SKL_PLANE_OFFSET(pipe, plane) \
    (((pipe) * 0x1000) + (((plane) - 1) * 0x100))

#define SKL_PLANE_CTL_REG(pipe, plane)     (SKL_PLANE_CTL_1_A + SKL_PLANE_OFFSET(pipe, plane))
#define SKL_PLANE_STRIDE_REG(pipe, plane)  (SKL_PLANE_STRIDE_1_A + SKL_PLANE_OFFSET(pipe, plane))
#define SKL_PLANE_POS_REG(pipe, plane)     (SKL_PLANE_POS_1_A + SKL_PLANE_OFFSET(pipe, plane))
#define SKL_PLANE_SIZE_REG(pipe, plane)    (SKL_PLANE_SIZE_1_A + SKL_PLANE_OFFSET(pipe, plane))
#define SKL_PLANE_SURF_REG(pipe, plane)    (SKL_PLANE_SURF_1_A + SKL_PLANE_OFFSET(pipe, plane))
#define SKL_PLANE_OFF_REG(pipe, plane)     (SKL_PLANE_OFFSET_1_A + SKL_PLANE_OFFSET(pipe, plane))
// --- CONFIGURAZIONE REGISTRI PLANE MODERN ---
#define SKL_PLANE_COLOR_CTL_REG(pipe, plane)  (0x701CC + (plane - 1) * 0x100 + pipe * 0x1000)
#define SKL_PLANE_BUF_CFG_REG(pipe, plane)    (0x7017C + (plane - 1) * 0x4 + pipe * 0x1000)

extern "C" {

status_t
gen9_configure_overlay(overlay_token overlayToken,
	const overlay_buffer* buffer, const overlay_window* window,
	const overlay_view* view)
{
	if (buffer == NULL || window == NULL || view == NULL)
		return B_BAD_VALUE;

	uintptr_t tokenVal = (uintptr_t)overlayToken;
	if (tokenVal == 0)
		return B_BAD_VALUE;

	// Plane 2 (Universal Plane/Overlay) su Pipe A (0)
	uint32 planeIndex = (uint32)tokenVal + 1; 
	// Choose the first enabled pipe (array index maps to pipe 0=A,1=B,...)
	uint32 pipeIndex = 0;
	for (uint32 _p = 0; _p < gInfo->pipe_count; _p++) {
		if (gInfo->pipes[_p] && gInfo->pipes[_p]->IsEnabled()) {
			pipeIndex = _p;
			break;
		}
	}

	// 1. CONFIGURAZIONE PLANE_CTL
	// Start with alpha/linear defaults but DO NOT enable yet — enable after programming SURF/OFF
	uint32 planeCtl = SKL_PLANE_CTL_ALPHA_DISABLE | PLANE_CTL_TILED_LINEAR;

	switch (buffer->space) {
		case B_RGB32:
			// Direct Color / RGB 8888 (xRGB/RGBA)
			planeCtl |= PLANE_FORMAT_RGB_8888;
			break;

		case B_YCbCr420:
		case B_YCbCr422:
		default:
			// YUV420 8bpc with range correction
			planeCtl |= PLANE_FORMAT_YUV420_8BPC | PLANE_YUV_RANGE_CORRECTION;
			break;
	}

	// 2. CONFIGURAZIONE GEOMETRIA (POS & SIZE)
	// POS: Y (v_start) nei 16 bit ALTI, X (h_start) nei 16 bit BASSI
	uint32 pos = ((uint32)window->v_start << 16) | ((uint32)window->h_start & 0xFFFF);

	// SIZE: Height - 1 nei 16 bit ALTI, Width - 1 nei 16 bit BASSI
	uint32 size = (((uint32)window->height - 1) << 16) | (((uint32)window->width - 1) & 0xFFFF);

	// 3. CALCOLO STRIDE (Unità di 64 byte per Gen9)
	uint32 strideInUnits = (buffer->bytes_per_row + 63) / 64;

	// 4. CALCOLO OFFSET MEMORIA MMIO
	// Use physical offset relative to physical_graphics_memory for SURF/OFF
	uint32 physOffset = (addr_t)buffer->buffer_dma - (addr_t)gInfo->shared_info->physical_graphics_memory;

	// 5. INDIRIZZI DEI REGISTRI MMIO
	uint32 regCtl    = SKL_PLANE_CTL_REG(pipeIndex, planeIndex);
	uint32 regStride = SKL_PLANE_STRIDE_REG(pipeIndex, planeIndex);
	uint32 regPos    = SKL_PLANE_POS_REG(pipeIndex, planeIndex);
	uint32 regSize   = SKL_PLANE_SIZE_REG(pipeIndex, planeIndex);
	uint32 regOff    = SKL_PLANE_OFF_REG(pipeIndex, planeIndex);
	uint32 regSurf   = SKL_PLANE_SURF_REG(pipeIndex, planeIndex);


	// Log diagnostico scannabile: dump valori prima
	uint32 oldCtl = read32(SKL_PLANE_CTL_REG(pipeIndex, planeIndex));
	uint32 oldStride = read32(SKL_PLANE_STRIDE_REG(pipeIndex, planeIndex));
	uint32 oldPos = read32(SKL_PLANE_POS_REG(pipeIndex, planeIndex));
	uint32 oldSize = read32(SKL_PLANE_SIZE_REG(pipeIndex, planeIndex));
	uint32 oldOff = read32(SKL_PLANE_OFF_REG(pipeIndex, planeIndex));
	uint32 oldSurf = read32(SKL_PLANE_SURF_REG(pipeIndex, planeIndex));
	uint32 oldBufCfg = read32(SKL_PLANE_BUF_CFG_REG(pipeIndex, planeIndex));
	uint32 oldColor = read32(SKL_PLANE_COLOR_CTL_REG(pipeIndex, planeIndex));

	debug_printf("\n[Gen9+ Overlay] >>> CONFIGURING PLANE %" PRIu32 " on pipe %" PRIu32 " <<<", planeIndex, pipeIndex);
	debug_printf("\n[Gen9+ Overlay] OLD CTL: 0x%08" PRIx32 " STRIDE: %" PRIu32 " POS: 0x%08" PRIx32 " SIZE: 0x%08" PRIx32, oldCtl, oldStride, oldPos, oldSize);
	debug_printf("\n[Gen9+ Overlay] OLD OFF: 0x%08" PRIx32 " SURF: 0x%08" PRIx32 " BUF_CFG: 0x%08" PRIx32 " COLOR_CTL: 0x%08" PRIx32, oldOff, oldSurf, oldBufCfg, oldColor);

	debug_printf("\n[Gen9+ Overlay] NEW CTL (pending): 0x%08" PRIx32 " | POS: 0x%08" PRIx32 " | SIZE: 0x%08" PRIx32, planeCtl, pos, size);
	debug_printf("\n[Gen9+ Overlay] STRIDE (units): %" PRIu32 " | SURF physOffset: 0x%08" PRIx32 "\n", strideInUnits, physOffset);

	// 6. SCRITTURA ORDINATA NEI REGISTRI
	// Program non-latched registers first
	write32(regStride, strideInUnits);
	write32(regPos, pos);
	write32(regSize, size);
	// regOff = low 12 bits offset within page, regSurf = page frame number (>>12)
	write32(regOff, physOffset & 0xFFF);

	// Configure buffer/colour control registers explicitly (safe defaults)
	write32(SKL_PLANE_BUF_CFG_REG(pipeIndex, planeIndex), 0);
	write32(SKL_PLANE_COLOR_CTL_REG(pipeIndex, planeIndex), 0);

	// Barrier di memoria hardware per garantire il completamento delle scritture prima del trigger
	asm volatile("mfence" ::: "memory");

	// SCRITTURA FINALE: SURF fa scattare l'update al prossimo VBLANK (Latch Hardware)
	write32(regSurf, physOffset >> 12);

	// Ensure SURF write is visible before enabling the plane
	asm volatile("mfence" ::: "memory");

	// Abilita la plane (ENABLE = 1) solo ora
	write32(regCtl, planeCtl | PLANE_ENABLE);

	// Log diagnostico scannabile: dump valori dopo
	uint32 newCtl = read32(SKL_PLANE_CTL_REG(pipeIndex, planeIndex));
	uint32 newStride = read32(SKL_PLANE_STRIDE_REG(pipeIndex, planeIndex));
	uint32 newPos = read32(SKL_PLANE_POS_REG(pipeIndex, planeIndex));
	uint32 newSize = read32(SKL_PLANE_SIZE_REG(pipeIndex, planeIndex));
	uint32 newOff = read32(SKL_PLANE_OFF_REG(pipeIndex, planeIndex));
	uint32 newSurf = read32(SKL_PLANE_SURF_REG(pipeIndex, planeIndex));
	uint32 newBufCfg = read32(SKL_PLANE_BUF_CFG_REG(pipeIndex, planeIndex));
	uint32 newColor = read32(SKL_PLANE_COLOR_CTL_REG(pipeIndex, planeIndex));

	debug_printf("\n[Gen9+ Overlay] POST CTL: 0x%08" PRIx32 " STRIDE: %" PRIu32 " POS: 0x%08" PRIx32 " SIZE: 0x%08" PRIx32, newCtl, newStride, newPos, newSize);
	debug_printf("\n[Gen9+ Overlay] POST OFF: 0x%08" PRIx32 " SURF: 0x%08" PRIx32 " BUF_CFG: 0x%08" PRIx32 " COLOR_CTL: 0x%08" PRIx32 "\n", newOff, newSurf, newBufCfg, newColor);

	return B_OK;
}


// --- ALLOCAZIONE BUFFER OVERLAY / PLANE ---
const overlay_buffer*
gen9_allocate_overlay_buffer(color_space space, uint16 width, uint16 height)
{
    uint32 bytesPerPixel = (space == B_RGB32) ? 4 : 2;
    uint32 bytesPerRow = (width * bytesPerPixel + 63) & ~63; 
    size_t size = (bytesPerRow * height + 4095) & ~4095;

    struct overlay* overlay = (struct overlay*)calloc(1, sizeof(struct overlay));
    if (overlay == NULL)
        return NULL;

    status_t status = intel_allocate_memory(size, 0, overlay->buffer_base);
    if (status < B_OK) {
        free(overlay);
        return NULL;
    }

    overlay->buffer_offset = overlay->buffer_base 
        - (addr_t)gInfo->shared_info->graphics_memory;

    overlay->buffer.space = space;
    overlay->buffer.width = width;
    overlay->buffer.height = height;
    overlay->buffer.bytes_per_row = bytesPerRow;
    overlay->buffer.buffer = (uint8*)overlay->buffer_base;
    overlay->buffer.buffer_dma = (uint8*)gInfo->shared_info->physical_graphics_memory 
        + overlay->buffer_offset;

    return &overlay->buffer;
}

status_t
gen9_release_overlay_buffer(const overlay_buffer* buffer)
{
    if (buffer == NULL || buffer->buffer == NULL)
        return B_BAD_VALUE;

    intel_free_memory((addr_t)buffer->buffer);
    return B_OK;
}


// --- GESTIONE OVERLAY SESSION ---
overlay_token
gen9_allocate_overlay(void)
{
    for (uint32 i = 0; i < 4; i++) {
        uint32 mask = (1u << i);
        uint32 prev = atomic_or(&gInfo->shared_info->overlay_channel_used, mask);
        if ((prev & mask) == 0) {
            return (overlay_token)(uintptr_t)(i + 1);
        }
    }
    return NULL;
}
status_t
gen9_release_overlay(overlay_token overlayToken)
{
	if (overlayToken == 0)
		return B_BAD_VALUE;

	uintptr_t t = (uintptr_t)overlayToken;
	
	// Presumendo che i token partano da 1 (es. Channel 0 -> Token 1):
	uint32 idx = (uint32)(t - 1);
	uint32 pipeIndex = 0;
	for (uint32 _p = 0; _p < gInfo->pipe_count; _p++) {
		if (gInfo->pipes[_p] && gInfo->pipes[_p]->IsEnabled()) {
			pipeIndex = _p;
			break;
		}
	}
	uint32 planeIndex = idx + 2; // Plane 2, 3...

	debug_printf("\n[Gen9+ Overlay] >>> RELEASING OVERLAY (Token %" PRIuPTR " -> Plane %" PRIu32 ") <<<\n", t, planeIndex);

	uint32 regCtl  = SKL_PLANE_CTL_REG(pipeIndex, planeIndex);
	uint32 regSurf = SKL_PLANE_SURF_REG(pipeIndex, planeIndex);

	// 1. Spegni il Plane (ENABLE = 0)
	write32(regCtl, 0);

	// 2. Memory Barrier
	asm volatile("mfence" ::: "memory");

	// 3. Latch hardware al VBLANK
	write32(regSurf, 0);

	// 4. Libera la maschera atomica del canale
	uint32 mask = ~(1u << idx);
	atomic_and(&gInfo->shared_info->overlay_channel_used, mask);

	return B_OK;
}


// --- HOOKS & CONSTRAINTS ---
uint32
gen9_overlay_count(const display_mode* mode)
{
    return 4;
}

const uint32*
gen9_overlay_supported_spaces(const display_mode* mode)
{
    static const uint32 kSupportedSpaces[] = {
        B_YCbCr422,
        B_RGB32,
        0
    };
    return kSupportedSpaces;
}

uint32
gen9_overlay_supported_features(uint32 colorSpace)
{
    return B_OVERLAY_COLOR_KEY
        | B_OVERLAY_HORIZONTAL_FILTERING
        | B_OVERLAY_VERTICAL_FILTERING
        | B_OVERLAY_HORIZONTAL_MIRRORING;
}

status_t
gen9_get_overlay_constraints(const display_mode* mode,
    const overlay_buffer* buffer, overlay_constraints* constraints)
{
    if (buffer == NULL || constraints == NULL)
        return B_BAD_VALUE;

    constraints->view.h_alignment = 2;
    constraints->view.v_alignment = 1;
    constraints->view.width_alignment = 2;
    constraints->view.height_alignment = 1;

    constraints->view.width.min = 16;
    constraints->view.height.min = 16;
    constraints->view.width.max = 4096;
    constraints->view.height.max = 4096;

    constraints->window.h_alignment = 1;
    constraints->window.v_alignment = 1;
    constraints->window.width_alignment = 1;
    constraints->window.height_alignment = 1;

    constraints->window.width.min = 16;
    constraints->window.height.min = 16;
    constraints->window.width.max = 4096;
    constraints->window.height.max = 4096;

    constraints->h_scale.min = 0.125f;
    constraints->h_scale.max = 3.0f;
    constraints->v_scale.min = 0.125f;
    constraints->v_scale.max = 3.0f;

    return B_OK;
}

} // extern "C"
