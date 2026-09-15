/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include <string.h>
#include "sm750_macros.h"
#include "DriverInterface.h"
#include "protos.h"


#define CALLED() debug_printf("SM750_ACC: CALLED %s\n", __FUNCTION__)

extern accelerant_info *gInfo;

void
sm750_move_cursor(uint16 x, uint16 y)
{
    shared_info *si = gInfo->si;
    vuint32 *regs = gInfo->regs;

    int16 x_pos = (int16)x - (int16)si->cursor.hot_x;
    int16 y_pos = (int16)y - (int16)si->cursor.hot_y;

    uint32 reg_val = 0;

    if (x_pos < 0) {
        // Bit 11 = 1 (Out left), the value becomes positive (dinstance from border)
        reg_val |= (1 << 11) | (uint32)((-x_pos) & 0x3F); 
    } else {
        reg_val |= (uint32)(x_pos & 0x07FF);
    }

    if (y_pos < 0) {
        // Bit 27 = 1 (Out up)
        reg_val |= (1 << 27) | (uint32)(((-y_pos) & 0x3F) << 16);
    } else {
        reg_val |= (uint32)((y_pos & 0x07FF) << 16);
    }

    if (si->card_info.is_panel)
        SM750_WREG32(SM750_DISP_PANEL_CUR_POS, reg_val);
    else
        SM750_WREG32(SM750_DISP_CRT_CUR_POS, reg_val);
    
    if (si->settings.usealphacursor) {
    	//TODO: stabilire posizione ALPHA LAYER
    }

    si->cursor.x = x;
    si->cursor.y = y;
    
    if (si->settings.usealphacursor) {
        // Calcoliamo la posizione top-left considerando l'hotspot
        int16 left = (int16)x - (int16)si->cursor.hot_x;
        int16 top = (int16)y - (int16)si->cursor.hot_y;
        
        // Supponendo che il cursore sia 64x64 pixel
        int16 right = left + 64;
        int16 bottom = top + 64;

        // Gestione base dei bordi (evitiamo valori negativi se escono dallo schermo, 
        // o lasciamo che il registro gestisca il clipping se supportato)
        // Per sicurezza clampiamo o passiamo direttamente i valori nei bit corretti:
        
        uint32 tl_val = ((top & 0x7FF) << 16) | (left & 0x07FF);
        uint32 br_val = ((bottom & 0x7FF) << 16) | (right & 0x07FF);

        SM750_WREG32(SM750_DISP_PANEL_ALPHA_PL_TL_POS, tl_val);
        SM750_WREG32(SM750_DISP_PANEL_ALPHA_PL_BR_POS, br_val);
    }
}

void
sm750_show_cursor(bool is_visible)
{
    shared_info *si = gInfo->si;
    vuint32 *regs = gInfo->regs;
    // Cursor Register
    uint32 reg = si->card_info.is_panel ? SM750_DISP_PANEL_CUR_ADDR : SM750_DISP_CRT_CUR_ADDR;
    uint32 ctrl = SM750_REG32(reg);
    if (is_visible)
        ctrl |= (1 << 31); // Enable bit
    else
        ctrl &= ~(1 << 31);
    SM750_WREG32(reg, ctrl);
    // Alpha layer
    if (si->settings.usealphacursor) {
        ctrl = SM750_REG32(SM750_DISP_PANEL_ALPHA_CTRL);
        if (is_visible)
            ctrl |= (1 << 2); // Enable bit
        else
            ctrl &= ~(1 << 2);
        SM750_WREG32(SM750_DISP_PANEL_ALPHA_CTRL, ctrl);
    }
}

