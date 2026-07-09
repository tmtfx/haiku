#include "accelerant.h"
#include "accelerant_protos.h"
#include "accelerant.h"
#include "evergreen_reg.h"
#include <Accelerant.h>
#include <string.h>

// Helper macros to read/write registers provided by accelerant.h

uint32 radeon_get_cursor_bits(void)
{
	// Modern GPUs support true-color cursors with alpha
	return 32;
}
status_t radeon_set_cursor_bitmap(uint16 width, uint16 height, uint16 hotX, uint16 hotY,
    color_space colorSpace, uint16 bytesPerRow, const uint8* bitmapData)
{
    if (width > 64 || height > 64 || hotX >= width || hotY >= height)
        return B_ERROR;

    // Sicurezza: Verifichiamo l'allocazione globale eseguita in init_common
    if (gInfo->cursor_vaddr == NULL)
        return B_NO_INIT;

    // Pulisci l'area del cursore
    memset(gInfo->cursor_vaddr, 0, 64 * 64 * 4);

    if (colorSpace != B_RGBA32 && colorSpace != B_RGB32)
        return B_ERROR;

    // Copia e conversione in ARGB8888
    for (int y = 0; y < height && y < 64; y++) {
        const uint8* srcRow = bitmapData + y * bytesPerRow;
        uint32* dstRow = (uint32*)(gInfo->cursor_vaddr + y * 64 * 4);
        for (int x = 0; x < width && x < 64; x++) {
            const uint8* pixel = srcRow + x * 4;
            uint8 b = pixel[0];
            uint8 g = pixel[1];
            uint8 r = pixel[2];
            uint8 a = (colorSpace == B_RGBA32) ? pixel[3] : 0xFF;

            if (a == 0)
                continue;

            uint32 packed = ((uint32)a << 24) | ((uint32)r << 16) | ((uint32)g << 8) | (uint32)b;
            dstRow[x] = packed;
        }
    }
	// PER TEST, prova a vedere se il quadrato effettivamente viene letto (colore bianco) e quindi la copia
	// dei byte è dalla sorgente sbagliata! (copia dal framebuffer)
	// memset(gInfo->cursor_vaddr, 0xFF, 64 * 64 * 4);

    gInfo->cursor_width = width;
    gInfo->cursor_height = height;
    gInfo->cursor_hotx = hotX;
    gInfo->cursor_hoty = hotY;

    // LOCK UPDATE prima della scrittura dei registri geometrie
    Write32(OUT, EVERGREEN_CUR_UPDATE, EVERGREEN_CURSOR_UPDATE_LOCK);
	
	uint64 gpu_cursor_phys = (uint64)gInfo->cursor_fb_offset;

    Write32(OUT, EVERGREEN_CUR_SURFACE_ADDRESS, (uint32)(gpu_cursor_phys & 0xFFFFFFFF));
#ifdef EVERGREEN_CUR_SURFACE_ADDRESS_HIGH
    Write32(OUT, EVERGREEN_CUR_SURFACE_ADDRESS_HIGH, (uint32)(gpu_cursor_phys >> 32));
#endif

	Write32(OUT, EVERGREEN_CUR_SIZE, ((uint32)gInfo->cursor_height << 16) | (uint32)gInfo->cursor_width);
    Write32(OUT, EVERGREEN_CUR_HOT_SPOT, ((uint32)gInfo->cursor_hoty << 16) | (uint32)gInfo->cursor_hotx);
	// uint32 control = EVERGREEN_CURSOR_EN | EVERGREEN_CURSOR_24_8_UNPRE_MULT | EVERGREEN_CURSOR_FORCE_MC_ON; //mi genera quadrato con colori invertiti dello sfondo
    uint32 control = EVERGREEN_CURSOR_EN 
                   | EVERGREEN_CURSOR_MODE(2) // <- quadrato modalità 32bit colore
                   | EVERGREEN_CURSOR_FORCE_MC_ON;
    Write32(OUT, EVERGREEN_CUR_CONTROL, control);

    // UNLOCK UPDATE
    Write32(OUT, EVERGREEN_CUR_UPDATE, 0);

    return B_OK;
}

