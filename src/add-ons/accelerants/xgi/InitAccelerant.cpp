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

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

AccelerantInfo gInfo;

static status_t
init_common(int fd, bool isClone)
{
	gInfo.deviceFD = fd;
	gInfo.isClone = isClone;
	gInfo.sharedInfoArea = -1;
	gInfo.sharedInfo = NULL;
	gInfo.regsArea = -1;
	gInfo.regs = NULL;
	gInfo.frameBufferArea = -1;
	gInfo.frameBuffer = NULL;
	gInfo.modeListArea = -1;
	gInfo.modeList = NULL;
	gInfo.modeCount = 0;

	XgiGetPrivateData gpd;
	gpd.magic = XGI_PRIVATE_DATA_MAGIC;
	if (ioctl(fd, XGI_GET_PRIVATE_DATA, &gpd, sizeof(gpd)) != B_OK)
		return B_ERROR;

	gInfo.sharedInfoArea = clone_area("xgi shared info clone",
		(void**)&gInfo.sharedInfo, B_ANY_ADDRESS,
		B_READ_AREA | B_WRITE_AREA, gpd.sharedInfoArea);
	if (gInfo.sharedInfoArea < B_OK)
		return gInfo.sharedInfoArea;

	gInfo.regsArea = clone_area("xgi regs clone",
		(void**)&gInfo.regs, B_ANY_ADDRESS,
		B_READ_AREA | B_WRITE_AREA, gInfo.sharedInfo->regsArea);
	if (gInfo.regsArea < B_OK) {
		delete_area(gInfo.sharedInfoArea);
		gInfo.sharedInfoArea = -1;
		gInfo.sharedInfo = NULL;
		return gInfo.regsArea;
	}

	gInfo.frameBufferArea = clone_area("xgi framebuffer clone",
		(void**)&gInfo.frameBuffer, B_ANY_ADDRESS,
		B_READ_AREA | B_WRITE_AREA, gInfo.sharedInfo->frameBufferArea);
	if (gInfo.frameBufferArea < B_OK) {
		delete_area(gInfo.regsArea);
		delete_area(gInfo.sharedInfoArea);
		gInfo.regsArea = -1;
		gInfo.regs = NULL;
		gInfo.sharedInfoArea = -1;
		gInfo.sharedInfo = NULL;
		return gInfo.frameBufferArea;
	}

	gInfo.sharedInfo->frameBuffer = gInfo.frameBuffer;

	/* Setup 2D Engine Token */
	gInfo.engineToken.engine_id = 1;
	gInfo.engineToken.capability_mask = 0;
	gInfo.engineToken.opaque = NULL;

	/* Initialize display modes */
	status_t status = xgi_create_mode_list();
	if (status != B_OK) {
		xgi_uninit_accelerant();
		return status;
	}

	/* Initial display mode setup */
	if (!isClone) {
		display_mode targetMode;
		memset(&targetMode, 0, sizeof(targetMode));

		if (gInfo.sharedInfo->hasBootMode && gInfo.sharedInfo->bootWidth > 0
			&& gInfo.sharedInfo->bootHeight > 0) {
			/* Construct display mode from bootloader mode */
			targetMode.timing.pixel_clock = (gInfo.sharedInfo->bootWidth
				* gInfo.sharedInfo->bootHeight * 60) / 1000;
			targetMode.timing.h_display = gInfo.sharedInfo->bootWidth;
			targetMode.timing.h_sync_start = targetMode.timing.h_display + 16;
			targetMode.timing.h_sync_end = targetMode.timing.h_sync_start + 96;
			targetMode.timing.h_total = targetMode.timing.h_sync_end + 48;
			targetMode.timing.v_display = gInfo.sharedInfo->bootHeight;
			targetMode.timing.v_sync_start = targetMode.timing.v_display + 1;
			targetMode.timing.v_sync_end = targetMode.timing.v_sync_start + 3;
			targetMode.timing.v_total = targetMode.timing.v_sync_end + 32;
			targetMode.timing.flags = 0;

			targetMode.space = (gInfo.sharedInfo->bootDepth == 16)
				? B_RGB16 : B_RGB32;
			targetMode.virtual_width = targetMode.timing.h_display;
			targetMode.virtual_height = targetMode.timing.v_display;
			targetMode.h_display_start = 0;
			targetMode.v_display_start = 0;
			targetMode.flags = 0;
			if (gInfo.sharedInfo->settings.hardcursor)
				targetMode.flags |= B_HARDWARE_CURSOR;
		} else if (gInfo.modeCount > 0) {
			targetMode = gInfo.modeList[0];
		}

		if (targetMode.timing.h_display > 0) {
			xgi_set_display_mode(&targetMode);
		}
	}

	return B_OK;
}

status_t
xgi_init_accelerant(int fd)
{
	status_t status = init_common(fd, false);
	if (status == B_OK) {
		if (gInfo.sharedInfo->accelerantInUse) {
			xgi_uninit_accelerant();
			return B_NOT_ALLOWED;
		}
		gInfo.sharedInfo->accelerantInUse = true;
	}
	return status;
}

void
xgi_uninit_accelerant(void)
{
	if (gInfo.sharedInfo != NULL && !gInfo.isClone) {
		gInfo.sharedInfo->accelerantInUse = false;
	}

	if (gInfo.modeListArea >= B_OK)
		delete_area(gInfo.modeListArea);
	if (gInfo.frameBufferArea >= B_OK)
		delete_area(gInfo.frameBufferArea);
	if (gInfo.regsArea >= B_OK)
		delete_area(gInfo.regsArea);
	if (gInfo.sharedInfoArea >= B_OK)
		delete_area(gInfo.sharedInfoArea);

	gInfo.modeListArea = -1;
	gInfo.modeList = NULL;
	gInfo.modeCount = 0;
	gInfo.frameBufferArea = -1;
	gInfo.frameBuffer = NULL;
	gInfo.regsArea = -1;
	gInfo.regs = NULL;
	gInfo.sharedInfoArea = -1;
	gInfo.sharedInfo = NULL;
}

