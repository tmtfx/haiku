/*
 * Copyright 1992-2003, Alan Hourihane. All rights reserved.
 * Copyright 2026, Gemini CLI. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Alan Hourihane <alanh@fairlite.demon.co.uk>
 *		Fabio Tomat <f.t.public@gmail.com>
 *		Gemini CLI <gemini-cli@google.com>
 *
 * Due to missing specs it was impossible to handle correct overlay regs
 * thus overlay is disabled in hooks
 */


#include "accel.h"
#include <string.h>
#include <video_overlay.h>
#include <unistd.h>


#define MAX_OVERLAY_BUFFERS 4

#define CALLED() debug_printf("Trident_OVL: CALLED %s\n", __FUNCTION__)

struct trident_overlay_buffer {
	overlay_buffer	buffer;
	bool			allocated;
	uint32			offset;
};

static trident_overlay_buffer sOverlayBuffers[MAX_OVERLAY_BUFFERS];
static int32 sOverlayToken = 0;


// Helper logging function for CRTC overlay register writes
static inline void
write_crtc_reg_logged(const char* name, uint8 index, uint8 value)
{
	uint8 old_val = read_crtc_reg(index);
	write_crtc_reg(index, value);
	uint8 new_val = read_crtc_reg(index);
	debug_printf("Trident_OVL: CR%02X (%s) Old=0x%02X, Write=0x%02X, Readback=0x%02X\n",
		index, name, old_val, value, new_val);
}


