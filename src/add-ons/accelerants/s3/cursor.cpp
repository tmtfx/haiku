/*
	Copyright 1999, Be Incorporated.   All Rights Reserved.
	This file may be used under the terms of the Be Sample Code License.

	Other authors:
	Gerald Zajac 2007-2008
	Fabio Tomat 2026
*/
#include <cstdlib>
#include <cstring>
#include "accel.h"

uint32
GetCursorBits(void)
{
    // 2 bpp: supporta cursore bitmap, ma non drag'n'drop (no true color cursor)
    return 2;
}

status_t
SetCursorShape(uint16 width, uint16 height, uint16 hot_x, uint16 hot_y,
				uint8* andMask, uint8* xorMask)
{
	// NOTE: Currently, for BeOS, cursor width and height must be equal to 16.

	if ((width != 16) || (height != 16)) {
		return B_ERROR;
	} else if ((hot_x >= width) || (hot_y >= height)) {
		return B_ERROR;
	} else {
		// Update cursor variables appropriately.

		SharedInfo& si = *gInfo.sharedInfo;
		si.cursorHotX = hot_x;
		si.cursorHotY = hot_y;

		if ( ! gInfo.LoadCursorImage(width, height, andMask, xorMask))
			return B_ERROR;
	}

	return B_OK;
}


status_t
SetCursorBitmap(uint16 width, uint16 height, uint16 hot_x, uint16 hot_y,
				color_space colorSpace, uint16 bytesPerRow, const uint8* bitmapData)
{
	// Debug: log incoming parameters and driver state
	SharedInfo& si = *gInfo.sharedInfo;
	TRACE("SetCursorBitmap: w=%u h=%u hot=%u,%u color=0x%X bpr=%u bDisableHdwCursor=%d cursorOffset=0x%X\n",
		width, height, hot_x, hot_y, (uint32)colorSpace, bytesPerRow,
		si.bDisableHdwCursor ? 1 : 0, (uint32)si.cursorOffset);

	// Currently only support 16x16 cursors and simple conversion to AND/XOR
	if ((width != 16) || (height != 16)) {
		TRACE("SetCursorBitmap: unsupported size %ux%u\n", width, height);
		return B_ERROR;
	}
	if ((hot_x >= width) || (hot_y >= height)) {
		TRACE("SetCursorBitmap: invalid hotspot %u,%u for size %ux%u\n", hot_x, hot_y, width, height);
		return B_ERROR;
	}

	si.cursorHotX = hot_x;
	si.cursorHotY = hot_y;

	const int bits = width * height;
	const int bytes = bits / 8; // 16*16/8 == 32

	uint8* andMask = (uint8*)malloc(bytes);
	uint8* xorMask = (uint8*)malloc(bytes);
	if (!andMask || !xorMask) {
		free(andMask);
		free(xorMask);
		TRACE("SetCursorBitmap: allocation failed\n");
		return B_NO_MEMORY;
	}

	// Default: fully transparent
	memset(andMask, 0xFF, bytes); // AND=1 -> leave pixel
	memset(xorMask, 0x00, bytes);

	// Only support 32-bit source bitmaps here (B_RGBA32 / B_RGB32)
	if (colorSpace == B_RGBA32 || colorSpace == B_RGB32) {
		for (int y = 0; y < height; y++) {
			const uint8* row = bitmapData + y * bytesPerRow;
			for (int x = 0; x < width; x++) {
				const uint8* px = row + x * 4;
				uint8 alpha = (colorSpace == B_RGBA32) ? px[3] : 0xFF;
				bool opaque = (alpha != 0);
				int bitIndex = y * width + x;
				int byteIndex = bitIndex / 8;
				int bit = 7 - (bitIndex % 8);
				if (opaque) {
					andMask[byteIndex] &= ~(1 << bit); // AND=0 -> draw
					xorMask[byteIndex] |= (1 << bit);  // XOR=1 -> invert (simple visible cursor)
				}
			}
		}
	} else {
		free(andMask);
		free(xorMask);
		TRACE("SetCursorBitmap: unsupported colorSpace 0x%X\n", (uint32)colorSpace);
		return B_ERROR; // unsupported color space
	}

	bool ok = gInfo.LoadCursorImage(width, height, andMask, xorMask);
	TRACE("SetCursorBitmap: LoadCursorImage returned %d\n", ok ? 1 : 0);

	free(andMask);
	free(xorMask);

	return ok ? B_OK : B_ERROR;
}


void
MoveCursor(uint16 xPos, uint16 yPos)
{
	// Move the cursor to the specified position on the desktop.  If we're
	// using some kind of virtual desktop, adjust the display start position
	// accordingly and position the cursor in the proper "virtual" location.

	int x = xPos;		// use signed int's since chip specific functions
	int y = yPos;		// need signed int to determine if cursor off screen

	SharedInfo& si = *gInfo.sharedInfo;
	DisplayModeEx& dm = si.displayMode;

	uint16 hds = dm.h_display_start;	// current horizontal starting pixel
	uint16 vds = dm.v_display_start;	// current vertical starting line

	// Clamp cursor to virtual display.
	if (x >= dm.virtual_width)
		x = dm.virtual_width - 1;
	if (y >= dm.virtual_height)
		y = dm.virtual_height - 1;

	// Adjust h/v display start to move cursor onto screen.
	if (x >= (dm.timing.h_display + hds))
		hds = x - dm.timing.h_display + 1;
	else if (x < hds)
		hds = x;

	if (y >= (dm.timing.v_display + vds))
		vds = y - dm.timing.v_display + 1;
	else if (y < vds)
		vds = y;

	// Reposition the desktop on the display if required.
	if (hds != dm.h_display_start || vds != dm.v_display_start)
		MoveDisplay(hds, vds);

	// Put cursor in correct physical position.
	x -= (hds + si.cursorHotX);
	y -= (vds + si.cursorHotY);

	// Position the cursor on the display.
	gInfo.SetCursorPosition(x, y);
}

