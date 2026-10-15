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

#define XGI_MAX_OVERLAY_BUFFERS 4

static inline void
SetVideoReg(uint8 reg, uint8 data)
{
	uint16 relIO = gInfo.sharedInfo->ioBase;
	uint16 pIndex = (relIO != 0) ? (relIO + 0x02) : 0x3C4;
	uint16 pData = pIndex + 1;
	out8(reg, pIndex);
	out8(data, pData);
}

static inline uint8
GetVideoReg(uint8 reg)
{
	uint16 relIO = gInfo.sharedInfo->ioBase;
	uint16 pIndex = (relIO != 0) ? (relIO + 0x02) : 0x3C4;
	uint16 pData = pIndex + 1;
	out8(reg, pIndex);
	return in8(pData);
}

static inline void
SetVideoRegMask(uint8 reg, uint8 data, uint8 mask)
{
	uint8 old = GetVideoReg(reg);
	data = (old & ~mask) | (data & mask);
	SetVideoReg(reg, data);
}

static void
SetEnableOverlay(bool enable)
{
	if (enable)
		SetVideoRegMask(XGI_VI_INDEX_CONTROL_MISC0, 0x02, 0x02);
	else
		SetVideoRegMask(XGI_VI_INDEX_CONTROL_MISC0, 0x00, 0x02);
}

uint32
xgi_overlay_count(const display_mode* mode)
{
	if (mode == NULL || mode->virtual_height == 0)
		return 0;
	return 1;
}

const uint32*
xgi_overlay_supported_spaces(const display_mode* mode)
{
	(void)mode;
	static const uint32 spaces[] = {
		B_YCbCr422,
		B_RGB16,
		B_RGB15,
		0
	};
	return spaces;
}

uint32
xgi_overlay_supported_features(uint32 colorSpace)
{
	switch (colorSpace) {
		case B_YCbCr422:
		case B_RGB16:
		case B_RGB15:
			return B_OVERLAY_COLOR_KEY
				| B_OVERLAY_HORIZONTAL_FILTERING
				| B_OVERLAY_VERTICAL_FILTERING;
		default:
			return 0;
	}
}

const overlay_buffer*
xgi_allocate_overlay_buffer(color_space space, uint16 width, uint16 height)
{
	if (width == 0 || height == 0 || width > 2048 || height > 2048)
		return NULL;

	if (space != B_YCbCr422 && space != B_RGB16 && space != B_RGB15)
		return NULL;

	/* Find a free slot */
	int slot = -1;
	for (int i = 0; i < XGI_MAX_OVERLAY_BUFFERS; i++) {
		if (!gInfo.sharedInfo->overlay.bufferAllocated[i]) {
			slot = i;
			break;
		}
	}
	if (slot < 0)
		return NULL;

	uint32 stride = (width * 2 + 63) & ~63;
	uint32 size = stride * height;

	/* Offscreen VRAM base */
	uint32 base = (gInfo.sharedInfo->frameBufferConfig.bytes_per_row
		* gInfo.sharedInfo->currentMode.virtual_height + 63) & ~63;
	uint32 limit = gInfo.sharedInfo->cursorOffset;

	/* Offset calculation to avoid collision with other allocated buffers */
	uint32 offset = base;
	for (int i = 0; i < XGI_MAX_OVERLAY_BUFFERS; i++) {
		if (gInfo.sharedInfo->overlay.bufferAllocated[i]) {
			uint32 end = gInfo.sharedInfo->overlay.bufferOffset[i]
				+ gInfo.sharedInfo->overlay.bufferSize[i];
			end = (end + 63) & ~63;
			if (end > offset)
				offset = end;
		}
	}

	if (offset + size > limit)
		return NULL;

	overlay_buffer* buf = &gInfo.sharedInfo->overlay.buffers[slot];
	buf->space = space;
	buf->width = width;
	buf->height = height;
	buf->bytes_per_row = stride;
	buf->buffer = gInfo.frameBuffer + offset;
	buf->buffer_dma = (void*)(gInfo.sharedInfo->frameBufferPCI + offset);

	gInfo.sharedInfo->overlay.bufferOffset[slot] = offset;
	gInfo.sharedInfo->overlay.bufferSize[slot] = size;
	gInfo.sharedInfo->overlay.bufferAllocated[slot] = true;

	XGI_ACCEL_TRACE("allocate_overlay_buffer: slot=%d offset=0x%" B_PRIx32
		" size=%" B_PRIu32 " (%ux%u)\n", slot, offset, size, width, height);

	return buf;
}

