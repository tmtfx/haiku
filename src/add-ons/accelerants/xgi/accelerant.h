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
#ifndef XGI_ACCELERANT_H
#define XGI_ACCELERANT_H

#include <Accelerant.h>
#include <Debug.h>
#include <SupportDefs.h>
#include <GraphicsDefs.h>
#include <PCI.h>
#include <OS.h>
#include <edid.h>

#include "DriverInterface.h"

#define XGI_ACCEL_TRACE(x...) debug_printf("xgi.accelerant: " x)

/* 2D Engine MMIO Registers (BAR 1) */
#define XGI_BR_SRC_BASE			0x8200
#define XGI_BR_SRC_PITCH		0x8204
#define XGI_BR_SRC_XY			0x8208
#define XGI_BR_DST_XY			0x820C
#define XGI_BR_DST_BASE			0x8210
#define XGI_BR_DST_PITCH		0x8214
#define XGI_BR_RECT_SIZE		0x8218
#define XGI_BR_PAT_FG			0x821C
#define XGI_BR_PAT_BG			0x8220
#define XGI_BR_MONO_PAT0		0x8224
#define XGI_BR_MONO_PAT1		0x8228
#define XGI_BR_CMD				0x823C

/* 2D Command Register bit definitions */
#define XGI_CMD_BITBLT			0x00000000
#define XGI_CMD_COLOREXP		0x00000001
#define XGI_CMD_ENCOLOREXP		0x00000002
#define XGI_CMD_MULTIPLE_SCANLN	0x00000003
#define XGI_CMD_LINE			0x00000004

#define XGI_CMD_SRCVIDEO		0x00000000
#define XGI_CMD_SRCSYSTEM		0x00000010
#define XGI_CMD_SRCAGP			0x00000020

#define XGI_CMD_PATFG			0x00000000
#define XGI_CMD_PATPATREG		0x00000040
#define XGI_CMD_PATMONO			0x00000080

#define XGI_CMD_X_INC			0x00010000
#define XGI_CMD_X_DEC			0x00000000
#define XGI_CMD_Y_INC			0x00020000
#define XGI_CMD_Y_DEC			0x00000000

#define XGI_CMD_NOCLIP			0x00000000
#define XGI_CMD_CLIPENABLE		0x00040000

#define XGI_CMD_OPAQUE			0x00000000
#define XGI_CMD_TRANSPARENT		0x00100000

#define XGI_CMD_DSTVIDEO		0x02000000

/* Raster Operations (ROP) */
#define XGI_ROP_COPY_SRC		0xCC	/* Dest = Src */
#define XGI_ROP_COPY_PAT		0xF0	/* Dest = Pat/Color */
#define XGI_ROP_INVERT_DST		0x55	/* Dest = ~Dest */
#define XGI_ROP_CLEAR			0x00	/* Dest = 0 */
#define XGI_ROP_SET				0xFF	/* Dest = 1 */
#define XGI_ROP_XOR				0x66	/* Dest = Src ^ Dest */

/* Engine status register */
#define XGI_REG_STATUS			0x85CC
#define XGI_STATUS_2D_IDLE		0x80000000

/* Hardware Cursor registers (CRT1) */
#define XGI_REG_CURSOR_CTRL		0x8500
#define XGI_REG_CURSOR_BG		0x8504
#define XGI_REG_CURSOR_FG		0x8508
#define XGI_REG_CURSOR_X		0x850C
#define XGI_REG_CURSOR_Y		0x8510
#define XGI_CURSOR_ENABLE		(1U << 30)

/* Internal Accelerant state */
struct AccelerantInfo {
	int				deviceFD;
	bool			isClone;

	area_id			sharedInfoArea;
	SharedInfo*		sharedInfo;

	area_id			regsArea;
	vuint8*			regs;

	area_id			frameBufferArea;
	uint8*			frameBuffer;

	area_id			modeListArea;
	display_mode*	modeList;
	uint32			modeCount;

	engine_token	engineToken;
};

extern AccelerantInfo gInfo;

/* Register access helpers */
static inline uint32
read32(uint32 offset)
{
	return *(volatile uint32*)(gInfo.regs + offset);
}

