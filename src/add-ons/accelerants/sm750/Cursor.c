/*
 * Copyright 2018, Your Name <your@email.address>
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
        // Bit 11 = 1 (Fuori a sinistra), il valore diventa positivo (distanza dal bordo)
        reg_val |= (1 << 11) | (uint32)((-x_pos) & 0x3F); 
    } else {
        reg_val |= (uint32)(x_pos & 0x07FF);
    }

    if (y_pos < 0) {
        // Bit 27 = 1 (Fuori in alto)
        reg_val |= (1 << 27) | (uint32)(((-y_pos) & 0x3F) << 16);
    } else {
        reg_val |= (uint32)((y_pos & 0x07FF) << 16);
    }

    if (si->card_info.is_panel)
        SM750_WREG32(SM750_DISP_PANEL_CUR_POS, reg_val);
    else
        SM750_WREG32(SM750_DISP_CRT_CUR_POS, reg_val);

    si->cursor.x = x;
    si->cursor.y = y;
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
/* vecchia
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
}*/
status_t
sm750_set_cursor_shape(uint16 width, uint16 height, uint16 hotX, uint16 hotY,
    const uint8* andMask, const uint8* xorMask)
{
    if (width > 64 || height > 64)
        return B_BAD_VALUE;

    shared_info *si = gInfo->si;
    vuint32 *regs = gInfo->regs;
    
    uint8* dest = (uint8*)si->cursor.v_address;

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

            // AND=0, XOR=0 -> Nero (10 binario = 2)
            // AND=0, XOR=1 -> Bianco (01 binario = 1)
            // AND=1, XOR=0 -> Trasparente (00 binario = 0)
            // AND=1, XOR=1 -> Inversione/Nero (per ora facciamo Nero = 2)
            
            uint8 val = 0;
            if (andBit == 0) {
                if (xorBit == 0) val = 2; // Nero
                else val = 1;            // Bianco
            } else {
                if (xorBit == 1) val = 2; // Inversione, mappata a Nero
                else val = 0;            // Trasparente
            }

            // writing to the interlaced buffer (16 bytes per row)
            uint32 destByteIdx = (y * 16) + (x / 4);
            uint8 shift = (x % 4) * 2;
            dest[destByteIdx] |= (val << shift);
        }
    }

    uint32 addr_val = (1 << 31) | ((si->card_info.mem_size - 1024) & 0x03FFFFF0);
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
	// TODO: if the mouse flickers bit 31 of SM750_DISP_PANEL_CUR_ADDR should be set 0
	//       while changing the bitmap
    shared_info *si = gInfo->si;
    vuint32 *regs = gInfo->regs;
    uint8* dest = (uint8*)si->cursor.v_address;
    
    if (si->cursor.v_address == NULL) {
        debug_printf("SM750_ACC: Cursor: indirizzo di memoria non inizializzato");
        return B_NO_INIT;
    }
 
    // Puliamo tutto il KB
    // Formato 2-bit per pixel
    // Proviamo a riempire tutto di "10" (Trasparente) -> in binario 10101010 = 0xAA
    // con 0xFF NERO
    // con 0xAA NERO
    // con 0x55 BIANCO
    // con 0x00 TRASPARENTE
    memset(dest, 0x00, 1024);  // trasparenza 

    const uint8* src = (const uint8*)bitmapData;

    for (uint32 y = 0; y < height && y < 64; y++) {
        for (uint32 x = 0; x < width && x < 64; x++) {
            const uint8* pixel = src + (y * bytesPerRow) + (x * 4);
            uint8 a = pixel[3]; // Alpha
            uint8 r = pixel[2];
            uint8 g = pixel[1];
            uint8 b = pixel[0];

            uint8 val = 0; // Default: 00 (Transparent)

            if (a > 128) {
                // Calcoliamo la luminosità per decidere tra Bianco e Nero
                uint32 luma = (r + g + b) / 3;
                if (luma > 128)
                    val = 1; // 01 (Colore 1: Bianco)
                else
                    val = 2; // 10 (Colore 2: Nero)
            }

            // Inseriamo i 2 bit nel byte corretto
            // Ogni riga hardware è di 16 byte (64 pixel * 2 bit / 8)
            uint32 byteIdx = (y * 16) + (x / 4); 
            uint8 shift = (x % 4) * 2; // Ordine dei bit nel byte (potrebbe servire 6 - ...)
            
            dest[byteIdx] |= (val << shift);
        }
    }
    
    
    // Forza i colori e l'indirizzo
    uint32 addr_val = (1 << 31) | (CRT_CURSOR_VRAM_OFFSET & 0x03FFFFF0);

    // Registri Colore SM750
    // Color 1 (0,0) = Bianco
    // Color 2 (0,1) = Nero
    uint32 color12 = 0x0000FFFF; // Nero nei bit alti, Bianco nei bassi

    if (si->card_info.is_panel) {
        SM750_WREG32(SM750_DISP_PANEL_CUR_COLOR12, color12);
        //SM750_WREG32(SM750_DISP_PANEL_CUR_COLOR3, 0x00000000);
        SM750_WREG32(SM750_DISP_PANEL_CUR_ADDR, addr_val);
    } else {
        SM750_WREG32(SM750_DISP_CRT_CUR_COLOR12, color12);
        //SM750_WREG32(SM750_DISP_CRT_CUR_COLOR3, 0x00000000);
        SM750_WREG32(SM750_DISP_CRT_CUR_ADDR, addr_val);
    }

    return B_OK;
}
