/*
 * Copyright 2018, Your Name <your@email.address>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include <string.h>
#include "sm750_macros.h"
#include "DriverInterface.h"
#include "protos.h"

extern accelerant_info *gInfo;

void
sm750_move_cursor(uint16 x, uint16 y)
{
    shared_info *si = gInfo->si;
    vuint32 *regs = gInfo->regs;

    uint32 x_pos = (x - si->cursor.hot_x) & 0x07FF;
    uint32 y_pos = (y - si->cursor.hot_y) & 0x07FF;
    
    si->cursor.x = x;
    si->cursor.y = y;

    if (si->card_info.is_panel)
        SM750_WREG32(SM750_DISP_PANEL_CUR_POS, (x_pos << 16) | y_pos);
    else
        SM750_WREG32(SM750_DISP_CRT_CUR_POS, (x_pos << 16) | y_pos);
}

void
sm750_show_cursor(bool is_visible)
{
    shared_info *si = gInfo->si;
    vuint32 *regs = gInfo->regs;
    
    uint32 reg = si->card_info.is_panel ? SM750_DISP_PANEL_CUR_ADDR : SM750_DISP_CRT_CUR_ADDR;
    
    uint32 ctrl = SM750_REG32(reg);
    
    if (is_visible)
        ctrl |= (1 << 31); // Enable bit
    else
        ctrl &= ~(1 << 31);
        
    SM750_WREG32(reg, ctrl);
}

status_t
sm750_set_cursor_shape(uint16 width, uint16 height, uint16 hot_x, uint16 hot_y,
    const uint8 *and_mask, const uint8 *xor_mask)
{
    shared_info *si = gInfo->si;
    vuint32 *regs = gInfo->regs;
    
    // 1. Destinazione in VRAM (ultimi 2KB)
    uint32 cursorOffset = si->card_info.mem_size - 2048;
    uint8 *dest = (uint8 *)si->framebuffer + cursorOffset;

    memset(dest, 0, 1024);

    // 2. Bit-packing (rimane invariato, è il formato hardware SMI)
    for (int y = 0; y < height && y < 64; y++) {
        uint8 *line_dest = dest + (y * 16); 
        for (int x = 0; x < width && x < 64; x++) {
            int byte_idx = (y * ((width + 7) / 8)) + (x / 8);
            int bit_idx = 7 - (x % 8);
            
            bool a = (and_mask[byte_idx] >> bit_idx) & 1;
            bool x_m = (xor_mask[byte_idx] >> bit_idx) & 1;

            if (!a) { 
                uint8 val = x_m ? 0x03 : 0x02; 
                line_dest[x / 4] |= (val << (2 * (3 - (x % 4))));
            }
        }
    }

    // 3. Update Registri in base all'uscita attiva
    if (si->card_info.is_panel) {
        SM750_WREG32(SM750_DISP_PANEL_CUR_ADDR, (cursorOffset & 0x03FFFFFF) | (1 << 31));
        SM750_WREG32(SM750_DISP_PANEL_CUR_COLOR12, (0xFFFF << 16) | 0x0000);
        SM750_WREG32(SM750_DISP_PANEL_CUR_COLOR3, 0x0000FFFF);
    } else {
        SM750_WREG32(SM750_DISP_CRT_CUR_ADDR, (cursorOffset & 0x03FFFFFF) | (1 << 31));
        SM750_WREG32(SM750_DISP_CRT_CUR_COLOR12, (0xFFFF << 16) | 0x0000);
        SM750_WREG32(SM750_DISP_CRT_CUR_COLOR3, 0x0000FFFF);
    }

    si->cursor.hot_x = hot_x;
    si->cursor.hot_y = hot_y;
    return B_OK;
}
