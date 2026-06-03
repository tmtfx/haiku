/*
 * Copyright 2006-2008, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *      Axel Doerfler, axeld@pinc-software.de
 */


#include "accelerant_protos.h"
#include "accelerant.h"

#include <string.h>


struct cursor_registers {
    uint32 control;
    uint32 base;
    uint32 position;
    uint32 size;
    uint32 palette;
};

static cursor_registers sCursorRegs;
static bool sCursorRegsInitialized = false;
static pipe_index sCursorPipe = INTEL_PIPE_ANY;


static addr_t
pipe_offset(pipe_index pipe)
{
    switch (pipe) {
        case INTEL_PIPE_B:
            return 0x1000;
        case INTEL_PIPE_C:
            return 0x2000;
        case INTEL_PIPE_D:
            return 0xf000;
        case INTEL_PIPE_A:
        case INTEL_PIPE_ANY:
        default:
            return 0;
    }
}


static pipe_index
active_pipe()
{
    // Cursor registers are per-pipe on modern hardware. Calling Port::IsConnected()
    // on every cursor move would be very expensive (EDID/I2C probing), so prefer a
    // register-based heuristic.

    // Fast path: use the enabled primary plane as an indicator.
    if ((read32(INTEL_DISPLAY_A_CONTROL) & DISPLAY_CONTROL_ENABLED) != 0)
        return INTEL_PIPE_A;
    if ((read32(INTEL_DISPLAY_B_CONTROL) & DISPLAY_CONTROL_ENABLED) != 0)
        return INTEL_PIPE_B;

    // Fallback: try to pick an enabled pipe.
    if ((read32(INTEL_DISPLAY_A_PIPE_CONTROL) & INTEL_PIPE_ENABLED) != 0)
        return INTEL_PIPE_A;
    if ((read32(INTEL_DISPLAY_B_PIPE_CONTROL) & INTEL_PIPE_ENABLED) != 0)
        return INTEL_PIPE_B;
    if ((read32(INTEL_DISPLAY_C_PIPE_CONTROL) & INTEL_PIPE_ENABLED) != 0)
        return INTEL_PIPE_C;

    return INTEL_PIPE_A;
}


static void
init_cursor_registers()
{
    pipe_index pipe = active_pipe();
    if (sCursorRegsInitialized && sCursorPipe == pipe)
        return;

    addr_t offset = pipe_offset(pipe);

    sCursorRegs.control = INTEL_CURSOR_CONTROL + offset;
    sCursorRegs.base = INTEL_CURSOR_BASE + offset;
    sCursorRegs.position = INTEL_CURSOR_POSITION + offset;
    sCursorRegs.size = INTEL_CURSOR_SIZE + offset;
    sCursorRegs.palette = INTEL_CURSOR_PALETTE + offset;

    sCursorPipe = pipe;
    sCursorRegsInitialized = true;
}


status_t
intel_set_cursor_shape(uint16 width, uint16 height, uint16 hotX, uint16 hotY,
    uint8* andMask, uint8* xorMask)
{
    if (width > 64 || height > 64)
        return B_BAD_VALUE;

    init_cursor_registers();

    // Disable cursor before touching the backing store.
    write32(sCursorRegs.control, 0);

    uint32 gen = gInfo->shared_info->device_type.Generation();
    if (gen >= 9) {
        // Gen9+ reliably supports ARGB; two-color cursor modes are not always
        // available on newer display engines.
        uint32* dest = (uint32*)gInfo->shared_info->cursor_memory;
        memset(dest, 0, 64 * 64 * sizeof(uint32));

        uint8 byteWidth = (width + 7) / 8;
        for (int32 y = 0; y < height; y++) {
            for (int32 x = 0; x < width; x++) {
                int32 byteIndex = byteWidth * y + (x / 8);
                int32 bitIndex = 7 - (x % 8);

                bool andBit = (andMask[byteIndex] >> bitIndex) & 1;
                bool xorBit = (xorMask[byteIndex] >> bitIndex) & 1;

                uint32 pixel = 0x00000000; // transparent
                if (!andBit && !xorBit)
                    pixel = 0xff000000; // black
                else if (!andBit && xorBit)
                    pixel = 0xffffffff; // white
                else if (andBit && xorBit)
                    pixel = 0xff000000; // invert not supported, approximate

                dest[64 * y + x] = pixel;
            }
        }

        gInfo->shared_info->cursor_format = CURSOR_FORMAT_ARGB;
        write32(sCursorRegs.control,
            CURSOR_ENABLED | gInfo->shared_info->cursor_format);
        write32(sCursorRegs.size, (height << 12) | width);
        write32(sCursorRegs.base,
            (uint32)gInfo->shared_info->physical_graphics_memory
            + gInfo->shared_info->cursor_buffer_offset);
    } else {
        // Two-color mode, data is ordered as follows (always 64 bit per line):
        //  plane 1: line 0 (AND mask)
        //  plane 0: line 0 (XOR mask)
        //  plane 1: line 1 (AND mask)
        //  ...
        uint8* data = gInfo->shared_info->cursor_memory;
        uint8 byteWidth = (width + 7) / 8;

        for (int32 y = 0; y < height; y++) {
            for (int32 x = 0; x < byteWidth; x++) {
                data[16 * y + x] = andMask[byteWidth * y + x];
                data[16 * y + x + 8] = xorMask[byteWidth * y + x];
            }
        }

        // Set palette entries to white/black.
        write32(sCursorRegs.palette + 0, 0x00ffffff);
        write32(sCursorRegs.palette + 4, 0);

        gInfo->shared_info->cursor_format = CURSOR_FORMAT_2_COLORS;

        write32(sCursorRegs.control,
            CURSOR_ENABLED | gInfo->shared_info->cursor_format);
        write32(sCursorRegs.size, (height << 12) | width);
        write32(sCursorRegs.base,
            (uint32)gInfo->shared_info->physical_graphics_memory
            + gInfo->shared_info->cursor_buffer_offset);
    }

    // Changing the hot point changes the cursor position.
    if (hotX != gInfo->shared_info->cursor_hot_x
        || hotY != gInfo->shared_info->cursor_hot_y) {
        int32 x = read32(sCursorRegs.position);
        int32 y = x >> 16;
        x &= 0xffff;

        if (x & CURSOR_POSITION_NEGATIVE)
            x = -(x & CURSOR_POSITION_MASK);
        if (y & CURSOR_POSITION_NEGATIVE)
            y = -(y & CURSOR_POSITION_MASK);

        x += gInfo->shared_info->cursor_hot_x;
        y += gInfo->shared_info->cursor_hot_y;

        gInfo->shared_info->cursor_hot_x = hotX;
        gInfo->shared_info->cursor_hot_y = hotY;

        intel_move_cursor(x, y);
    }

    return B_OK;
}


