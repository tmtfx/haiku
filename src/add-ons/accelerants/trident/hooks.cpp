/*
 * Copyright 2026, Gemini CLI. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Gemini CLI <gemini-cli@google.com>
 */


#include "accel.h"


extern "C" void* 
get_accelerant_hook(uint32 feature, void* data)
{
	SharedInfo& si = *gInfo.sharedInfo;
	(void)data;

	switch (feature) {
		// General
		case B_INIT_ACCELERANT:				return (void*)InitAccelerant;
		case B_UNINIT_ACCELERANT:			return (void*)UninitAccelerant;
		case B_CLONE_ACCELERANT:			return (void*)CloneAccelerant;
		case B_ACCELERANT_CLONE_INFO_SIZE:	return (void*)AccelerantCloneInfoSize;
		case B_GET_ACCELERANT_CLONE_INFO:	return (void*)GetAccelerantCloneInfo;
		case B_GET_ACCELERANT_DEVICE_INFO:	return (void*)GetAccelerantDeviceInfo;
		case B_ACCELERANT_RETRACE_SEMAPHORE: return (void*)AccelerantRetraceSemaphore;

		// Mode Configuration
		case B_ACCELERANT_MODE_COUNT:	return (void*)AccelerantModeCount;
		case B_GET_MODE_LIST:			return (void*)GetModeList;
		case B_PROPOSE_DISPLAY_MODE:	return (void*)ProposeDisplayMode;
		case B_SET_DISPLAY_MODE:		return (void*)SetDisplayMode;
		case B_GET_DISPLAY_MODE:		return (void*)GetDisplayMode;
		case B_SET_INDEXED_COLORS:		return (void*)trident_set_indexed_colors;
#ifdef __HAIKU__
		case B_GET_PREFERRED_DISPLAY_MODE: return (void*)GetPreferredDisplayMode;
		case B_GET_EDID_INFO:			return (void*)GetEdidInfo;
#endif
		case B_GET_FRAME_BUFFER_CONFIG:	return (void*)GetFrameBufferConfig;
		case B_GET_PIXEL_CLOCK_LIMITS:	return (void*)GetPixelClockLimits;
		case B_MOVE_DISPLAY:			return (void*)MoveDisplay;
		case B_GET_TIMING_CONSTRAINTS:	return (void*)GetTimingConstraints;

		// Cursor
		case B_SET_CURSOR_SHAPE:		return (void*)(si.bDisableHdwCursor ? NULL : SetCursorShape);
		case B_MOVE_CURSOR:				return (void*)(si.bDisableHdwCursor ? NULL : MoveCursor);
		case B_SHOW_CURSOR:				return (void*)(si.bDisableHdwCursor ? NULL : ShowCursor);
		case B_SET_CURSOR_BITMAP:		return (void*)(si.bDisableHdwCursor ? NULL : SetCursorBitmap);
		case B_GET_CURSOR_BITS:			return (void*)(si.bDisableHdwCursor ? NULL : GetCursorBits);

		// Engine Management
		case B_ACCELERANT_ENGINE_COUNT:	return (void*)AccelerantEngineCount;
		case B_ACQUIRE_ENGINE:			return (void*)AcquireEngine;
		case B_RELEASE_ENGINE:			return (void*)ReleaseEngine;
		case B_WAIT_ENGINE_IDLE:		return (void*)WaitEngineIdle;
		case B_GET_SYNC_TOKEN:			return (void*)GetSyncToken;
		case B_SYNC_TO_TOKEN:			return (void*)SyncToToken;

		// Overlay
		case B_OVERLAY_COUNT:				return (void*)trident_overlay_count;
		case B_OVERLAY_SUPPORTED_SPACES:	return (void*)trident_overlay_supported_spaces;
		case B_OVERLAY_SUPPORTED_FEATURES:	return (void*)trident_overlay_supported_features;
		case B_ALLOCATE_OVERLAY_BUFFER:		return (void*)trident_allocate_overlay_buffer;
		case B_RELEASE_OVERLAY_BUFFER:		return (void*)trident_release_overlay_buffer;
		case B_GET_OVERLAY_CONSTRAINTS:		return (void*)trident_get_overlay_constraints;
		case B_ALLOCATE_OVERLAY:			return (void*)trident_allocate_overlay;
		case B_RELEASE_OVERLAY:				return (void*)trident_release_overlay;
		case B_CONFIGURE_OVERLAY:			return (void*)trident_configure_overlay;

		// DPMS
		case B_DPMS_CAPABILITIES:			return (void*)trident_dpms_capabilities;
		case B_DPMS_MODE:					return (void*)trident_dpms_mode;
		case B_SET_DPMS_MODE:				return (void*)trident_set_dpms_mode;
	}

	return NULL;
}
