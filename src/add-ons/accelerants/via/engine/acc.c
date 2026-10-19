/* VIA 2D Acceleration functions (H2 and H6 Engines)
 *
 * Authors:
 * Rudolf Cornelissen 8/2003-11/2004.
 *
 * Portions based on the OpenChrome Project (https://www.freedesktop.org/wiki/Openchrome):
 * Copyright 1998-2008 VIA Technologies, Inc. All Rights Reserved.
 * Copyright 2001-2003 S3 Graphics, Inc. All Rights Reserved.
 * Copyright 2006 Thomas Hellström. All Rights Reserved.
 * Authors: Kevin Brace, Thomas Hellström, James Simmons, Xavier Bachelot
 */

#define MODULE_BIT 0x00080000

#include "std.h"

static uint32 s_gemode = VIA_GEM_32bpp;
static uint32 s_fill_color = 0;

static inline bool is_h6(void)
{
	return (si->ps.card_arch >= VX800);
}

status_t eng_acc_wait_idle(void)
{
	/* Wait until 2D engine is completely idle */
	int timeout = 100000;
	while ((BLTR(VIA_REG_GECMD) & VIA_GEC_BLT) && --timeout)
	{
		snooze(10);
	}
	return B_OK;
}

status_t eng_acc_init(void)
{
	switch (si->dm.space)
	{
	case B_CMAP8:
		s_gemode = VIA_GEM_8bpp;
		break;
	case B_RGB15_LITTLE:
	case B_RGB16_LITTLE:
		s_gemode = VIA_GEM_16bpp;
		break;
	case B_RGB32_LITTLE:
	case B_RGBA32_LITTLE:
		s_gemode = VIA_GEM_32bpp;
		break;
	default:
		LOG(8, ("ACC: init, invalid bit depth\n"));
		return B_ERROR;
	}

	eng_acc_wait_idle();

	if (is_h6())
		BLTW(VIA_REG_GEMODE_M1, s_gemode);
	else
		BLTW(VIA_REG_GEMODE, s_gemode);

	return B_OK;
}

/* Screen to screen blit */
status_t eng_acc_setup_blit(void)
{
	return B_OK;
}

status_t eng_acc_blit(uint16 xs, uint16 ys, uint16 xd, uint16 yd, uint16 w, uint16 h)
{
	uint32 cmd = VIA_GEC_BLT | (VIA_ROP_S << 24);
	uint32 base_offset = (uint32)((uint8 *)si->fbc.frame_buffer - (uint8 *)si->framebuffer);
	uint32 pitch = si->fbc.bytes_per_row;

	eng_acc_wait_idle();

	/* Handle overlap by setting decrement direction if copying forward */
	if (yd > ys)
	{
		cmd |= VIA_GEC_DECY;
		ys += h;
		yd += h;
	}
	if (xd > xs)
	{
		cmd |= VIA_GEC_DECX;
		xs += w;
		xd += w;
	}

	if (is_h6())
	{
		BLTW(VIA_REG_GEMODE_M1, s_gemode);
		BLTW(VIA_REG_PITCH_M1, ((pitch >> 3) << 16) | (pitch >> 3));
		BLTW(VIA_REG_DIMENSION_M1, ((uint32)h << 16) | ((uint32)w & 0xFFFF));
		BLTW(VIA_REG_DSTPOS_M1, ((uint32)yd << 16) | ((uint32)xd & 0xFFFF));
		BLTW(VIA_REG_DSTBASE_M1, base_offset >> 3);
		BLTW(VIA_REG_SRCPOS_M1, ((uint32)ys << 16) | ((uint32)xs & 0xFFFF));
		BLTW(VIA_REG_SRCBASE_M1, base_offset >> 3);
		BLTW(VIA_REG_GECMD_M1, cmd);
	}
	else
	{
		BLTW(VIA_REG_GEMODE, s_gemode);
		BLTW(VIA_REG_SRCBASE, base_offset >> 3);
		BLTW(VIA_REG_DSTBASE, base_offset >> 3);
		BLTW(VIA_REG_PITCH, VIA_PITCH_ENABLE | ((pitch >> 3) << 16) | (pitch >> 3));
		BLTW(VIA_REG_SRCPOS, ((uint32)ys << 16) | ((uint32)xs & 0xFFFF));
		BLTW(VIA_REG_DSTPOS, ((uint32)yd << 16) | ((uint32)xd & 0xFFFF));
		BLTW(VIA_REG_DIMENSION, ((uint32)h << 16) | ((uint32)w & 0xFFFF));
		BLTW(VIA_REG_GECMD, cmd);
	}

	return B_OK;
}

