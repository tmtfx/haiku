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

	// Clear the 1024-byte cursor pattern buffer
	// AND part to 0xFF (transparent), XOR part to 0x00 (background color)
	for (int y = 0; y < 64; y++) {
		memset(dest + y * 16, 0xFF, 8);
		memset(dest + y * 16 + 8, 0x00, 8);
	}

	uint32 stride = (width + 7) / 8;

	for (uint32 y = 0; y < height; y++) {
		for (uint32 x = 0; x < width; x++) {
			uint32 src_byte = y * stride + (x / 8);
			uint8 src_bit = 7 - (x % 8);

			bool and_bit = (andMask[src_byte] >> src_bit) & 1;
			bool xor_bit = (xorMask[src_byte] >> src_bit) & 1;

			uint32 and_byte_offset = y * 16 + (x / 8);
			uint32 xor_byte_offset = y * 16 + 8 + (x / 8);
			uint8 bit_pos = 7 - (x % 8);

			if (and_bit) {
				dest[and_byte_offset] |= (1 << bit_pos);
			} else {
				dest[and_byte_offset] &= ~(1 << bit_pos);
			}

			if (xor_bit) {
				dest[xor_byte_offset] |= (1 << bit_pos);
			} else {
				dest[xor_byte_offset] &= ~(1 << bit_pos);
			}
		}
	}

	// Set cursor base address registers CR48, CR49, CR4A
	uint32 addr = si.cursorOffset / 1024;
	write_crtc_reg(0x48, addr & 0xFF);
	write_crtc_reg(0x49, (addr >> 8) & 0xFF);
	write_crtc_reg(0x4A, (addr >> 16) & 0xFF);

	// Enable cursor (CR40: bit 0 enables cursor, bit 1 enables 64x64)
	write_crtc_reg(0x40, 0x03);

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

	for (int y = 0; y < 64; y++) {
		memset(dest + y * 16, 0xFF, 8);
		memset(dest + y * 16 + 8, 0x00, 8);
	}

	for (uint32 y = 0; y < height; y++) {
		for (uint32 x = 0; x < width; x++) {
			bool and_bit = true;
			bool xor_bit = false;

			if (colorSpace == B_RGBA32 || colorSpace == B_RGB32) {
				const uint8* pixel = bitmapData + y * bytesPerRow + x * 4;
				uint8 b = pixel[0];
				uint8 g = pixel[1];
				uint8 r = pixel[2];
				uint8 a = (colorSpace == B_RGBA32) ? pixel[3] : 255;

				if (a >= 128) {
					and_bit = false;
					uint32 brightness = (r + g + b) / 3;
					if (brightness >= 128) {
						xor_bit = true;
					} else {
						xor_bit = false;
					}
				}
			}

			uint32 and_byte_offset = y * 16 + (x / 8);
			uint32 xor_byte_offset = y * 16 + 8 + (x / 8);
			uint8 bit_pos = 7 - (x % 8);

			if (and_bit) {
				dest[and_byte_offset] |= (1 << bit_pos);
			} else {
				dest[and_byte_offset] &= ~(1 << bit_pos);
			}

			if (xor_bit) {
				dest[xor_byte_offset] |= (1 << bit_pos);
			} else {
				dest[xor_byte_offset] &= ~(1 << bit_pos);
			}
		}
	}

	uint32 addr = si.cursorOffset / 1024;
	write_crtc_reg(0x48, addr & 0xFF);
	write_crtc_reg(0x49, (addr >> 8) & 0xFF);
	write_crtc_reg(0x4A, (addr >> 16) & 0xFF);

	write_crtc_reg(0x40, 0x03);

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

	write_crtc_reg(0x4E, preset_x);
	write_crtc_reg(0x4F, preset_y);

	write_crtc_reg(0x44, x & 0xFF);
	write_crtc_reg(0x45, (x >> 8) & 0xFF);

	write_crtc_reg(0x46, y & 0xFF);
	write_crtc_reg(0x47, (y >> 8) & 0xFF);
}


void
ShowCursor(bool bShow)
{
	write_crtc_reg(0x39, 0x80);

	uint8 ctrl = read_crtc_reg(0x40);
	if (bShow) {
		ctrl |= 0x01;
	} else {
		ctrl &= ~0x01;
	}
	write_crtc_reg(0x40, ctrl);
}

} // extern "C"
