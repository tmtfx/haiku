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

/*
 * Porting of vWaitCRT1VerticalRetrace() (refcode/xgi_dac.c:633-657): wait
 * for a full retrace cycle (not yet in retrace -> enters retrace -> exits
 * retrace) via the standard VGA Input Status 1 register, bit 3
 * (IS_BIT_VERT_ACTIVE). Same I/O port as out_att()'s flip-flop reset
 * (relIO+0x5A / 0x3DA), already proven correct by the working CRTC/PLL
 * code in DisplayModes.cpp.
 *
 * The refcode calls this from Volari_ShowCursor()/Volari_HideCursor()
 * (not from LoadCursorImage) - i.e. right after flipping the cursor
 * enable bit, matching xgi_show_cursor() here. If the hardware only
 * latches the enable/address bits at the next vblank, writing them
 * without ever waiting for one (as this code did before) could explain
 * why the cursor only shows up intermittently.
 */
static void
xgi_wait_vertical_retrace(void)
{
	uint16 relIO = gInfo.sharedInfo->ioBase;
	uint16 pStat = (relIO != 0) ? (relIO + 0x5A) : 0x3DA;
	int32 count = 0x10000;

	while (--count > 0 && !(in8(pStat) & 0x08))
		;
	count = 0x10000;
	while (--count > 0 && (in8(pStat) & 0x08))
		;
}

/*
 * XGI's hardware cursor does not accept BeOS' native (AND, XOR) mono
 * planes directly: the Xorg reference driver declares
 * HARDWARE_CURSOR_INVERT_MASK | HARDWARE_CURSOR_AND_SOURCE_WITH_MASK |
 * HARDWARE_CURSOR_SWAP_SOURCE_AND_MASK for this chip
 * (refcode/xgi_cursor.c:198-204), which describe a transform relative to
 * X11's own (source, mask) cursor convention - not BeOS' AND/XOR one.
 * Traced against the actual transform code (Xorg xf86HWCurs.c,
 * RealizeCursorInterleave0(), formerly xf86Cursor.c) to derive the
 * equivalent directly from BeOS' andMask/xorMask:
 *
 *   hw mask   = andMask            (1 = invisible, matches BeOS "and=1
 *                                    transparent" - no inversion needed:
 *                                    the flag inverts X11's opposite
 *                                    convention, which we never go
 *                                    through here)
 *   hw source = (~andMask) & (~xorMask)   (1 only at opaque+foreground
 *                                           pixels; 0 elsewhere, zeroed
 *                                           by the mask anywhere
 *                                           invisible, like
 *                                           AND_SOURCE_WITH_MASK asks)
 *
 * and SWAP_SOURCE_AND_MASK puts the mask plane first in each 16-byte row
 * (bytes 0-7), source second (bytes 8-15).
 */
static void
xgi_build_cursor_bitmap(uint16 width, uint16 height, const uint8* andMask,
	const uint8* xorMask, uint8* dst /* 1024 bytes: 64 rows * 16 bytes */)
{
	uint32 byteWidth = (width + 7) / 8;
	for (uint32 y = 0; y < 64; y++) {
		uint8* row = dst + y * 16;
		for (uint32 b = 0; b < 8; b++) {
			uint8 andByte = (y < height && b < byteWidth)
				? andMask[y * byteWidth + b] : 0xFF;
			uint8 xorByte = (y < height && b < byteWidth)
				? xorMask[y * byteWidth + b] : 0x00;
			uint8 mask = andByte;
			uint8 src = (uint8)(~andByte) & (uint8)(~xorByte);
#if XGI_CURSOR_SWAP_PLANES
			row[b] = mask;
			row[8 + b] = src;
#else
			row[b] = src;
			row[8 + b] = mask;
#endif
		}
	}
}

