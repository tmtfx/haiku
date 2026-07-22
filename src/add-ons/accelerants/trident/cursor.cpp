/*
 * Copyright 2026, Gemini CLI. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Gemini CLI <gemini-cli@google.com>
 */


#include "accel.h"


uint32
GetCursorBits(void)
{
	return 2;
}


status_t
SetCursorShape(uint16 width, uint16 height, uint16 hot_x, uint16 hot_y,
				uint8* andMask, uint8* xorMask)
{
	(void)width; (void)height; (void)hot_x; (void)hot_y;
	(void)andMask; (void)xorMask;

	SharedInfo& si = *gInfo.sharedInfo;
	si.cursorHotX = hot_x;
	si.cursorHotY = hot_y;

	return B_OK;
}


status_t
SetCursorBitmap(uint16 width, uint16 height, uint16 hot_x, uint16 hot_y,
                color_space colorSpace, uint16 bytesPerRow, const uint8* bitmapData)
{
	(void)width; (void)height; (void)hot_x; (void)hot_y;
	(void)colorSpace; (void)bytesPerRow; (void)bitmapData;

	SharedInfo& si = *gInfo.sharedInfo;
	si.cursorHotX = hot_x;
	si.cursorHotY = hot_y;

	return B_OK;
}


void
MoveCursor(uint16 xPos, uint16 yPos)
{
	(void)xPos; (void)yPos;
}


void
ShowCursor(bool bShow)
{
	(void)bShow;
}
