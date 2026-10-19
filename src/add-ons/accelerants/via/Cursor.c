/*
	Copyright 1999, Be Incorporated.   All Rights Reserved.
	This file may be used under the terms of the Be Sample Code License.

	Other authors:
	Mark Watson,
	Rudolf Cornelissen 4/2003-9/2005

	Portions based on the OpenChrome Project (https://www.freedesktop.org/wiki/Openchrome):
	Copyright 1998-2007 VIA Technologies, Inc. All Rights Reserved.
	Copyright 2001-2003 S3 Graphics, Inc. All Rights Reserved.
	Authors: Kevin Brace, James Simmons, Bartosz Kosiorek
*/

#define MODULE_BIT 0x20000000

#include <string.h>
#include "acc_std.h"
#include "macros.h"

status_t SET_CURSOR_SHAPE(uint16 width, uint16 height, uint16 hot_x, uint16 hot_y, uint8 *andMask, uint8 *xorMask) 
{
	LOG(4,("SET_CURSOR_SHAPE: width %d, height %d, hot_x %d, hot_y %d\n",
		width, height, hot_x, hot_y));

	if ((width != 16) || (height != 16))
	{
		return B_ERROR;
	}
	else if ((hot_x >= width) || (hot_y >= height))
	{
		return B_ERROR;
	}
	else
	{
		si->cursor.width = width;
		si->cursor.height = height;
		si->cursor.hot_x = hot_x;
		si->cursor.hot_y = hot_y;

		if (si->ps.card_arch >= CX700) {
			/* Convert 16x16 mono masks to ARGB32 for Chrome9 / modern chips */
			uint32* dst = (uint32*)si->framebuffer;
			memset(dst, 0, 64 * 64 * 4);
			for (uint32 y = 0; y < 16; y++) {
				for (uint32 x = 0; x < 16; x++) {
					int byteOffset = y * 2 + (x / 8);
					int bit = 7 - (x % 8);
					bool andBit = (andMask[byteOffset] >> bit) & 1;
					bool xorBit = (xorMask[byteOffset] >> bit) & 1;
					if (!andBit && !xorBit)
						dst[y * 64 + x] = 0xFF000000; /* Black */
					else if (!andBit && xorBit)
						dst[y * 64 + x] = 0xFFFFFFFF; /* White */
					else if (andBit && !xorBit)
						dst[y * 64 + x] = 0x00000000; /* Transparent */
					else
						dst[y * 64 + x] = 0xFF000000; /* Invert / solid */
				}
			}
			CRTCDW(CURSOR_MODE, 0x00000001);
		} else {
			head1_cursor_define(andMask, xorMask);
			if ((si->dm.flags & DUALHEAD_BITS) != DUALHEAD_OFF)
				head2_cursor_define(andMask, xorMask);
			CRTCDW(CURSOR_MODE, 0x00000003);
		}
	}

	return B_OK;
}

