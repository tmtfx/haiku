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

#include <create_display_modes.h>
#include <string.h>

static void
out_seq(uint8 index, uint8 val)
{
	uint16 relIO = gInfo.sharedInfo->ioBase;
	uint16 pIndex = (relIO != 0) ? (relIO + 0x44) : 0x3C4;
	out8(index, pIndex);
	out8(val, pIndex + 1);
}

static uint8
in_seq(uint8 index)
{
	uint16 relIO = gInfo.sharedInfo->ioBase;
	uint16 pIndex = (relIO != 0) ? (relIO + 0x44) : 0x3C4;
	out8(index, pIndex);
	return in8(pIndex + 1);
}

static void
out_crt(uint8 index, uint8 val)
{
	uint16 relIO = gInfo.sharedInfo->ioBase;
	uint16 pIndex = (relIO != 0) ? (relIO + 0x54) : 0x3D4;
	out8(index, pIndex);
	out8(val, pIndex + 1);
}

static uint8
in_crt(uint8 index)
{
	uint16 relIO = gInfo.sharedInfo->ioBase;
	uint16 pIndex = (relIO != 0) ? (relIO + 0x54) : 0x3D4;
	out8(index, pIndex);
	return in8(pIndex + 1);
}

static bool
is_mode_usable(display_mode* mode)
{
	uint32 bpp = 4;
	if (mode->space == B_RGB16)
		bpp = 2;
	else if (mode->space == B_CMAP8)
		bpp = 1;

	uint32 size = mode->virtual_width * mode->virtual_height * bpp;
	if (size > gInfo.sharedInfo->frameBufferSize)
		return false;

	if (mode->timing.pixel_clock > gInfo.sharedInfo->maxPixelClock)
		return false;

	return true;
}

status_t
xgi_create_mode_list(void)
{
	edid1_info* edid = gInfo.sharedInfo->hasEdid
		? &gInfo.sharedInfo->edidInfo : NULL;

	display_mode extraModes[4];
	uint32 extraCount = 0;

	/* If bootloader provided a mode, ensure it's available as an extra mode */
	if (gInfo.sharedInfo->hasBootMode && gInfo.sharedInfo->bootWidth > 0
		&& gInfo.sharedInfo->bootHeight > 0) {
		display_mode& m = extraModes[extraCount++];
		memset(&m, 0, sizeof(display_mode));
		m.timing.pixel_clock = (gInfo.sharedInfo->bootWidth
			* gInfo.sharedInfo->bootHeight * 60) / 1000;
		m.timing.h_display = gInfo.sharedInfo->bootWidth;
		m.timing.h_sync_start = m.timing.h_display + 16;
		m.timing.h_sync_end = m.timing.h_sync_start + 96;
		m.timing.h_total = m.timing.h_sync_end + 48;
		m.timing.v_display = gInfo.sharedInfo->bootHeight;
		m.timing.v_sync_start = m.timing.v_display + 1;
		m.timing.v_sync_end = m.timing.v_sync_start + 3;
		m.timing.v_total = m.timing.v_sync_end + 32;
		m.timing.flags = 0;

		m.space = (gInfo.sharedInfo->bootDepth == 16) ? B_RGB16 : B_RGB32;
		m.virtual_width = m.timing.h_display;
		m.virtual_height = m.timing.v_display;
		m.h_display_start = 0;
		m.v_display_start = 0;
		m.flags = 0;
		if (gInfo.sharedInfo->settings.hardcursor)
			m.flags |= B_HARDWARE_CURSOR;
	}

	display_mode* list = NULL;
	uint32 count = 0;

	area_id area = create_display_modes("xgi modes",
		edid,
		extraCount > 0 ? extraModes : NULL, extraCount,
		gInfo.sharedInfo->colorSpaces, gInfo.sharedInfo->colorSpaceCount,
		is_mode_usable,
		&list, &count);

	if (area < B_OK)
		return area;

	gInfo.modeListArea = area;
	gInfo.modeList = list;
	gInfo.modeCount = count;
	gInfo.sharedInfo->modeArea = area;
	gInfo.sharedInfo->modeCount = count;

	XGI_ACCEL_TRACE("create_mode_list: generated %" B_PRIu32 " modes\n", count);
	return B_OK;
}

uint32
xgi_accelerant_mode_count(void)
{
	return gInfo.modeCount;
}

status_t
xgi_get_mode_list(display_mode* dmList)
{
	if (dmList == NULL || gInfo.modeList == NULL)
		return B_BAD_VALUE;

	memcpy(dmList, gInfo.modeList, gInfo.modeCount * sizeof(display_mode));
	return B_OK;
}

