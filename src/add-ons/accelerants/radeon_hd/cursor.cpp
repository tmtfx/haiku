#include "accelerant.h"
#include "accelerant_protos.h"
#include "accelerant.h"
#include "evergreen_reg.h"
#include <Accelerant.h>
#include <string.h>

// Helper macros to read/write registers provided by accelerant.h

uint32 radeon_get_cursor_bits(void)
{
	// Modern GPUs support true-color cursors with alpha
	return 32;
}

status_t radeon_set_cursor_bitmap(uint16 width, uint16 height, uint16 hotX, uint16 hotY,
	color_space colorSpace, uint16 bytesPerRow, const uint8* bitmapData)
{
	if (width > 64 || height > 64 || hotX >= width || hotY >= height)
		return B_ERROR;

	// Ensure cursor area allocated (use end of framebuffer)
	if (gInfo->cursor_vaddr == NULL) {
		uint64 fbSize = gInfo->fb.vramSize; // bytes
		uint64 cursorSize = 64 * 64 * 4; // ARGB32
		if (fbSize < cursorSize)
			return B_ERROR;

		uint64 offset = fbSize - cursorSize;
		// align to 4KB
		offset &= ~((uint64)0xfff);
		gInfo->cursor_fb_offset = (uint32)offset;
		gInfo->cursor_phys = (addr_t)gInfo->shared_info->frame_buffer_phys + gInfo->cursor_fb_offset;
		gInfo->cursor_vaddr = (uint8*)gInfo->shared_info->frame_buffer + gInfo->cursor_fb_offset;
	}

	// Clear full 64x64 area
	memset(gInfo->cursor_vaddr, 0, 64 * 64 * 4);

	// Copy and convert input bitmap into ARGB8888
	if (colorSpace != B_RGBA32 && colorSpace != B_RGB32)
		return B_ERROR;

	for (int y = 0; y < height && y < 64; y++) {
		const uint8* srcRow = bitmapData + y * bytesPerRow;
		uint32* dstRow = (uint32*)(gInfo->cursor_vaddr + y * 64 * 4);
		for (int x = 0; x < width && x < 64; x++) {
			const uint8* pixel = srcRow + x * 4;
			uint8 b = pixel[0];
			uint8 g = pixel[1];
			uint8 r = pixel[2];
			uint8 a = (colorSpace == B_RGBA32) ? pixel[3] : 0xFF;

			// If fully transparent skip (leave zero)
			if (a == 0)
				continue;

			uint32 packed = ((uint32)a << 24) | ((uint32)r << 16) | ((uint32)g << 8) | (uint32)b;
			dstRow[x] = packed;
		}
	}

	// Save metadata
	gInfo->cursor_width = width;
	gInfo->cursor_height = height;
	gInfo->cursor_hotx = hotX;
	gInfo->cursor_hoty = hotY;

	// Program registers
	// Surface address (mask as required)
	uint32 addr = (uint32)(gInfo->cursor_phys & EVERGREEN_CUR_SURFACE_ADDRESS_MASK);
	Write32(OUT, EVERGREEN_CUR_SURFACE_ADDRESS, addr);
	// High address if needed
	#ifdef EVERGREEN_CUR_SURFACE_ADDRESS_HIGH
	Write32(OUT, EVERGREEN_CUR_SURFACE_ADDRESS_HIGH, (uint32)(gInfo->cursor_phys >> 32));
	#endif

	// Size: width in low 16, height in high 16
	Write32(OUT, EVERGREEN_CUR_SIZE, ((uint32)gInfo->cursor_height << 16) | (uint32)gInfo->cursor_width);

	// Hot spot: low 16 = x, high 16 = y
	Write32(OUT, EVERGREEN_CUR_HOT_SPOT, ((uint32)gInfo->cursor_hoty << 16) | (uint32)gInfo->cursor_hotx);

	// Set control: enable, 24_8 unpremultiplied mode, request MC on
	uint32 control = EVERGREEN_CURSOR_EN | EVERGREEN_CURSOR_24_8_UNPRE_MULT | EVERGREEN_CURSOR_FORCE_MC_ON;
	Write32(OUT, EVERGREEN_CUR_CONTROL, control);

	// Trigger an update
	Write32(OUT, EVERGREEN_CUR_UPDATE, EVERGREEN_CURSOR_UPDATE_LOCK);
	// brief pause (no busy-wait)
	for (volatile int i = 0; i < 1000; i++) ;
	Write32(OUT, EVERGREEN_CUR_UPDATE, 0);

	return B_OK;
}