status_t
xgi_set_cursor_shape(uint16 width, uint16 height, uint16 hotX, uint16 hotY,
	uint8* andMask, uint8* xorMask)
{
	if (!gInfo.sharedInfo->settings.hardcursor) {
		debug_printf("xgi.accelerant: set_cursor_shape: REFUSED,"
			" hardcursor setting is off\n");
		return B_NOT_ALLOWED;
	}

	if (width > 64 || height > 64 || andMask == NULL || xorMask == NULL) {
		debug_printf("xgi.accelerant: set_cursor_shape: REFUSED, bad args"
			" %" B_PRIu16 "x%" B_PRIu16 " andMask=%p xorMask=%p\n",
			width, height, andMask, xorMask);
		return B_BAD_VALUE;
	}

	if (gInfo.frameBuffer == NULL) {
		debug_printf("xgi.accelerant: set_cursor_shape: REFUSED,"
			" frameBuffer is NULL\n");
		return B_NO_INIT;
	}

	uint8* cursorBuffer = gInfo.frameBuffer + gInfo.sharedInfo->cursorOffset;
	uint32 cursorBase = gInfo.sharedInfo->cursorOffset / 1024;

	gInfo.sharedInfo->cursor.hotX = hotX;
	gInfo.sharedInfo->cursor.hotY = hotY;
	gInfo.sharedInfo->cursor.width = width;
	gInfo.sharedInfo->cursor.height = height;

	/* Local copy, so a concurrent caller touching the original andMask/
	 * xorMask buffer (e.g. app_server reusing/freeing it from another
	 * thread) can't change what we convert mid-read. */
	uint8 localAnd[64 * 8];
	uint8 localXor[64 * 8];
	uint32 maskBytes = ((uint32)width + 7) / 8 * height;
	if (maskBytes > sizeof(localAnd))
		maskBytes = sizeof(localAnd);
	memcpy(localAnd, andMask, maskBytes);
	memcpy(localXor, xorMask, maskBytes);

	/*
	 * XGI 64x64 cursor image is 1024 bytes: 64 lines of 16 bytes each,
	 * as a (mask, source) pair per line - see xgi_build_cursor_bitmap().
	 */
	xgi_build_cursor_bitmap(width, height, localAnd, localXor, cursorBuffer);

	/* Dump every real row (up to height) as written to VRAM, to compare
	 * full bitmaps between a working and a failing cursor instead of
	 * just the first few rows / opaque-pixel counts. */
	uint32 dumpRows = (height < 64) ? height : 64;
	for (uint32 row = 0; row < dumpRows; row++) {
		uint8* r = cursorBuffer + row * 16;
		debug_printf("xgi.accelerant: cursorbuf row%" B_PRIu32
			": mask=%02x%02x%02x%02x%02x%02x%02x%02x"
			" src=%02x%02x%02x%02x%02x%02x%02x%02x\n", row,
			r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7],
			r[8], r[9], r[10], r[11], r[12], r[13], r[14], r[15]);
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

	debug_printf("xgi.accelerant: set_cursor_shape: done, %" B_PRIu16 "x%"
		B_PRIu16 " hot=(%" B_PRIu16 ",%" B_PRIu16 ") cursorBase=0x%" B_PRIx32
		" ctrl_after=0x%08" B_PRIx32 " visible=%d\n",
		width, height, hotX, hotY, cursorBase,
		read32(XGI_REG_CURSOR_CTRL), gInfo.sharedInfo->cursor.visible);

	return B_OK;
}

void
xgi_move_cursor(uint16 x, uint16 y)
{
	if (!gInfo.sharedInfo->settings.hardcursor) {
		debug_printf("xgi.accelerant: move_cursor: REFUSED,"
			" hardcursor setting is off\n");
		return;
	}

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
	if (!gInfo.sharedInfo->settings.hardcursor) {
		debug_printf("xgi.accelerant: show_cursor: REFUSED,"
			" hardcursor setting is off (isVisible=%d)\n", isVisible);
		return;
	}

	uint32 ctrlBefore = read32(XGI_REG_CURSOR_CTRL);
	uint32 ctrl = ctrlBefore;
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

	/* refcode/xgi_cursor.c:68,82 - Volari_ShowCursor/HideCursor both wait
	 * for a full retrace cycle right after flipping the enable bit. */
	xgi_wait_vertical_retrace();

	debug_printf("xgi.accelerant: show_cursor: isVisible=%d ctrl_before=0x%08"
		B_PRIx32 " ctrl_after=0x%08" B_PRIx32 "\n", isVisible, ctrlBefore,
		read32(XGI_REG_CURSOR_CTRL));
}

