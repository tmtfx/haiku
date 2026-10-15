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
#include <stdlib.h>

static inline uint32
get_bpp_code(color_space space)
{
	switch (space) {
		case B_RGB32:
		case B_RGBA32:
			return (2 << 16);
		case B_RGB16:
		case B_RGB15:
			return (1 << 16);
		default:
			return 0;
	}
}

void
xgi_wait_engine_idle(void)
{
	if (gInfo.regs == NULL)
		return;

	/* Bit 31 of MMIO 0x85CC: 1 = Idle, 0 = Busy */
	uint32 timeout = 2000000;
	while ((read32(XGI_REG_STATUS) & XGI_STATUS_2D_IDLE) == 0) {
		asm volatile("pause");
		if (--timeout == 0)
			break;
	}
}

void
xgi_init_2d_engine(display_mode* mode)
{
	if (gInfo.regs == NULL)
		return;

	/* Enable 2D accelerator in SR1E */
	uint16 relIO = gInfo.sharedInfo->ioBase;
	uint16 srIndex = (relIO != 0) ? (relIO + 0x44) : 0x3C4;
	out8(0x05, srIndex);
	out8(0x86, srIndex + 1);

	out8(0x1E, srIndex);
	uint8 sr1e = in8(srIndex + 1);
	out8(0x1E, srIndex);
	out8(sr1e | 0xDA, srIndex + 1);

	xgi_wait_engine_idle();

	uint32 bppCode = get_bpp_code((color_space)mode->space);
	uint32 bytesPerPixel = (mode->space == B_RGB32) ? 4 : ((mode->space == B_RGB16) ? 2 : 1);
	uint32 pitch = mode->virtual_width * bytesPerPixel;
	pitch = (pitch + 63) & ~63;

	write32(XGI_BR_SRC_BASE, 0);
	write32(XGI_BR_DST_BASE, 0);
	write32(XGI_BR_SRC_PITCH, (pitch & 0xFFFF) | bppCode);
	write32(XGI_BR_DST_PITCH, (0x0FFF << 16) | (pitch & 0xFFFF));

	XGI_ACCEL_TRACE("init_2d_engine: pitch=%" B_PRIu32 " bppCode=0x%08" B_PRIx32 "\n",
		pitch, bppCode);
}

uint32
xgi_accelerant_engine_count(void)
{
	return 1;
}

status_t
xgi_acquire_engine(uint32 capabilities, uint32 maxPriority, sync_token* st,
	engine_token** et)
{
	(void)capabilities;
	(void)maxPriority;

	if (gInfo.sharedInfo->engine.lock.Acquire() != B_OK)
		return B_ERROR;

	if (st != NULL)
		xgi_sync_to_token(st);

	*et = &gInfo.engineToken;
	return B_OK;
}

void
xgi_release_engine(engine_token* et, sync_token* st)
{
	if (st != NULL)
		xgi_get_sync_token(et, st);

	gInfo.sharedInfo->engine.lock.Release();
}

status_t
xgi_get_sync_token(engine_token* et, sync_token* st)
{
	if (et == NULL || st == NULL)
		return B_BAD_VALUE;

	st->engine_id = et->engine_id;
	st->counter = atomic_add((int32*)&gInfo.sharedInfo->engine.count, 1);
	return B_OK;
}

status_t
xgi_sync_to_token(sync_token* st)
{
	(void)st;
	xgi_wait_engine_idle();
	return B_OK;
}

void
xgi_screen_to_screen_blit(engine_token* et, blit_params* list, uint32 count)
{
	if (et == NULL || list == NULL || count == 0 || gInfo.regs == NULL)
		return;

	gInfo.sharedInfo->engine.lock.Lock();

	uint32 pitch = gInfo.sharedInfo->frameBufferConfig.bytes_per_row;
	uint32 bppCode = get_bpp_code((color_space)gInfo.sharedInfo->currentMode.space);

	for (uint32 i = 0; i < count; i++) {
		const blit_params& p = list[i];
		uint32 width = p.width + 1;
		uint32 height = p.height + 1;
		uint32 srcX = p.src_left;
		uint32 srcY = p.src_top;
		uint32 dstX = p.dest_left;
		uint32 dstY = p.dest_top;

		xgi_wait_engine_idle();

		write32(XGI_BR_SRC_BASE, 0);
		write32(XGI_BR_DST_BASE, 0);
		write32(XGI_BR_SRC_PITCH, (pitch & 0xFFFF) | bppCode);
		write32(XGI_BR_DST_PITCH, (0x0FFF << 16) | (pitch & 0xFFFF));
		write32(XGI_BR_SRC_XY, ((srcX & 0x1FFF) << 16) | (srcY & 0xFFFF));
		write32(XGI_BR_DST_XY, ((dstX & 0x1FFF) << 16) | (dstY & 0xFFFF));
		write32(XGI_BR_RECT_SIZE, ((height & 0xFFFF) << 16) | (width & 0xFFFF));

		/* Fire command: ROP 0xCC (Copy), BitBlt, Dest in Video RAM */
		uint32 cmd = (XGI_ROP_COPY_SRC << 8) | XGI_CMD_BITBLT | bppCode | XGI_CMD_DSTVIDEO;
		write32(XGI_BR_CMD, cmd);
	}

	gInfo.sharedInfo->engine.lock.Release();
}

