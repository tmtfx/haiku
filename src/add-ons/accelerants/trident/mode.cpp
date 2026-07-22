/*
 * Copyright 2026, Gemini CLI. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Gemini CLI <gemini-cli@google.com>
 */


#include "accel.h"

#include <create_display_modes.h>
#include <string.h>
#include <unistd.h>


static bool
IsThereEnoughFBMemory(const display_mode* mode, uint32 bitsPerPixel)
{
	uint32 maxWidth = mode->virtual_width;
	if (mode->timing.h_display > maxWidth)
		maxWidth = mode->timing.h_display;

	uint32 maxHeight = mode->virtual_height;
	if (mode->timing.v_display > maxHeight)
		maxHeight = mode->timing.v_display;

	uint32 bytesPerPixel = (bitsPerPixel + 7) / 8;

	return (maxWidth * maxHeight * bytesPerPixel < gInfo.sharedInfo->maxFrameBufferSize);
}


bool
IsModeUsable(display_mode* mode)
{
	uint32 bitsPerPixel = 0;
	uint32 maxPixelClock = 230000; // max pixel clock for Blade3D (KHz)

	switch (mode->space) {
		case B_CMAP8:
			bitsPerPixel = 8;
			break;
		case B_RGB15:
		case B_RGB16:
			bitsPerPixel = 16;
			break;
		case B_RGB32:
			bitsPerPixel = 32;
			break;
		default:
			return false;
	}

	if (!IsThereEnoughFBMemory(mode, bitsPerPixel))
		return false;

	if (mode->timing.pixel_clock > maxPixelClock)
		return false;

	return true;
}


status_t
CreateModeList(void)
{
	SharedInfo& si = *gInfo.sharedInfo;

	si.bHaveEDID = false;

	// Attempt to get EDID info from the driver
	edid1_raw rawEdid;
	if (ioctl(gInfo.deviceFileDesc, TRIDENT_GET_EDID, &rawEdid, sizeof(rawEdid)) == B_OK) {
		if (rawEdid.version.version == 1 && rawEdid.version.revision <= 4) {
			edid_decode(&si.edidInfo, &rawEdid);
			si.bHaveEDID = true;
		}
	}

	if (si.bHaveEDID) {
#ifdef ENABLE_DEBUG_TRACE
		edid_dump(&(si.edidInfo));
#endif
	} else {
		TRACE("CreateModeList(); Unable to get EDID info\n");
	}

	// Supported color spaces on Trident Blade3D
	static const color_space spaces[] = {
		B_RGB32,
		B_RGB16,
		B_CMAP8
	};
	static const uint32 spacesCount = sizeof(spaces) / sizeof(spaces[0]);

	display_mode* list;
	uint32 count = 0;
	area_id listArea;

	listArea = create_display_modes("Trident modes",
		si.bHaveEDID ? &si.edidInfo : NULL,
		NULL, 0, spaces, spacesCount, 
		(check_display_mode_hook)IsModeUsable, &list, &count);

	if (listArea < 0)
		return listArea;

	si.modeArea = gInfo.modeListArea = listArea;
	si.modeCount = count;
	gInfo.modeList = list;

	return B_OK;
}


status_t
ProposeDisplayMode(display_mode *target, const display_mode *low,
    const display_mode *high)
{
	(void)low;
	(void)high;

	TRACE("ProposeDisplayMode() %dx%d, pixel clock: %d kHz, space: 0x%X\n",
		target->timing.h_display, target->timing.v_display,
		target->timing.pixel_clock, target->space);

	SharedInfo& si = *(gInfo.sharedInfo);

	uint32 bytesPerPixel = 0;
	switch (target->space) {
		case B_CMAP8:  bytesPerPixel = 1; break;
		case B_RGB15:
		case B_RGB16:  bytesPerPixel = 2; break;
		case B_RGB32:  bytesPerPixel = 4; break;
		default:
			return B_BAD_VALUE;
	}

	uint32 requiredMemory = target->timing.h_display * target->timing.v_display * bytesPerPixel;
	if (si.videoMemSize > 0 && requiredMemory > si.videoMemSize)
		return B_BAD_VALUE;

	uint32 modeCount = si.modeCount;
	for (uint32 j = 0; j < modeCount; j++) {
		display_mode& mode = gInfo.modeList[j];

		if (target->timing.h_display == mode.timing.h_display
			&& target->timing.v_display == mode.timing.v_display
			&& target->space == mode.space) {
			*target = mode;
			return B_OK;
		}
	}

	if (modeCount > 0) {
		*target = gInfo.modeList[0];
		return B_OK;
	}

	return B_BAD_VALUE;
}


