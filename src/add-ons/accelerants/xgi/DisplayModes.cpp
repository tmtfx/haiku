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

void
out_seq(uint8 index, uint8 val)
{
	uint16 relIO = gInfo.sharedInfo->ioBase;
	uint16 pIndex = (relIO != 0) ? (relIO + 0x44) : 0x3C4;
	out8(index, pIndex);
	out8(val, pIndex + 1);
}

uint8
in_seq(uint8 index)
{
	uint16 relIO = gInfo.sharedInfo->ioBase;
	uint16 pIndex = (relIO != 0) ? (relIO + 0x44) : 0x3C4;
	out8(index, pIndex);
	return in8(pIndex + 1);
}

void
out_crt(uint8 index, uint8 val)
{
	uint16 relIO = gInfo.sharedInfo->ioBase;
	uint16 pIndex = (relIO != 0) ? (relIO + 0x54) : 0x3D4;
	out8(index, pIndex);
	out8(val, pIndex + 1);
}

uint8
in_crt(uint8 index)
{
	uint16 relIO = gInfo.sharedInfo->ioBase;
	uint16 pIndex = (relIO != 0) ? (relIO + 0x54) : 0x3D4;
	out8(index, pIndex);
	return in8(pIndex + 1);
}

void
out_gr(uint8 index, uint8 val)
{
	uint16 relIO = gInfo.sharedInfo->ioBase;
	uint16 pIndex = (relIO != 0) ? (relIO + 0x4E) : 0x3CE;
	out8(index, pIndex);
	out8(val, pIndex + 1);
}

uint8
in_gr(uint8 index)
{
	uint16 relIO = gInfo.sharedInfo->ioBase;
	uint16 pIndex = (relIO != 0) ? (relIO + 0x4E) : 0x3CE;
	out8(index, pIndex);
	return in8(pIndex + 1);
}

/* Attribute Controller: read the input-status register first to reset its
 * address/data flip-flop (refcode/init.c, e.g. XGI_New_SetATTRegs), then
 * write index and data in sequence. */
void
out_att(uint8 index, uint8 val)
{
	uint16 relIO = gInfo.sharedInfo->ioBase;
	uint16 pStat = (relIO != 0) ? (relIO + 0x5A) : 0x3DA;
	uint16 pAttr = (relIO != 0) ? (relIO + 0x40) : 0x3C0;
	in8(pStat);
	out8(index, pAttr);
	out8(val, pAttr);
}

void
out_misc(uint8 val)
{
	uint16 relIO = gInfo.sharedInfo->ioBase;
	uint16 p = (relIO != 0) ? (relIO + 0x42) : 0x3C2;
	out8(val, p);
}

/*
 * CRT1 static baseline registers ("ExtVGATable" / StandTable[0x0F],
 * refcode/vb_table.h:491-505). Used for any extended (non-text) mode on
 * the Volari family, independent of resolution/depth - XGI_New_GetModePtr()
 * (refcode/init.c:595-607) always resolves to this table entry for
 * ModeNo > 0x13. The MISC byte from that table entry (0x23) is omitted:
 * XGI_New_SetMiscRegs() writes it first, but XGI_SetCRT1Sync() always
 * overwrites P3c2 afterwards with the real sync-polarity value before the
 * screen is re-enabled, so the baseline value is never actually visible.
 */
static const uint8 kXgiExtSeq[4]   = { 0x01, 0x0f, 0x00, 0x0e };
static const uint8 kXgiExtCrtc[25] = {
	0x5f, 0x4f, 0x50, 0x82, 0x54, 0x80, 0x0b, 0x3e,
	0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0xea, 0x8c, 0xdf, 0x28, 0x40, 0xe7, 0x04, 0xa3,
	0xff
};
static const uint8 kXgiExtAttr[20] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
	0x01, 0x00, 0x00, 0x00
};
static const uint8 kXgiExtGrc[9]   = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0f, 0xff };

struct XgiCrtcTiming {
	uint16	hTotal, hDisplay, hBlankStart, hBlankEnd, hSyncStart, hSyncEnd;
	uint16	vTotal, vDisplay, vBlankStart, vBlankEnd, vSyncStart, vSyncEnd;
};

static inline uint8
bit_at(uint32 value, int from, int to)
{
	return (uint8)(((value >> from) & 1) << to);
}

