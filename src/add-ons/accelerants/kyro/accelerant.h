/*
 * Copyright 2026, Fabio Tomat.
 * Copyright 2026, GitHub Copilot.
 *
 * Distributed under the terms of the GNU General Public License version 2.
 */
#ifndef KYRO_ACCELERANT_H
#define KYRO_ACCELERANT_H


#include "DriverInterface.h"


struct AccelerantInfo {
	int				deviceFile;
	bool			isClone;

	area_id			sharedInfoArea;
	SharedInfo*		sharedInfo;

	area_id			regsArea;
	vuint8*			regs;

	area_id			frameBufferArea;
	uint8*			frameBuffer;

	area_id			modeListArea;
	display_mode*	modeList;
};


extern AccelerantInfo gInfo;

status_t CreateModeList(void);
status_t ProposeDisplayMode(display_mode* target, const display_mode* low,
	const display_mode* high);
status_t SetDisplayMode(display_mode* mode);
status_t GetDisplayMode(display_mode* currentMode);
status_t GetFrameBufferConfig(frame_buffer_config* config);
uint32 AccelerantModeCount(void);
status_t GetModeList(display_mode* modeList);
status_t GetPixelClockLimits(display_mode* mode, uint32* low, uint32* high);
status_t MoveDisplay(uint16 horizontalStart, uint16 verticalStart);
status_t GetTimingConstraints(display_timing_constraints* constraints);
status_t GetPreferredDisplayMode(display_mode* mode);
status_t GetEdidInfo(void* info, size_t size, uint32* version);
uint32 kyro_dpms_mode(void);
uint32 kyro_dpms_capabilities(void);
status_t kyro_set_dpms_mode(uint32 mode);
uint32 GetCursorBits(void);
status_t SetCursorShape(uint16 width, uint16 height, uint16 hotX, uint16 hotY,
	uint8* andMask, uint8* xorMask);
status_t SetCursorBitmap(uint16 width, uint16 height, uint16 hotX, uint16 hotY,
	color_space colorSpace, uint16 bytesPerRow, const uint8* bitmapData);
void MoveCursor(uint16 x, uint16 y);
void ShowCursor(bool show);
uint32 kyro_overlay_count(const display_mode* mode);
const uint32* kyro_overlay_supported_spaces(const display_mode* mode);
uint32 kyro_overlay_supported_features(uint32 colorSpace);
const overlay_buffer* kyro_allocate_overlay_buffer(color_space space,
	uint16 width, uint16 height);
status_t kyro_release_overlay_buffer(const overlay_buffer* buffer);
status_t kyro_get_overlay_constraints(const display_mode* mode,
	const overlay_buffer* buffer, overlay_constraints* constraints);
overlay_token kyro_allocate_overlay(void);
status_t kyro_release_overlay(overlay_token token);
status_t kyro_configure_overlay(overlay_token token, const overlay_buffer* buffer,
	const overlay_window* window, const overlay_view* view);
void ResetOverlayState(void);

status_t InitAccelerant(int fileDesc);
ssize_t AccelerantCloneInfoSize(void);
void GetAccelerantCloneInfo(void* data);
status_t CloneAccelerant(void* data);
void UninitAccelerant(void);
status_t GetAccelerantDeviceInfo(accelerant_device_info* info);
sem_id AccelerantRetraceSemaphore(void);


#endif	// KYRO_ACCELERANT_H