status_t 
SetDisplayMode(display_mode* pMode)
{
	TRACE("SetDisplayMode() begin\n");

	SharedInfo& si = *gInfo.sharedInfo;
	DisplayModeEx mode;
	(display_mode&)mode = *pMode;

	switch (mode.space) {
		case B_CMAP8:
			mode.bpp = 8;
			break;
		case B_RGB15:
		case B_RGB16:
			mode.bpp = 16;
			break;
		case B_RGB32:
			mode.bpp = 32;
			break;
		default:
			return B_BAD_VALUE;
	}

	if (ProposeDisplayMode(&mode, pMode, pMode) != B_OK)
		return B_BAD_VALUE;

	int bytesPerPixel = (mode.bpp + 7) / 8;
	mode.bytesPerRow = mode.timing.h_display * bytesPerPixel;

	if (!IsThereEnoughFBMemory(&mode, mode.bpp))
		return B_NO_MEMORY;

	TRACE("Set display mode: %dx%d virtual size: %dx%d color depth: %d bpp\n",
		mode.timing.h_display, mode.timing.v_display,
		mode.virtual_width, mode.virtual_height, mode.bpp);

	si.displayMode = mode;

	TRACE("SetDisplayMode() done\n");
	return B_OK;
}


status_t 
MoveDisplay(uint16 horizontalStart, uint16 verticalStart)
{
	DisplayModeEx& mode = gInfo.sharedInfo->displayMode;

	if (mode.timing.h_display + horizontalStart > mode.virtual_width
		|| mode.timing.v_display + verticalStart > mode.virtual_height)
		return B_ERROR;

	mode.h_display_start = horizontalStart;
	mode.v_display_start = verticalStart;

	return B_OK;
}


uint32 
AccelerantModeCount(void)
{
	if (gInfo.sharedInfo->modeCount == 0) {
		CreateModeList();
	}
	return gInfo.sharedInfo->modeCount;
}


status_t 
GetModeList(display_mode* dmList)
{
	if (gInfo.sharedInfo->modeCount == 0) {
		CreateModeList();
	}
	memcpy(dmList, gInfo.modeList, gInfo.sharedInfo->modeCount * sizeof(display_mode));
	return B_OK;
}


status_t 
GetDisplayMode(display_mode* current_mode)
{
	*current_mode = gInfo.sharedInfo->displayMode;
	return B_OK;
}


status_t 
GetFrameBufferConfig(frame_buffer_config* pFBC)
{
	SharedInfo& si = *gInfo.sharedInfo;

	pFBC->frame_buffer = (void*)((addr_t)si.videoMemAddr + si.frameBufferOffset);
	pFBC->frame_buffer_dma = (void*)((addr_t)si.videoMemPCI + si.frameBufferOffset);
	uint32 bytesPerPixel = (si.displayMode.bpp + 7) / 8;
	pFBC->bytes_per_row = si.displayMode.virtual_width * bytesPerPixel;

	return B_OK;
}


status_t 
GetPixelClockLimits(display_mode* mode, uint32* low, uint32* high)
{
	uint32 maxPixelClock = 230000;

	if (low != NULL) {
		uint32 totalClocks = (uint32)mode->timing.h_total * (uint32)mode->timing.v_total;
		uint32 lowClock = (totalClocks * 48L) / 1000L;
		if (lowClock > maxPixelClock)
			return B_ERROR;

		*low = lowClock;
	}

	if (high != NULL)
		*high = maxPixelClock;

	return B_OK;
}


status_t
GetTimingConstraints(display_timing_constraints *constraints)
{
	(void)constraints;
	return B_ERROR;
}


status_t
GetPreferredDisplayMode(display_mode* preferredMode)
{
	(void)preferredMode;
	return B_ERROR;
}


#ifdef __HAIKU__
status_t
GetEdidInfo(void* info, size_t size, uint32* _version)
{
	SharedInfo& si = *gInfo.sharedInfo;

	if (!si.bHaveEDID)
		return B_ERROR;

	if (size < sizeof(struct edid1_info))
		return B_BUFFER_OVERFLOW;

	memcpy(info, &si.edidInfo, sizeof(struct edid1_info));
	*_version = EDID_VERSION_1;
	return B_OK;
}
#endif