/*
 * Porting of XGI_CalcCRRegisters() (refcode/init.c:1843-1894). Pure
 * arithmetic from CRTC timing to the 17 bytes CR0-CR7/CR10-12/CR15-16
 * (indices 0-12) and SRA-SRC (indices 13-15), plus the extra CR9/SR0E bit
 * (index 16). No BIOS or table dependency - this is what lets us drive
 * arbitrary (non-VESA-standard) modes, matching what
 * XGI_CheckBuildCustomMode() does for "custom mode" in the refcode.
 */
static void
xgi_calc_cr_registers(const XgiCrtcTiming& t, int depth, uint8 cr[17])
{
	cr[0] = (uint8)(((t.hTotal >> 3) - 5) & 0xFF);
	cr[1] = (uint8)((t.hDisplay >> 3) - 1);
	cr[2] = (uint8)((t.hBlankStart >> 3) - 1);
	cr[3] = (uint8)((((t.hBlankEnd >> 3) - 1) & 0x1F) | 0x80);
	cr[4] = (uint8)((t.hSyncStart >> 3) + 3);
	cr[5] = (uint8)((((t.hBlankEnd >> 3) - 1) & 0x20) << 2)
		| (uint8)(((t.hSyncEnd >> 3) + 3) & 0x1F);

	cr[6] = (uint8)((t.vTotal - 2) & 0xFF);
	cr[7] = bit_at(t.vTotal - 2, 8, 0) | bit_at(t.vDisplay - 1, 8, 1)
		| bit_at(t.vSyncStart, 8, 2) | bit_at(t.vBlankStart - 1, 8, 3)
		| 0x10
		| bit_at(t.vTotal - 2, 9, 5) | bit_at(t.vDisplay - 1, 9, 6)
		| bit_at(t.vSyncStart, 9, 7);

	cr[16] = bit_at(t.vBlankStart - 1, 9, 0);
	if (depth != 8) {
		if (t.hDisplay >= 1600)
			cr[16] |= 0x60;
		else if (t.hDisplay >= 640)
			cr[16] |= 0x40;
	}

	cr[8] = (uint8)(t.vSyncStart & 0xFF);
	cr[9] = (uint8)((t.vSyncEnd & 0x0F) | 0x80);
	cr[10] = (uint8)((t.vDisplay - 1) & 0xFF);
	cr[11] = (uint8)((t.vBlankStart - 1) & 0xFF);
	cr[12] = (uint8)((t.vBlankEnd - 1) & 0xFF);

	cr[13] = bit_at(t.vTotal - 2, 10, 0) | bit_at(t.vDisplay - 1, 10, 1)
		| bit_at(t.vBlankStart - 1, 10, 2) | bit_at(t.vSyncStart, 10, 3)
		| bit_at(t.vBlankEnd - 1, 8, 4) | bit_at(t.vSyncEnd, 4, 5);

	cr[14] = bit_at((t.hTotal >> 3) - 5, 8, 0)
		| bit_at((t.hTotal >> 3) - 5, 9, 1)
		| bit_at((t.hDisplay >> 3) - 1, 8, 2)
		| bit_at((t.hDisplay >> 3) - 1, 9, 3)
		| bit_at((t.hBlankStart >> 3) - 1, 8, 4)
		| bit_at((t.hBlankStart >> 3) - 1, 9, 5)
		| bit_at((t.hSyncStart >> 3) + 3, 8, 6)
		| bit_at((t.hSyncStart >> 3) + 3, 9, 7);

	cr[15] = bit_at((t.hBlankEnd >> 3) - 1, 6, 0)
		| bit_at((t.hBlankEnd >> 3) - 1, 7, 1)
		| bit_at((t.hSyncEnd >> 3) + 3, 5, 2);
}

/*
 * Full CRT1 programming sequence for Volari, porting
 * XGI_New_SetCRT1Group() (refcode/init.c:1373-1439) and the functions it
 * calls. Simplified for Haiku's use case: single head (CRT1 only, no
 * video-bridge/CRT2/LCD/TV handling), always a "custom" (non-VESA-table)
 * mode, never text/EGA/interlace/doublescan.
 *
 * XGI_ResetCRT1VCLK() (refcode/init.c) is intentionally not ported: it
 * only pre-loads a known-safe low clock before the real one as a glitch
 * precaution, requires VCLK table data we don't carry, and is unnecessary
 * here since the screen stays blanked (SR01 bit5) for the whole sequence
 * below, until the final DisplayOn.
 */
