/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef SM750_PROTOS_H
#define SM750_PROTOS_H

#include <Accelerant.h> // Serve per il tipo display_mode e status_t
#ifdef __cplusplus
extern "C" {
#endif
status_t sm750_init_accelerant(int fd);
void sm750_uninit_accelerant(void);

status_t sm750_set_display_mode(display_mode *mode);
status_t sm750_get_display_mode(display_mode *mode);
status_t sm750_propose_display_mode(display_mode *target, 
                                    const display_mode *low, 
                                    const display_mode *high);
uint32 sm750_accelerant_mode_count(void);
status_t sm750_get_preferred_mode(display_mode* mode);
status_t create_mode_list_from_edid(uint8* raw_buffer);
status_t sm750_get_mode_list(display_mode* dm);
status_t sm750_get_frame_buffer_config(frame_buffer_config *config);
status_t sm750_set_cursor_shape(uint16 width, uint16 height, uint16 hot_x, uint16 hot_y, const uint8 *and_mask, const uint8 *xor_mask);
void sm750_move_cursor(uint16 x, uint16 y);
void sm750_show_cursor(bool is_visible);
status_t sm750_set_cursor_bitmap(uint16 width, uint16 height, uint16 hotX, uint16 hotY, color_space colorSpace, uint16 bytesPerRow, const uint8* bitmapData);
status_t sm750_get_edid_info(void* info, size_t size, uint32* _version);

status_t sm750_read_edid(uint8* buffer);

void sm750_program_pll(uint32 target_khz, bool is_panel);
status_t sm750_move_display_area(uint16 h_display_start, uint16 v_display_start);

uint32 sm750_accelerant_clone_info_size(void);
void sm750_get_accelerant_clone_info(void *data);
status_t sm750_clone_accelerant(void* info);
status_t sm750_get_accelerant_device_info(accelerant_device_info *adi);
status_t sm750_get_pixel_clock_limits(display_mode *dm, uint32 *low, uint32 *high);
uint32 sm750_accelerant_engine_count(void);
status_t sm750_acquire_engine(uint32 capabilities, uint32 max_priority, sync_token *st, engine_token **et);
void sm750_release_engine(engine_token *et, sync_token *st);
void sm750_wait_engine_idle(void);
status_t sm750_get_sync_token(engine_token *et, sync_token *st);
status_t sm750_sync_to_token(sync_token *st);
status_t sm750_set_fb_addr(uint32 offset, bool is_panel);
status_t sm750_set_indexed_colors(uint32 count, uint8 first, uint8 *colors, uint32 flags);
status_t sm750_get_timing_constraints(display_timing_constraints *dtc);
uint32 sm750_dpms_mode(void);
uint32 sm750_dpms_capabilities(void);
status_t sm750_set_dpms_mode(uint32 mode);

int32 sm750_vblank_service_thread(void *arg);
sem_id sm750_retrace_semaphore(void);

uint32 sm750_overlay_count(const display_mode *dm);
const uint32 *sm750_overlay_supported_spaces(const display_mode *dm);
void sm750_get_overlay_constraints(const display_mode *dm, const overlay_buffer *ob, overlay_constraints *oc);
overlay_buffer *sm750_allocate_overlay_buffer(color_space cs, uint16 width, uint16 height);
status_t sm750_release_overlay_buffer(const overlay_buffer *buffer);
status_t sm750_allocate_overlay(overlay_token *token);
status_t sm750_release_overlay(overlay_token token);
status_t sm750_configure_overlay_api(overlay_token token, const overlay_buffer *buffer, const overlay_window *window, const overlay_view *view);
uint32 sm750_overlay_supported_features(uint32 space);

void sm750_screen_to_screen_blit(engine_token *et, blit_params *p, uint32 count);
void sm750_fill_rectangle(engine_token *et, uint32 color, fill_rect_params *params, uint32 count);
void sm750_init_2d_engine(display_mode *mode);
void sm750_fill_span(engine_token *et, uint32 color, uint16 *spans, uint32 count);
void sm750_invert_rectangle(engine_token *et, fill_rect_params *list, uint32 count);

#ifdef __cplusplus
}
#endif
#endif // SM750_PROTOS_H
