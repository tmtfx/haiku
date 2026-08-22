/*
 * Copyright 2026, Haiku contributors.
 * Distributed under the terms of the MIT License.
 */

#pragma once

#include <Accelerant.h>
#include <video_overlay.h>


#ifdef __cplusplus
extern "C" {
#endif

void*		get_accelerant_hook(uint32 feature, void* data);

status_t	intel_arc_init_accelerant(int device);
ssize_t		intel_arc_accelerant_clone_info_size(void);
void		intel_arc_get_accelerant_clone_info(void* info);
status_t	intel_arc_clone_accelerant(void* info);
void		intel_arc_uninit_accelerant(void);
status_t	intel_arc_get_accelerant_device_info(accelerant_device_info* info);
sem_id		intel_arc_accelerant_retrace_semaphore(void);
uint32		intel_arc_dpms_capabilities(void);
uint32		intel_arc_dpms_mode(void);
status_t	intel_arc_set_dpms_mode(uint32 mode);
uint32		intel_arc_accelerant_engine_count(void);
status_t	intel_arc_acquire_engine(uint32 capabilities, uint32 maxWait,
				sync_token* syncToken, engine_token** engineToken);
status_t	intel_arc_release_engine(engine_token* engineToken,
				sync_token* syncToken);
void		intel_arc_wait_engine_idle(void);
status_t	intel_arc_get_sync_token(engine_token* engineToken,
				sync_token* syncToken);
status_t	intel_arc_sync_to_token(sync_token* syncToken);

uint32		intel_arc_accelerant_mode_count(void);
status_t	intel_arc_get_mode_list(display_mode* modeList);
status_t	intel_arc_propose_display_mode(display_mode* target,
				display_mode* low, display_mode* high);
status_t	intel_arc_get_preferred_mode(display_mode* mode);
status_t	intel_arc_set_display_mode(display_mode* mode);
status_t	intel_arc_get_display_mode(display_mode* mode);
status_t	intel_arc_get_edid_info(void* info, size_t size, uint32* version);
status_t	intel_arc_get_frame_buffer_config(frame_buffer_config* config);
status_t	intel_arc_get_pixel_clock_limits(display_mode* mode, uint32* low,
				uint32* high);
uint32		intel_arc_overlay_count(const display_mode* mode);
const uint32*	intel_arc_overlay_supported_spaces(const display_mode* mode);
uint32		intel_arc_overlay_supported_features(uint32 colorSpace);
const overlay_buffer* intel_arc_allocate_overlay_buffer(color_space colorSpace,
				uint16 width, uint16 height);
status_t	intel_arc_release_overlay_buffer(const overlay_buffer* buffer);
status_t	intel_arc_get_overlay_constraints(const display_mode* mode,
				const overlay_buffer* buffer, overlay_constraints* constraints);
overlay_token	intel_arc_allocate_overlay(void);
status_t	intel_arc_release_overlay(overlay_token token);
status_t	intel_arc_configure_overlay(overlay_token token,
				const overlay_buffer* buffer, const overlay_window* window,
				const overlay_view* view);

#ifdef __cplusplus
}
#endif
