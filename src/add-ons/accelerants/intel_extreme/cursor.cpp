/*
 * Copyright 2006-2009, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 * Axel Dörfler, axeld@pinc-software.de
 */

#include "accelerant_protos.h"
#include "accelerant.h"

#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>

#undef TRACE
//#define TRACE_CURSOR
#ifdef TRACE_CURSOR
#    define TRACE(x...) _sPrintf("intel_extreme cursor: " x)
#else
#    define TRACE(x...) do { } while (0)
#endif

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

// Buffer dedicato da 16KB per evitare il buffer overflow sui 4KB di sistema
static uint8* sArgbCursorBuffer = NULL;
static uint32 sArgbCursorOffset = 0;        // Offset GTT
static phys_addr_t sArgbCursorPhysical = 0;    // Indirizzo Fisico hardware

#define ARGB_CURSOR_SIZE    (64 * 64 * 4)   // 16384 Bytes necessari

// Costanti native Gen5+ (Ironlake)
#define ILK_CURSOR_MODE_DISABLE     0x00
#define ILK_CURSOR_MODE_64_2COLOR   0x06
#define ILK_CURSOR_MODE_64_ARGB     0x27

static addr_t
pipe_offset(pipe_index pipe)
{
    if (pipe <= INTEL_PIPE_A)
        return 0;

    // Se siamo su Ironlake (Gen 5), lo stride tra le Pipe è 0x1000
    if (gInfo->shared_info->device_type.Generation() == 5)
        return 0x1000 * (pipe - INTEL_PIPE_A);

    // Vecchio fallback pre-Gen5
    return 0x40 * (pipe - INTEL_PIPE_A);
}

static bool
hardware_cursor_supported()
{
    if (gInfo == NULL || gInfo->shared_info == NULL)
        return false;

    if (!gInfo->shared_info->hardware_cursor_enabled)
        return false;

    // ABILITAZIONE SELETTIVA: Accendiamo l'hardware cursor SOLO su IronLake.
    // Haswell, GeminiLake e IceLake (Gen10 1005G1) useranno il cursore software dell'app_server.
    if (gInfo->shared_info->device_type.InGroup(INTEL_GROUP_ILK))
        return true;

    return false;
}

static pipe_index
active_pipe()
{
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

    TRACE("init: pipe=%d off=0x%" B_PRIxADDR "\n", (int)pipe, offset);
}

static void
post_cursor_writes()
{
    (void)read32(sCursorRegs.control);
}

status_t
intel_set_cursor_shape(uint16 width, uint16 height, uint16 hotX, uint16 hotY,
    uint8* andMask, uint8* xorMask)
{
    if (!hardware_cursor_supported())
        return B_OK;

    if (width > 64 || height > 64 || andMask == NULL || xorMask == NULL)
        return B_BAD_VALUE;

    init_cursor_registers();

    // Spegne il cursore prima delle modifiche
    write32(sCursorRegs.control, ILK_CURSOR_MODE_DISABLE);
    post_cursor_writes();

    uint8* data = gInfo->shared_info->cursor_memory;
    uint8 byteWidth = (width + 7) / 8;

    for (int32 y = 0; y < height; y++) {
        for (int32 x = 0; x < byteWidth; x++) {
            data[16 * y + x] = andMask[byteWidth * y + x];
            data[16 * y + x + 8] = xorMask[byteWidth * y + x];
        }
    }

    write32(sCursorRegs.palette + 0, 0x00ffffff);
    write32(sCursorRegs.palette + 4, 0);

    gInfo->shared_info->cursor_format = ILK_CURSOR_MODE_64_2COLOR;

    write32(sCursorRegs.size, (height << 12) | width);
    write32(sCursorRegs.base, (uint32)gInfo->shared_info->physical_graphics_memory
        + gInfo->shared_info->cursor_buffer_offset);
    
    write32(sCursorRegs.control, ILK_CURSOR_MODE_64_2COLOR);
    post_cursor_writes();

    return B_OK;
}