void
xgi_fill_rectangle(engine_token* et, uint32 color, fill_rect_params* list,
	uint32 count)
{
	if (et == NULL || list == NULL || count == 0 || gInfo.regs == NULL)
		return;

	gInfo.sharedInfo->engine.lock.Lock();

	uint32 pitch = gInfo.sharedInfo->frameBufferConfig.bytes_per_row;
	uint32 bppCode = get_bpp_code((color_space)gInfo.sharedInfo->currentMode.space);

	for (uint32 i = 0; i < count; i++) {
		const fill_rect_params& r = list[i];
		uint32 width = r.right - r.left + 1;
		uint32 height = r.bottom - r.top + 1;

		xgi_wait_engine_idle();

		write32(XGI_BR_PAT_FG, color);
		write32(XGI_BR_DST_BASE, 0);
		write32(XGI_BR_DST_PITCH, (0x0FFF << 16) | (pitch & 0xFFFF));
		write32(XGI_BR_DST_XY, (((uint32)r.left & 0x1FFF) << 16) | ((uint32)r.top & 0xFFFF));
		write32(XGI_BR_RECT_SIZE, ((height & 0xFFFF) << 16) | (width & 0xFFFF));

		/* Fire command: ROP 0xF0 (Copy Pattern/Color), Solid FG, BitBlt */
		uint32 cmd = (XGI_ROP_COPY_PAT << 8) | XGI_CMD_PATFG | XGI_CMD_BITBLT | bppCode | XGI_CMD_DSTVIDEO;
		write32(XGI_BR_CMD, cmd);
	}

	gInfo.sharedInfo->engine.lock.Release();
}

void
xgi_invert_rectangle(engine_token* et, fill_rect_params* list, uint32 count)
{
	if (et == NULL || list == NULL || count == 0 || gInfo.regs == NULL)
		return;

	gInfo.sharedInfo->engine.lock.Lock();

	uint32 pitch = gInfo.sharedInfo->frameBufferConfig.bytes_per_row;
	uint32 bppCode = get_bpp_code((color_space)gInfo.sharedInfo->currentMode.space);

	for (uint32 i = 0; i < count; i++) {
		const fill_rect_params& r = list[i];
		uint32 width = r.right - r.left + 1;
		uint32 height = r.bottom - r.top + 1;

		xgi_wait_engine_idle();

		write32(XGI_BR_DST_BASE, 0);
		write32(XGI_BR_DST_PITCH, (0x0FFF << 16) | (pitch & 0xFFFF));
		write32(XGI_BR_DST_XY, (((uint32)r.left & 0x1FFF) << 16) | ((uint32)r.top & 0xFFFF));
		write32(XGI_BR_RECT_SIZE, ((height & 0xFFFF) << 16) | (width & 0xFFFF));

		/* Fire command: ROP 0x55 (Invert Dest: ~Dest) */
		uint32 cmd = (XGI_ROP_INVERT_DST << 8) | XGI_CMD_BITBLT | bppCode | XGI_CMD_DSTVIDEO;
		write32(XGI_BR_CMD, cmd);
	}

	gInfo.sharedInfo->engine.lock.Release();
}

void
xgi_fill_span(engine_token* et, uint32 color, uint16* list, uint32 count)
{
	if (et == NULL || list == NULL || count == 0)
		return;

	fill_rect_params* rects = (fill_rect_params*)malloc(count * sizeof(fill_rect_params));
	if (rects == NULL)
		return;

	for (uint32 i = 0; i < count; i++) {
		rects[i].top = list[i * 3 + 0];
		rects[i].bottom = rects[i].top;
		rects[i].left = list[i * 3 + 1];
		rects[i].right = list[i * 3 + 2];
	}

	xgi_fill_rectangle(et, color, rects, count);
	free(rects);
}
