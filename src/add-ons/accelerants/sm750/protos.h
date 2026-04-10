/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef SM750_PROTOS_H
#define SM750_PROTOS_H

#include <Accelerant.h> // Serve per il tipo display_mode e status_t

status_t sm750_set_display_mode(display_mode *mode);
status_t sm750_get_display_mode(display_mode *mode);
status_t sm750_propose_display_mode(display_mode *target, 
                                    const display_mode *low, 
                                    const display_mode *high);
status_t sm750_get_frame_buffer_config(frame_buffer_config *config);
status_t sm750_set_cursor_shape(uint16 width, uint16 height, uint16 hot_x, uint16 hot_y, const uint8 *and_mask, const uint8 *xor_mask);
void sm750_move_cursor(uint16 x, uint16 y);
void sm750_show_cursor(bool is_visible);


#endif // SM750_PROTOS_H
