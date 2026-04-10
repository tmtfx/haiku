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

    uint16 x_pos = x - si->cursor.hot_x;
    uint16 y_pos = y - si->cursor.hot_y;
    
    si->cursor.x = x;
    si->cursor.y = y;

    DISP_W(CRT_CUR_POS, (x_pos << 16) | y_pos);
}

void
sm750_show_cursor(bool is_visible)
{
    vuint32 *regs = gInfo->regs;
    uint32 ctrl = DISP_R(CRT_CUR_CTRL);
    
    if (is_visible)
        ctrl |= (1 << 31); 
    else
        ctrl &= ~(1 << 31);
        
    DISP_W(CRT_CUR_CTRL, ctrl);
}

status_t
sm750_set_cursor_shape(uint16 width, uint16 height, uint16 hot_x, uint16 hot_y,
    const uint8 *and_mask, const uint8 *xor_mask)
{
    shared_info *si = gInfo->si;
    vuint32 *regs = gInfo->regs;

    // 1. Destinazione: fine della VRAM
    uint32 cursorOffset = si->card_info.mem_size - 1024;
    uint8 *dest = (uint8 *)si->framebuffer + cursorOffset;

    // 2. Pulizia (Trasparente)
    memset(dest, 0, 1024);

    // 3. Bit-packing 2bpp per SM750
    for (int y = 0; y < height && y < 64; y++) {
        uint8 *line_dest = dest + (y * 16); 
        for (int x = 0; x < width && x < 64; x++) {
            int byte_idx = (y * ((width + 7) / 8)) + (x / 8);
            int bit_idx = 7 - (x % 8);
            
            bool a = (and_mask[byte_idx] >> bit_idx) & 1;
            bool x = (xor_mask[byte_idx] >> bit_idx) & 1;

            // Formato standard SMI: and=0, xor=0 -> Color0; and=1, xor=1 -> Invert...
            // Noi semplifichiamo per Nero (10) e Bianco (11), resto trasparente (00)
            if (!a) { // Pixel opaco
                uint8 val = x ? 0x03 : 0x02; // 11 (Bianco) o 10 (Nero)
                line_dest[x / 4] |= (val << (2 * (3 - (x % 4))));
            }
        }
    }

    // 4. Update Registri tramite Macro
    DISP_W(CRT_CUR_ADDR, cursorOffset | (1 << 31)); // Bit 31 = External VRAM
    DISP_W(CRT_CUR_COLOR12, (0xFFFF << 16) | 0x0000); // Bianco (16 bit) | Nero (16 bit)
    DISP_W(CRT_CUR_COLOR3, 0x0000FFFF);

    // Salviamo hotspot per move_cursor
    si->cursor.hot_x = hot_x;
    si->cursor.hot_y = hot_y;
    si->cursor.width = width;
    si->cursor.height = height;

    return B_OK;
}