status_t
sm750_set_cursor_shape(uint16 width, uint16 height, uint16 hotX, uint16 hotY,
    const uint8* andMask, const uint8* xorMask)
{
	//CALLED();
    if (width > 64 || height > 64)
        return B_BAD_VALUE;

    shared_info *si = gInfo->si;
    vuint32 *regs = gInfo->regs;
    
    //uint8* dest = (uint8*)si->cursor.v_address;
    uint8* dest = (uint8*)gInfo->cursor_virtual_address;

    if (dest == NULL)
        return B_NO_INIT;

    si->cursor.hot_x = hotX;
    si->cursor.hot_y = hotY;

    // 1. Clear: 0x00 is transparent
    memset(dest, 0x00, 1024);

    // 2. BeOS mask traduction -> SM750 2-bit Format
    // Every hardware row is 16 byte (64 pixels * 2 bit)
    uint32 byteWidth = (width + 7) / 8; // Width in byte of the original mask

    for (uint32 y = 0; y < height; y++) {
        for (uint32 x = 0; x < width; x++) {
            // bit extraction of Haiku's masks
            uint32 srcByteIdx = (y * byteWidth) + (x / 8);
            uint8 bitPos = 7 - (x % 8);
            
            bool andBit = (andMask[srcByteIdx] >> bitPos) & 0x01;
            bool xorBit = (xorMask[srcByteIdx] >> bitPos) & 0x01;

            // AND=0, XOR=0 -> Black (10 binary = 2)
            // AND=0, XOR=1 -> White (01 binary = 1)
            // AND=1, XOR=0 -> Transparent (00 binary = 0)
            // AND=1, XOR=1 -> Inversion/Black (for now let's make it black = 2)
            
            uint8 val = 0;
            if (andBit == 0) {
                if (xorBit == 0) val = 2; // Black
                else val = 1;            // White
            } else {
                if (xorBit == 1) val = 2; // Inversion, mapped to black
                else val = 0;            // Transparent
            }

            // writing to the interlaced buffer (16 bytes per row)
            uint32 destByteIdx = (y * 16) + (x / 4);
            uint8 shift = (x % 4) * 2;
            dest[destByteIdx] |= (val << shift);
        }
    }

    uint32 addr_val = (1 << 31) | (si->cursor.vram_offset & 0x03FFFFF0);
    uint32 color12 = 0x0000FFFF;

    if (si->card_info.is_panel) {
        SM750_WREG32(SM750_DISP_PANEL_CUR_COLOR12, color12);
        SM750_WREG32(SM750_DISP_PANEL_CUR_ADDR, addr_val);
    } else {
        SM750_WREG32(SM750_DISP_CRT_CUR_COLOR12, color12);
        SM750_WREG32(SM750_DISP_CRT_CUR_ADDR, addr_val);
    }

    sm750_move_cursor(si->cursor.x, si->cursor.y);

    return B_OK;
}

