/*
 * Copyright 2026, GitHub Copilot. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "accelerant.h"

#include <string.h>

#define KYRO_OVL_TRACE(x...) debug_printf("kyro.overlay: " x)


namespace {

static const uint32 kNoScaling = 0x800;
static const uint32 kNoDecimation = 0xffffffff;

static const uint32 kDACOverlayAddr = 0x1410;
static const uint32 kDACOverlayUAddr = 0x1414;
static const uint32 kDACOverlayVAddr = 0x1418;
static const uint32 kDACOverlaySize = 0x141c;
static const uint32 kDACOverlayVtDec = 0x1420;
static const uint32 kDACVerticalScal = 0x1448;
static const uint32 kDACPixelFormat = 0x144c;
static const uint32 kDACHorizontalScal = 0x1450;
static const uint32 kDACVidWinStart = 0x1454;
static const uint32 kDACVidWinEnd = 0x1458;
static const uint32 kDACBlendCtrl = 0x145c;
static const uint32 kDACStreamCtrl = 0x1480;

static const uint32 kOverlayMaxWidth = 720;
static const uint32 kOverlayMaxHeight = 576;
static const uint32 kOverlaySlots = 4;

static const uint32 kDecim8[33] = {
	0xffffffff, 0xfffeffff, 0xffdffbff, 0xfefefeff, 0xfdf7efbf,
	0xfbdf7bdf, 0xf7bbddef, 0xeeeeeeef, 0xeeddbb77, 0xedb76db7,
	0xdb6db6db, 0xdb5b5b5b, 0xdab5ad6b, 0xd5ab55ab, 0xd555aaab,
	0xaaaaaaab, 0xaaaa5555, 0xaa952a55, 0xa94a5295, 0xa5252525,
	0xa4924925, 0x92491249, 0x91224489, 0x91111111, 0x90884211,
	0x88410821, 0x88102041, 0x81010101, 0x80800801, 0x80010001,
	0x80000001, 0x00000001, 0x00000000
};

enum OverlayPixelFormat {
	kUYVY = 0,
	kVYUY = 1,
	kYUYV = 2,
	kYVYU = 3
};

struct OverlaySourceDest {
	uint32	dstX1;
	uint32	dstY1;
	uint32	dstX2;
	uint32	dstY2;
	uint32	srcX1;
	uint32	srcY1;
	uint32	srcX2;
	uint32	srcY2;
	int32	logicalDstX1;
	int32	logicalDstY1;
	int32	logicalDstX2;
	int32	logicalDstY2;
};

struct OverlaySlotInfo {
	bool	used;
	bool	linear;
	uint32	width;
	uint32	height;
	uint32	stride;
	uint32	uvStride;
	uint32	offset;
	uint32	uOffset;
	uint32	vOffset;
	uint32	pixelFormat;
	uint32	size;
};

static OverlaySlotInfo sOverlayInfo[kOverlaySlots];


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


static inline void
clear_bits(uint32& value, uint32 from, uint32 to)
{
	for (uint32 i = from; i <= to; i++)
		value &= ~(1u << i);
}


static void
reset_overlay_registers(void)
{
	KYRO_OVL_TRACE("reset_overlay_registers\n");
	uint32 value = read32(kDACOverlayAddr);
	clear_bits(value, 0, 20);
	value &= ~(1u << 31);
	write32(kDACOverlayAddr, value);

	value = read32(kDACOverlayUAddr);
	clear_bits(value, 0, 20);
	write32(kDACOverlayUAddr, value);

	value = read32(kDACOverlayVAddr);
	clear_bits(value, 0, 20);
	write32(kDACOverlayVAddr, value);

	value = read32(kDACOverlaySize);
	clear_bits(value, 0, 10);
	clear_bits(value, 12, 31);
	write32(kDACOverlaySize, value);

	write32(kDACOverlayVtDec, kNoDecimation);

	value = read32(kDACPixelFormat);
	clear_bits(value, 4, 9);
	clear_bits(value, 16, 22);
	value &= ~(1u << 7);
	write32(kDACPixelFormat, value);

	value = read32(kDACVerticalScal);
	clear_bits(value, 0, 11);
	clear_bits(value, 16, 22);
	value |= kNoScaling;
	write32(kDACVerticalScal, value);

	value = read32(kDACHorizontalScal);
	clear_bits(value, 0, 11);
	clear_bits(value, 16, 17);
	value |= kNoScaling;
	write32(kDACHorizontalScal, value);

	write32(kDACBlendCtrl, 0);
	write32(kDACStreamCtrl, read32(kDACStreamCtrl) & ~(1u << 1));
	gInfo.sharedInfo->overlay.active = false;
}


static uint32
overlap(uint32 bits, uint32 pattern)
{
	uint32 count = 0;
	while (bits-- > 0) {
		if ((pattern & 1) == 0)
			count++;
		pattern >>= 1;
	}
	return count;
}


static int
slot_index(const overlay_buffer* buffer)
{
	for (uint32 i = 0; i < kOverlaySlots; i++) {
		if (&gInfo.sharedInfo->overlay.buffers[i] == buffer && sOverlayInfo[i].used)
			return (int)i;
	}
	return -1;
}


static uint32
aligned(uint32 value, uint32 alignment)
{
	return (value + alignment - 1) & ~(alignment - 1);
}


static void
set_blend_mode(const overlay_window* window)
{
	KYRO_OVL_TRACE("set_blend_mode flags=0x%08" B_PRIx32 "\n",
		window != NULL ? window->flags : 0);
	uint32 value = read32(kDACBlendCtrl);
	clear_bits(value, 28, 30);

	if (window != NULL && (window->flags & B_OVERLAY_COLOR_KEY) != 0) {
		clear_bits(value, 0, 23);
		value |= (1u << 28);
		value |= ((uint32)window->red.value << 16)
			| ((uint32)window->green.value << 8)
			| (uint32)window->blue.value;
	} else {
		clear_bits(value, 0, 30);
	}

	write32(kDACBlendCtrl, value);
}


static void
enable_overlay_plane(void)
{
	write32(kDACPixelFormat, read32(kDACPixelFormat) | (1u << 7));
	write32(kDACStreamCtrl, read32(kDACStreamCtrl) | (1u << 1));
	gInfo.sharedInfo->overlay.active = true;
	KYRO_OVL_TRACE("enable_overlay_plane\n");
}


static status_t
program_overlay_surface(const OverlaySlotInfo& info)
{
	KYRO_OVL_TRACE("program_overlay_surface linear=%d offset=0x%08" B_PRIx32
		" u=0x%08" B_PRIx32 " v=0x%08" B_PRIx32 " stride=%" B_PRIu32
		" uvStride=%" B_PRIu32 " format=%" B_PRIu32 "\n",
		info.linear, info.offset, info.uOffset, info.vOffset, info.stride,
		info.uvStride, info.pixelFormat);
	uint32 value = read32(kDACOverlayAddr);
	clear_bits(value, 0, 20);
	if (info.linear)
		value &= ~(1u << 31);
	else
		value |= 1u << 31;
	value |= (info.offset >> 4);
	write32(kDACOverlayAddr, value);

	if (!info.linear) {
		value = read32(kDACOverlayUAddr);
		clear_bits(value, 0, 20);
		value |= (info.uOffset >> 4);
		write32(kDACOverlayUAddr, value);

		value = read32(kDACOverlayVAddr);
		clear_bits(value, 0, 20);
		value |= (info.vOffset >> 4);
		write32(kDACOverlayVAddr, value);
	}

	value = read32(kDACPixelFormat);
	clear_bits(value, 4, 9);
	if (info.linear)
		value |= (info.pixelFormat & 0x3u) << 4;
	write32(kDACPixelFormat, value);

	return B_OK;
}


static status_t
set_overlay_view_port(const OverlaySlotInfo& info, const overlay_window* window,
	const overlay_view* view)
{
	KYRO_OVL_TRACE("set_overlay_view_port src=(%u,%u %ux%u) dst=(%d,%d %ux%u)"
		" clip lrtb=(%u,%u,%u,%u)\n",
		view->h_start, view->v_start, view->width, view->height,
		window->h_start, window->v_start, window->width, window->height,
		window->offset_left, window->offset_right, window->offset_top,
		window->offset_bottom);
	OverlaySourceDest srcDest = {};
	srcDest.srcX1 = view->h_start;
	srcDest.srcY1 = view->v_start;
	srcDest.srcX2 = view->h_start + view->width - 1;
	srcDest.srcY2 = view->v_start + view->height - 1;

	srcDest.dstX1 = window->h_start + window->offset_left;
	srcDest.dstY1 = window->v_start + window->offset_top;
	srcDest.dstX2 = window->h_start + window->width - window->offset_right - 1;
	srcDest.dstY2 = window->v_start + window->height - window->offset_bottom - 1;

	srcDest.logicalDstX1 = window->h_start;
	srcDest.logicalDstY1 = window->v_start;
	srcDest.logicalDstX2 = window->h_start + window->width - 1;
	srcDest.logicalDstY2 = window->v_start + window->height - 1;

	uint32 srcTop = srcDest.srcY1;
	uint32 srcBottom = srcDest.srcY2;
	uint32 src = srcBottom - srcTop;
	uint32 dest = srcDest.logicalDstY2 - srcDest.logicalDstY1;
	if (src <= 1)
		return B_BAD_VALUE;

	uint32 fxScale = (dest << 11) / src;
	uint32 fxOffset = (srcDest.logicalDstY2 - (int32)srcDest.dstY2) << 11;
	srcBottom = srcBottom - (fxOffset / fxScale);
	src = srcBottom - srcTop;
	uint32 height = src;

	dest = srcDest.dstY2 - (srcDest.dstY1 - 1);
	uint32 bits = 0;
	uint32 pattern = kDecim8[bits];

	if (src > dest) {
		uint32 decimate = src - dest;
		uint32 applied = src / 32;
		while (((bits * applied) + overlap((src % 32), kDecim8[bits])) < decimate)
			bits++;

		pattern = kDecim8[bits];
		uint32 decimated = (bits * applied) + overlap((src % 32), pattern);
		src -= decimated;
	}

	uint32 vertDecFactor = 1;
	if (bits != 0 && bits != 32)
		vertDecFactor = (63 - bits) / (32 - bits);

	uint32 dacYScale = ((src - 1) * 2048) / (dest + 1);
	write32(kDACOverlayVtDec, pattern);

	src = srcDest.srcX2 - srcDest.srcX1;
	dest = srcDest.logicalDstX2 - srcDest.logicalDstX1;
	uint32 left;
	uint32 right;
	if (srcDest.dstX1 > 2) {
		left = srcDest.dstX1 + 2;
		right = srcDest.dstX2 + 1;
	} else {
		left = srcDest.dstX1;
		right = srcDest.dstX2 + 1;
	}

	uint32 clipOffset = 0;
	uint32 srcLeft = 0;
	uint32 srcRight = 0;
	uint32 scaleLeft = 0;
	uint32 hDecim = 0;
	uint32 scale = 0;
	uint32 width = 0;
	uint32 stride = 0;
	uint32 excessPixels = 0;
	uint32 addStride = 0;

	if (dest == 0)
		return B_BAD_VALUE;

	fxScale = ((src - 1) << 11) / dest;
	fxOffset = fxScale * ((srcDest.dstX1 - srcDest.logicalDstX1) + clipOffset);
	fxOffset >>= 11;
	srcLeft = srcDest.srcX1 + fxOffset;

	fxOffset = fxScale * (srcDest.logicalDstX2 - (int32)srcDest.dstX2);
	fxOffset >>= 11;
	srcRight = srcDest.srcX2 - fxOffset;
	scaleLeft = srcLeft;

	hDecim = 0;
	scale = (((srcRight - srcLeft) - 1) << (11 - hDecim)) / (right - left + 2);
	while (scale > 0x800) {
		hDecim++;
		scale = (((srcRight - srcLeft) - 1) << (11 - hDecim)) / (right - left + 2);
	}

	if (!info.linear) {
		srcLeft &= ~0x1f;
		srcRight = aligned(srcRight + 1, 32);
	} else {
		srcLeft &= ~0x7;
		srcRight = aligned(srcRight + 1, 8);
	}

	width = srcRight - srcLeft;
	uint32 strideValue = ((width / 8) >> hDecim);
	if (width != (strideValue << hDecim) * 8)
		addStride = 1;

	src = width >> hDecim;
	if (src <= 2)
		return B_BAD_VALUE;

	excessPixels = ((scaleLeft - srcLeft) << (11 - hDecim)) / scale;
	uint32 clip = (src << 11) / scale;
	clip -= (right - left);
	clip += excessPixels;
	if (clip != 0)
		clip--;

	uint32 extraLines = (1u << hDecim) * vertDecFactor + 64;
	height += extraLines;

	uint32 vertical = read32(kDACVerticalScal);
	clear_bits(vertical, 0, 11);
	clear_bits(vertical, 16, 22);
	stride = (width >> (hDecim + 3)) + addStride;
	vertical |= (stride << 16) | (dacYScale & 0xfff);
	write32(kDACVerticalScal, vertical);

	uint32 overlaySize = read32(kDACOverlaySize);
	clear_bits(overlaySize, 0, 10);
	clear_bits(overlaySize, 12, 31);
	if (info.linear) {
		overlaySize |= (info.stride / 16)
			| ((height + 1) << 12)
			| (((width / 8) - 1) << 23);
	} else {
		overlaySize |= (info.stride / 16)
			| ((height + 1) << 12)
			| (((width / 32) - 1) << 23);
	}
	write32(kDACOverlaySize, overlaySize);

	write32(kDACVidWinStart, (left << 16) | srcDest.dstY1);
	write32(kDACVidWinEnd, (right << 16) | srcDest.dstY2);

	uint32 pixelFormat = read32(kDACPixelFormat);
	pixelFormat = ((excessPixels << 16) | pixelFormat) & 0x7fffffff;
	write32(kDACPixelFormat, pixelFormat);

	uint32 horizontal = read32(kDACHorizontalScal);
	clear_bits(horizontal, 0, 11);
	clear_bits(horizontal, 16, 17);
	horizontal |= (hDecim << 16) | (scale & 0xfff);
	write32(kDACHorizontalScal, horizontal);
	KYRO_OVL_TRACE("set_overlay_view_port done left=%" B_PRIu32 " right=%" B_PRIu32
		" height=%" B_PRIu32 " width=%" B_PRIu32 " hDecim=%" B_PRIu32
		" scale=0x%08" B_PRIx32 " yScale=0x%08" B_PRIx32 "\n",
		left, right, height, width, hDecim, scale, dacYScale);

	return B_OK;
}

} // namespace


uint32
kyro_overlay_count(const display_mode* mode)
{
	(void)mode;
	return 1;
}


const uint32*
kyro_overlay_supported_spaces(const display_mode* mode)
{
	(void)mode;
	static const uint32 spaces[] = {
		B_YCbCr422,
		B_YUV422,
		B_YCbCr420,
		B_YUV420,
		0
	};
	return spaces;
}


uint32
kyro_overlay_supported_features(uint32 colorSpace)
{
	switch (colorSpace) {
		case B_YCbCr422:
		case B_YUV422:
		case B_YCbCr420:
		case B_YUV420:
			return B_OVERLAY_COLOR_KEY
				| B_OVERLAY_HORIZONTAL_FILTERING
				| B_OVERLAY_VERTICAL_FILTERING;
		default:
			return 0;
	}
}


const overlay_buffer*
kyro_allocate_overlay_buffer(color_space space, uint16 width, uint16 height)
{
	KYRO_OVL_TRACE("allocate_overlay_buffer space=0x%08" B_PRIx32 " %ux%u\n",
		(uint32)space, width, height);
	if (width == 0 || height == 0 || width > kOverlayMaxWidth
		|| height > kOverlayMaxHeight) {
		return NULL;
	}

	bool linear;
	uint32 pixelFormat = kUYVY;
	switch (space) {
		case B_YCbCr422:
			linear = true;
			pixelFormat = kYUYV;
			break;
		case B_YUV422:
			linear = true;
			pixelFormat = kUYVY;
			break;
		case B_YCbCr420:
		case B_YUV420:
			linear = false;
			break;
		default:
			return NULL;
	}

	uint32 base = aligned(gInfo.sharedInfo->frameBufferConfig.bytes_per_row
		* gInfo.sharedInfo->currentMode.virtual_height, 32);
	uint32 limit = gInfo.sharedInfo->cursorOffset;

	int slot = -1;
	for (uint32 i = 0; i < kOverlaySlots; i++) {
		if (!sOverlayInfo[i].used) {
			slot = (int)i;
			break;
		}
	}
	if (slot < 0)
		return NULL;

	uint32 strideWords;
	uint32 strideBytes;
	uint32 uvStrideBytes = 0;
	uint32 size;
	if (linear) {
		strideWords = ((width & 0x7) == 0) ? (width / 8) : ((width + 8) / 8);
		strideBytes = strideWords * 16;
		size = strideBytes * height;
	} else {
		strideWords = ((width & 0xf) == 0) ? (width / 16) : ((width + 16) / 16);
		strideBytes = strideWords * 16;

		uint32 uvWidth = (width + 1) / 2;
		uint32 uvStrideWords = ((uvWidth & 0xf) == 0) ? (uvWidth / 16)
			: ((uvWidth + 16) / 16);
		uvStrideBytes = uvStrideWords * 16;

		uint32 uOffset = aligned(strideBytes * height, 32);
		uint32 vOffset = aligned(uOffset + (height / 2) * uvStrideBytes, 32);
		size = vOffset + (height / 2) * uvStrideBytes;
	}

	uint32 offset = base;
	for (uint32 i = 0; i < kOverlaySlots; i++) {
		if (!sOverlayInfo[i].used)
			continue;
		uint32 end = sOverlayInfo[i].offset + sOverlayInfo[i].size;
		if (end > offset)
			offset = aligned(end, 32);
	}

	if (offset + size > limit)
		return NULL;

	OverlaySlotInfo& meta = sOverlayInfo[slot];
	memset(&meta, 0, sizeof(meta));
	meta.used = true;
	meta.linear = linear;
	meta.width = width;
	meta.height = height;
	meta.stride = strideBytes;
	meta.uvStride = uvStrideBytes;
	meta.offset = offset;
	meta.pixelFormat = pixelFormat;
	meta.size = size;
	if (!linear) {
		meta.uOffset = offset + aligned(strideBytes * height, 32);
		meta.vOffset = offset + aligned(aligned(strideBytes * height, 32)
			+ (height / 2) * uvStrideBytes, 32);
	}

	overlay_buffer& buffer = gInfo.sharedInfo->overlay.buffers[slot];
	memset(&buffer, 0, sizeof(buffer));
	buffer.space = space;
	buffer.width = width;
	buffer.height = height;
	buffer.bytes_per_row = strideBytes;
	buffer.buffer = gInfo.sharedInfo->frameBuffer + offset;
	buffer.buffer_dma = (void*)(addr_t)(gInfo.sharedInfo->frameBufferPCI + offset);

	gInfo.sharedInfo->overlay.bufferAllocated[slot] = true;
	gInfo.sharedInfo->overlay.bufferOffset[slot] = offset;
	KYRO_OVL_TRACE("allocate_overlay_buffer slot=%d offset=0x%08" B_PRIx32
		" size=%" B_PRIu32 " stride=%" B_PRIu32 " uvStride=%" B_PRIu32 "\n",
		slot, offset, size, strideBytes, uvStrideBytes);
	return &buffer;
}


status_t
kyro_release_overlay_buffer(const overlay_buffer* buffer)
{
	KYRO_OVL_TRACE("release_overlay_buffer buffer=%p\n", buffer);
	if (buffer == NULL)
		return B_BAD_VALUE;

	int slot = slot_index(buffer);
	if (slot < 0)
		return B_BAD_VALUE;

	memset(&gInfo.sharedInfo->overlay.buffers[slot], 0,
		sizeof(gInfo.sharedInfo->overlay.buffers[slot]));
	gInfo.sharedInfo->overlay.bufferAllocated[slot] = false;
	gInfo.sharedInfo->overlay.bufferOffset[slot] = 0;
	memset(&sOverlayInfo[slot], 0, sizeof(sOverlayInfo[slot]));
	return B_OK;
}


status_t
kyro_get_overlay_constraints(const display_mode* mode, const overlay_buffer* buffer,
	overlay_constraints* constraints)
{
	if (mode == NULL || buffer == NULL || constraints == NULL)
		return B_BAD_VALUE;

	memset(constraints, 0, sizeof(*constraints));
	constraints->view.width_alignment = buffer->space == B_YCbCr420
		|| buffer->space == B_YUV420 ? 1 : 0;
	constraints->view.height_alignment = buffer->space == B_YCbCr420
		|| buffer->space == B_YUV420 ? 1 : 0;
	constraints->window.width_alignment = 0;
	constraints->window.height_alignment = 0;
	constraints->view.width.min = 32;
	constraints->view.width.max = kOverlayMaxWidth;
	constraints->view.height.min = 32;
	constraints->view.height.max = kOverlayMaxHeight;
	constraints->window.width.min = 16;
	constraints->window.width.max = mode->virtual_width;
	constraints->window.height.min = 16;
	constraints->window.height.max = mode->virtual_height;
	constraints->h_scale.min = 1.0f / 8.0f;
	constraints->h_scale.max = 8.0f;
	constraints->v_scale.min = 1.0f / 8.0f;
	constraints->v_scale.max = 8.0f;
	return B_OK;
}


overlay_token
kyro_allocate_overlay(void)
{
	KYRO_OVL_TRACE("allocate_overlay token=%" B_PRIu32 "\n",
		gInfo.sharedInfo->overlay.token);
	if (gInfo.sharedInfo->overlay.token != 0)
		return NULL;

	gInfo.sharedInfo->overlay.token = 1;
	return (overlay_token)(addr_t)gInfo.sharedInfo->overlay.token;
}


status_t
kyro_release_overlay(overlay_token token)
{
	KYRO_OVL_TRACE("release_overlay token=%p current=%" B_PRIu32 "\n", token,
		gInfo.sharedInfo->overlay.token);
	if ((uint32)(addr_t)token != gInfo.sharedInfo->overlay.token)
		return B_BAD_VALUE;

	reset_overlay_registers();
	gInfo.sharedInfo->overlay.token = 0;
	return B_OK;
}


status_t
kyro_configure_overlay(overlay_token token, const overlay_buffer* buffer,
	const overlay_window* window, const overlay_view* view)
{
	KYRO_OVL_TRACE("configure_overlay token=%p buffer=%p window=%p view=%p\n",
		token, buffer, window, view);
	if ((uint32)(addr_t)token != gInfo.sharedInfo->overlay.token)
		return B_BAD_VALUE;

	if (buffer == NULL || window == NULL || view == NULL) {
		reset_overlay_registers();
		return B_OK;
	}

	int slot = slot_index(buffer);
	if (slot < 0)
		return B_BAD_VALUE;

	reset_overlay_registers();
	status_t status = program_overlay_surface(sOverlayInfo[slot]);
	if (status != B_OK)
		return status;

	status = set_overlay_view_port(sOverlayInfo[slot], window, view);
	if (status != B_OK) {
		reset_overlay_registers();
		return status;
	}

	set_blend_mode(window);
	enable_overlay_plane();
	KYRO_OVL_TRACE("configure_overlay done active=%d\n",
		gInfo.sharedInfo->overlay.active);
	return B_OK;
}


void
ResetOverlayState(void)
{
	KYRO_OVL_TRACE("ResetOverlayState\n");
	reset_overlay_registers();
	gInfo.sharedInfo->overlay.token = 0;
	memset(gInfo.sharedInfo->overlay.buffers, 0,
		sizeof(gInfo.sharedInfo->overlay.buffers));
	memset(gInfo.sharedInfo->overlay.bufferAllocated, 0,
		sizeof(gInfo.sharedInfo->overlay.bufferAllocated));
	memset(gInfo.sharedInfo->overlay.bufferOffset, 0,
		sizeof(gInfo.sharedInfo->overlay.bufferOffset));
	memset(sOverlayInfo, 0, sizeof(sOverlayInfo));
}
