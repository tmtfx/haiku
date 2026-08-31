/*
 * Copyright 2026, Haiku and the Pirati Del Frico contributors.
 * Distributed under the terms of the MIT License.
 */
#include "accelerant_protos.h"
#include "accelerant.h"
#include <cstring>

#define CALLED() debug_printf("INTEL_ARC_ACC: CALLED %s\n", __FUNCTION__)

void
ShowCursor(bool isVisible)
{
	CALLED();
    const int8 pipe = gInfo->shared_info->active_pipe;
    if (pipe < 0){
    	debug_printf("intel_arc SHOW_CURSOR error, pipe < 0\n");
        return;
    }

    const uint32 pipeOffset = (uint32)pipe * INTEL_ARC_MMIO_PIPE_OFFSET;
    
    gInfo->shared_info->cursor_visible = isVisible;

    if (!isVisible) {
        // Disabilita il cursore scrivendo 0 su CUR_CTL
        debug_printf("intel_arc SHOW_CURSOR disabling cursor...\n");
        write_register(INTEL_ARC_MMIO_CUR_CTL_A + pipeOffset, MCURSOR_MODE_DISABLE);
        write_register(INTEL_ARC_MMIO_CUR_SURF_A + pipeOffset, 0);
        return;
    }
    debug_printf("intel_arc SHOW_CURSOR enabling cursor...\n");

    // Imposta indirizzo fisico VRAM (GGTT/LMEM) del buffer cursore (deve essere a 4K boundary)
    uint32 cursorOffset = gInfo->shared_info->cursor_physical_base;
    write_register(INTEL_ARC_MMIO_CUR_SURF_A + pipeOffset, cursorOffset);

    // Abilita la modalità 64x64 32bpp ARGB
    uint32 curCtl = MCURSOR_MODE_64_ARGB8888;
    write_register(INTEL_ARC_MMIO_CUR_CTL_A + pipeOffset, curCtl);
}

void
MoveCursor(uint16 x, uint16 y)
{
    const int8 pipe = gInfo->shared_info->active_pipe;
    if (pipe < 0){
    	debug_printf("intel_arc SHOW_CURSOR error, pipe < 0\n");
        return;
    }

    const uint32 pipeOffset = (uint32)pipe * INTEL_ARC_MMIO_PIPE_OFFSET;

    // Sottrai l'hotspot registrato in shared_info
    int32 posX = (int32)x - (int32)gInfo->shared_info->cursor_hot_x;
    int32 posY = (int32)y - (int32)gInfo->shared_info->cursor_hot_y;

    uint32 curPos = 0;

    // Gestione coordinata X (Bit 0:12 pos, Bit 15 sign)
    if (posX < 0) {
        curPos |= CUR_POS_SIGN_X | ((uint32)(-posX) & 0x1FFF);
    } else {
        curPos |= ((uint32)posX & 0x1FFF);
    }

    // Gestione coordinata Y (Bit 16:28 pos, Bit 31 sign)
    if (posY < 0) {
        curPos |= CUR_POS_SIGN_Y | (((uint32)(-posY) & 0x1FFF) << 16);
    } else {
        curPos |= (((uint32)posY & 0x1FFF) << 16);
    }

    write_register(INTEL_ARC_MMIO_CUR_POS_A + pipeOffset, curPos);
}