extern "C" {

uint32
trident_overlay_count(const display_mode* dm)
{
	CALLED();
	(void)dm;
	return 1;
}


const uint32*
trident_overlay_supported_spaces(const display_mode* dm)
{
	CALLED();

	(void)dm;
	static const uint32 kSupportedSpaces[] = {
		B_YCbCr422,
		B_RGB16,
		B_RGB15,
		0
	};
	return kSupportedSpaces;
}


uint32
trident_overlay_supported_features(uint32 a_color_space)
{
	CALLED();

	(void)a_color_space;
	return B_OVERLAY_COLOR_KEY
		| B_OVERLAY_HORIZONTAL_FILTERING
		| B_OVERLAY_VERTICAL_FILTERING;
}


const overlay_buffer*
trident_allocate_overlay_buffer(color_space cs, uint16 width, uint16 height)
{
	CALLED();

	SharedInfo& si = *gInfo.sharedInfo;
	uint32 bytesPerPixel = 2;

	if (cs == B_RGB32)
		bytesPerPixel = 4;
	else if (cs == B_RGB16 || cs == B_RGB15)
		bytesPerPixel = 2;
	else if (cs == B_YCbCr422)
		bytesPerPixel = 2;

	uint32 pitch = (width * bytesPerPixel + 15) & ~15;
	uint32 size = (pitch * height + 15) & ~15;

	// Base of overlay area in video memory (after frame buffer display area)
	uint32 overlayBase = (si.displayMode.bytesPerRow * si.displayMode.virtual_height + 4095) & ~4095;
	uint32 overlayLimit = si.cursorOffset;

	int slot = -1;
	for (int i = 0; i < MAX_OVERLAY_BUFFERS; i++) {
		if (!sOverlayBuffers[i].allocated) {
			slot = i;
			break;
		}
	}
	if (slot == -1)
		return NULL;

	// Simple sequential allocation checking occupied blocks
	uint32 highestOffset = overlayBase;
	for (int i = 0; i < MAX_OVERLAY_BUFFERS; i++) {
		if (sOverlayBuffers[i].allocated) {
			uint32 end = sOverlayBuffers[i].offset + sOverlayBuffers[i].buffer.bytes_per_row * sOverlayBuffers[i].buffer.height;
			if (end > highestOffset) {
				highestOffset = (end + 4095) & ~4095;
			}
		}
	}

	if (highestOffset + size > overlayLimit) {
		return NULL; // Out of video memory
	}

	sOverlayBuffers[slot].allocated = true;
	sOverlayBuffers[slot].offset = highestOffset;
	sOverlayBuffers[slot].buffer.space = cs;
	sOverlayBuffers[slot].buffer.width = width;
	sOverlayBuffers[slot].buffer.height = height;
	sOverlayBuffers[slot].buffer.bytes_per_row = pitch;
	sOverlayBuffers[slot].buffer.buffer = (void*)((addr_t)si.videoMemAddr + highestOffset);
	sOverlayBuffers[slot].buffer.buffer_dma = (void*)((addr_t)si.videoMemPCI + highestOffset);

	return &sOverlayBuffers[slot].buffer;
}


status_t
trident_release_overlay_buffer(const overlay_buffer* ob)
{
	CALLED();

	if (!ob)
		return B_BAD_VALUE;

	for (int i = 0; i < MAX_OVERLAY_BUFFERS; i++) {
		if (sOverlayBuffers[i].allocated && &sOverlayBuffers[i].buffer == ob) {
			sOverlayBuffers[i].allocated = false;
			return B_OK;
		}
	}
	return B_BAD_VALUE;
}


status_t
trident_get_overlay_constraints(const display_mode* dm, const overlay_buffer* ob,
	overlay_constraints* oc)
{
	CALLED();

	if (!dm || !ob || !oc)
		return B_BAD_VALUE;

	oc->view.width_alignment = 7;
	oc->view.height_alignment = 0;
	oc->window.width_alignment = 7;
	oc->window.height_alignment = 0;

	oc->view.width.min = 32;
	oc->view.width.max = ob->width;
	oc->view.height.min = 32;
	oc->view.height.max = ob->height;

	oc->window.width.min = 32;
	oc->window.width.max = dm->virtual_width;
	oc->window.height.min = 32;
	oc->window.height.max = dm->virtual_height;

	oc->h_scale.min = 1.0f / 8.0f;
	oc->h_scale.max = 8.0f;
	oc->v_scale.min = 1.0f / 8.0f;
	oc->v_scale.max = 8.0f;

	return B_OK;
}


overlay_token
trident_allocate_overlay(void)
{
	CALLED();

	sOverlayToken++;
	return (overlay_token)(addr_t)sOverlayToken;
}


status_t
trident_release_overlay(overlay_token ot)
{
	CALLED();

	(void)ot;

	// Re-enable MMIO decoder via kernel ioctl (since standard VGA writes may have disabled it)
	ioctl(gInfo.deviceFileDesc, TRIDENT_ENABLE_MMIO);

	// Ensure CRTC registers remain unlocked with MMIO active (CR39 = 0x87)
	write_crtc_reg_logged("PCIReg", 0x39, 0x87);

	// Disable Trident video overlay (BES control register CR8E = 0x00)
	write_crtc_reg_logged("BESControl", 0x8E, 0x00);

	return B_OK;
}


status_t
trident_configure_overlay(overlay_token ot, const overlay_buffer* ob,
	const overlay_window* ow, const overlay_view* ov)
{
	(void)ot;
	(void)ov;

	debug_printf("Trident_OVL: trident_configure_overlay started. Ob=0x%" B_PRIXADDR "\n", (addr_t)ob);

	// Re-enable MMIO decoder via kernel ioctl (since standard VGA writes may have disabled it)
	ioctl(gInfo.deviceFileDesc, TRIDENT_ENABLE_MMIO);

	// Ensure CRTC registers remain unlocked with MMIO active (CR39 = 0x87)
	write_crtc_reg_logged("PCIReg", 0x39, 0x87);

	if (!ob || !ow || !ov) {
		// Disable BES overlay
		write_crtc_reg_logged("BESControl", 0x8E, 0x00);
		return B_OK;
	}

	SharedInfo& si = *gInfo.sharedInfo;
	uint32 buffer_offset = (uint32)((addr_t)ob->buffer - (addr_t)si.videoMemAddr);

	// 1. Write buffer address/offset to registers CR92, CR93, CR94 (expressed in bytes)
	write_crtc_reg_logged("OverlayAddrLow", 0x92, buffer_offset & 0xFF);
	write_crtc_reg_logged("OverlayAddrMid", 0x93, (buffer_offset >> 8) & 0xFF);
	write_crtc_reg_logged("OverlayAddrHigh", 0x94, (buffer_offset >> 16) & 0x0F);

	// 2. Write active line width (pitch) to registers CR90, CR91 (expressed in bytes)
	write_crtc_reg_logged("OverlayPitchLow", 0x90, ob->bytes_per_row & 0xFF);
	write_crtc_reg_logged("OverlayPitchHigh", 0x91, (ob->bytes_per_row >> 8) & 0xFF);

	// 3. Write coordinates to CRTC extension registers (X1=CR86-87, Y1=CR88-89, X2=CR8A-8B, Y2=CR8C-8D)
	uint16 tx1 = ow->h_start;
	uint16 ty1 = ow->v_start;
	uint16 tx2 = ow->h_start + ow->width;
	uint16 ty2 = ow->v_start + ow->height;

	write_crtc_reg_logged("HStartLow", 0x86, tx1 & 0xFF);
	write_crtc_reg_logged("HStartHigh", 0x87, (tx1 >> 8) & 0xFF);
	write_crtc_reg_logged("VStartLow", 0x88, ty1 & 0xFF);
	write_crtc_reg_logged("VStartHigh", 0x89, (ty1 >> 8) & 0xFF);

	write_crtc_reg_logged("HEndLow", 0x8A, tx2 & 0xFF);
	write_crtc_reg_logged("HEndHigh", 0x8B, (tx2 >> 8) & 0xFF);
	write_crtc_reg_logged("VEndLow", 0x8C, ty2 & 0xFF);
	write_crtc_reg_logged("VEndHigh", 0x8D, (ty2 >> 8) & 0xFF);

	// 4. Reset zoom scaling registers CR80-CR83 to 0x00 (1:1 scale) for overlay stability
	write_crtc_reg_logged("HZoomLow", 0x80, 0x00);
	write_crtc_reg_logged("HZoomHigh", 0x81, 0x00);
	write_crtc_reg_logged("VZoomLow", 0x82, 0x00);
	write_crtc_reg_logged("VZoomHigh", 0x83, 0x00);

	// 5. Enable video overlay: write 0x94 to BES control register CR8E
	// Value 0x94 corresponds to the official X.org YUV/RGB overlay enable parameters
	write_crtc_reg_logged("BESControl", 0x8E, 0x94);

	return B_OK;
}

} // extern "C"
