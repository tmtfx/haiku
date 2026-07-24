/*
 * Copyright 1992-2003, Alan Hourihane. All rights reserved.
 * Copyright 2026, Gemini CLI. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Alan Hourihane <alanh@fairlite.demon.co.uk>
 *		Fabio Tomat <f.t.public@gmail.com>
 *		Gemini CLI <gemini-cli@google.com>
 */


#include "accel.h"
#include "trident_regs.h"
#include <string.h>
#include <video_overlay.h>
#include <unistd.h>


#define MAX_OVERLAY_BUFFERS 4

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
	(void)dm;
	return 1;
}


const uint32*
trident_overlay_supported_spaces(const display_mode* dm)
{
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
	(void)a_color_space;
	return B_OVERLAY_COLOR_KEY
		| B_OVERLAY_HORIZONTAL_FILTERING
		| B_OVERLAY_VERTICAL_FILTERING;
}


const overlay_buffer*
trident_allocate_overlay_buffer(color_space cs, uint16 width, uint16 height)
{
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
	sOverlayToken++;
	return (overlay_token)(addr_t)sOverlayToken;
}


status_t
trident_release_overlay(overlay_token ot)
{
	(void)ot;

	debug_printf("Trident_OVL: trident_release_overlay called\n");

	// Re-enable MMIO decoder via kernel ioctl (since standard VGA writes may have disabled it)
	ioctl(gInfo.deviceFileDesc, TRIDENT_ENABLE_MMIO);

	// Ensure CRTC registers remain unlocked with MMIO active (CR39 = 0x87)
	//write_crtc_reg_logged("PCIReg", 0x39, 0x87);
	write_crtc_reg(PCIReg, 0x87);

	// Disable Trident video overlay (BES)
	uint8 cr70 = read_crtc_reg(0x70); // 0x70
	cr70 &= ~0x01; // Disable BES overlay
	write_crtc_reg_logged("BESControl", 0x70, cr70); // 0x70

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
	//write_crtc_reg_logged("PCIReg", 0x39, 0x87);
	write_crtc_reg(PCIReg, 0x87);
	

	if (!ob || !ow || !ov) {
		// Disable BES overlay
		uint8 cr70 = read_crtc_reg(0x70);
		cr70 &= ~0x01;
		write_crtc_reg_logged("BESControl", 0x70, cr70);
		return B_OK;
	}

	SharedInfo& si = *gInfo.sharedInfo;
	uint32 buffer_offset = (uint32)((addr_t)ob->buffer - (addr_t)si.videoMemAddr);

	// Set overlay buffer address (expressed in double words)
	uint32 dword_addr = buffer_offset / 4;
	write_crtc_reg_logged("OverlayAddrLow", 0x7C, dword_addr & 0xFF);
	write_crtc_reg_logged("OverlayAddrMid", 0x7D, (dword_addr >> 8) & 0xFF);
	write_crtc_reg_logged("OverlayAddrHigh", 0x7E, (dword_addr >> 16) & 0xFF);

	// Write coordinates to CRTC extension registers
	write_crtc_reg_logged("HStartLow", 0x71, ow->h_start & 0xFF);
	write_crtc_reg_logged("HStartHigh", 0x72, (ow->h_start >> 8) & 0xFF);
	write_crtc_reg_logged("VStartLow", 0x73, ow->v_start & 0xFF);
	write_crtc_reg_logged("VStartHigh", 0x74, (ow->v_start >> 8) & 0xFF);

	uint16 right = ow->h_start + ow->width;
	uint16 bottom = ow->v_start + ow->height;
	write_crtc_reg_logged("HEndLow", 0x75, right & 0xFF);
	write_crtc_reg_logged("HEndHigh", 0x76, (right >> 8) & 0xFF);
	write_crtc_reg_logged("VEndLow", 0x77, bottom & 0xFF);
	write_crtc_reg_logged("VEndHigh", 0x78, (bottom >> 8) & 0xFF);

	// Enable video overlay (BES bit 0 = 1, YUV format select bit 1 = 1)
	uint8 cr70 = read_crtc_reg(0x70);
	cr70 |= 0x03;
	write_crtc_reg_logged("BESControl", 0x70, cr70);

	return B_OK;
}

} // extern "C"