status_t
xgi_release_overlay_buffer(const overlay_buffer* buffer)
{
	if (buffer == NULL)
		return B_BAD_VALUE;

	for (int i = 0; i < XGI_MAX_OVERLAY_BUFFERS; i++) {
		if (&gInfo.sharedInfo->overlay.buffers[i] == buffer
			&& gInfo.sharedInfo->overlay.bufferAllocated[i]) {
			gInfo.sharedInfo->overlay.bufferAllocated[i] = false;
			XGI_ACCEL_TRACE("release_overlay_buffer: slot=%d\n", i);
			return B_OK;
		}
	}

	return B_ENTRY_NOT_FOUND;
}

status_t
xgi_get_overlay_constraints(const display_mode* mode,
	const overlay_buffer* buffer, overlay_constraints* constraints)
{
	(void)mode;
	(void)buffer;
	if (constraints == NULL)
		return B_BAD_VALUE;

	/* View constraints */
	constraints->view.h_alignment = 2;
	constraints->view.v_alignment = 1;
	constraints->view.width_alignment = 2;
	constraints->view.height_alignment = 1;
	constraints->view.width.min = 4;
	constraints->view.width.max = 2048;
	constraints->view.height.min = 4;
	constraints->view.height.max = 2048;

	/* Window constraints */
	constraints->window.h_alignment = 1;
	constraints->window.v_alignment = 1;
	constraints->window.width_alignment = 1;
	constraints->window.height_alignment = 1;
	constraints->window.width.min = 4;
	constraints->window.width.max = 2048;
	constraints->window.height.min = 4;
	constraints->window.height.max = 2048;

	/* Scale limits */
	constraints->h_scale.min = 0.125f;
	constraints->h_scale.max = 8.0f;
	constraints->v_scale.min = 0.125f;
	constraints->v_scale.max = 8.0f;

	return B_OK;
}

overlay_token
xgi_allocate_overlay(void)
{
	if (gInfo.sharedInfo->overlay.token != 0)
		return NULL;

	gInfo.sharedInfo->overlay.token = 1;
	return (overlay_token)1;
}

status_t
xgi_release_overlay(overlay_token token)
{
	if (token == NULL || (uint32)(addr_t)token != gInfo.sharedInfo->overlay.token)
		return B_BAD_VALUE;

	SetEnableOverlay(false);
	gInfo.sharedInfo->overlay.active = false;
	gInfo.sharedInfo->overlay.token = 0;

	XGI_ACCEL_TRACE("release_overlay\n");
	return B_OK;
}