status_t radeon_set_cursor_shape(uint16 width, uint16 height, uint16 hotX, uint16 hotY,
	uint8* andMask, uint8* xorMask)
{
	if (width > 64 || height > 64 || hotX >= width || hotY >= height)
		return B_ERROR;

	// Ensure cursor area allocated
	if (gInfo->cursor_vaddr == NULL) {
		uint64 fbSize = gInfo->fb.vramSize; // bytes
		uint64 cursorSize = 64 * 64 * 4; // ARGB32
		if (fbSize < cursorSize)
			return B_ERROR;

		uint64 offset = fbSize - cursorSize;
		// align to 4KB
		offset &= ~((uint64)0xfff);
		gInfo->cursor_fb_offset = (uint32)offset;
		gInfo->cursor_phys = (addr_t)gInfo->shared_info->frame_buffer_phys + gInfo->cursor_fb_offset;
		gInfo->cursor_vaddr = (uint8*)gInfo->shared_info->frame_buffer + gInfo->cursor_fb_offset;
	}

	// Clear framebuffer cursor area
	memset(gInfo->cursor_vaddr, 0, 64 * 64 * 4);

	// Compute bytes per row in masks
	int maskBytesPerRow = (width + 7) / 8;

	for (int y = 0; y < height && y < 64; y++) {
		uint32* dstRow = (uint32*)(gInfo->cursor_vaddr + y * 64 * 4);
		const uint8* andRow = andMask + y * maskBytesPerRow;
		const uint8* xorRow = xorMask + y * maskBytesPerRow;
		for (int x = 0; x < width && x < 64; x++) {
			int byteIdx = x / 8;
			int bit = 7 - (x % 8);
			bool andBit = ((andRow[byteIdx] >> bit) & 0x1) != 0;
			bool xorBit = ((xorRow[byteIdx] >> bit) & 0x1) != 0;

			if (andBit) {
				// transparent
				continue;
			}
			// opaque: xorBit==0 => white, xorBit==1 => black
			uint8 r = xorBit ? 0x00 : 0xFF;
			uint8 g = xorBit ? 0x00 : 0xFF;
			uint8 b = xorBit ? 0x00 : 0xFF;
			uint8 a = 0xFF;
			uint32 packed = ((uint32)a << 24) | ((uint32)r << 16) | ((uint32)g << 8) | (uint32)b;
			dstRow[x] = packed;
		}
	}

	// Save metadata
	gInfo->cursor_width = width;
	gInfo->cursor_height = height;
	gInfo->cursor_hotx = hotX;
	gInfo->cursor_hoty = hotY;

	// Program registers like bitmap
	uint32 addr = (uint32)(gInfo->cursor_phys & EVERGREEN_CUR_SURFACE_ADDRESS_MASK);
	Write32(OUT, EVERGREEN_CUR_SURFACE_ADDRESS, addr);
	#ifdef EVERGREEN_CUR_SURFACE_ADDRESS_HIGH
	Write32(OUT, EVERGREEN_CUR_SURFACE_ADDRESS_HIGH, (uint32)(gInfo->cursor_phys >> 32));
	#endif

	Write32(OUT, EVERGREEN_CUR_SIZE, ((uint32)gInfo->cursor_height << 16) | (uint32)gInfo->cursor_width);
	Write32(OUT, EVERGREEN_CUR_HOT_SPOT, ((uint32)gInfo->cursor_hoty << 16) | (uint32)gInfo->cursor_hotx);

	uint32 control = EVERGREEN_CURSOR_EN | EVERGREEN_CURSOR_24_8_UNPRE_MULT | EVERGREEN_CURSOR_FORCE_MC_ON;
	Write32(OUT, EVERGREEN_CUR_CONTROL, control);

	Write32(OUT, EVERGREEN_CUR_UPDATE, EVERGREEN_CURSOR_UPDATE_LOCK);
	for (volatile int i = 0; i < 1000; i++) ;
	Write32(OUT, EVERGREEN_CUR_UPDATE, 0);

	return B_OK;
}

void radeon_move_cursor(uint16 x, uint16 y)
{
	// Position register: high 16 = x, low 16 = y (match older drivers)
	uint32 pos = ((uint32)x << 16) | (uint32)y;
	Write32(OUT, EVERGREEN_CUR_POSITION, pos);
}

void radeon_show_cursor(bool isVisible)
{
	uint32 control = Read32(OUT, EVERGREEN_CUR_CONTROL);
	if (isVisible)
		control |= EVERGREEN_CURSOR_EN;
	else
		control &= ~EVERGREEN_CURSOR_EN;
	Write32(OUT, EVERGREEN_CUR_CONTROL, control);
}