status_t
SetCursorShape(uint16 width, uint16 height, uint16 hotX, uint16 hotY,
    const uint8* andMask, const uint8* xorMask)
{
	CALLED();
    if (width > 64 || height > 64 || !andMask || !xorMask)
        return B_BAD_VALUE;

    gInfo->shared_info->cursor_hot_x = hotX;
    gInfo->shared_info->cursor_hot_y = hotY;

    // Buffer di destinazione mappato in memoria virtuale nell'accelerant
    uint32* cursorBuffer = (uint32*)gInfo->shared_info->cursor_virtual_base;
    if (!cursorBuffer)
        return B_NO_INIT;

    // Pulisce il buffer 64x64 (4 byte per pixel = 16 KB) a trasparente puro
    memset(cursorBuffer, 0, 64 * 64 * sizeof(uint32));

    uint16 bytesPerRow = (width + 7) / 8;

    for (uint16 y = 0; y < height; y++) {
        for (uint16 x = 0; x < width; x++) {
            uint8 bitPos = 7 - (x % 8);
            uint8 byteIdx = (y * bytesPerRow) + (x / 8);

            bool andBit = (andMask[byteIdx] >> bitPos) & 1;
            bool xorBit = (xorMask[byteIdx] >> bitPos) & 1;

            uint32 pixel = 0x00000000; // Trasparente

            if (!andBit && !xorBit) {
                // Nero opaco
                pixel = 0xFF000000;
            } else if (!andBit && xorBit) {
                // Bianco opaco
                pixel = 0xFFFFFFFF;
            } else if (andBit && xorBit) {
                // Inversione (Invisibile / Invertito su display)
                pixel = 0x80FFFFFF;
            }

            cursorBuffer[y * 64 + x] = pixel;
        }
    }

    // Se il cursore è attualmente visibile, invia un refresh dell'indirizzo di superficie
    if (gInfo->shared_info->cursor_visible) {
        ShowCursor(true);
    }

    return B_OK;
}

#ifdef IS_PIRATI_BUILD

status_t
SetCursorBitmap(uint16 width, uint16 height, uint16 hotX, uint16 hotY,
    color_space colorSpace, uint16 bytesPerRow, const uint8* bitmapData)
{
	CALLED();
    if (width > 64 || height > 64 || !bitmapData)
        return B_BAD_VALUE;

    // Salva le coordinate dell'hotspot nella struttura condivisa
    gInfo->shared_info->cursor_hot_x = hotX;
    gInfo->shared_info->cursor_hot_y = hotY;

    uint32* cursorBuffer = (uint32*)gInfo->shared_info->cursor_virtual_base;
    if (!cursorBuffer)
        return B_NO_INIT;

    // Azzera il buffer 64x64 ARGB8888 (16 KB)
    memset(cursorBuffer, 0, 64 * 64 * sizeof(uint32));

    const uint8* srcRow = bitmapData;
    const size_t copyBytesPerRow = min_c((size_t)(width * sizeof(uint32)), (size_t)bytesPerRow);

    for (uint16 y = 0; y < height; y++) {
        uint32* dstRow = cursorBuffer + (y * 64);

        if (colorSpace == B_RGBA32 || colorSpace == B_RGB32) {
            // Copia diretta dei pixel a 32-bit (ARGB8888)
            memcpy(dstRow, srcRow, copyBytesPerRow);
        } else if (colorSpace == B_RGB16 || colorSpace == B_RGB15) {
            // Conversione al volo da 16-bit a 32-bit ARGB se inviato in 16-bit
            const uint16* src16 = (const uint16*)srcRow;
            for (uint16 x = 0; x < width; x++) {
                uint16 pixel16 = src16[x];
                uint8 r = ((pixel16 >> 11) & 0x1F) << 3;
                uint8 g = ((pixel16 >> 5) & 0x3F) << 2;
                uint8 b = (pixel16 & 0x1F) << 3;
                dstRow[x] = (0xFF << 24) | (r << 16) | (g << 8) | b;
            }
        }

        srcRow += bytesPerRow;
    }

    // Se il cursore è visibile, invia il flush dell'indirizzo superficie
    if (gInfo->shared_info->cursor_visible) {
        ShowCursor(true);
    }

    return B_OK;
}

uint32
GetCursorBits(void)
{
	CALLED();
	debug_printf("intel_arc GET_CURSOR_BITS ritorna %d\n", gInfo->shared_info->settings.cursorbits);
    return gInfo->shared_info->settings.cursorbits;
}

#endif // IS_PIRATI_BUILD