#ifdef IS_PIRATI_BUILD
status_t
xgi_set_cursor_bitmap(uint16 width, uint16 height, uint16 hotX, uint16 hotY,
	color_space space, uint16 bytesPerRow, const uint8* bitmapData)
{
	if (!gInfo.sharedInfo->settings.hardcursor) {
		debug_printf("xgi.accelerant: set_cursor_bitmap: REFUSED,"
			" hardcursor setting is off\n");
		return B_NOT_ALLOWED;
	}

	uint32 opaqueCount = 0, sampleAlpha = 0, sampleB = 0, sampleG = 0,
		sampleR = 0;
	if (bitmapData != NULL && width > 0 && height > 0) {
		/* Sample the pixel at (width/2, height/2) for the log line below,
		 * regardless of what the full conversion decides. */
		const uint8* p = bitmapData + (height / 2) * bytesPerRow
			+ (width / 2) * 4;
		sampleB = p[0];
		sampleG = p[1];
		sampleR = p[2];
		sampleAlpha = p[3];
	}
	debug_printf("xgi.accelerant: set_cursor_bitmap: %" B_PRIu16 "x%" B_PRIu16
		" hot=(%" B_PRIu16 ",%" B_PRIu16 ") space=0x%" B_PRIx32
		" bpr=%" B_PRIu16 " centerPixel(BGRA)=%" B_PRIu32 ",%" B_PRIu32
		",%" B_PRIu32 ",%" B_PRIu32 "\n",
		width, height, hotX, hotY, (uint32)space, bytesPerRow,
		sampleB, sampleG, sampleR, sampleAlpha);

	if (width > 64 || height > 64 || bitmapData == NULL) {
		debug_printf("xgi.accelerant: set_cursor_bitmap: REFUSED, bad args"
			" %" B_PRIu16 "x%" B_PRIu16 " bitmapData=%p\n",
			width, height, bitmapData);
		return B_BAD_VALUE;
	}

	/* Only B_RGBA32/B_RGB32 (memory order BGRA, GraphicsDefs.h) are
	 * converted below. Anything else must fail rather than silently hand
	 * xgi_set_cursor_shape() an all-transparent mask (the previous
	 * behaviour here: unhandled formats left andMask at its all-0xFF
	 * default, making the cursor fully invisible) - returning B_BAD_VALUE
	 * makes app_server fall back to its software cursor instead. */
	if (space != B_RGBA32 && space != B_RGB32) {
		debug_printf("xgi.accelerant: set_cursor_bitmap: unsupported"
			" color_space 0x%" B_PRIx32 " (%" B_PRIu16 "x%" B_PRIu16
			"), falling back to SW cursor\n",
			(uint32)space, width, height);
		return B_BAD_VALUE;
	}

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

			/* B_RGBA32/B_RGB32 memory order is B,G,R,A (BGRA) - see
			 * GraphicsDefs.h. p[0]=Blue, p[1]=Green, p[2]=Red, p[3]=Alpha. */
			const uint8* p = srcRow + x * 4;
			uint8 alpha = (space == B_RGBA32) ? p[3] : 0xFF;
			if (alpha < 128) {
				/* Transparent: AND=1, XOR=0 */
				andRow[byteIdx] |= bitMask;
				xorRow[byteIdx] &= ~bitMask;
			} else {
				/* Opaque: AND=0 */
				opaqueCount++;
				andRow[byteIdx] &= ~bitMask;
				/* Luminance (Rec. 601, BGRA order: p[0]=B,p[1]=G,p[2]=R) */
				uint32 luma = (uint32)p[2] * 299 + (uint32)p[1] * 587
					+ (uint32)p[0] * 114;
				if (luma > 128000)
					xorRow[byteIdx] &= ~bitMask; /* White (BG) */
				else
					xorRow[byteIdx] |= bitMask;  /* Black (FG) */
			}
		}
	}

	debug_printf("xgi.accelerant: set_cursor_bitmap: %" B_PRIu32
		" opaque px out of %" B_PRIu32 "\n", opaqueCount,
		(uint32)width * height);

	return xgi_set_cursor_shape(width, height, hotX, hotY, andMask, xorMask);
}

uint32
xgi_get_cursor_bits(void)
{
	return 1;
}
#endif
