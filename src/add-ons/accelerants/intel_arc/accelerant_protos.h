/*
 * Copyright 2026, Haiku contributors.
 * Distributed under the terms of the MIT License.
 */

#pragma once

#include <Accelerant.h>


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

#ifdef __cplusplus
}
#endif