status_t
xgi_clone_accelerant(void* data)
{
	char path[B_PATH_NAME_LENGTH];
	snprintf(path, sizeof(path), "/dev/%s", (const char*)data);

	int fd = open(path, O_RDWR);
	if (fd < 0)
		return errno;

	status_t status = init_common(fd, true);
	if (status != B_OK) {
		close(fd);
		return status;
	}

	return B_OK;
}

ssize_t
xgi_accelerant_clone_info_size(void)
{
	return B_PATH_NAME_LENGTH;
}

void
xgi_get_accelerant_clone_info(void* data)
{
	strlcpy((char*)data, gInfo.sharedInfo->deviceName, B_PATH_NAME_LENGTH);
}

status_t
xgi_get_accelerant_device_info(accelerant_device_info* adi)
{
	adi->version = B_ACCELERANT_VERSION;
	strlcpy(adi->name, gInfo.sharedInfo->chipName, sizeof(adi->name));
	strlcpy(adi->chipset, "XGI Volari", sizeof(adi->chipset));
	snprintf(adi->serial_no, sizeof(adi->serial_no), "Rev 0x%02x",
		gInfo.sharedInfo->revision);
	adi->memory = gInfo.sharedInfo->frameBufferSize;
	adi->dac_speed = gInfo.sharedInfo->maxPixelClock;
	return B_OK;
}

sem_id
xgi_accelerant_retrace_semaphore(void)
{
	return B_ERROR;
}

extern "C" void*
get_accelerant_hook(uint32 feature, void* data)
{
	(void)data;

	switch (feature) {
		/* Accelerant Initialization */
		case B_INIT_ACCELERANT:
			return (void*)xgi_init_accelerant;
		case B_UNINIT_ACCELERANT:
			return (void*)xgi_uninit_accelerant;
		case B_CLONE_ACCELERANT:
			return (void*)xgi_clone_accelerant;
		case B_ACCELERANT_CLONE_INFO_SIZE:
			return (void*)xgi_accelerant_clone_info_size;
		case B_GET_ACCELERANT_CLONE_INFO:
			return (void*)xgi_get_accelerant_clone_info;
		case B_GET_ACCELERANT_DEVICE_INFO:
			return (void*)xgi_get_accelerant_device_info;
		case B_ACCELERANT_RETRACE_SEMAPHORE:
			return (void*)xgi_accelerant_retrace_semaphore;

		/* Display Modes */
		case B_ACCELERANT_MODE_COUNT:
			return (void*)xgi_accelerant_mode_count;
		case B_GET_MODE_LIST:
			return (void*)xgi_get_mode_list;
		case B_PROPOSE_DISPLAY_MODE:
			return (void*)xgi_propose_display_mode;
		case B_SET_DISPLAY_MODE:
			return (void*)xgi_set_display_mode;
		case B_GET_DISPLAY_MODE:
			return (void*)xgi_get_display_mode;
		case B_GET_FRAME_BUFFER_CONFIG:
			return (void*)xgi_get_frame_buffer_config;
		case B_GET_PIXEL_CLOCK_LIMITS:
			return (void*)xgi_get_pixel_clock_limits;
		case B_MOVE_DISPLAY:
			return (void*)xgi_move_display_area;
		case B_GET_TIMING_CONSTRAINTS:
			return (void*)xgi_get_timing_constraints;
		case B_SET_INDEXED_COLORS:
			return (void*)xgi_set_indexed_colors;
		case B_DPMS_CAPABILITIES:
			return (void*)xgi_dpms_capabilities;
		case B_DPMS_MODE:
			return (void*)xgi_dpms_mode;
		case B_SET_DPMS_MODE:
			return (void*)xgi_set_dpms_mode;
		case B_GET_PREFERRED_DISPLAY_MODE:
			return (void*)xgi_get_preferred_mode;
		case B_GET_EDID_INFO:
			return (void*)xgi_get_edid_info;

		/* Hardware Cursor */
		case B_MOVE_CURSOR:
			return (void*)xgi_move_cursor;
		case B_SET_CURSOR_SHAPE:
			return (void*)xgi_set_cursor_shape;
		case B_SHOW_CURSOR:
			return (void*)xgi_show_cursor;
#ifdef IS_PIRATI_BUILD
		case B_SET_CURSOR_BITMAP:
			return (void*)xgi_set_cursor_bitmap;
		case B_GET_CURSOR_BITS:
			return (void*)xgi_get_cursor_bits;
#endif

		/* 2D Acceleration Engine */
		case B_ACCELERANT_ENGINE_COUNT:
			return (void*)xgi_accelerant_engine_count;
		case B_ACQUIRE_ENGINE:
			return (void*)xgi_acquire_engine;
		case B_RELEASE_ENGINE:
			return (void*)xgi_release_engine;
		case B_WAIT_ENGINE_IDLE:
			return (void*)xgi_wait_engine_idle;
		case B_GET_SYNC_TOKEN:
			return (void*)xgi_get_sync_token;
		case B_SYNC_TO_TOKEN:
			return (void*)xgi_sync_to_token;
		case B_SCREEN_TO_SCREEN_BLIT:
			return (void*)xgi_screen_to_screen_blit;
		case B_FILL_RECTANGLE:
			return (void*)xgi_fill_rectangle;
		case B_INVERT_RECTANGLE:
			return (void*)xgi_invert_rectangle;
		case B_FILL_SPAN:
			return (void*)xgi_fill_span;

		default:
			return NULL;
	}
}
