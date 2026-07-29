/*
	Copyright 2026, Haiku, Inc. All Rights Reserved.
	Distributed under the terms of the MIT License.

	Authors:
		Fabio Tomat 2026
		Gemini CLI 2026
*/

#include "accel.h"
#include <string.h>
#include <video_overlay.h>
#include <unistd.h>

#define MAX_OVERLAY_BUFFERS 4

struct s3_overlay_buffer {
	overlay_buffer	buffer;
	bool			allocated;
	uint32			offset;
};

static s3_overlay_buffer sOverlayBuffers[MAX_OVERLAY_BUFFERS];
static int32 sOverlayToken = 0;

extern "C" {

uint32
S3_OverlayCount(const display_mode* dm)
{
	(void)dm;
	// Streams processor is not supported on original Trio64
	if (gInfo.sharedInfo->chipType == S3_TRIO64)
		return 0;
	return 1;
}

const uint32*
S3_OverlaySpacesSupported(const display_mode* dm)
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
S3_OverlaySupportedFeatures(uint32 a_color_space)
{
	(void)a_color_space;
	return B_OVERLAY_COLOR_KEY
		| B_OVERLAY_HORIZONTAL_FILTERING
		| B_OVERLAY_VERTICAL_FILTERING;
}

const overlay_buffer*
S3_AllocateOverlayBuffer(color_space cs, uint16 width, uint16 height)
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
S3_ReleaseOverlayBuffer(const overlay_buffer* ob)
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
S3_GetOverlayConstraints(const display_mode* dm, const overlay_buffer* ob,
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
S3_AllocateOverlay(void)
{
	sOverlayToken++;
	return (overlay_token)(addr_t)sOverlayToken;
}

status_t
S3_ReleaseOverlay(overlay_token ot)
{
	(void)ot;

	// Disable overlay/streams blend
	WriteReg32(0x81a0, 0x01000000); // disable secondary stream overlay blend
	WriteReg32(0x8190, 0x00000000); // disable secondary stream control

	return B_OK;
}

status_t
S3_ConfigureOverlay(overlay_token ot, const overlay_buffer* ob,
	const overlay_window* ow, const overlay_view* ov)
{
	(void)ot;

	if (!ob || !ow || !ov) {
		// Disable secondary stream (overlay)
		WriteReg32(0x81a0, 0x01000000); // disable secondary stream overlay blend
		WriteReg32(0x8190, 0x00000000); // disable secondary stream control
		return B_OK;
	}

	SharedInfo& si = *gInfo.sharedInfo;
	uint32 buffer_offset = (uint32)((addr_t)ob->buffer - (addr_t)si.videoMemAddr);

	// Initialize streams processor parameters for primary and secondary stream
	uint32 pst_wind = (si.displayMode.virtual_width - 1) << 16 | (si.displayMode.virtual_height);

	// Setup primary stream
	WriteReg32(0x8180, 0x05000000 & 0x77000000); // prim_stream_cntl
	WriteReg32(0x81c8, (si.displayMode.bytesPerRow) & 0x0fff); // prim_stream_stride
	WriteReg32(0x81f0, 0x00010001);              // prim_start_coord
	WriteReg32(0x81f4, pst_wind & 0x07ff07ff);    // prim_window_size

	// Setup format and filtering
	uint32 formatBits = 0x01000000; // default to YUV 4:2:2 (YUY2) as in Xorg s3_video.c
	if (ob->space == B_RGB16)
		formatBits = 0x05000000;
	else if (ob->space == B_RGB15)
		formatBits = 0x03000000;

	uint32 filterBits = (ow->width == ov->width) ? 0 : 2;

	uint32 sstream_cntl = (filterBits << 28) | formatBits | ((((ov->width - 1) << 1) - (ow->width - 1)) & 0xfff);
	uint32 sstretch = ((ov->width - 1) & 0x7ff) | (((ov->width - ow->width) & 0x7ff) << 16);

	// Update secondary stream registers
	WriteReg32(0x8190, sstream_cntl);             // second_stream_cntl
	WriteReg32(0x8198, sstretch);                 // second_stream_stretch
	WriteReg32(0x81d0, buffer_offset & 0x3fffff); // second_fbaddr0
	WriteReg32(0x81d8, ob->bytes_per_row & 0xfff);// second_stream_stride

	WriteReg32(0x81e0, ov->height - 1);           // k1
	WriteReg32(0x81e4, (ov->height - ow->height) & 0x7ff); // k2
	WriteReg32(0x81e8, ((~ow->height) - 1) & 0xfff);       // dda_vert

	uint32 start_coord = (((ow->h_start + 1) << 16) | (ow->v_start + 1));
	uint32 window_size = (((ow->width - 1) << 16) | ow->height) & 0x7ff07ff;

	WriteReg32(0x81f8, start_coord);              // second_start_coord
	WriteReg32(0x81fc, window_size);              // second_window_size

	// Set chroma keying
	uint8 red = 0, green = 0, blue = 0;
	switch (si.displayMode.space) {
		case B_RGB15:
			red = ow->red.value << 3;
			green = ow->green.value << 3;
			blue = ow->blue.value << 3;
			break;
		case B_RGB16:
			red = ow->red.value << 3;
			green = ow->green.value << 2;
			blue = ow->blue.value << 3;
			break;
		default:
			red = ow->red.value;
			green = ow->green.value;
			blue = ow->blue.value;
			break;
	}
	uint32 chromaKey = 0x17000000 | (red << 16) | (green << 8) | blue;

	WriteReg32(0x8184, chromaKey);                // col_chroma_key_cntl

	// Enable secondary stream overlay blending (0x05000000)
	WriteReg32(0x81a0, 0x05000000);              // blend_cntl
	WriteReg32(0x81dc, 0x40000000);              // opaq_overlay_cntl

	// Ensure STREAMS processor is enabled in CR67.
	uint8 cr67 = ReadCrtcReg(0x67);
	if ((cr67 & 0x0c) != 0x0c) {
		WriteCrtcReg(0x67, cr67 | 0x0c);
	}

	return B_OK;
}

} // extern "C"
