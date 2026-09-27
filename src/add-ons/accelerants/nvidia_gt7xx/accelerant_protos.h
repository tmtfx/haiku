/*
 * Copyright 2005-2008, Axel Dörfler, axeld@pinc-software.de.
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#ifndef _ACCELERANT_PROTOS_H
#define _ACCELERANT_PROTOS_H


#include <Accelerant.h>
#include "video_overlay.h"


#ifdef __cplusplus
extern "C" {
#endif

// general
status_t nvidia_gt7xx_init_accelerant(int fd);
ssize_t nvidia_gt7xx_accelerant_clone_info_size(void);
void nvidia_gt7xx_get_accelerant_clone_info(void *data);
status_t nvidia_gt7xx_clone_accelerant(void *data);
void nvidia_gt7xx_uninit_accelerant(void);
status_t nvidia_gt7xx_get_accelerant_device_info(accelerant_device_info *adi);
sem_id nvidia_gt7xx_accelerant_retrace_semaphore(void);

// modes & constraints
uint32 nvidia_gt7xx_accelerant_mode_count(void);
status_t nvidia_gt7xx_get_mode_list(display_mode *dm);
status_t nvidia_gt7xx_propose_display_mode(display_mode *target,
	const display_mode *low, const display_mode *high);
status_t nvidia_gt7xx_set_display_mode(display_mode *modeToSet);
status_t nvidia_gt7xx_get_display_mode(display_mode *currentMode);
status_t nvidia_gt7xx_get_edid_info(void *info, size_t size, uint32 *_version);
status_t nvidia_gt7xx_get_frame_buffer_config(frame_buffer_config *config);
status_t nvidia_gt7xx_get_pixel_clock_limits(display_mode *dm, uint32 *low,
	uint32 *high);
status_t nvidia_gt7xx_move_display(uint16 hDisplayStart, uint16 vDisplayStart);
status_t nvidia_gt7xx_get_timing_constraints(display_timing_constraints *dtc);
void nvidia_gt7xx_set_indexed_colors(uint count, uint8 first, uint8 *colorData,
	uint32 flags);

// DPMS
uint32 nvidia_gt7xx_dpms_capabilities(void);
uint32 nvidia_gt7xx_dpms_mode(void);
status_t nvidia_gt7xx_set_dpms_mode(uint32 dpmsFlags);

// cursor
status_t nvidia_gt7xx_set_cursor_shape(uint16 width, uint16 height, uint16 hotX,
	uint16 hotY, const uint8 *andMask, const uint8 *xorMask);
status_t nvidia_gt7xx_set_cursor_bitmap(uint16 width, uint16 height, uint16 hotX,
	uint16 hotY, color_space colorSpace, uint16 bytesPerRow,
	const uint8* bitmapData);
void nvidia_gt7xx_move_cursor(uint16 x, uint16 y);
void nvidia_gt7xx_show_cursor(bool is_visible);

// 2D acceleration
void nvidia_gt7xx_screen_to_screen_blit(engine_token *et, blit_params *list,
	uint32 count);
void nvidia_gt7xx_fill_rectangle(engine_token *et, uint32 color, fill_rect_params *list,
	uint32 count);
void nvidia_gt7xx_invert_rectangle(engine_token *et, fill_rect_params *list,
	uint32 count);
void nvidia_gt7xx_fill_span(engine_token *et, uint32 color, uint16 *list, uint32 count);

// overlay
uint32 nvidia_gt7xx_overlay_count(const display_mode *dm);
const uint32 *nvidia_gt7xx_overlay_supported_spaces(const display_mode *dm);
uint32 nvidia_gt7xx_overlay_supported_features(uint32 a_color_space);
const overlay_buffer *nvidia_gt7xx_allocate_overlay_buffer(color_space cs, uint16 width,
	uint16 height);
status_t nvidia_gt7xx_release_overlay_buffer(const overlay_buffer *ob);
status_t nvidia_gt7xx_get_overlay_constraints(const display_mode *dm,
	const overlay_buffer *ob, overlay_constraints *oc);
overlay_token nvidia_gt7xx_allocate_overlay(void);
status_t nvidia_gt7xx_release_overlay(overlay_token ot);
status_t nvidia_gt7xx_configure_overlay(overlay_token ot, const overlay_buffer *ob,
	const overlay_window *ow, const overlay_view *ov);

#ifdef __cplusplus
}
#endif

#endif	/* _ACCELERANT_PROTOS_H */