uint32
intel_get_cursor_bits(void)
{
    if (!hardware_cursor_supported())
        return 0;
    return 32;
}

status_t
intel_set_cursor_bitmap(uint16 width, uint16 height, uint16 hotX, uint16 hotY,
    color_space colorSpace, uint16 bytesPerRow, const uint8* bitmapData)
{
    if (!hardware_cursor_supported())
        return B_OK;

    if (width == 0 || height == 0 || width > 64 || height > 64 || bitmapData == NULL)
        return B_BAD_VALUE;

    if (colorSpace != B_RGBA32 && colorSpace != B_RGB32)
        return B_BAD_TYPE;

    init_cursor_registers();

    // ALLOCAZIONE DINAMICA SICURA 16KB (Previene il crash di memoria)
    if (sArgbCursorBuffer == NULL) {
        intel_allocate_graphics_memory alloc;
        alloc.magic = INTEL_PRIVATE_DATA_MAGIC;
        alloc.size = ARGB_CURSOR_SIZE;
        alloc.alignment = ARGB_CURSOR_SIZE; 
        alloc.flags = 0;
        if (ioctl(gInfo->device, INTEL_ALLOCATE_GRAPHICS_MEMORY, &alloc, sizeof(alloc)) < 0)
            return B_NO_MEMORY;

        sArgbCursorBuffer = (uint8*)alloc.buffer_base;
        sArgbCursorOffset = (uint32)(alloc.buffer_base - (addr_t)gInfo->shared_info->graphics_memory);
        sArgbCursorPhysical = (phys_addr_t)gInfo->shared_info->physical_graphics_memory + sArgbCursorOffset;
    }

    write32(sCursorRegs.control, ILK_CURSOR_MODE_DISABLE);
    post_cursor_writes();

    // Pulisce in modo sicuro lo spazio allocato da 16KB
    memset(sArgbCursorBuffer, 0, ARGB_CURSOR_SIZE);

    for (uint16 y = 0; y < height && y < 64; y++) {
        const uint32* src = (const uint32*)(bitmapData + y * bytesPerRow);
        uint32* dst = (uint32*)sArgbCursorBuffer + y * 64;
        for (uint16 x = 0; x < width && x < 64; x++) {
            dst[x] = src[x];
        }
    }

    gInfo->shared_info->cursor_format = ILK_CURSOR_MODE_64_ARGB;
    
    write32(sCursorRegs.size, (64 << 12) | 64);
    write32(sCursorRegs.base, (uint32)sArgbCursorPhysical);
    write32(sCursorRegs.control, ILK_CURSOR_MODE_64_ARGB);
    post_cursor_writes();

    return B_OK;
}

void
intel_move_cursor(uint16 _x, uint16 _y)
{
    if (!hardware_cursor_supported())
        return;

    init_cursor_registers();

    int32 x = (int32)_x - gInfo->shared_info->cursor_hot_x;
    int32 y = (int32)_y - gInfo->shared_info->cursor_hot_y;

    if (x < 0)
        x = -x | CURSOR_POSITION_NEGATIVE;
    if (y < 0)
        y = -y | CURSOR_POSITION_NEGATIVE;

    write32(sCursorRegs.position, (y << 16) | x);
    post_cursor_writes();
}

void
intel_show_cursor(bool isVisible)
{
    if (!hardware_cursor_supported())
        return;

    init_cursor_registers();

    write32(sCursorRegs.control, isVisible ? gInfo->shared_info->cursor_format : ILK_CURSOR_MODE_DISABLE);
    
    if (gInfo->shared_info->cursor_format == ILK_CURSOR_MODE_64_ARGB && sArgbCursorBuffer != NULL) {
        write32(sCursorRegs.base, (uint32)sArgbCursorPhysical);
    } else {
        write32(sCursorRegs.base, (uint32)gInfo->shared_info->physical_graphics_memory
            + gInfo->shared_info->cursor_buffer_offset);
    }
    post_cursor_writes();

    gInfo->shared_info->cursor_visible = isVisible;
}
