/*
 * Copyright (C) 2001-2004 by Thomas Winischhofer, Vienna, Austria
 * Copyright (C) 1998-2000 by Alan Hourihane, Wigan, England
 * Copyright (C) 2000 Mike Chapman <mike@paranoia.com>
 * Copyright (C) 2000 Juanjo Santamarta <santamarta@ctv.es>
 * Copyright (C) 2000 Mitani Hiroshi <hmitani@drl.mei.co.jp>
 * Copyright (C) 2000 David Thomas <davtom@dream.org.uk>
 * Copyright (C) 2003-2004 Sung-Ching Lin, Ademar Reis
 * Copyright (C) 2003-2004 Eric Anholt <anholt@FreeBSD.org>
 * Copyright (C) 2004-2009 Nan-Hsing Chang <nan_chang@xgitech.com>
 * Copyright (C) 2004-2009 Jong Lin (XGI Technology Inc.)
 * Copyright (C) 2004-2009 Can-Ru Yeou (XGI Technology Inc.)
 * Copyright (C) 2004-2009 Hsuan, Vicent, kuku (XGI Technology Inc.)
 * Copyright (C) 2000 Mark Vojkovich
 * Copyright (C) 2000 Jonathan Bian <jonathan.bian@intel.com>
 * Copyright (C) 2005-2024 Adam Jackson <ajax@redhat.com>
 * Copyright (C) 2005-2024 Alan Barrett <apb@NetBSD.org>
 * Copyright (C) 2005-2024 Alan Coopersmith <alan.coopersmith@oracle.com>
 * Copyright (C) 2005-2024 Alberto Bursi <starshipeleven@outlook.com>
 * Copyright (C) 2005-2024 Chris Ball <cjb@laptop.org>
 * Copyright (C) 2005-2024 Connor Behan <connor.behan@gmail.com>
 * Copyright (C) 2005-2024 Dave Airlie <airlied@redhat.com>
 * Copyright (C) 2005-2024 Egbert Eich <eich@freedesktop.org>
 * Copyright (C) 2005-2024 Emil Velikov <emil.velikov@collabora.com>
 * Copyright (C) 2005-2024 Enrico Weigelt <info@metux.net>
 * Copyright (C) 2005-2024 Fernando Carrijo <fcarrijo@yahoo.com.br>
 * Copyright (C) 2005-2024 Gaetan Nadon <memsize@videotron.ca>
 * Copyright (C) 2005-2024 Ian Romanick <idr@us.ibm.com>
 * Copyright (C) 2005-2024 Jakub Bogusz <qboosh@pld-linux.org>
 * Copyright (C) 2005-2024 James Cloos <cloos@jhcloos.com>
 * Copyright (C) 2005-2024 Jeremy Huddleston <jeremyhu@apple.com>
 * Copyright (C) 2005-2024 Jesse Adkins <jesserayadkins@gmail.com>
 * Copyright (C) 2005-2024 Johannes Obermayr <johannesobermayr@gmx.de>
 * Copyright (C) 2005-2024 Peter Hutterer <peter.hutterer@who-t.net>
 * Copyright (C) 2005-2024 Rémi Cardona <remi@gentoo.org>
 * Copyright (C) 2005-2024 Stefan Dirsch <sndirsch@novell.com>
 * Copyright (C) 2005-2024 Thomas Klausner <wiz@NetBSD.org>
 * Copyright (C) 2005-2024 Trevor Woerner <twoerner@gmail.com>
 * Copyright (C) 2005-2024 Tristan Plumb <freedesktop@trstn.net>
 * Copyright (C) 2004-2009 eXtreme Graphics Innovation Corp, Inc.
 * Copyright (C) 1999-2004 The XFree86 Project, Inc.
 * Copyright (C) 2000-2003 ATI Technologies Inc.
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT
 * OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
 * THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "accelerant.h"

#include <string.h>

status_t
xgi_set_cursor_shape(uint16 width, uint16 height, uint16 hotX, uint16 hotY,
	uint8* andMask, uint8* xorMask)
{
	if (!gInfo.sharedInfo->settings.hardcursor)
		return B_NOT_ALLOWED;

	if (width > 64 || height > 64 || andMask == NULL || xorMask == NULL)
		return B_BAD_VALUE;

	if (gInfo.frameBuffer == NULL)
		return B_NO_INIT;

	uint8* cursorBuffer = gInfo.frameBuffer + gInfo.sharedInfo->cursorOffset;
	uint32 cursorBase = gInfo.sharedInfo->cursorOffset / 1024;

	gInfo.sharedInfo->cursor.hotX = hotX;
	gInfo.sharedInfo->cursor.hotY = hotY;
	gInfo.sharedInfo->cursor.width = width;
	gInfo.sharedInfo->cursor.height = height;

	/*
	 * XGI 64x64 mono cursor image is 1024 bytes:
	 * 64 lines of 16 bytes each.
	 * First 8 bytes of each line: AND mask (64 pixels).
	 * Next 8 bytes of each line: XOR mask (64 pixels).
	 * Transparent pixel: AND=1, XOR=0.
	 */
	for (uint32 y = 0; y < 64; y++) {
		uint8* row = cursorBuffer + y * 16;
		memset(row, 0xFF, 8);		/* AND mask: transparent */
		memset(row + 8, 0x00, 8);	/* XOR mask: 0 */

		if (y < height) {
			uint32 byteWidth = (width + 7) / 8;
			uint32 srcOffset = y * byteWidth;
			for (uint32 b = 0; b < byteWidth && b < 8; b++) {
				row[b] = andMask[srcOffset + b];
				row[8 + b] = xorMask[srcOffset + b];
			}
		}
	}

	/* Program cursor address in MMIO 0x8500 */
	uint32 ctrl = read32(XGI_REG_CURSOR_CTRL);
	ctrl &= 0xF0FE0000;
	ctrl |= (cursorBase & 0x1FFFF);
	write32(XGI_REG_CURSOR_CTRL, ctrl);

	/* Set standard cursor colors: White BG, Black FG */
	write32(XGI_REG_CURSOR_BG, 0x00FFFFFF);
	write32(XGI_REG_CURSOR_FG, 0x00000000);

	/* Update position with hot spot applied */
	xgi_move_cursor(gInfo.sharedInfo->cursor.x, gInfo.sharedInfo->cursor.y);

	return B_OK;
}

