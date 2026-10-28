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
#include <unistd.h>
#include <edid.h>
#include <video_overlay.h>

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

/* XGI's hardware cursor wants a (mask, source) pair, not BeOS' native
 * (AND, XOR) planes (refcode/xgi_cursor.c:198-204:
 * HARDWARE_CURSOR_INVERT_MASK | HARDWARE_CURSOR_AND_SOURCE_WITH_MASK |
 * HARDWARE_CURSOR_SWAP_SOURCE_AND_MASK). The exact plane order isn't
 * derivable from the refcode alone (depends on an Xorg core convention
 * not present in this repository) - flip this if the first hardware test
 * shows an empty/inverted cursor box instead of the arrow. */
#define XGI_CURSOR_SWAP_PLANES	1

/* Video Overlay Register Indices (accessed via RelIO + 0x02 index, RelIO + 0x03 data) */
#define XGI_VI_INDEX_WIN_HOR_START_LOW		0x01
#define XGI_VI_INDEX_WIN_HOR_END_LOW		0x02
#define XGI_VI_INDEX_WIN_HOR_OVER			0x03
#define XGI_VI_INDEX_WIN_VER_START_LOW		0x04
#define XGI_VI_INDEX_WIN_VER_END_LOW		0x05
#define XGI_VI_INDEX_WIN_VER_OVER			0x06
#define XGI_VI_INDEX_DISP_Y_BUF_START_LOW	0x07
#define XGI_VI_INDEX_DISP_Y_BUF_START_MID	0x08
#define XGI_VI_INDEX_DISP_Y_BUF_START_HIGH	0x09
#define XGI_VI_INDEX_DISP_Y_BUF_PITCH_LOW	0x10
#define XGI_VI_INDEX_DISP_Y_UV_PITCH_HIGH	0x12
#define XGI_VI_INDEX_HOR_UP_SCALE_LOW		0x18
#define XGI_VI_INDEX_HOR_UP_SCALE_HIGH		0x19
#define XGI_VI_INDEX_VER_UP_SCALE_LOW		0x1A
#define XGI_VI_INDEX_VER_UP_SCALE_HIGH		0x1B
#define XGI_VI_INDEX_SCALE_CONTROL			0x1C
#define XGI_VI_INDEX_LINE_BUF_SIZE			0x1F
#define XGI_VI_INDEX_COLORKEY_RED_MIN		0x20
#define XGI_VI_INDEX_COLORKEY_GREEN_MIN		0x21
#define XGI_VI_INDEX_COLORKEY_BLUE_MIN		0x22
#define XGI_VI_INDEX_COLORKEY_RED_MAX		0x23
#define XGI_VI_INDEX_COLORKEY_GREEN_MAX		0x24
#define XGI_VI_INDEX_COLORKEY_BLUE_MAX		0x25
#define XGI_VI_INDEX_CONTRAST_FACTOR		0x2C
#define XGI_VI_INDEX_BRIGHTNESS				0x2D
#define XGI_VI_INDEX_KEY_OVERLAY_OP			0x2F
#define XGI_VI_INDEX_CONTROL_MISC0			0x30
#define XGI_VI_INDEX_CONTROL_MISC1			0x31
#define XGI_VI_INDEX_CONTROL_MISC2			0x32
#define XGI_VI_INDEX_DISP_Y_BUF_EXT_HIGH	0x6B

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

static inline void
xgi_out8(uint8 value, uint16 port)
{
	XgiGetSetPIO pio;
	pio.magic = XGI_PRIVATE_DATA_MAGIC;
	pio.port = port;
	pio.value = value;
	ioctl(gInfo.deviceFD, XGI_SET_PIO, &pio, sizeof(pio));
}

static inline uint8
xgi_in8(uint16 port)
{
	XgiGetSetPIO pio;
	pio.magic = XGI_PRIVATE_DATA_MAGIC;
	pio.port = port;
	pio.value = 0;
	ioctl(gInfo.deviceFD, XGI_GET_PIO, &pio, sizeof(pio));
	return pio.value;
}

#define out8(value, port) xgi_out8((value), (port))
#define in8(port)         xgi_in8((port))

/* VGA-style indexed register accessors (SEQ/CRTC/GRC/ATTR/MISC), shared
 * between DisplayModes.cpp (CRTC/timing programming) and Pll.cpp (VCLK).
 * Implemented in DisplayModes.cpp. */
void		out_seq(uint8 index, uint8 val);
uint8		in_seq(uint8 index);
void		out_crt(uint8 index, uint8 val);
uint8		in_crt(uint8 index);
void		out_gr(uint8 index, uint8 val);
uint8		in_gr(uint8 index);
void		out_att(uint8 index, uint8 val);
void		out_misc(uint8 val);

/* PLL (VCLK) divider calculation - Pll.cpp. Porting of compute_vclk()/
 * XGICalcClock() from refcode/xgi_dac.c. */
struct XgiPllParams {
	uint8	sr2b;
	uint8	sr2c;
};

bool		xgi_calc_pll(uint32 pixelClockKHz, XgiPllParams* out);
void		xgi_program_vclk(const XgiPllParams& params);

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
void		xgi_program_crt1(const display_mode& mode, uint32 pitchBytes);

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

/* Prototypes - Overlay.cpp */
uint32					xgi_overlay_count(const display_mode* mode);
const uint32*			xgi_overlay_supported_spaces(const display_mode* mode);
uint32					xgi_overlay_supported_features(uint32 colorSpace);
const overlay_buffer*	xgi_allocate_overlay_buffer(color_space space,
							uint16 width, uint16 height);
status_t				xgi_release_overlay_buffer(const overlay_buffer* buffer);
status_t				xgi_get_overlay_constraints(const display_mode* mode,
							const overlay_buffer* buffer,
							overlay_constraints* constraints);
overlay_token			xgi_allocate_overlay(void);
status_t				xgi_release_overlay(overlay_token token);
status_t				xgi_configure_overlay(overlay_token token,
							const overlay_buffer* buffer,
							const overlay_window* window,
							const overlay_view* view);

#endif /* XGI_ACCELERANT_H */