status_t radeon_set_cursor_shape(uint16 width, uint16 height, uint16 hotX, uint16 hotY,
    uint8* andMask, uint8* xorMask)
{
    if (width > 64 || height > 64 || hotX >= width || hotY >= height)
        return B_ERROR;

    if (gInfo->cursor_vaddr == NULL)
        return B_NO_INIT;

    memset(gInfo->cursor_vaddr, 0, 64 * 64 * 4);

    int maskBytesPerRow = (width + 7) / 8;

    for (int y = 0; y < height && y < 64; y++) {
        uint32* dstRow = (uint32*)(gInfo->cursor_vaddr + y * 64 * 4);
        const uint8* andRow = andMask + y * maskBytesPerRow;
        const uint8* xorRow = xorMask + y * maskBytesPerRow;
        for (int x = 0; x < width && x < 64; x++) {
            int byteIdx = x / 8;
            int bit = 7 - (x % 8);
            bool andBit = ((andRow[byteIdx] >> bit) & 0x1) != 0;
            bool xorBit = ((xorRow[byteIdx] >> bit) & 0x1) != 0;

            if (andBit) {
                continue;
            }
            uint8 r = xorBit ? 0x00 : 0xFF;
            uint8 g = xorBit ? 0x00 : 0xFF;
            uint8 b = xorBit ? 0x00 : 0xFF;
            uint8 a = 0xFF;
            uint32 packed = ((uint32)a << 24) | ((uint32)r << 16) | ((uint32)g << 8) | (uint32)b;
            dstRow[x] = packed;
        }
    }

    gInfo->cursor_width = width;
    gInfo->cursor_height = height;
    gInfo->cursor_hotx = hotX;
    gInfo->cursor_hoty = hotY;

    // LOCK UPDATE prima della scrittura dei registri geometrie
    Write32(OUT, EVERGREEN_CUR_UPDATE, EVERGREEN_CURSOR_UPDATE_LOCK);
	
	uint64 gpu_cursor_phys = (uint64)gInfo->cursor_fb_offset;

    Write32(OUT, EVERGREEN_CUR_SURFACE_ADDRESS, (uint32)(gpu_cursor_phys & 0xFFFFFFFF));
#ifdef EVERGREEN_CUR_SURFACE_ADDRESS_HIGH
    Write32(OUT, EVERGREEN_CUR_SURFACE_ADDRESS_HIGH, (uint32)(gpu_cursor_phys >> 32));
#endif

	Write32(OUT, EVERGREEN_CUR_SIZE, ((uint32)gInfo->cursor_height << 16) | (uint32)gInfo->cursor_width);
    Write32(OUT, EVERGREEN_CUR_HOT_SPOT, ((uint32)gInfo->cursor_hoty << 16) | (uint32)gInfo->cursor_hotx);
	// uint32 control = EVERGREEN_CURSOR_EN | EVERGREEN_CURSOR_24_8_UNPRE_MULT | EVERGREEN_CURSOR_FORCE_MC_ON; //mi genera quadrato con colori invertiti dello sfondo
    uint32 control = EVERGREEN_CURSOR_EN 
                   | EVERGREEN_CURSOR_MODE(2) // <- quadrato modalità 32bit colore
                   | EVERGREEN_CURSOR_FORCE_MC_ON;
    Write32(OUT, EVERGREEN_CUR_CONTROL, control);

    // UNLOCK UPDATE
    Write32(OUT, EVERGREEN_CUR_UPDATE, 0);

    return B_OK;
}

void radeon_move_cursor(uint16 x, uint16 y)
{
	// Position register: high 16 = x, low 16 = y (match older drivers)
	uint32 pos = ((uint32)x << 16) | (uint32)y;
	Write32(OUT, EVERGREEN_CUR_POSITION, pos);
}

void radeon_show_cursor(bool isVisible)
{
	uint32 control = Read32(OUT, EVERGREEN_CUR_CONTROL);
	if (isVisible)
		control |= EVERGREEN_CURSOR_EN;
	else
		control &= ~EVERGREEN_CURSOR_EN;
	Write32(OUT, EVERGREEN_CUR_CONTROL, control);
}