void
xgi_move_cursor(uint16 x, uint16 y)
{
	if (!gInfo.sharedInfo->settings.hardcursor)
		return;

	gInfo.sharedInfo->cursor.x = x;
	gInfo.sharedInfo->cursor.y = y;

	int32 posX = (int32)x - (int32)gInfo.sharedInfo->cursor.hotX;
	int32 posY = (int32)y - (int32)gInfo.sharedInfo->cursor.hotY;

	uint8 presetX = 0;
	uint8 presetY = 0;

	if (posX < 0) {
		presetX = (uint8)(-posX);
		posX = 0;
	}
	if (posY < 0) {
		presetY = (uint8)(-posY);
		posY = 0;
	}

	write32(XGI_REG_CURSOR_X, ((uint32)posX & 0xFFFF) | ((uint32)presetX << 16));
	write32(XGI_REG_CURSOR_Y, ((uint32)posY & 0xFFFF) | ((uint32)presetY << 16));
}

void
xgi_show_cursor(bool isVisible)
{
	if (!gInfo.sharedInfo->settings.hardcursor)
		return;

	uint32 ctrl = read32(XGI_REG_CURSOR_CTRL);
	uint32 cursorBase = gInfo.sharedInfo->cursorOffset / 1024;

	if (isVisible) {
		ctrl &= 0x00FE0000;
		ctrl |= XGI_CURSOR_ENABLE | (cursorBase & 0x1FFFF);
		write32(XGI_REG_CURSOR_CTRL, ctrl);
		xgi_move_cursor(gInfo.sharedInfo->cursor.x, gInfo.sharedInfo->cursor.y);
	} else {
		ctrl &= ~XGI_CURSOR_ENABLE;
		write32(XGI_REG_CURSOR_CTRL, ctrl);
	}

	gInfo.sharedInfo->cursor.visible = isVisible;
}

#ifdef IS_PIRATI_BUILD
status_t
xgi_set_cursor_bitmap(uint16 width, uint16 height, uint16 hotX, uint16 hotY,
	color_space space, uint16 bytesPerRow, const uint8* bitmapData)
{
	if (!gInfo.sharedInfo->settings.hardcursor)
		return B_NOT_ALLOWED;

	if (width > 64 || height > 64 || bitmapData == NULL)
		return B_BAD_VALUE;

	/* Convert RGBA32 / RGB16 to mono shape */
	uint8 andMask[64 * 8];
	uint8 xorMask[64 * 8];
	memset(andMask, 0xFF, sizeof(andMask));
	memset(xorMask, 0x00, sizeof(xorMask));

	uint32 byteWidth = (width + 7) / 8;

	for (uint16 y = 0; y < height; y++) {
		const uint8* srcRow = bitmapData + y * bytesPerRow;
		uint8* andRow = andMask + y * byteWidth;
		uint8* xorRow = xorMask + y * byteWidth;

		for (uint16 x = 0; x < width; x++) {
			uint8 byteIdx = x / 8;
			uint8 bitMask = 0x80 >> (x % 8);

			if (space == B_RGBA32 || space == B_RGB32) {
				const uint8* p = srcRow + x * 4;
				uint8 alpha = (space == B_RGBA32) ? p[3] : 0xFF;
				if (alpha < 128) {
					/* Transparent: AND=1, XOR=0 */
					andRow[byteIdx] |= bitMask;
					xorRow[byteIdx] &= ~bitMask;
				} else {
					/* Opaque: AND=0 */
					andRow[byteIdx] &= ~bitMask;
					/* Luminance */
					uint32 luma = (uint32)p[0] * 299 + (uint32)p[1] * 587
						+ (uint32)p[2] * 114;
					if (luma > 128000)
						xorRow[byteIdx] &= ~bitMask; /* White (BG) */
					else
						xorRow[byteIdx] |= bitMask;  /* Black (FG) */
				}
			}
		}
	}

	return xgi_set_cursor_shape(width, height, hotX, hotY, andMask, xorMask);
}

uint32
xgi_get_cursor_bits(void)
{
	return 1;
}
#endif
