/*
 * Copyright 2026, Fabio Tomat.
 * Copyright 2026, GitHub Copilot.
 *
 * Distributed under the terms of the GNU General Public License version 2.
 */


#include "accelerant.h"

#include <driver_settings.h>
#include <stdlib.h>
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


// The real STG4000 hardware cursor buffer layout isn't documented anywhere
// we have access to, and guessing it wrong doesn't fail loudly - it just
// draws garbage. So instead of hardcoding one guess, "cursorformat" (set in
// kyro.settings) picks between a few candidates.
//
// Two things are no longer guesses, deduced from how different candidates
// actually looked on real hardware:
//   - The row pitch is 16 bytes, 64 rows, 1024 bytes total, regardless of
//     pixel format. A candidate using a 64-byte pitch visibly failed to
//     wrap to a new row (the image just smeared downward); two candidates
//     that both happened to use a 16-byte pitch instead produced a clean,
//     undistorted (if wrongly colored) repeating image.
//   - The bytes are read as raw pixel color, not a 1bpp AND/XOR mask: an
//     AND/XOR-mask candidate showed our own fill pattern (0xff then 0x00
//     bytes) as solid color bars, which only makes sense if those bytes
//     were read directly as color rather than composited as a mask.
//
// What's still a guess is how many bits make up one pixel within that
// fixed 16-byte row - which also decides the usable width:
//
//   0 = raw 8bpp grayscale pixels, 16 wide x 64 tall. No transparency.
//       (16 wide matches the classic BeOS cursor size; current best guess)
//   1 = raw BGR565 pixels, 8 wide x 64 tall. No transparency.
//   2 = 2bpp packed color index (4 pixels/byte), 64 wide x 64 tall - the
//       very first attempt. Produced a solid black block with a couple of
//       tiny colored specks, consistent with the hardware having no color
//       LUT for this index scheme. Kept only for reference.
//   3 = AND/XOR 1bpp mask planes, 64 wide x 64 tall - the second attempt.
//       Known not to work; see above. Kept only for reference.
enum {
	kCursorFormatGray8 = 0,
	kCursorFormatBGR565 = 1,
	kCursorFormatPacked2bpp = 2,
	kCursorFormatAndXor = 3
};


struct CursorGeometry {
	uint32	width;
	uint32	height;
	uint32	bytesPerPixel;	// 0 means a special sub-byte layout (packed
							// 2bpp index or AND/XOR planes - see format)
};


static CursorGeometry
cursor_geometry(uint32 format)
{
	CursorGeometry geo;
	geo.height = 64;
	switch (format) {
		case kCursorFormatBGR565:
			geo.width = 8; geo.bytesPerPixel = 2;
			break;
		case kCursorFormatPacked2bpp:
			geo.width = 64; geo.bytesPerPixel = 0;
			break;
		case kCursorFormatAndXor:
			geo.width = 64; geo.bytesPerPixel = 0;
			break;
		case kCursorFormatGray8:
		default:
			geo.width = 16; geo.bytesPerPixel = 1;
			break;
	}
	return geo;
}


static void
cursor_clear(uint8* dest, uint32 format, const CursorGeometry& geo)
{
	if (format == kCursorFormatAndXor) {
		// Default to fully transparent: AND=1, XOR=0.
		const uint32 stride = geo.width / 8 * 2;
		for (uint32 row = 0; row < geo.height; row++) {
			memset(dest + row * stride, 0xff, geo.width / 8);
			memset(dest + row * stride + geo.width / 8, 0x00, geo.width / 8);
		}
	} else if (format == kCursorFormatPacked2bpp) {
		// Index 0 is transparent in this scheme; an all-zero buffer is
		// already fully transparent.
		memset(dest, 0, geo.width / 4 * geo.height);
	} else
		memset(dest, 0, geo.width * geo.height * geo.bytesPerPixel);
}


static void
cursor_set_pixel(uint8* dest, const CursorGeometry& geo, uint32 x, uint32 y,
	uint8 red, uint8 green, uint8 blue, uint8 alpha)
{
	switch (geo.bytesPerPixel) {
		case 4:
		{
			uint8* p = dest + (y * geo.width + x) * 4;
			p[0] = blue;
			p[1] = green;
			p[2] = red;
			p[3] = alpha;
			break;
		}
		case 2:
		{
			uint16 value = alpha < 128 ? 0 : (uint16)(((blue >> 3) << 0)
				| ((green >> 2) << 5) | ((red >> 3) << 11));
			uint8* p = dest + (y * geo.width + x) * 2;
			p[0] = (uint8)(value & 0xff);
			p[1] = (uint8)(value >> 8);
			break;
		}
		case 1:
		{
			uint32 luma = (red * 30 + green * 59 + blue * 11) / 100;
			dest[y * geo.width + x] = alpha < 128 ? 0 : (uint8)luma;
			break;
		}
	}
}


static void
cursor_set_andxor_pixel(uint8* dest, const CursorGeometry& geo, uint32 x,
	uint32 y, bool andBit, bool xorBit)
{
	const uint32 stride = geo.width / 8 * 2;
	uint32 byteOffset = y * stride + x / 8;
	uint8 bit = 1 << (7 - (x % 8));
	if (andBit)
		dest[byteOffset] |= bit;
	else
		dest[byteOffset] &= ~bit;

	uint32 xorByteOffset = byteOffset + geo.width / 8;
	if (xorBit)
		dest[xorByteOffset] |= bit;
	else
		dest[xorByteOffset] &= ~bit;
}


