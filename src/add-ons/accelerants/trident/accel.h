/*
 * Copyright 2026, Gemini CLI. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Gemini CLI <gemini-cli@google.com>
 */
#ifndef TRIDENT_ACCEL_H
#define TRIDENT_ACCEL_H


#include "DriverInterface.h"


#undef TRACE

#ifdef ENABLE_DEBUG_TRACE
extern "C" void  _sPrintf(const char* format, ...);
#	define TRACE(x...) _sPrintf("Trident: " x)
#else
#	define TRACE(x...) ;
#endif


struct AccelerantInfo {
	int			 deviceFileDesc;

	SharedInfo*	 sharedInfo;
	area_id		 sharedInfoArea;

	uint8*		 regs;
	area_id		 regsArea;

	display_mode* modeList;
	area_id		 modeListArea;

	bool	bAccelerantIsClone;
};

extern AccelerantInfo gInfo;


#if defined(__cplusplus)
extern "C" {
#endif

// General Hook Declarations
status_t InitAccelerant(int fd);
ssize_t  AccelerantCloneInfoSize(void);
void	 GetAccelerantCloneInfo(void* data);
status_t CloneAccelerant(void* data);
void	 UninitAccelerant(void);
status_t GetAccelerantDeviceInfo(accelerant_device_info* adi);
sem_id	 AccelerantRetraceSemaphore(void);

// Mode Hook Declarations
uint32	 AccelerantModeCount(void);
status_t GetModeList(display_mode* dm);
status_t ProposeDisplayMode(display_mode* target, const display_mode* low, const display_mode* high);
status_t SetDisplayMode(display_mode* mode_to_set);
status_t GetDisplayMode(display_mode* current_mode);
status_t GetFrameBufferConfig(frame_buffer_config* a_frame_buffer);
status_t GetPixelClockLimits(display_mode* dm, uint32* low, uint32* high);
status_t MoveDisplay(uint16 h_display_start, uint16 v_display_start);
status_t GetTimingConstraints(display_timing_constraints* dtc);
status_t GetPreferredDisplayMode(display_mode* preferredMode);
status_t GetEdidInfo(void* info, size_t size, uint32* _version);

// Engine Hook Declarations
uint32   AccelerantEngineCount(void);
status_t AcquireEngine(uint32 capabilities, uint32 max_wait, sync_token* st, engine_token** et);
status_t ReleaseEngine(engine_token* et, sync_token* st);
void	 WaitEngineIdle(void);
status_t GetSyncToken(engine_token* et, sync_token* st);
status_t SyncToToken(sync_token* st);

// Cursor Hook Declarations
status_t SetCursorShape(uint16 width, uint16 height, uint16 hot_x, uint16 hot_y,
						uint8* andMask, uint8* xorMask);
status_t SetCursorBitmap(uint16 width, uint16 height, uint16 hot_x, uint16 hot_y,
						color_space colorSpace, uint16 bytesPerRow, const uint8* bitmapData);
uint32   GetCursorBits(void);
void	 MoveCursor(uint16 x, uint16 y);
void	 ShowCursor(bool bShow);

#if defined(__cplusplus)
}
#endif


#endif	// TRIDENT_ACCEL_H
