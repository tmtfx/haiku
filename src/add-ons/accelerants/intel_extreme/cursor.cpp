/*
 * Copyright 2006-2008, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 */


#include "accelerant_protos.h"
#include "accelerant.h"

#include <string.h>


status_t
intel_set_cursor_shape(uint16 width, uint16 height, uint16 hotX, uint16 hotY,
	uint8* andMask, uint8* xorMask)
{
	if (width > 64 || height > 64)
		return B_BAD_VALUE;

	write32(INTEL_CURSOR_CONTROL, 0);
		// disable cursor

	// In two-color mode, the data is ordered as follows (always 64 bit per
	// line):
	//	plane 1: line 0 (AND mask)
	//	plane 0: line 0 (XOR mask)
	//	plane 1: line 1 (AND mask)
	//	...
	//
	// If the planes add to the value 0x2, the corresponding pixel is
	// transparent, for 0x3 it inverts the background, so only the first
	// two palette entries will be used (since we're using the 2 color mode).

	uint8* data = gInfo->shared_info->cursor_memory;
	uint8 byteWidth = (width + 7) / 8;

	for (int32 y = 0; y < height; y++) {
		for (int32 x = 0; x < byteWidth; x++) {
			data[16 * y + x] = andMask[byteWidth * y + x];
			data[16 * y + x + 8] = xorMask[byteWidth * y + x];
		}
	}

	// set palette entries to white/black
	write32(INTEL_CURSOR_PALETTE + 0, 0x00ffffff);
	write32(INTEL_CURSOR_PALETTE + 4, 0);

	gInfo->shared_info->cursor_format = CURSOR_FORMAT_2_COLORS;

	write32(INTEL_CURSOR_CONTROL,
		CURSOR_ENABLED | gInfo->shared_info->cursor_format);
	write32(INTEL_CURSOR_SIZE, height << 12 | width);

	write32(INTEL_CURSOR_BASE,
		(uint32)gInfo->shared_info->physical_graphics_memory
		+ gInfo->shared_info->cursor_buffer_offset);

	// changing the hot point changes the cursor position, too

	if (hotX != gInfo->shared_info->cursor_hot_x
		|| hotY != gInfo->shared_info->cursor_hot_y) {
		int32 x = read32(INTEL_CURSOR_POSITION);
		int32 y = x >> 16;
		x &= 0xffff;
		
		if (x & CURSOR_POSITION_NEGATIVE)
			x = -(x & CURSOR_POSITION_MASK);
		if (y & CURSOR_POSITION_NEGATIVE)
			y = -(y & CURSOR_POSITION_MASK);

		x += gInfo->shared_info->cursor_hot_x;
		y += gInfo->shared_info->cursor_hot_y;

		gInfo->shared_info->cursor_hot_x = hotX;
		gInfo->shared_info->cursor_hot_y = hotY;

		intel_move_cursor(x, y);
	}

	return B_OK;
}


uint32
intel_get_cursor_bits(void)
{
	if (gInfo->shared_info->cursor_memory == NULL){
		debug_printf("Intel Extreme Accelerant: No cursor memory, assigning 0 bits\n");
		return 0;
	}

	if (gInfo->shared_info->device_type.Generation() >= 4) {
		debug_printf("Intel Extreme Accelerant: Cursor Color Depth 32-bit\n");
		return 32;
	}

	debug_printf("Intel Extreme Accelerant: Cursor Color Depth 1-bit\n");
	return 1;
}


status_t
intel_set_cursor_bitmap(uint16 width, uint16 height, uint16 hotX, uint16 hotY,
	color_space colorSpace, uint16 bytesPerRow, const uint8* bitmapData)
{
	if (gInfo->shared_info->device_type.Generation() < 4) {
		debug_printf("Intel Extreme Accelerant: Cursor Bitmap unavailable, device generation %d\n",
			gInfo->shared_info->device_type.Generation());
		return B_UNSUPPORTED;
	}

	if (gInfo->shared_info->cursor_memory == NULL || bitmapData == NULL)
		return B_NO_INIT;

	debug_printf("Intel Extreme Accelerant: Cursor Bitmap as 32-bit\n");

	// The Intel hardware cursor supports up to 64x64.
	if (width == 0 || height == 0 || width > 64 || height > 64)
		return B_BAD_VALUE;

	// We support Haiku's 32-bit cursor formats.
	if (colorSpace != B_RGBA32 && colorSpace != B_RGB32)
		return B_BAD_TYPE;

	if (bytesPerRow < width * 4)
		return B_BAD_VALUE;

	// Disable the cursor before touching the backing store.
	write32(INTEL_CURSOR_CONTROL, 0);

	uint32* dest = (uint32*)gInfo->shared_info->cursor_memory;
	const uint32* src = (const uint32*)bitmapData;
	uint32 srcPixelsPerRow = bytesPerRow / 4;

	// Clear the whole 64x64 buffer to avoid garbage when the cursor is smaller.
	memset(dest, 0, 64 * 64 * sizeof(uint32));

	// Copy into the 64-pixel-wide hardware layout.
	for (int32 y = 0; y < height; y++) {
		for (int32 x = 0; x < width; x++) {
			dest[64 * y + x] = src[srcPixelsPerRow * y + x];
		}
	}

	// Switch cursor to ARGB.
	gInfo->shared_info->cursor_format = CURSOR_FORMAT_ARGB;

	write32(INTEL_CURSOR_CONTROL,
		CURSOR_ENABLED | gInfo->shared_info->cursor_format);
	write32(INTEL_CURSOR_SIZE, (height << 12) | width);

	write32(INTEL_CURSOR_BASE,
		(uint32)gInfo->shared_info->physical_graphics_memory
		+ gInfo->shared_info->cursor_buffer_offset);

	// Changing the hot point changes the cursor position.
	if (hotX != gInfo->shared_info->cursor_hot_x
		|| hotY != gInfo->shared_info->cursor_hot_y) {
		int32 x = read32(INTEL_CURSOR_POSITION);
		int32 y = x >> 16;
		x &= 0xffff;

		if (x & CURSOR_POSITION_NEGATIVE)
			x = -(x & CURSOR_POSITION_MASK);
		if (y & CURSOR_POSITION_NEGATIVE)
			y = -(y & CURSOR_POSITION_MASK);

		x += gInfo->shared_info->cursor_hot_x;
		y += gInfo->shared_info->cursor_hot_y;

		gInfo->shared_info->cursor_hot_x = hotX;
		gInfo->shared_info->cursor_hot_y = hotY;

		intel_move_cursor(x, y);
	}

	return B_OK;
}


void
intel_move_cursor(uint16 _x, uint16 _y)
{
	int32 x = (int32)_x - gInfo->shared_info->cursor_hot_x;
	int32 y = (int32)_y - gInfo->shared_info->cursor_hot_y;

	if (x < 0)
		x = -x | CURSOR_POSITION_NEGATIVE;
	if (y < 0)
		y = -y | CURSOR_POSITION_NEGATIVE;

	write32(INTEL_CURSOR_POSITION, (y << 16) | x);
}


void
intel_show_cursor(bool isVisible)
{
	if (gInfo->shared_info->cursor_visible == isVisible)
		return;

	write32(INTEL_CURSOR_CONTROL, (isVisible ? CURSOR_ENABLED : 0)
		| gInfo->shared_info->cursor_format);
	write32(INTEL_CURSOR_BASE,
		(uint32)gInfo->shared_info->physical_graphics_memory
		+ gInfo->shared_info->cursor_buffer_offset);

	gInfo->shared_info->cursor_visible = isVisible;
}