static inline void
write32(uint32 offset, uint32 value)
{
	*(volatile uint32*)(gInfo.regs + offset) = value;
}

static inline uint8
read8(uint32 offset)
{
	return *(volatile uint8*)(gInfo.regs + offset);
}

static inline void
write8(uint32 offset, uint8 value)
{
	*(volatile uint8*)(gInfo.regs + offset) = value;
}

#if defined(__x86_64__) || defined(__i386__)
static inline void
out8(uint8 value, uint16 port)
{
	__asm__ volatile ("outb %0, %1" : : "a"(value), "d"(port));
}

static inline uint8
in8(uint16 port)
{
	uint8 value;
	__asm__ volatile ("inb %1, %0" : "=a"(value) : "d"(port));
	return value;
}
#else
static inline void
out8(uint8 value, uint16 port)
{
	(void)value;
	(void)port;
}

static inline uint8
in8(uint16 port)
{
	(void)port;
	return 0;
}
#endif

/* Prototypes - InitAccelerant.cpp */
status_t	xgi_init_accelerant(int fd);
void		xgi_uninit_accelerant(void);
status_t	xgi_clone_accelerant(void* data);
ssize_t		xgi_accelerant_clone_info_size(void);
void		xgi_get_accelerant_clone_info(void* data);
status_t	xgi_get_accelerant_device_info(accelerant_device_info* adi);
sem_id		xgi_accelerant_retrace_semaphore(void);

/* Prototypes - DisplayModes.cpp */
uint32		xgi_accelerant_mode_count(void);
status_t	xgi_get_mode_list(display_mode* dmList);
status_t	xgi_propose_display_mode(display_mode* target, const display_mode* low,
				const display_mode* high);
status_t	xgi_set_display_mode(display_mode* modeToSet);
status_t	xgi_get_display_mode(display_mode* currentMode);
status_t	xgi_get_frame_buffer_config(frame_buffer_config* config);
status_t	xgi_get_pixel_clock_limits(display_mode* dm, uint32* low, uint32* high);
status_t	xgi_move_display_area(uint16 hDisplayStart, uint16 vDisplayStart);
status_t	xgi_get_timing_constraints(display_timing_constraints* dtc);
void		xgi_set_indexed_colors(uint count, uint8 first, uint8* colorData,
				uint32 flags);
uint32		xgi_dpms_capabilities(void);
uint32		xgi_dpms_mode(void);
status_t	xgi_set_dpms_mode(uint32 flags);
status_t	xgi_get_preferred_mode(display_mode* preferredMode);
status_t	xgi_get_edid_info(void* info, size_t size, uint32* version);
status_t	xgi_create_mode_list(void);

/* Prototypes - Cursor.cpp */
status_t	xgi_set_cursor_shape(uint16 width, uint16 height, uint16 hotX,
				uint16 hotY, uint8* andMask, uint8* xorMask);
void		xgi_move_cursor(uint16 x, uint16 y);
void		xgi_show_cursor(bool isVisible);
#ifdef IS_PIRATI_BUILD
status_t	xgi_set_cursor_bitmap(uint16 width, uint16 height, uint16 hotX,
				uint16 hotY, color_space space, uint16 bytesPerRow,
				const uint8* bitmapData);
uint32		xgi_get_cursor_bits(void);
#endif

/* Prototypes - Engine.cpp */
uint32		xgi_accelerant_engine_count(void);
status_t	xgi_acquire_engine(uint32 capabilities, uint32 maxPriority,
				sync_token* st, engine_token** et);
void		xgi_release_engine(engine_token* et, sync_token* st);
void		xgi_wait_engine_idle(void);
status_t	xgi_get_sync_token(engine_token* et, sync_token* st);
status_t	xgi_sync_to_token(sync_token* st);
void		xgi_screen_to_screen_blit(engine_token* et, blit_params* list,
				uint32 count);
void		xgi_fill_rectangle(engine_token* et, uint32 color,
				fill_rect_params* list, uint32 count);
void		xgi_invert_rectangle(engine_token* et, fill_rect_params* list,
				uint32 count);
void		xgi_fill_span(engine_token* et, uint32 color, uint16* list,
				uint32 count);
void		xgi_init_2d_engine(display_mode* mode);

#endif /* XGI_ACCELERANT_H */
