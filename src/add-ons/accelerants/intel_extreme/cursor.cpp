/*
 * Copyright 2006-2009, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 */


#include "accelerant_protos.h"
#include "accelerant.h"

#include <inttypes.h>
#include <string.h>

#undef TRACE
//#define TRACE_CURSOR
#ifdef TRACE_CURSOR
#	define TRACE(x...) _sPrintf("intel_extreme cursor: " x)
#else
#	define TRACE(x...) do { } while (0)
#endif


struct cursor_registers {
	uint32 control;
	uint32 base;
	uint32 position;
	uint32 size;
	uint32 palette;
};

static cursor_registers sCursorRegs;
static bool sCursorRegsInitialized = false;
static pipe_index sCursorPipe = INTEL_PIPE_ANY;


static addr_t
pipe_offset(pipe_index pipe)
{
	// Cursor registers use a 0x40 stride between pipes (A:+0x00, B:+0x40, ...).
	if (pipe <= INTEL_PIPE_A)
		return 0;

	return 0x40 * (pipe - INTEL_PIPE_A);
}


static bool
hardware_cursor_supported()
{
	// Some newer display engines (DDI platforms) need different cursor
	// programming; until implemented, prefer software cursor there.
	if (gInfo == NULL || gInfo->shared_info == NULL)
		return false;

	if (!gInfo->shared_info->hardware_cursor_enabled)
		return false;

	if (gInfo->shared_info->cursor_memory == NULL)
		return false;

	return !gInfo->shared_info->device_type.HasDDI();
}


static pipe_index
active_pipe()
{
	// Heuristic: prefer an enabled display plane/pipe.
	if ((read32(INTEL_DISPLAY_A_CONTROL) & DISPLAY_CONTROL_ENABLED) != 0)
		return INTEL_PIPE_A;
	if ((read32(INTEL_DISPLAY_B_CONTROL) & DISPLAY_CONTROL_ENABLED) != 0)
		return INTEL_PIPE_B;

	if ((read32(INTEL_DISPLAY_A_PIPE_CONTROL) & INTEL_PIPE_ENABLED) != 0)
		return INTEL_PIPE_A;
	if ((read32(INTEL_DISPLAY_B_PIPE_CONTROL) & INTEL_PIPE_ENABLED) != 0)
		return INTEL_PIPE_B;
	if ((read32(INTEL_DISPLAY_C_PIPE_CONTROL) & INTEL_PIPE_ENABLED) != 0)
		return INTEL_PIPE_C;

	return INTEL_PIPE_A;
}


static void
init_cursor_registers()
{
	pipe_index pipe = active_pipe();
	if (sCursorRegsInitialized && sCursorPipe == pipe)
		return;

	addr_t offset = pipe_offset(pipe);
	sCursorRegs.control = INTEL_CURSOR_CONTROL + offset;
	sCursorRegs.base = INTEL_CURSOR_BASE + offset;
	sCursorRegs.position = INTEL_CURSOR_POSITION + offset;
	sCursorRegs.size = INTEL_CURSOR_SIZE + offset;
	sCursorRegs.palette = INTEL_CURSOR_PALETTE + offset;

	sCursorPipe = pipe;
	sCursorRegsInitialized = true;

	TRACE("init: pipe=%d off=0x%" B_PRIxADDR "\n", (int)pipe, offset);
}


static void
post_cursor_writes()
{
	// Posting read to ensure register writes are committed.
	(void)read32(sCursorRegs.control);
}


static bool
cursor_uses_mode_bits()
{
	// Gen5+ uses a cursor "mode" value (disables when mode==0).
	return gInfo->shared_info->device_type.Generation() >= 5;
}


// Gen5+ mode values in bits [5:0] of CUR*CNTR.
static const uint32 kCursorModeDisable = 0x00;
static const uint32 kCursorMode64TwoColor = 0x06;
static const uint32 kCursorMode64Argb = 0x27;


static uint32
cursor_control_value(bool visible)
{
	if (!visible)
		return cursor_uses_mode_bits() ? kCursorModeDisable : 0;

	if (cursor_uses_mode_bits())
		return gInfo->shared_info->cursor_format;

	return CURSOR_ENABLED | gInfo->shared_info->cursor_format;
}


static uint32
cursor_base_value()
{
	// Different generations interpret CUR*BASE differently.
	//
	// - On some older platforms, programming the physical address works.
	// - On newer (but still pre-DDI) platforms, an offset into the graphics
	//   aperture (GGTT address) is expected, same as primary planes.
	uint32 gen = gInfo->shared_info->device_type.Generation();

	if (gen >= 6)
		return gInfo->shared_info->cursor_buffer_offset;

	phys_addr_t physical = gInfo->shared_info->physical_cursor_memory;
	if (physical == 0)
		physical = gInfo->shared_info->physical_graphics_memory
			+ gInfo->shared_info->cursor_buffer_offset;

	if ((physical >> 32) != 0) {
		// If the address doesn't fit, fall back to the GGTT offset.
		return gInfo->shared_info->cursor_buffer_offset;
	}

	return (uint32)physical;
}


