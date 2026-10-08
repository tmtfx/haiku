/*
 * Copyright 2026, GitHub Copilot. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "accelerant.h"

#include <compute_display_timing.h>
#include <create_display_modes.h>

#include <string.h>


namespace {

static const uint32 kRefClockKHz = 14318;
static const uint32 kPixelBusWidth = 128;

static const uint32 kSoftwareReset = 0x0080;
static const uint32 kDACPLLMode = 0x0184;
static const uint32 kDACPrimAddress = 0x1400;
static const uint32 kDACPrimSize = 0x1404;
static const uint32 kDACCursorAddr = 0x1408;
static const uint32 kDACCursorCtrl = 0x140c;
static const uint32 kDACPixelFormat = 0x144c;
static const uint32 kDACVidWinStart = 0x1454;
static const uint32 kDACVidWinEnd = 0x1458;
static const uint32 kDACHorTim1 = 0x1460;
static const uint32 kDACHorTim2 = 0x1464;
static const uint32 kDACHorTim3 = 0x1468;
static const uint32 kDACVerTim1 = 0x146c;
static const uint32 kDACVerTim2 = 0x1470;
static const uint32 kDACVerTim3 = 0x1474;
static const uint32 kDACBorderColor = 0x1478;
static const uint32 kDACSyncCtrl = 0x147c;
static const uint32 kDACStreamCtrl = 0x1480;
static const uint32 kDACBurstCtrl = 0x148c;
static const uint32 kDACCrcTrigger = 0x1490;
static const uint32 kDigVidPortCtrl = 0x1700;

struct ModePreset {
	uint32	pixelClock;
	uint16	hDisplay;
	uint16	hSyncStart;
	uint16	hSyncEnd;
	uint16	hTotal;
	uint16	vDisplay;
	uint16	vSyncStart;
	uint16	vSyncEnd;
	uint16	vTotal;
	uint32	flags;
};

static const uint32 kPositiveHSync = B_POSITIVE_HSYNC;
static const uint32 kPositiveVSync = B_POSITIVE_VSYNC;

static const ModePreset kModePresets[] = {
	{31500,  640,  736,  800,  832,  350,  382,  385,  445,  kPositiveHSync},
	{31500,  640,  736,  800,  832,  400,  401,  404,  445,  kPositiveVSync},
	{35500,  720,  828,  900,  936,  400,  401,  404,  446,  kPositiveVSync},
	{25175,  640,  656,  752,  800,  480,  490,  492,  525,  0},
	{31500,  640,  664,  704,  832,  480,  489,  492,  520,  0},
	{31500,  640,  656,  720,  840,  480,  481,  484,  500,  0},
	{36000,  640,  696,  752,  832,  480,  481,  484,  509,  0},
	{36000,  800,  824,  896, 1024,  600,  601,  603,  625,  kPositiveHSync | kPositiveVSync},
	{40000,  800,  840,  968, 1056,  600,  601,  605,  628,  kPositiveHSync | kPositiveVSync},
	{50000,  800,  856,  976, 1040,  600,  637,  643,  666,  kPositiveHSync | kPositiveVSync},
	{49500,  800,  816,  896, 1056,  600,  601,  604,  625,  kPositiveHSync | kPositiveVSync},
	{56250,  800,  832,  896, 1048,  600,  601,  604,  631,  kPositiveHSync | kPositiveVSync},
	{65000, 1024, 1048, 1184, 1344,  768,  771,  777,  806,  0},
	{75000, 1024, 1048, 1184, 1328,  768,  771,  777,  806,  0},
	{78750, 1024, 1040, 1136, 1312,  768,  769,  772,  800,  kPositiveHSync | kPositiveVSync},
	{94500, 1024, 1072, 1168, 1376,  768,  769,  772,  808,  kPositiveHSync | kPositiveVSync},
	{108000, 1152, 1216, 1344, 1472, 864, 865, 868, 914, kPositiveHSync | kPositiveVSync},
	{108000, 1280, 1376, 1488, 1688, 960, 961, 964, 1013, kPositiveHSync | kPositiveVSync},
	{148500, 1280, 1344, 1504, 1728, 960, 961, 964, 1011, kPositiveHSync | kPositiveVSync},
	{108000, 1280, 1328, 1440, 1688, 1024, 1025, 1028, 1066, kPositiveHSync | kPositiveVSync},
	{135000, 1280, 1296, 1440, 1688, 1024, 1025, 1028, 1066, kPositiveHSync | kPositiveVSync},
	{157500, 1280, 1344, 1504, 1728, 1024, 1025, 1028, 1070, kPositiveHSync | kPositiveVSync},
	{162000, 1600, 1664, 1856, 2160, 1200, 1201, 1204, 1250, kPositiveHSync | kPositiveVSync},
	{175500, 1600, 1664, 1856, 2160, 1200, 1201, 1204, 1250, kPositiveHSync | kPositiveVSync},
	{189000, 1600, 1664, 1856, 2160, 1200, 1201, 1204, 1250, kPositiveHSync | kPositiveVSync},
	{202500, 1600, 1664, 1856, 2160, 1200, 1201, 1204, 1250, kPositiveHSync | kPositiveVSync},
	{229500, 1600, 1664, 1856, 2160, 1200, 1201, 1204, 1250, kPositiveHSync | kPositiveVSync},
	{204750, 1792, 1920, 2120, 2248, 1344, 1345, 1348, 1394, kPositiveVSync},
	{261000, 1792, 1888, 2104, 2144, 1344, 1345, 1348, 1394, kPositiveVSync},
	{218250, 1856, 1952, 2176, 2304, 1392, 1393, 1396, 1439, kPositiveVSync},
	{288000, 1856, 1984, 2208, 2336, 1392, 1393, 1396, 1439, kPositiveVSync},
	{234000, 1920, 2048, 2256, 2392, 1440, 1441, 1444, 1487, kPositiveVSync},
	{297000, 1920, 2064, 2288, 2448, 1440, 1441, 1444, 1482, kPositiveVSync},
};


static void
update_frame_buffer_config(const display_mode& mode);


static uint32
get_line_length(uint32 width, uint32 bitsPerPixel)
{
	return (((width * bitsPerPixel) + 31) & ~31) >> 3;
}


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


static uint32
color_space_bits_per_pixel(color_space space)
{
	switch (space) {
		case B_RGB16:
			return 16;
		case B_RGB32:
			return 32;
		default:
			return 0;
	}
}


static bool
has_enough_frame_buffer(const display_mode* mode, uint32 bitsPerPixel)
{
	uint64 needed = (uint64)get_line_length(mode->virtual_width, bitsPerPixel)
		* mode->virtual_height;
	return needed <= gInfo.sharedInfo->frameBufferSize;
}


static bool
is_mode_usable(display_mode* mode)
{
	uint32 bitsPerPixel = color_space_bits_per_pixel((color_space)mode->space);
	if (bitsPerPixel == 0)
		return false;

	if (mode->timing.pixel_clock > gInfo.sharedInfo->maxPixelClock)
		return false;

	mode->virtual_width = mode->timing.h_display;
	mode->virtual_height = mode->timing.v_display;
	mode->h_display_start = 0;
	mode->v_display_start = 0;
	mode->flags = B_SCROLL | B_DPMS | B_PARALLEL_ACCESS | B_SUPPORTS_OVERLAYS;
	if (gInfo.sharedInfo->settings.hardcursor)
		mode->flags |= B_HARDWARE_CURSOR;
	return has_enough_frame_buffer(mode, bitsPerPixel);
}


static uint32
to_100hz(uint32 pixelClockKHz)
{
	return pixelClockKHz * 10;
}


static uint32
program_clock(uint32 refClock, uint32 requested100Hz, uint32& feedbackOut,
	uint32& dividerOut, uint32& postDividerOut)
{
	static const uint32 kScaler = 8;
	static const uint32 kMinR = 2;
	static const uint32 kMaxR = 33;
	static const uint32 kMinF = 2;
	static const uint32 kMaxF = 513;
	static const uint32 kMinRestrictedVco = 100000000;
	static const uint32 kMaxRestrictedVco = 500000000;
	static const uint32 kMaxVco = 500000000;
	static const uint32 kODValues[] = {1, 2, 0};

	uint32 bestClock = 0;
	uint32 bestScore = 0;
	uint32 bestR = 0;
	uint32 bestF = 0;
	uint32 bestOD = 0;

	uint32 requestedHz = requested100Hz * 100;
	uint32 refHz = refClock * 1000;
	uint32 minClock = requestedHz - (requestedHz >> 8);
	uint32 maxClock = requestedHz + (requestedHz >> 8);
	uint32 scaledRequest = requestedHz >> kScaler;

	for (uint32 i = 0; i < 3; i++) {
		uint32 od = kODValues[i];

		for (uint32 r = kMinR; r <= kMaxR; r++) {
			uint32 scaled = r * (scaledRequest << od);
			uint32 f = scaled / (refHz >> kScaler);
			if (f > kMinF)
				f--;

			while (f >= kMinF && f <= kMaxF) {
				uint64 vco = (uint64)refHz * f / r;
				if (vco >= kMinRestrictedVco
					&& (vco <= kMaxRestrictedVco
						|| (requestedHz > kMaxRestrictedVco && vco <= kMaxVco))) {
					uint32 clock = (uint32)(vco >> od);
					if (clock >= minClock && clock <= maxClock) {
						uint32 phaseScore = (((refHz / r) - (refHz / kMaxR))
							/ ((refHz - (refHz / kMaxR)) >> 10));
						uint32 vcoScore = ((vco - kMinRestrictedVco)
							/ ((kMaxRestrictedVco - kMinRestrictedVco) >> 10));
						uint32 score = phaseScore + vcoScore;

						if (bestScore == 0 || (score >= bestScore && od > 0)) {
							bestScore = score;
							bestClock = clock;
							bestR = r;
							bestF = f;
							bestOD = od;
						}
					}
				}
				f++;
			}
		}
	}

	if (bestScore == 0)
		return 0;

	dividerOut = bestR;
	feedbackOut = bestF;
	postDividerOut = (bestOD == 2 || bestOD == 3) ? 3 : bestOD;
	return bestClock;
}


static void
disable_vga(void)
{
	uint32 value = read32(kSoftwareReset) & ~(1u << 8);
	write32(kSoftwareReset, value);
	for (volatile uint32 i = 0; i < 1000; i++) {
	}
	write32(kSoftwareReset, read32(kSoftwareReset) | (1u << 8));
}


static void
stop_vtg(void)
{
	uint32 value = read32(kDACSyncCtrl);
	value |= (1u << 0) | (1u << 2);
	value &= ~(1u << 31);
	write32(kDACSyncCtrl, value);
}


static void
start_vtg(void)
{
	uint32 value = read32(kDACSyncCtrl);
	value |= (1u << 31);
	value &= ~((1u << 0) | (1u << 2));
	write32(kDACSyncCtrl, value);
}


static void
disable_output(void)
{
	write32(kDACStreamCtrl, read32(kDACStreamCtrl) & ~(1u << 0));
}


static void
enable_output(void)
{
	write32(kDACStreamCtrl, read32(kDACStreamCtrl) | (1u << 0));
}


static status_t
initialise_ramdac(const display_mode& mode)
{
	uint32 bitsPerPixel = color_space_bits_per_pixel((color_space)mode.space);
	if (bitsPerPixel == 0)
		return B_BAD_VALUE;

	uint32 physicalDepth = bitsPerPixel;
	uint32 pixelFormat = read32(kDACPixelFormat) & ~0x307;
	switch (bitsPerPixel) {
		case 16:
			pixelFormat |= 2;
			break;
		case 32:
			pixelFormat |= 4;
			break;
		default:
			return B_BAD_VALUE;
	}
	write32(kDACPixelFormat, pixelFormat);

	uint32 divisor = kPixelBusWidth / physicalDepth;
	uint32 stride = mode.virtual_width;
	uint32 primSize = (((mode.timing.v_display - 1) << 12)
		| (((mode.virtual_width / divisor) - 1) << 23)
		| (stride / divisor));
	write32(kDACPrimSize, primSize);

	uint32 feedback;
	uint32 divider;
	uint32 postDivider;
	if (program_clock(kRefClockKHz, to_100hz(mode.timing.pixel_clock), feedback,
			divider, postDivider) == 0) {
		return B_ERROR;
	}

	uint32 pllMode = read32(kDACPLLMode) & ~0xffff;
	pllMode |= postDivider | ((feedback - 2) << 2) | ((divider - 2) << 11);
	write32(kDACPLLMode, pllMode);

	write32(kDACPrimAddress, 0);
	write32(kDACCursorCtrl, read32(kDACCursorCtrl) & ~(1u << 31));
	write32(kDACCursorAddr, 0);
	write32(kDACVidWinStart, 0);
	write32(kDACVidWinEnd, 0);
	write32(kDACBorderColor, 0);
	write32(kDACBurstCtrl, 0x0404);
	write32(kDACCrcTrigger, read32(kDACCrcTrigger) & ~(1u << 0));
	write32(kDigVidPortCtrl, 0);
	return B_OK;
}


static void
setup_vtg(const display_mode& mode)
{
	uint32 hDisplay = mode.timing.h_display;
	uint32 hFrontPorch = mode.timing.h_sync_start - mode.timing.h_display;
	uint32 hSync = mode.timing.h_sync_end - mode.timing.h_sync_start;
	uint32 hBackPorch = mode.timing.h_total - mode.timing.h_sync_end;
	uint32 hTotal = mode.timing.h_total;

	uint32 vDisplay = mode.timing.v_display;
	uint32 vSync = mode.timing.v_sync_end - mode.timing.v_sync_start;
	uint32 vBackPorch = mode.timing.v_total - mode.timing.v_sync_end;
	uint32 vTotal = mode.timing.v_total;

	uint32 margins = 0;
	if (hDisplay == 640 && vDisplay == 480)
		margins = 8;

	uint32 border = (hTotal - (hSync + (hBackPorch - margins) + hDisplay
		+ (hFrontPorch - margins))) >> 1;

	uint32 hBackPorchStart = hSync;
	uint32 hDisplayStart = hSync + (hBackPorch - margins) + border;
	uint32 hLeftBorderStart = hDisplayStart - border;
	uint32 hFrontPorchStart = hSync + (hBackPorch - margins) + border
		+ hDisplay + border;
	uint32 hRightBorderStart = hFrontPorchStart - border;

	uint32 vBackPorchStart = vSync;
	uint32 vDisplayStart = vSync + (vBackPorch - margins) + border;
	uint32 vTopBorderStart = vDisplayStart - border;
	uint32 vFrontPorchStart = vSync + (vBackPorch - margins) + border
		+ vDisplay + border;
	uint32 vBottomBorderStart = vFrontPorchStart - border;

	write32(kDACHorTim1, hTotal | (hBackPorchStart << 16));
	write32(kDACHorTim2, hLeftBorderStart | (hDisplayStart << 16));
	write32(kDACHorTim3, hRightBorderStart | (hFrontPorchStart << 16));

	write32(kDACVerTim1, vTotal | (vBackPorchStart << 16));
	write32(kDACVerTim2, vTopBorderStart | (vDisplayStart << 16));
	write32(kDACVerTim3, vBottomBorderStart | (vFrontPorchStart << 16));

	uint32 sync = read32(kDACSyncCtrl) | (1u << 3) | (1u << 1);
	bool positiveH = (mode.timing.flags & B_POSITIVE_HSYNC) != 0;
	bool positiveV = (mode.timing.flags & B_POSITIVE_VSYNC) != 0;

	if (positiveH && !positiveV)
		sync &= ~(1u << 3);
	else if (!positiveH && positiveV)
		sync &= ~(1u << 1);
	else if (!positiveH && !positiveV)
		sync &= ~((1u << 3) | (1u << 1));

	write32(kDACSyncCtrl, sync);
}


static status_t
apply_mode(const display_mode& mode)
{
	stop_vtg();
	disable_output();
	disable_vga();
	status_t status = initialise_ramdac(mode);
	if (status != B_OK)
		return status;
	setup_vtg(mode);
	ResetOverlayState();
	enable_output();
	start_vtg();
	return B_OK;
}


static void
update_frame_buffer_config(const display_mode& mode)
{
	gInfo.sharedInfo->frameBufferConfig.frame_buffer = gInfo.sharedInfo->frameBuffer;
	gInfo.sharedInfo->frameBufferConfig.frame_buffer_dma
		= (void*)(addr_t)gInfo.sharedInfo->frameBufferPCI;
	gInfo.sharedInfo->frameBufferConfig.bytes_per_row
		= get_line_length(mode.virtual_width,
			color_space_bits_per_pixel((color_space)mode.space));
}


static void
select_initial_mode_if_needed(void)
{
	SharedInfo& shared = *gInfo.sharedInfo;
	if (shared.currentMode.virtual_width != 0 || gInfo.modeList == NULL)
		return;

	uint32 selectedIndex = 0;
	if (shared.hasBootMode) {
		uint32 requestedBits = shared.bootDepth >= 24 ? 32 : 16;
		for (uint32 i = 0; i < shared.modeCount; i++) {
			const display_mode& mode = gInfo.modeList[i];
			if (mode.timing.h_display != shared.bootWidth
				|| mode.timing.v_display != shared.bootHeight) {
				continue;
			}
			selectedIndex = i;
			if (color_space_bits_per_pixel((color_space)mode.space) == requestedBits)
				break;
		}
	}

	shared.currentMode = gInfo.modeList[selectedIndex];
	shared.preferredMode = gInfo.modeList[selectedIndex];
	shared.dpmsMode = B_DPMS_ON;
	update_frame_buffer_config(shared.currentMode);
}

} // namespace


status_t
CreateModeList(void)
{
	const uint32 baseCount = sizeof(kModePresets) / sizeof(kModePresets[0]);
	display_mode initialModes[baseCount + 1];
	uint32 initialCount = 0;

	for (uint32 i = 0; i < baseCount; i++) {
		initialModes[initialCount].timing.pixel_clock = kModePresets[i].pixelClock;
		initialModes[initialCount].timing.h_display = kModePresets[i].hDisplay;
		initialModes[initialCount].timing.h_sync_start = kModePresets[i].hSyncStart;
		initialModes[initialCount].timing.h_sync_end = kModePresets[i].hSyncEnd;
		initialModes[initialCount].timing.h_total = kModePresets[i].hTotal;
		initialModes[initialCount].timing.v_display = kModePresets[i].vDisplay;
		initialModes[initialCount].timing.v_sync_start = kModePresets[i].vSyncStart;
		initialModes[initialCount].timing.v_sync_end = kModePresets[i].vSyncEnd;
		initialModes[initialCount].timing.v_total = kModePresets[i].vTotal;
		initialModes[initialCount].timing.flags = kModePresets[i].flags;
		initialModes[initialCount].space = B_RGB32;
		initialModes[initialCount].virtual_width = kModePresets[i].hDisplay;
		initialModes[initialCount].virtual_height = kModePresets[i].vDisplay;
		initialModes[initialCount].h_display_start = 0;
		initialModes[initialCount].v_display_start = 0;
		initialModes[initialCount].flags = B_SCROLL | B_DPMS
			| B_PARALLEL_ACCESS | B_SUPPORTS_OVERLAYS;
		if (gInfo.sharedInfo->settings.hardcursor)
			initialModes[initialCount].flags |= B_HARDWARE_CURSOR;
		initialCount++;
	}

	if (gInfo.sharedInfo->hasBootMode) {
		bool duplicate = false;
		for (uint32 i = 0; i < initialCount; i++) {
			if (initialModes[i].timing.h_display == gInfo.sharedInfo->bootWidth
				&& initialModes[i].timing.v_display == gInfo.sharedInfo->bootHeight) {
				duplicate = true;
				break;
			}
		}

		if (!duplicate) {
			display_mode& mode = initialModes[initialCount];
			fill_display_mode(gInfo.sharedInfo->bootWidth,
				gInfo.sharedInfo->bootHeight, &mode);
			compute_display_timing(gInfo.sharedInfo->bootWidth,
				gInfo.sharedInfo->bootHeight, 60.0f, false, &mode.timing);
			initialModes[initialCount].virtual_width = gInfo.sharedInfo->bootWidth;
			initialModes[initialCount].virtual_height = gInfo.sharedInfo->bootHeight;
			initialModes[initialCount].h_display_start = 0;
			initialModes[initialCount].v_display_start = 0;
			initialModes[initialCount].flags = B_SCROLL | B_DPMS
				| B_PARALLEL_ACCESS | B_SUPPORTS_OVERLAYS;
			initialModes[initialCount].space = B_RGB32;
			if (gInfo.sharedInfo->settings.hardcursor)
				initialModes[initialCount].flags |= B_HARDWARE_CURSOR;
			initialCount++;
		}
	}

	gInfo.modeListArea = create_display_modes("kyro modes",
		gInfo.sharedInfo->hasEdid ? &gInfo.sharedInfo->edidInfo : NULL,
		initialModes, initialCount,
		gInfo.sharedInfo->colorSpaces, gInfo.sharedInfo->colorSpaceCount,
		is_mode_usable, &gInfo.modeList, &gInfo.sharedInfo->modeCount);
	if (gInfo.modeListArea < B_OK)
		return gInfo.modeListArea;

	gInfo.sharedInfo->modeArea = gInfo.modeListArea;
	select_initial_mode_if_needed();
	return B_OK;
}


uint32
AccelerantModeCount(void)
{
	return gInfo.sharedInfo->modeCount;
}


status_t
GetModeList(display_mode* modeList)
{
	memcpy(modeList, gInfo.modeList,
		gInfo.sharedInfo->modeCount * sizeof(display_mode));
	return B_OK;
}


status_t
ProposeDisplayMode(display_mode* target, const display_mode* low,
	const display_mode* high)
{
	(void)low;
	(void)high;

	for (uint32 i = 0; i < gInfo.sharedInfo->modeCount; i++) {
		const display_mode& mode = gInfo.modeList[i];

		if (target->timing.h_display == mode.timing.h_display
			&& target->timing.v_display == mode.timing.v_display
			&& target->space == mode.space) {
			*target = mode;
			return B_OK;
		}
	}

	return B_BAD_VALUE;
}


status_t
SetDisplayMode(display_mode* mode)
{
	display_mode selected = *mode;
	if (ProposeDisplayMode(&selected, mode, mode) != B_OK)
		return B_BAD_VALUE;

	uint32 bitsPerPixel = color_space_bits_per_pixel((color_space)selected.space);
	if (!has_enough_frame_buffer(&selected, bitsPerPixel))
		return B_NO_MEMORY;

	status_t status = apply_mode(selected);
	if (status != B_OK)
		return status;

	gInfo.sharedInfo->currentMode = selected;
	if (gInfo.sharedInfo->preferredMode.virtual_width == 0)
		gInfo.sharedInfo->preferredMode = selected;
	gInfo.sharedInfo->dpmsMode = B_DPMS_ON;
	update_frame_buffer_config(selected);
	*mode = selected;
	return B_OK;
}


status_t
GetDisplayMode(display_mode* currentMode)
{
	*currentMode = gInfo.sharedInfo->currentMode;
	return B_OK;
}


status_t
GetFrameBufferConfig(frame_buffer_config* config)
{
	*config = gInfo.sharedInfo->frameBufferConfig;
	return B_OK;
}


status_t
GetPixelClockLimits(display_mode* mode, uint32* low, uint32* high)
{
	uint32 totalPixels = mode->timing.h_total * mode->timing.v_total;
	uint32 lower = totalPixels * 48L / 1000L;
	if (lower > gInfo.sharedInfo->maxPixelClock)
		return B_ERROR;

	if (low != NULL)
		*low = lower;
	if (high != NULL)
		*high = gInfo.sharedInfo->maxPixelClock;
	return B_OK;
}


status_t
MoveDisplay(uint16 horizontalStart, uint16 verticalStart)
{
	if (horizontalStart != 0 || verticalStart != 0)
		return B_UNSUPPORTED;

	gInfo.sharedInfo->currentMode.h_display_start = 0;
	gInfo.sharedInfo->currentMode.v_display_start = 0;
	return B_OK;
}


status_t
GetTimingConstraints(display_timing_constraints* constraints)
{
	if (constraints == NULL)
		return B_BAD_VALUE;

	constraints->h_res = 8;
	constraints->h_sync_min = 8;
	constraints->h_sync_max = 256;
	constraints->h_blank_min = 8;
	constraints->h_blank_max = 1024;
	constraints->v_res = 1;
	constraints->v_sync_min = 1;
	constraints->v_sync_max = 16;
	constraints->v_blank_min = 1;
	constraints->v_blank_max = 256;
	return B_OK;
}


status_t
GetPreferredDisplayMode(display_mode* mode)
{
	*mode = gInfo.sharedInfo->preferredMode.virtual_width != 0
		? gInfo.sharedInfo->preferredMode : gInfo.sharedInfo->currentMode;
	return B_OK;
}


status_t
GetEdidInfo(void* info, size_t size, uint32* version)
{
	if (!gInfo.sharedInfo->hasEdid)
		return B_ERROR;
	if (size < sizeof(edid1_info))
		return B_BUFFER_OVERFLOW;

	memcpy(info, &gInfo.sharedInfo->edidInfo, sizeof(edid1_info));
	if (version != NULL)
		*version = EDID_VERSION_1;
	return B_OK;
}


uint32
kyro_dpms_mode(void)
{
	return gInfo.sharedInfo->dpmsMode;
}


uint32
kyro_dpms_capabilities(void)
{
	return B_DPMS_ON | B_DPMS_OFF;
}


status_t
kyro_set_dpms_mode(uint32 mode)
{
	switch (mode) {
		case B_DPMS_ON:
			enable_output();
			start_vtg();
			break;
		case B_DPMS_OFF:
			disable_output();
			stop_vtg();
			break;
		default:
			return B_BAD_VALUE;
	}

	gInfo.sharedInfo->dpmsMode = mode;
	return B_OK;
}