status_t
xgi_configure_overlay(overlay_token token, const overlay_buffer* buffer,
	const overlay_window* window, const overlay_view* view)
{
	if (token == NULL || (uint32)(addr_t)token != gInfo.sharedInfo->overlay.token)
		return B_BAD_VALUE;

	if (buffer == NULL || window == NULL || view == NULL) {
		SetEnableOverlay(false);
		gInfo.sharedInfo->overlay.active = false;
		return B_OK;
	}

	/* 1. Set Video Format in Control Misc0 (0x30) */
	uint8 fmt = 0x28;	/* YUY2 / YCbCr422 */
	if (buffer->space == B_RGB16)
		fmt = 0x10;
	else if (buffer->space == B_RGB15)
		fmt = 0x00;

	SetVideoRegMask(XGI_VI_INDEX_CONTROL_MISC0, fmt, 0x7C);

	/* 2. Window Position & Clipping */
	int16 left = (int16)(window->h_start + window->offset_left);
	int16 right = (int16)(window->h_start + window->width - window->offset_right);
	int16 top = (int16)(window->v_start + window->offset_top);
	int16 bottom = (int16)(window->v_start + window->height - window->offset_bottom);

	int16 screenW = (int16)gInfo.sharedInfo->currentMode.virtual_width;
	int16 screenH = (int16)gInfo.sharedInfo->currentMode.virtual_height;

	if (left < 0) left = 0;
	if (top < 0) top = 0;
	if (right > screenW) right = screenW;
	if (bottom > screenH) bottom = screenH;

	if (right <= left || bottom <= top) {
		SetEnableOverlay(false);
		return B_OK;
	}

	SetVideoReg(XGI_VI_INDEX_WIN_HOR_START_LOW, (uint8)(left & 0xFF));
	SetVideoReg(XGI_VI_INDEX_WIN_HOR_END_LOW, (uint8)(right & 0xFF));
	SetVideoReg(XGI_VI_INDEX_WIN_HOR_OVER, (uint8)(((right >> 8) << 4) | ((left >> 8) & 0x0F)));

	SetVideoReg(XGI_VI_INDEX_WIN_VER_START_LOW, (uint8)(top & 0xFF));
	SetVideoReg(XGI_VI_INDEX_WIN_VER_END_LOW, (uint8)(bottom & 0xFF));
	SetVideoReg(XGI_VI_INDEX_WIN_VER_OVER, (uint8)(((bottom >> 8) << 4) | ((top >> 8) & 0x0F)));

	/* 3. Buffer Start Address (word-aligned: >> 1) */
	uint32 bufferOffset = (uint32)((uint8*)buffer->buffer - gInfo.frameBuffer);
	uint32 PSY = (bufferOffset + (uint32)view->h_start * 2
		+ (uint32)view->v_start * buffer->bytes_per_row) >> 1;

	SetVideoReg(XGI_VI_INDEX_DISP_Y_BUF_START_LOW, (uint8)(PSY & 0xFF));
	SetVideoReg(XGI_VI_INDEX_DISP_Y_BUF_START_MID, (uint8)((PSY >> 8) & 0xFF));
	SetVideoReg(XGI_VI_INDEX_DISP_Y_BUF_START_HIGH, (uint8)((PSY >> 16) & 0xFF));
	SetVideoRegMask(XGI_VI_INDEX_DISP_Y_BUF_EXT_HIGH, (uint8)((PSY >> 24) & 0x03), 0x03);

	/* 4. Buffer Pitch (in unit of words) */
	uint32 tmpYPitch = buffer->bytes_per_row >> 1;
	SetVideoReg(XGI_VI_INDEX_DISP_Y_BUF_PITCH_LOW, (uint8)(tmpYPitch & 0xFF));
	SetVideoRegMask(XGI_VI_INDEX_DISP_Y_UV_PITCH_HIGH, (uint8)((tmpYPitch >> 8) & 0x0F), 0x0F);

	/* 5. Scale Factor Calculation */
	uint32 srcW = view->width;
	uint32 srcH = view->height;
	uint32 dstW = (right > left) ? (uint32)(right - left) : 1;
	uint32 dstH = (bottom > top) ? (uint32)(bottom - top) : 1;

	uint16 husf = 0;
	uint16 vusf = 0;
	uint8 intBit = 0;

	if (dstW >= srcW) {
		husf = (uint16)(((srcW << 16) + dstW - 1) / dstW);
		intBit = 0x04;
	} else {
		husf = (uint16)(((srcW - dstW) << 16) / dstW);
		intBit = 0x01;
	}

	if (dstH >= srcH) {
		vusf = (uint16)(((srcH << 16) + dstH - 1) / dstH);
		intBit |= 0x08;
	} else {
		vusf = (uint16)(((srcH - dstH) << 16) / dstH);
		intBit |= 0x02;
	}

	SetVideoReg(XGI_VI_INDEX_HOR_UP_SCALE_LOW, (uint8)(husf & 0xFF));
	SetVideoReg(XGI_VI_INDEX_HOR_UP_SCALE_HIGH, (uint8)((husf >> 8) & 0xFF));
	SetVideoReg(XGI_VI_INDEX_VER_UP_SCALE_LOW, (uint8)(vusf & 0xFF));
	SetVideoReg(XGI_VI_INDEX_VER_UP_SCALE_HIGH, (uint8)((vusf >> 8) & 0xFF));
	SetVideoRegMask(XGI_VI_INDEX_SCALE_CONTROL, (uint8)(intBit << 3), 0x78);

	/* 6. Line Buffer Size */
	SetVideoReg(XGI_VI_INDEX_LINE_BUF_SIZE, (uint8)((srcW >> 3) & 0xFF));

	/* 7. Color Key */
	if ((window->flags & B_OVERLAY_COLOR_KEY) != 0) {
		SetVideoRegMask(XGI_VI_INDEX_KEY_OVERLAY_OP, 0x03, 0x0F);	/* Dest ColorKey */

		uint8 r = window->red.value;
		uint8 g = window->green.value;
		uint8 b = window->blue.value;

		SetVideoReg(XGI_VI_INDEX_COLORKEY_RED_MIN, r);
		SetVideoReg(XGI_VI_INDEX_COLORKEY_RED_MAX, r);
		SetVideoReg(XGI_VI_INDEX_COLORKEY_GREEN_MIN, g);
		SetVideoReg(XGI_VI_INDEX_COLORKEY_GREEN_MAX, g);
		SetVideoReg(XGI_VI_INDEX_COLORKEY_BLUE_MIN, b);
		SetVideoReg(XGI_VI_INDEX_COLORKEY_BLUE_MAX, b);
	} else {
		SetVideoRegMask(XGI_VI_INDEX_KEY_OVERLAY_OP, 0x00, 0x0F);
	}

	/* 8. Enable Overlay */
	SetEnableOverlay(true);
	gInfo.sharedInfo->overlay.active = true;

	XGI_ACCEL_TRACE("configure_overlay: src=%ux%u dst=%ux%u PSY=0x%" B_PRIx32 "\n",
		srcW, srcH, dstW, dstH, PSY);

	return B_OK;
}
