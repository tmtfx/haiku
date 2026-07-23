/*
 * Copyright 2026, Gemini CLI. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Gemini CLI <gemini-cli@google.com>
 */


#include "accel.h"
#include <string.h>


extern "C" {

uint32
GetCursorBits(void)
{
	return 2;
}


status_t
SetCursorShape(uint16 width, uint16 height, uint16 hot_x, uint16 hot_y,
				uint8* andMask, uint8* xorMask)
{
	if (width > 64 || height > 64)
		return B_BAD_VALUE;

	SharedInfo& si = *gInfo.sharedInfo;
	si.cursorHotX = hot_x;
	si.cursorHotY = hot_y;

	uint8* dest = (uint8*)((addr_t)si.videoMemAddr + si.cursorOffset);
	if (!dest)
		return B_NO_INIT;

	// Unlock CRTC extended registers
	write_crtc_reg(0x39, 0x80);

	// Initialize the 1024-byte cursor pattern buffer to transparent (AND=1, XOR=0)
	// Each row is 16 bytes: 4 blocks of 4 bytes (2 bytes AND, 2 bytes XOR)
	for (int y = 0; y < 64; y++) {
		uint8* row = dest + y * 16;
		for (int block = 0; block < 4; block++) {
			row[block * 4 + 0] = 0xFF; // AND byte 0
			row[block * 4 + 1] = 0xFF; // AND byte 1
			row[block * 4 + 2] = 0x00; // XOR byte 0
			row[block * 4 + 3] = 0x00; // XOR byte 1
		}
	}

	uint32 stride = (width + 7) / 8;

	for (uint32 y = 0; y < height && y < 64; y++) {
		uint8* row = dest + y * 16;
		for (uint32 x = 0; x < width && x < 64; x++) {
			uint32 src_byte = y * stride + (x / 8);
			uint8 src_bit = 7 - (x % 8);

			bool and_bit = (andMask[src_byte] >> src_bit) & 1;
			bool xor_bit = (xorMask[src_byte] >> src_bit) & 1;

			int block = x / 16;
			int byte_offset = (x % 16) / 8;
			int bit_shift = 7 - (x % 8);

			int and_idx = block * 4 + byte_offset;
			int xor_idx = block * 4 + byte_offset + 2;

			if (and_bit) {
				row[and_idx] |= (1 << bit_shift);
			} else {
				row[and_idx] &= ~(1 << bit_shift);
			}

			if (xor_bit) {
				row[xor_idx] |= (1 << bit_shift);
			} else {
				row[xor_idx] &= ~(1 << bit_shift);
			}
		}
	}

	// Set cursor base address registers CR44, CR45 (CursorLocLow, CursorLocHigh)
	uint32 addr = si.cursorOffset / 1024;
	write_crtc_reg(0x44, addr & 0xFF);
	write_crtc_reg(0x45, (addr >> 8) & 0xFF);

	// Set cursor colors: Background to Black (CR4C-CR4F), Foreground to White (CR48-CR4B)
	for (int i = 0; i < 4; i++) {
		write_crtc_reg(0x48 + i, 0xFF); // FG
		write_crtc_reg(0x4C + i, 0x00); // BG
	}

	// Enable cursor (CR50: Bit 0=Enable, Bit 6=64x64, Bit 7=Windows/X11 mode)
	write_crtc_reg(0x50, 0xC1);

	// Update cursor position
	MoveCursor(si.cursorHotX, si.cursorHotY);

	return B_OK;
}


status_t
SetCursorBitmap(uint16 width, uint16 height, uint16 hot_x, uint16 hot_y,
                color_space colorSpace, uint16 bytesPerRow, const uint8* bitmapData)
{
	if (width > 64 || height > 64)
		return B_BAD_VALUE;

	SharedInfo& si = *gInfo.sharedInfo;
	si.cursorHotX = hot_x;
	si.cursorHotY = hot_y;

	uint8* dest = (uint8*)((addr_t)si.videoMemAddr + si.cursorOffset);
	if (!dest)
		return B_NO_INIT;

	write_crtc_reg(0x39, 0x80);

	// Initialize the 1024-byte cursor pattern buffer to transparent (AND=1, XOR=0)
	for (int y = 0; y < 64; y++) {
		uint8* row = dest + y * 16;
		for (int block = 0; block < 4; block++) {
			row[block * 4 + 0] = 0xFF; // AND byte 0
			row[block * 4 + 1] = 0xFF; // AND byte 1
			row[block * 4 + 2] = 0x00; // XOR byte 0
			row[block * 4 + 3] = 0x00; // XOR byte 1
		}
	}

	if (colorSpace == B_RGBA32 || colorSpace == B_RGB32) {
		for (uint32 y = 0; y < height && y < 64; y++) {
			uint8* row = dest + y * 16;
			const uint8* srcRow = bitmapData + y * bytesPerRow;

			for (uint32 x = 0; x < width && x < 64; x++) {
				const uint8* pixel = srcRow + x * 4;
				uint8 b = pixel[0];
				uint8 g = pixel[1];
				uint8 r = pixel[2];
				uint8 a = (colorSpace == B_RGBA32) ? pixel[3] : 255;

				if (a < 128)
					continue; // Keep transparent (AND=1, XOR=0)

				int block = x / 16;
				int byte_offset = (x % 16) / 8;
				int bit_shift = 7 - (x % 8);

				int and_idx = block * 4 + byte_offset;
				int xor_idx = block * 4 + byte_offset + 2;

				// Opaque pixel (AND=0)
				row[and_idx] &= ~(1 << bit_shift);

				uint32 luma = (r + g + b) / 3;
				if (luma > 128) {
					// White (AND=0, XOR=1)
					row[xor_idx] |= (1 << bit_shift);
				} else {
					// Black (AND=0, XOR=0)
					row[xor_idx] &= ~(1 << bit_shift);
				}
			}
		}
	} else {
		return B_BAD_VALUE;
	}

	// Set cursor base address registers CR44, CR45 (CursorLocLow, CursorLocHigh)
	uint32 addr = si.cursorOffset / 1024;
	write_crtc_reg(0x44, addr & 0xFF);
	write_crtc_reg(0x45, (addr >> 8) & 0xFF);

	// Set cursor colors: Background to Black (CR4C-CR4F), Foreground to White (CR48-CR4B)
	for (int i = 0; i < 4; i++) {
		write_crtc_reg(0x48 + i, 0xFF); // FG
		write_crtc_reg(0x4C + i, 0x00); // BG
	}

	// Enable cursor (CR50: Bit 0=Enable, Bit 6=64x64, Bit 7=Windows/X11 mode)
	write_crtc_reg(0x50, 0xC1);

	// Update cursor position
	MoveCursor(si.cursorHotX, si.cursorHotY);

	return B_OK;
}


void
MoveCursor(uint16 xPos, uint16 yPos)
{
	SharedInfo& si = *gInfo.sharedInfo;

	write_crtc_reg(0x39, 0x80);

	int16 x = (int16)xPos - (int16)si.cursorHotX;
	int16 y = (int16)yPos - (int16)si.cursorHotY;
	uint8 preset_x = 0;
	uint8 preset_y = 0;

	if (x < 0) {
		preset_x = -x;
		x = 0;
	}
	if (y < 0) {
		preset_y = -y;
		y = 0;
	}

	// Write preset offsets
	write_crtc_reg(0x46, preset_x);
	write_crtc_reg(0x47, preset_y);

	// Write X position (CR40, CR41)
	write_crtc_reg(0x40, x & 0xFF);
	write_crtc_reg(0x41, (x >> 8) & 0xFF);

	// Write Y position (CR42, CR43)
	write_crtc_reg(0x42, y & 0xFF);
	write_crtc_reg(0x43, (y >> 8) & 0xFF);
}


void
ShowCursor(bool bShow)
{
	write_crtc_reg(0x39, 0x80);

	uint8 ctrl = read_crtc_reg(0x50);
	if (bShow) {
		ctrl |= 0x01;
	} else {
		ctrl &= ~0x01;
	}
	write_crtc_reg(0x50, ctrl);
}

} // extern "C"