status_t
xgi_propose_display_mode(display_mode* target, const display_mode* low,
	const display_mode* high)
{
	if (target == NULL || low == NULL || high == NULL)
		return B_BAD_VALUE;

	/* Basic bounds validation */
	if (target->timing.h_display < low->timing.h_display
		|| target->timing.h_display > high->timing.h_display)
		return B_BAD_VALUE;

	if (target->timing.v_display < low->timing.v_display
		|| target->timing.v_display > high->timing.v_display)
		return B_BAD_VALUE;

	if (target->timing.pixel_clock > gInfo.sharedInfo->maxPixelClock)
		target->timing.pixel_clock = gInfo.sharedInfo->maxPixelClock;

	if (target->virtual_width < target->timing.h_display)
		target->virtual_width = target->timing.h_display;
	if (target->virtual_height < target->timing.v_display)
		target->virtual_height = target->timing.v_display;

	if (gInfo.sharedInfo->settings.hardcursor)
		target->flags |= B_HARDWARE_CURSOR;

	return B_OK;
}

status_t
xgi_set_display_mode(display_mode* modeToSet)
{
	if (modeToSet == NULL)
		return B_BAD_VALUE;

	display_mode proposed = *modeToSet;
	if (xgi_propose_display_mode(&proposed, modeToSet, modeToSet) != B_OK)
		return B_BAD_VALUE;

	uint32 bytesPerPixel = 4;
	if (proposed.space == B_RGB16)
		bytesPerPixel = 2;
	else if (proposed.space == B_CMAP8)
		bytesPerPixel = 1;

	uint32 pitch = proposed.virtual_width * bytesPerPixel;
	/* Align pitch to 64 bytes */
	pitch = (pitch + 63) & ~63;

	uint32 totalSize = pitch * proposed.virtual_height;
	if (totalSize > gInfo.sharedInfo->frameBufferSize)
		return B_NO_MEMORY;

	/* Update Framebuffer Configuration */
	gInfo.sharedInfo->frameBufferConfig.frame_buffer = gInfo.frameBuffer;
	gInfo.sharedInfo->frameBufferConfig.frame_buffer_dma =
		(void*)gInfo.sharedInfo->frameBufferPCI;
	gInfo.sharedInfo->frameBufferConfig.bytes_per_row = pitch;

	/* Re-initialize 2D engine parameters for new mode */
	xgi_init_2d_engine(&proposed);

	/* Set CRTC start address to top-left (0, 0) */
	xgi_move_display_area(0, 0);

	gInfo.sharedInfo->currentMode = proposed;
	if (gInfo.sharedInfo->preferredMode.virtual_width == 0)
		gInfo.sharedInfo->preferredMode = proposed;

	gInfo.sharedInfo->dpmsMode = B_DPMS_ON;
	*modeToSet = proposed;

	XGI_ACCEL_TRACE("set_display_mode: %" B_PRIu16 "x%" B_PRIu16
		" space=0x%08" B_PRIx32 " pitch=%" B_PRIu32 "\n",
		proposed.timing.h_display, proposed.timing.v_display,
		proposed.space, pitch);

	return B_OK;
}

status_t
xgi_get_display_mode(display_mode* currentMode)
{
	if (currentMode == NULL)
		return B_BAD_VALUE;

	*currentMode = gInfo.sharedInfo->currentMode;
	return B_OK;
}

status_t
xgi_get_frame_buffer_config(frame_buffer_config* config)
{
	if (config == NULL)
		return B_BAD_VALUE;

	*config = gInfo.sharedInfo->frameBufferConfig;
	return B_OK;
}

status_t
xgi_get_pixel_clock_limits(display_mode* dm, uint32* low, uint32* high)
{
	(void)dm;
	if (low != NULL)
		*low = 10000;	/* 10 MHz */
	if (high != NULL)
		*high = gInfo.sharedInfo->maxPixelClock;	/* 350 MHz */

	return B_OK;
}