void
xgi_program_crt1(const display_mode& mode, uint32 pitchBytes)
{
	const display_timing& tm = mode.timing;
	int depth = (mode.space == B_CMAP8) ? 8 : (mode.space == B_RGB16 ? 16 : 32);

	XGI_ACCEL_TRACE("program_crt1: %" B_PRIu16 "x%" B_PRIu16
		" clock=%" B_PRIu32 " depth=%d pitch=%" B_PRIu32 "\n",
		tm.h_display, tm.v_display, tm.pixel_clock, depth, pitchBytes);

	/* 1. Blank screen + baseline SEQ (XGI_New_SetSeqRegs, init.c) */
	out_seq(0x00, 0x03);
	out_seq(0x01, kXgiExtSeq[0] | 0x20);
	out_seq(0x02, kXgiExtSeq[1]);
	out_seq(0x03, kXgiExtSeq[2]);
	out_seq(0x04, kXgiExtSeq[3]);

	/* 2. MISC - result of XGI_SetCRT1Sync (init.c:938-951), applied here
	 * directly since the baseline table value is always superseded by it
	 * anyway. */
	uint8 misc = 0x2F;
	if (!(tm.flags & B_POSITIVE_HSYNC))
		misc |= 0x40;
	if (!(tm.flags & B_POSITIVE_VSYNC))
		misc |= 0x80;
	out_misc(misc);

	/* 3. CRTC baseline (XGI_New_SetCRTCRegs, init.c) */
	out_crt(0x11, in_crt(0x11) & 0x7F);
	for (int i = 0; i <= 0x18; i++)
		out_crt((uint8)i, kXgiExtCrtc[i]);

	/* 4. ATTR baseline (XGI_New_SetATTRegs, init.c) */
	for (int i = 0; i <= 0x13; i++)
		out_att((uint8)i, kXgiExtAttr[i]);
	out_att(0x14, 0x00);
	/* "Enable Attribute": index-only write with bit5 set, no data byte
	 * follows (init.c comment "Enable Attribute Controller"). */
	{
		uint16 relIO = gInfo.sharedInfo->ioBase;
		uint16 pStat = (relIO != 0) ? (relIO + 0x5A) : 0x3DA;
		uint16 pAttr = (relIO != 0) ? (relIO + 0x40) : 0x3C0;
		in8(pStat);
		out8(0x20, pAttr);
	}

	/* 5. GRC baseline (XGI_New_SetGRCRegs, init.c) */
	for (int i = 0; i <= 0x08; i++)
		out_gr((uint8)i, kXgiExtGrc[i]);
	if (depth != 8)
		out_gr(0x05, in_gr(0x05) & 0xBF);	/* 256-color mode disable */

	/* 6. Clear extended regs (XGI_New_ClearExt1Regs, init.c) */
	for (int i = 0x0A; i <= 0x0E; i++)
		out_seq((uint8)i, 0x00);
	out_seq(0x37, in_seq(0x37) & 0xFE);

	/* 7. Real CRTC timing for this mode (XGI_New_SetCRT1CRTC, init.c:958) */
	XgiCrtcTiming t;
	t.hTotal = tm.h_total;
	t.hDisplay = tm.h_display;
	t.hBlankStart = tm.h_display;
	t.hBlankEnd = tm.h_total;
	t.hSyncStart = tm.h_sync_start;
	t.hSyncEnd = tm.h_sync_end;
	t.vTotal = tm.v_total;
	t.vDisplay = tm.v_display;
	t.vBlankStart = tm.v_sync_start - 1;
	t.vBlankEnd = tm.v_total;
	t.vSyncStart = tm.v_sync_start;
	t.vSyncEnd = tm.v_sync_end;

	uint8 cr[17];
	xgi_calc_cr_registers(t, depth, cr);

	out_crt(0x11, in_crt(0x11) & 0x7F);
	for (int i = 0; i <= 7; i++)
		out_crt((uint8)i, cr[i]);
	out_crt(0x10, cr[8]);
	out_crt(0x11, cr[9]);
	out_crt(0x12, cr[10]);
	out_crt(0x15, cr[11]);
	out_crt(0x16, cr[12]);
	out_seq(0x0A, cr[13]);
	out_seq(0x0B, cr[14]);
	out_seq(0x0C, cr[15]);
	out_seq(0x0E, cr[16] & 0xE0);
	out_crt(0x09, (in_crt(0x09) & 0x5F) | ((cr[16] & 0x01) << 5));
	if (depth != 8)
		out_crt(0x14, 0x4F);

	/* 8. Offset/pitch (XGI_New_SetCRT1Offset + XGI_SetPitchCRT1,
	 * init.c:1033-1061,1474-1481). The base "Display Unit" (feeds SR10)
	 * is computed VESA-style from h_display; CR13/SR0E are then
	 * re-written from the real byte pitch so virtual_width != h_display
	 * (panning) still addresses memory correctly. */
	uint32 colorDepthUnit = (depth == 8) ? 2 : (depth == 16 ? 4 : 8);
	uint32 displayUnit = (tm.h_display / 16) * colorDepthUnit;
	if (tm.h_display % 16)
		displayUnit += colorDepthUnit / 2;

	out_seq(0x0E, (in_seq(0x0E) & 0xF0) | (uint8)((displayUnit >> 8) & 0x0F));
	out_crt(0x13, (uint8)(displayUnit & 0xFF));

	uint32 du = displayUnit << 5;
	uint8 sr10 = (uint8)((du & 0xFF00) >> 8);
	if (du & 0xFF)
		sr10++;
	sr10++;
	out_seq(0x10, sr10);

	uint32 pitchUnit = pitchBytes >> 3;
	out_crt(0x13, (uint8)(pitchUnit & 0xFF));
	out_seq(0x0E, (in_seq(0x0E) & 0xF0) | (uint8)((pitchUnit >> 8) & 0x0F));

	/* 9. PLL (XGI_New_SetCRT1VCLK, init.c:1068 - via Pll.cpp) */
	XgiPllParams pll;
	xgi_calc_pll(tm.pixel_clock, &pll);
	xgi_program_vclk(pll);

	/* 10. Mode regs (XGI_New_SetCRT1ModeRegs + XGI_New_SetVCLKState,
	 * init.c:1106-1235). ModeType deltas (ModeVGA=3, Mode16Bpp=5,
	 * Mode32Bpp=7, initdef.h) collapse to a simple shift by depth here. */
	out_seq(0x1F, in_seq(0x1F) & 0x3F);		/* disable DPMS */

	uint8 modeShift = (depth == 8) ? 0 : (depth == 16 ? 2 : 4);
	out_seq(0x06, (in_seq(0x06) & 0xC0) | 0x02 | (modeShift << 2));

	out_crt(0x19, 0x00);
	out_crt(0x1A, in_crt(0x1A) & 0xFC);

	bool lineCompareOff = (tm.h_display >= 1024 || tm.v_display >= 1024
		|| tm.v_total >= 1024);
	out_seq(0x0F, (in_seq(0x0F) & 0xB7) | (lineCompareOff ? 0x08 : 0x00));

	out_seq(0x31, in_seq(0x31) & 0xFB);		/* chip >= XGI_661 */
	out_seq(0x21, (in_seq(0x21) & 0x1F) | 0xA0);	/* always graphics, never text/EGA */

	uint32 vclkMHz = tm.pixel_clock / 1000 + 1;	/* matches refcode's CSRClock rounding */
	out_seq(0x32, (in_seq(0x32) & 0xF3) | (vclkMHz >= 166 ? 0x0C : 0x00));
	if (vclkMHz >= 166)
		out_seq(0x1F, in_seq(0x1F) & 0xE7);
	out_seq(0x07, (in_seq(0x07) & 0xE8) | 0x10);	/* DAC speed, chip >= XGI_661 */

	/* 11. Display on (XGI_New_DisplayOn, init.c:272) */
	out_seq(0x01, in_seq(0x01) & 0xDF);
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

	/* Program CRTC/PLL/DAC so the hardware actually switches mode,
	 * instead of staying on the bootloader's VESA mode. */
	xgi_program_crt1(proposed, pitch);

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
		*high = gInfo.sharedInfo->maxPixelClock;	/* 250 MHz */

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