/* Rectangle fill */
status_t eng_acc_setup_rectangle(uint32 color)
{
	s_fill_color = color;
	return B_OK;
}

status_t eng_acc_rectangle(uint32 xs, uint32 xe, uint32 ys, uint32 yl)
{
	uint32 w = (xe > xs) ? (xe - xs - 1) : 0;
	uint32 h = (yl > 0) ? (yl - 1) : 0;
	uint32 cmd = VIA_GEC_BLT | VIA_GEC_FIXCOLOR_PAT | (VIA_ROP_P << 24);
	uint32 base_offset = (uint32)((uint8 *)si->fbc.frame_buffer - (uint8 *)si->framebuffer);
	uint32 pitch = si->fbc.bytes_per_row;

	eng_acc_wait_idle();

	if (is_h6())
	{
		BLTW(VIA_REG_GEMODE_M1, s_gemode);
		BLTW(VIA_REG_PITCH_M1, ((pitch >> 3) << 16));
		BLTW(VIA_REG_DIMENSION_M1, (h << 16) | (w & 0xFFFF));
		BLTW(VIA_REG_DSTPOS_M1, (ys << 16) | (xs & 0xFFFF));
		BLTW(VIA_REG_DSTBASE_M1, base_offset >> 3);
		BLTW(VIA_REG_MONOPATFGC_M1, s_fill_color);
		BLTW(VIA_REG_GECMD_M1, cmd);
	}
	else
	{
		BLTW(VIA_REG_GEMODE, s_gemode);
		BLTW(VIA_REG_DSTBASE, base_offset >> 3);
		BLTW(VIA_REG_PITCH, VIA_PITCH_ENABLE | ((pitch >> 3) << 16));
		BLTW(VIA_REG_DSTPOS, (ys << 16) | (xs & 0xFFFF));
		BLTW(VIA_REG_DIMENSION, (h << 16) | (w & 0xFFFF));
		BLTW(VIA_REG_FGCOLOR, s_fill_color);
		BLTW(VIA_REG_GECMD, cmd);
	}

	return B_OK;
}

/* Rectangle invert */
status_t eng_acc_setup_rect_invert(void)
{
	return B_OK;
}

status_t eng_acc_rectangle_invert(uint32 xs, uint32 xe, uint32 ys, uint32 yl)
{
	uint32 w = (xe > xs) ? (xe - xs - 1) : 0;
	uint32 h = (yl > 0) ? (yl - 1) : 0;
	uint32 cmd = VIA_GEC_BLT | (0x55 << 24); /* 0x55 = ROP_Dn (Invert) */
	uint32 base_offset = (uint32)((uint8 *)si->fbc.frame_buffer - (uint8 *)si->framebuffer);
	uint32 pitch = si->fbc.bytes_per_row;

	eng_acc_wait_idle();

	if (is_h6())
	{
		BLTW(VIA_REG_GEMODE_M1, s_gemode);
		BLTW(VIA_REG_PITCH_M1, ((pitch >> 3) << 16));
		BLTW(VIA_REG_DIMENSION_M1, (h << 16) | (w & 0xFFFF));
		BLTW(VIA_REG_DSTPOS_M1, (ys << 16) | (xs & 0xFFFF));
		BLTW(VIA_REG_DSTBASE_M1, base_offset >> 3);
		BLTW(VIA_REG_GECMD_M1, cmd);
	}
	else
	{
		BLTW(VIA_REG_GEMODE, s_gemode);
		BLTW(VIA_REG_DSTBASE, base_offset >> 3);
		BLTW(VIA_REG_PITCH, VIA_PITCH_ENABLE | ((pitch >> 3) << 16));
		BLTW(VIA_REG_DSTPOS, (ys << 16) | (xs & 0xFFFF));
		BLTW(VIA_REG_DIMENSION, (h << 16) | (w & 0xFFFF));
		BLTW(VIA_REG_GECMD, cmd);
	}

	return B_OK;
}

/* Screen to screen transparent blit */
status_t eng_acc_transparent_blit(uint16 xs, uint16 ys, uint16 xd, uint16 yd, uint16 w, uint16 h, uint32 colour)
{
	return B_ERROR;
}

/* Screen to screen scaled filtered blit */
status_t eng_acc_video_blit(uint16 xs, uint16 ys, uint16 ws, uint16 hs,
	uint16 xd, uint16 yd, uint16 wd, uint16 hd)
{
	return B_ERROR;
}
