/*
	Copyright 2009 Haiku, Inc.  All rights reserved.
	Distributed under the terms of the MIT license.

	Authors:
	Gerald Zajac 2009
*/


#include "accelerant.h"
#include "mach64.h"

#include <string.h>



void
Mach64_ShowCursor(bool bShow)
{
	// Turn cursor on/off.

   	OUTREGM(GEN_TEST_CNTL, bShow ? HWCURSOR_ENABLE : 0, HWCURSOR_ENABLE);
}


void
Mach64_SetCursorPosition(int x, int y)
{
	SharedInfo& si = *gInfo.sharedInfo;

	// xOffset & yOffset are used for displaying partial cursors on screen edges.

	uint8 xOffset = 0;
	uint8 yOffset = 0;

	if (x < 0) {
		xOffset = -x;
		x = 0;
	}

	if (y < 0) {
		yOffset = -y;
		y = 0;
	}

	OUTREG(CUR_OFFSET, (si.cursorOffset >> 3) + (yOffset << 1));
	OUTREG(CUR_HORZ_VERT_OFF, (yOffset << 16) | xOffset);
	OUTREG(CUR_HORZ_VERT_POSN, (y << 16) | x);
}


bool
Mach64_LoadCursorImage(int width, int height, uint8* andMask, uint8* xorMask)
{
	SharedInfo& si = *gInfo.sharedInfo;

	if (andMask == NULL || xorMask == NULL)
		return false;

	uint16* fbCursor = (uint16*)((addr_t)si.videoMemAddr + si.cursorOffset);

	// Initialize the hardware cursor as completely transparent.

	memset(fbCursor, 0xaa, CURSOR_BYTES);

	// Now load the AND & XOR masks for the cursor image into the cursor
	// buffer.  Note that a particular bit in these masks will have the
	// following effect upon the corresponding cursor pixel:
	//	AND  XOR	Result
	//	 0	0		 White pixel
	//	 0	1		 Black pixel
	//	 1	0		 Screen color (for transparency)
	//	 1	1		 Reverse screen color to black or white

	for (int row = 0; row < height; row++) {
		for (int colByte = 0; colByte < width / 8; colByte++) {
			// Convert the 8 bit AND and XOR masks into a 16 bit mask containing
			// pairs of the bits from the AND and XOR maks.

			uint8 andBits = *andMask++;
			uint8 xorBits = *xorMask++;
			uint16 cursorBits = 0;

			for (int j = 0; j < 8; j++) {
				cursorBits <<= 2;
				cursorBits |= ((andBits & 0x01) << 1);
				cursorBits |= (xorBits & 0x01);
				andBits >>= 1;
				xorBits >>= 1;
			}

			fbCursor[row * 8 + colByte] = cursorBits;
		}
	}

	// Set the cursor colors which are white background and black foreground.

	OUTREG(CUR_CLR0, ~0);
	OUTREG(CUR_CLR1, 0);

	return true;
}

status_t
Mach64_SetCursorBitmap(uint16 width, uint16 height, uint16 hot_x, uint16 hot_y,
					   color_space colorSpace, uint16 bytesPerRow, const uint8* bitmapData)
{
	SharedInfo& si = *gInfo.sharedInfo;
	uint16* fbCursor = (uint16*)((addr_t)si.videoMemAddr + si.cursorOffset);
	if (fbCursor == NULL)
		return B_NO_INIT;

	// INITIALIZATION: 64 rows, every row are 8 Words (16 bytes).
	// Start Transparent. Bit hardware: AND=1, XOR=0 -> binary 10 (0xA hex)
	for (int i = 0; i < 64 * 8; i++) {
		fbCursor[i] = 0xAAAA;
	}

	// COPY THE PIXELS CONVERTING INTO AN INTERLACED 2-BITS STRUCTURE
	if (colorSpace == B_RGBA32 || colorSpace == B_RGB32) {
		for (int y = 0; y < height && y < 64; y++) {
			const uint8* srcRow = bitmapData + y * bytesPerRow;
			uint16* rowPtr = fbCursor + (y * 8); // 8 words per row

			for (int x = 0; x < width && x < 64; x++) {
				const uint8* pixel = srcRow + x * 4;
				
				uint8 b = pixel[0];
				uint8 g = pixel[1];
				uint8 r = pixel[2];
				uint8 a = (colorSpace == B_RGBA32) ? pixel[3] : 0xFF;

				if (a < 128)
					continue; // keep transparent (0x10) as initialized

				// Find which word (of those 8) contains the pixel "x"
				int wordIdx = x / 8;
				int bitPos = 14 - ((x % 8) * 2);

				// Clear actual 2 bits (temporarily set them to 00)
				rowPtr[wordIdx] &= ~(0x3 << bitPos);

				// Luma
				uint32 luma = (r * 77 + g * 150 + b * 29) >> 8;
				if (luma > 128) {
					// white -> AND=0, XOR=0 -> 00b
					// No need to "or", already zeroed the bits
				} else {
					// black -> AND=0, XOR=1 -> 01b
					rowPtr[wordIdx] |= (0x1 << bitPos);
				}
			}
		}
	} else {
		return B_ERROR;
	}

	// UPDATE CURSOR COLOR REGISTERS(Mach64)
	OUTREG(CUR_CLR0, 0xFFFFFF); // Background White
	OUTREG(CUR_CLR1, 0x000000); // Foreground Black

	return B_OK;
}