status_t
xgi_move_display_area(uint16 hDisplayStart, uint16 vDisplayStart)
{
	const display_mode& dm = gInfo.sharedInfo->currentMode;
	uint32 bpp = 4;
	if (dm.space == B_RGB16)
		bpp = 2;
	else if (dm.space == B_CMAP8)
		bpp = 1;

	uint32 pitch = gInfo.sharedInfo->frameBufferConfig.bytes_per_row;
	if (pitch == 0)
		pitch = dm.virtual_width * bpp;

	/* Calculate base in 32-bit units */
	uint32 base = (vDisplayStart * pitch + hDisplayStart * bpp) >> 2;

	/* Unlock CRTC */
	out_seq(0x05, 0x86);

	uint8 cr11 = in_crt(0x11);
	out_crt(0x11, cr11 & 0x7F);

	out_crt(0x0D, (uint8)(base & 0xFF));
	out_crt(0x0C, (uint8)((base >> 8) & 0xFF));
	out_seq(0x0D, (uint8)((base >> 16) & 0xFF));

	uint8 sr37 = in_seq(0x37);
	sr37 = (sr37 & 0xFE) | (uint8)((base >> 24) & 0x01);
	out_seq(0x37, sr37);

	out_crt(0x11, cr11);

	gInfo.sharedInfo->currentMode.h_display_start = hDisplayStart;
	gInfo.sharedInfo->currentMode.v_display_start = vDisplayStart;

	return B_OK;
}

status_t
xgi_get_timing_constraints(display_timing_constraints* dtc)
{
	if (dtc == NULL)
		return B_BAD_VALUE;

	dtc->h_res = 8;
	dtc->h_sync_min = 8;
	dtc->h_sync_max = 512;
	dtc->h_blank_min = 8;
	dtc->h_blank_max = 1024;
	dtc->v_res = 1;
	dtc->v_sync_min = 1;
	dtc->v_sync_max = 256;
	dtc->v_blank_min = 1;
	dtc->v_blank_max = 512;

	return B_OK;
}

void
xgi_set_indexed_colors(uint count, uint8 first, uint8* colorData, uint32 flags)
{
	(void)flags;
	if (colorData == NULL)
		return;

	uint16 relIO = gInfo.sharedInfo->ioBase;
	uint16 dacWrite = (relIO != 0) ? (relIO + 0x48) : 0x3C8;
	uint16 dacData = (relIO != 0) ? (relIO + 0x49) : 0x3C9;

	out8(first, dacWrite);
	for (uint i = 0; i < count; i++) {
		out8(colorData[i * 3 + 0] >> 2, dacData);
		out8(colorData[i * 3 + 1] >> 2, dacData);
		out8(colorData[i * 3 + 2] >> 2, dacData);
	}
}

uint32
xgi_dpms_capabilities(void)
{
	return B_DPMS_ON | B_DPMS_STAND_BY | B_DPMS_SUSPEND | B_DPMS_OFF;
}

uint32
xgi_dpms_mode(void)
{
	return gInfo.sharedInfo->dpmsMode;
}

status_t
xgi_set_dpms_mode(uint32 flags)
{
	uint8 sr1 = 0x00;
	uint8 sr1f = 0x00;

	switch (flags) {
		case B_DPMS_ON:
			sr1 = 0x00;
			sr1f = 0x00;
			break;
		case B_DPMS_STAND_BY:
			sr1 = 0x20;
			sr1f = 0x40;
			break;
		case B_DPMS_SUSPEND:
			sr1 = 0x20;
			sr1f = 0x80;
			break;
		case B_DPMS_OFF:
			sr1 = 0x20;
			sr1f = 0xC0;
			break;
		default:
			return B_BAD_VALUE;
	}

	out_seq(0x05, 0x86);

	uint8 curSR1 = in_seq(0x01);
	curSR1 = (curSR1 & ~0x20) | (sr1 & 0x20);
	out_seq(0x01, curSR1);

	uint8 curSR1F = in_seq(0x1F);
	curSR1F = (curSR1F & ~0xC0) | (sr1f & 0xC0);
	out_seq(0x1F, curSR1F);

	gInfo.sharedInfo->dpmsMode = flags;
	return B_OK;
}

status_t
xgi_get_preferred_mode(display_mode* preferredMode)
{
	if (preferredMode == NULL)
		return B_BAD_VALUE;

	if (gInfo.sharedInfo->preferredMode.virtual_width > 0) {
		*preferredMode = gInfo.sharedInfo->preferredMode;
		return B_OK;
	}

	if (gInfo.modeCount > 0) {
		*preferredMode = gInfo.modeList[0];
		return B_OK;
	}

	return B_ENTRY_NOT_FOUND;
}

status_t
xgi_get_edid_info(void* info, size_t size, uint32* version)
{
	if (info == NULL || version == NULL)
		return B_BAD_VALUE;

	if (!gInfo.sharedInfo->hasEdid)
		return B_NAME_NOT_FOUND;

	if (size < sizeof(edid1_info))
		return B_BUFFER_OVERFLOW;

	memcpy(info, &gInfo.sharedInfo->edidInfo, sizeof(edid1_info));
	*version = EDID_VERSION_1;
	return B_OK;
}
