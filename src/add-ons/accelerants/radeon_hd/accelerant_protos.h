/*
 * Copyright 2006-2011, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 *		Alexander von Gluck, kallisti5@unixzen.com
 */
#ifndef ACCELERANT_PROTOS_H
#define ACCELERANT_PROTOS_H


#include <Accelerant.h>

#include "video_overlay.h"


#ifdef __cplusplus
extern "C" {
#endif


void spin(bigtime_t delay);

// general
status_t radeon_init_accelerant(int fd);
void radeon_uninit_accelerant(void);
status_t radeon_get_accelerant_device_info(accelerant_device_info* di);

// modes & constraints
uint32 radeon_accelerant_mode_count(void);
status_t radeon_get_mode_list(display_mode* dm);
status_t radeon_set_display_mode(display_mode* mode);
status_t radeon_get_display_mode(display_mode* currentMode);
status_t radeon_get_preferred_mode(display_mode* preferredMode);
status_t radeon_get_frame_buffer_config(frame_buffer_config* config);
status_t radeon_get_pixel_clock_limits(display_mode* mode,
	uint32* low, uint32* high);
status_t radeon_get_edid_info(void* info, size_t size, uint32* edid_version);

//brightness
status_t radeon_set_brightness(float brightness);
status_t radeon_get_brightness(float* brightness);

// accelerant engine
uint32 radeon_accelerant_engine_count(void);
status_t radeon_acquire_engine(uint32 capabilities, uint32 maxWait,
	sync_token* syncToken, engine_token** _engineToken);
status_t radeon_release_engine(engine_token* engineToken,
	sync_token* syncToken);

// cursor
status_t radeon_set_cursor_shape(uint16 width, uint16 height, uint16 hotX, uint16 hotY,
	uint8* andMask, uint8* xorMask);
status_t radeon_set_cursor_bitmap(uint16 width, uint16 height, uint16 hotX, uint16 hotY,
	color_space colorSpace, uint16 bytesPerRow, const uint8* bitmapData);
uint32 radeon_get_cursor_bits(void);
void radeon_move_cursor(uint16 x, uint16 y);
void radeon_show_cursor(bool isVisible);


#ifdef __cplusplus
}
#endif

#endif	/* ACCELERANT_PROTOS_H */