/* Move the cursor to the specified position on the desktop, taking account of virtual/dual issues */
void MOVE_CURSOR(uint16 x, uint16 y) 
{
	uint16 hds = si->dm.h_display_start;	/* the current horizontal starting pixel */
	uint16 vds = si->dm.v_display_start;	/* the current vertical starting line */
	uint16 h_adjust;                     

	/* clamp cursor to display */
	if (x >= si->dm.virtual_width) x = si->dm.virtual_width - 1;
	if (y >= si->dm.virtual_height) y = si->dm.virtual_height - 1;

	/* store, for our info */
	si->cursor.x = x;
	si->cursor.y = y;

	/* set up minimum amount to scroll */
	if (si->dm.flags & DUALHEAD_BITS)
	{
		//fixme for VIA...
		switch(si->dm.space)
		{
		case B_RGB16_LITTLE:
			h_adjust = 0x1f;
			break;
		case B_RGB32_LITTLE:
			h_adjust = 0x0f;
			break;
		default:
			h_adjust = 0x1f;
			break;
		}
	}
	else
	{
		switch(si->dm.space)
		{
		case B_CMAP8:
			h_adjust = 0x07;
			break;
		case B_RGB15_LITTLE:case B_RGB16_LITTLE:
			h_adjust = 0x03;
			break;
		case B_RGB32_LITTLE:
			h_adjust = 0x01;
			break;
		default:
			h_adjust = 0x07;
			break;
		}
	}

	/* adjust h/v_display_start to move cursor onto screen */
	switch (si->dm.flags & DUALHEAD_BITS)
	{
	case DUALHEAD_ON:
	case DUALHEAD_SWITCH:
		if (x >= ((si->dm.timing.h_display * 2) + hds))
		{
			hds = ((x - (si->dm.timing.h_display * 2)) + 1 + h_adjust) & ~h_adjust;
			/* make sure we stay within the display! */
			if ((hds + (si->dm.timing.h_display * 2)) > si->dm.virtual_width)
				hds -= (h_adjust + 1);
		}
		else if (x < hds)
			hds = x & ~h_adjust;
		break;
	default:
		if (x >= (si->dm.timing.h_display + hds))
		{
			hds = ((x - si->dm.timing.h_display) + 1 + h_adjust) & ~h_adjust;
			/* make sure we stay within the display! */
			if ((hds + si->dm.timing.h_display) > si->dm.virtual_width)
				hds -= (h_adjust + 1);
		}
		else if (x < hds)
			hds = x & ~h_adjust;
		break;
	}

	if (y >= (si->dm.timing.v_display + vds))
		vds = y - si->dm.timing.v_display + 1;
	else if (y < vds)
		vds = y;

	/* reposition the desktop _and_ the overlay on the display if required */
	if ((hds!=si->dm.h_display_start) || (vds!=si->dm.v_display_start)) 
	{
		MOVE_DISPLAY(hds,vds);
		eng_bes_move_overlay();
	}

	/* put cursor in correct physical position, so stay onscreen (rel. to CRTC) */
	if (x > (hds + si->cursor.hot_x)) x -= (hds + si->cursor.hot_x);
	else x = 0;
	if (y > (vds + si->cursor.hot_y)) y -= (vds + si->cursor.hot_y);
	else y = 0;

	/* position the cursor on the display */
	switch (si->dm.flags & DUALHEAD_BITS)
	{
	case DUALHEAD_CLONE:
		head1_cursor_position(x,y);
		head2_cursor_position(x,y);
		break;
	case DUALHEAD_ON:
	case DUALHEAD_SWITCH:
		if (x < si->dm.timing.h_display)
		{
			if (si->cursor.dh_right)
			{
				LOG(4,("MOVE_CURSOR: now on left side\n"));
				head2_cursor_hide();
				head1_cursor_show();
				si->cursor.dh_right = false;
			}
			head1_cursor_position(x, y);
		}
		else
		{
			if (!si->cursor.dh_right)
			{
				LOG(4,("MOVE_CURSOR: now on right side\n"));
				head1_cursor_hide();
				head2_cursor_show();
				si->cursor.dh_right = true;
			}
			head2_cursor_position((x - si->dm.timing.h_display), y);
		}
		break;
	default: /* singlehead mode */
		head1_cursor_position(x,y);
		break;
	}
}

void SHOW_CURSOR(bool is_visible) 
{
	/* record for our info */
	si->cursor.is_visible = is_visible;

	switch (si->dm.flags & DUALHEAD_BITS)
	{
	case DUALHEAD_CLONE:
		if (is_visible)
		{
			head1_cursor_show();
			head2_cursor_show();
		}
		else
		{
			head1_cursor_hide();
			head2_cursor_hide();
		}
		break;
	case DUALHEAD_ON:
	case DUALHEAD_SWITCH:
		if (is_visible)
		{
			if (!si->cursor.dh_right)
			{
				head1_cursor_show();
			}
			else
			{
				head2_cursor_show();
			}
		}
		else
		{
			head1_cursor_hide();
			head2_cursor_hide();
		}
		break;
	default: /* singlehead mode */
		if (is_visible)
		{
			head1_cursor_show();
		}
		else
		{
			head1_cursor_hide();
		}
		break;
	}
}