status_t
intel_set_cursor_shape(uint16 width, uint16 height, uint16 hotX, uint16 hotY,
	uint8* andMask, uint8* xorMask)
{
	if (!hardware_cursor_supported())
		return B_OK;

	if (width > 64 || height > 64)
		return B_BAD_VALUE;

	if (andMask == NULL || xorMask == NULL)
		return B_BAD_VALUE;

	init_cursor_registers();

	// Disable cursor before touching the backing store.
	write32(sCursorRegs.control, cursor_control_value(false));
	post_cursor_writes();

	// Two-color mode: 64-bit per line, plane 1 = AND, plane 0 = XOR.
	uint8* data = gInfo->shared_info->cursor_memory;
	uint8 byteWidth = (width + 7) / 8;

	for (int32 y = 0; y < height; y++) {
		for (int32 x = 0; x < byteWidth; x++) {
			data[16 * y + x] = andMask[byteWidth * y + x];
			data[16 * y + x + 8] = xorMask[byteWidth * y + x];
		}
	}

	// Palette entries: white/black.
	write32(sCursorRegs.palette + 0, 0x00ffffff);
	write32(sCursorRegs.palette + 4, 0);

	if (cursor_uses_mode_bits())
		gInfo->shared_info->cursor_format = kCursorMode64TwoColor;
	else
		gInfo->shared_info->cursor_format = CURSOR_FORMAT_2_COLORS;

	write32(sCursorRegs.size, (height << 12) | width);
	write32(sCursorRegs.base, cursor_base_value());
	write32(sCursorRegs.control,
		cursor_control_value(gInfo->shared_info->cursor_visible));
	post_cursor_writes();

	// Changing the hot point changes the cursor position.
	if (hotX != gInfo->shared_info->cursor_hot_x
		|| hotY != gInfo->shared_info->cursor_hot_y) {
		int32 x = read32(sCursorRegs.position);
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
	if (!hardware_cursor_supported())
		return 0;

	if (gInfo->shared_info->device_type.Generation() >= 4)
		return 32;

	return 1;
}


status_t
intel_set_cursor_bitmap(uint16 width, uint16 height, uint16 hotX, uint16 hotY,
	color_space colorSpace, uint16 bytesPerRow, const uint8* bitmapData)
{
	if (!hardware_cursor_supported())
		return B_OK;

	if (gInfo->shared_info->device_type.Generation() < 4)
		return B_UNSUPPORTED;

	if (width == 0 || height == 0 || width > 64 || height > 64)
		return B_BAD_VALUE;

	if (colorSpace != B_RGBA32 && colorSpace != B_RGB32)
		return B_BAD_TYPE;

	if (bytesPerRow < width * 4)
		return B_BAD_VALUE;

	if (bitmapData == NULL)
		return B_BAD_VALUE;

	init_cursor_registers();

	// Disable cursor before touching the backing store.
	write32(sCursorRegs.control, cursor_control_value(false));
	post_cursor_writes();

	uint32* dest = (uint32*)gInfo->shared_info->cursor_memory;
	const uint32* src = (const uint32*)bitmapData;
	uint32 srcPixelsPerRow = bytesPerRow / 4;

	// Clear the whole 64x64 buffer to avoid garbage when the cursor is smaller.
	memset(dest, 0, 64 * 64 * sizeof(uint32));

	for (int32 y = 0; y < height; y++) {
		for (int32 x = 0; x < width; x++) {
			uint32 pixel = src[srcPixelsPerRow * y + x];
			if (colorSpace == B_RGB32)
				pixel |= 0xff000000;
			dest[64 * y + x] = pixel;
		}
	}

	if (cursor_uses_mode_bits()) {
		gInfo->shared_info->cursor_format = kCursorMode64Argb;
	} else {
		gInfo->shared_info->cursor_format
			= (colorSpace == B_RGB32) ? CURSOR_FORMAT_XRGB : CURSOR_FORMAT_ARGB;
	}

	// ARGB cursor uses a 64x64 surface.
	write32(sCursorRegs.size, (64 << 12) | 64);
	write32(sCursorRegs.base, cursor_base_value());
	write32(sCursorRegs.control,
		cursor_control_value(gInfo->shared_info->cursor_visible));
	post_cursor_writes();

	// Changing the hot point changes the cursor position.
	if (hotX != gInfo->shared_info->cursor_hot_x
		|| hotY != gInfo->shared_info->cursor_hot_y) {
		int32 x = read32(sCursorRegs.position);
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
	if (!hardware_cursor_supported())
		return;

	init_cursor_registers();

	int32 x = (int32)_x - gInfo->shared_info->cursor_hot_x;
	int32 y = (int32)_y - gInfo->shared_info->cursor_hot_y;

	if (x < 0)
		x = -x | CURSOR_POSITION_NEGATIVE;
	if (y < 0)
		y = -y | CURSOR_POSITION_NEGATIVE;

	write32(sCursorRegs.position, (y << 16) | x);
	post_cursor_writes();
}


void
intel_show_cursor(bool isVisible)
{
	if (!hardware_cursor_supported())
		return;

	init_cursor_registers();

	if (gInfo->shared_info->cursor_visible == isVisible)
		return;

	// Some generations need rewriting the base to latch double-buffered state.
	write32(sCursorRegs.base, cursor_base_value());
	write32(sCursorRegs.control, cursor_control_value(isVisible));
	post_cursor_writes();

	gInfo->shared_info->cursor_visible = isVisible;
}
