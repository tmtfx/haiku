/*
 * Copyright (C) 2001-2004 by Thomas Winischhofer, Vienna, Austria
 * Copyright (C) 1998-2000 by Alan Hourihane, Wigan, England
 * Copyright (C) 2004-2009 eXtreme Graphics Innovation Corp, Inc.
 * Copyright (C) 1999-2004 The XFree86 Project, Inc.
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

/*
 * The Volari BitBlt engine does not accept direct MMIO writes to its
 * BR(x) registers (XGI_BR_SRC_BASE etc.) - refcode/xgi_accel.h shows the
 * "direct MMIO_OUT32" version of every Volari_Setup*() macro commented
 * out, replaced by one that pushes a (header, value) pair into a ring
 * buffer in VRAM and pokes the hardware write pointer (MMIO 0x85C4).
 * The ring's VRAM location is programmed once by the kernel driver
 * (driver.cpp:init_command_queue(), mirroring Volari_InitCmdQueue) and
 * published via SharedInfo::cmdQueue.
 *
 * Writing BR(x) directly (the previous version of this file) explains
 * the "window content degrades/disappears progressively" symptom seen
 * once 2D acceleration was enabled in Screen preferences: the engine was
 * never actually told about those writes, and screen-to-screen blits
 * (used for scrolling/moving window content) silently did nothing or
 * operated on stale state.
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

/* refcode/xgi_accel.c:129 - only PCI_CHIP_XGIXG20 uses 64-bit (2-word, no
 * padding) ring entries; every other chip in the table uses 128-bit
 * (4-word, 2 GR_NIL_CMD padding words) entries. XG21/XG27 are assumed to
 * share XG20's format (same "Z" lineage, not in the refcode's table at
 * all); XG40/41/42/47 ("V" lineage) use the 4-word format. Verified on
 * real hardware for XG20 (18ca:0020) only. */
static inline bool
xgi_queue_uses_64bit_entries(void)
{
	uint32 chipType = gInfo.sharedInfo->chipType;
	return chipType == XGI_XG20 || chipType == XGI_XG21
		|| chipType == XGI_XG27;
}

/* GuardBand (refcode/xgi_accel.h:212-231): block until there is enough
 * free space in the ring ahead of our software write pointer, so we
 * never overwrite entries the GPU hasn't consumed yet (tracked via the
 * hardware read pointer at MMIO 0x85C8). */
static void
xgi_queue_wait_space(uint32 needed)
{
	uint32 wp = gInfo.sharedInfo->cmdQueue.swWP;
	uint32 size = gInfo.sharedInfo->cmdQueue.size;
	uint32 mask = gInfo.sharedInfo->cmdQueue.sizeMask;
	if (size == 0)
		return;	/* queue never initialized - nothing we can do here */

	uint32 timeout = 2000000;
	while (--timeout > 0) {
		uint32 rp = read32(XGI_REG_CMDQUEUE_RP);
		/* No real hardware read pointer ever reports "everything since
		 * byte 0 is still unconsumed" as 0xFFFFFFFF - that's the same
		 * always-reads-as-all-ones behaviour XGI_REG_CURSOR_CTRL and
		 * this same register showed during cmdQueue init (see the
		 * swWP comment in driver.cpp:init_command_queue). Spinning on
		 * it would just burn the full timeout on every single 2D op,
		 * so treat it as "unknown, proceed" rather than "never free". */
		if (rp == 0xFFFFFFFF)
			break;
		if (rp == wp)
			break;
		uint32 free = (rp - wp + size) & mask;
		if (free >= needed)
			break;
		asm volatile("pause");
	}
}

/* Push one (register offset, value) pair into the command queue and
 * advance the write pointer - the ring-buffer equivalent of the
 * Volari_Setup*()/Volari_DoCMD macros in refcode/xgi_accel.h. */