/* Pirati Del Frico extension: returns number of color bits supported by HW cursor */
uint32 GET_CURSOR_BITS(void)
{
	if (si->ps.card_arch >= CX700)
		return 32; /* ARGB32 TrueColor hardware cursor */
	return 1; /* 1-bit mono + transparency */
}

/* Pirati Del Frico extension: sets arbitrary bitmap cursor */
status_t SET_CURSOR_BITMAP(uint16 width, uint16 height, uint16 hotX, uint16 hotY,
	color_space colorSpace, uint16 bytesPerRow, const uint8* bitmapData)
{
	if (width > 64 || height > 64 || hotX >= width || hotY >= height)
		return B_ERROR;

	if (bitmapData == NULL || si->framebuffer == NULL)
		return B_NO_INIT;

	si->cursor.width = width;
	si->cursor.height = height;
	si->cursor.hot_x = hotX;
	si->cursor.hot_y = hotY;

	if (si->ps.card_arch >= CX700) {
		/* TrueColor ARGB32 cursor (up to 64x64x4) */
		uint32* dst = (uint32*)si->framebuffer;
		memset(dst, 0, 64 * 64 * 4);

		if (colorSpace != B_RGBA32 && colorSpace != B_RGB32)
			return B_ERROR;

		for (uint32 y = 0; y < height && y < 64; y++) {
			const uint8* srcRow = bitmapData + (y * bytesPerRow);
			uint32* dstRow = dst + (y * 64);
			for (uint32 x = 0; x < width && x < 64; x++) {
				const uint8* pixel = srcRow + (x * 4);
				uint8 b = pixel[0];
				uint8 g = pixel[1];
				uint8 r = pixel[2];
				uint8 a = (colorSpace == B_RGBA32) ? pixel[3] : 0xFF;

				if (a == 0)
					continue;

				dstRow[x] = ((uint32)a << 24) | ((uint32)r << 16) | ((uint32)g << 8) | b;
			}
		}

		/* Enable 64x64 ARGB32 cursor: bit 1 = 0 (64x64), bit 0 = 1 (enable) */
		CRTCDW(CURSOR_MODE, 0x00000001);
	} else {
		/* 1-bit Mono cursor (AND / XOR planes converted from 32-bit bitmap) */
		uint8 andMask[128];
		uint8 xorMask[128];

		memset(andMask, 0xFF, sizeof(andMask));
		memset(xorMask, 0x00, sizeof(xorMask));

		for (uint32 y = 0; y < height && y < 16; y++) {
			const uint8* srcRow = bitmapData + (y * bytesPerRow);
			for (uint32 x = 0; x < width && x < 16; x++) {
				const uint8* pixel = srcRow + (x * 4);
				uint8 b = pixel[0];
				uint8 g = pixel[1];
				uint8 r = pixel[2];
				uint8 a = (colorSpace == B_RGBA32) ? pixel[3] : 0xFF;

				if (a < 128)
					continue; /* transparent */

				int byteOffset = y * 2 + (x / 8);
				int bit = 7 - (x % 8);

				andMask[byteOffset] &= ~(1 << bit);

				uint32 luma = (r + g + b) / 3;
				if (luma > 128)
					xorMask[byteOffset] |= (1 << bit); /* white */
			}
		}

		head1_cursor_define(andMask, xorMask);
		if ((si->dm.flags & DUALHEAD_BITS) != DUALHEAD_OFF)
			head2_cursor_define(andMask, xorMask);

		/* 32x32 mono cursor: bit 1 = 1 (32x32), bit 0 = 1 (enable) */
		CRTCDW(CURSOR_MODE, 0x00000003);
	}

	MOVE_CURSOR(si->cursor.x, si->cursor.y);
	return B_OK;
}
