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

// Buffer dedicato da 16KB per evitare il buffer overflow sui 4KB stock
static uint8* sArgbCursorBuffer = NULL;
static uint32 sArgbCursorOffset = 0;        
static phys_addr_t sArgbCursorPhysical = 0;    

#define ARGB_CURSOR_SIZE    (64 * 64 * 4)   

// Costanti di controllo per generazione
#define LEGACY_CURSOR_MODE_DISABLE   0x00
#define LEGACY_CURSOR_MODE_64_ARGB   0x27

// Gen9+ (GeminiLake, IceLake) -> Bit 31 (Enable) + Bit 2:0 (010b = ARGB 32bpp)
#define GEN9_CURSOR_MODE_DISABLE     0x00
#define GEN9_CURSOR_MODE_64_ARGB     ((1UL << 31) | (2UL << 0))


static addr_t
pipe_offset(pipe_index pipe)
{
    if (pipe <= INTEL_PIPE_A)
        return 0;

    uint32 gen = gInfo->shared_info->device_type.Generation();

    // Da Ironlake (Gen5) in poi, lo stride tra le Pipe è di 0x1000
    if (gen >= 5)
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

    if (gInfo->shared_info->cursor_memory == NULL)
        return false;

    // Ora supportiamo esplicitamente tutto l'hardware da Ironlake in su!
    return true;
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
    uint32 gen = gInfo->shared_info->device_type.Generation();

    if (gen >= 9) {
        // Layout moderno Gen9+ (GeminiLake, IceLake / Skylake+)
        // I registri del cursore sono mappati nel blocco del Plane 7
        sCursorRegs.control  = 0x70180 + offset;
        sCursorRegs.position = 0x70188 + offset;
        sCursorRegs.base     = 0x7019c + offset;
        sCursorRegs.size     = 0x701a0 + offset;
        sCursorRegs.palette  = 0x701a4 + offset;
    } else {
        // Layout classico (Ironlake, Haswell, e precedenti)
        sCursorRegs.control  = INTEL_CURSOR_CONTROL + offset;
        sCursorRegs.base     = INTEL_CURSOR_BASE + offset;
        sCursorRegs.position = INTEL_CURSOR_POSITION + offset;
        sCursorRegs.size     = INTEL_CURSOR_SIZE + offset;
        sCursorRegs.palette  = INTEL_CURSOR_PALETTE + offset;
    }

    sCursorPipe = pipe;
    sCursorRegsInitialized = true;

    TRACE("init: pipe=%d off=0x%" B_PRIxADDR " ctl_reg=0x%" PRIx32 "\n", 
        (int)pipe, offset, sCursorRegs.control);
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
    // Lasciamo che i cursori monocromatici usino il fallback software o 
    // l'implementazione stock se non siamo su hardware supportato
    if (!hardware_cursor_supported())
        return B_OK;

    // Di fatto Haiku moderno usa quasi esclusivamente set_cursor_bitmap per i cursori ARGB.
    // Manteniamo una disattivazione pulita di sicurezza qui.
    init_cursor_registers();
    write32(sCursorRegs.control, 0);
    post_cursor_writes();
    
    return B_OK;
}


uint32
intel_get_cursor_bits(void)
{
    // Deve rispondere sempre. Da Gen4 in poi la GPU supporta i cursori a 32-bit ARGB.
    if (gInfo != NULL && gInfo->shared_info != NULL 
        && gInfo->shared_info->device_type.Generation() >= 4) {
        return 32;
    }
    return 1;
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

    // Allocazione del buffer grafico da 16KB condiviso
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

    // Spegne il cursore prima di aggiornare i pixel
    write32(sCursorRegs.control, 0);
    post_cursor_writes();

    // Copia della bitmap
    memset(sArgbCursorBuffer, 0, ARGB_CURSOR_SIZE);
    for (uint16 y = 0; y < height && y < 64; y++) {
        const uint32* src = (const uint32*)(bitmapData + y * bytesPerRow);
        uint32* dst = (uint32*)sArgbCursorBuffer + y * 64;
        for (uint16 x = 0; x < width && x < 64; x++) {
            dst[x] = src[x];
        }
    }

    uint32 gen = gInfo->shared_info->device_type.Generation();
    uint32 ctlValue = 0;

    if (gen >= 9) {
        // GeminiLake / IceLake
        ctlValue = GEN9_CURSOR_MODE_64_ARGB;
        gInfo->shared_info->cursor_format = GEN9_CURSOR_MODE_64_ARGB;
        
        write32(sCursorRegs.size, (64 << 12) | 64);
        write32(sCursorRegs.base, (uint32)sArgbCursorOffset); // Su Gen9+ serve l'offset GGTT, non il fisico!
    } else {
        // Ironlake (Gen5) e Haswell (Gen7.5)
        ctlValue = LEGACY_CURSOR_MODE_64_ARGB;
        gInfo->shared_info->cursor_format = LEGACY_CURSOR_MODE_64_ARGB;
        
        write32(sCursorRegs.size, (64 << 12) | 64);
        write32(sCursorRegs.base, (uint32)sArgbCursorPhysical); // Richiede l'indirizzo fisico
    }

    write32(sCursorRegs.control, ctlValue);
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
    uint32 gen = gInfo->shared_info->device_type.Generation();

    if (isVisible) {
        write32(sCursorRegs.control, gInfo->shared_info->cursor_format);
    } else {
        write32(sCursorRegs.control, 0);
    }

    // Carica la base corretta per fare il latch del registro double-buffered
    if (gen >= 9) {
        write32(sCursorRegs.base, (uint32)sArgbCursorOffset);
    } else {
        write32(sCursorRegs.base, (uint32)sArgbCursorPhysical);
    }
    post_cursor_writes();

    gInfo->shared_info->cursor_visible = isVisible;
}
