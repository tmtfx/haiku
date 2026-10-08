/*
 * Copyright 2026, Fabio Tomat.
 * Copyright 2026, GitHub Copilot.
 *
 * Distributed under the terms of the GNU General Public License version 2.
 */


#include "accelerant.h"


extern "C" void*
get_accelerant_hook(uint32 feature, void* data)
{
	(void)data;

	switch (feature) {
		case B_INIT_ACCELERANT:
			return (void*)InitAccelerant;
		case B_UNINIT_ACCELERANT:
			return (void*)UninitAccelerant;
		case B_CLONE_ACCELERANT:
			return (void*)CloneAccelerant;
		case B_ACCELERANT_CLONE_INFO_SIZE:
			return (void*)AccelerantCloneInfoSize;
		case B_GET_ACCELERANT_CLONE_INFO:
			return (void*)GetAccelerantCloneInfo;
		case B_GET_ACCELERANT_DEVICE_INFO:
			return (void*)GetAccelerantDeviceInfo;
		case B_ACCELERANT_RETRACE_SEMAPHORE:
			return (void*)AccelerantRetraceSemaphore;

		case B_ACCELERANT_MODE_COUNT:
			return (void*)AccelerantModeCount;
		case B_GET_MODE_LIST:
			return (void*)GetModeList;
		case B_PROPOSE_DISPLAY_MODE:
			return (void*)ProposeDisplayMode;
		case B_SET_DISPLAY_MODE:
			return (void*)SetDisplayMode;
		case B_GET_DISPLAY_MODE:
			return (void*)GetDisplayMode;
		case B_GET_FRAME_BUFFER_CONFIG:
			return (void*)GetFrameBufferConfig;
		case B_GET_PIXEL_CLOCK_LIMITS:
			return (void*)GetPixelClockLimits;
		case B_MOVE_DISPLAY:
			return (void*)MoveDisplay;
		case B_GET_TIMING_CONSTRAINTS:
			return (void*)GetTimingConstraints;
		case B_GET_PREFERRED_DISPLAY_MODE:
			return (void*)GetPreferredDisplayMode;
		case B_GET_EDID_INFO:
			return (void*)GetEdidInfo;
		case B_MOVE_CURSOR:
			return NULL;
		case B_SET_CURSOR_SHAPE:
			return NULL;
		case B_SHOW_CURSOR:
			return NULL;
		case B_SET_CURSOR_BITMAP:
			return NULL;
		case B_GET_CURSOR_BITS:
			return NULL;

		case B_DPMS_MODE:
			return (void*)kyro_dpms_mode;
		case B_DPMS_CAPABILITIES:
			return (void*)kyro_dpms_capabilities;
		case B_SET_DPMS_MODE:
			return (void*)kyro_set_dpms_mode;
		case B_OVERLAY_COUNT:
			return (void*)kyro_overlay_count;
		case B_OVERLAY_SUPPORTED_SPACES:
			return (void*)kyro_overlay_supported_spaces;
		case B_OVERLAY_SUPPORTED_FEATURES:
			return (void*)kyro_overlay_supported_features;
		case B_ALLOCATE_OVERLAY_BUFFER:
			return (void*)kyro_allocate_overlay_buffer;
		case B_RELEASE_OVERLAY_BUFFER:
			return (void*)kyro_release_overlay_buffer;
		case B_GET_OVERLAY_CONSTRAINTS:
			return (void*)kyro_get_overlay_constraints;
		case B_ALLOCATE_OVERLAY:
			return (void*)kyro_allocate_overlay;
		case B_RELEASE_OVERLAY:
			return (void*)kyro_release_overlay;
		case B_CONFIGURE_OVERLAY:
			return (void*)kyro_configure_overlay;
	}

	return NULL;
}