static void
xgi_queue_push(uint32 regOffset, uint32 value)
{
	if (gInfo.frameBuffer == NULL || gInfo.sharedInfo->cmdQueue.size == 0)
		return;

	/* Sanity-check the ring bounds against the real framebuffer size
	 * before trusting them for pointer arithmetic. A mismatched kernel
	 * driver / accelerant pair (different SharedInfo layout) would make
	 * cmdQueue.offset/swWP read as garbage from the wrong byte offset,
	 * turning this into an out-of-bounds write and crashing app_server -
	 * this turns that into a safe no-op instead. */
	uint64 queueEnd = (uint64)gInfo.sharedInfo->cmdQueue.offset
		+ gInfo.sharedInfo->cmdQueue.size;
	if (queueEnd > gInfo.sharedInfo->frameBufferSize
			|| gInfo.sharedInfo->cmdQueue.swWP >= gInfo.sharedInfo->cmdQueue.size) {
		XGI_ACCEL_TRACE("queue_push: bad cmdQueue bounds offset=0x%" B_PRIx32
			" size=0x%" B_PRIx32 " swWP=0x%" B_PRIx32 " fbSize=0x%" B_PRIx32
			" - kernel driver/accelerant mismatch?\n",
			gInfo.sharedInfo->cmdQueue.offset, gInfo.sharedInfo->cmdQueue.size,
			gInfo.sharedInfo->cmdQueue.swWP, gInfo.sharedInfo->frameBufferSize);
		return;
	}

	bool entry64 = xgi_queue_uses_64bit_entries();
	uint32 entrySize = entry64 ? 8 : 16;

	xgi_queue_wait_space(entrySize + XGI_CMDQUEUE_BAND_SIZE);

	uint32 wp = gInfo.sharedInfo->cmdQueue.swWP;

	/* Defense in depth: regardless of how swWP got here, never let an
	 * entry write start close enough to the end of the ring to spill
	 * past it (and past the mapped framebuffer right behind it, which
	 * is what crashed app_server here once already - see the swWP
	 * comment in driver.cpp:init_command_queue). Wrap to 0 instead. */
	if (wp + entrySize > gInfo.sharedInfo->cmdQueue.size)
		wp = 0;

	uint8* ring = gInfo.frameBuffer + gInfo.sharedInfo->cmdQueue.offset;
	uint32* entry = (uint32*)(ring + wp);

	entry[0] = GR_SKPC_HEADER + regOffset;
	entry[1] = value;
	if (!entry64) {
		entry[2] = GR_NIL_CMD;
		entry[3] = GR_NIL_CMD;
	}

	wp = (wp + entrySize) & gInfo.sharedInfo->cmdQueue.sizeMask;
	gInfo.sharedInfo->cmdQueue.swWP = wp;
	write32(XGI_REG_CMDQUEUE_WP, wp);
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

	xgi_wait_engine_idle();

	uint32 bppCode = get_bpp_code((color_space)mode->space);
	uint32 bytesPerPixel = (mode->space == B_RGB32) ? 4 : ((mode->space == B_RGB16) ? 2 : 1);
	uint32 pitch = mode->virtual_width * bytesPerPixel;
	pitch = (pitch + 63) & ~63;

	xgi_queue_push(XGI_BR_SRC_BASE, 0);
	xgi_queue_push(XGI_BR_DST_BASE, 0);
	xgi_queue_push(XGI_BR_SRC_PITCH, (pitch & 0xFFFF) | bppCode);
	xgi_queue_push(XGI_BR_DST_PITCH, (0x0FFF << 16) | (pitch & 0xFFFF));

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

		/* The refcode (and the upstream Xorg driver it comes from) always
		 * issues this command with the direction bits at their default
		 * (X_DEC/Y_DEC - scan starts top-left, same corner regardless of
		 * where src/dest actually sit). That corrupts any blit whose
		 * source and destination rectangles overlap with dest below/
		 * right of src - e.g. scrolling a window's content down or right
		 * by a few pixels - since the engine would read pixels this same
		 * operation already overwrote earlier in the scan. Picking the
		 * scan corner from the relative src/dest position instead avoids
		 * that; this is the standard convention for VGA-descended BitBlt
		 * engines of this era (see Mach64_ScreenToScreenBlit in the ati
		 * accelerant for the same pattern on different hardware) - the
		 * XGI refcode defines XGI_CMD_X_INC/Y_INC but never actually uses
		 * them, so this isn't verified against a working reference, just
		 * against the general convention and the bit definitions XGI's
		 * own headers document. */
		uint32 cmdDir = 0;
		if (dstX <= srcX) {
			cmdDir |= XGI_CMD_X_INC;
		} else {
			srcX += p.width;
			dstX += p.width;
		}
		if (dstY <= srcY) {
			cmdDir |= XGI_CMD_Y_INC;
		} else {
			srcY += p.height;
			dstY += p.height;
		}

		xgi_wait_engine_idle();

		xgi_queue_push(XGI_BR_SRC_BASE, 0);
		xgi_queue_push(XGI_BR_DST_BASE, 0);
		xgi_queue_push(XGI_BR_SRC_PITCH, (pitch & 0xFFFF) | bppCode);
		xgi_queue_push(XGI_BR_DST_PITCH, (0x0FFF << 16) | (pitch & 0xFFFF));
		xgi_queue_push(XGI_BR_SRC_XY, ((srcX & 0x1FFF) << 16) | (srcY & 0xFFFF));
		xgi_queue_push(XGI_BR_DST_XY, ((dstX & 0x1FFF) << 16) | (dstY & 0xFFFF));
		xgi_queue_push(XGI_BR_RECT_SIZE, ((height & 0xFFFF) << 16) | (width & 0xFFFF));

		/* Fire command: ROP 0xCC (Copy), BitBlt, Dest in Video RAM */
		uint32 cmd = (XGI_ROP_COPY_SRC << 8) | XGI_CMD_BITBLT | bppCode
			| XGI_CMD_DSTVIDEO | cmdDir;
		xgi_queue_push(XGI_BR_CMD, cmd);
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

		xgi_queue_push(XGI_BR_PAT_FG, color);
		xgi_queue_push(XGI_BR_DST_BASE, 0);
		xgi_queue_push(XGI_BR_DST_PITCH, (0x0FFF << 16) | (pitch & 0xFFFF));
		xgi_queue_push(XGI_BR_DST_XY, (((uint32)r.left & 0x1FFF) << 16) | ((uint32)r.top & 0xFFFF));
		xgi_queue_push(XGI_BR_RECT_SIZE, ((height & 0xFFFF) << 16) | (width & 0xFFFF));

		/* Fire command: ROP 0xF0 (Copy Pattern/Color), Solid FG, BitBlt */
		uint32 cmd = (XGI_ROP_COPY_PAT << 8) | XGI_CMD_PATFG | XGI_CMD_BITBLT | bppCode | XGI_CMD_DSTVIDEO;
		xgi_queue_push(XGI_BR_CMD, cmd);
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

		xgi_queue_push(XGI_BR_DST_BASE, 0);
		xgi_queue_push(XGI_BR_DST_PITCH, (0x0FFF << 16) | (pitch & 0xFFFF));
		xgi_queue_push(XGI_BR_DST_XY, (((uint32)r.left & 0x1FFF) << 16) | ((uint32)r.top & 0xFFFF));
		xgi_queue_push(XGI_BR_RECT_SIZE, ((height & 0xFFFF) << 16) | (width & 0xFFFF));

		/* Fire command: ROP 0x55 (Invert Dest: ~Dest) */
		uint32 cmd = (XGI_ROP_INVERT_DST << 8) | XGI_CMD_BITBLT | bppCode | XGI_CMD_DSTVIDEO;
		xgi_queue_push(XGI_BR_CMD, cmd);
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
