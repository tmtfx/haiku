/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef VESA_MODES_H
#define VESA_MODES_H

typedef struct {
    uint16 width;
    uint16 height;
    uint32 refresh;
    uint32 pixel_clock; // in kHz
    uint16 h_sync_start;
    uint16 h_sync_end;
    uint16 h_total;
    uint16 v_sync_start;
    uint16 v_sync_end;
    uint16 v_total;
    uint32 flags;
} vesa_timing_t;

static const vesa_timing_t vesa_dmt_table[] = {
    // 640x480 @ 60Hz
    { 640, 480, 60, 25175, 656, 752, 800, 490, 492, 525, 0 },
    // 800x600 @ 60Hz
    { 800, 600, 60, 40000, 840, 968, 1056, 601, 605, 628, B_POSITIVE_HSYNC | B_POSITIVE_VSYNC },
    // 1024x768 @ 60Hz
    { 1024, 768, 60, 65000, 1048, 1184, 1344, 771, 777, 806, 0 },
    // 1280x1024 @ 60Hz (Il tuo target attuale)
    { 1280, 1024, 60, 108000, 1328, 1440, 1688, 1025, 1028, 1066, B_POSITIVE_HSYNC | B_POSITIVE_VSYNC },
    // 1600x1200 @ 60Hz
    { 1600, 1200, 60, 162000, 1664, 1856, 2160, 1201, 1204, 1250, B_POSITIVE_HSYNC | B_POSITIVE_VSYNC },
    // Terminatore
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }
};

#endif