static void
cursor_set_packed2bpp_pixel(uint8* dest, const CursorGeometry& geo, uint32 x,
	uint32 y, bool andBit, bool xorBit)
{
	// The very first attempt's encoding: transparent=0, black=1, white=2,
	// invert=3, 4 pixels packed per byte.
	uint8 value = 0;
	if (!andBit && xorBit)
		value = 1;
	else if (!andBit && !xorBit)
		value = 2;
	else if (andBit && xorBit)
		value = 3;

	uint32 destByte = y * (geo.width / 4) + x / 4;
	uint8 shift = (x % 4) * 2;
	dest[destByte] |= value << shift;
}


// Read directly from kyro.settings in user space, independent of the
// kernel-driver-owned SharedInfo struct: that struct is shared memory laid
// out once by the kernel driver at boot, so adding a field to it requires
// the kernel driver AND the accelerant to agree on its layout, which needs
// a full reboot (not just an app_server restart) to stay in sync. Reading
// our own settings file here avoids that risk entirely and lets a new
// cursorformat value be tried with just an app_server restart.
static uint32
cursor_format(void)
{
	static uint32 format = 0;
	static bool loaded = false;
	if (!loaded) {
		void* handle = load_driver_settings("kyro.settings");
		if (handle != NULL) {
			const char* value = get_driver_parameter(handle, "cursorformat",
				NULL, NULL);
			if (value != NULL)
				format = (uint32)atoi(value);
			unload_driver_settings(handle);
		}
		loaded = true;
	}
	return format;
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

	uint32 format = cursor_format();
	CursorGeometry geo = cursor_geometry(format);
	cursor_clear(dest, format, geo);

	const uint32 srcStride = (width + 7) / 8;
	const uint32 clipWidth = min_c(width, geo.width);
	const uint32 clipHeight = min_c(height, geo.height);

	for (uint32 y = 0; y < clipHeight; y++) {
		for (uint32 x = 0; x < clipWidth; x++) {
			uint32 srcByte = y * srcStride + x / 8;
			uint8 srcBit = 7 - (x % 8);
			bool andBit = ((andMask[srcByte] >> srcBit) & 1) != 0;
			bool xorBit = ((xorMask[srcByte] >> srcBit) & 1) != 0;

			if (format == kCursorFormatAndXor) {
				cursor_set_andxor_pixel(dest, geo, x, y, andBit, xorBit);
				continue;
			}
			if (format == kCursorFormatPacked2bpp) {
				cursor_set_packed2bpp_pixel(dest, geo, x, y, andBit, xorBit);
				continue;
			}

			// Matches the classic AND/XOR truth table: (0,0)->white,
			// (0,1)->black, (1,0)->transparent, (1,1)->invert
			// (approximated as opaque gray since raw pixel formats can't
			// XOR the screen).
			uint8 red = 0, green = 0, blue = 0, alpha = 255;
			if (andBit && !xorBit)
				alpha = 0;
			else if (!andBit && !xorBit)
				red = green = blue = 255;
			else if (andBit && xorBit)
				red = green = blue = 128;
			cursor_set_pixel(dest, geo, x, y, red, green, blue, alpha);
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

	uint32 format = cursor_format();
	CursorGeometry geo = cursor_geometry(format);
	cursor_clear(dest, format, geo);

	const uint32 clipWidth = min_c(width, geo.width);
	const uint32 clipHeight = min_c(height, geo.height);

	for (uint32 y = 0; y < clipHeight; y++) {
		for (uint32 x = 0; x < clipWidth; x++) {
			const uint8* pixel = bitmapData + y * bytesPerRow + x * 4;
			uint8 blue = pixel[0];
			uint8 green = pixel[1];
			uint8 red = pixel[2];
			uint8 alpha = colorSpace == B_RGBA32 ? pixel[3] : 255;

			if (format == kCursorFormatAndXor || format == kCursorFormatPacked2bpp) {
				bool andBit = true;
				bool xorBit = false;
				if (alpha >= 192) {
					uint32 luma = (red * 30 + green * 59 + blue * 11) / 100;
					andBit = false;
					xorBit = luma < 128;
				} else if (alpha >= 96) {
					andBit = true;
					xorBit = true;
				}
				if (format == kCursorFormatAndXor)
					cursor_set_andxor_pixel(dest, geo, x, y, andBit, xorBit);
				else
					cursor_set_packed2bpp_pixel(dest, geo, x, y, andBit, xorBit);
				continue;
			}

			cursor_set_pixel(dest, geo, x, y, red, green, blue, alpha);
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

	// The DAC's cursor position register has its X/Y fields swapped with
	// respect to what the naming in the datasheet-derived macros suggests:
	// on real hardware the low field (bits 0-10) controls the vertical
	// position and the high field (bits 16-26) controls the horizontal
	// position. Confirmed by on-hardware testing: without this swap,
	// horizontal mouse movement moved the cursor vertically and vice versa.
	uint32 value = read32(kDACCursorCtrl);
	value &= ~((0x7ffu << 0) | (0x7ffu << 16));
	value |= (cursorY & 0x7ffu);
	value |= (cursorX & 0x7ffu) << 16;
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
