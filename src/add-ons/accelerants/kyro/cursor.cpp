/*
 * Copyright 2026, Fabio Tomat.
 * Copyright 2026, GitHub Copilot.
 *
 * Distributed under the terms of the GNU General Public License version 2.
 */


#include "accelerant.h"

#include <string.h>


namespace {

static const uint32 kDACCursorAddr = 0x1408;
static const uint32 kDACCursorCtrl = 0x140c;


static inline uint32
read32(uint32 offset)
{
	return *(volatile uint32*)(gInfo.regs + offset);
}


static inline void
write32(uint32 offset, uint32 value)
{
	*(volatile uint32*)(gInfo.regs + offset) = value;
}


static uint8*
cursor_buffer()
{
	return gInfo.frameBuffer == NULL
		? NULL : gInfo.frameBuffer + gInfo.sharedInfo->cursorOffset;
}


static void
program_cursor_address(void)
{
	uint32 value = read32(kDACCursorAddr);
	value &= ~0x001fffff;
	value |= (gInfo.sharedInfo->cursorOffset >> 4) & 0x001fffff;
	write32(kDACCursorAddr, value);
}

} // namespace


uint32
GetCursorBits(void)
{
	return gInfo.sharedInfo->settings.cursorbits;
}


status_t
SetCursorShape(uint16 width, uint16 height, uint16 hotX, uint16 hotY,
	uint8* andMask, uint8* xorMask)
{
	if (width > 64 || height > 64)
		return B_BAD_VALUE;

	uint8* dest = cursor_buffer();
	if (dest == NULL)
		return B_NO_INIT;

	memset(dest, 0, gInfo.sharedInfo->cursorBufferSize);

	const uint32 srcStride = (width + 7) / 8;
	for (uint32 y = 0; y < height; y++) {
		for (uint32 x = 0; x < width; x++) {
			uint32 srcByte = y * srcStride + x / 8;
			uint8 srcBit = 7 - (x % 8);
			bool andBit = ((andMask[srcByte] >> srcBit) & 1) != 0;
			bool xorBit = ((xorMask[srcByte] >> srcBit) & 1) != 0;

			uint8 value = 0;
			if (!andBit && xorBit)
				value = 1;
			else if (!andBit && !xorBit)
				value = 2;
			else if (andBit && xorBit)
				value = 3;

			uint32 destByte = y * 16 + x / 4;
			uint8 shift = (x % 4) * 2;
			dest[destByte] |= value << shift;
		}
	}

	gInfo.sharedInfo->cursor.hotX = hotX;
	gInfo.sharedInfo->cursor.hotY = hotY;
	program_cursor_address();
	MoveCursor(gInfo.sharedInfo->cursor.x, gInfo.sharedInfo->cursor.y);
	return B_OK;
}


status_t
SetCursorBitmap(uint16 width, uint16 height, uint16 hotX, uint16 hotY,
	color_space colorSpace, uint16 bytesPerRow, const uint8* bitmapData)
{
	if (width > 64 || height > 64)
		return B_BAD_VALUE;
	if (colorSpace != B_RGBA32 && colorSpace != B_RGB32)
		return B_BAD_VALUE;

	uint8* dest = cursor_buffer();
	if (dest == NULL)
		return B_NO_INIT;

	memset(dest, 0, gInfo.sharedInfo->cursorBufferSize);

	for (uint32 y = 0; y < height; y++) {
		for (uint32 x = 0; x < width; x++) {
			const uint8* pixel = bitmapData + y * bytesPerRow + x * 4;
			uint8 blue = pixel[0];
			uint8 green = pixel[1];
			uint8 red = pixel[2];
			uint8 alpha = colorSpace == B_RGBA32 ? pixel[3] : 255;

			uint8 value = 0;
			if (alpha >= 192) {
				uint32 luma = (red * 30 + green * 59 + blue * 11) / 100;
				value = luma >= 128 ? 1 : 2;
			} else if (alpha >= 96) {
				value = 3;
			}

			uint32 destByte = y * 16 + x / 4;
			uint8 shift = (x % 4) * 2;
			dest[destByte] |= value << shift;
		}
	}

	gInfo.sharedInfo->cursor.hotX = hotX;
	gInfo.sharedInfo->cursor.hotY = hotY;
	program_cursor_address();
	MoveCursor(gInfo.sharedInfo->cursor.x, gInfo.sharedInfo->cursor.y);
	return B_OK;
}


void
MoveCursor(uint16 x, uint16 y)
{
	gInfo.sharedInfo->cursor.x = x;
	gInfo.sharedInfo->cursor.y = y;

	int32 cursorX = (int32)x - gInfo.sharedInfo->cursor.hotX;
	int32 cursorY = (int32)y - gInfo.sharedInfo->cursor.hotY;
	if (cursorX < 0)
		cursorX = 0;
	if (cursorY < 0)
		cursorY = 0;

	uint32 value = read32(kDACCursorCtrl);
	value &= ~((0x7ffu << 0) | (0x7ffu << 16));
	value |= (cursorX & 0x7ffu);
	value |= (cursorY & 0x7ffu) << 16;
	write32(kDACCursorCtrl, value);
}


void
ShowCursor(bool show)
{
	uint32 value = read32(kDACCursorCtrl);
	if (show)
		value |= 1u << 31;
	else
		value &= ~(1u << 31);
	write32(kDACCursorCtrl, value);
	gInfo.sharedInfo->cursor.visible = show;
}