status_t
sm750_set_cursor_bitmap(uint16 width, uint16 height, uint16 hotX, uint16 hotY,
    color_space colorSpace, uint16 bytesPerRow, const uint8* bitmapData)
{
	//CALLED();
	(void)colorSpace;
	// TODO: if the mouse flickers bit 31 of SM750_DISP_PANEL_CUR_ADDR should be set 0
	//       while changing the bitmap
    shared_info *si = gInfo->si;
    vuint32 *regs = gInfo->regs;
    //uint8* dest = (uint8*)si->cursor.v_address;
    si->cursor.hot_x = hotX;
    si->cursor.hot_y = hotY;
    
    if (si->settings.usealphacursor){
    	if (gInfo->alphacursor_virtual_address == NULL) {
            debug_printf("SM750_ACC: Cursor: indirizzo di memoria cursore alpha non inizializzato");
            return B_NO_INIT;
        }
        uint16* dest = (uint16*)gInfo->alphacursor_virtual_address;
        const uint8* src = (const uint8*)bitmapData;
        
        
        // Calcoliamo la posizione top-left considerando l'hotspot
        int16 left = (int16)si->cursor.x - (int16)si->cursor.hot_x;
        int16 top = (int16)si->cursor.y - (int16)si->cursor.hot_y;
        
        // Supponendo che il cursore sia 64x64 pixel
        int16 right = left + 64;
        int16 bottom = top + 64;

        // Gestione base dei bordi (evitiamo valori negativi se escono dallo schermo, 
        // o lasciamo che il registro gestisca il clipping se supportato)
        // Per sicurezza clampiamo o passiamo direttamente i valori nei bit corretti:
        
        uint32 tl_val = ((top & 0x7FF) << 16) | (left & 0x07FF);
        uint32 br_val = ((bottom & 0x7FF) << 16) | (right & 0x07FF);

        SM750_WREG32(SM750_DISP_PANEL_ALPHA_PL_TL_POS, tl_val);
        SM750_WREG32(SM750_DISP_PANEL_ALPHA_PL_BR_POS, br_val);
        
        // L'offset in VRAM deve essere allineato (i 4 bit inferiori a zero)
        uint32 alpha_addr = si->cursor.alpha_vram_offset & 0x03FFFFF0;
        // Se usiamo la memoria locale, il bit 27 è 0. 
        // Possiamo eventualmente impostare il bit 31 se serve un trigger di flip, 
        // ma per l'inizializzazione statica o l'aggiornamento diretto basta l'indirizzo pulito.
        SM750_WREG32(SM750_DISP_PANEL_ALPHA_FB_ADDR, alpha_addr);
        
        uint32 fb_offset = 8;     // 128 byte / 16 = 8 blocchi
        uint32 window_width = 8;  // Stessa larghezza per la finestra del cursore (64 pixel)
        uint32 reg_val = (window_width << 20) | (fb_offset << 4);
        SM750_WREG32(SM750_DISP_PANEL_ALPHA_FB_OFFSET_WWIDTH, reg_val);
        
        // Pulisci l'area (64x64 pixel a 16-bit = 8192 byte)
        memset(dest, 0, 64 * 64 * 2);
        for (uint32 y = 0; y < height && y < 64; y++) {
            for (uint32 x = 0; x < width && x < 64; x++) {
                const uint8* pixel = src + (y * bytesPerRow) + (x * 4);
                uint8 b = pixel[0] >> 4; // Da 8-bit a 4-bit (0-15)
                uint8 g = pixel[1] >> 4;
                uint8 r = pixel[2] >> 4;
                uint8 a = pixel[3] >> 4;

                // Formato aRGB 4:4:4:4: [A:15-12][R:11-8][G:7-4][B:3-0]
                uint16 val = (a << 12) | (r << 8) | (g << 4) | b;
                dest[y * 64 + x] = val;
            }
        }
        uint32 alpha_ctrl = (1 << 2) | (3 << 0); // Enable = 1, Format = 11 (16-bit aRGB 4:4:4:4)
        SM750_WREG32(SM750_DISP_PANEL_ALPHA_CTRL, alpha_ctrl);
    
    } else {
        //if (si->cursor.v_address == NULL) {
        if (gInfo->cursor_virtual_address == NULL) {
            debug_printf("SM750_ACC: Cursor: indirizzo di memoria non inizializzato");
            return B_NO_INIT;
        }
        
        uint8* dest = (uint8*)gInfo->cursor_virtual_address;
    
        // Cleaning the entire KB
        // 2-bit per pixel format
        // Fill with "10" (Transparent) -> binary: 10101010 = 0xAA
        // with 0xFF Black
        // with 0xAA BLACK
        // with 0x55 White
        // with 0x00 TRANSPARENTE
        memset(dest, 0x00, 1024);  // transparence

        const uint8* src = (const uint8*)bitmapData;

        for (uint32 y = 0; y < height && y < 64; y++) {
            for (uint32 x = 0; x < width && x < 64; x++) {
                const uint8* pixel = src + (y * bytesPerRow) + (x * 4);
                uint8 a = pixel[3]; // Alpha
                uint8 r = pixel[2];
                uint8 g = pixel[1];
                uint8 b = pixel[0];

                uint8 val = 0; // Default: 00 (Transparent)

                /* two colors white and black
                if (a > 128) {
                    // Luminance for select between White and Black
                    uint32 luma = (r + g + b) / 3;
                    if (luma > 128)
                        val = 1; // 01 (Color 1: WHITE)
                    else
                        val = 2; // 10 (Color 2: BLACK)
                }*/
                if (a < 100) {
                    val = 0; // Transparent
                } else if (a < 200) {
                    val = 3; // COLOR 3 (Grey for shadows!)
                } else {
                    // Solid pixel: White or black
                    uint32 luma = (r + g + b) / 3;
                    val = (luma > 128) ? 1 : 2; 
                }

                // Insert the 2 bits
                uint32 byteIdx = (y * 16) + (x / 4); 
                uint8 shift = (x % 4) * 2;
            
                dest[byteIdx] |= (val << shift);
            }
        }
    
    
        // Force colors and address
        uint32 addr_val = (1 << 31) | (si->cursor.vram_offset & 0x03FFFFF0);

        // SM750 Color registers
        // Color 1 (0,0) = White
        // Color 2 (0,1) = Black
        uint32 color12 = 0x0000FFFF; // Black higher bits, White lower bits
        uint32 color3 = 0x00888888; // Grey (R=88, G=88, B=88)

        if (si->card_info.is_panel) {
            SM750_WREG32(SM750_DISP_PANEL_CUR_COLOR12, color12);
            SM750_WREG32(SM750_DISP_PANEL_CUR_COLOR3, color3);
            SM750_WREG32(SM750_DISP_PANEL_CUR_ADDR, addr_val);
        } else {
            SM750_WREG32(SM750_DISP_CRT_CUR_COLOR12, color12);
            SM750_WREG32(SM750_DISP_CRT_CUR_COLOR3, color3);
            SM750_WREG32(SM750_DISP_CRT_CUR_ADDR, addr_val);
        }

    }
    return B_OK;
}


uint32
sm750_get_cursor_bits(void)
{
	if (gInfo == NULL || gInfo->si == NULL)
		return 0;
	
	uint32 cb = 32;
	if (gInfo->si->settings.usealphacursor) {
		debug_printf("SM750 Cursor color bits: 32\n");
		return cb;
	}
	cb = gInfo->si->settings.cursorbits;
	debug_printf("SM750 Cursor color bits: %d\n",cb);
	return cb;
}