uint32
intel_get_cursor_bits(void)
{
    if (gInfo->shared_info->cursor_memory == NULL)
        return 0;

    if (gInfo->shared_info->device_type.Generation() >= 4)
        return 32;

    return 1;
}


status_t
intel_set_cursor_bitmap(uint16 width, uint16 height, uint16 hotX, uint16 hotY,
    color_space colorSpace, uint16 bytesPerRow, const uint8* bitmapData)
{
    if (width == 0 || height == 0 || width > 64 || height > 64)
        return B_BAD_VALUE;

    if (colorSpace != B_RGBA32 && colorSpace != B_RGB32)
        return B_BAD_TYPE;

    if (bytesPerRow < width * 4)
        return B_BAD_VALUE;

    if (gInfo->shared_info->cursor_memory == NULL || bitmapData == NULL)
        return B_NO_INIT;

    init_cursor_registers();

    // Disable the cursor before touching the backing store.
    write32(sCursorRegs.control, 0);

    uint32* dest = (uint32*)gInfo->shared_info->cursor_memory;
    const uint32* src = (const uint32*)bitmapData;
    uint32 srcPixelsPerRow = bytesPerRow / 4;

    // Clear the whole 64x64 buffer to avoid garbage when the cursor is smaller.
    memset(dest, 0, 64 * 64 * sizeof(uint32));

    // Copy into the 64-pixel-wide hardware layout.
    // Note: B_RGB32 typically has an alpha byte of 0; force it opaque.
    for (int32 y = 0; y < height; y++) {
        for (int32 x = 0; x < width; x++) {
            uint32 pixel = src[srcPixelsPerRow * y + x];
            if (colorSpace == B_RGB32)
                pixel |= 0xff000000;
            dest[64 * y + x] = pixel;
        }
    }

    gInfo->shared_info->cursor_format
        = (colorSpace == B_RGB32) ? CURSOR_FORMAT_XRGB : CURSOR_FORMAT_ARGB;

    write32(sCursorRegs.control,
        CURSOR_ENABLED | gInfo->shared_info->cursor_format);
    write32(sCursorRegs.size, (height << 12) | width);
    write32(sCursorRegs.base,
        (uint32)gInfo->shared_info->physical_graphics_memory
        + gInfo->shared_info->cursor_buffer_offset);

    // Changing the hot point changes the cursor position.
    if (hotX != gInfo->shared_info->cursor_hot_x
        || hotY != gInfo->shared_info->cursor_hot_y) {
        int32 x = read32(sCursorRegs.position);
        int32 y = x >> 16;
        x &= 0xffff;

        if (x & CURSOR_POSITION_NEGATIVE)
            x = -(x & CURSOR_POSITION_MASK);
        if (y & CURSOR_POSITION_NEGATIVE)
            y = -(y & CURSOR_POSITION_MASK);

        x += gInfo->shared_info->cursor_hot_x;
        y += gInfo->shared_info->cursor_hot_y;

        gInfo->shared_info->cursor_hot_x = hotX;
        gInfo->shared_info->cursor_hot_y = hotY;

        intel_move_cursor(x, y);
    }

    return B_OK;
}


void
intel_move_cursor(uint16 _x, uint16 _y)
{
    init_cursor_registers();

    int32 x = (int32)_x - gInfo->shared_info->cursor_hot_x;
    int32 y = (int32)_y - gInfo->shared_info->cursor_hot_y;

    if (x < 0)
        x = -x | CURSOR_POSITION_NEGATIVE;
    if (y < 0)
        y = -y | CURSOR_POSITION_NEGATIVE;

    write32(sCursorRegs.position, (y << 16) | x);
}


void
intel_show_cursor(bool isVisible)
{
    init_cursor_registers();

    if (gInfo->shared_info->cursor_visible == isVisible)
        return;

    write32(sCursorRegs.control, (isVisible ? CURSOR_ENABLED : 0)
        | gInfo->shared_info->cursor_format);

    // Some generations require rewriting the base to commit double-buffered state.
    write32(sCursorRegs.base,
        (uint32)gInfo->shared_info->physical_graphics_memory
        + gInfo->shared_info->cursor_buffer_offset);

    gInfo->shared_info->cursor_visible = isVisible;
}
