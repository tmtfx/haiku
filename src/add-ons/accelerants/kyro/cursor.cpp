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
static const uint32 kDACLUTAddress = 0x1484;
static const uint32 kDACLUTData = 0x1488;


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
// kyro.settings) picks between a few candidates. Gotcha found the hard way:
// this is read from kyro.settings in /boot/home/config/settings/kernel/
// drivers/ - editing the copy in this source tree does nothing on a real
// test run unless that copy is also updated.
//
// What later testing with that file actually being picked up established:
//   - The bytes are read as raw pixel color, not a 1bpp AND/XOR mask: an
//     AND/XOR-mask candidate showed our own fill pattern (0xff then 0x00
//     bytes) as solid color bars, which only makes sense read as color.
//   - The row pitch is 32 bytes (1024 bytes / 32 = a 32-row canvas): an
//     8bpp/32-wide candidate at that pitch produced the hand glyph at
//     exactly the right size.
//   - At that same size, the colors came out pink/flesh-toned instead of
//     black and white - consistent with those bytes being read as a color
//     LUT index rather than direct grayscale. See program_grayscale_lut().
//   - A 16bpp candidate at the same 32-byte pitch got the colors right
//     (truecolor needs no LUT) but came out twice as wide - consistent
//     with the hardware still reading 1 byte per pixel, so our 2-byte
//     pixels get read as two separate pixels each.
//
//   0 = raw 4bpp grayscale pixels (2 pixels/byte), 64 wide x 32 tall.
//       Untested since the kyro.settings gotcha above was found.
//   1 = raw 8bpp grayscale pixels, 32 wide x 32 tall. Current best guess:
//       right size, and now paired with program_grayscale_lut() to fix
//       the color.
//   2 = raw BGR565 pixels, 16 wide x 32 tall. Right colors, wrong (double)
//       size - see above.
//   3 = 2bpp packed color index (4 pixels/byte), 64 wide x 64 tall, 16-byte
//       pitch - the very first attempt, from before the pitch was known to
//       be 32 bytes. Produced a solid black block with a couple of tiny
//       colored specks. Kept only for reference.
//   4 = AND/XOR 1bpp mask planes, 64 wide x 64 tall, 16-byte pitch - the
//       second attempt, also from before the pitch was known. Kept only
//       for reference.
enum {
	kCursorFormat4bpp = 0,
	kCursorFormatGray8 = 1,
	kCursorFormatBGR565 = 2,
	kCursorFormatPacked2bpp = 3,
	kCursorFormatAndXor = 4
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
	switch (format) {
		case kCursorFormatGray8:
			// 32-byte pitch guess: 32 wide x 1 byte/px = 32 bytes/row.
			geo.width = 32; geo.height = 32; geo.bytesPerPixel = 1;
			break;
		case kCursorFormatBGR565:
			geo.width = 16; geo.height = 32; geo.bytesPerPixel = 2;
			break;
		case kCursorFormatPacked2bpp:
			// Legacy 16-byte pitch guess, kept as-is for reference.
			geo.width = 64; geo.height = 64; geo.bytesPerPixel = 0;
			break;
		case kCursorFormatAndXor:
			// Legacy 16-byte pitch guess, kept as-is for reference.
			geo.width = 64; geo.height = 64; geo.bytesPerPixel = 0;
			break;
		case kCursorFormat4bpp:
		default:
			geo.width = 64; geo.height = 32; geo.bytesPerPixel = 0;
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
	} else if (format == kCursorFormat4bpp) {
		// Nibble value 0 is black; there's no transparency to default to.
		memset(dest, 0, geo.width / 2 * geo.height);
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
cursor_set_4bpp_pixel(uint8* dest, const CursorGeometry& geo, uint32 x,
	uint32 y, uint8 red, uint8 green, uint8 blue, uint8 alpha)
{
	uint32 luma = (red * 30 + green * 59 + blue * 11) / 100;
	uint8 nibble = alpha < 128 ? 0 : (uint8)(luma >> 4);

	uint32 byteOffset = y * (geo.width / 2) + x / 2;
	if ((x % 2) == 0)
		dest[byteOffset] = (dest[byteOffset] & 0x0f) | (nibble << 4);
	else
		dest[byteOffset] = (dest[byteOffset] & 0xf0) | nibble;
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


// Same idea as cursor_format(): read directly from kyro.settings in user
// space. When enabled, SetCursorBitmap ignores the real cursor bitmap and
// draws a fixed L-shaped marker (top row + left column of the first 16x16
// pixels, rest black) instead. The real hand-cursor glyph is hard to read
// distortion from in a small, blurry photo - repeatedly showing up
// "doubled" at different widths with different candidates. A single
// unambiguous asymmetric shape makes it obvious whether what we're seeing
// is a real duplicate, a mirrored copy, a transposed (row/column-swapped)
// read, or something else - which decides what to try next far better
// than another full guess at the cursor glyph does.
static bool
cursor_debug_marker(void)
{
	static int state = -1;
	if (state < 0) {
		state = 0;
		void* handle = load_driver_settings("kyro.settings");
		if (handle != NULL) {
			state = get_driver_boolean_parameter(handle, "cursordebug",
				false, false) ? 1 : 0;
			unload_driver_settings(handle);
		}
	}
	return state != 0;
}


// kCursorFormatGray8 produced the hand at the exact right size, but in a
// pinkish/flesh color instead of black and white - consistent with the
// hardware reading those bytes as an index into a color LUT we never
// programmed, rather than as direct grayscale. DACLUTAddress/DACLUTData
// exist in STG4000Reg.h but are never written anywhere in the reference
// code we have access to, so the exact protocol here is also a guess:
// write the index, then a packed 0x00RRGGBB value. Since every entry of a
// grayscale ramp has R == G == B, this guess is safe even if the real
// byte order/packing is different - a gray value looks the same regardless
// of channel order.
static void
program_grayscale_lut(void)
{
	static bool done = false;
	if (done)
		return;
	done = true;

	for (uint32 i = 0; i < 256; i++) {
		write32(kDACLUTAddress, i);
		write32(kDACLUTData, (i << 16) | (i << 8) | i);
	}
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
	if (format == kCursorFormatGray8)
		program_grayscale_lut();
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
			if (format == kCursorFormat4bpp)
				cursor_set_4bpp_pixel(dest, geo, x, y, red, green, blue, alpha);
			else
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
	if (format == kCursorFormatGray8)
		program_grayscale_lut();
	CursorGeometry geo = cursor_geometry(format);
	cursor_clear(dest, format, geo);

	bool debugMarker = cursor_debug_marker();
	const uint32 markerSize = 16;
	const uint32 clipWidth
		= min_c(debugMarker ? markerSize : width, geo.width);
	const uint32 clipHeight
		= min_c(debugMarker ? markerSize : height, geo.height);

	for (uint32 y = 0; y < clipHeight; y++) {
		for (uint32 x = 0; x < clipWidth; x++) {
			uint8 blue, green, red, alpha;
			if (debugMarker) {
				bool on = x == 0 || y == 0;
				blue = green = red = on ? 255 : 0;
				alpha = 255;
			} else {
				const uint8* pixel = bitmapData + y * bytesPerRow + x * 4;
				blue = pixel[0];
				green = pixel[1];
				red = pixel[2];
				alpha = colorSpace == B_RGBA32 ? pixel[3] : 255;
			}

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

			if (format == kCursorFormat4bpp)
				cursor_set_4bpp_pixel(dest, geo, x, y, red, green, blue, alpha);
			else
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
